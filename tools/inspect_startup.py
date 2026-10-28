#!/usr/bin/env python3

"""Inspect existing bios-startup JSONL files without executing guest code."""

import argparse
from collections import Counter
from dataclasses import dataclass
import gzip
import json
from pathlib import Path
import sys
from typing import Optional
import zlib


# Stable public identities from neko/regression_trace.hpp, not memory bitfields.
CPUS = {10: "ee", 11: "iop"}
SUBSYSTEMS = {**CPUS, 15: "cdvd"}
EVENTS = {5: "instruction_issued", 7: "memory_access",
          8: "exception_entered", 21: "fault_context",
          22: "serial_transmit_completed",
          23: "command_accepted", 24: "command_completed"}
EVENT_FILTERS = {name: number for number, name in EVENTS.items()}
EVENT_FILTERS.update(instruction=5, memory=7, exception=8, fault=21)
RECORD_TYPES = {
    "bios_startup_input", "startup_run_summary", "startup_runtime_error",
    "startup_trace_summary", "trace_event", "startup_first_failure",
    "startup_failure_capture",
}
DMAC_FIELDS = {
    "control", "hold_control", "status", "interleave_size", "interrupt_pending",
    "priority_control", "stall_address", "sif_grant_order", "channels",
}
HEX_FIELDS = {
    "pc", "epc", "bad_vaddr", "status", "cause", "bits", "branch_pc",
    "vector", "fault_address", "virtual_address", "physical_address",
    "exception_address", "rejected_instruction", "address", "madr", "tadr", "sadr",
}
EVIDENCE_NOTE = (
    "Only retained evidence is shown. No match or null failure is not a "
    "fault-free, boot, or handshake verdict."
)


@dataclass
class StartupLog:
    raw: bytes
    records: list
    input: dict
    result: dict
    trace: Optional[dict]
    failure_record: Optional[dict]
    events: list
    capture: Optional[dict]

    @property
    def coverage(self):
        if self.trace is None:
            return "untraced"
        return "truncated_prefix" if self.trace["truncated"] else "retained_trace"


def uint(value, label, bits=64):
    if type(value) is not int or not 0 <= value < (1 << bits):
        raise ValueError(f"{label} must be an unsigned {bits}-bit integer")
    return value


def require(record, key, kind):
    if key not in record or type(record[key]) is not kind:
        raise ValueError(f"{key} must be {kind.__name__}")
    return record[key]


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError(f"non-finite JSON number: {value}")


def validate_event(record):
    uint(record.get("master_cycle"), "master_cycle")
    uint(record.get("subsystem"), "subsystem", 8)
    uint(record.get("event_type"), "event_type", 8)
    values = require(record, "values", list)
    if len(values) != 4:
        raise ValueError("values must contain four raw words")
    for value in values:
        uint(value, "values")
    cdvd_command = record["subsystem"] == 15 and record["event_type"] in (23, 24)
    if not cdvd_command:
        if "cdvd" in record:
            raise ValueError("cdvd requires a CDVD command event")
        return
    cdvd = require(record, "cdvd", dict)
    fields = {"transport", "opcode", "phase_before", "phase_after",
              "cause_before", "cause_after", "rtc_ticks", "result"}
    if cdvd.keys() != fields:
        raise ValueError("cdvd must contain exactly: " + ", ".join(sorted(fields)))
    if cdvd["transport"] not in ("n", "s"):
        raise ValueError("cdvd.transport must be n or s")
    for field in ("phase_before", "phase_after"):
        if cdvd[field] not in ("idle", "pending", "result"):
            raise ValueError(f"cdvd.{field} must be idle, pending or result")
    for field in ("opcode", "cause_before", "cause_after"):
        uint(cdvd[field], f"cdvd.{field}", 8)
    uint(cdvd["rtc_ticks"], "cdvd.rtc_ticks")
    result = cdvd["result"]
    if type(result) is not list:
        raise ValueError("cdvd.result must be list")
    if len(result) != 8:
        raise ValueError("cdvd.result must contain eight bytes")
    for value in result:
        uint(value, "cdvd.result", 8)


def validate_record(record):
    kind = require(record, "type", str)
    if kind not in RECORD_TYPES:
        raise ValueError(f"unsupported record type: {kind}")
    if kind == "bios_startup_input":
        uint(record.get("rom_bytes"), "rom_bytes")
        uint(record.get("max_master_cycles"), "max_master_cycles")
        require(record, "rom_fnv1a64", str)
        require(record, "start", str)
        start = (uint(record.get("start_master_cycle"), "start_master_cycle")
                 if record["start"] == "checkpoint" else 0)
        if start > record["max_master_cycles"]:
            raise ValueError("checkpoint exceeds the absolute cycle ceiling")
        if "trace_start_cycle" in record:
            trace_start = uint(record["trace_start_cycle"], "trace_start_cycle")
            if not start <= trace_start <= record["max_master_cycles"]:
                raise ValueError("trace start is outside the run interval")
    elif kind in ("startup_run_summary", "startup_runtime_error"):
        snapshot = require(record, "snapshot", dict)
        uint(snapshot.get("master_cycle"), "snapshot.master_cycle")
        for cpu in ("ee", "iop"):
            state = require(snapshot, cpu, dict)
            uint(state.get("pc"), f"snapshot.{cpu}.pc", 32)
        dmac = require(snapshot, "dmac", dict)
        if dmac.keys() != DMAC_FIELDS:
            raise ValueError("snapshot.dmac must contain exactly: " +
                             ", ".join(sorted(DMAC_FIELDS)))
        uint(dmac.get("hold_control"), "snapshot.dmac.hold_control", 32)
        if kind == "startup_run_summary":
            run = require(record, "run", dict)
            require(run, "cycle_limit_reached", bool)
            for key in ("master_cycles", "ee_cycles", "ee_accepted_instructions",
                        "iop_cycles", "iop_retired_instructions"):
                uint(run.get(key), f"run.{key}")
        else:
            require(record, "message", str)
            if record.get("execution_boundary") != "incomplete_cycle":
                raise ValueError("runtime error must have incomplete_cycle boundary")
            if "run" not in record or record["run"] is not None:
                raise ValueError("runtime error run must be null")
    elif kind == "startup_trace_summary":
        uint(record.get("event_limit"), "event_limit")
        uint(record.get("captured_events"), "captured_events")
        require(record, "truncated", bool)
    elif kind == "trace_event":
        validate_event(record)
    elif kind == "startup_failure_capture":
        if record.get("capacity") != 4096:
            raise ValueError("failure capture capacity must be 4096")
        uint(record.get("observed_events"), "observed_events")
        uint(record.get("first_event_index"), "first_event_index")
        require(record, "failure_cycle_truncated", bool)
        events = require(record, "events", list)
        if len(events) > record["capacity"]:
            raise ValueError("failure capture exceeds capacity")
        previous = 0
        for event in events:
            if type(event) is not dict:
                raise ValueError("failure capture events must be objects")
            validate_event(event)
            if event["master_cycle"] < previous:
                raise ValueError("failure capture cycle order is not monotonic")
            previous = event["master_cycle"]
    else:
        require(record, "trace_truncated", bool)
        if record.get("execution_boundary") not in ("complete_cycle", "incomplete_cycle"):
            raise ValueError("invalid execution_boundary")
        if "failure" not in record:
            raise ValueError("missing failure")
        if record.get("event_source", "prefix") not in ("prefix", "failure_capture"):
            raise ValueError("invalid event_source")
        if record["failure"] is not None:
            failure = require(record, "failure", dict)
            uint(failure.get("event_index"), "failure.event_index")
            uint(failure.get("master_cycle"), "failure.master_cycle")


def load_log(path):
    if path.suffix == ".gz":
        with gzip.open(path, "rb") as stream:
            raw = stream.read()
    else:
        raw = path.read_bytes()
    records = []
    single = {}
    events = []
    for number, line in enumerate(raw.splitlines(), 1):
        try:
            record = json.loads(line, object_pairs_hook=unique_object,
                                parse_constant=reject_constant)
            if not isinstance(record, dict):
                raise ValueError("expected a JSON object")
            validate_record(record)
            kind = record["type"]
            if kind == "trace_event":
                events.append(record)
            elif kind in single:
                raise ValueError(f"duplicate {kind} record")
            else:
                single[kind] = record
            records.append(record)
        except (ValueError, UnicodeError) as error:
            raise ValueError(f"{path}:{number}: {error}") from error

    def invalid(message):
        raise ValueError(f"{path}: {message}")

    input_record = single.get("bios_startup_input")
    results = [single[key] for key in ("startup_run_summary", "startup_runtime_error")
               if key in single]
    if input_record is None or len(results) != 1:
        invalid("expected bios_startup_input and exactly one run result")
    result = results[0]
    trace = single.get("startup_trace_summary")
    failure = single.get("startup_first_failure")
    capture = single.get("startup_failure_capture")
    if records[:2] != [input_record, result]:
        invalid("expected input followed by run result")
    if trace is None:
        if events or failure is not None or capture is not None:
            invalid("trace records require startup_trace_summary")
    else:
        if failure is None:
            invalid("missing startup_first_failure; file is incomplete")
        suffix = [capture, failure] if capture is not None else [failure]
        if records[2:] != [trace, *events, *suffix]:
            invalid("expected trace summary, ordered events, then startup_first_failure")
        count = trace["captured_events"]
        if count != len(events) or count > trace["event_limit"]:
            invalid("captured_events disagrees with retained events or event_limit")
        if trace["truncated"] and count != trace["event_limit"]:
            invalid("truncated trace must reach event_limit")
        if trace["truncated"] != failure["trace_truncated"]:
            invalid("trace_truncated disagrees with trace summary")
        expected = ("incomplete_cycle" if result["type"] == "startup_runtime_error"
                    else "complete_cycle")
        if failure["execution_boundary"] != expected:
            invalid("execution_boundary disagrees with run result")
        source = failure.get("event_source", "prefix")
        if source == "failure_capture" and capture is None:
            invalid("failure_capture source requires startup_failure_capture")
        if capture is not None:
            if source != "failure_capture":
                invalid("failure capture requires matching event_source")
            observed = capture["observed_events"]
            retained = len(capture["events"])
            first = capture["first_event_index"]
            if observed < len(events) or first + retained > observed:
                invalid("failure capture observed_events disagrees with retained bounds")
            if capture["events"] and capture["events"][-1]["master_cycle"] > \
                    result["snapshot"]["master_cycle"]:
                invalid("failure capture extends beyond the ending snapshot")
            if not trace["truncated"] and observed != len(events):
                invalid("complete prefix disagrees with failure capture observed_events")
        selected = failure["failure"]
        selected_events = capture["events"] if capture is not None else events
        if selected is not None:
            index = selected["event_index"]
            if index >= len(selected_events):
                invalid("failure.event_index is outside the retained trace")
            if selected["master_cycle"] != selected_events[index]["master_cycle"]:
                invalid("failure.master_cycle disagrees with selected event")
        if capture is not None:
            if selected is None and capture["first_event_index"] + \
                    len(selected_events) != capture["observed_events"]:
                invalid("capture without a failure must retain the latest events")
            if selected is None and len(selected_events) != \
                    min(capture["observed_events"], capture["capacity"]):
                invalid("capture without a failure must fill its bounded history")
            if capture["failure_cycle_truncated"] and (
                    selected is None or len(selected_events) != capture["capacity"]):
                invalid("truncated failure cycle requires a full capture and failure")
    return StartupLog(raw, records, input_record, result, trace, failure, events, capture)


def readable(value):
    if isinstance(value, list):
        return [readable(item) for item in value]
    if isinstance(value, dict):
        return {key: (f"0x{item:08x}" if key in HEX_FIELDS and type(item) is int
                      else readable(item)) for key, item in value.items()}
    return value


def execution_context(log):
    return dict(
        outcome=log.result["type"],
        execution_boundary=("incomplete_cycle" if
                            log.result["type"] == "startup_runtime_error"
                            else "complete_cycle"),
        runtime_error=log.result.get("message"),
    )


def summary(log):
    return dict(
        **execution_context(log), input=log.input, run=log.result["run"],
        snapshot=readable(log.result["snapshot"]), coverage=log.coverage,
        trace=log.trace,
        first_failure=(readable(log.failure_record["failure"])
                       if log.failure_record is not None else None),
        failure_event_source=(log.failure_record.get("event_source", "prefix")
                              if log.failure_record is not None else None),
        failure_capture=({key: value for key, value in log.capture.items()
                          if key != "events"} if log.capture is not None else None),
        event_counts=dict(Counter(
            f"{SUBSYSTEMS.get(event['subsystem'], event['subsystem'])}:"
            f"{EVENTS.get(event['event_type'], event['event_type'])}"
            for event in log.events)),
        note=EVIDENCE_NOTE,
    )


def describe_event(index, event):
    row = dict(event_index=index, master_cycle=event["master_cycle"],
               subsystem=event["subsystem"], event_type=event["event_type"],
               values_raw=[f"0x{word:016x}" for word in event["values"]])
    cpu = CPUS.get(event["subsystem"])
    if cpu is not None:
        row["cpu"] = cpu
    row["event"] = EVENTS.get(event["event_type"], "unknown")
    if event["subsystem"] == 15:
        row["subsystem_name"] = "cdvd"
        if event["event_type"] in (23, 24):
            row["cdvd"] = event["cdvd"]
    values = event["values"]
    if cpu is not None and event["event_type"] == 5:
        row["payload"] = dict(pc=f"0x{values[0]:08x}", opcode=f"0x{values[1]:08x}",
                              operation_raw=f"0x{values[2]:016x}",
                              flags_raw=f"0x{values[3]:016x}")
    elif cpu is not None and event["event_type"] == 7:
        row["payload"] = dict(virtual_address=f"0x{values[0]:08x}",
                              data_low_raw=f"0x{values[1]:016x}",
                              data_high_raw=f"0x{values[2]:016x}",
                              metadata_raw=f"0x{values[3]:016x}")
    elif cpu == "ee" and event["event_type"] == 22:
        row["payload"] = dict(byte=values[0],
                              physical_address=f"0x{values[1]:08x}")
    return row


def select_events(log, arguments):
    if log.trace is None:
        raise ValueError("no trace was exported in this file")
    if arguments.from_cycle > arguments.to_cycle:
        raise ValueError("--from-cycle must not exceed --to-cycle")
    captured = arguments.source == "failure_capture"
    if captured and log.capture is None:
        raise ValueError("no failure capture was exported in this file")
    events = log.capture["events"] if captured else log.events
    selected = []
    for index, event in enumerate(events):
        cpu, kind, values = event["subsystem"], event["event_type"], event["values"]
        cycle = event["master_cycle"]
        if not arguments.from_cycle <= cycle <= arguments.to_cycle:
            continue
        if arguments.cycle is not None and cycle != arguments.cycle:
            continue
        if arguments.cpu is not None and CPUS.get(cpu) != arguments.cpu:
            continue
        if arguments.event is not None and kind != arguments.event:
            continue
        if arguments.command == "instructions" and (cpu not in CPUS or kind != 5):
            continue
        if arguments.pc is not None and not (
                cpu in CPUS and kind in (5, 21) and values[0] == arguments.pc):
            continue
        if arguments.address is not None and not (
                cpu in CPUS and kind == 7 and values[0] == arguments.address):
            continue
        selected.append((index, event))
    rows = [describe_event(index, event)
            for index, event in selected[:arguments.limit]]
    if captured:
        for row in rows:
            row["global_event_index"] = log.capture["first_event_index"] + row["event_index"]
    return dict(**execution_context(log),
                coverage="bounded_failure_capture" if captured else log.coverage,
                event_source=arguments.source,
                trace=log.trace, matched_events=len(selected),
                limited=len(selected) > arguments.limit,
                events=rows,
                note=EVIDENCE_NOTE)


def first_difference(left, right, path="$"):
    if type(left) is not type(right):
        return path
    if isinstance(left, dict):
        for key in sorted(left.keys() | right.keys()):
            child = f"{path}.{key}"
            if key not in left or key not in right:
                return child
            difference = first_difference(left[key], right[key], child)
            if difference is not None:
                return difference
    elif isinstance(left, list):
        for index, (a, b) in enumerate(zip(left, right)):
            difference = first_difference(a, b, f"{path}[{index}]")
            if difference is not None:
                return difference
        if len(left) != len(right):
            return f"{path}.length"
    elif left != right:
        return path
    return None


def continuation_view(log, traced):
    input_record = dict(log.input)
    start = input_record.pop("start_master_cycle", 0)
    input_record.pop("start")
    trace_start = input_record.pop("trace_start_cycle", start)
    result = dict(log.result)
    if result["run"] is not None:
        result["run"] = dict(cycle_limit_reached=result["run"]["cycle_limit_reached"])
    view = dict(input=input_record, result=result)
    if traced:
        view.update(trace_start_cycle=trace_start, trace=log.trace,
                    events=log.events, capture=log.capture,
                    first_failure=log.failure_record)
    return view


def compare(left, right, mode):
    if mode in ("trace", "continuation-trace"):
        if left.trace is None or right.trace is None:
            raise ValueError("trace comparison requires two traced files")
    if mode == "trace":
        a, b = left.records, right.records
    elif mode in ("continuation", "continuation-trace"):
        a, b = (continuation_view(log, mode == "continuation-trace")
                for log in (left, right))
    else:
        a, b = (dict(input=log.input, result=log.result) for log in (left, right))
    difference = first_difference(a, b)
    return dict(mode=mode, equal=difference is None,
                byte_equal=left.raw == right.raw, first_difference=difference,
                left_coverage=left.coverage, right_coverage=right.coverage,
                note=EVIDENCE_NOTE)


def number(text, bits=64):
    try:
        return uint(int(text, 16 if text.lower().startswith("0x") else 10),
                    text, bits)
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error


def event_number(text):
    return EVENT_FILTERS[text] if text in EVENT_FILTERS else number(text, 8)


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("summary", "events", "instructions", "compare"):
        sub = commands.add_parser(command)
        sub.add_argument("file", type=Path)
        if command == "compare":
            sub.add_argument("other", type=Path)
            sub.add_argument("--mode", choices=(
                "run", "trace", "continuation", "continuation-trace"), default="run")
        elif command != "summary":
            sub.add_argument("--source", choices=("prefix", "failure_capture"),
                             default="prefix",
                             help="Retained event source (default: prefix).")
            sub.add_argument("--cpu", type=str.lower, choices=("ee", "iop"))
            sub.add_argument("--cycle", type=number)
            sub.add_argument("--from-cycle", type=number, default=0)
            sub.add_argument("--to-cycle", type=number, default=(1 << 64) - 1)
            sub.add_argument("--pc", type=lambda text: number(text, 32),
                             help="Issued/fault PC; never inferred for memory events.")
            sub.add_argument("--address", type=lambda text: number(text, 32),
                             help="MemoryAccess virtual address only.")
            sub.add_argument("--limit", type=number, default=200,
                             help="Maximum displayed events (default: 200).")
            if command == "events":
                sub.add_argument("--event", type=event_number,
                                 help="Numeric ID or instruction/memory/exception/fault/"
                                      "serial_transmit_completed/command_accepted/"
                                      "command_completed.")
            else:
                sub.set_defaults(event=None)
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    try:
        log = load_log(arguments.file)
        if arguments.command == "summary":
            report = summary(log)
        elif arguments.command == "compare":
            report = compare(log, load_log(arguments.other), arguments.mode)
        else:
            report = select_events(log, arguments)
        print(json.dumps(report, indent=2, sort_keys=True, allow_nan=False))
        return 1 if arguments.command == "compare" and not report["equal"] else 0
    except (OSError, EOFError, UnicodeError, ValueError, zlib.error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
