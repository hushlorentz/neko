"""BIOS-free regression coverage for read-only native timing reports."""

from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest

import bios_campaign as campaign
import report_bios_timings as timings


class TimingReportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.context = dict(host="authored host", build="assertions, -O2",
                            revision="authored revision")
        self.context_path = self.root / "context.json"
        campaign.write_json(self.context_path, self.context)

    def batch(self, name, wall=10, *, flags=(), start=0, identity="a" * 64):
        directory = self.root / name
        directory.mkdir()
        samples = []
        for phase, cycle, elapsed in (
                ("execution", start, 0.1), ("execution", 100, wall),
                ("report", 100, wall + 0.2), ("checkpoint", 100, wall + 2),
                ("finished", 100, wall + 3)):
            samples.append(dict(
                format="neko-bios-progress-v1", phase=phase,
                start_master_cycle=start, master_cycle=cycle, cycle_ceiling=100,
                elapsed_wall_seconds=elapsed, elapsed_cpu_seconds=elapsed / 2,
                cycles_per_second=(cycle - start) / elapsed,
                sampled_at_unix_seconds=1000 + elapsed))
        progress = directory / "plain.progress.jsonl"
        progress.write_text("".join(json.dumps(row) + "\n" for row in samples))
        command = ["neko", "bios-startup", "bios.bin", "--cycles", "100", *flags]
        if start:
            command += ["--resume-checkpoint", "checkpoint.state"]
        entry = dict(name="plain", command=command, returncode=0,
                     elapsed_seconds=wall + 5, progress_file=progress.name,
                     progress=samples[-1])
        manifest = dict(status="equal", commands=[entry],
                        binary_identity=dict(sha256=identity, bytes=123),
                        bios_identity=dict(sha256="b" * 64, bytes=4194304))
        if start:
            manifest["checkpoint"] = dict(receipt=dict(
                state=dict(sha256="c" * 64, bytes=456), master_cycle=start,
                binary=manifest["binary_identity"], bios=manifest["bios_identity"]))
        campaign.write_json(directory / "manifest.json", manifest)
        campaign.write_json(directory / "execution.json", dict(
            format=campaign.EXECUTION_FORMAT, returncode=0,
            manifest=campaign.file_identity(directory / "manifest.json")))
        return directory

    def rewrite(self, directory, change):
        path = directory / "manifest.json"
        manifest = campaign.read_json(path)
        change(manifest)
        campaign.write_json(path, manifest)
        receipt = campaign.read_json(directory / "execution.json")
        receipt["manifest"] = campaign.file_identity(path)
        campaign.write_json(directory / "execution.json", receipt)

    def test_separates_execution_reporting_checkpoint_and_external_overhead(self):
        directory = self.batch("one")
        before = {path.name: path.read_bytes() for path in directory.iterdir()}
        report = timings.report([directory], self.context)
        stage = report["stages"][0]
        self.assertAlmostEqual(stage["execution_wall_seconds"], 10.1)
        self.assertAlmostEqual(stage["execution_cpu_seconds"], 5.05)
        self.assertAlmostEqual(stage["report_wall_seconds"], 1.8)
        self.assertAlmostEqual(stage["checkpoint_wall_seconds"], 1)
        self.assertAlmostEqual(stage["external_overhead_wall_seconds"], 2.1)
        self.assertAlmostEqual(stage["cycles_per_second"], 100 / 10.1)
        self.assertEqual(stage["master_cycles"], 100)
        self.assertEqual(before, {path.name: path.read_bytes() for path in directory.iterdir()})
        self.assertIn("not", report["note"])

    def test_groups_only_identical_workloads_and_records_all_repetitions(self):
        directories = [
            self.batch("one", wall=10), self.batch("two", wall=12),
            self.batch("traced", flags=("--trace-events", "256")),
            self.batch("probe", flags=("--scout",)),
            self.batch("other-build", identity="d" * 64),
            self.batch("restored", start=50),
        ]
        report = timings.report(directories, self.context)
        self.assertEqual(len(report["groups"]), 5)
        repeated = next(group for group in report["groups"] if group["repetitions"] == 2)
        self.assertAlmostEqual(repeated["median_execution_wall_seconds"], 11.1)
        self.assertAlmostEqual(repeated["execution_wall_spread_seconds"], 2)
        self.assertEqual(len(repeated["stage_indices"]), 2)
        self.assertEqual(len(report["stages"]), 6)
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            timings.report([directories[0], directories[0]], self.context)

    def test_rejects_incomplete_changed_or_nonmonotonic_receipts(self):
        for defect in ("pending", "error", "changed", "partial", "backwards", "bounds"):
            with self.subTest(defect=defect):
                directory = self.batch(defect)
                if defect in ("pending", "error"):
                    self.rewrite(directory, lambda manifest: manifest["commands"][0].update(
                        returncode=None if defect == "pending" else 2))
                elif defect == "changed":
                    (directory / "manifest.json").write_text("{}")
                else:
                    path = directory / "plain.progress.jsonl"
                    samples = [json.loads(row) for row in path.read_text().splitlines()]
                    if defect == "partial":
                        path.write_text(path.read_text().rstrip("\n"))
                    elif defect == "backwards":
                        samples[1]["elapsed_wall_seconds"] = 0
                        path.write_text("".join(json.dumps(row) + "\n" for row in samples))
                    else:
                        samples[1]["cycle_ceiling"] = 101
                        path.write_text("".join(json.dumps(row) + "\n" for row in samples))
                with self.assertRaises(ValueError):
                    timings.report([directory], self.context)

    def test_refuses_missing_timing_and_missing_launch_context(self):
        directory = self.batch("one")
        self.rewrite(directory, lambda manifest: manifest["commands"][0].pop("progress_file"))
        with self.assertRaisesRegex(ValueError, "progress"):
            timings.report([directory], self.context)
        with self.assertRaisesRegex(ValueError, "context"):
            timings.report([directory], dict(host="host"))

    def test_marks_overlapping_stage_windows_and_unknown_concurrency(self):
        directory = self.batch("overlap")
        def change(manifest):
            first = manifest["commands"][0]
            first["started_at"] = "2026-11-20T00:00:00+00:00"
            second = dict(first, name="other", started_at="2026-11-20T00:00:01+00:00")
            manifest["commands"].append(second)
        self.rewrite(directory, change)
        report = timings.report([directory], self.context)
        self.assertTrue(report["stages"][0]["workload"]["stage_window_overlap"])
        self.assertEqual(report["stages"][0]["overlapping_stage_windows"],
                         [dict(directory=str(directory.resolve()), name="other")])
        unknown = timings.report([self.batch("unknown")], self.context)
        self.assertIsNone(unknown["stages"][0]["workload"]["stage_window_overlap"])

    def test_malformed_checkpoint_receipts_fail_explicitly(self):
        for value in (None, {}, {"receipt": None}, {"receipt": []}):
            with self.subTest(value=value):
                directory = self.batch(str(len(list(self.root.iterdir()))), start=50)
                self.rewrite(directory, lambda manifest: manifest.update(checkpoint=value))
                with self.assertRaises(ValueError):
                    timings.report([directory], self.context)

    def test_detects_overlap_between_separate_campaigns(self):
        directories = [self.batch("one"), self.batch("two")]
        for directory in directories:
            self.rewrite(directory, lambda manifest: manifest["commands"][0].update(
                started_at="2026-11-20T00:00:00+00:00"))
        report = timings.report(directories, self.context)
        self.assertTrue(all(stage["workload"]["stage_window_overlap"]
                            for stage in report["stages"]))

    def test_sampled_stages_are_not_grouped_with_unprofiled_repetitions(self):
        directories = [self.batch("one"), self.batch("sampled")]
        self.rewrite(directories[1], lambda manifest: manifest["commands"][0].update(
            sampling_seconds=1))
        report = timings.report(directories, self.context)
        self.assertEqual(len(report["groups"]), 2)
        self.assertEqual(report["stages"][1]["workload"]["sampling_seconds"], 1)

    def test_cli_outputs_json_and_surfaces_errors(self):
        directory = self.batch("one")
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            code = timings.main([str(directory), "--context", str(self.context_path)])
        self.assertEqual(code, 0, errors.getvalue())
        self.assertEqual(json.loads(output.getvalue())["context"], self.context)
        with redirect_stdout(io.StringIO()), redirect_stderr(errors):
            code = timings.main([str(directory), "--context", str(self.root / "missing")])
        self.assertEqual(code, 2)
        self.assertIn("error:", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
