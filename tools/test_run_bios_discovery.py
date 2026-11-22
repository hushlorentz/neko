"""BIOS-free tests for the repeatable startup discovery workflow."""

from contextlib import redirect_stderr, redirect_stdout
import gzip
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

import run_bios_discovery as discovery
from test_inspect_startup import capture_records, records


REAL_RUN = subprocess.run
REAL_POPEN = subprocess.Popen


class StagedDiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "staged"
        self.bios = self.root / "authored.bin"
        self.bios.write_bytes(b"authored fixture identity")
        self.binary = self.root / "authored"
        self.binary.write_bytes(b"authored executable identity")
        self.commands = []
        self.failure_cycle = 25
        self.replay_mismatch = False
        self.capture = discovery.capture
        self.before_scout = None
        self.runtime_error = False
        self.runtime_replay_mismatch = False

    def fake_capture(self, directory, manifest, name, command, **kwargs):
        if command[0] != str(self.binary):
            return self.capture(directory, manifest, name, command, **kwargs)
        if "--scout" in command and self.before_scout is not None:
            self.before_scout(name, manifest)
        self.commands.append(command)
        ceiling = int(command[command.index("--cycles") + 1])
        start = 0
        if "--resume-checkpoint" in command:
            path = Path(command[command.index("--resume-checkpoint") + 1])
            start = json.loads(path.read_text())["master_cycle"]
        failed = (self.failure_cycle is not None and
                  start < self.failure_cycle <= ceiling)
        cycle = self.failure_cycle if failed else ceiling
        traced = "--trace-events" in command or "--scout" in command
        rows = records(traced)
        rows[0].update(max_master_cycles=ceiling)
        if start:
            rows[0].update(start="checkpoint", start_master_cycle=start)
        if "--scout" in command:
            rows[0]["observation"] = "failure_probe"
        if "--trace-from-cycle" in command:
            rows[0]["trace_start_cycle"] = int(command[command.index("--trace-from-cycle") + 1])
        rows[1]["run"].update(master_cycles=cycle - start,
                              cycle_limit_reached=cycle == ceiling,
                              ee_cycles=cycle - start)
        rows[1]["snapshot"]["master_cycle"] = cycle
        if traced:
            if failed:
                for row in rows:
                    if row["type"] == "trace_event":
                        row["master_cycle"] = cycle
                rows[-1]["failure"]["master_cycle"] = cycle
                if self.replay_mismatch and "--trace-events" in command:
                    rows[-1]["failure"]["access"]["virtual_address"] += 4
            else:
                rows = rows[:2] + [
                    dict(type="startup_trace_summary", event_limit=16,
                         captured_events=0, truncated=False),
                    dict(type="startup_first_failure", trace_truncated=False,
                         execution_boundary="complete_cycle", failure=None),
                ]
        if failed and self.runtime_error:
            rows[1].update(type="startup_runtime_error", run=None,
                           execution_boundary="incomplete_cycle",
                           message="Authored GS transfer runtime error")
            if self.runtime_replay_mismatch and name == "runtime-replay":
                rows[1]["message"] = "Different authored runtime error"
            if "--scout" in command:
                rows = rows[:2] + [
                    dict(type="startup_trace_summary", event_limit=16,
                         captured_events=0, truncated=False),
                    dict(type="startup_first_failure", trace_truncated=False,
                         execution_boundary="incomplete_cycle", failure=None),
                ]
            elif traced:
                rows[-1].update(execution_boundary="incomplete_cycle", failure=None)
        if "--save-checkpoint" in command and not (failed and self.runtime_error):
            path = Path(command[command.index("--save-checkpoint") + 1])
            path.write_text(json.dumps(rows[1]["snapshot"], sort_keys=True))
        extension = kwargs.get("extension", "stdout")
        path = directory / f"{name}.{extension}"
        raw = "".join(json.dumps(row) + "\n" for row in rows).encode()
        if kwargs.get("compressed"):
            path = Path(str(path) + ".gz")
            raw = gzip.compress(raw)
        path.write_bytes(raw)
        (directory / f"{name}.stderr").touch()
        status = 1 if failed and "--stop-on-first-failure" in command or (
            failed and "--scout" in command and cycle < ceiling) else 0
        if failed and self.runtime_error:
            status = 2
        manifest["commands"].append(dict(name=name, command=command,
                                         returncode=status, elapsed_seconds=0.1,
                                         accepted_exit_codes=list(kwargs.get("accepted", (0,))),
                                         output_bytes=path.stat().st_size,
                                         stdout=path.name, stderr=f"{name}.stderr"))
        discovery.write_json(directory / "manifest.json", manifest)
        return status

    def invoke(self, expected=0, *, ceiling=30, extra_args=()):
        with patch.object(discovery, "build_neko", return_value=self.binary), \
             patch.object(discovery, "capture", side_effect=self.fake_capture), \
             redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            status = discovery.main([
                str(self.output), "--bios", str(self.bios),
                "--mode", "scout", "--cycles", str(ceiling),
                "--checkpoint-every", "10", "--replay-window", "3", *extra_args])
        self.assertEqual(status, expected)
        return json.loads((self.output / "manifest.json").read_text())

    def test_scout_replays_only_the_nearest_preceding_checkpoint_and_verifies(self):
        manifest = self.invoke()
        self.assertEqual(manifest["status"], "equal")
        self.assertEqual(len(self.commands), 5)  # three scout chunks, replay, plain
        replay, plain = self.commands[-2:]
        self.assertIn("--stop-on-first-failure", replay)
        self.assertNotIn("--trace-events", plain)
        self.assertEqual(replay[replay.index("--cycles") + 1], "25")
        self.assertEqual(replay[replay.index("--trace-from-cycle") + 1], "22")
        checkpoint = Path(replay[replay.index("--resume-checkpoint") + 1])
        self.assertEqual(json.loads(checkpoint.read_text())["master_cycle"], 20)
        self.assertTrue(manifest["failure_reproduced"])
        self.assertTrue(manifest["final_states"]["plain"]["equal"])
        self.assertFalse(manifest["checkpoint_receipts"][-1]["validated"])
        self.assertEqual(manifest["checkpoint_receipts"][-1]["binary"],
                         discovery.file_identity(self.binary))
        self.assertFalse((self.output / "scout-10.state").exists())

    def test_scout_at_ceiling_does_not_invent_a_failure_or_run_replays(self):
        self.failure_cycle = None
        manifest = self.invoke()
        self.assertEqual(manifest["status"], "ceiling_reached")
        self.assertEqual(len(self.commands), 3)
        self.assertNotIn("failure_reproduced", manifest)
        status = discovery.campaign.status(self.output)
        self.assertEqual(status["state"], "execution_finished")
        self.assertEqual(status["scope"], "scout_ceiling")

    def test_scout_runtime_error_retains_bounded_replays_but_stays_failed(self):
        self.runtime_error = True
        manifest = self.invoke(expected=2)
        self.assertEqual(manifest["status"], "error")
        self.assertEqual(len(self.commands), 5)
        replay, plain = self.commands[-2:]
        self.assertEqual(replay[replay.index("--cycles") + 1], "26")
        self.assertEqual(replay[replay.index("--trace-from-cycle") + 1], "22")
        self.assertNotIn("--trace-events", plain)
        self.assertTrue(manifest["runtime_error_reproduced"])
        self.assertTrue(manifest["runtime_error_comparisons_equal"])
        self.assertNotIn("final_states", manifest)
        self.assertFalse((self.output / "scout-30.state").exists())
        self.assertTrue((self.output / "scout-20.state").exists())
        self.assertTrue((self.output / "runtime-error-events.json").exists())
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")
        with self.assertRaises(ValueError):
            discovery.campaign.finalize(self.output, 2)

    def test_runtime_replay_disagreement_does_not_claim_reproduction(self):
        self.runtime_error = True
        self.runtime_replay_mismatch = True
        manifest = self.invoke(expected=2)
        self.assertFalse(manifest["runtime_error_reproduced"])
        self.assertFalse(manifest["runtime_error_comparisons_equal"])
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")
        self.assertTrue((self.output / "runtime-replay.jsonl.gz").is_file())
        self.assertTrue((self.output / "runtime-plain.jsonl").is_file())

    def test_scout_replay_disagreement_is_preserved_as_a_mismatch(self):
        self.replay_mismatch = True
        manifest = self.invoke(expected=1)
        self.assertEqual(manifest["status"], "mismatch")
        self.assertFalse(manifest["failure_reproduced"])
        self.assertTrue((self.output / "scout-10.state").is_file())
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")

    def test_scout_failure_on_checkpoint_boundary_replays_from_before_the_fault(self):
        self.failure_cycle = 20
        manifest = self.invoke()
        self.assertTrue(manifest["failure_reproduced"])
        replay = self.commands[-2]
        checkpoint = Path(replay[replay.index("--resume-checkpoint") + 1])
        self.assertEqual(json.loads(checkpoint.read_text())["master_cycle"], 10)

    def test_first_chunk_failure_replays_cold_without_a_postfailure_checkpoint(self):
        self.failure_cycle = 5
        manifest = self.invoke()
        self.assertTrue(manifest["failure_reproduced"])
        self.assertNotIn("--resume-checkpoint", self.commands[-2])

    def test_zero_budget_only_records_a_cold_reset_snapshot(self):
        self.failure_cycle = None
        manifest = self.invoke(ceiling=0)
        self.assertEqual(manifest["status"], "ceiling_reached")
        self.assertEqual(len(self.commands), 1)

    def test_scout_checkpoint_identity_guard_checks_executable_bios_and_state(self):
        for field in ("binary", "bios", "state"):
            with self.subTest(field=field):
                path = self.root / f"{field}.identity"
                path.write_bytes(b"before")
                receipt = dict(binary=discovery.file_identity(self.binary),
                               bios=discovery.file_identity(self.bios),
                               state=discovery.file_identity(path), path=str(path))
                changed = dict(binary=self.binary, bios=self.bios, state=path)[field]
                original = changed.read_bytes()
                changed.write_bytes(original + b"changed")
                with self.assertRaisesRegex(ValueError, "identity changed"):
                    discovery.verify_scout_checkpoint(receipt, self.binary, self.bios)
                changed.write_bytes(original)

    def test_live_retention_keeps_two_recent_states_and_launch_pins(self):
        self.failure_cycle = None

        def inspect_live(name, manifest):
            if name == "scout-60":
                retained = {path.name for path in self.output.glob("scout-*.state")}
                self.assertEqual(retained, {"scout-10.state", "scout-40.state",
                                            "scout-50.state"})
                self.assertTrue(manifest["checkpoint_receipts"][1]["pruned"])
                self.assertTrue((self.output / "scout-20.state.json").is_file())

        self.before_scout = inspect_live
        manifest = self.invoke(ceiling=60, extra_args=("--pin-checkpoint-at", "10"))
        self.assertEqual({path.name for path in self.output.glob("scout-*.state")},
                         {"scout-10.state", "scout-50.state", "scout-60.state"})
        self.assertEqual(manifest["pinned_checkpoint_cycles"], [10])
        self.assertTrue(manifest["checkpoint_receipts"][0]["pinned"])
        self.assertEqual(manifest["pruned_state_bytes"], sum(
            item["state"]["bytes"] for item in manifest["checkpoint_receipts"]
            if item.get("pruned")))
        self.assertEqual(discovery.campaign.status(self.output)["state"], "execution_finished")

    def test_long_mismatch_preserves_recent_failure_and_pinned_evidence(self):
        self.failure_cycle = 65
        self.replay_mismatch = True
        manifest = self.invoke(expected=1, ceiling=100,
                               extra_args=("--pin-checkpoint-at", "10"))
        self.assertEqual({path.name for path in self.output.glob("scout-*.state")},
                         {"scout-10.state", "scout-50.state", "scout-60.state",
                          "scout-70.state"})
        self.assertTrue(manifest["checkpoint_receipts"][1]["pruned"])
        self.assertTrue((self.output / "replay-final.state").is_file())
        self.assertTrue((self.output / "plain-final.state").is_file())

    def test_live_retention_preserves_recent_checkpoints_on_interruption(self):
        self.failure_cycle = None

        def interrupt(name, manifest):
            if name == "scout-70":
                raise KeyboardInterrupt

        self.before_scout = interrupt
        manifest = self.invoke(expected=130, ceiling=100)
        self.assertEqual(manifest["error"], "interrupted")
        self.assertEqual({path.name for path in self.output.glob("scout-*.state")},
                         {"scout-50.state", "scout-60.state"})

    def test_live_retention_refuses_changed_obsolete_state(self):
        self.failure_cycle = None

        def change_old_state(name, manifest):
            if name == "scout-30":
                (self.output / "scout-10.state").write_bytes(b"changed")

        self.before_scout = change_old_state
        manifest = self.invoke(expected=2, ceiling=40)
        self.assertIn("changed before cleanup", manifest["error"])
        self.assertEqual((self.output / "scout-10.state").read_bytes(), b"changed")
        self.assertTrue((self.output / "scout-20.state").is_file())
        self.assertTrue((self.output / "scout-30.state").is_file())

    def test_live_retention_records_pending_deletion_before_unlink_error(self):
        self.failure_cycle = None
        unlink = Path.unlink

        def fail_unlink(path, *args, **kwargs):
            if (path.name == "scout-10.state" and
                    path.parent.resolve() == self.output.resolve()):
                raise PermissionError("authored cleanup denial")
            return unlink(path, *args, **kwargs)

        with patch.object(Path, "unlink", fail_unlink):
            manifest = self.invoke(expected=2, ceiling=40)
        receipt = manifest["checkpoint_receipts"][0]
        self.assertTrue(receipt["prune_pending"])
        self.assertFalse(receipt.get("pruned", False))
        self.assertTrue((self.output / "scout-10.state").is_file())
        self.assertTrue((self.output / "scout-20.state").is_file())

    def test_invalid_pins_fail_before_creating_a_batch(self):
        for cycle in (0, 11, 41):
            with self.subTest(cycle=cycle), self.assertRaises(ValueError):
                discovery.run_scout_batch(
                    self.output, self.bios, master_cycles=40,
                    checkpoint_every=10, replay_window=3,
                    pinned_checkpoint_cycles=(cycle,))
            self.assertFalse(self.output.exists())

    def test_interrupted_unlink_keeps_pending_receipt_and_recent_states(self):
        self.failure_cycle = None
        unlink = Path.unlink

        def interrupt_after_unlink(path, *args, **kwargs):
            result = unlink(path, *args, **kwargs)
            if (path.name == "scout-10.state" and
                    path.parent.resolve() == self.output.resolve()):
                raise KeyboardInterrupt
            return result

        with patch.object(Path, "unlink", interrupt_after_unlink):
            manifest = self.invoke(expected=130, ceiling=40)
        receipt = manifest["checkpoint_receipts"][0]
        self.assertTrue(receipt["prune_pending"])
        self.assertFalse(receipt.get("pruned", False))
        self.assertFalse((self.output / "scout-10.state").exists())
        self.assertTrue((self.output / "scout-20.state").is_file())
        self.assertTrue((self.output / "scout-30.state").is_file())
        self.assertEqual(discovery.campaign.status(self.output)["state"], "interrupted")

    def test_cleanup_refuses_a_state_outside_the_campaign(self):
        self.output.mkdir()
        foreign = self.root / "foreign.state"
        foreign.write_bytes(b"retain external evidence")
        manifest = {"checkpoint_receipts": [
            {"path": str(foreign), "state": discovery.file_identity(foreign)}]}
        with self.assertRaises(ValueError):
            discovery.prune_scout_states(self.output, manifest, set())
        self.assertEqual(foreign.read_bytes(), b"retain external evidence")

    def test_repeatable_pins_include_the_partial_final_interval(self):
        self.failure_cycle = None
        manifest = self.invoke(ceiling=35, extra_args=(
            "--pin-checkpoint-at", "10", "--pin-checkpoint-at", "20",
            "--pin-checkpoint-at", "35", "--pin-checkpoint-at", "10"))
        self.assertEqual(manifest["pinned_checkpoint_cycles"], [10, 20, 35])
        self.assertEqual({item["master_cycle"] for item in
                          manifest["checkpoint_receipts"] if item["pinned"]},
                         {10, 20, 35})
        self.assertTrue(all(Path(item["path"]).is_file()
                            for item in manifest["checkpoint_receipts"]))

    def test_pins_are_rejected_outside_scout_mode(self):
        with redirect_stderr(io.StringIO()):
            status = discovery.main([str(self.output), "--bios", str(self.bios),
                                     "--pin-checkpoint-at", "10"])
        self.assertEqual(status, 2)
        self.assertFalse(self.output.exists())


class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        self.output = self.root / "evidence with spaces"
        self.bios = self.root / "input with spaces.bin"
        self.bios.touch()
        build = self.root / "out" / "check"
        build.mkdir(parents=True)
        (build / "CMakeCache.txt").touch()
        self.binary = build / discovery.NEKO_NAME
        self.binary.touch()
        self.binary.chmod(0o755)
        self.calls = []
        self.processes = {}
        self.run_statuses = [0, 0, 0, 0]
        self.run_count = 0
        self.build_status = 0
        self.failure = True
        self.change_plain = False
        self.change_canonical = False
        self.malformed = False
        self.cpu = "iop"
        self.run_error = None
        self.root_patch = patch.object(discovery, "ROOT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)

    def execute(self, command, **kwargs):
        self.calls.append(command)
        self.assertEqual(kwargs["cwd"], self.root)
        if command[0] == "cmake":
            return subprocess.CompletedProcess(command, self.build_status)
        if command[0] == str(self.binary):
            index = self.run_count
            self.run_count += 1
            if self.run_error is not None:
                raise self.run_error
            rows = (capture_records() if "--failure-context" in command else
                    records("--trace-events" in command))
            budget = int(command[command.index("--cycles") + 1])
            rows[0]["max_master_cycles"] = budget
            if self.run_statuses[index] == 0:
                rows[1]["run"].update(master_cycles=budget,
                                      cycle_limit_reached=True)
                rows[1]["snapshot"]["master_cycle"] = budget
            if self.change_plain and index == 2:
                rows[1]["snapshot"]["ee"]["pc"] += 4
            if "--trace-events" in command:
                if not self.failure:
                    rows[-1]["failure"] = None
                else:
                    rows[-1]["failure"]["cpu"] = self.cpu
            if "--trace-from-cycle" in command:
                rows[0]["trace_start_cycle"] = int(
                    command[command.index("--trace-from-cycle") + 1])
            if "--resume-checkpoint" in command:
                path = Path(command[command.index("--resume-checkpoint") + 1])
                cycle = json.loads(path.read_text())["master_cycle"]
                rows[0].update(start="checkpoint", start_master_cycle=cycle)
            if "--save-checkpoint" in command:
                path = Path(command[command.index("--save-checkpoint") + 1])
                state = dict(rows[1]["snapshot"])
                if self.change_canonical and index == 3:
                    state["authored_hidden_state"] = 1
                path.write_text(json.dumps(state, sort_keys=True))
            raw = ("not json\n" if self.malformed else
                   "".join(json.dumps(row) + "\n" for row in rows))
            kwargs["stdout"].write(raw.encode("utf-8"))
            kwargs["stderr"].write(f"authored stderr {index}\n".encode("utf-8"))
            if "--progress-file" in command:
                progress = Path(command[command.index("--progress-file") + 1])
                sample = dict(
                    format="neko-bios-progress-v1", phase="finished",
                    master_cycle=rows[1]["snapshot"]["master_cycle"],
                    start_master_cycle=rows[0].get("start_master_cycle", 0),
                    cycle_ceiling=budget, elapsed_wall_seconds=2.0,
                    elapsed_cpu_seconds=1.5, cycles_per_second=4.0,
                    sampled_at_unix_seconds=1000.0)
                progress.write_text(json.dumps(sample) + "\n")
            return subprocess.CompletedProcess(command, self.run_statuses[index])
        return REAL_RUN(command, **kwargs)

    def spawn(self, command, **kwargs):
        if command[0] == "/usr/bin/sample":
            self.calls.append(command)
            Path(command[command.index("-file") + 1]).write_text("Authored sample report\n")
            return Mock(stdout=None, pid=5432,
                        wait=Mock(return_value=getattr(self, "sample_status", 0)), kill=Mock())
        if command[0] not in ("cmake", str(self.binary)):
            self.calls.append(command)
            return REAL_POPEN(command, **kwargs)
        piped = kwargs["stdout"] == subprocess.PIPE
        output = io.BytesIO() if piped else kwargs["stdout"]
        result = self.execute(command, cwd=kwargs["cwd"], stdout=output,
                              stderr=kwargs["stderr"])
        if piped:
            output.seek(0)
        process = Mock(stdout=output if piped else None, pid=4321,
                       wait=Mock(return_value=result.returncode), kill=Mock())
        if "--save-checkpoint" in command:
            path = Path(command[command.index("--save-checkpoint") + 1])
            self.processes[path.stem] = process
        return process

    def invoke(self, status=0, *, failure_context=False, cycles=None,
               checkpoint_at=None, checkpoint=None, sample_seconds=None,
               executable=None, mode=None):
        output = io.StringIO()
        errors = io.StringIO()
        with patch.object(discovery.sys, "platform", "darwin"), \
             patch.object(discovery.subprocess, "run", side_effect=self.execute), \
             patch.object(discovery.subprocess, "Popen", side_effect=self.spawn):
            with redirect_stdout(output), redirect_stderr(errors):
                arguments = [str(self.output), "--bios", str(self.bios)]
                if failure_context:
                    arguments.append("--failure-context")
                if cycles is not None:
                    arguments += ["--cycles", str(cycles)]
                if checkpoint_at is not None:
                    arguments += ["--checkpoint-at", str(checkpoint_at)]
                if checkpoint is not None:
                    arguments += ["--checkpoint", str(checkpoint)]
                if sample_seconds is not None:
                    arguments += ["--sample-seconds", str(sample_seconds)]
                if executable is not None:
                    arguments += ["--executable", str(executable)]
                if mode is not None:
                    arguments += ["--mode", mode]
                result = discovery.main(arguments)
        self.assertEqual(result, status, errors.getvalue())
        return output.getvalue(), errors.getvalue()

    def read(self, name):
        return json.loads((self.output / name).read_text(encoding="utf-8"))

    def test_prebuilt_matrix_records_identity_without_building(self):
        self.invoke(executable=self.binary)
        self.assertFalse(any(command[0] == "cmake" for command in self.calls))
        self.assertEqual(self.run_count, 4)
        manifest = self.read("manifest.json")
        self.assertEqual(manifest["prebuilt_executable"],
                         dict(path=str(self.binary),
                              identity=discovery.file_identity(self.binary)))
        self.assertEqual(manifest["binary_identity"],
                         manifest["prebuilt_executable"]["identity"])
        self.assertEqual(discovery.campaign.status(self.output)["state"],
                         "execution_finished")

    def test_prebuilt_invalid_input_and_scout_scope_do_not_launch(self):
        self.invoke(status=2, executable=self.root / "missing")
        self.assertFalse(self.output.exists())
        self.binary.chmod(0o644)
        self.invoke(status=2, executable=self.binary)
        self.assertFalse(self.output.exists())
        self.binary.chmod(0o755)
        self.invoke(status=2, executable=self.binary, mode="scout")
        self.assertFalse(self.output.exists())
        self.assertEqual(self.calls, [])

    def test_prebuilt_change_during_capture_stops_further_execution(self):
        execute = self.execute

        def change_binary(command, **kwargs):
            result = execute(command, **kwargs)
            if command[0] == str(self.binary):
                self.binary.write_bytes(b"changed executable")
            return result

        self.execute = change_binary
        self.invoke(status=2, executable=self.binary)
        self.assertEqual(self.run_count, 1)
        self.assertIn("executable changed", self.read("manifest.json")["error"])
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")

    def test_prebuilt_change_during_checkpoint_inspection_cannot_be_certified(self):
        spawn = self.spawn
        self.run_statuses = [0, 1, 1, 1]

        def change_binary(command, **kwargs):
            result = spawn(command, **kwargs)
            if str(discovery.INSPECTOR) in command and "compare" in command:
                self.assertEqual(self.run_count, 4)
                self.binary.write_bytes(b"replacement after native captures")
            return result

        self.spawn = change_binary
        self.invoke(status=2, executable=self.binary, checkpoint_at=40, cycles=1000)
        self.assertEqual(self.run_count, 4)
        self.assertFalse((self.output / "checkpoint.state.json").exists())
        self.assertIn("executable changed", self.read("manifest.json")["error"])
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")

    def test_trace_capture_is_stream_compressed_without_an_uncompressed_copy(self):
        self.output.mkdir()
        manifest = dict(commands=[])
        raw = b'{"type":"authored"}\n' * 10000
        command = [sys.executable, "-c",
                   "import sys; sys.stdout.buffer.write(" + repr(raw[:20]) +
                   " * 10000)"]
        discovery.capture(self.output, manifest, "trace", command,
                          extension="jsonl", compressed=True)
        path = self.output / "trace.jsonl.gz"
        with gzip.open(path, "rb") as stream:
            self.assertEqual(stream.read(), raw[:20] * 10000)
        self.assertFalse((self.output / "trace.jsonl").exists())
        self.assertLess(path.stat().st_size, 4096)
        self.assertEqual(manifest["commands"][0]["returncode"], 0)
        self.assertEqual(manifest["commands"][0]["uncompressed_bytes"],
                         len(raw[:20]) * 10000)
        self.assertEqual(manifest["commands"][0]["output_bytes"], path.stat().st_size)

    def test_sampling_uses_the_owned_plain_pid_and_retains_its_report(self):
        self.invoke(sample_seconds=1)
        samples = [command for command in self.calls if command[0] == "/usr/bin/sample"]
        self.assertEqual(len(samples), 1)
        self.assertEqual(samples[0][1:4], ["4321", "1", "1"])
        self.assertIn("-mayDie", samples[0])
        manifest = self.read("manifest.json")
        plain = next(entry for entry in manifest["commands"] if entry["name"] == "plain1")
        self.assertEqual(plain["sampling_seconds"], 1)
        self.assertEqual(plain["sample_identity"],
                         discovery.file_identity(self.output / plain["sample_file"]))
        self.assertEqual(discovery.campaign.status(self.output)["state"], "execution_finished")
        (self.output / plain["sample_file"]).write_text("changed")
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")

    def test_sampler_failure_reaps_the_owned_native_capture_and_stays_failed(self):
        self.sample_status = 1
        self.invoke(status=2, sample_seconds=1)
        self.processes["plain1-final"].kill.assert_called_once()
        self.assertEqual(self.run_count, 3)
        self.assertEqual(self.read("manifest.json")["status"], "error")
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")

    def test_invalid_sampling_scope_fails_before_build_or_capture(self):
        for seconds, checkpoint_at in ((-1, None), (61, None), (1, 0)):
            with self.subTest(seconds=seconds, checkpoint_at=checkpoint_at):
                self.invoke(status=2, sample_seconds=seconds, checkpoint_at=checkpoint_at)
                self.assertEqual(self.calls, [])
                self.assertFalse(self.output.exists())

    def test_sampling_on_other_hosts_fails_before_build_or_capture(self):
        with patch.object(discovery.sys, "platform", "linux"):
            with self.assertRaisesRegex(ValueError, "macOS"):
                discovery.run_batch(self.output, self.bios, sample_seconds=1)
        self.assertEqual(self.calls, [])
        self.assertFalse(self.output.exists())

    def test_compression_failure_reaps_child_and_preserves_partial_receipt(self):
        self.output.mkdir()
        process = Mock(stdout=io.BytesIO(b"authored"),
                       wait=Mock(return_value=-9), kill=Mock())
        manifest = dict(commands=[])
        with patch.object(discovery.subprocess, "Popen", return_value=process), \
             patch.object(discovery.gzip, "GzipFile",
                          side_effect=OSError("authored disk failure")):
            with self.assertRaisesRegex(OSError, "authored disk failure"):
                discovery.capture(self.output, manifest, "trace", ["authored"],
                                  extension="jsonl", compressed=True)
        process.kill.assert_called_once()
        process.wait.assert_called_once()
        self.assertTrue(process.stdout.closed)
        self.assertEqual(self.read("manifest.json")["commands"][0]["returncode"], -9)

    def create_checkpoint(self):
        self.run_statuses = [0, 1, 1, 1]
        self.invoke(checkpoint_at=40, cycles=1000)
        return self.output / "checkpoint.state"

    def test_checkpoint_creation_proves_cold_trace_plain_and_canonical_equivalence(self):
        checkpoint = self.create_checkpoint()
        manifest = self.read("manifest.json")
        self.assertEqual(manifest["status"], "equal")
        self.assertEqual(self.run_count, 4)
        runs = [command for command in self.calls if command[0] == str(self.binary)]
        self.assertIn("--save-checkpoint", runs[0])
        self.assertEqual(runs[0][runs[0].index("--cycles") + 1], "40")
        self.assertIn("--trace-from-cycle", runs[1])
        self.assertNotIn("--resume-checkpoint", runs[1])
        for run in runs[2:]:
            self.assertIn("--resume-checkpoint", run)
        self.assertNotIn("--trace-events", runs[3])
        self.assertTrue(self.read("compare-cold-resumed.json")["equal"])
        self.assertTrue(self.read("compare-resumed-plain.json")["equal"])
        self.assertTrue((self.output / "cold-final.state").is_file())
        for name in ("resumed", "resumed-plain"):
            self.assertFalse((self.output / f"{name}-final.state").exists())
            self.assertTrue(manifest["final_states"][name]["pruned_duplicate"])
        receipt = json.loads(Path(str(checkpoint) + ".json").read_text())
        self.assertTrue(receipt["validated"])
        self.assertEqual(receipt["master_cycle"], 40)
        self.assertEqual(receipt["state"], discovery.file_identity(checkpoint))

    def test_cold_pass_starts_before_checkpoint_wait_and_status_tracks_both(self):
        execute = self.execute

        def while_checkpoint_runs(command, **kwargs):
            if "--trace-from-cycle" in command:
                checkpoint = self.processes.get("checkpoint")
                self.assertIsNotNone(checkpoint, "checkpoint must already be running")
                checkpoint.wait.assert_not_called()
                report = discovery.campaign.status(self.output)
                self.assertEqual(report["state"], "running")
                self.assertEqual(report["active_stages"], ["checkpoint", "cold"])

                def join_checkpoint():
                    report = discovery.campaign.status(self.output)
                    self.assertEqual(report["active_stage"], "checkpoint")
                    self.assertEqual(report["active_stages"], ["checkpoint"])
                    return 0

                checkpoint.wait.side_effect = join_checkpoint
            return execute(command, **kwargs)

        with patch.object(self, "execute", side_effect=while_checkpoint_runs):
            self.create_checkpoint()
        for process in self.processes.values():
            process.wait.assert_called_once()
            process.kill.assert_not_called()
        self.assertTrue(all(entry["returncode"] is not None
                            for entry in self.read("manifest.json")["commands"]))

    def test_running_status_exposes_independent_stage_samples_and_final_progress_is_protected(self):
        execute = self.execute

        def while_checkpoint_runs(command, **kwargs):
            if "--trace-from-cycle" in command:
                report = discovery.campaign.status(self.output)
                checkpoint, cold = report["stage_progress"]
                self.assertEqual(checkpoint["sample"]["cycle_ceiling"], 40)
                self.assertEqual(checkpoint["sample"]["master_cycle"], 40)
                self.assertGreaterEqual(checkpoint["sample_age_seconds"], 0)
                self.assertGreaterEqual(cold["elapsed_wall_seconds"], 0)
                self.assertIsNone(cold["sample"])
            return execute(command, **kwargs)

        with patch.object(self, "execute", side_effect=while_checkpoint_runs):
            self.create_checkpoint()
        self.assertEqual(discovery.campaign.status(self.output)["state"], "execution_finished")
        (self.output / "cold.progress.jsonl").write_text("{}\n")
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")

    def test_cold_error_reaps_checkpoint_before_releasing_campaign_ownership(self):
        execute = self.execute

        def observe_checkpoint(command, **kwargs):
            if "--trace-from-cycle" in command:
                checkpoint = self.processes.get("checkpoint")
                self.assertIsNotNone(checkpoint, "checkpoint must already be running")
                checkpoint.kill.side_effect = lambda: self.assertEqual(
                    discovery.campaign.status(self.output)["state"], "running")
            return execute(command, **kwargs)

        self.run_statuses = [0, 2, 1, 1]
        with patch.object(self, "execute", side_effect=observe_checkpoint):
            self.invoke(status=2, checkpoint_at=40)
        checkpoint = self.processes["checkpoint"]
        checkpoint.kill.assert_called_once()
        checkpoint.wait.assert_called_once()
        self.assertEqual(self.run_count, 2)
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")
        self.assertFalse((self.output / "checkpoint.state.json").exists())

    def test_cold_launch_interrupt_reaps_checkpoint_and_preserves_partial_receipts(self):
        execute = self.execute

        def interrupt_cold_launch(command, **kwargs):
            if "--trace-from-cycle" in command:
                raise KeyboardInterrupt()
            return execute(command, **kwargs)

        with patch.object(self, "execute", side_effect=interrupt_cold_launch):
            self.invoke(status=130, checkpoint_at=40)
        checkpoint = self.processes.get("checkpoint")
        self.assertIsNotNone(checkpoint, "interrupted launch must reap its sibling")
        checkpoint.kill.assert_called_once()
        checkpoint.wait.assert_called_once()
        entries = self.read("manifest.json")["commands"]
        self.assertIsNotNone(entries[-2]["returncode"])
        self.assertIsNone(entries[-1]["returncode"])
        self.assertEqual(discovery.campaign.status(self.output)["state"], "interrupted")

    def test_validated_checkpoint_can_drive_the_regular_four_run_matrix(self):
        checkpoint = self.create_checkpoint()
        self.output = self.root / "reuse"
        self.run_count = 0
        self.run_statuses = [1] * 4
        self.calls.clear()
        self.invoke(checkpoint=checkpoint, failure_context=True)
        self.assertEqual(self.run_count, 4)
        for run in self.calls:
            if run[0] == str(self.binary):
                self.assertIn("--resume-checkpoint", run)
        self.assertTrue(self.read("compare-traces.json")["equal"])
        manifest = self.read("manifest.json")
        self.assertEqual(set(manifest["final_states"]), set(discovery.RUN_NAMES))
        baseline = self.output / "traced1-final.state"
        self.assertTrue(baseline.is_file())
        for name, state in manifest["final_states"].items():
            self.assertTrue(state["equal"])
            self.assertEqual(state["sha256"], discovery.file_identity(baseline)["sha256"])
            self.assertEqual(state.get("pruned_duplicate", False), name != "traced1")
            self.assertEqual((self.output / state["file"]).exists(), name == "traced1")
        self.assertEqual(discovery.campaign.status(self.output)["state"],
                         "execution_finished")
        baseline.write_bytes(b"changed canonical ending")
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")

    def test_reuse_canonical_mismatch_fails_even_when_reports_agree(self):
        checkpoint = self.create_checkpoint()
        self.output = self.root / "reuse-mismatch"
        self.run_count = 0
        self.run_statuses = [1] * 4
        self.change_canonical = True
        self.invoke(status=1, checkpoint=checkpoint)
        self.assertTrue(self.read("compare-traces.json")["equal"])
        self.assertTrue(self.read("compare-traced-plain.json")["equal"])
        self.assertTrue(self.read("compare-plain.json")["equal"])
        manifest = self.read("manifest.json")
        self.assertFalse(manifest["final_states"]["plain2"]["equal"])
        for state in manifest["final_states"].values():
            self.assertFalse(state.get("pruned_duplicate", False))
            self.assertTrue((self.output / state["file"]).is_file())
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")

    def test_checkpoint_identity_changes_are_rejected_before_bios_reuse(self):
        for changed in ("state", "bios", "binary"):
            with self.subTest(changed=changed):
                self.output = self.root / f"create-{changed}"
                self.run_count = 0
                self.change_plain = False
                checkpoint = self.create_checkpoint()
                path = dict(state=checkpoint, bios=self.bios,
                            binary=self.binary)[changed]
                original = path.read_bytes()
                path.write_bytes(original + b"changed")
                self.output = self.root / f"rejected-{changed}"
                self.run_count = 0
                self.invoke(status=2, checkpoint=checkpoint)
                self.assertEqual(self.run_count, 0)
                path.write_bytes(original)

    def test_failed_checkpoint_equivalence_retains_all_states_and_no_reuse_receipt(self):
        self.change_plain = True
        self.run_statuses = [0, 1, 1, 1]
        self.invoke(status=1, checkpoint_at=40)
        self.assertEqual(self.read("manifest.json")["status"], "mismatch")
        self.assertFalse((self.output / "checkpoint.state.json").exists())
        for name in ("cold", "resumed", "resumed-plain"):
            self.assertTrue((self.output / f"{name}-final.state").is_file())

    def test_checkpoint_host_stop_is_not_a_usable_checkpoint(self):
        self.run_statuses = [1] * 4
        self.invoke(status=2, checkpoint_at=40)
        self.assertEqual(self.run_count, 2)
        self.assertFalse((self.output / "checkpoint.state.json").exists())

    def test_fixed_matrix_and_real_inspection_preserve_artifacts(self):
        self.invoke()
        runs = [command for command in self.calls if command[0] == str(self.binary)]
        base = [str(self.binary), "bios-startup", str(self.bios),
                "--cycles", "1000000"]
        expected = [
            base + ["--trace-events", "3000000"],
            base + ["--trace-events", "3000000"], base, base,
        ]
        self.assertEqual(runs, [
            command + ["--save-checkpoint", str(self.output / f"{name}-final.state"),
                       "--progress-file", str(self.output / f"{name}.progress.jsonl")]
            for name, command in zip(discovery.RUN_NAMES, expected)])
        self.assertEqual(self.calls[0], [
            "cmake", "--build", str(self.root / "out" / "check"),
            "--target", "neko", "--config", "Debug", "-j", "4",
        ])
        self.assertEqual(self.read("summary.json")["first_failure"]["master_cycle"], 49)
        self.assertTrue(self.read("compare-traces.json")["byte_equal"])
        self.assertTrue(self.read("compare-traced-plain.json")["equal"])
        self.assertTrue(self.read("compare-plain.json")["equal"])
        context = self.read("first-failure-events.json")
        self.assertTrue(all(row["cpu"] == "iop" for row in context["events"]))
        instructions = [command for command in self.calls if "instructions" in command]
        self.assertEqual(instructions[0][-8:], [
            "--cpu", "iop", "--from-cycle", "33", "--to-cycle", "65",
            "--limit", "200",
        ])
        manifest = self.read("manifest.json")
        self.assertEqual(manifest["status"], "equal")
        for index, name in enumerate(("traced1", "traced2", "plain1", "plain2")):
            self.assertTrue(discovery.report_path(self.output, name).is_file())
            self.assertEqual((self.output / f"{name}.stderr").read_text(),
                             f"authored stderr {index}\n")
            entry, = [step for step in manifest["commands"] if step["name"] == name]
            self.assertEqual(entry["returncode"], 0)
            self.assertEqual(entry["command"], runs[index])

    def test_expected_host_stops_do_not_skip_repetitions_or_inspection(self):
        self.run_statuses = [1, 1, 1, 1]
        self.invoke()
        self.assertEqual(self.run_count, 4)
        manifest = self.read("manifest.json")
        self.assertEqual([entry["returncode"] for entry in manifest["commands"]
                          if entry["name"].startswith(("traced", "plain"))],
                         [1, 1, 1, 1])
        self.assertEqual(self.read("summary.json")["run"]["master_cycles"], 50)

    def test_explicit_budget_reaches_every_run_and_manifest_with_capture(self):
        self.invoke(cycles=10000000, failure_context=True)
        runs = [command for command in self.calls if command[0] == str(self.binary)]
        base = [str(self.binary), "bios-startup", str(self.bios),
                "--cycles", "10000000"]
        traced = base + ["--trace-events", "3000000", "--failure-context"]
        self.assertEqual(runs, [
            command + ["--save-checkpoint", str(self.output / f"{name}-final.state"),
                       "--progress-file", str(self.output / f"{name}.progress.jsonl")]
            for name, command in zip(discovery.RUN_NAMES, (traced, traced, base, base))])
        self.assertEqual(self.read("manifest.json")["max_master_cycles"], 10000000)
        self.assertEqual(self.read("summary.json")["input"]["max_master_cycles"],
                         10000000)
        self.assertTrue(self.read("compare-traces.json")["equal"])
        self.assertTrue(self.read("compare-traced-plain.json")["equal"])
        self.assertEqual(self.read("first-failure-events.json")["event_source"],
                         "failure_capture")

    def test_explicit_budget_accepts_cli_unsigned_boundaries(self):
        for cycles in (0, (1 << 64) - 1):
            with self.subTest(cycles=cycles):
                self.output = self.root / str(cycles)
                self.run_count = 0
                self.invoke(cycles=cycles)
                self.assertEqual(self.run_count, 4)
                self.assertEqual(self.read("manifest.json")["max_master_cycles"],
                                 cycles)
                self.assertEqual(self.read("summary.json")["input"]["max_master_cycles"],
                                 cycles)

    def test_out_of_range_budget_fails_before_build_or_artifact_creation(self):
        for cycles in (-1, 1 << 64):
            with self.subTest(cycles=cycles):
                _, errors = self.invoke(status=2, cycles=cycles)
                self.assertIn("unsigned 64-bit", errors)
                self.assertEqual(self.calls, [])
                self.assertFalse(self.output.exists())

    def test_opt_in_capture_keeps_untraced_runs_plain_and_selects_failure_context(self):
        self.invoke(failure_context=True)
        runs = [command for command in self.calls if command[0] == str(self.binary)]
        self.assertTrue(all("--failure-context" in command for command in runs[:2]))
        for command in runs[2:]:
            self.assertNotIn("--failure-context", command)
            self.assertNotIn("--trace-events", command)
        context = self.read("first-failure-events.json")
        self.assertEqual(context["event_source"], "failure_capture")
        self.assertEqual(context["events"][0]["master_cycle"], 49)
        self.assertEqual(context["events"][0]["global_event_index"], 92)
        self.assertTrue(self.read("compare-traces.json")["equal"])
        self.assertTrue(self.read("compare-traced-plain.json")["equal"])
        self.assertTrue(self.read("manifest.json")["failure_context"])

    def test_unexpected_run_error_is_recorded_and_aborts(self):
        self.run_statuses[0] = 2
        _, errors = self.invoke(status=2)
        self.assertIn("traced1", errors)
        self.assertEqual(self.run_count, 1)
        manifest = self.read("manifest.json")
        self.assertEqual(manifest["status"], "error")
        self.assertEqual(manifest["commands"][-1]["returncode"], 2)
        self.assertTrue(discovery.report_path(self.output, "traced1").is_file())
        self.assertFalse(discovery.report_path(self.output, "traced2").exists())

    def test_signal_exit_is_not_an_expected_host_stop(self):
        self.run_statuses[0] = -9
        self.invoke(status=2)
        self.assertEqual(self.run_count, 1)
        self.assertEqual(self.read("manifest.json")["commands"][-1]["returncode"], -9)

    def test_launch_error_preserves_pending_command_and_explicit_error(self):
        self.run_error = OSError("authored launch failure")
        _, errors = self.invoke(status=2)
        self.assertIn("authored launch failure", errors)
        manifest = self.read("manifest.json")
        self.assertEqual(manifest["status"], "error")
        self.assertIsNone(manifest["commands"][-1]["returncode"])

    def test_interruption_preserves_partial_artifacts(self):
        self.run_error = KeyboardInterrupt()
        self.invoke(status=130)
        manifest = self.read("manifest.json")
        self.assertEqual(manifest["status"], "error")
        self.assertEqual(manifest["error"], "interrupted")
        self.assertEqual(self.run_count, 1)

    def test_existing_directory_is_never_overwritten(self):
        self.output.mkdir()
        sentinel = self.output / "manifest.json"
        sentinel.write_text("keep this evidence", encoding="utf-8")
        self.invoke(status=2)
        self.assertEqual(self.calls, [])
        self.assertEqual(sentinel.read_text(), "keep this evidence")

    def test_missing_bios_fails_before_build_or_capture(self):
        self.bios.unlink()
        self.invoke(status=2)
        self.assertEqual(self.calls, [])
        self.assertFalse(self.output.exists())

    def test_build_failure_does_not_run_a_stale_binary(self):
        self.build_status = 1
        self.invoke(status=2)
        self.assertEqual(self.run_count, 0)
        self.assertEqual(self.read("manifest.json")["status"], "error")

    def test_absent_configuration_is_configured_before_target_build(self):
        (self.root / "out" / "check" / "CMakeCache.txt").unlink()
        self.invoke()
        self.assertEqual(self.calls[0], [
            "cmake", "-S", str(self.root), "-B", str(self.root / "out" / "check"),
            "-DCMAKE_BUILD_TYPE=Debug", "-DNEKO_OPTIMIZE_CHECKS=ON",
        ])
        self.assertIn("--build", self.calls[1])

    def test_debug_configuration_binary_is_supported(self):
        self.binary.unlink()
        self.binary = self.binary.parent / "Debug" / discovery.NEKO_NAME
        self.binary.parent.mkdir()
        self.binary.touch()
        self.binary.chmod(0o755)
        self.invoke()
        self.assertEqual(self.run_count, 4)

    def test_comparison_mismatch_preserves_remaining_checks_and_context(self):
        self.change_plain = True
        self.invoke(status=1)
        self.assertFalse(self.read("compare-traced-plain.json")["equal"])
        self.assertFalse(self.read("compare-plain.json")["equal"])
        self.assertTrue((self.output / "first-failure-events.json").is_file())
        self.assertEqual(self.read("manifest.json")["status"], "mismatch")

    def test_no_captured_failure_is_explicit_not_a_boot_verdict(self):
        self.failure = False
        output, _ = self.invoke()
        report = json.loads(output)
        self.assertIsNone(report["first_failure"])
        self.assertIn("handshake", report["note"])
        self.assertFalse(any("instructions" in command for command in self.calls))
        self.assertFalse((self.output / "first-failure-events.json").exists())

    def test_dma_failure_context_is_not_incorrectly_cpu_filtered(self):
        self.cpu = None
        self.invoke()
        commands = [command for command in self.calls if "events" in command]
        self.assertNotIn("--cpu", commands[0])

    def test_invalid_capture_is_an_inspection_error_not_success(self):
        self.malformed = True
        self.invoke(status=2)
        self.assertEqual(self.run_count, 4)
        self.assertEqual(self.read("manifest.json")["status"], "error")
        self.assertTrue((self.output / "summary.stderr").read_text())

    def test_execution_completion_requires_explicit_inspection_and_outer_exit(self):
        self.invoke()
        report = discovery.campaign.status(self.output)
        self.assertEqual(report["state"], "execution_finished")
        self.assertEqual(report["scope"], "conformance")
        receipt = self.read("execution.json")
        self.assertEqual(receipt["returncode"], 0)
        self.assertIn(str(self.bios), receipt["command"])
        for name in ("verification-notes.txt", "review.txt", "cleanup.receipt"):
            (self.output / name).write_text(f"Authored {name}\n")
        with self.assertRaisesRegex(ValueError, "outer exit"):
            discovery.campaign.finalize(self.output, 2)
        self.assertEqual(discovery.campaign.status(self.output)["state"],
                         "execution_finished")
        discovery.campaign.finalize(self.output, 0)
        self.assertEqual(discovery.campaign.status(self.output)["state"], "verified")
        discovery.campaign.finalize(self.output, 0)  # Idempotent, not a second run.
        self.assertEqual(self.run_count, 4)
        (self.output / "review.txt").write_text("Changed after finalization\n")
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")

    def test_checkpoint_finalization_checks_retained_canonical_bytes(self):
        self.create_checkpoint()
        self.assertEqual(discovery.campaign.status(self.output)["state"],
                         "execution_finished")
        path = self.output / "cold-final.state"
        data = path.read_bytes()
        path.write_bytes(bytes([data[0] ^ 1]) + data[1:])
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")

    def test_invalid_nested_canonical_receipt_fails_closed(self):
        self.create_checkpoint()
        manifest = self.read("manifest.json")
        manifest["final_states"]["cold"] = None
        discovery.write_json(self.output / "manifest.json", manifest)
        execution = self.read("execution.json")
        execution["manifest"] = discovery.file_identity(self.output / "manifest.json")
        discovery.write_json(self.output / "execution.json", execution)
        report = discovery.campaign.status(self.output)
        self.assertEqual(report["state"], "failed")
        self.assertIn("Invalid canonical", report["reason"])

    def test_status_cli_cannot_launch_a_campaign_and_rejects_run_options(self):
        output = io.StringIO()
        with redirect_stdout(output):
            result = discovery.main([str(self.output), "--status"])
        self.assertEqual(result, 2)
        self.assertEqual(json.loads(output.getvalue())["state"], "unknown")
        self.assertFalse(self.output.exists())
        self.assertEqual(self.calls, [])
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            discovery.main([str(self.output), "--status", "--cycles", "10"])

    def test_finalization_refuses_missing_inspection_evidence(self):
        self.invoke()
        with self.assertRaisesRegex((OSError, ValueError), "verification-notes"):
            discovery.campaign.finalize(self.output, 0)
        self.assertFalse((self.output / "inspection.json").exists())

    def test_missing_or_changed_terminal_evidence_never_reports_success(self):
        self.invoke()
        comparison = self.output / "compare-traces.json"
        original = comparison.read_bytes()
        for content in (b"{", b'{"equal": false}\n'):
            with self.subTest(content=content):
                comparison.write_bytes(content)
                self.assertEqual(discovery.campaign.status(self.output)["state"],
                                 "failed")
        comparison.write_bytes(original)
        self.assertEqual(discovery.campaign.status(self.output)["state"],
                         "execution_finished")
        execution = self.read("execution.json")
        execution["returncode"] = None
        discovery.write_json(self.output / "execution.json", execution)
        self.assertEqual(discovery.campaign.status(self.output)["state"], "interrupted")

    def test_malformed_summary_and_truncated_native_output_fail_closed(self):
        self.invoke()
        for name in ("summary.json", "traced1.jsonl.gz"):
            with self.subTest(name=name):
                path = self.output / name
                original = path.read_bytes()
                path.write_bytes(b"[" + original[1:] if name.endswith(".json")
                                 else original[:-1])
                self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")
                path.write_bytes(original)

    def test_completion_between_receipt_read_and_lock_probe_uses_terminal_receipt(self):
        self.invoke()
        initial = dict(self.read("execution.json"), returncode=None)
        read_json = discovery.campaign.read_json
        execution_reads = 0

        def read_before_release(path):
            nonlocal execution_reads
            if path.name == "execution.json":
                execution_reads += 1
                if execution_reads == 1:
                    return initial
            return read_json(path)

        with patch.object(discovery.campaign, "read_json",
                          side_effect=read_before_release):
            report = discovery.campaign.status(self.output)
        self.assertEqual(report["state"], "execution_finished")

    def test_error_and_interrupt_are_terminal_but_not_verified(self):
        self.build_status = 1
        self.invoke(status=2)
        self.assertEqual(discovery.campaign.status(self.output)["state"], "failed")
        self.output = self.root / "interrupted"
        self.build_status = 0
        self.run_error = KeyboardInterrupt()
        self.invoke(status=130)
        self.assertEqual(discovery.campaign.status(self.output)["state"], "interrupted")

    def test_legacy_manifest_and_malformed_receipt_are_unknown_not_verified(self):
        self.output.mkdir()
        discovery.write_json(self.output / "manifest.json", dict(status="equal"))
        self.assertEqual(discovery.campaign.status(self.output)["state"], "unknown")
        (self.output / "execution.json").write_text("{")
        self.assertEqual(discovery.campaign.status(self.output)["state"], "unknown")

    def test_atomic_manifest_failure_preserves_previous_readable_receipt(self):
        self.output.mkdir()
        path = self.output / "manifest.json"
        discovery.write_json(path, dict(status="running"))
        with patch.object(discovery.campaign.os, "replace",
                          side_effect=OSError("authored replacement failure")):
            with self.assertRaisesRegex(OSError, "replacement failure"):
                discovery.write_json(path, dict(status="equal"))
        self.assertEqual(self.read("manifest.json"), dict(status="running"))
        self.assertEqual(list(self.output.iterdir()), [path])


class CampaignOwnershipTests(unittest.TestCase):
    def test_progress_tail_is_bounded_and_ignores_only_an_unfinished_record(self):
        sample = dict(format="neko-bios-progress-v1", phase="execution",
                      master_cycle=16, start_master_cycle=8, cycle_ceiling=100,
                      elapsed_wall_seconds=2.0, elapsed_cpu_seconds=1.5,
                      cycles_per_second=4.0, sampled_at_unix_seconds=1000.0)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            path = directory / "cold.progress.jsonl"
            row = json.dumps(sample) + "\n"
            path.write_text(row * 1000 + '{"format":')
            self.assertEqual(discovery.campaign.read_progress(directory, path.name), sample)
            path.write_text(row + '{"format":"broken"}\n')
            with self.assertRaisesRegex(ValueError, "progress"):
                discovery.campaign.read_progress(directory, path.name)
            for field, value in (("master_cycle", 101), ("elapsed_wall_seconds", float("nan")),
                                 ("elapsed_cpu_seconds", -1), ("master_cycle", True),
                                 ("cycles_per_second", 10 ** 1000)):
                with self.subTest(field=field, value=value):
                    path.write_text(json.dumps(dict(sample, **{field: value})) + "\n")
                    with self.assertRaisesRegex(ValueError, "progress"):
                        discovery.campaign.read_progress(directory, path.name)
            with self.assertRaisesRegex(ValueError, "artifact"):
                discovery.campaign.read_progress(directory, "../outside.progress.jsonl")
            self.assertIsNone(discovery.campaign.read_progress(directory, "not-published.jsonl"))

    def test_live_ownership_and_abrupt_death_are_not_inferred_from_a_pid(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            program = (
                "import sys; from pathlib import Path; import bios_campaign as c; "
                "d = Path(sys.argv[1]); "
                "c.write_json(d / 'manifest.json', "
                "{'status': 'running', 'commands': []}); "
                "owner = c.Owner(d); print('ready', flush=True); sys.stdin.read()")
            process = REAL_POPEN(
                [sys.executable, "-B", "-c", program, str(directory)],
                cwd=Path(discovery.__file__).parent, stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                self.assertEqual(process.stdout.readline().strip(), "ready")
                self.assertEqual(discovery.campaign.status(directory)["state"], "running")
                with self.assertRaisesRegex(ValueError, "already owned"):
                    discovery.campaign.Owner(directory)
                with self.assertRaisesRegex(ValueError, "running"):
                    discovery.campaign.finalize(directory, 0)
                process.kill()
                process.wait(timeout=5)
                self.assertEqual(discovery.campaign.status(directory)["state"],
                                 "interrupted")
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main()
