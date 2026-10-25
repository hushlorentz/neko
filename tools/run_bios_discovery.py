#!/usr/bin/env python3

"""Build neko and capture the fixed two-traced/two-untraced BIOS discovery batch."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

from inspect_startup import EVIDENCE_NOTE, uint


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
            accepted=(0,)):
    output = directory / f"{name}.{extension}"
    errors = directory / f"{name}.stderr"
    entry = dict(name=name, command=command, returncode=None,
                 stdout=output.name, stderr=errors.name)
    manifest["commands"].append(entry)
    write_json(directory / "manifest.json", manifest)
    with output.open("xb") as stdout, errors.open("xb") as stderr:
        result = subprocess.run(command, cwd=ROOT, stdout=stdout, stderr=stderr,
                                check=False)
    entry["returncode"] = result.returncode
    write_json(directory / "manifest.json", manifest)
    if result.returncode not in accepted:
        raise ValueError(
            f"{name} exited {result.returncode}; see {output} and {errors}")
    return result.returncode


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


def inspect_batch(directory, manifest):
    inspector = [sys.executable, "-B", str(INSPECTOR)]
    traced = str(directory / "traced1.jsonl")
    capture(directory, manifest, "summary", inspector + ["summary", traced],
            extension="json")
    equal = True
    for name, left, right, mode in (
        ("compare-traces", "traced1", "traced2", "trace"),
        ("compare-traced-plain", "traced1", "plain1", "run"),
        ("compare-plain", "plain1", "plain2", "run"),
    ):
        status = capture(directory, manifest, name, inspector + [
            "compare", str(directory / f"{left}.jsonl"),
            str(directory / f"{right}.jsonl"), "--mode", mode,
        ], extension="json", accepted=(0, 1))
        equal = equal and status == 0
    summary = json.loads((directory / "summary.json").read_text(encoding="utf-8"))
    failure = summary["first_failure"]
    if failure is not None:
        cycle = uint(failure["master_cycle"], "first failure master_cycle")
        cpu = failure.get("cpu")
        if cpu not in ("ee", "iop", None):
            raise ValueError(f"unsupported first failure CPU: {cpu}")
        filters = [] if cpu is None else ["--cpu", cpu]
        filters += ["--from-cycle", str(max(0, cycle - 16)),
                    "--to-cycle", str(min((1 << 64) - 1, cycle + 16)),
                    "--limit", "200"]
        for command in ("instructions", "events"):
            capture(directory, manifest, f"first-failure-{command}",
                    inspector + [command, traced, *filters], extension="json")
    return equal, failure


def run_batch(directory, bios):
    if not bios.is_file():
        raise ValueError(f"BIOS input does not exist: {bios}")
    directory.mkdir(parents=True, exist_ok=False)
    manifest = dict(status="running", bios=str(bios),
                    max_master_cycles=MASTER_CYCLES, trace_events=TRACE_EVENTS,
                    commands=[], note=EVIDENCE_NOTE)
    try:
        binary = build_neko(directory, manifest)
        exits = {}
        for name in RUN_NAMES:
            command = [str(binary), "bios-startup", str(bios),
                       "--cycles", str(MASTER_CYCLES)]
            if name.startswith("traced"):
                command += ["--trace-events", str(TRACE_EVENTS)]
            exits[name] = capture(directory, manifest, name, command,
                                  extension="jsonl", accepted=(0, 1))
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
    arguments = parser.parse_args(argv)
    try:
        report = run_batch(arguments.directory.resolve(), arguments.bios.resolve())
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
