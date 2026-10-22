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
  counts, shared free-word count, and SIF0 packing cursor.
- EE DMAC control/status/interleave size and interrupt output, plus implemented
  channels 1 (VIF1), 2 (GIF), 8 (fromSPR), and 9 (toSPR).
- EE/IOP INTC latches, masks, request outputs, and IOP control enable.
  DMAC and EE INTC request outputs remain separate; these are not claims that
  the CPU has admitted an interrupt.
- Both IOP DMA banks' priority/interrupt words and aggregate request, plus
  channel 10's control, MADR, BCR, packet remainder, terminal bit, enable, and
  side-effect-free next-beat inspection.

Run progress is **per call**: EE instruction counts are accepted instructions,
IOP instruction counts are retired instructions. Snapshot cycle/retirement
counts are **absolute**, not per-call deltas.

`clockEligible` reports a DMA owner's `clockActive()` condition, not guaranteed
progress or sink readiness. An asserted channel START with DMA disabled is
controller-gated. `lastTransferStalled` is the retained last-attempt flag for
VIF1/GIF; it may remain set after the controller is disabled or the sink changes.
Scratchpad channels do not expose a last-stall flag. SIF counts show
empty/full/partially occupied storage, not a decoded guest wait reason.
For IOP SIF1, zero `remainingWords` means awaiting a four-word header while TR
is set; a nonzero remainder means payload. `nextBeat` is the result of a
hypothetical grant now, not the last attempted beat or a promise of an IOP
clock pulse. `wait` with TR clear or enable clear is channel/controller gating;
otherwise it means insufficient header/payload words. `accepted` is one
nonterminal grant, `completed` is a terminal grant, and `rejected` identifies
an unaligned header or invalid RAM destination. Address, value, and bus status
describe that available header word or 32-bit physical payload write; they are
zero/default when waiting. Header acceptance does not prevalidate payload
memory, and inspection never performs a write or consumes queued words.

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

The DMA array order is 1, 2, 8, 9. Channel 8 has no TADR; only channels 8/9
have SADR. These absent JSON registers are `null` (the corresponding C++
snapshot fields are zero and not applicable). No unimplemented channel is
reported as an idle engine.

Writers preserve the destination stream's formatting and locale, and use
normal ostream error reporting. Unknown named snapshot enum values throw
`std::invalid_argument` before any record is emitted. Output formatting
allocates only when explicitly requested; inspection uses fixed-size values.
Nothing is automatically printed or attached to core runner results, and
the existing EE-only `--bios` frontend is unchanged.

## Headless discovery command

`neko bios-startup <file> --cycles <count> [--trace-events <count>]` wraps the
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

The snapshot's DMA inventory remains the subset described above; it does
not enumerate EE SIF0/SIF1 or IOP SIF0 engines. Use the owners' inspection APIs
and existing ordered traces when those details are needed. The command does
not infer an exact guest request/response checkpoint from ending register
values.

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
