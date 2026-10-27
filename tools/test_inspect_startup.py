"""BIOS-free command-line tests for the offline startup inspector."""

import gzip
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("inspect_startup.py")


def event(cycle, cpu, kind, values):
    return dict(type="trace_event", master_cycle=cycle, subsystem=cpu,
                event_type=kind, values=values)


def records(traced=True):
    result = [
        dict(type="bios_startup_input", rom_bytes=4194304,
             rom_fnv1a64="0123456789abcdef", max_master_cycles=1000,
             start="cold_dual_reset_vectors"),
        dict(type="startup_run_summary",
             run=dict(master_cycles=50, cycle_limit_reached=False, ee_cycles=50,
                      ee_accepted_instructions=10, iop_cycles=7,
                      iop_retired_instructions=4),
             snapshot=dict(master_cycle=50, ee=dict(pc=0x80001004),
                           iop=dict(pc=0xBFC00188, stop_reason="store_failure"),
                           sif=dict(sif0_words=0, sif1_words=0),
                           dmac=dict(control=0, hold_control=0x1201, status=0,
                                     interleave_size=0, interrupt_pending=False,
                                     priority_control=0, stall_address=0,
                                     sif_grant_order=["sif0", "sif1"],
                                     channels=[]))),
    ]
    if traced:
        events = [
            event(41, 10, 5, [0x80001000, 0xAC220000, 34, 0]),
            event(41, 10, 7, [0x70003FE0, 0xFEDCBA9876543210, 0,
                              0x3FE006400B08]),
            event(49, 11, 7, [0xBF801010, 0, 0, 0x1F80101000021804]),
            event(50, 11, 7, [0xBFC00000, 28, 0, 0x1FC0000000022904]),
        ]
        result += [
            dict(type="startup_trace_summary", event_limit=16,
                 captured_events=len(events), truncated=False),
            *events,
            dict(type="startup_first_failure", trace_truncated=False,
                 execution_boundary="complete_cycle",
                 failure=dict(kind="memory_access", cpu="iop", event_index=2,
                              master_cycle=49,
                              instruction=dict(pc=0x80002000, bits=0x8C220000,
                                               delay_slot=False, branch_pc=None),
                              access=dict(kind="data_load", outcome="unmapped",
                                          virtual_address=0xBF801010,
                                          physical_address=0x1F801010,
                                          data=[0, 0]),
                              exception=dict(code=7), dma=None)),
        ]
    return result


def capture_records():
    rows = records(False)
    original = records()
    context = [{key: value for key, value in row.items() if key != "type"}
               for row in original[3:-1]]
    failure = original[-1]
    failure.update(trace_truncated=True, event_source="failure_capture")
    return rows + [
        dict(type="startup_trace_summary", event_limit=1,
             captured_events=1, truncated=True),
        event(1, 10, 5, [0xBFC00000, 0, 0, 0]),
        dict(type="startup_failure_capture", capacity=4096,
             observed_events=100, first_event_index=90,
             failure_cycle_truncated=False, events=context),
        failure,
    ]


class InspectorTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "run.jsonl"
        self.write(self.path, records())

    def write(self, path, rows):
        path.write_text("".join(json.dumps(row) + "\n" for row in rows),
                        encoding="utf-8")

    def invoke(self, *arguments, status=0):
        process = subprocess.run(
            [sys.executable, "-B", str(SCRIPT), *map(str, arguments)],
            capture_output=True, text=True, check=False)
        self.assertEqual(process.returncode, status, process.stderr)
        return process

    def report(self, *arguments, status=0):
        return json.loads(self.invoke(*arguments, status=status).stdout)

    def reject(self, rows, message):
        self.write(self.path, rows)
        process = self.invoke("summary", self.path, status=2)
        self.assertIn(message, process.stderr)
        self.assertEqual(process.stdout, "")

    def test_gzip_reports_compare_as_their_original_jsonl_bytes(self):
        compressed = self.path.with_suffix(".jsonl.gz")
        with gzip.open(compressed, "wb") as stream:
            stream.write(self.path.read_bytes())
        report = self.report("compare", self.path, compressed, "--mode", "trace")
        self.assertTrue(report["equal"])
        self.assertTrue(report["byte_equal"])
        self.assertEqual(self.report("summary", compressed),
                         self.report("summary", self.path))

    def test_truncated_gzip_is_an_error_not_partial_evidence(self):
        compressed = self.path.with_suffix(".jsonl.gz")
        compressed.write_bytes(gzip.compress(self.path.read_bytes())[:-8])
        result = self.invoke("summary", compressed, status=2)
        self.assertEqual(result.stdout, "")
        self.assertTrue(result.stderr)

    def test_invalid_deflate_is_an_input_error_not_a_comparison_mismatch(self):
        compressed = self.path.with_suffix(".jsonl.gz")
        header = gzip.compress(self.path.read_bytes())[:10]
        compressed.write_bytes(header + b"\x07" + b"\x00" * 8)
        for arguments in (("summary", compressed),
                          ("compare", self.path, compressed)):
            with self.subTest(command=arguments[0]):
                result = self.invoke(*arguments, status=2)
                self.assertEqual(result.stdout, "")
                self.assertTrue(result.stderr)
                self.assertNotIn("Traceback", result.stderr)

    def test_continuation_comparison_checks_snapshot_and_common_trace_origin(self):
        other = self.path.with_name("resumed.jsonl")
        cold = records()
        cold[0]["trace_start_cycle"] = 40
        resumed = json.loads(json.dumps(cold))
        resumed[0].update(start="checkpoint", start_master_cycle=40)
        resumed[1]["run"].update(master_cycles=10, ee_cycles=10,
                                 ee_accepted_instructions=3,
                                 iop_cycles=2, iop_retired_instructions=1)
        self.write(self.path, cold)
        self.write(other, resumed)
        self.assertTrue(self.report("compare", self.path, other,
                                    "--mode", "continuation-trace")["equal"])
        self.assertTrue(self.report("compare", self.path, other,
                                    "--mode", "continuation")["equal"])
        resumed[3]["values"][1] ^= 1
        self.write(other, resumed)
        self.assertFalse(self.report("compare", self.path, other,
                                     "--mode", "continuation-trace", status=1)["equal"])
        resumed = json.loads(json.dumps(cold))
        resumed[1]["snapshot"]["ee"]["pc"] += 4
        self.write(other, resumed)
        self.assertFalse(self.report("compare", self.path, other,
                                     "--mode", "continuation", status=1)["equal"])
        resumed = json.loads(json.dumps(cold))
        resumed[0]["trace_start_cycle"] = 39
        self.write(other, resumed)
        self.assertFalse(self.report("compare", self.path, other,
                                     "--mode", "continuation-trace", status=1)["equal"])

    def test_summary_uses_exported_first_failure_not_ending_host_stop(self):
        report = self.report("summary", self.path)
        self.assertEqual(report["first_failure"]["master_cycle"], 49)
        self.assertEqual(report["first_failure"]["access"]["kind"], "data_load")
        self.assertEqual(report["first_failure"]["access"]["outcome"], "unmapped")
        self.assertEqual(report["snapshot"]["iop"]["stop_reason"], "store_failure")
        self.assertEqual(report["snapshot"]["ee"]["pc"], "0x80001004")
        self.assertEqual(report["coverage"], "retained_trace")

    def test_dma_hold_summary_preserves_raw_unsigned_words(self):
        for value in (0, 0x1201, 0x80001201, 0x80011201, 2**32 - 1):
            for runtime_error in (False, True):
                with self.subTest(value=value, runtime_error=runtime_error):
                    rows = records(False)
                    rows[1]["snapshot"]["dmac"]["hold_control"] = value
                    if runtime_error:
                        rows[1] = dict(
                            type="startup_runtime_error", run=None,
                            execution_boundary="incomplete_cycle",
                            message="authored", snapshot=rows[1]["snapshot"])
                    self.write(self.path, rows)
                    report = self.report("summary", self.path)
                    self.assertEqual(report["snapshot"]["dmac"]["hold_control"],
                                     value)

    def test_dma_hold_requires_an_exact_key_and_unsigned_32_bit_word(self):
        for runtime_error in (False, True):
            for scenario in ("missing_dmac", "missing", "camel_case", "extra",
                             "unknown", None, False, True, -1, 2**32, 1.5,
                             "4609", []):
                with self.subTest(scenario=scenario, runtime_error=runtime_error):
                    rows = records(False)
                    dmac = rows[1]["snapshot"]["dmac"]
                    if scenario == "missing_dmac":
                        del rows[1]["snapshot"]["dmac"]
                    elif scenario == "missing":
                        del dmac["hold_control"]
                    elif scenario == "camel_case":
                        dmac["holdControl"] = dmac.pop("hold_control")
                    elif scenario == "extra":
                        dmac["holdControl"] = dmac["hold_control"]
                    elif scenario == "unknown":
                        dmac["future_field"] = 0
                    else:
                        dmac["hold_control"] = scenario
                    if runtime_error:
                        rows[1] = dict(
                            type="startup_runtime_error", run=None,
                            execution_boundary="incomplete_cycle",
                            message="authored", snapshot=rows[1]["snapshot"])
                    self.reject(rows, "dmac")

    def test_dma_hold_comparison_detects_reserved_bits_in_both_modes(self):
        other = self.path.with_name("other.jsonl")
        for mode in ("run", "trace"):
            with self.subTest(mode=mode):
                changed = records()
                changed[1]["snapshot"]["dmac"]["hold_control"] |= 0x80000000
                self.write(other, changed)
                report = self.report("compare", self.path, other,
                                     "--mode", mode, status=1)
                self.assertFalse(report["equal"])
                expected = ("$.result.snapshot.dmac.hold_control" if mode == "run"
                            else "$[1].snapshot.dmac.hold_control")
                self.assertEqual(report["first_difference"], expected)

    def test_dma_hold_comparison_rejects_a_missing_field_on_either_side(self):
        other = self.path.with_name("other.jsonl")
        for missing_left in (False, True):
            for mode in ("run", "trace"):
                with self.subTest(missing_left=missing_left, mode=mode):
                    complete = records()
                    missing = records()
                    del missing[1]["snapshot"]["dmac"]["hold_control"]
                    self.write(self.path, missing if missing_left else complete)
                    self.write(other, complete if missing_left else missing)
                    process = self.invoke("compare", self.path, other,
                                          "--mode", mode, status=2)
                    self.assertIn("dmac", process.stderr)
                    self.assertEqual(process.stdout, "")

    def test_late_failure_uses_capture_indices_not_the_truncated_prefix(self):
        self.write(self.path, capture_records())
        report = self.report("summary", self.path)
        self.assertEqual(report["coverage"], "truncated_prefix")
        self.assertEqual(report["failure_event_source"], "failure_capture")
        self.assertEqual(report["first_failure"]["event_index"], 2)
        self.assertEqual(report["first_failure"]["master_cycle"], 49)
        self.assertEqual(report["failure_capture"]["first_event_index"], 90)
        self.assertNotIn("events", report["failure_capture"])
        prefix = self.report("events", self.path)
        self.assertEqual(prefix["matched_events"], 1)
        window = self.report("events", self.path, "--source", "failure_capture",
                             "--cpu", "iop", "--cycle", "49")
        self.assertEqual(window["coverage"], "bounded_failure_capture")
        row, = window["events"]
        self.assertEqual(row["event_index"], 2)
        self.assertEqual(row["global_event_index"], 92)
        self.assertEqual(row["master_cycle"], 49)
        issued = self.report("instructions", self.path,
                             "--source", "failure_capture")
        self.assertEqual(issued["matched_events"], 1)

    def test_capture_source_and_bounds_are_validated(self):
        for scenario, message in (
                ("missing", "requires startup_failure_capture"),
                ("source", "event_source"),
                ("capacity", "capacity"),
                ("count", "observed_events"),
                ("index", "outside the retained"),
                ("order", "cycle order"),
                ("values", "four raw words")):
            with self.subTest(scenario=scenario):
                rows = capture_records()
                capture = rows[-2]
                if scenario == "missing":
                    rows.pop(-2)
                elif scenario == "source":
                    rows[-1].pop("event_source")
                elif scenario == "capacity":
                    capture["capacity"] = 0
                elif scenario == "count":
                    capture["observed_events"] = 1
                elif scenario == "index":
                    rows[-1]["failure"]["event_index"] = 4
                elif scenario == "order":
                    capture["events"][1]["master_cycle"] = 1
                else:
                    capture["events"][0]["values"] = []
                self.reject(rows, message)

    def test_absent_capture_is_not_silently_replaced_by_prefix(self):
        result = self.invoke("events", self.path, "--source", "failure_capture",
                             status=2)
        self.assertIn("no failure capture", result.stderr)

    def test_capture_without_failure_and_runtime_partial_cycle_are_explicit(self):
        rows = capture_records()
        rows[-1]["failure"] = None
        rows[-2].update(first_event_index=0, observed_events=4)
        self.write(self.path, rows)
        report = self.report("summary", self.path)
        self.assertIsNone(report["first_failure"])
        self.assertIn("fault-free", report["note"])
        rows[-1]["failure"] = records()[-1]["failure"]
        rows[-2]["failure_cycle_truncated"] = True
        rows[1] = dict(type="startup_runtime_error", run=None,
                       execution_boundary="incomplete_cycle", message="authored",
                       snapshot=rows[1]["snapshot"])
        rows[-1]["execution_boundary"] = "incomplete_cycle"
        # An oversized failure cycle retains exactly the bounded capacity.
        rows[-2]["events"] += [rows[-2]["events"][-1]] * (4096 - 4)
        rows[-2].update(observed_events=5000, first_event_index=90)
        self.write(self.path, rows)
        report = self.report("summary", self.path)
        self.assertTrue(report["failure_capture"]["failure_cycle_truncated"])
        self.assertEqual(report["execution_boundary"], "incomplete_cycle")

    def test_memory_data_is_not_opcode(self):
        report = self.report("events", self.path, "--cpu", "ee",
                             "--event", "memory", "--cycle", "41")
        row, = report["events"]
        self.assertEqual(row["event_index"], 1)
        self.assertEqual(row["payload"]["data_low_raw"], "0xfedcba9876543210")
        self.assertEqual(row["payload"]["metadata_raw"], "0x00003fe006400b08")
        self.assertNotIn("opcode", row["payload"])
        self.assertNotIn("kind", row["payload"])

    def test_instructions_only_uses_issued_events(self):
        report = self.report("instructions", self.path, "--pc", "0x80001000")
        row, = report["events"]
        self.assertEqual(row["payload"]["opcode"], "0xac220000")
        self.assertEqual(row["payload"]["pc"], "0x80001000")
        self.assertEqual(row["cpu"], "ee")

    def test_iop_address_filter_and_inclusive_cycle_range(self):
        report = self.report("events", self.path, "--cpu", "iop",
                             "--from-cycle", "49", "--to-cycle", "50",
                             "--address", "0xbf801010")
        row, = report["events"]
        self.assertEqual(row["master_cycle"], 49)
        self.assertEqual(row["payload"]["metadata_raw"], "0x1f80101000021804")

    def test_serial_completion_preserves_raw_byte_and_endpoint(self):
        rows = records()
        rows[-2] = event(50, 10, 22, [0xff, 0x1000f180, 0, 0])
        self.write(self.path, rows)
        report = self.report("events", self.path, "--cpu", "ee",
                             "--event", "serial_transmit_completed")
        self.assertEqual(len(report["events"]), 1)
        row = report["events"][0]
        self.assertEqual(row["event"], "serial_transmit_completed")
        self.assertEqual(row["master_cycle"], 50)
        self.assertEqual(row["payload"],
                         dict(byte=255, physical_address="0x1000f180"))
        self.assertEqual(row["values_raw"][0], "0x00000000000000ff")

    def test_unknown_event_ids_remain_raw(self):
        rows = records()
        rows[3] = event(41, 99, 250, [1, 2, 3, 2**64 - 1])
        self.write(self.path, rows)
        report = self.report("events", self.path, "--event", "250")
        row, = report["events"]
        self.assertEqual(row["subsystem"], 99)
        self.assertEqual(row["values_raw"][-1], "0xffffffffffffffff")
        self.assertNotIn("payload", row)

    def test_limit_and_empty_selection_are_explicit(self):
        report = self.report("events", self.path, "--limit", "1")
        self.assertEqual(report["matched_events"], 4)
        self.assertEqual(len(report["events"]), 1)
        self.assertTrue(report["limited"])
        empty = self.report("instructions", self.path, "--cpu", "iop")
        self.assertEqual(empty["events"], [])
        self.assertIn("retained", empty["note"])

    def test_truncated_prefix_does_not_claim_no_failures(self):
        rows = records()
        rows[2].update(event_limit=4, truncated=True)
        rows[-1].update(trace_truncated=True, failure=None)
        self.write(self.path, rows)
        report = self.report("summary", self.path)
        self.assertEqual(report["coverage"], "truncated_prefix")
        self.assertIsNone(report["first_failure"])
        self.assertIn("fault-free", report["note"])

    def test_empty_capture_and_exact_fit_are_not_inferred_as_truncated(self):
        rows = records()
        rows[2]["event_limit"] = rows[2]["captured_events"]
        self.write(self.path, rows)
        self.assertEqual(self.report("summary", self.path)["coverage"],
                         "retained_trace")
        empty = rows[:3] + [rows[-1]]
        empty[0]["max_master_cycles"] = 0
        empty[1]["run"] = dict.fromkeys(empty[1]["run"], 0)
        empty[1]["run"]["cycle_limit_reached"] = True
        empty[1]["snapshot"]["master_cycle"] = 0
        empty[2].update(event_limit=0, captured_events=0)
        empty[-1]["failure"] = None
        self.write(self.path, empty)
        report = self.report("events", self.path)
        self.assertEqual(report["coverage"], "retained_trace")
        self.assertEqual(report["matched_events"], 0)
        self.assertFalse(report["limited"])

    def test_untraced_summary_and_trace_commands(self):
        self.write(self.path, records(False))
        self.assertEqual(self.report("summary", self.path)["coverage"], "untraced")
        self.assertIn("no trace", self.invoke("events", self.path, status=2).stderr)

    def test_runtime_error_preserves_incomplete_cycle(self):
        rows = records()
        rows[1] = dict(type="startup_runtime_error", run=None,
                       execution_boundary="incomplete_cycle", message="authored error",
                       snapshot=rows[1]["snapshot"])
        rows[-1]["execution_boundary"] = "incomplete_cycle"
        self.write(self.path, rows)
        report = self.report("summary", self.path)
        self.assertEqual(report["runtime_error"], "authored error")
        self.assertEqual(report["execution_boundary"], "incomplete_cycle")
        self.assertIsNone(report["run"])

    def test_filtered_views_preserve_runtime_error_boundary(self):
        rows = records()
        rows[1] = dict(type="startup_runtime_error", run=None,
                       execution_boundary="incomplete_cycle", message="authored error",
                       snapshot=rows[1]["snapshot"])
        rows[-1]["execution_boundary"] = "incomplete_cycle"
        self.write(self.path, rows)
        for command in ("events", "instructions"):
            with self.subTest(command=command):
                report = self.report(command, self.path)
                self.assertEqual(report["coverage"], "retained_trace")
                self.assertEqual(report["execution_boundary"], "incomplete_cycle")
                self.assertEqual(report["outcome"], "startup_runtime_error")
                self.assertEqual(report["runtime_error"], "authored error")

    def test_comparison_modes_and_first_difference(self):
        other = self.path.with_name("other.jsonl")
        self.write(other, records(False))
        report = self.report("compare", self.path, other)
        self.assertTrue(report["equal"])
        self.assertFalse(report["byte_equal"])
        self.assertIn("trace", self.invoke("compare", self.path, other,
                                          "--mode", "trace", status=2).stderr)
        changed = records()
        changed[1]["snapshot"]["ee"]["pc"] += 4
        self.write(other, changed)
        report = self.report("compare", self.path, other, status=1)
        self.assertEqual(report["first_difference"], "$.result.snapshot.ee.pc")

    def test_trace_comparison_preserves_order_but_ignores_json_formatting(self):
        other = self.path.with_name("other.jsonl")
        other.write_text("\n".join(json.dumps(row, sort_keys=True)
                                   for row in records()), encoding="utf-8")
        report = self.report("compare", self.path, other, "--mode", "trace")
        self.assertTrue(report["equal"])
        self.assertFalse(report["byte_equal"])
        rows = records()
        rows[3], rows[4] = rows[4], rows[3]
        self.write(other, rows)
        self.assertFalse(self.report("compare", self.path, other,
                                     "--mode", "trace", status=1)["equal"])

    def test_malformed_json_reports_file_and_line(self):
        self.path.write_text('{"type":\n', encoding="utf-8")
        process = self.invoke("summary", self.path, status=2)
        self.assertIn(f"{self.path}:1:", process.stderr)
        self.assertEqual(process.stdout, "")

    def test_missing_records_and_count_mismatch_are_errors(self):
        self.reject(records()[:-1], "startup_first_failure")
        rows = records()
        rows[2]["captured_events"] += 1
        self.reject(rows, "captured_events")
        self.reject(records(False)[:1], "run result")
        self.reject(records(False) + [records(False)[0]], "duplicate")
        self.reject(records(False) + [dict(type="future_record")], "unsupported")

    def test_invalid_trace_shapes_and_numbers(self):
        for value in ([1, 2, 3], [0, 0, 0, 2**64], [0, True, 0, 0],
                      [0, 1.5, 0, 0]):
            rows = records()
            rows[3]["values"] = value
            with self.subTest(value=value):
                self.reject(rows, "values")
        rows = records()
        rows[-1]["trace_truncated"] = True
        self.reject(rows, "trace_truncated")

    def test_duplicate_json_keys_and_nonfinite_numbers_are_errors(self):
        for text, message in [('{"type":"a","type":"b"}\n', "duplicate"),
                              ('{"type":"a","value":NaN}\n', "non-finite")]:
            self.path.write_text(text, encoding="utf-8")
            self.assertIn(message, self.invoke("summary", self.path,
                                               status=2).stderr)

    def test_invalid_filter_arguments_are_errors(self):
        for arguments in (("--from-cycle", "50", "--to-cycle", "49"),
                          ("--limit", "-1"), ("--event", "typo"),
                          ("--pc", "0x100000000")):
            with self.subTest(arguments=arguments):
                self.invoke("events", self.path, *arguments, status=2)

    def test_trace_failure_index_must_reference_retained_event(self):
        rows = records()
        rows[-1]["failure"]["event_index"] = 99
        self.reject(rows, "event_index")

    def test_unreadable_input_is_a_reported_error(self):
        process = self.invoke("summary", self.path.with_name("missing"), status=2)
        self.assertIn("missing", process.stderr)
        self.assertEqual(process.stdout, "")


if __name__ == "__main__":
    unittest.main()
