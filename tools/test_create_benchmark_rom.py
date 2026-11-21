"""Authored benchmark ROM correctness, without retail BIOS input."""

from copy import deepcopy
import filecmp
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

import create_benchmark_rom as benchmark
from inspect_startup import compare, load_log


class BenchmarkROMTests(unittest.TestCase):
    def test_cop1_conformance_rom_is_finite_and_deterministic(self):
        self.assertEqual(benchmark.create_rom("cop1", 3), benchmark.create_rom("cop1", 3))
        self.assertEqual(len(benchmark.create_rom("cop1", 3)), benchmark.ROM_BYTES)
        with self.assertRaises(ValueError):
            benchmark.create_rom("cop1")

    def capture_cop1(self, binary, root, rom, name, cycles=512, options=()):
        state, log = root / f"{name}.state", root / f"{name}.jsonl"
        run = subprocess.run([
            str(binary), "bios-startup", str(rom), "--cycles", str(cycles),
            *options, "--save-checkpoint", str(state)], capture_output=True)
        self.assertEqual(run.returncode, 0, (name, run.stderr))
        self.assertEqual(run.stderr, b"", name)
        log.write_bytes(run.stdout)
        report = load_log(log)
        self.assertEqual(report.result["snapshot"]["master_cycle"], cycles)
        self.assertEqual(report.result["type"], "startup_run_summary")
        if report.failure_record is not None:
            self.assertIsNone(report.failure_record["failure"])
        self.assertTrue(report.result["run"]["cycle_limit_reached"])
        if report.trace is not None:
            self.assertFalse(report.trace["truncated"], name)
        return report, state

    def require_cop1_equal(self, first, second, mode):
        comparison = compare(first[0], second[0], mode)
        self.assertTrue(comparison["equal"], comparison)
        self.assertTrue(filecmp.cmp(first[1], second[1], shallow=False),
                        (first[1], second[1]))

    def cop1_matrix(self, binary, root, rom, checkpoint_cycle=None):
        root.mkdir()
        plain = self.capture_cop1(binary, root, rom, "plain")
        traced = self.capture_cop1(
            binary, root, rom, "traced", options=("--trace-events", "65536"))
        self.require_cop1_equal(traced, plain, "run")
        ee_events = [event for event in traced[0].events if event["subsystem"] == 10]
        # EE trace identities: load interlock 11, stage transition 14, retirement 15.
        self.assertTrue(any(event["event_type"] == 11 for event in ee_events))
        load_stages = [
            event for event in ee_events if event["event_type"] == 14 and
            event["values"][1] >> 58 == 0x31]
        self.assertEqual(len({event["values"][0] for event in load_stages}), 3)
        if checkpoint_cycle is None:
            checkpoint_cycle = load_stages[0]["master_cycle"]
        producer_order = load_stages[0]["values"][0]
        self.assertTrue(any(event["event_type"] == 15 and
                            event["values"][0] == producer_order and
                            event["master_cycle"] > checkpoint_cycle
                            for event in ee_events))
        checkpoint = self.capture_cop1(
            binary, root, rom, "checkpoint", cycles=checkpoint_cycle)
        tail = self.capture_cop1(
            binary, root, rom, "tail", options=(
                "--trace-events", "65536", "--trace-from-cycle", str(checkpoint_cycle)))
        resumed = self.capture_cop1(
            binary, root, rom, "resumed", options=(
                "--resume-checkpoint", str(checkpoint[1]), "--trace-events", "65536"))
        resumed_plain = self.capture_cop1(
            binary, root, rom, "resumed-plain", options=(
                "--resume-checkpoint", str(checkpoint[1]),))
        self.require_cop1_equal(tail, resumed, "continuation-trace")
        self.require_cop1_equal(resumed, resumed_plain, "continuation")
        self.require_cop1_equal(plain, resumed_plain, "continuation")
        for cpu, expected in (
                ("ee", [0x4e454b4f, 3, 9, 0x40400000]),
                ("iop", [0x4e454b4f, 3, 9, 3 ^ 9])):
            result = subprocess.run([
                str(binary), "save-state", f"{cpu}-memory", str(plain[1]),
                "0x2000", "--words", "4"], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            words = [int(value, 16) for value in re.findall(
                rb"ram=(0x[0-9a-fA-F]+)", result.stdout)]
            self.assertEqual(words, expected, cpu)
        result = subprocess.run([
            str(binary), "save-state", "ee-memory", str(plain[1]),
            "0x4000", "--words", "2"], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        words = [int(value, 16) for value in re.findall(
            rb"ram=(0x[0-9a-fA-F]+)", result.stdout)]
        self.assertEqual(words, [0x3f800000, 0x40400000])
        return checkpoint_cycle, {
            "plain": plain, "traced": traced, "checkpoint": checkpoint,
            "tail": tail, "resumed": resumed, "resumed-plain": resumed_plain}

    @unittest.skipUnless(os.environ.get("NEKO_TEST_BINARY"), "Requires existing native executable")
    def test_cop1_guest_matches_traced_plain_and_active_restore(self):
        binary = Path(os.environ["NEKO_TEST_BINARY"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rom = root / "cop1.bin"
            rom.write_bytes(benchmark.create_rom("cop1", 3))
            _, captures = self.cop1_matrix(binary, root / "current", rom)
            changed_trace = deepcopy(captures["traced"][0])
            changed_trace.events[0]["values"][0] ^= 1
            with self.assertRaises(AssertionError):
                self.require_cop1_equal(
                    captures["traced"], (changed_trace, captures["traced"][1]), "trace")
            changed_state = root / "changed.state"
            shutil.copyfile(captures["plain"][1], changed_state)
            with changed_state.open("r+b") as output:
                output.seek(1024)
                value = output.read(1)[0]
                output.seek(1024)
                output.write(bytes([value ^ 1]))
            with self.assertRaises(AssertionError):
                self.require_cop1_equal(
                    captures["plain"], (captures["plain"][0], changed_state), "run")

    @unittest.skipUnless(os.environ.get("NEKO_TEST_BINARY") and
                         os.environ.get("NEKO_REFERENCE_BINARY"),
                         "Requires current and explicitly supplied frozen reference executables")
    def test_cop1_guest_matches_frozen_reference_at_identical_boundaries(self):
        current = Path(os.environ["NEKO_TEST_BINARY"]).resolve()
        reference = Path(os.environ["NEKO_REFERENCE_BINARY"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rom = root / "cop1.bin"
            rom.write_bytes(benchmark.create_rom("cop1", 3))
            cycle, baseline = self.cop1_matrix(reference, root / "reference", rom)
            _, candidate = self.cop1_matrix(current, root / "current", rom, cycle)
            for name in baseline:
                with self.subTest(capture=name):
                    mode = "trace" if baseline[name][0].trace is not None else "run"
                    self.require_cop1_equal(baseline[name], candidate[name], mode)

    def test_generator_is_deterministic_and_refuses_invalid_requests(self):
        for kind in benchmark.KINDS:
            self.assertEqual(benchmark.create_rom(kind, 8), benchmark.create_rom(kind, 8))
            self.assertEqual(len(benchmark.create_rom(kind)), 4194304)
        for kind, iterations in (("unknown", 1), ("cpu", 0), ("ram", -1), ("devices", 65536)):
            with self.assertRaises(ValueError):
                benchmark.create_rom(kind, iterations)

    @unittest.skipUnless(os.environ.get("NEKO_TEST_BINARY"), "Requires existing native executable")
    def test_finite_controls_publish_independent_results_on_both_cpus(self):
        binary = Path(os.environ["NEKO_TEST_BINARY"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for kind, iterations in [(kind, 8) for kind in benchmark.KINDS] + [("ram", 32)]:
                with self.subTest(kind=kind, iterations=iterations):
                    rom, state, log = (root / f"{kind}-{iterations}.{suffix}"
                                       for suffix in ("bin", "state", "jsonl"))
                    rom.write_bytes(benchmark.create_rom(kind, iterations))
                    run = subprocess.run([
                        str(binary), "bios-startup", str(rom), "--cycles", "4096",
                        "--save-checkpoint", str(state)], capture_output=True)
                    self.assertEqual(run.returncode, 0, run.stderr)
                    log.write_bytes(run.stdout)
                    snapshot = load_log(log).result["snapshot"]
                    self.assertEqual(snapshot["master_cycle"], 4096)
                    for cpu in ("ee", "iop"):
                        self.assertEqual(snapshot[cpu]["stop_reason"], "none")
                        result = subprocess.run([
                            str(binary), "save-state", f"{cpu}-memory", str(state),
                            "0x2000", "--words", "4"], capture_output=True)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        words = [int(value, 16) for value in re.findall(
                            rb"ram=(0x[0-9a-fA-F]+)", result.stdout)]
                        previous = max(0, iterations - 16) if kind == "ram" else iterations * 3
                        self.assertEqual(words, [0x4e454b4f, iterations, iterations * 3,
                                                 previous ^ iterations], (cpu, snapshot[cpu]))
                    if kind == "ram":
                        for cpu in ("ee", "iop"):
                            result = subprocess.run([
                                str(binary), "save-state", f"{cpu}-memory", str(state),
                                "0x4000", "--words", "16"], capture_output=True)
                            self.assertEqual(result.returncode, 0, result.stderr)
                            words = [int(value, 16) for value in re.findall(
                                rb"ram=(0x[0-9a-fA-F]+)", result.stdout)]
                            expected = (list(range(1, 9)) + [0] * 8 if iterations == 8 else
                                        list(range(17, 33)))
                            self.assertEqual(words, expected)
                    if kind == "devices":
                        result = subprocess.run([
                            str(binary), "save-state", "ee-memory", str(state),
                            "0x4000", "--words", "16"], capture_output=True)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        words = [int(value, 16) for value in re.findall(
                            rb"ram=(0x[0-9a-fA-F]+)", result.stdout)]
                        self.assertEqual(words, [0x12340000 + index for index in range(16)])
                        self.assertNotEqual(snapshot["ee_interrupts"]["status"] & 0x200, 0)
                        self.assertNotEqual(snapshot["iop_interrupts"]["status"] & 0x10, 0)


if __name__ == "__main__":
    unittest.main()
