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

    def fake_capture(self, directory, manifest, name, command, **kwargs):
        if command[0] != str(self.binary):
            return self.capture(directory, manifest, name, command, **kwargs)
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
        if "--save-checkpoint" in command:
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
        manifest["commands"].append(dict(name=name, command=command,
                                         returncode=status, elapsed_seconds=0.1))
        discovery.write_json(directory / "manifest.json", manifest)
        return status

    def invoke(self, expected=0, *, ceiling=30):
        with patch.object(discovery, "build_neko", return_value=self.binary), \
             patch.object(discovery, "capture", side_effect=self.fake_capture), \
             redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            status = discovery.main([
                str(self.output), "--bios", str(self.bios),
                "--mode", "scout", "--cycles", str(ceiling),
                "--checkpoint-every", "10", "--replay-window", "3"])
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

    def test_scout_replay_disagreement_is_preserved_as_a_mismatch(self):
        self.replay_mismatch = True
        manifest = self.invoke(expected=1)
        self.assertEqual(manifest["status"], "mismatch")
        self.assertFalse(manifest["failure_reproduced"])
        self.assertTrue((self.output / "scout-10.state").is_file())

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
        self.run_statuses = [0, 0, 0, 0]
        self.run_count = 0
        self.build_status = 0
        self.failure = True
        self.change_plain = False
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
                path.write_text(json.dumps(rows[1]["snapshot"], sort_keys=True))
            raw = ("not json\n" if self.malformed else
                   "".join(json.dumps(row) + "\n" for row in rows))
            kwargs["stdout"].write(raw.encode("utf-8"))
            kwargs["stderr"].write(f"authored stderr {index}\n".encode("utf-8"))
            return subprocess.CompletedProcess(command, self.run_statuses[index])
        return REAL_RUN(command, **kwargs)

    def spawn(self, command, **kwargs):
        if command[0] != str(self.binary):
            return REAL_POPEN(command, **kwargs)
        output = io.BytesIO()
        result = self.execute(command, cwd=kwargs["cwd"], stdout=output,
                              stderr=kwargs["stderr"])
        output.seek(0)
        return Mock(stdout=output, wait=Mock(return_value=result.returncode),
                    kill=Mock())

    def invoke(self, status=0, *, failure_context=False, cycles=None,
               checkpoint_at=None, checkpoint=None):
        output = io.StringIO()
        errors = io.StringIO()
        with patch.object(discovery.subprocess, "run", side_effect=self.execute), \
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
                result = discovery.main(arguments)
        self.assertEqual(result, status, errors.getvalue())
        return output.getvalue(), errors.getvalue()

    def read(self, name):
        return json.loads((self.output / name).read_text(encoding="utf-8"))

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
        self.assertEqual(self.run_count, 1)
        self.assertFalse((self.output / "checkpoint.state.json").exists())

    def test_fixed_matrix_and_real_inspection_preserve_artifacts(self):
        self.invoke()
        runs = [command for command in self.calls if command[0] == str(self.binary)]
        base = [str(self.binary), "bios-startup", str(self.bios),
                "--cycles", "1000000"]
        self.assertEqual(runs, [
            base + ["--trace-events", "3000000"],
            base + ["--trace-events", "3000000"], base, base,
        ])
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
        self.assertEqual(runs, [traced, traced, base, base])
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
        self.assertEqual([command[-1] for command in runs[:2]],
                         ["--failure-context", "--failure-context"])
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


if __name__ == "__main__":
    unittest.main()
