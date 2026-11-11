"""Durable ownership and inspection gates for the existing BIOS discovery runner."""

import argparse
from datetime import datetime, timezone
import errno
import hashlib
import json
import os
from pathlib import Path
import tempfile

if os.name == "nt":
    import msvcrt
else:
    import fcntl


EXECUTION_FORMAT = "neko-bios-execution-v1"
INSPECTION_FORMAT = "neko-bios-inspection-v1"
INSPECTION_FILES = ("verification-notes.txt", "review.txt", "cleanup.receipt")


def write_json(path, report):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", dir=path.parent,
                prefix=f".{path.name}.", delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(report, stream, indent=2, sort_keys=True, allow_nan=False)
            stream.write("\n")
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def file_identity(path):
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
            size += len(chunk)
    return dict(sha256=digest.hexdigest(), bytes=size)


def read_json(path):
    report = json.loads(path.read_text(encoding="utf-8"))
    if type(report) is not dict:
        raise ValueError(f"{path.name} must contain a JSON object")
    return report


def try_lock(stream):
    stream.seek(0)
    try:
        if os.name == "nt":
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError as error:
        if error.errno in (errno.EACCES, errno.EAGAIN, errno.EDEADLK):
            return False
        raise
    return True


class Owner:
    def __init__(self, directory, invocation=None):
        self.directory = directory
        self.stream = (directory / "owner.lock").open("a+b")
        try:
            if self.stream.tell() == 0:
                self.stream.write(b"\0")
                self.stream.flush()
            if not try_lock(self.stream):
                raise ValueError("Campaign is already owned; do not launch another driver")
            if (directory / "execution.json").exists():
                raise ValueError("Campaign already has an execution receipt; use a new directory")
            self.receipt = dict(
                format=EXECUTION_FORMAT, owner_pid=os.getpid(), command=invocation,
                started_at=datetime.now(timezone.utc).isoformat(), returncode=None)
            write_json(directory / "execution.json", self.receipt)
        except BaseException:
            self.stream.close()
            raise

    def finish(self, manifest, error):
        try:
            if isinstance(error, KeyboardInterrupt):
                code = 130
            elif error is not None:
                code = 2 if isinstance(error, (OSError, ValueError)) else 1
            else:
                code = 0 if manifest.get("status") in ("equal", "ceiling_reached") else 1
            self.receipt.update(
                returncode=code, finished_at=datetime.now(timezone.utc).isoformat(),
                manifest=file_identity(self.directory / "manifest.json"))
            write_json(self.directory / "execution.json", self.receipt)
        finally:
            self.stream.close()


def artifact(directory, name):
    if (type(name) is not str or Path(name).name != name or
            (directory / name).resolve().parent != directory.resolve()):
        raise ValueError(f"Invalid campaign artifact name: {name!r}")
    path = directory / name
    if not path.is_file():
        raise ValueError(f"Missing campaign artifact: {name}")
    return path


def completed_evidence(directory, execution):
    manifest_path = artifact(directory, "manifest.json")
    if execution.get("manifest") != file_identity(manifest_path):
        raise ValueError("Manifest changed or is incomplete after execution")
    manifest = read_json(manifest_path)
    if manifest.get("status") not in ("equal", "ceiling_reached"):
        raise ValueError(f"Unsuccessful execution status: {manifest.get('status')}")
    scout = manifest.get("mode") == "scout"
    ceiling = manifest["status"] == "ceiling_reached"
    if ceiling and not scout:
        raise ValueError("Only a scout can finish at a ceiling without comparisons")
    scope = "scout_ceiling" if ceiling else "scout_origin" if scout else "conformance"
    commands = manifest.get("commands")
    if type(commands) is not list or not commands:
        raise ValueError("Missing completed command receipts")
    evidence = {"manifest.json": file_identity(manifest_path),
                "execution.json": file_identity(directory / "execution.json")}
    names = set()
    for entry in commands:
        if type(entry) is not dict or type(entry.get("name")) is not str:
            raise ValueError("Invalid command receipt")
        name, code = entry["name"], entry.get("returncode")
        accepted = entry.get("accepted_exit_codes")
        if (name in names or type(code) is not int or
                type(accepted) is not list or not accepted or
                any(type(value) is not int or value not in (0, 1) for value in accepted) or
                code not in accepted):
            raise ValueError(f"Incomplete or unsuccessful command: {name}")
        names.add(name)
        for field in ("stdout", "stderr"):
            path = artifact(directory, entry.get(field))
            if field == "stdout" and (
                    type(entry.get("output_bytes")) is not int or
                    entry["output_bytes"] != path.stat().st_size):
                raise ValueError(f"Command output size changed: {path.name}")
            if path.suffix == ".json":
                read_json(path)
                evidence[path.name] = file_identity(path)
    if scout:
        required = {"scout-summary"}
        if not any(name.startswith("scout-") and name != "scout-summary" for name in names):
            raise ValueError("Missing native scout command")
        if ceiling:
            progress = manifest.get("progress", {})
            if (type(progress) is not dict or
                    type(manifest.get("max_master_cycles")) is not int or
                    progress.get("master_cycle") != manifest["max_master_cycles"]):
                raise ValueError("Scout did not reach its declared ceiling")
            comparisons = ()
        else:
            required.update(("replay", "plain", "summary"))
            comparisons = ("compare-replay-plain",)
            if manifest.get("failure_reproduced") is not True:
                raise ValueError("Scout failure was not reproduced")
    elif "checkpoint_at" in manifest:
        required = {"checkpoint", "cold", "resumed", "resumed-plain", "summary"}
        comparisons = ("compare-cold-resumed", "compare-resumed-plain")
        metadata_path = artifact(directory, "checkpoint.state.json")
        metadata = read_json(metadata_path)
        checkpoint = artifact(directory, "checkpoint.state")
        if (metadata.get("validated") is not True or
                metadata.get("state") != file_identity(checkpoint)):
            raise ValueError("Checkpoint validation receipt or state changed")
        evidence[metadata_path.name] = file_identity(metadata_path)
        evidence[checkpoint.name] = file_identity(checkpoint)
    else:
        required = {"traced1", "traced2", "plain1", "plain2", "summary"}
        comparisons = ("compare-traces", "compare-traced-plain", "compare-plain")
    required.update(comparisons)
    if not required <= names:
        raise ValueError(f"Missing required stages: {sorted(required - names)}")
    for name in comparisons:
        comparison = read_json(artifact(directory, f"{name}.json"))
        if comparison.get("equal") is not True or comparison.get("first_difference") is not None:
            raise ValueError(f"Comparison did not agree: {name}")
    states = manifest.get("final_states", {})
    if (scout and not ceiling) or "checkpoint_at" in manifest:
        expected = {"scout", "replay", "plain"} if scout else {"cold", "resumed", "resumed-plain"}
        if type(states) is not dict or set(states) != expected:
            raise ValueError("Missing canonical comparisons")
        if any(type(state) is not dict for state in states.values()):
            raise ValueError("Invalid canonical comparison receipt")
        baseline = states["scout" if scout else "cold"]
        identity = {key: baseline.get(key) for key in ("bytes", "sha256")}
        retained = 0
        for state in states.values():
            if (state.get("equal") is not True or
                    any(state.get(key) != value for key, value in identity.items())):
                raise ValueError("Canonical comparisons disagree")
            if state.get("pruned_duplicate") is not True:
                path = artifact(directory, state.get("file"))
                if file_identity(path) != identity:
                    raise ValueError(f"Canonical state changed: {path.name}")
                evidence[path.name] = identity
                retained += 1
        if not retained:
            raise ValueError("No retained canonical baseline")
    if scout:
        receipts = manifest.get("checkpoint_receipts")
        if type(receipts) is not list or not receipts:
            raise ValueError("Missing scout checkpoint receipts")
        for receipt in receipts:
            if type(receipt) is not dict or type(receipt.get("path")) is not str:
                raise ValueError("Invalid scout checkpoint receipt")
            if receipt.get("pruned") is not True:
                path = artifact(directory, Path(receipt.get("path", "")).name)
                if receipt.get("state") != file_identity(path):
                    raise ValueError(f"Scout checkpoint changed: {path.name}")
                evidence[path.name] = receipt["state"]
    return scope, evidence


def inspection_evidence(directory, evidence):
    evidence = dict(evidence)
    for name in INSPECTION_FILES:
        path = artifact(directory, name)
        if not path.read_text(encoding="utf-8").strip():
            raise ValueError(f"Empty inspection evidence: {name}")
        evidence[name] = file_identity(path)
    return evidence


def terminal_status(directory, execution):
    code = execution.get("returncode")
    if code is None or code == 130:
        return dict(state="interrupted",
                    reason="Ownership ended without success; inspect partial evidence and possible orphan children")
    if type(code) is not int or code != 0:
        return dict(state="failed", reason=f"Execution returned {code!r}")
    try:
        if not execution.get("finished_at"):
            raise ValueError("Missing execution completion timestamp")
        scope, evidence = completed_evidence(directory, execution)
        if (directory / "inspection.json").exists():
            inspection = read_json(directory / "inspection.json")
            if (inspection.get("format") != INSPECTION_FORMAT or
                    type(inspection.get("outer_exit")) is not int or
                    inspection["outer_exit"] != 0 or inspection.get("scope") != scope or
                    inspection.get("evidence") != inspection_evidence(directory, evidence)):
                raise ValueError("Inspection receipt is invalid or its evidence changed")
            return dict(state="verified", scope=scope)
        return dict(state="execution_finished", scope=scope,
                    reason="Inspection pending; no boot or hardware-conformance claim")
    except (OSError, ValueError) as error:
        return dict(state="failed", reason=str(error))


def status(directory):
    try:
        execution = read_json(directory / "execution.json")
        if execution.get("format") != EXECUTION_FORMAT:
            raise ValueError("Unknown execution receipt format")
        with (directory / "owner.lock").open("r+b") as stream:
            if not try_lock(stream):
                manifest = read_json(directory / "manifest.json")
                commands = manifest.get("commands", [])
                if (type(commands) is not list or
                        any(type(entry) is not dict for entry in commands)):
                    raise ValueError("Invalid running command receipts")
                active = commands[-1] if commands else {}
                return dict(state="running", owner_pid=execution.get("owner_pid"),
                            command=execution.get("command"),
                            active_stage=active.get("name"),
                            stage_exit=active.get("returncode"),
                            stage_started_at=active.get("started_at"),
                            progress=manifest.get("progress"))
            execution = read_json(directory / "execution.json")
            if execution.get("format") != EXECUTION_FORMAT:
                raise ValueError("Unknown execution receipt format")
            return terminal_status(directory, execution)
    except (OSError, ValueError) as error:
        return dict(state="unknown", reason=str(error))


def finalize(directory, outer_exit):
    if type(outer_exit) is not int or outer_exit != 0:
        raise ValueError("Finalization requires the observed outer exit 0")
    with (directory / "owner.lock").open("r+b") as stream:
        if not try_lock(stream):
            raise ValueError("Campaign is running; inspection cannot be finalized")
        execution = read_json(directory / "execution.json")
        if execution.get("format") != EXECUTION_FORMAT:
            raise ValueError("Unknown execution receipt format")
        report = terminal_status(directory, execution)
        if report["state"] == "verified":
            return report
        if report["state"] != "execution_finished":
            raise ValueError(f"Cannot finalize {report['state']}: {report.get('reason')}")
        scope, evidence = completed_evidence(directory, execution)
        evidence = inspection_evidence(directory, evidence)
        write_json(directory / "inspection.json", dict(
            format=INSPECTION_FORMAT, inspected_at=datetime.now(timezone.utc).isoformat(),
            outer_exit=outer_exit, scope=scope, evidence=evidence))
        return dict(state="verified", scope=scope)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--status", action="store_true")
    action.add_argument("--finalize", action="store_true")
    parser.add_argument("--outer-exit", type=int)
    arguments = parser.parse_args(argv)
    if arguments.status and arguments.outer_exit is not None:
        parser.error("--outer-exit requires --finalize")
    try:
        report = (status(arguments.directory.resolve()) if arguments.status else
                  finalize(arguments.directory.resolve(), arguments.outer_exit))
        print(json.dumps(report, indent=2, sort_keys=True, allow_nan=False))
        return (0 if report["state"] == "verified" else
                1 if report["state"] in ("running", "execution_finished") else 2)
    except (OSError, ValueError) as error:
        parser.exit(2, f"error: {error}\n")
