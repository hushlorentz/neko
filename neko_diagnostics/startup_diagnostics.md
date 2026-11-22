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
- EE DMAC control/status/interleave size, raw hold control, priority control,
  stall address, interrupt output, and SIF grant order, plus implemented channels 1 (VIF1),
  2 (GIF), 5 (SIF0), 6 (SIF1), 8 (fromSPR), and 9 (toSPR).
- EE/IOP INTC latches, masks, request outputs, and IOP control enable.
  DMAC and EE INTC request outputs remain separate; these are not claims that
  the CPU has admitted an interrupt.
- Both IOP DMA banks' priority/interrupt words and aggregate request, effective
  SIF grant order and retained tie-break channel, plus channels 9 (SIF0) and
  10 (SIF1): control, MADR, BCR, applicable TADR, packet remainder, terminal bit,
  active/enable state, priority, and side-effect-free next-beat inspection.
- SIO2 ingress phase/count (`sio2.input_phase`/`input_bytes`) and input channel
  11 programming, active/enable state and remaining words (`iop_dma.sio2_input`).
  The separate `dma_result_phase` is `empty`, `pending`, or `ready`.
  A ready absent-card probe exposes `dma_result_bytes:144`, split into
  `dma_logical_bytes:4` no-ACK samples and `dma_padding_bytes:140` zero padding;
  the bounded absent-card `f3` profile instead has five no-ACK samples and
  139 padding bytes. Counts describe unread bytes: after one output word,
  `f3` retains one logical byte and 139 padding bytes. Pending/empty results
  expose zero bytes. These are provisional absent-device compatibility
  profiles, not successful card replies. Output channel12
  (`iop_dma.sio2_output`) distinguishes `inactive`, `armed_waiting_results`,
  and `armed_results_ready`. Arming or ready results do not imply output
  transport or DMA completion. The inspector still accepts older snapshots
  without result fields.

Run progress is **per call**: EE instruction counts are accepted instructions,
IOP instruction counts are retired instructions. Snapshot cycle/retirement
counts are **absolute**, not per-call deltas.

`clockEligible` reports a DMA owner's clock eligibility (`clockActive()` for
the existing non-SIF engines, `transferEnabled()` for EE SIF), not guaranteed
progress or sink readiness. An asserted channel START with DMA disabled is
controller-gated, as is an asserted START while DMAC hold is set.
EE SIF additionally distinguishes active START from the
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

`StartupDMACSummary::holdControl` is the controller's retained 32-bit word,
serialized as decimal `dmac.hold_control`, including reserved bits. Bit 16
is the hold state; the remaining bits are not decoded as additional behavior.
An inspected reset controller reports `0x1201` (decimal 4609). Inspection reads
the const controller accessor, not either guest MMIO alias, and does not
advance or complete a transfer. The raw word is current retained evidence,
not a history of hold transitions or a claim about sink readiness.

Writers preserve the destination stream's formatting and locale, and use
normal ostream error reporting. Unknown named snapshot enum values throw
`std::invalid_argument` before any record is emitted. Output formatting
allocates only when explicitly requested; inspection uses fixed-size values.
Nothing is automatically printed or attached to core runner results, and
the existing EE-only `--bios` frontend is unchanged.

## Headless discovery command

`neko bios-startup <file> --cycles <count> [--trace-events <count> [--failure-context] [--trace-from-cycle <count>]] [--resume-checkpoint <file>] [--save-checkpoint <file>] [--scout | --stop-on-first-failure]` wraps the
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

`--save-checkpoint <file>` writes the existing canonical whole-machine state
after a complete-cycle run, including a CPU host stop. It never overwrites an
existing destination; write/close failures return2 with stderr, even if the
JSON run report has already been emitted. Incomplete-cycle runtime failures
never produce a checkpoint. Treat a partial file from an I/O failure as unusable.

`--resume-checkpoint <file>` installs the supplied external BIOS, restores the
state through the ordinary transactional codec, and continues without reset or
CPU reseeding. The codec verifies format/checksum and external ROM identities.
`--cycles` remains an **absolute master-cycle ceiling**: resuming at1000000
under a10000000 ceiling permits9000000 additional cycles. A ceiling before
the restored clock is rejected; equality permits a zero-cycle observation.
Input records use `start: "checkpoint"` and `start_master_cycle`; run counters
remain per-call progress, while snapshot clocks are absolute. Trace history
is not restored; tracing starts afresh at the restored boundary.

`--trace-from-cycle` delays tracing, not execution, until that completed
absolute cycle. It requires `--trace-events` and a start between the run's
initial clock and ceiling. The input adds `trace_start_cycle`; without that
field the trace origin is `start_master_cycle`, or zero for a cold start.
The untraced prefix still uses the normal runner and contributes to per-call
counters. Stops/errors before trace start remain explicit, but their event
history is unavailable: an empty failure window is not a fault-free verdict.

The snapshot includes all four implemented SIF DMA endpoints and their common
controller evidence. It does not infer an exact guest request/response
checkpoint from ending register values.

## Bounded trace export

`--trace-events <count>` enables collection before the first executed cycle
unless a later `--trace-from-cycle` is selected. It requires an unsigned 64-bit decimal event ceiling;
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
the earlier accepted store. The provisional BGR `0`/`0x1d` serial profiles
complete after 2,560/74,240 master ticks respectively, using the rate captured
when the byte was accepted. FIFO reset cancels without a completion and
preserves the next-byte baud selection; cold reset selects BGR zero. Trace
capacity and enablement do not affect serial progress or retain output.
Numbers are decimal, including 64-bit words: consumers must retain integer
precision. A decoded `startup_first_failure` record follows the raw events.

CDVD subsystem 15 events 23 (`CommandAccepted`) and 24 (`CommandCompleted`)
add a `cdvd` object alongside the unchanged four raw `values` words:

```json
{"transport":"s","opcode":54,"phase_before":"pending","phase_after":"result","cause_before":0,"cause_after":0,"rtc_ticks":43,"result":[0,8,0,65,65,101,110,103,65,85,0,0,0,0,0,0]}
```

This illustrates the approved provisional SCMD36 compatibility profile, not
measured console configuration.
`transport` is `n` or `s`; phases are `idle`, `pending`, or `result`.
Opcode and cause fields are unsigned byte integers, `rtc_ticks` is the
unsigned 64-bit RTC tick value, and `result` contains sixteen byte integers in
result order. Raw `values[2]` carries bytes0..7 and `values[3]` bytes8..15,
least-significant byte first in each word. Only S completion carries result
bytes; acceptance and N events carry sixteen zeros. Unused result bytes are
zero: version03 has four bytes, ReadClock08 eight, and SCMD36 fifteen.
No BCD/calendar conversion or readiness inference is made.
The exporter uses `nekoDecodeCDVDTrace`, not a second packed-field decoder.
Unknown transport or phase enum values throw `std::invalid_argument` before
the affected record is emitted rather than being labelled as a known state.
Other event identities retain their existing JSON bytes without a `cdvd`
object. The same shared event-field writer serves prefix and failure capture.

### SPU DMA and input observations

SPU2 subsystem 16 event 20 (`DMABeat`) adds `spu_dma` beside the unchanged
four raw `values` words:

```json
{"endpoint":"iop_spu1","channel":7,"core":1,"kind":"payload","outcome":"wait","failure":"none","bus_status":"none","attempted_address":4096,"remaining_before":17,"remaining_after":17,"target_half":1,"target_word":256}
```

Channels 4/7 pair with cores 0/1. Outcomes are `accepted`, `wait`, `completed`,
or `rejected`; only rejection carries `failure: "source_read"` and a
non-`none` bus status (`misaligned`, `read_only`, or `unmapped`). The physical
source address, remaining word counts, and captured target half/word describe
the attempted source beat, not a CPU access or a newly inferred request.
For PCM `kind: "payload"`, target halves are 0/1; word positions are 0..256,
including a full-half wait at 256. Acceptance advances the source count;
completion is the final accepted
word. Waiting and rejection leave it unchanged.

The raw beat encoding is channel/core/outcome/bus status in successive bytes
of `values[0]`, physical source in `values[1]`, remaining-before/after in the
low/high 32 bits of `values[2]`, and target half/word in the low byte and bits
8..23 of `values[3]`.

Normal sound-RAM writes instead use `kind: "normal_write"`:

```json
{"endpoint":"iop_spu0","channel":4,"core":0,"kind":"normal_write","outcome":"accepted","failure":"none","bus_status":"none","attempted_address":4096,"remaining_before":16,"remaining_after":15,"destination_before":12288,"destination_after":12290,"source_value":2309737967}
```

`destination_before`/`destination_after` are even 20-bit sound-RAM halfword
coordinates, advancing by 2 modulo `0x100000` on acceptance/completion and
unchanged on wait/rejection. `source_value` is the accepted 32-bit word, or
`null` on wait/rejection; these beats do not contain PCM target positions.
Their raw encoding sets bit32 of `values[0]`, packs source address/value in
the low/high 32 bits of `values[1]` (value 0 when unaccepted), and destination
before/after in the low/high 32 bits of `values[3]`. The remaining-count and
channel/core/outcome/status encodings are unchanged. Use
`nekoDecodeSPUDMABeat` for both typed variants.

SPU2 event 1 (`StateChanged`) with `values[0]` bit32 clear adds `spu_input`
with `channel`, `core`, and
`before`/`after` snapshots. Each snapshot contains `selected`, `write_open`,
`requested`, `fill_half`, `read_half`, and `fill_words`. These observe
selection, opening/cancellation, full-half request changes and reader-half
boundaries; ordinary partial-word progress and per-sample consumption do not
emit input events. `values[0]` packs channel/core in successive bytes,
`values[1]`/`values[2]` pack the snapshots, and `values[3]` is zero. Snapshot
bits 0/1/2 are write-open/selected/requested, fill/read half occupy successive
bytes beginning at bits 8/16, and fill-word count occupies bits 32..47.
Use `nekoDecodeSPUInput`, not a second packed-field decoder.

SPU2 event24 (`CommandCompleted`) adds `spu_command`: `core`, `command`
(`koff`/`kon`), consumed24-bit `mask`, and `sample_position` (the old shared
ring index0..511). Raw values are core | command<<8 (KOFF0/KON1), mask,
position, zero. This means consumption, not the earlier CPU command write.

SPU2 event1 with `values[0]` bit32 set adds `spu_voice`: `core`, `voice`
(0..23), `reason`, `phase_before`/`phase_after`, `level_before`/`level_after`,
and `sample_position`. Reasons are `kon`, `koff`, `endpoint`,
`envelope_phase`, and `envelope_zero` (raw0..4); phases are stopped, attack,
decay, sustain, release (raw0..4); levels are0..32767. Raw values are
bit32 | core | voice<<8 | reason<<16, before phase | level<<8, after
phase | level<<8, position. Use `nekoDecodeSPUCommand`/`nekoDecodeSPUVoice`.
Retrigger and repeated live KOFF emit even without a phase change. Repeating
endpoints emit; a nonrepeat endpoint stop emits no duplicate envelope-zero
event. Ordinary per-sample level changes are intentionally not logged.

The existing shared observation sequence orders a source beat before its
resulting input transitions, then completion interrupt and CPU observations.
Sample-edge command/lifecycle events precede DMA and CPU work. The SPU
journal's conservative bound is204: four events per voice (KOFF, KON,
endpoint, envelope transition) across48voices, four command consumptions and
eight existing input transitions. Overflow is a fatal invariant violation,
not dropped evidence. Seven common DMA attempts (two EE and five IOP
candidates) retain their separate journal per scheduled master cycle.
They are enabled only for trace/failure-probe observation, with no canonical
diagnostic state. Host setup outside scheduled observation does not produce
these events. Failure-only probes retain the originating failure rather than
the preceding successful lifecycle events; full traces/captures retain those
events subject to their existing limits.

An identical blocked beat emits only its first wait in an observation
session. Changed position or a newly available request rearms it; starting
a fresh trace/probe session can report an already blocked endpoint once.
`clearTrace()` only clears the retained prefix, not this session history.
Loading preserves destination diagnostics; use a fresh session after a
rewind. A newly attached restored suffix can therefore contain a first wait
that an uninterrupted cold trace would suppress.

Rejected SPU source beats are DMA failures, with no CPU or causal instruction
attribution. The first-failure `dma` object uses the same decoded fields.
Failure-only probes do not retain preceding wait/input history; enable the
full trace stream and optional flight recorder when that context is needed.
Both prefix and failure-capture writers validate paired identities, enums and
target/input bounds through the shared C++ decoders before emitting the
affected record. These diagnostics do not establish silicon timing or boot.

### SIO2 DMA observations

SIO2 subsystem 17 event 20 (`DMABeat`) adds `sio2_dma` beside the raw words:

```json
{"endpoint":"iop_sio2_input","channel":11,"kind":"payload","outcome":"accepted","failure":"none","bus_status":"none","attempted_address":4096,"source_value":287454020,"remaining_before":36,"remaining_after":35,"staged_before":0,"staged_after":4}
```

Outcomes and source rejection statuses match SPU DMA. Counts describe the
bounded 36-word/144-byte transport, not serial execution or a peer reply.
An active disabled channel can emit a wait; identical waits are suppressed
within the observation session. Each granted word records its physical source
address/value and before/after remaining words and staged bytes. Completion is
the final accepted word, ordered before its DMA interrupt and CPU observations.
No per-byte or serial-completion events are generated.

Channel12 uses endpoint `iop_sio2_output` and records the physical destination
in `attempted_address`, the peeked result word in `source_value`, and remaining
SIO2 bytes in `result_before`/`result_after` instead of staged input counts.
A rejected destination reports `destination_write` without consuming results.
The result snapshot counts unread bytes: the probe profile has four logical
absent-card samples followed by140 explicit padding bytes; the `f3` profile
has five followed by139. After the first output word, `f3` retains one logical
byte while the probe has only padding. DMA completion is independent of the
earlier serial IRQ.
Wait suppression is independent for each channel.
CPU replies from the infrared, bounded `81 52 00` absent-card, and
`21 12 00 00 00 00` absent-multitap profiles do not enter these DMA result
counts. An armed output channel can remain
waiting while CPU receive bytes are available or drained.

The low three bytes of `values[0]` are channel/outcome/bus status.
`values[1]` packs address/value in its low/high 32 bits; `values[2]` packs
remaining-before/after in its low two bytes; `values[3]` packs staged-before/after
in its low two halfwords (remaining result bytes for channel12).
Use `nekoDecodeSIO2DMABeat` for the validated C++ view.
Rejections enter the existing typed DMA first-failure path without a causal
CPU instruction. Trace and failure-probe gating, session lifecycle and
destination-owned diagnostics follow the shared DMA rules above.

The inspector supports `--subsystem sio2` and `--cpu iop` for these peripheral
rows and preserves `sio2_dma` alongside `values_raw`. Older reports need not
contain the new event or snapshot fields.

### Trace limits and lifecycle

The collector retains the earliest events, never a rolling window. Truncation
means at least one event was omitted, not merely that the collection reached
its ceiling. An exact-fit run is not truncated; a zero-budget run has an empty,
non-truncated trace even with a zero event ceiling. A truncated prefix may end
within a cycle or between related events, and an absent event does not prove
that its operation never occurred. The master budget, guest exception handlers,
CPU stop precedence, snapshots, and host exit status remain unchanged.

The limit bounds retained event count at insertion, not just output size.
It does not preallocate the requested ceiling, shrink previously allocated
vector capacity, or impose a byte budget. After truncation is established,
EE state and presentation hashes are skipped only when no observer is attached:
those discarded events have no remaining consumer. Other observation and
failure-detection costs remain, including first-failure stopping beyond the
prefix. An attached observer, including `--failure-context`, retains the full
event stream and its hashing costs even after its recording window freezes.
Clearing or restarting collection restores hashing; exact-fit and truncation
semantics are unchanged. Output is formatted one record at a time after
execution rather than building another complete trace in memory.
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
with the usual raw event fields and decoded `cdvd`, `spu_dma`, or `spu_input`
objects for their respective events.
`observed_events` counts the whole observed
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
python3 -B tools/inspect_startup.py events run.jsonl --event command_accepted
python3 -B tools/inspect_startup.py events run.jsonl --source failure_capture --event command_completed
python3 -B tools/inspect_startup.py events run.jsonl --subsystem spu2 --event dma_beat
python3 -B tools/inspect_startup.py events run.jsonl --source failure_capture --subsystem spu2 --event state_changed
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
`--cpu` selects actual EE or IOP CPU events;
without it, all subsystems are included. `--subsystem` selects a numeric ID or
known subsystem name, including `spu2` and `sif`. `--cycle`, `--from-cycle`, and
`--to-cycle` are inclusive master-cycle constraints. `--event` accepts a
numeric ID, `instruction`, `memory`, `exception`, `fault`, or
`serial_transmit_completed`, `command_accepted`, `command_completed`,
`dma_beat`, or `state_changed`
(the other full event names also work).
Serial completion payloads expose the unconverted byte and physical endpoint.
CDVD command rows retain numeric `subsystem: 15`, add `subsystem_name: "cdvd"`,
and expose the exported nested `cdvd` object unchanged, with the original
`values_raw` alongside it. Summary counts use the `cdvd` subsystem label.
This works for both event sources; `--cpu ee` and `--cpu iop` still exclude
device events. Python validates the decoded object's fields, enum labels,
integer ranges and sixteen-byte result shape (or eight for historical reports),
but never decodes raw CDVD words
or derives BCD/calendar policy. Missing or invalid decoded command objects
are input errors; existing non-CDVD logs need no new fields.
SPU2 rows similarly expose `subsystem_name: "spu2"` and the exported
`spu_dma`/`spu_input` object alongside `values_raw`, without CPU attribution.
Python validates their exact decoded field shapes, paired identities, enums,
types and bounds; it never decodes the raw SPU words. Missing/malformed decoded
SPU objects are errors; existing non-SPU reports need no new fields.
`--address` remains a CPU memory-event virtual-address filter, not a SPU
physical source-address filter.
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

`--mode continuation` compares BIOS identity, absolute ceiling, complete
ending snapshot and cycle-limit outcome, excluding per-call progress counters
and cold/restored origin metadata. `--mode continuation-trace` additionally
requires the same absolute trace origin and compares retained trace events,
limits and failure-window records. These modes do not by themselves prove
canonical RAM/cache/device equality; checkpoint validation also compares
complete canonical save-state bytes.

All commands accept both `.jsonl` and gzip-compressed `.jsonl.gz` inputs.
`byte_equal` compares decompressed JSONL bytes, independent of gzip headers.
Corrupt/truncated gzip is an input error, not partial successful evidence.
Decompression does not change the in-memory loading bound described above.

Exit status is 0 for inspection/equality, 1 for a comparison mismatch, and 2
for an argument, file, or validation error. These statuses describe the
inspection operation, not the BIOS execution outcome. Missing/duplicate or
unknown record types, malformed JSON, inconsistent counts/boundaries, and
out-of-range raw words are errors, not empty successful reports. Additive
record fields are retained. A missing final record is an incomplete file;
the producer's explicit trace truncation is valid evidence and is reported
separately. An input error produces no successful JSON report.

Both successful and runtime-error snapshots require a `dmac` object with
exactly `control`, `hold_control`, `status`, `interleave_size`,
`interrupt_pending`, `priority_control`, `stall_address`, `sif_grant_order`,
and `channels`. `hold_control` must be an integer from 0 through 4294967295;
booleans, missing fields, and alternate spellings are rejected, not defaulted.
Older reports missing the field must be regenerated. Both comparison modes
validate both inputs before comparing and preserve all hold-control bits,
including reserved bits. This adds a snapshot field without changing the
existing record types or unrelated format versions.

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
defaults to 1,000,000 master cycles; traced runs retain at most 3,000,000 events.
Use `--cycles <count>` to select an explicit unsigned 64-bit cycle ceiling
for all four starts, including zero. The selected ceiling is recorded in
the manifest and each exact run command. The helper never increases it
automatically, changes the trace cap, or runs tests.
For conformance-only measurements of an explicitly retained native build,
`--executable "<path>"` skips configuration/build and uses that prebuilt
executable. The path must be an executable file; its identity is recorded in
`manifest.json` and checked before and after every native capture, and before
publishing checkpoint validation. Receipts bind to the launch identity, not a
later replacement. Mutation stops the campaign with partial evidence retained.
Checkpoint executable
identity validation, native progress, comparisons and lifecycle/finalization
remain unchanged. Scout mode rejects this option. Build flags/host context
for a prebuilt executable must be caller-attested, not inferred from its path.
Its optional `--failure-context` flag enables the bounded recorder only on
the two traced runs; both untraced runs remain genuinely untraced. The
manifest records this choice and automatic first-failure windows select the
exporter's indicated event source.

The artifact directory must not already exist, even if empty. Build and
inspection stdout/stderr, two `traced*.jsonl.gz` reports, two uncompressed
`plain*.jsonl` reports and separate run stderr are retained. Traced reports
stream directly into gzip with bounded copy buffers; no full uncompressed
temporary trace is written. `manifest.json` records exact commands, exit
statuses, stored output sizes and uncompressed trace byte counts,
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

### Campaign ownership and completion

Scouts retain bounded diagnostic replay on native runtime-error exit2. The
helper still exits2 and status remains `failed`; replay agreement is not
verification. `runtime-replay.jsonl.gz`, `runtime-plain.jsonl`,
`runtime-error-summary.json`, `runtime-error-events.json` (bounded GIF writes)
and `compare-runtime-replay-plain.json` preserve the available evidence.
The last successful same-build scout checkpoint is reused only for this
origin-only diagnostic. No incomplete-cycle checkpoint or canonical ending
is fabricated; normal fault and conformance comparisons remain unchanged.

Runtime reports additionally contain `gs_transfer`, copied from the GS owner
after unwinding. It identifies attempted parameters on a GS activation error;
on other errors it is retained GS state, not a claim of causality. Historical
reports without it remain supported. The inspector validates its field bounds.
Because a synchronous device exception may occur before the scheduler advances
its clock, diagnostic replay includes at most one tick beyond the reported
clock, capped by the original native ceiling. Error identity and full retained
runtime snapshots must agree; disagreement remains explicitly recorded and
failed. This does not change guest execution, trace events or save-state layout.

Retain campaign ownership through completion, monitor progress, reconcile the
actual process exit and finalize evidence before reporting verification.
A background launch is **running**, never done. Recovery starts from the
existing batch and ownership record, not another launch.

New batches retain `owner.lock` and atomic `execution.json`/`manifest.json`
receipts. The OS-held lock identifies a live owner without trusting a PID that
may have been reused. `execution.json` records the invocation, owner PID,
timestamps, expected helper return code and final manifest identity; input
and executable identities are recorded in the manifest. The receipt is written
before the helper returns, so the actual outer process exit must still be
observed separately. Abrupt termination releases the lock but may leave a
native child running; no automatic recovery or relaunch is attempted.

Inspect an existing batch without building or running the BIOS:

```sh
python3 -B tools/run_bios_discovery.py out/bios-discovery/after-ier --status
```

| State | Meaning |
| --- | --- |
| `running` | Ownership lock is held; active stages/start/progress are reported, not proof of throughput. |
| `execution_finished` | Expected helper exit0 and required terminal evidence agree; inspection pending. |
| `verified` | Explicit inspection receipt and its retained evidence still agree. |
| `failed` | Nonzero completion, inconsistent evidence or invalidated inspection. |
| `interrupted` | Recorded interrupt or ownership lost without a terminal receipt; inspect possible children. |
| `unknown` | Missing/legacy/malformed ownership records; never inferred success. |

Status exit0 means `verified`; exit1 means running/inspection pending; exit2
means failed/interrupted/unknown. Read the JSON, not just the code.
`active_stages` lists all pending commands while running; `active_stage` and
its timestamp/exit describe the latest pending command, or the latest completed
command when none is pending. A completed cold pass does not hide a still-owned
checkpoint capture.

New native captures also retain `<stage>.progress.jsonl`. While running,
`stage_progress` contains one entry per pending stage: parent-observed
`elapsed_wall_seconds`, the latest native `sample`, and its host-clock
`sample_age_seconds`. Samples report absolute `master_cycle`, the invocation's
`start_master_cycle` and `cycle_ceiling`, native elapsed wall/process-CPU seconds,
and cumulative `cycles_per_second`. Native timing starts after ROM loading and
checkpoint restoration; parent stage wall time includes that preparation and
any delay before reaping the child. CPU time is for that native process only,
not the driver, other stages, or the complete campaign.

Native phases are `execution`, `report`, `checkpoint`, `finished` and
`runtime_error`. A finished sample is not an observed process exit or verified
campaign. Each overlapping cold/checkpoint stage has its own sample and ceiling;
the existing top-level scout `progress` remains a completed-segment summary.
A null sample means the stage has not published one or is not a native capture.
Old manifests need no synthetic progress records.

Progress is sampled at coarse execution-slice boundaries (default one million
master cycles), plus phase transitions, without per-cycle clock polling.
Inspect sample age before treating the reported cycle as current: slow traced
slices, report export, checkpoint I/O or a stalled process can leave an old
sample. Status reads at most the last 64 KiB of each active sidecar, accepts
the last complete line while a new line is being written, and reports malformed
complete records as errors. Final progress and sidecar identities are retained
in command/inspection evidence; they do not replace any execution comparison.

For direct diagnostics, progress is opt-in and its destination must not exist:

```sh
./out/check/neko bios-startup path/to/bios.bin --cycles 10000000 --progress-file progress.jsonl --progress-every 1000000
```

`--progress-every` requires `--progress-file` and a positive unsigned 64-bit
cycle interval. The file's records use `neko-bios-progress-v1`; telemetry is
separate from stdout JSONL and stderr. Enabling it does not change run/trace
payloads, guest timing, canonical state, failure-stop policy or cycle ceilings.

## Reproducible execution timing

Analyze completed campaign receipts without executing guest code:

```sh
python3 -B tools/report_bios_timings.py <batch-directory> [<repeat-directory> ...] --context <launch-context.json>
```

This command reads only. It does not build, launch, resume, finalize, change or
certify a campaign. Exit 0 means timing analysis succeeded, not that the BIOS
booted or conformance was inspected. Exit 2 reports incomplete, changed,
malformed or unsuccessful execution/timing receipts. Missing historical
sidecars are an error; no timings are reconstructed from file timestamps.

Record context before the benchmark launch in a separate JSON file:

```json
{
  "host": "OS/version, architecture, CPU, RAM, power mode and competing load",
  "build": "compiler/version, configuration, exact flags, assertions, sampling mode",
  "revision": "Git commit and any source changes affecting the executable"
}
```

These nonempty strings are caller-attested, not independently verified.
Apply one context only to batches captured with that context. Do not label
historical captures with the current host or compiler. Record the exact build
and campaign commands/exits alongside it; retain compiler/options evidence.
Existing manifests supply the captured executable and BIOS SHA-256 identities.
Keep the assertion-enabled `-O2` configuration as the initial control, with
unchanged floating-point rules, input and hardware settings.

The JSON report retains every native stage and groups only matching executable,
BIOS, start/end/ceiling, observation options, progress interval, sampling duration
and checkpoint identity. Cold, restored, traced and failure-probe runs remain distinct.
Groups report repetitions, median execution wall time and min/max/spread;
each stage retains wall/process-CPU time, cycles/second and seconds/billion.
A zero-cycle run has no seconds/billion value. Small authored runs validate
reporting, not representative BIOS performance.

Timing boundaries are explicit:

- `execution_*_seconds`: initial execution sample to the `report` sample.
  Includes coarse progress writes, trace setup and startup snapshots, but
  excludes ROM loading/reset, checkpoint restoration and final report export.
  It is an execution window, not an isolated core-loop measurement.
- `report_wall_seconds`: `report` to `checkpoint`, or `finished` when no
  checkpoint is saved. Includes final JSON/trace export.
- `checkpoint_wall_seconds`: `checkpoint` to `finished`; null without a save.
- `external_overhead_wall_seconds`: parent stage wall time minus the native
  window. Combines preparation, process startup and reaping delay; these costs
  cannot be separated further from existing sidecars.

Native command timestamps and parent durations identify potentially overlapping
stage windows. `stage_window_overlap: true` or null (missing timestamps) is not
a sequential baseline; overlapping windows do not prove continuous CPU overlap.
In particular, `--checkpoint-at` overlaps the two cold captures, so neither
is a sequential cold-throughput control. Its later restored stages are
sequential but cover only their continuation, not the whole cold workload.

For a performance baseline, agree finite workload bounds first, then use
sequential unprofiled native stages from maintained conformance batches
(`plain1`/`plain2` in the ordinary matrix). Repeat under identical recorded
conditions, retaining all results rather than selecting the fastest run.
Use the existing inspection/finalization gates separately for correctness.
Sample in a separate run and context; never include profiler overhead in an
unprofiled speed claim. BIOS benchmarks need their agreed performance-only
scope and do not resume paused discovery. Do not carry broad discovery ceilings
into traced matrices merely to collect timings.

### Build-only IPO experiments

Use CMake's standard interprocedural-optimization setting in separate
assertion-enabled configurations; do not combine a build experiment with
source changes, fast-math, PGO or forced-inlining annotations:

```sh
cmake -S . -B out/ipo-off -DCMAKE_BUILD_TYPE=Debug -DNEKO_OPTIMIZE_CHECKS=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
cmake -S . -B out/ipo-on -DCMAKE_BUILD_TYPE=Debug -DNEKO_OPTIMIZE_CHECKS=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build out/ipo-off --target neko neko_tests --config Debug -j 4
cmake --build out/ipo-on --target neko neko_tests --config Debug -j 4
NEKO_REFERENCE_BINARY="$PWD/out/ipo-off/neko" cmake --build out/ipo-on --target check --config Debug -j 4
```

Inspect the generated compile/link options and retain executable identities.
The compiler selects the IPO implementation; Apple Clang 21 uses ThinLTO.
Do not assume another compiler's options or a speedup. An IPO-unavailable
configuration/build is a failed experiment, not an optimized baseline.
For the complete sanitizer gate, configure another directory with the same
options plus `-DNEKO_ENABLE_SANITIZERS=ON`, then run its `check` target.
These isolated targets retain all native shards, Python/native fixtures and
repository checks without changing the usual `out/check` or `out/sanitize`.

Use `--executable` in separately authorized conformance batches to measure
the two prebuilt configurations. Keep all unsampled observations, matched
inputs/bounds, required trace and full canonical comparisons; checkpoints
still require their own build's validated provenance. Build-only equivalence
and authored control gains do not establish retail gains or justify changing
the default build.

### Authored benchmark controls

`tools/create_benchmark_rom.py` creates original synthetic 4 MiB ROMs, not copies
of retail firmware. It refuses an existing output. Both CPUs execute guest
instructions from their ordinary reset vectors; setup is never host-seeded.

```sh
python3 -B tools/create_benchmark_rom.py cpu --output out/performance-controls/inputs/cpu.bin
python3 -B tools/create_benchmark_rom.py ram --output out/performance-controls/inputs/ram.bin
python3 -B tools/create_benchmark_rom.py devices --output out/performance-controls/inputs/devices.bin
```

`cpu` sustains scalar arithmetic and branches with no guest-armed DMA/timers.
`ram` adds uncached loads/stores across sixteen words in each CPU's separate
RAM. `devices` dispatches on the implemented CPU PRId profiles: EE guest code
initializes scratchpad with normal toSPR DMA, repeatedly copies four qwords
back through fromSPR, and enables Timer0 compare/zero-return IRQs. IOP guest
code runs the scalar loop with its Timer0 target/repeat IRQ enabled. IRQ
latches are exercised without enabling CPU interrupt admission.
Other devices retain their normal reset/timing behavior; "CPU control" does
not mean the entire machine stops advancing.

Omitting `--iterations` selects sustained execution bounded only by the host
cycle ceiling. A finite `--iterations <1-65535>` variant publishes four RAM
words at physical `0x2000` on each CPU, then parks in a branch loop: `0x4e454b4f`,
iteration count, three times that count, and the final XOR. The RAM variant's
XOR uses the previously loaded word; after 32 iterations it is `16 ^ 32`.
Device copies end with words `0x12340000` through `0x1234000f` at EE `0x4000`.
These hand-defined results and traced/plain/restored comparisons are covered
by BIOS-free native tests. Finite variants are correctness fixtures, not
sustained-performance workloads.

#### COP1 reference conformance

The separate `cop1` kind requires finite iterations; it is not a sustained
performance control. EE guest code alternates integer work with
`LWC1` -> `ADD.S` -> `MFC1` -> a dependent integer store, then drains before
the next iteration. IOP executes only the scalar control path. Both CPUs
publish the completion marker and counters at `0x2000`; EE's fourth word
is raw `3.0` (`0x40400000`), while IOP retains the scalar XOR result.
EE `0x4000`/`0x4004` contain raw `1.0` and the stored raw `3.0`.

```sh
python3 -B tools/create_benchmark_rom.py cop1 --iterations 3 --output out/cop1-conformance.bin
```

The native fixtures in `tools/test_create_benchmark_rom.py` use three iterations
and a 512-master-cycle ceiling. They require complete traces, completed-load
interlocks and all three load producers, select an active load boundary from
the reference trace, and compare cold/plain/traced and restored continuations
using the maintained inspector and full canonical save-state bytes. Corrupted
trace payloads and canonical bytes must fail comparison even when the ending
summary alone would agree.

Set `NEKO_TEST_BINARY` to the current headless executable. For cross-build
comparison, explicitly supply a retained reference executable as
`NEKO_REFERENCE_BINARY`; the fixture creates each build's own checkpoint at
the same selected boundary, never restores one build's checkpoint in the other.

```sh
NEKO_TEST_BINARY="$PWD/out/check/neko" \
NEKO_REFERENCE_BINARY="$PWD/out/performance-cop1-reference-20261121/neko" \
python3 -B -m unittest discover -s tools -p 'test_create_benchmark_rom.py'
```

Freeze and record the reference executable identity before changing/rebuilding
execution. No supplied reference means the cross-build test explicitly skips;
traced/plain/restored self-comparison still runs. Identical executable identities
validate the harness only, not reference/optimized acceptance. These BIOS-free
fixtures do not build executables, launch retail campaigns, sample performance
or establish BIOS correctness or a speedup.

Use the existing campaign driver and a new directory for each control:

```sh
python3 -B tools/run_bios_discovery.py out/performance-controls/cpu-timing --mode conformance --bios out/performance-controls/inputs/cpu.bin --cycles 5000000
```

The ordinary matrix keeps two sequential unprofiled repetitions, repeated
traced captures, snapshot comparisons and full canonical endings. Traces keep
their configured prefix/explicit truncation semantics; do not claim full
history beyond a truncated prefix. Controls isolate implemented costs but
are not representative retail BIOS phases, graphics/audio benchmarks or proof
of the retail BIOS performance gate.

On macOS, sample a separate fresh conformance batch:

```sh
python3 -B tools/run_bios_discovery.py out/performance-controls/cpu-profile --mode conformance --bios out/performance-controls/inputs/cpu.bin --cycles 5000000 --sample-seconds 1
```

`--sample-seconds <1-60>` attaches `/usr/bin/sample` to the driver's owned
`plain1` native PID, using a 1 ms interval and `-mayDie`; the default 0 disables
sampling. It is unavailable for scout or overlapping checkpoint-creation
matrices. Each native receipt retains its `process_pid`; the profiled receipt
also retains duration, report filename and SHA-256. Sampler command/exit,
stdout and stderr remain separate receipts. Missing/failed sampling is an
explicit campaign error, not a successful profile. Finalization protects the
retained report identity along with existing evidence.

Sampling starts near native process launch and can include ROM loading and
initialization, or a shortened capture if the process exits. Inspect the actual
sample report; requested duration is not guaranteed execution-only coverage.
Distinguish core-loop stacks from setup/report stacks. Increase a workload's
finite ceiling only under its authorized measurement scope if it is too short
to sample reliably. Mark the sampling context explicitly and keep its timing
out of the unprofiled baseline.

Required compact JSON must parse, recorded output sizes must agree, required comparisons
must agree and retained canonical/checkpoint bytes are hashed,
so status inspection can read tens of MiB. It does not reread large JSONL
traces. Status/finalize reject run options and cannot trigger execution.

After the actual outer exit0 has been observed, inspect originating evidence,
complete the required review, update project/roadmap state and inspect/clean
obsolete output. Retain substantive `verification-notes.txt`, `review.txt`
and `cleanup.receipt` in the batch, then explicitly attest completion:

```sh
python3 -B tools/run_bios_discovery.py out/bios-discovery/after-ier --finalize --outer-exit 0
```

Finalization refuses a live owner, unsuccessful/incomplete evidence, a nonzero
outer exit or missing/empty inspection files. `inspection.json` binds those
files and compact/canonical evidence by hash; repeating unchanged finalization
is idempotent. Nonempty files and the outer-exit argument are attestations,
not independent proof of their contents. Lost outer results remain inspection
blocked; never supply a guessed zero. Sessions/watchdogs do not provide
cross-session orchestration. Legacy batches are not rewritten or retroactively
certified. Keep current gate evidence; later pruning/modification invalidates
current verified status, even when historical comparison receipts remain.

The reported `scope` is `conformance`, `scout_origin` or `scout_ceiling`.
`verified` only certifies this recorded workflow/scope, not real-hardware
conformance or successful boot. Authorization, bounds, checkpoint provenance
and commit checkpoints are unchanged.

### Staged discovery: scout, replay, verify

Use the staged mode to find the next dependency without repeating long full
traces. For an explicitly authorized BIOS input and absolute ceiling:

```sh
python3 -B tools/run_bios_discovery.py out/bios-discovery/scout-example --mode scout --cycles 50000000 --checkpoint-every 5000000 --replay-window 10000
```

This example is not an authorization to run that budget. Defaults are the same
BIOS and one-million-cycle ceiling as conformance mode, a five-million-cycle
checkpoint interval and a ten-thousand-cycle trace window.

The helper starts cold and invokes `bios-startup --scout` in checkpoint-sized
segments. Scout mode uses the existing failed-memory, rejected-instruction and
rejected-SIF-DMA classification, not a blanket exception stop. It reuses CPU
observation records but omits full component tracing, rolling prehistory and
per-cycle EE state hashes. The compact evidence retains the last issued
instruction and failure-cycle context; inspector coverage is `failure_probe`,
not a complete instruction trace. Ordinary syscalls and interrupts do not stop
the scout. Stop decisions are made between complete master cycles, preserving
same-cycle work and normal guest state. Runtime errors still produce an
incomplete-cycle report and abort the helper without publishing a checkpoint.

Each successful segment saves a canonical snapshot and identity receipt.
After a successful nonterminal segment, only the two newest checkpoint states
and explicitly pinned states are retained. The next segment can temporarily
add one state. Terminal failures, mismatches and interruptions preserve the
remaining diagnostic states; previously pruned states cannot be recovered.
Compact checkpoint receipts, sidecars and command logs remain available.

Use repeatable `--pin-checkpoint-at <cycle>` in scout mode to retain selected
same-batch investigation checkpoints. Cycles must be positive multiples of
`--checkpoint-every`, or the final absolute ceiling, and cannot exceed that
ceiling. A pin is created only if execution reaches its requested cycle;
the manifest retains the full requested pin list even after an earlier fault.
For example, add `--pin-checkpoint-at 10000000` to the scout command above.
Pins do not authorize a different input, extend execution or validate a scout
checkpoint for conformance reuse.

Cleanup verifies each obsolete state's identity and campaign-local path before
deleting it. The manifest records `prune_pending` before unlink and confirms
`pruned` plus cumulative reclaimed bytes afterward. An interruption between
those operations leaves an explicit uncertain deletion, not a recovery or
success receipt. Do not resume or infer a missing state's identity from it.

Progress on helper stderr reports the absolute cycle, ceiling, elapsed time
and measured segment throughput. All modes also report stage start/completion
on stderr; command receipts contain UTC start time and elapsed seconds.
The helper's final machine-readable result remains on stdout.

At the first failure, replay restores the immediately preceding checkpoint
(or starts cold if none precedes the fault). It runs to the scout's failure
cycle, enabling full tracing only within the selected trailing window, with
the existing prefix cap and bounded failure recorder. `--stop-on-first-failure`
ends traced replay after the first observed failure cycle; when tracing is
delayed, this stop policy begins at the trace origin. A plain continuation
then runs to the same absolute cycle. The helper requires matching scout/replay
failure details, matching traced/plain snapshots, and byte-identical complete
canonical states for scout, replay and plain execution. It never advances past
the selected ceiling to investigate a failure.

`scout-summary.json` retains compact detection; `summary.json` and the standard
first-failure windows describe replay. The manifest retains comparisons,
checkpoint identities, output sizes, timings and final-state hashes.
Success prunes only obsolete snapshots generated by this batch and duplicate
ending states, recording reclaimed bytes. The preceding replay checkpoint,
scout ending state, raw reports and compact receipts remain. Mismatch/error
paths preserve evidence instead of pruning.

Scout checkpoint receipts are deliberately `validated: false`, with
`validation: scout_origin_only`: local replay equivalence is **not**
cold/restore conformance. The helper can consume them only within that batch
and checks snapshot, executable and BIOS identities before each restore.
They are not accepted by the existing `--checkpoint` reuse path. After a
production fix, start a new cold scout; format compatibility does not establish
behavioral compatibility. Scout mode rejects externally supplied checkpoints
and manages its own failure capture.

The helper exits 0 for successful local verification or a reached scout
ceiling, 1 for disagreement or a CPU stop without classified failure, 2 for
errors and 130 for interruption. Neither exit 0 nor `ceiling_reached` proves
boot success. Keep the existing `--mode conformance` (the unchanged default)
and validated checkpoint matrix for broader cold/restore and trace-mode gates.

BIOS-free integration against an already-built headless executable:

```sh
NEKO_TEST_BINARY="$PWD/out/check/neko" python3 -B -m unittest discover -s tools -p 'test_bios_scout_integration.py'
```

This uses only an ephemeral, independently authored synthetic ROM, never the
local integration BIOS, and does not build the executable.
The complete `cmake -P cmake/Check.cmake` and `cmake -P cmake/Sanitize.cmake`
workflows run all Python tests, including these integration cases, against
their own built executable. CTest schedules the Python suite alongside the
C++ shards under the same concurrency limit. Plain Python discovery without
`NEKO_TEST_BINARY` skips the native cases.

### Throughput acceptance and capture selection

The BIOS-free integration suite checks these features together, not just as
isolated options: recent/pinned scout retention with completed progress
sidecars, replay failure attribution after prefix saturation, cold/restored
conformance with both full and one-event prefixes, and byte-identical complete
canonical endings across plain, full, saturated, deferred, restored and scout
execution with or without progress.

Choose the maintained capture by its evidence requirement:

Use risk-based BIOS gates rather than full cold/restored conformance after
every small hardware change. Routine low-risk changes retain focused/authored
tests, a fresh post-commit cold scout and bounded traced/plain replay evidence.
Require full conformance for persistence/save-state/schema, timing/ordering,
tracing/determinism or cross-cutting changes, at milestone boundaries, and for
suspicious or unexplained scout results or disagreements. Record the selected
scope and rationale before launch; this policy does not change helper defaults,
authorization, comparison strength or finalization requirements.

Confirm direct retirement of the former fault as well as the next origin.
The automatic scout replay may retain only the latter when the failures are
far apart. Inspect actual coverage; if necessary select the smallest supported
bounded capture or a full gate. Do not infer direct evidence from cycle advance
or promote scout checkpoints/receipts to cold/restore conformance.

| Workflow | Hash demand and bounds |
| --- | --- |
| Cold scout (`--mode scout`) | Failure-only observation omits full component hashes. Retain the two recent states plus necessary launch-time pins; inspect pruning receipts. |
| Automatic scout replay | The helper restores the nearest preceding same-build checkpoint and traces only the trailing window through the originating failure. Its failure recorder still requires full hashes; prefix saturation does not make a long replay cheap. |
| Cold/restored verification (`--mode conformance --checkpoint-at ...`) | Select a fresh checkpoint before the capability and a justified ending ceiling. Full trace/recorder and complete canonical comparisons remain required; use tightly bounded windows rather than a broad discovery ceiling. |
| Direct bounded prefix without a recorder | Once truncation is established, unconsumed EE/presentation hashes are skipped. This is not a replacement for the campaign failure recorder or canonical verification. |

Coarse progress reports sampled native wall/process-CPU time and throughput,
not health or a completion verdict. Retention bounds surviving checkpoint
storage, not serialization cost or checkpoint conformance. Performance
comparisons must retain exact commands, input/executable identities, timing
scope and matching reports/states; a local prefix-only speedup is not an
end-to-end campaign estimate. Do not remove recorder events, weaken canonical
comparison or introduce a reduced trace profile to obtain faster measurements.

At block closure, run the complete optimized and AddressSanitizer gates:

```sh
cmake -P cmake/Check.cmake
cmake -P cmake/Sanitize.cmake
```

These are BIOS-free gates. A subsequent real-BIOS campaign still needs a fresh
batch, unchanged authorized input, explicit finite bounds, owned execution
through terminal inspection, and the status/finalization procedure above.

### Validated checkpoint workflow

Select the checkpoint cycle and absolute ending ceiling explicitly:

```sh
python3 -B tools/run_bios_discovery.py out/bios-discovery/checkpoint-1m-2m --checkpoint-at 1000000 --cycles 2000000
```

This mode performs exactly four BIOS runs: one untraced cold start creating
`checkpoint.state`, one cold traced continuation with tracing delayed to the
checkpoint cycle, one restored traced continuation, and one genuinely
untraced restored continuation. The two independent cold starts overlap, using
at most two native processes; both are reaped before restored execution starts.
One driver owns the processes and writes the manifest serially. Exceptions
or interruption kill and reap outstanding children before releasing ownership.
Trace compression still streams through bounded buffers. Per-command elapsed
times overlap and include time until reaping; do not sum them as batch wall time.
The last three stop at the same absolute
ceiling or earlier CPU host stop. Both traced runs always use the bounded
failure recorder and the existing 3,000,000-event cap.

It compares the cold/restored ending snapshots and retained trace suffix,
restored traced/untraced snapshots, and all three complete canonical ending
states byte-for-byte. Successful validation retains `cold-final.state`;
only the two newly generated byte-identical duplicate ending states are
pruned, with hashes/sizes and removal recorded in the manifest. Mismatches
retain all three states and produce no reusable checkpoint receipt.
Checkpoint creation must reach its selected cycle with exit 0; an earlier
stop is not published as usable. Runtime/I/O errors preserve partial evidence.

`checkpoint.state.json` records successful validation, absolute cycle and
SHA-256/size identities for the BIOS, checkpoint bytes and executable.
Reuse that checkpoint in the ordinary two-traced/two-untraced matrix:

```sh
python3 -B tools/run_bios_discovery.py out/bios-discovery/from-checkpoint --checkpoint out/bios-discovery/checkpoint-1m-2m/checkpoint.state --cycles 2000000 --failure-context
```

Reuse requires an unchanged BIOS, checkpoint and executable. Any executable
change conservatively invalidates the checkpoint, even if a developer believes
it affects only later execution: regenerate and revalidate with
`--checkpoint-at`. This avoids treating codec compatibility as proof that an
old state reflects current early-startup behavior. Low-level CLI restore does
not enforce this helper provenance policy.

The ordinary matrix, with or without checkpoint reuse, saves all four complete
canonical ending states and compares them byte-for-byte in addition to its
trace and snapshot comparisons. Agreement retains `traced1-final.state` for
offline hardware inspection or `save-state frame` export; only the three
newly generated duplicate endings are pruned. The manifest records each ending
identity, comparison result, duplicate removal and total pruned bytes.
Any disagreement retains all endings and fails the campaign. Runtime/I/O
errors preserve partial evidence. Status/finalization verifies the retained
canonical bytes; older ordinary matrices without canonical receipts cannot
satisfy this gate and are not retroactively certified.

Cold starts remain the authoritative regression baseline; restored runs are
an iteration accelerator, not proof that reset/early startup still works.
Each invocation still needs its own approved input/budget. Compression is not
a disk quota: keep the event cap and inspect/prune genuinely obsolete evidence
at handoffs. Existing batches are not automatically deleted or rewritten.

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
