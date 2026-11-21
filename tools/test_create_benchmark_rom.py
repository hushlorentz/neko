"""Authored benchmark ROM correctness, without retail BIOS input."""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

import create_benchmark_rom as benchmark
from inspect_startup import load_log


class BenchmarkROMTests(unittest.TestCase):
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
