#!/usr/bin/env python3

"""Capture compact cold-start discovery or validate/reuse a BIOS checkpoint."""

import argparse
import filecmp
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from inspect_startup import EVIDENCE_NOTE, load_log, uint


ROOT = Path(__file__).resolve().parent.parent
INSPECTOR = Path(__file__).with_name("inspect_startup.py").resolve()
NEKO_NAME = "neko.exe" if os.name == "nt" else "neko"
MASTER_CYCLES = 1000000
TRACE_EVENTS = 3000000
RUN_NAMES = ("traced1", "traced2", "plain1", "plain2")


def write_json(path, report):
    path.write_text(json.dumps(report, indent=2, sort_keys=True,
                               allow_nan=False) + "\n", encoding="utf-8")


def capture(directory, manifest, name, command, *, extension="stdout",
            accepted=(0,), compressed=False):
    output = directory / f"{name}.{extension}{'.gz' if compressed else ''}"
    errors = directory / f"{name}.stderr"
    entry = dict(name=name, command=command, returncode=None,
                 stdout=output.name, stderr=errors.name)
    manifest["commands"].append(entry)
    write_json(directory / "manifest.json", manifest)
    with output.open("xb") as stdout, errors.open("xb") as stderr:
        if compressed:
            process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE,
                                       stderr=stderr)
            try:
                with process.stdout as pipe:
                    with gzip.GzipFile(filename="", mode="wb", fileobj=stdout,
                                       compresslevel=6, mtime=0) as zipped:
                        uncompressed_bytes = 0
                        for chunk in iter(lambda: pipe.read(1024 * 1024), b""):
                            zipped.write(chunk)
                            uncompressed_bytes += len(chunk)
                status = process.wait()
            except BaseException:
                process.kill()
                entry["returncode"] = process.wait()
                write_json(directory / "manifest.json", manifest)
                raise
        else:
            result = subprocess.run(command, cwd=ROOT, stdout=stdout, stderr=stderr,
                                    check=False)
            status = result.returncode
    entry["returncode"] = status
    entry["output_bytes"] = output.stat().st_size
    if compressed:
        entry["uncompressed_bytes"] = uncompressed_bytes
    write_json(directory / "manifest.json", manifest)
    if status not in accepted:
        raise ValueError(
            f"{name} exited {status}; see {output} and {errors}")
    return status


def report_path(directory, name):
    compressed = name.startswith("traced") or name in ("cold", "resumed")
    return directory / f"{name}.jsonl{'.gz' if compressed else ''}"


def file_identity(path):
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
            size += len(chunk)
    return dict(sha256=digest.hexdigest(), bytes=size)


def read_checkpoint(path, bios, ceiling):
    metadata = json.loads(Path(str(path) + ".json").read_text(encoding="utf-8"))
    if (type(metadata) is not dict or
            metadata.get("format") != "neko-bios-checkpoint-v1" or
            metadata.get("validated") is not True):
        raise ValueError("Checkpoint requires a successful validation receipt")
    cycle = uint(metadata.get("master_cycle"), "Checkpoint master cycle")
    if cycle > ceiling:
        raise ValueError("Checkpoint exceeds the absolute cycle ceiling")
    if metadata.get("state") != file_identity(path):
        raise ValueError("Checkpoint state differs from its validation receipt")
    if metadata.get("bios") != file_identity(bios):
        raise ValueError("Checkpoint BIOS differs from its validation receipt")
    return metadata


def build_neko(directory, manifest):
    build = ROOT / "out" / "check"
    if not (build / "CMakeCache.txt").is_file():
        capture(directory, manifest, "configure", [
            "cmake", "-S", str(ROOT), "-B", str(build),
            "-DCMAKE_BUILD_TYPE=Debug", "-DNEKO_OPTIMIZE_CHECKS=ON",
        ])
    capture(directory, manifest, "build", [
        "cmake", "--build", str(build), "--target", "neko",
        "--config", "Debug", "-j", "4",
    ])
    for candidate in (build / NEKO_NAME, build / "Debug" / NEKO_NAME):
        if candidate.is_file():
            return candidate
    raise ValueError(f"built {NEKO_NAME} was not found under {build}")


def inspect_failure(directory, manifest, traced):
    inspector = [sys.executable, "-B", str(INSPECTOR)]
    traced = str(traced)
    capture(directory, manifest, "summary", inspector + ["summary", traced],
            extension="json")
    summary = json.loads((directory / "summary.json").read_text(encoding="utf-8"))
    failure = summary["first_failure"]
    if failure is not None:
        cycle = uint(failure["master_cycle"], "first failure master_cycle")
        cpu = failure.get("cpu")
        if cpu not in ("ee", "iop", None):
            raise ValueError(f"unsupported first failure CPU: {cpu}")
        filters = [] if cpu is None else ["--cpu", cpu]
        if summary.get("failure_event_source") == "failure_capture":
            filters += ["--source", "failure_capture"]
        filters += ["--from-cycle", str(max(0, cycle - 16)),
                    "--to-cycle", str(min((1 << 64) - 1, cycle + 16)),
                    "--limit", "200"]
        for command in ("instructions", "events"):
            capture(directory, manifest, f"first-failure-{command}",
                    inspector + [command, traced, *filters], extension="json")
    return failure


def compare_reports(directory, manifest, comparisons):
    equal = True
    for name, left, right, mode in comparisons:
        status = capture(directory, manifest, name, [
            sys.executable, "-B", str(INSPECTOR), "compare",
            str(report_path(directory, left)), str(report_path(directory, right)),
            "--mode", mode,
        ], extension="json", accepted=(0, 1))
        equal = equal and status == 0
    return equal


def inspect_batch(directory, manifest):
    failure = inspect_failure(directory, manifest, report_path(directory, "traced1"))
    equal = compare_reports(directory, manifest, (
        ("compare-traces", "traced1", "traced2", "trace"),
        ("compare-traced-plain", "traced1", "plain1", "run"),
        ("compare-plain", "plain1", "plain2", "run"),
    ))
    return equal, failure


def validate_checkpoint(directory, bios, binary, manifest, cycle, ceiling):
    checkpoint = directory / "checkpoint.state"
    base = [str(binary), "bios-startup", str(bios)]
    exits = dict(checkpoint=capture(directory, manifest, "checkpoint",
        base + ["--cycles", str(cycle), "--save-checkpoint", str(checkpoint)],
        extension="jsonl"))
    created = load_log(directory / "checkpoint.jsonl")
    if (created.result["snapshot"]["master_cycle"] != cycle or
            not created.result["run"]["cycle_limit_reached"]):
        raise ValueError("Checkpoint creation did not reach its selected cycle")
    for name in ("cold", "resumed", "resumed-plain"):
        command = base + ["--cycles", str(ceiling)]
        if name != "cold":
            command += ["--resume-checkpoint", str(checkpoint)]
        traced = name != "resumed-plain"
        if traced:
            command += ["--trace-events", str(TRACE_EVENTS), "--failure-context"]
            if name == "cold":
                command += ["--trace-from-cycle", str(cycle)]
        command += ["--save-checkpoint", str(directory / f"{name}-final.state")]
        exits[name] = capture(directory, manifest, name, command,
            extension="jsonl", accepted=(0, 1), compressed=traced)
    failure = inspect_failure(directory, manifest, report_path(directory, "resumed"))
    equal = compare_reports(directory, manifest, (
        ("compare-cold-resumed", "cold", "resumed", "continuation-trace"),
        ("compare-resumed-plain", "resumed", "resumed-plain", "continuation"),
    ))
    states = {}
    baseline = directory / "cold-final.state"
    for name in ("cold", "resumed", "resumed-plain"):
        path = directory / f"{name}-final.state"
        same = filecmp.cmp(baseline, path, shallow=False)
        states[name] = dict(file=path.name, **file_identity(path), equal=same)
        equal = equal and same
    manifest["final_states"] = states
    metadata = dict(format="neko-bios-checkpoint-v1", validated=equal,
                    master_cycle=cycle, state=file_identity(checkpoint),
                    bios=file_identity(bios), binary=file_identity(binary),
                    validation_manifest=str(directory / "manifest.json"))
    if equal:
        write_json(Path(str(checkpoint) + ".json"), metadata)
        # Only duplicate files generated by this validation are eligible.
        for name in ("resumed", "resumed-plain"):
            (directory / states[name]["file"]).unlink()
            states[name]["pruned_duplicate"] = True
    return exits, equal, failure, str(checkpoint)


def run_batch(directory, bios, *, master_cycles=MASTER_CYCLES, failure_context=False,
              checkpoint=None, checkpoint_at=None):
    master_cycles = uint(master_cycles, "Cycle ceiling")
    if not bios.is_file():
        raise ValueError(f"BIOS input does not exist: {bios}")
    if checkpoint is not None and checkpoint_at is not None:
        raise ValueError("Select checkpoint reuse or checkpoint creation, not both")
    metadata = (read_checkpoint(checkpoint, bios, master_cycles)
                if checkpoint is not None else None)
    if checkpoint_at is not None:
        checkpoint_at = uint(checkpoint_at, "Checkpoint cycle")
        if checkpoint_at > master_cycles:
            raise ValueError("Checkpoint exceeds the absolute cycle ceiling")
    directory.mkdir(parents=True, exist_ok=False)
    manifest = dict(status="running", bios=str(bios),
                    max_master_cycles=master_cycles, trace_events=TRACE_EVENTS,
                    trace_storage="streamed_gzip", commands=[], note=EVIDENCE_NOTE)
    if metadata is not None:
        manifest["checkpoint"] = dict(path=str(checkpoint), receipt=metadata)
    if checkpoint_at is not None:
        manifest["checkpoint_at"] = checkpoint_at
    if failure_context or checkpoint_at is not None:
        manifest["failure_context"] = True
    try:
        binary = build_neko(directory, manifest)
        if metadata is not None and metadata.get("binary") != file_identity(binary):
            raise ValueError("Checkpoint executable changed; regenerate and validate "
                             "with --checkpoint-at")
        if checkpoint_at is not None:
            exits, equal, failure, checkpoint_path = validate_checkpoint(
                directory, bios, binary, manifest, checkpoint_at, master_cycles)
            manifest["status"] = "equal" if equal else "mismatch"
            write_json(directory / "manifest.json", manifest)
            return dict(artifacts=str(directory), bios_exit_codes=exits,
                        comparisons_equal=equal, first_failure=failure,
                        checkpoint=checkpoint_path, note=EVIDENCE_NOTE)
        exits = {}
        for name in RUN_NAMES:
            command = [str(binary), "bios-startup", str(bios),
                       "--cycles", str(master_cycles)]
            if checkpoint is not None:
                command += ["--resume-checkpoint", str(checkpoint)]
            if name.startswith("traced"):
                command += ["--trace-events", str(TRACE_EVENTS)]
                if failure_context:
                    command += ["--failure-context"]
            exits[name] = capture(directory, manifest, name, command,
                                  extension="jsonl", accepted=(0, 1),
                                  compressed=name.startswith("traced"))
        equal, failure = inspect_batch(directory, manifest)
        manifest["status"] = "equal" if equal else "mismatch"
        write_json(directory / "manifest.json", manifest)
        return dict(artifacts=str(directory), bios_exit_codes=exits,
                    comparisons_equal=equal, first_failure=failure,
                    note=EVIDENCE_NOTE)
    except (OSError, ValueError, KeyboardInterrupt) as error:
        manifest.update(status="error",
                        error="interrupted" if isinstance(error, KeyboardInterrupt)
                        else str(error))
        write_json(directory / "manifest.json", manifest)
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path,
                        help="New artifact directory; existing paths are refused.")
    parser.add_argument("--bios", type=Path,
                        default=ROOT / "local_integration/bios/SCPH-70012.bin",
                        help="BIOS input (default: local_integration/bios/SCPH-70012.bin).")
    parser.add_argument("--cycles", type=int, default=MASTER_CYCLES,
                        help="Unsigned 64-bit master-cycle ceiling (default: 1000000).")
    parser.add_argument("--failure-context", action="store_true",
                        help="Retain bounded first-failure context in traced runs.")
    checkpoints = parser.add_mutually_exclusive_group()
    checkpoints.add_argument("--checkpoint", type=Path,
                             help="Reuse a validated checkpoint from this executable.")
    checkpoints.add_argument("--checkpoint-at", type=int,
                             help="Create and cold/restore-validate a checkpoint at "
                                  "this absolute cycle (four BIOS runs total).")
    arguments = parser.parse_args(argv)
    try:
        report = run_batch(arguments.directory.resolve(), arguments.bios.resolve(),
                           master_cycles=arguments.cycles,
                           failure_context=arguments.failure_context,
                           checkpoint=(arguments.checkpoint.resolve()
                                       if arguments.checkpoint is not None else None),
                           checkpoint_at=arguments.checkpoint_at)
        print(json.dumps(report, indent=2, sort_keys=True, allow_nan=False))
        return 0 if report["comparisons_equal"] else 1
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("error: discovery interrupted; partial artifacts were retained",
              file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
