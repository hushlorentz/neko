"""BIOS-free tests for the repeatable startup discovery workflow."""

from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import run_bios_discovery as discovery
from test_inspect_startup import capture_records, records


REAL_RUN = subprocess.run


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
            rows[0]["max_master_cycles"] = 1000000
            if self.run_statuses[index] == 0:
                rows[1]["run"].update(master_cycles=1000000,
                                      cycle_limit_reached=True)
                rows[1]["snapshot"]["master_cycle"] = 1000000
            if self.change_plain and index == 2:
                rows[1]["snapshot"]["ee"]["pc"] += 4
            if "--trace-events" in command:
                if not self.failure:
                    rows[-1]["failure"] = None
                else:
                    rows[-1]["failure"]["cpu"] = self.cpu
            raw = ("not json\n" if self.malformed else
                   "".join(json.dumps(row) + "\n" for row in rows))
            kwargs["stdout"].write(raw.encode("utf-8"))
            kwargs["stderr"].write(f"authored stderr {index}\n".encode("utf-8"))
            return subprocess.CompletedProcess(command, self.run_statuses[index])
        return REAL_RUN(command, **kwargs)

    def invoke(self, status=0, *, failure_context=False):
        output = io.StringIO()
        errors = io.StringIO()
        with patch.object(discovery.subprocess, "run", side_effect=self.execute):
            with redirect_stdout(output), redirect_stderr(errors):
                arguments = [str(self.output), "--bios", str(self.bios)]
                if failure_context:
                    arguments.append("--failure-context")
                result = discovery.main(arguments)
        self.assertEqual(result, status, errors.getvalue())
        return output.getvalue(), errors.getvalue()

    def read(self, name):
        return json.loads((self.output / name).read_text(encoding="utf-8"))

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
            self.assertTrue((self.output / f"{name}.jsonl").is_file())
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
        self.assertTrue((self.output / "traced1.jsonl").is_file())
        self.assertFalse((self.output / "traced2.jsonl").exists())

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
