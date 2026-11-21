"""Opt-in BIOS-free end-to-end coverage against the built headless executable."""

from contextlib import redirect_stderr, redirect_stdout
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import run_bios_discovery as discovery
from inspect_startup import first_difference


@unittest.skipUnless(os.environ.get("NEKO_TEST_BINARY"),
                     "Set NEKO_TEST_BINARY to the existing headless executable")
class ScoutIntegrationTests(unittest.TestCase):
    def test_native_progress_is_observational_across_trace_and_resume_boundaries(self):
        binary = Path(os.environ["NEKO_TEST_BINARY"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bios = root / "authored.bin"
            bios.write_bytes(bytes(4194304))
            checkpoint = root / "prefix.state"
            prepared = subprocess.run(
                [str(binary), "bios-startup", str(bios), "--cycles", "8",
                 "--save-checkpoint", str(checkpoint)], capture_output=True)
            self.assertEqual(prepared.returncode, 0, prepared.stderr)
            profiles = (
                [], ["--trace-events", "0"], ["--trace-events", "1"],
                ["--trace-events", "256", "--failure-context"],
                ["--trace-events", "256", "--failure-context", "--trace-from-cycle", "3"],
                ["--trace-events", "1", "--trace-from-cycle", "3"],
                ["--resume-checkpoint", str(checkpoint)],
                ["--resume-checkpoint", str(checkpoint), "--trace-events", "1"],
                ["--scout"],
            )
            canonical_identity = None
            for index, options in enumerate(profiles):
                with self.subTest(options=options):
                    states = [root / f"{index}-{mode}.state" for mode in ("plain", "progress")]
                    command = [str(binary), "bios-startup", str(bios), "--cycles", "24"]
                    plain = subprocess.run(
                        command + options + ["--save-checkpoint", str(states[0])],
                        capture_output=True)
                    progress = root / f"{index}.progress.jsonl"
                    observed = subprocess.run(
                        command + options + ["--save-checkpoint", str(states[1]),
                        "--progress-file", str(progress), "--progress-every", "8"],
                        capture_output=True)
                    self.assertEqual(observed.returncode, plain.returncode, observed.stderr)
                    self.assertEqual(plain.returncode, 0, plain.stderr)
                    self.assertEqual(observed.stdout, plain.stdout)
                    self.assertEqual(observed.stderr, plain.stderr)
                    identity = discovery.file_identity(states[0])
                    if canonical_identity is None:
                        canonical_identity = identity
                    self.assertEqual(identity, canonical_identity)
                    self.assertEqual(discovery.file_identity(states[1]), canonical_identity)
                    samples = [json.loads(line) for line in progress.read_text().splitlines()]
                    start = 8 if "--resume-checkpoint" in options else 0
                    execution = [sample for sample in samples if sample["phase"] == "execution"]
                    expected = ([0, 3, 11, 19, 24] if "--trace-from-cycle" in options else
                                list(range(start, 25, 8)))
                    self.assertEqual([sample["master_cycle"] for sample in execution],
                                     expected)
                    self.assertEqual(samples[-1]["phase"], "finished")
                    for sample in samples:
                        self.assertEqual(sample["format"], "neko-bios-progress-v1")
                        self.assertEqual(sample["start_master_cycle"], start)
                        self.assertEqual(sample["cycle_ceiling"], 24)
                        self.assertGreaterEqual(sample["elapsed_wall_seconds"], 0)
                        self.assertGreaterEqual(sample["elapsed_cpu_seconds"], 0)
                        self.assertGreaterEqual(sample["cycles_per_second"], 0)
                    self.assertEqual(sorted(sample["elapsed_wall_seconds"] for sample in samples),
                                     [sample["elapsed_wall_seconds"] for sample in samples])

    def test_native_progress_retains_early_failure_and_refuses_existing_destination(self):
        binary = Path(os.environ["NEKO_TEST_BINARY"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bios = root / "authored.bin"
            bios.write_bytes((0x4c000000).to_bytes(4, "little") + bytes(4194300))
            command = [str(binary), "bios-startup", str(bios), "--cycles", "100", "--scout"]
            plain = subprocess.run(command, capture_output=True)
            progress = root / "progress.jsonl"
            observed = subprocess.run(
                command + ["--progress-file", str(progress), "--progress-every", "8"],
                capture_output=True)
            self.assertEqual(observed.returncode, 1, observed.stderr)
            self.assertEqual(observed.stdout, plain.stdout)
            samples = [json.loads(line) for line in progress.read_text().splitlines()]
            self.assertEqual(samples[-1]["phase"], "finished")
            self.assertGreater(samples[-1]["master_cycle"], 0)
            self.assertLess(samples[-1]["master_cycle"], 8)
            before = progress.read_bytes()
            rejected = subprocess.run(
                command + ["--progress-file", str(progress)], capture_output=True)
            self.assertEqual(rejected.returncode, 2)
            self.assertIn(b"Cannot create progress", rejected.stderr)
            self.assertEqual(progress.read_bytes(), before)

    def test_native_progress_zero_and_partial_budgets_and_option_validation(self):
        binary = Path(os.environ["NEKO_TEST_BINARY"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bios = root / "authored.bin"
            bios.write_bytes(bytes(4194304))
            for ceiling in (0, 10):
                with self.subTest(ceiling=ceiling):
                    progress = root / f"{ceiling}.jsonl"
                    result = subprocess.run(
                        [str(binary), "bios-startup", str(bios), "--cycles", str(ceiling),
                         "--progress-file", str(progress), "--progress-every", "8"],
                        capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    samples = [json.loads(line) for line in progress.read_text().splitlines()]
                    self.assertEqual(samples[-1]["phase"], "finished")
                    self.assertEqual(samples[-1]["master_cycle"], ceiling)
            for interval in ("0", "-1", "18446744073709551616"):
                result = subprocess.run(
                    [str(binary), "bios-startup", str(bios), "--cycles", "10",
                     "--progress-file", str(root / "invalid.jsonl"),
                     "--progress-every", interval], capture_output=True)
                self.assertEqual(result.returncode, 2)
                self.assertFalse((root / "invalid.jsonl").exists())
            result = subprocess.run(
                [str(binary), "bios-startup", str(bios), "--cycles", "10",
                 "--progress-every", "8"], capture_output=True)
            self.assertEqual(result.returncode, 2)

    def test_native_scout_ceiling_retains_recent_and_pinned_checkpoints(self):
        binary = Path(os.environ["NEKO_TEST_BINARY"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bios = root / "authored.bin"
            bios.write_bytes(bytes(4194304))
            directory = root / "discovery"
            with patch.object(discovery, "build_neko", return_value=binary), \
                 redirect_stderr(io.StringIO()):
                result = discovery.run_scout_batch(
                    directory, bios, master_cycles=48, checkpoint_every=8,
                    replay_window=3, pinned_checkpoint_cycles=(8,))
            self.assertEqual(result["status"], "ceiling_reached")
            self.assertEqual({path.name for path in directory.glob("scout-*.state")},
                             {"scout-8.state", "scout-40.state", "scout-48.state"})
            manifest = json.loads((directory / "manifest.json").read_text())
            self.assertEqual(manifest["pruned_state_bytes"], sum(
                item["state"]["bytes"] for item in manifest["checkpoint_receipts"]
                if item.get("pruned")))
            for receipt in manifest["checkpoint_receipts"]:
                self.assertFalse(receipt.get("prune_pending", False))
                self.assertTrue(Path(receipt["path"] + ".json").is_file())
            for entry in manifest["commands"]:
                if len(entry["command"]) < 2 or entry["command"][1] != "bios-startup":
                    continue
                self.assertIn("--progress-file", entry["command"])
                sample = discovery.campaign.read_progress(directory, entry["progress_file"])
                self.assertEqual(sample, entry["progress"])
                self.assertEqual(sample["phase"], "finished")
                self.assertEqual(sample["master_cycle"], int(
                    entry["command"][entry["command"].index("--cycles") + 1]))
            self.assertEqual(discovery.campaign.status(directory)["state"],
                             "execution_finished")

    def test_checkpoint_conformance_preserves_native_cold_restore_equivalence(self):
        binary = Path(os.environ["NEKO_TEST_BINARY"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bios = root / "authored.bin"
            bios.write_bytes(bytes(4194304))
            for limit in (1, discovery.TRACE_EVENTS):
                with self.subTest(trace_limit=limit):
                    directory = root / f"conformance-{limit}"
                    with patch.object(discovery, "build_neko", return_value=binary), \
                         patch.object(discovery, "TRACE_EVENTS", limit), \
                         redirect_stderr(io.StringIO()):
                        result = discovery.run_batch(
                            directory, bios, master_cycles=64, checkpoint_at=8)
                    self.assertTrue(result["comparisons_equal"])
                    self.assertEqual(result["bios_exit_codes"],
                                     {"checkpoint": 0, "cold": 0, "resumed": 0,
                                      "resumed-plain": 0})
                    manifest = json.loads((directory / "manifest.json").read_text())
                    self.assertTrue(all(state["equal"]
                                        for state in manifest["final_states"].values()))
                    self.assertTrue(all(entry["returncode"] == 0
                                        for entry in manifest["commands"]))
                    self.assertEqual(discovery.campaign.status(directory)["state"],
                                     "execution_finished")

    def test_native_checkpoint_reuse_retains_and_protects_canonical_ending(self):
        binary = Path(os.environ["NEKO_TEST_BINARY"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bios = root / "authored.bin"
            bios.write_bytes(bytes(4194304))
            created = root / "created"
            reused = root / "reused"
            with patch.object(discovery, "build_neko", return_value=binary), \
                 redirect_stderr(io.StringIO()):
                discovery.run_batch(created, bios, master_cycles=16, checkpoint_at=8)
                result = discovery.run_batch(
                    reused, bios, master_cycles=24, checkpoint=created / "checkpoint.state",
                    failure_context=True)
            self.assertTrue(result["comparisons_equal"])
            manifest = json.loads((reused / "manifest.json").read_text())
            self.assertEqual(set(manifest["final_states"]), set(discovery.RUN_NAMES))
            baseline = reused / "traced1-final.state"
            identity = discovery.file_identity(baseline)
            for name, state in manifest["final_states"].items():
                self.assertTrue(state["equal"])
                self.assertEqual({key: state[key] for key in ("bytes", "sha256")}, identity)
                self.assertEqual(state.get("pruned_duplicate", False), name != "traced1")
            self.assertEqual(discovery.campaign.status(reused)["state"],
                             "execution_finished")
            with baseline.open("r+b") as stream:
                stream.write(b"changed")
            self.assertEqual(discovery.campaign.status(reused)["state"], "failed")

    def test_architectural_rejection_retains_matching_scout_replay_identity(self):
        binary = Path(os.environ["NEKO_TEST_BINARY"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bios = root / "authored.bin"
            bios.write_bytes((0x4c000000).to_bytes(4, "little") + bytes(4194300))
            with patch.object(discovery, "build_neko", return_value=binary), \
                 redirect_stderr(io.StringIO()):
                result = discovery.run_scout_batch(
                    root / "discovery", bios, master_cycles=100,
                    checkpoint_every=8, replay_window=3)
            self.assertEqual(result["status"], "equal")
            self.assertTrue(result["failure_reproduced"])
            self.assertEqual(result["first_failure"]["instruction"]["bits"], "0x4c000000")
            status = discovery.campaign.status(root / "discovery")
            self.assertEqual(status["state"], "execution_finished")
            self.assertEqual(status["scope"], "scout_origin")

    def test_authored_halfword_fault_reproduces_from_a_periodic_checkpoint(self):
        binary = Path(os.environ["NEKO_TEST_BINARY"]).resolve()
        self.assertTrue(binary.is_file())
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bios = root / "authored.bin"
            # Store into read-only boot ROM, not an unimplemented MMIO register.
            words = [0] * 16 + [0x3c02bfc0, 0x34420008, 0x24040003, 0xa4440000]
            image = b"".join(word.to_bytes(4, "little") for word in words)
            bios.write_bytes(image + bytes(4194304 - len(image)))
            directory = root / "discovery"
            with patch.object(discovery, "build_neko", return_value=binary), \
                 patch.object(discovery, "TRACE_EVENTS", 1), \
                 redirect_stderr(io.StringIO()):
                result = discovery.run_scout_batch(
                    directory, bios, master_cycles=100,
                    checkpoint_every=8, replay_window=3)
            self.assertEqual(result["status"], "equal")
            failure = result["first_failure"]
            self.assertEqual(failure["cpu"], "ee")
            self.assertEqual(failure["access"]["width"], 2)
            self.assertEqual(failure["access"]["physical_address"], "0x1fc00008")
            self.assertEqual(failure["access"]["data"], [3, 0])
            self.assertLess(failure["master_cycle"], 100)
            manifest = json.loads((directory / "manifest.json").read_text())
            self.assertTrue(manifest["failure_reproduced"])
            self.assertTrue(all(state["equal"] for state in manifest["final_states"].values()))
            self.assertEqual(manifest["commands"][-1]["returncode"], 0)
            self.assertTrue(all("elapsed_seconds" in entry
                                for entry in manifest["commands"]))
            self.assertGreater(manifest["progress"]["cycles_per_second"], 0)
            self.assertGreater(manifest["pruned_state_bytes"], 0)
            self.assertEqual(json.loads((directory / "summary.json").read_text())[
                "failure_event_source"], "failure_capture")

    def test_native_runtime_error_retains_replays_without_verifying(self):
        binary = Path(os.environ["NEKO_TEST_BINARY"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bios = root / "authored.bin"
            # Packed AD writes: unsupported PSMT8 host-to-local transfer, 64x32.
            packet = [0x8004, 0x10000000, 0x0e, 0,
                      0, 0x13013120, 0x50, 0,
                      0, 0, 0x51, 0,
                      64, 32, 0x52, 0,
                      0, 0, 0x53, 0]
            stores = [(0x1000 + index * 4, value)
                      for index, value in enumerate(packet)]
            stores += [(0xb000e000, 1), (0xb000a010, 0x1000),
                       (0xb000a020, 5), (0xb000a000, 0x100)]
            words = []
            for address, value in stores:
                words += [0x3c010000 | (address >> 16),
                          0x34210000 | (address & 0xffff),
                          0x3c020000 | (value >> 16),
                          0x34420000 | (value & 0xffff), 0xac220000]
            words += [0x1000ffff, 0]
            image = b"".join(word.to_bytes(4, "little") for word in words)
            bios.write_bytes(image + bytes(4194304 - len(image)))
            directory = root / "discovery"
            with patch.object(discovery, "build_neko", return_value=binary), \
                 redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                result = discovery.main([
                    str(directory), "--bios", str(bios), "--mode", "scout",
                    "--cycles", "256", "--checkpoint-every", "32", "--replay-window", "16"])
            manifest = json.loads((directory / "manifest.json").read_text())
            self.assertEqual(result, 2, manifest["commands"][-1])
            self.assertEqual(manifest["status"], "error")
            scout = next(item for item in reversed(manifest["commands"])
                         if item["name"].startswith("scout-") and item["returncode"] == 2)
            self.assertTrue(manifest["runtime_error_reproduced"], first_difference(
                discovery.load_log(directory / scout["stdout"]).result,
                discovery.load_log(directory / "runtime-replay.jsonl.gz").result))
            self.assertTrue(manifest["runtime_error_comparisons_equal"])
            self.assertNotIn("final_states", manifest)
            self.assertEqual(discovery.campaign.status(directory)["state"], "failed")
            for name in ("runtime-replay", "runtime-plain"):
                entry = next(item for item in manifest["commands"] if item["name"] == name)
                self.assertEqual(entry["returncode"], 2)
                self.assertEqual(entry["progress"]["phase"], "runtime_error")
            summary = json.loads((directory / "runtime-error-summary.json").read_text())
            self.assertIsNone(summary["run"])
            self.assertEqual(summary["execution_boundary"], "incomplete_cycle")
            self.assertIn("gs_transfer", summary)
            self.assertEqual(summary["gs_transfer"]["destination_pixel_storage_mode"], 19)
            self.assertEqual(summary["gs_transfer"]["destination_base_pointer"], 12576)
            self.assertEqual(summary["gs_transfer"]["width"], 64)
            self.assertEqual(summary["gs_transfer"]["height"], 32)
            self.assertTrue((directory / "runtime-error-events.json").is_file())
            with self.assertRaises(ValueError):
                discovery.campaign.finalize(directory, 0)


if __name__ == "__main__":
    unittest.main()
