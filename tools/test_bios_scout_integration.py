"""Opt-in BIOS-free end-to-end coverage against the built headless executable."""

from contextlib import redirect_stderr
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run_bios_discovery as discovery


@unittest.skipUnless(os.environ.get("NEKO_TEST_BINARY"),
                     "Set NEKO_TEST_BINARY to the existing headless executable")
class ScoutIntegrationTests(unittest.TestCase):
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

    def test_authored_halfword_fault_reproduces_from_a_periodic_checkpoint(self):
        binary = Path(os.environ["NEKO_TEST_BINARY"]).resolve()
        self.assertTrue(binary.is_file())
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bios = root / "authored.bin"
            words = [0] * 16 + [0x3c02ba00, 0x34420008, 0x24040003, 0xa4440000]
            image = b"".join(word.to_bytes(4, "little") for word in words)
            bios.write_bytes(image + bytes(4194304 - len(image)))
            directory = root / "discovery"
            with patch.object(discovery, "build_neko", return_value=binary), \
                 redirect_stderr(io.StringIO()):
                result = discovery.run_scout_batch(
                    directory, bios, master_cycles=100,
                    checkpoint_every=8, replay_window=3)
            self.assertEqual(result["status"], "equal")
            failure = result["first_failure"]
            self.assertEqual(failure["cpu"], "ee")
            self.assertEqual(failure["access"]["width"], 2)
            self.assertEqual(failure["access"]["physical_address"], "0x1a000008")
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


if __name__ == "__main__":
    unittest.main()
