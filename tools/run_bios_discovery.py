#!/usr/bin/env python3

"""Capture compact cold-start discovery or validate/reuse a BIOS checkpoint."""

import argparse
from datetime import datetime, timezone
import filecmp
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

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
                 stdout=output.name, stderr=errors.name,
                 started_at=datetime.now(timezone.utc).isoformat())
    started = time.monotonic()
    print(f"[{name}] started", file=sys.stderr, flush=True)
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
                entry["elapsed_seconds"] = time.monotonic() - started
                write_json(directory / "manifest.json", manifest)
                raise
        else:
            result = subprocess.run(command, cwd=ROOT, stdout=stdout, stderr=stderr,
                                    check=False)
            status = result.returncode
    entry["returncode"] = status
    entry["elapsed_seconds"] = time.monotonic() - started
    entry["output_bytes"] = output.stat().st_size
    if compressed:
        entry["uncompressed_bytes"] = uncompressed_bytes
    write_json(directory / "manifest.json", manifest)
    print(f"[{name}] exit {status}, {entry['elapsed_seconds']:.2f}s",
          file=sys.stderr, flush=True)
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


def failure_signature(failure):
    if failure is None:
        return None
    return {key: value for key, value in failure.items() if key != "event_index"}


def verify_scout_checkpoint(receipt, binary, bios):
    if (receipt["binary"] != file_identity(binary) or
            receipt["bios"] != file_identity(bios) or
            receipt["state"] != file_identity(Path(receipt["path"]))):
        raise ValueError("Scout checkpoint identity changed; start a new cold scout")


def prune_scout_states(directory, manifest, keep):
    reclaimed = 0
    for receipt in manifest["checkpoint_receipts"]:
        path = Path(receipt["path"])
        if path not in keep:
            if file_identity(path) != receipt["state"]:
                raise ValueError("Scout checkpoint changed before cleanup")
            path.unlink()
            receipt["pruned"] = True
            reclaimed += receipt["state"]["bytes"]
    manifest["pruned_state_bytes"] = reclaimed
    write_json(directory / "manifest.json", manifest)


def run_scout_batch(directory, bios, *, master_cycles, checkpoint_every,
                    replay_window):
    ceiling = uint(master_cycles, "Cycle ceiling")
    interval = uint(checkpoint_every, "Checkpoint interval")
    window = uint(replay_window, "Replay window")
    if interval == 0 or window == 0:
        raise ValueError("Checkpoint interval and replay window must be positive")
    if not bios.is_file():
        raise ValueError(f"BIOS input does not exist: {bios}")
    directory.mkdir(parents=True, exist_ok=False)
    manifest = dict(status="running", mode="scout", bios=str(bios),
                    max_master_cycles=ceiling, checkpoint_every=interval,
                    replay_window=window, commands=[], checkpoint_receipts=[],
                    note=EVIDENCE_NOTE)
    write_json(directory / "manifest.json", manifest)
    started = time.monotonic()
    try:
        binary = build_neko(directory, manifest)
        identity = dict(binary=file_identity(binary), bios=file_identity(bios))
        checkpoint = None
        start = 0
        while True:
            target = min(start + interval, ceiling)
            name = f"scout-{target}"
            state = directory / f"{name}.state"
            command = [str(binary), "bios-startup", str(bios),
                       "--cycles", str(target), "--scout",
                       "--save-checkpoint", str(state)]
            if checkpoint is not None:
                verify_scout_checkpoint(checkpoint, binary, bios)
                command += ["--resume-checkpoint", checkpoint["path"]]
            stage_start = time.monotonic()
            status = capture(directory, manifest, name, command,
                             extension="jsonl", accepted=(0, 1))
            log = load_log(directory / f"{name}.jsonl")
            end = uint(log.result["snapshot"]["master_cycle"], "Scout cycle")
            if not start <= end <= target:
                raise ValueError("Scout returned a cycle outside its requested interval")
            failure = log.failure_record["failure"] if log.failure_record else None
            limited = log.result["run"]["cycle_limit_reached"]
            if (limited != (status == 0) or (limited and end != target) or
                    (failure is not None and failure["master_cycle"] != end)):
                raise ValueError("Scout stop receipt disagrees with its failure/budget")
            elapsed = time.monotonic() - stage_start
            progress = dict(master_cycle=end, cycle_ceiling=ceiling,
                            elapsed_seconds=time.monotonic() - started,
                            cycles_per_second=(end - start) / elapsed)
            manifest["progress"] = progress
            print(f"[scout] {end}/{ceiling} cycles, "
                  f"{progress['cycles_per_second']:.0f} cycles/s, "
                  f"{progress['elapsed_seconds']:.1f}s elapsed",
                  file=sys.stderr, flush=True)
            receipt = dict(format="neko-bios-scout-checkpoint-v1",
                           validated=False, validation="scout_origin_only",
                           path=str(state), master_cycle=end,
                           state=file_identity(state), **identity)
            write_json(Path(str(state) + ".json"), receipt)
            manifest["checkpoint_receipts"].append(receipt)
            write_json(directory / "manifest.json", manifest)
            if failure is not None or not limited or end == ceiling:
                capture(directory, manifest, "scout-summary",
                        [sys.executable, "-B", str(INSPECTOR), "summary",
                         str(directory / f"{name}.jsonl")], extension="json")
                break
            checkpoint = receipt
            start = end

        if failure is None:
            manifest["status"] = "ceiling_reached" if limited else "cpu_stop"
            if limited:
                keep = {Path(item["path"])
                        for item in manifest["checkpoint_receipts"][-2:]}
                prune_scout_states(directory, manifest, keep)
            write_json(directory / "manifest.json", manifest)
            return dict(artifacts=str(directory), status=manifest["status"],
                        first_failure=None, note=EVIDENCE_NOTE)

        failure_cycle = uint(failure["master_cycle"], "Failure cycle")
        trace_start = max(start, failure_cycle - min(window, failure_cycle))
        for name in ("replay", "plain"):
            command = [str(binary), "bios-startup", str(bios),
                       "--cycles", str(failure_cycle),
                       "--save-checkpoint", str(directory / f"{name}-final.state")]
            if checkpoint is not None:
                verify_scout_checkpoint(checkpoint, binary, bios)
                command += ["--resume-checkpoint", checkpoint["path"]]
            elif (file_identity(binary) != identity["binary"] or
                  file_identity(bios) != identity["bios"]):
                raise ValueError("Scout input/executable changed before cold replay")
            if name == "replay":
                command += ["--trace-events", str(TRACE_EVENTS), "--failure-context",
                            "--trace-from-cycle", str(trace_start),
                            "--stop-on-first-failure"]
            capture(directory, manifest, name, command, extension="jsonl",
                    accepted=(0, 1), compressed=name == "replay")
        replay_failure = inspect_failure(directory, manifest, directory / "replay.jsonl.gz")
        scout_failure = json.loads((directory / "scout-summary.json").read_text(
            encoding="utf-8"))["first_failure"]
        reproduced = failure_signature(scout_failure) == failure_signature(replay_failure)
        manifest["failure_reproduced"] = reproduced
        comparison = capture(directory, manifest, "compare-replay-plain",
            [sys.executable, "-B", str(INSPECTOR), "compare",
             str(directory / "replay.jsonl.gz"), str(directory / "plain.jsonl"),
             "--mode", "continuation"], extension="json", accepted=(0, 1))
        states = {}
        for name, path in (("scout", state),
                           ("replay", directory / "replay-final.state"),
                           ("plain", directory / "plain-final.state")):
            states[name] = dict(file=path.name, **file_identity(path),
                                equal=filecmp.cmp(state, path, shallow=False))
        manifest["final_states"] = states
        equal = reproduced and comparison == 0 and all(item["equal"] for item in states.values())
        manifest["status"] = "equal" if equal else "mismatch"
        if equal:
            keep = {state}
            if checkpoint is not None:
                keep.add(Path(checkpoint["path"]))
            prune_scout_states(directory, manifest, keep)
            for name in ("replay", "plain"):
                (directory / states[name]["file"]).unlink()
                states[name]["pruned_duplicate"] = True
                manifest["pruned_state_bytes"] += states[name]["bytes"]
        manifest["elapsed_seconds"] = time.monotonic() - started
        write_json(directory / "manifest.json", manifest)
        return dict(artifacts=str(directory), status=manifest["status"],
                    first_failure=replay_failure, failure_reproduced=reproduced,
                    note=EVIDENCE_NOTE)
    except (OSError, ValueError, KeyboardInterrupt) as error:
        manifest.update(status="error",
                        error="interrupted" if isinstance(error, KeyboardInterrupt)
                        else str(error),
                        elapsed_seconds=time.monotonic() - started)
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
    parser.add_argument("--mode", choices=("conformance", "scout"), default="conformance",
                        help="Existing full matrix or staged cold scout and targeted replay.")
    parser.add_argument("--checkpoint-every", type=int,
                        help="Scout checkpoint/progress interval in master cycles (default: 5000000).")
    parser.add_argument("--replay-window", type=int,
                        help="Trace at most this many cycles before the scout failure (default: 10000).")
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
        if arguments.mode == "scout":
            if arguments.checkpoint or arguments.checkpoint_at or arguments.failure_context:
                raise ValueError("Scout mode starts cold and manages its own checkpoints and capture")
            report = run_scout_batch(
                arguments.directory.resolve(), arguments.bios.resolve(),
                master_cycles=arguments.cycles,
                checkpoint_every=(5000000 if arguments.checkpoint_every is None
                                  else arguments.checkpoint_every),
                replay_window=(10000 if arguments.replay_window is None
                               else arguments.replay_window))
            print(json.dumps(report, indent=2, sort_keys=True, allow_nan=False))
            return 0 if report["status"] in ("equal", "ceiling_reached") else 1
        if arguments.checkpoint_every is not None or arguments.replay_window is not None:
            raise ValueError("Checkpoint interval and replay window require --mode scout")
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
