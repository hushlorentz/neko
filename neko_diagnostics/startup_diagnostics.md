# Startup Diagnostics API

Link `neko_diagnostics` and include `startup_diagnostics.hpp`. These host-side
helpers inspect the implemented machine; they do not establish BIOS readiness
or successful startup.

## Capture

```cpp
#include <iostream>
#include "startup_diagnostics.hpp"

// romBytes contains an external, legally obtained 4 MiB image.
NekoSystem system;
const auto report =
  runDualCPUBIOSWithStartupSummary(system, romBytes, 1000000);
writeStartupRunSummaryJsonLine(std::cout, report);

// Continue without installing/resetting/releasing anything.
const auto continued = runDualCPUWithStartupSummary(system, 1000);

// Inspect at any completed master-cycle boundary without executing work.
const auto snapshot = inspectStartup(system);
writeStartupSummaryJsonLine(std::cout, snapshot);
```

The wrappers delegate to the corresponding `NekoSystem` runners and capture
immediately after return. BIOS admission requirements and exceptions, guest
exception-handler execution, exact shared master budgets, and host-stop
precedence remain unchanged. A stop does not halt the peer or cancel devices.
`StartupRunSummary::execution` is the original runner result;
`startup` is a value snapshot, independent of subsequent machine changes.

`inspectStartup(const NekoSystem&)` uses const owner getters, not guest MMIO
reads. In particular, it does not perform the IOP INTC control-register read
that would clear its enable latch. It does not advance clocks, acknowledge
interrupts, drain SIF data, enable/consume trace collection, or mutate caches.
Calls must be serialized between completed master cycles or after synchronous
execution has unwound, not made from an in-cycle observation callback or
concurrently with execution. Inspection after an execution error describes a
possibly incomplete cycle, not a completed boundary.

## Meaning and limits

The snapshot contains:

- Absolute master time; EE/IOP cycle totals; IOP retired-instruction total;
  both PCs, run states, stop reasons, and COP0 status/cause/EPC/BadVAddr.
- EE rejected-instruction and retained exception metadata; the IOP's last-step
  exception entry event and retained rejected physical-store details.
- Canonical SIF mailboxes, flags, raw CTRL, IOP doorbell, directional word
  counts, shared free-word count, and SIF0 packing cursor/latch.
- EE DMAC control/status/interleave size, priority control, stall address,
  interrupt output, and SIF grant order, plus implemented channels 1 (VIF1),
  2 (GIF), 5 (SIF0), 6 (SIF1), 8 (fromSPR), and 9 (toSPR).
- EE/IOP INTC latches, masks, request outputs, and IOP control enable.
  DMAC and EE INTC request outputs remain separate; these are not claims that
  the CPU has admitted an interrupt.
- Both IOP DMA banks' priority/interrupt words and aggregate request, effective
  SIF grant order and retained tie-break channel, plus channels 9 (SIF0) and
  10 (SIF1): control, MADR, BCR, applicable TADR, packet remainder, terminal bit,
  active/enable state, priority, and side-effect-free next-beat inspection.

Run progress is **per call**: EE instruction counts are accepted instructions,
IOP instruction counts are retired instructions. Snapshot cycle/retirement
counts are **absolute**, not per-call deltas.

`clockEligible` reports a DMA owner's clock eligibility (`clockActive()` for
the existing non-SIF engines, `transferEnabled()` for EE SIF), not guaranteed
progress or sink readiness. An asserted channel START with DMA disabled is
controller-gated. EE SIF additionally distinguishes active START from the
common controller's channel enable, including D_PCR gating.
`lastTransferStalled` is the retained last-attempt flag for
VIF1/GIF; it may remain set after the controller is disabled or the sink changes.
Scratchpad and SIF channels do not expose a last-stall flag. SIF counts show
empty/full/partially occupied storage, not a decoded guest wait reason.
For IOP SIF1, zero `remainingWords` means awaiting a four-word header while TR
is set; a nonzero remainder means payload. `nextBeat` is the result of a
hypothetical grant now, not the last attempted beat or a promise of an IOP
clock pulse. `wait` with TR clear or enable clear is channel/controller gating;
otherwise it means insufficient header/payload words. `accepted` is one
nonterminal grant, `completed` is a terminal grant, and `rejected` identifies
an unaligned header or invalid RAM destination. Address, value, and bus status
describe that available header word or 32-bit physical payload write; they are
zero/default when the receiver is waiting. Header acceptance does not
prevalidate payload memory, and inspection never performs a write or consumes
queued words.

EE SIF snapshots include the owner's structural `nextBeatKind` (source tag,
destination tag, payload, or completion) and a copied `DMACChannelContinuation`.
The terminal field is the retained terminate-after-packet flag, not a new
completion prediction; normal transfers also end when QWC reaches zero.
SIF0 exposes whether its destination packet was accepted, distinguishing it
from preloaded QWC before a tag. SIF1 exposes the remaining QWC of an already
admitted stall slice. These facts are not reconstructed from retained CHCR
tag bits, and they reset with their owner's existing completion/abort rules.
Neither structural beat kind nor eligibility validates an EE address or
predicts a transport, scratchpad, or stall-control grant.

For IOP SIF0, zero `remainingWords` selects the six-word descriptor/prologue
phase; otherwise the endpoint is sending individual payload words.
`nextBeat` uses the common controller's existing const inspection path for
both IOP channels. A source rejection identifies a descriptor read, unaligned
source address, or payload read. A source wait may retain attempted RAM
address/value evidence because source validation precedes transport admission;
it need not have zero fields. Inspection does not advance TADR, consume a
descriptor, write memory, change continuation, or latch completion/interrupts.
BCR remains a raw stored register, not the packet remainder.

Grant orders are the existing owners' candidate order at inspection time,
including gated or inactive endpoints, not two promised grants. The scheduler
accepts at most one EE SIF grant per master cycle and one IOP SIF grant per IOP
pulse. The EE order reflects its rotating preference; the IOP order applies
channel priority first, then its retained tie breaker. The separately reported
IOP tie-break channel can therefore differ from the first candidate while
priorities are unequal. No polling, readiness, arbitration victory, or deadlock
is inferred from these values. SIF0's packing latch is included because its
retained lanes determine final partial-quadword fill.

There is no inferred polling-loop detection, deadlock verdict, synthetic
readiness, placeholder IOP DMA state, or per-poll log. Scheduled IOP SIF1
rejections throw `IOPDMATransferError`, carrying the typed channel, cause,
physical address, word, and bus status. The rejected beat changes neither RAM,
stream, continuation, nor completion status; earlier phases of that master
cycle are not rolled back. This is not a fabricated guest DMA bus exception
or a retained failure latch. Callers can inspect the still-pending beat after
the error.

An unsupported EE instruction is a host stop, whereas an EE rejected bus
access can enter a running guest exception handler. IOP rejected physical
stores carry width, value, virtual/physical address, access outcome, cache
route, and merge lanes. `IOPStoreWidth::None` means no retained store failure.
EE exception metadata is retained; the IOP exception event clears at its next
step, while its COP0 handler context remains. This is current retained
evidence, not a complete history of rejected accesses or a reconstruction of
an instruction by rereading guest memory.

## JSON lines

The writers emit one decimal, locale-independent JSON object followed by a
newline. On-demand records have `type: "startup_summary"` and `snapshot`.
Run records have `type: "startup_run_summary"`, `run`, and `snapshot`.
`run` includes master/CPU deltas, the budget-exhaustion flag, and explicitly
named accepted/retired instruction counts. Enum names are lowercase strings;
store width is in bytes. An absent IOP store failure is JSON `null`.

The DMA array order is 1, 2, 8, 9, 5, 6, preserving the earlier four indices.
Channels 5/8 have no TADR; only channels 8/9 have SADR. These absent JSON
registers are `null` (the corresponding C++ snapshot fields are zero and not
applicable). No unimplemented channel is reported as an idle engine.
EE channels 5/6 add a `sif` object with `active`, `enabled`, `next_beat_kind`,
and `continuation` (`terminal`, `destination_packet_accepted`,
`slice_qwc_remaining`). The destination-packet flag is null on SIF1, the slice
remainder is null on SIF0, and `last_transfer_stalled` is null on both.

`dmac` adds `priority_control`, `stall_address`, and `sif_grant_order` (direction
names). `iop_dma` adds `sif0` beside the existing `sif1`, `sif_grant_order`
(channel numbers), and `sif_tie_break_channel`. Each endpoint includes its
registers, `remaining_words`, `terminal`, `active`, `enabled`, `priority`, and
`next_beat`; TADR is null on channel 10. `sif` adds `packing_latch`, four words
in lane order. These are additive JSON fields and endpoints; the record types
and existing field meanings remain unchanged. In C++, the former flat IOP
channel-10 fields now live under `StartupIOPDMASummary::sif1`; `sif0` has the
same focused channel-summary type.

Writers preserve the destination stream's formatting and locale, and use
normal ostream error reporting. Unknown named snapshot enum values throw
`std::invalid_argument` before any record is emitted. Output formatting
allocates only when explicitly requested; inspection uses fixed-size values.
Nothing is automatically printed or attached to core runner results, and
the existing EE-only `--bios` frontend is unchanged.

## Headless discovery command

`neko bios-startup <file> --cycles <count> [--trace-events <count> [--failure-context]]` wraps the
cold-start API without changing its execution policy. The master-cycle ceiling
is required and accepts an unsigned
64-bit decimal integer, including zero. The image must be exactly 4 MiB.
Both CPUs start at their reset vectors with the ordinary reset state and
shared ROM; no guest registers, RAM, readiness flags, or replies are seeded.

The first JSON line has `type: "bios_startup_input"`, `rom_bytes`,
`rom_fnv1a64` (a fixed-width hexadecimal compatibility fingerprint, **not a
cryptographic digest**), `max_master_cycles`, and
`start: "cold_dual_reset_vectors"`. The second is the existing
`startup_run_summary`. No ROM bytes or filenames are included.

Status 0 means budget exhaustion (including a zero-budget release), not
successful startup; status 1 means the runner returned a CPU host stop.
Status 2 reports invalid arguments, file errors, or runtime/output errors
on stderr. After successful BIOS admission, an execution exception retains the
input record and emits `startup_runtime_error` instead of a successful
`startup_run_summary`. Guest exception
handlers still execute under the existing runner policy: the ending snapshot
is retained state, not the first rejected-access history or proof of deadlock.
No tracing is enabled unless `--trace-events` is supplied.

The snapshot includes all four implemented SIF DMA endpoints and their common
controller evidence. It does not infer an exact guest request/response
checkpoint from ending register values.

## Bounded trace export

`--trace-events <count>` enables collection after cold reset/release and before
the first executed cycle. It requires an unsigned 64-bit decimal event ceiling;
zero retains no events. The original input and run-summary lines are unchanged.
They are followed by one `startup_trace_summary` line and one `trace_event` line
per retained event, in the original collection order:

```json
{"type":"startup_trace_summary","event_limit":1,"captured_events":1,"truncated":true}
{"type":"trace_event","master_cycle":1,"subsystem":10,"event_type":5,"values":[3217031168,608305153,34,0]}
```

The event above illustrates the record shape, not a particular BIOS result.
`subsystem` and `event_type` are the stable numeric identities from
`regression_trace.hpp`; `values` contains its four raw payload words.
EE event 22 (`SerialTransmitCompleted`) records the raw byte in `values[0]`
and physical TXFIFO address in `values[1]`; the other words are zero. It
precedes EE execution observations on the completion tick, independently of
the earlier accepted store. The provisional fixed serial profile completes
after 2,560 master ticks; reset cancellation emits no completion. Trace
capacity and enablement do not affect serial progress or retain output.
Numbers are decimal, including 64-bit words: consumers must retain integer
precision. A decoded `startup_first_failure` record follows the raw events.

The collector retains the earliest events, never a rolling window. Truncation
means at least one event was omitted, not merely that the collection reached
its ceiling. An exact-fit run is not truncated; a zero-budget run has an empty,
non-truncated trace even with a zero event ceiling. A truncated prefix may end
within a cycle or between related events, and an absent event does not prove
that its operation never occurred. The master budget, guest exception handlers,
CPU stop precedence, snapshots, and host exit status remain unchanged.

The limit bounds retained event count at insertion, not just output size.
It does not preallocate the requested ceiling, shrink previously allocated
vector capacity, or impose a byte budget. Enabled tracing still incurs normal
observation costs after the prefix fills. Output is formatted one record at a
time after execution rather than building another complete trace in memory.
Runtime errors export the retained prefix when tracing was requested. This is
best-effort reporting: allocation, inspection, or output failure can still
prevent some evidence from being emitted. The original execution error is
written to stderr before formatting/output, so a later output error does not
replace it.

The same boundary is available to host tools:

```cpp
NekoSystem system;
system.runDualCPUBIOS(romBytes, 0); // Reset clears trace controls/history.
system.startTrace(10000);
const auto report = runDualCPUWithStartupSummary(system, 1000000);
writeStartupRunSummaryJsonLine(std::cout, report);
writeStartupTraceJsonLines(std::cout, system);
```

`startTrace(limit)` clears history/loss metadata and starts bounded collection;
the existing `startTrace()` starts unlimited collection.
`traceEventLimit()` and `traceTruncated()` inspect host-only capture state.
`clearTrace()` clears events/truncation but keeps the ceiling and enable state.
`stopTrace()` disables capture while retaining events, the ceiling, and
truncation. Whole-machine reset clears all of them (disabled, ceiling zero).
Save states exclude capture controls/history; loading preserves the
destination's controls, retained events, and truncation. Trace hashes hash
retained events only, so equal hashes alone do not establish equal coverage.

### Opt-in first-failure flight recorder

Add `--failure-context` after `--trace-events <count>` to retain failure
context independently of prefix truncation, including with a zero prefix
ceiling. This does not increase the cycle budget or prefix event limit.
Ordinary traced and untraced commands remain unchanged.

The diagnostics-owned recorder keeps at most 4,096 raw events in a fixed
ring. It uses the same C++ failure predicates as ordinary first-failure
attribution, then freezes after the first failure's master cycle. It retains
preceding events and same-cycle fault/exception context in collection order.
The bound is **events**, not a guaranteed number of cycles. If the failure
cycle alone exceeds capacity, the trigger is preserved, later events are
discarded and `failure_cycle_truncated` is set. Incomplete runtime-error
boundaries remain explicit; capture never suppresses guest handlers or stops
the run at the failure.

An additional `startup_failure_capture` JSON line appears after prefix events
and before `startup_first_failure`. It contains `capacity`, `observed_events`,
`first_event_index`, `failure_cycle_truncated` and an ordered `events` array
with the usual raw event fields. `observed_events` counts the whole observed
stream, including events after freezing; `first_event_index` identifies the
window's first event in that stream. The final failure record adds
`event_source: "failure_capture"`; its `failure.event_index` indexes the
window, **not** the prefix. C++ remains the sole failure/packed-memory decoder.
A null failure does not establish boot or handshake success.

Host code can pass a shared `StartupFailureCapture` to
`startTrace(limit, capture)`, then call `writeStartupFailureCaptureJsonLine`
and pass `capture.get()` as the final argument of
`writeStartupFirstFailureJsonLine`. The system owns the passive, nonthrowing
observer attachment until stop, replacement or reset; it receives events
before prefix limiting. `clearTrace()` clears only the prefix, not the
observer's session. Capture state/attachments are not serialized or hashed;
load preserves destination diagnostics as with existing tracing. Use a fresh
recorder/session after rewinding machine state. There is no per-event
allocation, hardware-state mutation or text formatting during capture.
Only explicitly enabled diagnostic runs incur recorder storage and dispatch.

## Offline inspection

`tools/inspect_startup.py` reads saved `neko bios-startup` JSONL reports using
only the Python 3 standard library. It never opens a BIOS, executes guest code,
or changes the input files. Commands emit deterministic, readable JSON:

```sh
python3 -B tools/inspect_startup.py summary run.jsonl
python3 -B tools/inspect_startup.py events run.jsonl --cpu ee --cycle 41
python3 -B tools/inspect_startup.py events run.jsonl --event memory --address 0x70003fe0
python3 -B tools/inspect_startup.py instructions run.jsonl --cpu iop --from-cycle 200 --to-cycle 241
python3 -B tools/inspect_startup.py compare run.jsonl repeat.jsonl --mode trace
python3 -B tools/inspect_startup.py compare run.jsonl untraced.jsonl
```

`summary` shows ROM identity, execution counters, the full ending snapshot,
capture coverage/counts, and the already decoded `startup_first_failure`.
Addresses, instruction bits, and selected architectural registers display as
hexadecimal; integer precision is preserved. The originating first failure
is not replaced by a later handler failure or ending host stop.

`events` filters the ordered retained events. Both event commands default to
`--source prefix`; use `--source failure_capture` to select the bounded window.
Requesting an absent window is an error, never a fallback to the prefix.
`--cpu` selects EE or IOP;
without it, all subsystems are included. `--cycle`, `--from-cycle`, and
`--to-cycle` are inclusive master-cycle constraints. `--event` accepts a
numeric ID, `instruction`, `memory`, `exception`, `fault`, or
`serial_transmit_completed` (the other full event names also work).
Serial completion payloads expose the unconverted byte and physical endpoint.
Unknown numeric identities remain raw, not silently
discarded or interpreted as a known subsystem/event.

`instructions` selects only EE/IOP `InstructionIssued` events and shows PC,
raw opcode, and raw operation/flag words. It does not disassemble instructions
or treat issued events as retired instructions. Both event commands support
`--pc` for issued/fault-context PCs and `--address` for memory-event virtual
addresses; these filters never guess instruction attribution for memory
events. Filters are combined with AND. Decimal and `0x` numbers are accepted.

Memory payloads explicitly distinguish virtual address, raw data words, and
raw packed metadata. Arbitrary memory events are **not** bitfield-decoded in
Python; authoritative decoded failure access kinds/outcomes come from the
existing C++ exporter. Failed-read data must not be treated as valid values.

Event output defaults to at most 200 matches; `--limit` changes that display
bound. `event_index` is the zero-based index within the selected source.
Window rows also include `global_event_index` in the observed stream.
`matched_events` and `limited` distinguish no matches from display clipping.
This bound does not limit file loading: input records are loaded into memory.

Summary and event reports retain the execution outcome, boundary, and any
runtime-error message. `coverage` independently distinguishes `untraced`,
`retained_trace`, and `truncated_prefix`; selected failure-window views use
`bounded_failure_capture`. Summary reports include `failure_event_source`
and capture metadata separately from prefix coverage. Untruncated capture does not imply
a completed final cycle, and a null failure/no match never establishes
fault-free execution, boot, or handshake success.

`compare` defaults to `--mode run`: compare input identity/budget and the
complete run result/snapshot, deliberately excluding trace-only records.
This supports traced/untraced equivalence checks. `--mode trace` requires two
traced files and compares every parsed record, preserving event order and
capture metadata. Neither mode discards snapshot fields or normalizes away
execution differences. Reports also include exact `byte_equal` and the path
of the first parsed difference. JSON whitespace/object-key order do not
affect parsed equality, but do affect byte equality.

Exit status is 0 for inspection/equality, 1 for a comparison mismatch, and 2
for an argument, file, or validation error. These statuses describe the
inspection operation, not the BIOS execution outcome. Missing/duplicate or
unknown record types, malformed JSON, inconsistent counts/boundaries, and
out-of-range raw words are errors, not empty successful reports. Additive
record fields are retained. A missing final record is an incomplete file;
the producer's explicit trace truncation is valid evidence and is reported
separately. An input error produces no successful JSON report.

Run the inspector's BIOS-free tests independently of the C++ build:

```sh
python3 -B -m unittest discover -s tools -p 'test_inspect_startup.py' -v
```

## Repeatable discovery batch

From the repository root, run:

```sh
python3 -B tools/run_bios_discovery.py out/bios-discovery/after-ier
```

The default input is `local_integration/bios/SCPH-70012.bin`; use
`--bios "<path>"` for a different input. The helper configures `out/check`
if needed, builds only the optimized assertion-enabled `neko` target,
then runs two traced and two genuinely untraced cold starts. Every run
uses 1,000,000 master cycles; traced runs retain at most 3,000,000 events.
The helper does not raise these limits or run tests.
Its optional `--failure-context` flag enables the bounded recorder only on
the two traced runs; both untraced runs remain genuinely untraced. The
manifest records this choice and automatic first-failure windows select the
exporter's indicated event source.

The artifact directory must not already exist, even if empty. Build and
inspection stdout/stderr, four raw JSONL reports and separate run stderr
are retained. `manifest.json` records exact commands and exit statuses,
including a pending command if interrupted. Errors retain partial evidence;
use a new directory for the next attempt.

The existing inspector produces `summary.json`, `compare-traces.json`,
`compare-traced-plain.json` and `compare-plain.json`, including parsed and
byte-equality verdicts. If the exported first failure is non-null, its CPU
and cycle select `first-failure-instructions.json` and
`first-failure-events.json`: inclusive +/-16-cycle windows, capped at 200
displayed matches each. DMA failures without CPU attribution remain
unfiltered by CPU. No memory metadata is decoded again in the helper.

BIOS exit 1 is an expected CPU host stop and does not skip repetitions or
inspection. Any other nonzero BIOS exit aborts with its output preserved.
The helper itself exits 0 for matching comparisons, 1 for a parsed comparison
mismatch, 2 for build/run/inspection errors, or 130 for interruption. It
continues all three comparisons after a mismatch. Matching runs, a reached
cycle ceiling, or a null first failure do **not** establish boot or handshake
success; inspect the reported trace coverage and originating failure.

Run the BIOS-free workflow and inspector tests with:

```sh
python3 -B -m unittest discover -s tools -p 'test_*.py' -v
```

## First captured failure

`firstStartupFailure(trace)` performs a read-only, linear scan of the retained
ordered events and returns fixed-size evidence. `found == false` means no
qualifying failure was captured, **not** that execution was fault-free. The
writer `writeStartupFirstFailureJsonLine(output, system, boundary)` emits:

- `type: "startup_first_failure"`, `trace_truncated`, `execution_boundary`
  (`complete_cycle` or `incomplete_cycle`), and nullable `failure`.
- A failure's `kind` (`memory_access`, `instruction_rejection`, or
  `dma_rejection`), nullable `cpu`, zero-based `event_index`, and `master_cycle`.
  Selection preserves collection order, including equal timestamps; it does
  not claim a new global bus ordering between independently collected owners.
- Nullable `instruction`: `pc`, nullable raw `bits`, nullable `delay_slot`,
  and nullable `branch_pc`. These are originating instruction facts, not a
  reread of guest memory or a substitution of the ending PC.
- Nullable `access`: access kind, virtual and nullable resolved physical
  address, byte width, write flag, and two raw data words. EE accesses include
  translation outcome, cache route/access/attribute, scratchpad selection,
  transfer outcome, and failure phase. IOP accesses include outcome, cache
  route, and merge byte enables. Failed reads' raw data are not valid values.
- Nullable `dma`: endpoint, beat kind, rejection cause, and attempted address.
  This address retains any EE scratchpad selector; it is not a CPU virtual
  address. DMA rejection never receives a CPU instruction attribution.
- Nullable `exception`: architectural `code`, `vector`, `cause`, and either
  EE `fault_address` or IOP `epc`. Nullable `rejection_reason` distinguishes
  unsupported/undefined host instructions from a guest instruction exception.

Qualifying events are failed EE translation/physical transfers, failed IOP
accesses, reserved/unusable instruction exceptions, undefined/unsupported
instruction host stops, and rejected SIF DMA beats. EE stalls/device waits,
successful accesses, interrupts, deliberate syscall/breakpoint traps, and
normal DMA waits/completion are not failures for this selection. A captured
fetch failure can be speculative and need not ever become a guest exception.
No selected failure establishes an emulator defect or changes guest-handler
execution.

EE trace event 21, `FaultContext`, is an append-only extension emitted just
before a non-interrupt exception entry, or alone at an instruction host stop.
Its words are originating PC, opcode, branch PC, and flags (`instruction
available` bit 0, `delay slot` bit 1, `host rejection` bit 2, EE stop reason in
bits 8-15). Fetch faults have no available opcode. The context comes from the
actual transition request, including delayed COP1 ownership; retained COP0
EPC/BD can describe an older nested exception and is not used to reconstruct
the origin. Existing event identities and payloads are unchanged, but faulting
traces gain records and therefore different event counts/hashes.

Data-fault association uses the same-cycle transition following that memory
attempt, without crossing another CPU issue/access/retirement. Fetch context
is associated only when a same-cycle transition identifies that exact unfetched
PC; a prefetched fault resolved later remains unattributed rather than guessing
whether intervening control flow discarded it. IOP attempt context comes from
the captured step's instruction-issued event; failed fetches have only their
attempted PC until an entry supplies delay context. For IOP memory faults and
instruction rejections in a captured delay slot, that entry's EPC supplies the
branch PC. Missing/capped context is
JSON `null`, not zero/false. C++ availability fields explicitly distinguish
these cases. The bounded collector can end between a memory attempt, its
context, and exception entry; no analysis runs beyond that prefix.

## Runtime-error evidence

After admission, host tools can use the opt-in error-capturing wrapper:

```cpp
system.runDualCPUBIOS(romBytes, 0); // Admission errors still throw.
system.startTrace(10000);
const auto evidence = runDualCPUWithStartupEvidence(system, 1000000);
if (evidence.outcome == StartupRunOutcome::RuntimeError)
{
  writeStartupRuntimeErrorJsonLine(std::cout, evidence.error);
  writeStartupTraceJsonLines(std::cout, system);
  writeStartupFirstFailureJsonLine(
    std::cout, system, StartupExecutionBoundary::IncompleteCycle);
}
else
{
  writeStartupRunSummaryJsonLine(std::cout, evidence.completed);
}
```

Only the `StartupRunEvidence` member selected by `outcome` is valid. The older
summary wrappers still propagate execution exceptions unchanged. Error
capture delegates to the original runner; it does not retry, step extra
cycles, halt the peer, or roll back completed effects.

`startup_runtime_error` contains a JSON-escaped `message`, `snapshot`,
`execution_boundary: "incomplete_cycle"`, and `run: null`. There is deliberately
no completed-cycle delta or fabricated normal execution result: a scheduler
clock can advance before later phases fail, or an earlier phase can throw
before the clock advances. Snapshot CPU/clock counters retain their owner
meanings and do not certify completion of the failing cycle. Message escaping
preserves UTF-8 text and escapes JSON control characters, quotes, and backslashes.

The raw trace contains only events already published at the throw. Rejected
scheduled IOP DMA grants publish the executed prefix before throwing, without
admitting that pulse's IOP instruction. Other exceptions can leave unpublished
in-cycle observations; the last retained timestamp is not proof that every
phase of that cycle ran. Capture truncation and execution incompleteness are
independent. Nothing enables tracing implicitly, and successful no-trace
command output remains the original two lines.
