#!/usr/bin/env python3

"""Report existing campaign timings without executing or certifying BIOS work."""

import argparse
from datetime import datetime
import json
import math
from pathlib import Path
import statistics
import sys

import bios_campaign as campaign
from inspect_startup import uint


NOTE = (
    "Read-only timing analysis, not campaign verification or a boot verdict. "
    "Execution windows include coarse progress writes and startup snapshots, "
    "not just the core loop. External overhead combines load/restore and parent "
    "reaping delay; their individual costs are unavailable. Host/build/revision "
    "context is supplied by the caller and is not inferred from historical receipts. "
    "Overlapping stage windows or unknown concurrency are not sequential baselines."
)


def identity(value, label):
    if (type(value) is not dict or type(value.get("sha256")) is not str or
            len(value["sha256"]) != 64 or
            any(char not in "0123456789abcdef" for char in value["sha256"]) or
            type(value.get("bytes")) is not int or value["bytes"] <= 0):
        raise ValueError(f"Invalid {label} identity")
    return {key: value[key] for key in ("sha256", "bytes")}


def seconds(value, label):
    try:
        valid = type(value) in (int, float) and math.isfinite(value) and value >= 0
    except OverflowError:
        valid = False
    if not valid:
        raise ValueError(f"Invalid {label}")
    return value


def options(command):
    values = {}
    flags = {"--scout", "--failure-context", "--stop-on-first-failure"}
    paired = {"--cycles", "--trace-events", "--trace-from-cycle",
              "--resume-checkpoint", "--save-checkpoint",
              "--progress-file", "--progress-every"}
    index = 3
    while index < len(command):
        option = command[index]
        if option in values or option not in flags | paired:
            raise ValueError(f"Invalid native timing command option: {option}")
        if option in flags:
            values[option] = True
            index += 1
        else:
            if index + 1 == len(command):
                raise ValueError(f"Missing native timing command value: {option}")
            values[option] = command[index + 1]
            index += 2
    if "--cycles" not in values:
        raise ValueError("Missing native cycle ceiling")
    for key in ("--cycles", "--trace-events", "--trace-from-cycle", "--progress-every"):
        if key in values:
            value = values[key]
            if not value.isascii() or not value.isdecimal():
                raise ValueError(f"Invalid native timing integer: {key}")
            values[key] = uint(int(value), key)
    if values.get("--progress-every", 1000000) == 0:
        raise ValueError("Invalid progress interval")
    return values


def samples(directory, entry):
    if "progress_file" not in entry:
        raise ValueError(f"Missing native progress: {entry.get('name')}")
    path = campaign.artifact(directory, entry["progress_file"])
    rows = []
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            if not line.endswith("\n"):
                raise ValueError(f"Incomplete progress line: {path.name}")
            row = campaign.validate_progress_sample(json.loads(line), path.name)
            if rows:
                previous = rows[-1]
                if any(row[key] < previous[key] for key in (
                        "master_cycle", "elapsed_wall_seconds", "elapsed_cpu_seconds")):
                    raise ValueError(f"Nonmonotonic progress: {path.name}")
                if any(row[key] != previous[key] for key in (
                        "start_master_cycle", "cycle_ceiling")):
                    raise ValueError(f"Changed progress bounds: {path.name}")
            rows.append(row)
    if not rows or rows[-1] != entry.get("progress"):
        raise ValueError(f"Terminal progress differs from receipt: {path.name}")
    phases = [row["phase"] for row in rows]
    first_report = next((index for index, phase in enumerate(phases) if phase == "report"), None)
    if (first_report is None or first_report == 0 or
            any(phase != "execution" for phase in phases[:first_report]) or
            phases[first_report:] not in (["report", "finished"],
                                         ["report", "checkpoint", "finished"]) or
            rows[0]["master_cycle"] != rows[0]["start_master_cycle"] or
            any(row["master_cycle"] != rows[first_report - 1]["master_cycle"]
                for row in rows[first_report:])):
        raise ValueError(f"Incomplete or inconsistent progress phases: {path.name}")
    return rows, first_report


def checkpoint_identity(directory, manifest, start):
    if "checkpoint" in manifest:
        checkpoint = manifest["checkpoint"]
        if type(checkpoint) is not dict:
            raise ValueError("Invalid checkpoint timing receipt")
        receipt = checkpoint.get("receipt")
    elif "checkpoint_at" in manifest:
        receipt = campaign.read_json(directory / "checkpoint.state.json")
    else:
        receipts = manifest.get("checkpoint_receipts", [])
        if type(receipts) is not list or any(type(item) is not dict for item in receipts):
            raise ValueError("Invalid scout checkpoint timing receipts")
        receipt = next((item for item in receipts if item.get("master_cycle") == start), None)
    if type(receipt) is not dict or receipt.get("master_cycle") != start:
        raise ValueError("Missing same-build checkpoint timing provenance")
    if (receipt.get("binary") != manifest["binary_identity"] or
            receipt.get("bios") != manifest["bios_identity"]):
        raise ValueError("Checkpoint timing input/executable provenance differs")
    return identity(receipt.get("state"), "checkpoint")


def stage(directory, manifest, entry):
    command = entry.get("command")
    if (type(command) is not list or len(command) < 3 or
            any(type(part) is not str for part in command) or command[1] != "bios-startup"):
        raise ValueError("Invalid native command receipt")
    if type(entry.get("returncode")) is not int or entry["returncode"] not in (0, 1):
        raise ValueError(f"Incomplete or unsuccessful native stage: {entry.get('name')}")
    selected = options(command)
    rows, report_index = samples(directory, entry)
    initial, reported, finished = rows[0], rows[report_index], rows[-1]
    start, end = initial["start_master_cycle"], finished["master_cycle"]
    if selected["--cycles"] != initial["cycle_ceiling"]:
        raise ValueError("Command/progress cycle ceilings differ")
    if entry["returncode"] == 0 and end != selected["--cycles"]:
        raise ValueError("Successful native timing did not reach its ceiling")
    resumed = "--resume-checkpoint" in selected
    if not resumed and start != 0:
        raise ValueError("Cold timing command has a restored start cycle")
    traced = "--trace-events" in selected
    if (("--scout" in selected and traced) or
            ("--trace-from-cycle" in selected and not traced) or
            traced and not start <= selected.get("--trace-from-cycle", start) <= selected["--cycles"]):
        raise ValueError("Invalid observation timing bounds")
    workload = dict(
        binary=identity(manifest.get("binary_identity"), "executable"),
        bios=identity(manifest.get("bios_identity"), "BIOS"),
        start_master_cycle=start, end_master_cycle=end,
        cycle_ceiling=selected["--cycles"], returncode=entry["returncode"],
        start="checkpoint" if resumed else "cold",
        observation="failure_probe" if "--scout" in selected else "traced" if traced else "untraced",
        trace_events=selected.get("--trace-events"),
        trace_from_cycle=selected.get("--trace-from-cycle", start) if traced else None,
        failure_context=selected.get("--failure-context", False),
        stop_on_first_failure=selected.get("--stop-on-first-failure", False),
        progress_every=selected.get("--progress-every", 1000000),
        checkpoint=checkpoint_identity(directory, manifest, start) if resumed else None)
    wall = reported["elapsed_wall_seconds"] - initial["elapsed_wall_seconds"]
    cpu = reported["elapsed_cpu_seconds"] - initial["elapsed_cpu_seconds"]
    native_wall = finished["elapsed_wall_seconds"] - initial["elapsed_wall_seconds"]
    process_wall = seconds(entry.get("elapsed_seconds"), "parent stage elapsed seconds")
    external = process_wall - native_wall
    if external < 0:
        raise ValueError("Native timing exceeds parent stage elapsed time")
    checkpoint = rows[-2] if rows[-2]["phase"] == "checkpoint" else None
    report_end = checkpoint if checkpoint is not None else finished
    return dict(
        directory=str(directory), name=entry.get("name"), command=command,
        workload=workload, master_cycles=end - start,
        execution_wall_seconds=wall, execution_cpu_seconds=cpu,
        cycles_per_second=(end - start) / wall if wall > 0 else None,
        seconds_per_billion_cycles=wall * 1000000000 / (end - start) if end > start else None,
        report_wall_seconds=report_end["elapsed_wall_seconds"] - reported["elapsed_wall_seconds"],
        checkpoint_wall_seconds=(finished["elapsed_wall_seconds"] -
                                 checkpoint["elapsed_wall_seconds"]) if checkpoint is not None else None,
        native_window_wall_seconds=native_wall, parent_stage_wall_seconds=process_wall,
        external_overhead_wall_seconds=external)


def stage_window(entry):
    value = entry.get("started_at")
    if value is None:
        return None
    if type(value) is not str:
        raise ValueError("Invalid native stage start timestamp")
    started = datetime.fromisoformat(value)
    if started.tzinfo is None:
        raise ValueError("Native stage timestamp requires a timezone")
    start = started.timestamp()
    return start, start + seconds(entry.get("elapsed_seconds"), "native stage elapsed seconds")


def report(directories, context):
    if (type(context) is not dict or any(
            type(context.get(key)) is not str or not context[key].strip()
            for key in ("host", "build", "revision"))):
        raise ValueError("Launch context requires nonempty host, build and revision strings")
    stages, windows, seen = [], [], set()
    for directory in directories:
        directory = directory.resolve()
        if directory in seen:
            raise ValueError(f"Duplicate campaign directory: {directory}")
        seen.add(directory)
        execution = campaign.read_json(directory / "execution.json")
        manifest_path = directory / "manifest.json"
        if (execution.get("format") != campaign.EXECUTION_FORMAT or
                type(execution.get("returncode")) is not int or execution["returncode"] != 0 or
                execution.get("manifest") != campaign.file_identity(manifest_path)):
            raise ValueError(f"Incomplete or changed execution receipt: {directory}")
        manifest = campaign.read_json(manifest_path)
        if manifest.get("status") not in ("equal", "ceiling_reached"):
            raise ValueError(f"Unsuccessful campaign receipt: {directory}")
        entries = manifest.get("commands")
        if type(entries) is not list or any(
                type(entry) is not dict or type(entry.get("command")) is not list
                for entry in entries):
            raise ValueError("Invalid campaign commands")
        native = [entry for entry in entries if type(entry.get("command")) is list and
                  len(entry["command"]) > 1 and entry["command"][1] == "bios-startup"]
        if not native:
            raise ValueError("No native timing commands")
        names = [entry.get("name") for entry in native]
        if any(type(name) is not str or not name for name in names) or len(set(names)) != len(names):
            raise ValueError("Invalid or duplicate native stage names")
        windows.extend(stage_window(entry) for entry in native)
        stages.extend(stage(directory, manifest, entry) for entry in native)
    for index, item in enumerate(stages):
        window = windows[index]
        overlap = None if any(value is None for value in windows) else [
            dict(directory=stages[other]["directory"], name=stages[other]["name"])
            for other, value in enumerate(windows)
            if other != index and window[0] < value[1] and value[0] < window[1]]
        item["overlapping_stage_windows"] = overlap
        item["workload"]["stage_window_overlap"] = bool(overlap) if overlap is not None else None
    groups = {}
    for index, item in enumerate(stages):
        key = json.dumps(item["workload"], sort_keys=True)
        groups.setdefault(key, []).append(index)
    summaries = []
    for indices in groups.values():
        walls = [stages[index]["execution_wall_seconds"] for index in indices]
        summaries.append(dict(
            workload=stages[indices[0]]["workload"], stage_indices=indices,
            repetitions=len(indices), median_execution_wall_seconds=statistics.median(walls),
            execution_wall_spread_seconds=max(walls) - min(walls),
            min_execution_wall_seconds=min(walls), max_execution_wall_seconds=max(walls)))
    return dict(format="neko-bios-timings-v1", context=context,
                stages=stages, groups=summaries, note=NOTE)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", type=Path, nargs="+")
    parser.add_argument("--context", type=Path, required=True,
                        help="Recorded launch-time host/build/revision JSON; caller-attested.")
    arguments = parser.parse_args(argv)
    try:
        result = report(arguments.directories, campaign.read_json(arguments.context))
        print(json.dumps(result, indent=2, sort_keys=True, allow_nan=False))
        return 0
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
