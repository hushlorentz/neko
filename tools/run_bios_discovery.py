#!/usr/bin/env python3

"""Capture compact cold-start discovery or validate/reuse a BIOS checkpoint."""

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import filecmp
import gzip
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from inspect_startup import EVIDENCE_NOTE, load_log, uint
import bios_campaign as campaign
from bios_campaign import file_identity, write_json


ROOT = Path(__file__).resolve().parent.parent
INSPECTOR = Path(__file__).with_name("inspect_startup.py").resolve()
NEKO_NAME = "neko.exe" if os.name == "nt" else "neko"
MASTER_CYCLES = 1000000
TRACE_EVENTS = 3000000
RUN_NAMES = ("traced1", "traced2", "plain1", "plain2")


@contextmanager
def running_capture(directory, manifest, name, command, *, extension="stdout",
                    accepted=(0,), compressed=False, sample_seconds=0):
    command = list(command)
    output = directory / f"{name}.{extension}{'.gz' if compressed else ''}"
    errors = directory / f"{name}.stderr"
    entry = dict(name=name, command=command, returncode=None,
                 accepted_exit_codes=list(accepted),
                 stdout=output.name, stderr=errors.name,
                 started_at=datetime.now(timezone.utc).isoformat())
    if len(command) > 1 and command[1] == "bios-startup":
        entry["progress_file"] = f"{name}.progress.jsonl"
        command += ["--progress-file", str(directory / entry["progress_file"])]
    started = time.monotonic()
    print(f"[{name}] started", file=sys.stderr, flush=True)
    manifest["commands"].append(entry)
    write_json(directory / "manifest.json", manifest)
    with output.open("xb") as stdout, errors.open("xb") as stderr:
        process = subprocess.Popen(command, cwd=ROOT,
                                   stdout=subprocess.PIPE if compressed else stdout,
                                   stderr=stderr)
        try:
            if len(command) > 1 and command[1] == "bios-startup":
                entry["process_pid"] = process.pid
                write_json(directory / "manifest.json", manifest)
            if sample_seconds:
                entry.update(sampling_seconds=sample_seconds, sample_file=f"{name}.sample.txt")
                write_json(directory / "manifest.json", manifest)
                capture(directory, manifest, f"sample-{name}", [
                    "/usr/bin/sample", str(process.pid), str(sample_seconds), "1",
                    "-mayDie", "-file", str(directory / entry["sample_file"]),
                ])
                sample = directory / entry["sample_file"]
                if not sample.is_file() or sample.stat().st_size == 0:
                    raise ValueError(f"Sampler did not retain a report: {name}")
                entry["sample_identity"] = file_identity(sample)
            yield process, stdout, entry
            status = process.wait()
        except BaseException:
            process.kill()
            entry["returncode"] = process.wait()
            entry["elapsed_seconds"] = time.monotonic() - started
            write_json(directory / "manifest.json", manifest)
            raise
        finally:
            if process.stdout is not None:
                process.stdout.close()
    entry["returncode"] = status
    entry["elapsed_seconds"] = time.monotonic() - started
    entry["output_bytes"] = output.stat().st_size
    write_json(directory / "manifest.json", manifest)
    print(f"[{name}] exit {status}, {entry['elapsed_seconds']:.2f}s",
          file=sys.stderr, flush=True)
    if status not in accepted:
        raise ValueError(
            f"{name} exited {status}; see {output} and {errors}")
    if "progress_file" in entry:
        entry["progress"] = campaign.read_progress(directory, entry["progress_file"])
        expected_phase = "runtime_error" if status == 2 else "finished"
        if entry["progress"] is None or entry["progress"]["phase"] != expected_phase:
            raise ValueError(f"{name} did not publish {expected_phase} progress")
        write_json(directory / "manifest.json", manifest)


def capture(directory, manifest, name, command, *, extension="stdout",
            accepted=(0,), compressed=False, sample_seconds=0):
    with running_capture(directory, manifest, name, command, extension=extension,
                         accepted=accepted, compressed=compressed,
                         sample_seconds=sample_seconds) as (process, stdout, entry):
        if compressed:
            with gzip.GzipFile(filename="", mode="wb", fileobj=stdout,
                               compresslevel=6, mtime=0) as zipped:
                uncompressed_bytes = 0
                for chunk in iter(lambda: process.stdout.read(1024 * 1024), b""):
                    zipped.write(chunk)
                    uncompressed_bytes += len(chunk)
            entry["uncompressed_bytes"] = uncompressed_bytes
    return entry["returncode"]


def report_path(directory, name):
    compressed = name.startswith("traced") or name in ("cold", "resumed")
    return directory / f"{name}.jsonl{'.gz' if compressed else ''}"


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


def capture_runtime_error(directory, manifest, binary, bios, checkpoint, report, log, window):
    cycle = uint(log.result["snapshot"]["master_cycle"], "Runtime error cycle")
    start = checkpoint["master_cycle"] if checkpoint is not None else 0
    trace_start = max(start, cycle - min(window, cycle))
    ceiling = min(log.input["max_master_cycles"], cycle + 1)
    manifest["runtime_error"] = dict(master_cycle=cycle, message=log.result["message"])
    for name in ("runtime-replay", "runtime-plain"):
        command = [str(binary), "bios-startup", str(bios), "--cycles", str(ceiling)]
        if checkpoint is not None:
            verify_scout_checkpoint(checkpoint, binary, bios)
            command += ["--resume-checkpoint", checkpoint["path"]]
        elif (file_identity(binary) != manifest["binary_identity"] or
              file_identity(bios) != manifest["bios_identity"]):
            raise ValueError("Runtime replay input/executable changed")
        if name == "runtime-replay":
            command += ["--trace-events", str(TRACE_EVENTS), "--failure-context",
                        "--trace-from-cycle", str(trace_start)]
        capture(directory, manifest, name, command, extension="jsonl",
                accepted=(0, 1, 2), compressed=name == "runtime-replay")
    replay = directory / "runtime-replay.jsonl.gz"
    plain = directory / "runtime-plain.jsonl"
    manifest["runtime_error_reproduced"] = all(
        load_log(path).result == log.result for path in (replay, plain))
    for name, path in (("scout-summary", report),
                       ("runtime-error-summary", replay)):
        capture(directory, manifest, name,
                [sys.executable, "-B", str(INSPECTOR), "summary", str(path)],
                extension="json")
    capture(directory, manifest, "runtime-error-events",
            [sys.executable, "-B", str(INSPECTOR), "events", str(replay),
             "--subsystem", "gif", "--from-cycle", str(trace_start),
             "--to-cycle", str(ceiling), "--limit", "200"], extension="json")
    comparison = capture(directory, manifest, "compare-runtime-replay-plain",
        [sys.executable, "-B", str(INSPECTOR), "compare", str(replay), str(plain),
         "--mode", "continuation"], extension="json", accepted=(0, 1))
    manifest["runtime_error_comparisons_equal"] = comparison == 0
    write_json(directory / "manifest.json", manifest)


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


def compare_final_states(directory, names):
    baseline = directory / f"{names[0]}-final.state"
    states = {}
    for name in names:
        path = directory / f"{name}-final.state"
        states[name] = dict(file=path.name, **file_identity(path),
                            equal=filecmp.cmp(baseline, path, shallow=False))
    return states


def prune_duplicate_final_states(directory, manifest):
    for state in list(manifest["final_states"].values())[1:]:
        (directory / state["file"]).unlink()
        state["pruned_duplicate"] = True
        manifest["pruned_state_bytes"] = manifest.get("pruned_state_bytes", 0) + state["bytes"]


def validate_checkpoint(directory, bios, binary, manifest, cycle, ceiling):
    checkpoint = directory / "checkpoint.state"
    base = [str(binary), "bios-startup", str(bios)]
    with running_capture(directory, manifest, "checkpoint",
            base + ["--cycles", str(cycle), "--save-checkpoint", str(checkpoint)],
            extension="jsonl") as (_, _, checkpoint_entry):
        exits = dict(cold=capture(directory, manifest, "cold",
            base + ["--cycles", str(ceiling), "--trace-events", str(TRACE_EVENTS),
                    "--failure-context", "--trace-from-cycle", str(cycle),
                    "--save-checkpoint", str(directory / "cold-final.state")],
            extension="jsonl", accepted=(0, 1), compressed=True))
    exits["checkpoint"] = checkpoint_entry["returncode"]
    created = load_log(directory / "checkpoint.jsonl")
    if (created.result["snapshot"]["master_cycle"] != cycle or
            not created.result["run"]["cycle_limit_reached"]):
        raise ValueError("Checkpoint creation did not reach its selected cycle")
    for name in ("resumed", "resumed-plain"):
        command = base + ["--cycles", str(ceiling),
                          "--resume-checkpoint", str(checkpoint)]
        traced = name != "resumed-plain"
        if traced:
            command += ["--trace-events", str(TRACE_EVENTS), "--failure-context"]
        command += ["--save-checkpoint", str(directory / f"{name}-final.state")]
        exits[name] = capture(directory, manifest, name, command,
            extension="jsonl", accepted=(0, 1), compressed=traced)
    failure = inspect_failure(directory, manifest, report_path(directory, "resumed"))
    equal = compare_reports(directory, manifest, (
        ("compare-cold-resumed", "cold", "resumed", "continuation-trace"),
        ("compare-resumed-plain", "resumed", "resumed-plain", "continuation"),
    ))
    states = compare_final_states(directory, ("cold", "resumed", "resumed-plain"))
    equal = equal and all(state["equal"] for state in states.values())
    manifest["final_states"] = states
    metadata = dict(format="neko-bios-checkpoint-v1", validated=equal,
                    master_cycle=cycle, state=file_identity(checkpoint),
                    bios=file_identity(bios), binary=file_identity(binary),
                    validation_manifest=str(directory / "manifest.json"))
    if equal:
        write_json(Path(str(checkpoint) + ".json"), metadata)
        prune_duplicate_final_states(directory, manifest)
    return exits, equal, failure, str(checkpoint)


def run_batch(directory, bios, *, master_cycles=MASTER_CYCLES, failure_context=False,
              checkpoint=None, checkpoint_at=None, invocation=None, sample_seconds=0):
    master_cycles = uint(master_cycles, "Cycle ceiling")
    if not bios.is_file():
        raise ValueError(f"BIOS input does not exist: {bios}")
    if checkpoint is not None and checkpoint_at is not None:
        raise ValueError("Select checkpoint reuse or checkpoint creation, not both")
    if type(sample_seconds) is not int or not 0 <= sample_seconds <= 60:
        raise ValueError("Sample duration must be between 0 and 60 seconds")
    if sample_seconds and (sys.platform != "darwin" or checkpoint_at is not None):
        raise ValueError("Sampling requires macOS and the sequential conformance matrix")
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
    write_json(directory / "manifest.json", manifest)
    owner = campaign.Owner(directory, invocation)
    try:
        binary = build_neko(directory, manifest)
        manifest.update(binary_identity=file_identity(binary), bios_identity=file_identity(bios))
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
            command += ["--save-checkpoint", str(directory / f"{name}-final.state")]
            exits[name] = capture(directory, manifest, name, command,
                                  extension="jsonl", accepted=(0, 1),
                                  compressed=name.startswith("traced"),
                                  sample_seconds=sample_seconds if name == "plain1" else 0)
        equal, failure = inspect_batch(directory, manifest)
        states = compare_final_states(directory, RUN_NAMES)
        manifest["final_states"] = states
        equal = equal and all(state["equal"] for state in states.values())
        if equal:
            prune_duplicate_final_states(directory, manifest)
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
    finally:
        owner.finish(manifest, sys.exc_info()[1])


def failure_signature(failure):
    if failure is None:
        return None
    return {key: value for key, value in failure.items() if key != "event_index"}


def verify_scout_checkpoint(receipt, binary, bios):
    if (receipt["binary"] != file_identity(binary) or
            receipt["bios"] != file_identity(bios) or
            receipt["state"] != file_identity(Path(receipt["path"]))):
        raise ValueError("Scout checkpoint identity changed; start a new cold scout")


def prune_scout_states(directory, manifest, keep=None):
    if keep is None:
        keep = {Path(item["path"])
                for item in manifest["checkpoint_receipts"][-2:]}
    manifest.setdefault("pruned_state_bytes", 0)
    for receipt in manifest["checkpoint_receipts"]:
        path = Path(receipt["path"])
        if not receipt.get("pruned") and not receipt.get("pinned") and path not in keep:
            if campaign.artifact(directory, path.name).resolve() != path.resolve():
                raise ValueError("Scout cleanup must stay inside its campaign")
            if file_identity(path) != receipt["state"]:
                raise ValueError("Scout checkpoint changed before cleanup")
            receipt["prune_pending"] = True
            write_json(directory / "manifest.json", manifest)
            path.unlink()
            receipt.pop("prune_pending")
            receipt["pruned"] = True
            manifest["pruned_state_bytes"] += receipt["state"]["bytes"]
            write_json(directory / "manifest.json", manifest)
    write_json(directory / "manifest.json", manifest)


def run_scout_batch(directory, bios, *, master_cycles, checkpoint_every,
                    replay_window, invocation=None, pinned_checkpoint_cycles=()):
    ceiling = uint(master_cycles, "Cycle ceiling")
    interval = uint(checkpoint_every, "Checkpoint interval")
    window = uint(replay_window, "Replay window")
    if interval == 0 or window == 0:
        raise ValueError("Checkpoint interval and replay window must be positive")
    pins = sorted({uint(cycle, "Pinned checkpoint cycle")
                   for cycle in pinned_checkpoint_cycles})
    if any(cycle == 0 or cycle > ceiling or
           (cycle % interval != 0 and cycle != ceiling) for cycle in pins):
        raise ValueError("Pinned cycles must be positive scheduled scout checkpoints "
                         "at or below the ceiling")
    if not bios.is_file():
        raise ValueError(f"BIOS input does not exist: {bios}")
    directory.mkdir(parents=True, exist_ok=False)
    manifest = dict(status="running", mode="scout", bios=str(bios),
                    max_master_cycles=ceiling, checkpoint_every=interval,
                    replay_window=window, commands=[], checkpoint_receipts=[],
                    pinned_checkpoint_cycles=pins,
                    note=EVIDENCE_NOTE)
    write_json(directory / "manifest.json", manifest)
    started = time.monotonic()
    owner = campaign.Owner(directory, invocation)
    try:
        binary = build_neko(directory, manifest)
        identity = dict(binary=file_identity(binary), bios=file_identity(bios))
        manifest.update(binary_identity=identity["binary"], bios_identity=identity["bios"])
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
                             extension="jsonl", accepted=(0, 1, 2))
            log = load_log(directory / f"{name}.jsonl")
            end = uint(log.result["snapshot"]["master_cycle"], "Scout cycle")
            if not start <= end <= target:
                raise ValueError("Scout returned a cycle outside its requested interval")
            if status == 2:
                if log.result["type"] != "startup_runtime_error":
                    raise ValueError("Scout exit 2 did not retain a runtime error")
                capture_runtime_error(
                    directory, manifest, binary, bios, checkpoint,
                    directory / f"{name}.jsonl", log, window)
                raise ValueError(
                    f"{name} runtime error at {end}: {log.result['message']}; "
                    "bounded diagnostic replay retained, not verified")
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
                           state=file_identity(state), pinned=end in pins, **identity)
            write_json(Path(str(state) + ".json"), receipt)
            manifest["checkpoint_receipts"].append(receipt)
            write_json(directory / "manifest.json", manifest)
            if failure is not None or not limited or end == ceiling:
                capture(directory, manifest, "scout-summary",
                        [sys.executable, "-B", str(INSPECTOR), "summary",
                         str(directory / f"{name}.jsonl")], extension="json")
                break
            prune_scout_states(directory, manifest)
            checkpoint = receipt
            start = end

        if failure is None:
            manifest["status"] = "ceiling_reached" if limited else "cpu_stop"
            if limited:
                prune_scout_states(directory, manifest)
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
    finally:
        owner.finish(manifest, sys.exc_info()[1])


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if "--status" in argv or "--finalize" in argv:
        return campaign.main(argv)
    invocation = [sys.executable, "-B", str(Path(__file__).resolve()), *argv]
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog="Inspect without execution: DIRECTORY --status; finalize inspected "
               "evidence: DIRECTORY --finalize --outer-exit 0.")
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
    parser.add_argument("--pin-checkpoint-at", type=int, action="append",
                        help="Retain a scheduled scout checkpoint at this absolute cycle; repeatable.")
    parser.add_argument("--failure-context", action="store_true",
                        help="Retain bounded first-failure context in traced runs.")
    parser.add_argument("--sample-seconds", type=int, default=0,
                        help="macOS only: sample the owned plain1 native PID for 1-60 seconds; "
                             "use a separate profiling batch, not an unprofiled timing control.")
    checkpoints = parser.add_mutually_exclusive_group()
    checkpoints.add_argument("--checkpoint", type=Path,
                             help="Reuse a validated checkpoint from this executable.")
    checkpoints.add_argument("--checkpoint-at", type=int,
                             help="Create and cold/restore-validate a checkpoint at "
                                  "this absolute cycle (four BIOS runs total).")
    arguments = parser.parse_args(argv)
    try:
        if arguments.mode == "scout":
            if (arguments.checkpoint or arguments.checkpoint_at or arguments.failure_context or
                    arguments.sample_seconds):
                raise ValueError("Scout mode starts cold and manages its own checkpoints and capture")
            report = run_scout_batch(
                arguments.directory.resolve(), arguments.bios.resolve(),
                master_cycles=arguments.cycles,
                checkpoint_every=(5000000 if arguments.checkpoint_every is None
                                  else arguments.checkpoint_every),
                replay_window=(10000 if arguments.replay_window is None
                               else arguments.replay_window),
                pinned_checkpoint_cycles=arguments.pin_checkpoint_at or (),
                invocation=invocation)
            print(json.dumps(report, indent=2, sort_keys=True, allow_nan=False))
            return 0 if report["status"] in ("equal", "ceiling_reached") else 1
        if (arguments.checkpoint_every is not None or arguments.replay_window is not None or
                arguments.pin_checkpoint_at is not None):
            raise ValueError("Checkpoint interval, replay window and pins require --mode scout")
        report = run_batch(arguments.directory.resolve(), arguments.bios.resolve(),
                           master_cycles=arguments.cycles,
                           failure_context=arguments.failure_context,
                           checkpoint=(arguments.checkpoint.resolve()
                                       if arguments.checkpoint is not None else None),
                           checkpoint_at=arguments.checkpoint_at,
                           invocation=invocation, sample_seconds=arguments.sample_seconds)
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
