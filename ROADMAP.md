# Neko Roadmap

This is a living plan for evolving Neko from its current Vector Unit foundation
into a deterministic PlayStation 2 emulation core. Tasks are ordered by
dependency rather than estimated completion date.

The reference core remains cycle-oriented, deterministic, manual-backed, and
independent of frontends. Optimization follows correctness and profiling.

## Current Foundation

- [x] Cross-platform CMake build structure validated on macOS
- [x] Raw 32-bit VU floating-point register storage
- [x] Correct constant behavior for `VF00` and `VI00`
- [x] Synchronous microprogram execution
- [x] Upper FMAC instruction decoding and execution
- [x] Lane-aware RAW hazard detection and masked writeback
- [x] Basic E/D/T state transitions
- [x] Explicit rejection of unsupported instructions
- [x] Separate VU0 and VU1 configurations
- [x] Correct 4 KiB and 16 KiB code/data memory capacities
- [x] Checked microprogram upload, fetch, and host data-memory access

## Milestone 1: Run a Small Real VU0 Program

The first integration target is
[`test_vu0.asm`](https://github.com/mikeakohn/java_grinder/blob/master/samples/playstation2/test_vu0.asm).
It fills a requested number of data-memory qwords with a supplied 32-bit value.

### Execution Control

- [x] Refactor the run loop around one-cycle `tick()`
- [x] Add instruction-pair stepping
- [x] Add bounded execution to detect infinite programs
- [x] Expose the current PC and configurable start address
- [x] Add optional trace events for issue, stalls, and writeback
- [x] Keep debugging interfaces read-only unless mutation is explicit

### Termination Control

- [x] Implement the one-instruction E-bit delay slot
- [x] Record the termination PC
- [x] Verify D/T timing and enable behavior
- [x] Implement Force Break and verify its state and pipeline behavior

### Manual Timing Conformance Pass

- [x] Verify independent FMAC instructions issue every cycle without stalls
- [x] Verify dependent instructions wait through S-stage writeback
- [x] Verify hazards are independent across x/y/z/w fields
- [x] Verify `VF00` never generates a data hazard
- [x] Verify issue and writeback trace ordering for overlapping pipelines
- [x] Verify flags become visible at the documented writeback stage
- [x] Verify E delay-slot behavior with multiple active pipelines

### Lower Execution Unit

- [x] Add lower-instruction decoding and dispatch
- [x] Model IALU and LSU separately and reserve branch dispatch
- [x] Enforce upper/lower dual-issue restrictions
- [x] Add integer-register hazard timing

### Lower Timing Conformance Pass

- [x] Verify LSU S-stage writeback and exact integer stall cycles
- [x] Verify Force Break cancels LSU writes and hazards
- [x] Verify VU0/VU1 qword address wrapping
- [x] Verify `VI00` remains constant and hazard-free
- [x] Verify lane-specific `LQ` and `SQI` hazards
- [x] Verify overlapping LSU writeback ordering
- [x] Verify invalid `ILW` masks are rejected
- [x] Verify `I`-bit data waits for paired upper issue

### Correctness Hardening

Complete the branch-sensitive timing and hazard work before adding `IBNE`.

- [x] Make lower-instruction hazard handling exhaustive
- [x] Make integer hazard state reusable by branch control
- [x] Separate IALU bypass availability from S-stage writeback

### Branch Control

- [x] Implement signed relative branch targets
- [x] Implement register-based branch targets
- [x] Implement the one-instruction branch delay slot
- [x] Reuse deferred-control timing where E termination and branches overlap
- [x] Verify branch timing alongside integer-register hazards

### Instructions Required by `test_vu0`

- [x] `IADD`
- [x] `ISUBIU`
- [x] `MFIR`
- [x] `ILW`
- [x] `LQ`
- [x] `SQI`
- [x] `IBNE`

### Integration Test

- [x] Assemble the source into a raw binary with `naken_asm`
- [x] Verify instruction byte order and pair layout
- [x] Load input parameters through the checked data-memory API
- [x] Execute from address zero with a cycle budget
- [x] Compare the final VU memory with hand-defined expected qwords

### Integration Harness and Diagnostics

- [x] Add a reusable program runner with a cycle budget and final checks for
      execution state, PC, termination position, cycle count, and data memory
- [x] Capture the existing issue, stall, writeback, and Force Break trace events
- [x] Add optional newline-delimited JSON trace output in the runner/frontend,
      keeping serialization, files, and streams outside the VPU core
- [x] Allow traces to be restricted to a cycle range for focused deterministic
      reruns
- [x] Keep tracing disabled during normal passing runs and make diagnostic
      output available on demand or after a failure

### Original Integration Suite

- [x] `integer_fill.asm`: exercise `ILW`, `IADD`, `ISUBIU`, `MFIR`, `SQI`,
      backward `IBNE`, and a meaningful branch delay slot while generating
      distinct qword values
- [x] `lane_masks.asm`: verify `.x`, `.y`, `.z`, `.w`, and mixed destination
      masks using distinct sentinel values in every lane
- [x] `branch_paths.asm`: cover taken and untaken forward and backward branches,
      delay-slot effects, and path-signature output
- [x] `indirect_calls.asm`: cover `JR`, `JALR`, delayed control transfer, link
      visibility, and call/return signatures
- [x] `vector_math.asm`: exercise upper FMAC operations, dependencies,
      accumulator behavior, and exact expected vectors
- [x] `dual_issue.asm`: sustain simultaneous upper/lower execution and verify
      both output and exact cycle count
- [x] `termination.asm`: cover E-bit delay behavior, branch/E overlap, final
      memory markers, and the stopping PC
- [x] Add a capstone vector kernel that loads input vectors, applies a scale and
      bias, clamps the results, and stores them while overlapping memory,
      integer, branch, and upper-pipeline work

## Milestone 2: VU Execution Accuracy

### Pipeline Model

- [x] Replace the monolithic run loop with a system-schedulable clock interface
- [x] Give FMAC stages explicit timing behavior
- [x] Add IALU, LSU, branch, FDIV, EFU, and XGKICK pipelines
- [x] Reject pipeline types without defined stage timing
- [x] Carry operation-specific state on pipelines instead of pending decode state
- [x] Model register, flag, and special-register availability timing
- [x] Validate stalls and forwarding behavior from manual examples
- [x] Add FDIV/EFU structural hazards and Q/P synchronization

### Pipeline Integration Programs

- [x] Add compact assembly programs with exact results and cycle counts
- [x] Assert final registers, output memory, and issued instruction addresses
- [x] Cover chained FMAC and ACC operations with overlapping independent work
- [x] Distinguish forwarded ACC values from architectural S-stage visibility
- [x] Cover IALU bypass into LSU addresses and branch decisions
- [x] Cover LOI timing across paired and subsequent upper instructions
- [x] Cover mixed-lane VF hazards without stalling independent lanes
- [x] Cover branch and termination drain with FMAC, IALU, LSU, and LOI active

### VU Floating Point

Raw arithmetic and fixed-point conversions are bit-oriented.

- [x] Build raw-bit operation APIs returning result bits and exception flags
- [x] Treat exponent-zero inputs as signed zero during calculations
- [x] Treat exponent-255 encodings as finite VU values
- [x] Correct exponent overflow and underflow detection
- [x] Implement VU 24-bit truncating add, subtract, and multiply
- [x] Implement raw VU division results and exception behavior, including
      signed `MAX` for `0 / 0` with the I flag
- [x] Implement raw VU square-root and reciprocal-square-root results and
      exception behavior
- [x] Implement truncating fixed-point conversions with defined out-of-range behavior
- [x] Validate edge cases against the VU manual and independent reference
      vectors; incorporate hardware-derived vectors when available

### Floating-Point Diagnostics

- [x] Add optional structured FMAC trace data through the existing callback,
      including per-lane raw multiply results, accumulator inputs, final
      results, exception flags, and ignored-result fields
- [x] Add a reusable raw VU value decomposition helper exposing sign, exponent,
      mantissa, and VU classification without formatting inside the core
- [x] Add trace contract tests for distinct multiplication and accumulator
      exceptions in MADD, MSUB, and OPMSUB
- [x] Include arithmetic details in the runner's on-demand NDJSON trace output
      while keeping tracing disabled during normal execution

### Floating-Point Integration Programs

- [x] Add chained arithmetic programs with exact raw results and flag states
- [x] Cover VU zero, denormal, maximum, overflow, and underflow behavior
- [x] Cover multiply-add/subtract sequences where intermediate precision matters
- [x] Cover fixed-point conversion boundaries and out-of-range inputs
- [x] Cover DIV, SQRT, RSQRT, WAITQ, raw Q results, and I/D flag state

### Instruction Coverage

- [x] Complete commonly used lower integer instructions (`IADD`, `IADDI`,
      `IADDIU`, `IAND`, `IOR`, `ISUB`, and `ISUBIU`)
- [x] Complete VU memory load/store variants and lane masks
- [x] Complete branches and jumps
- [x] Add Q pipeline division and square-root operations
- [x] Add VU1 `XGKICK` initiation, PATH1 busy timing, and a non-owning GIF
      handoff boundary

### Official Microinstruction Audit

- [x] Reconcile all 59 upper and 69 lower microinstructions in the VU User's
      Manual with implemented or dependency-aligned roadmap families
- [x] Keep EE COP2 macro-mode encodings in the system-level milestone rather
      than conflating them with VU microprogram decoding
- [x] After the milestone-driven instruction families are complete, perform
      the final implementation audit: verify decoding, execution, timing,
      hazards, and integration coverage for every architecturally meaningful
      VU0/VU1 microinstruction and deterministic rejection of every reserved
      encoding
### Lower Data Movement and Special State

- [x] Implement `MFIR`
- [x] Complete basic register movement with `MTIR`, `MOVE`, and `MR32`
- [x] Complete clipping-flag access with `FCAND`, `FCEQ`, `FCGET`, `FCOR`,
      and `FCSET`
- [x] Complete MAC-flag access with `FMAND`, `FMEQ`, and `FMOR`
- [x] Complete status-flag access with `FSAND`, `FSEQ`, `FSOR`, and `FSSET`
- [x] Complete R-register operations with `RGET`, `RINIT`, `RNEXT`, and
      `RXOR`

### VU1 P Pipeline and Elementary Function Unit

- [x] Audit all EFU encodings, source-field rules, VU1-only restrictions,
      throughput/latency values, and exceptional-result behavior against the
      official instruction reference
- [x] Add the architectural P register and complete `MFP`, including four-cycle
      masked VF writeback, VF00 behavior, source visibility, and simultaneous
      upper-write priority
- [x] Add the shared EFU pipeline and `WAITP`, including instruction-specific
      execution lengths, throughput stalls, delayed P writeback, pipeline
      drain, Force Break, and deterministic VU0 rejection
- [x] Implement the reduction operations `ESUM` and `ESADD`
- [x] Implement the scalar root operations `ESQRT` and `ERSQRT`
- [x] Implement the length and reciprocal operations `ELENG`, `ERCPR`,
      `ERSADD`, and `ERLENG`
- [x] Implement the polynomial transcendental operations `ESIN` and `EEXP`
      using the documented VU approximation algorithms rather than
      host-library substitutes
- [x] Implement the arctangent operations `EATAN`, `EATANxy`, and `EATANxz`
      using the documented VU approximation algorithm

### VU1 Elementary Function Unit Conformance

- [x] Add fixed bit-level reference vectors for `ESUM`, `ESADD`, `ESQRT`, and
      `ERSQRT` across representative valid-domain inputs
- [x] Add fixed bit-level reference vectors for `ELENG`, `ERCPR`, `ERSADD`,
      and `ERLENG` across representative valid-domain inputs
- [x] Add fixed bit-level reference vectors for `ESIN`, `EEXP`, `EATAN`,
      `EATANxy`, and `EATANxz` across representative valid-domain inputs
- [x] Cover valid-domain boundaries, signed zero, denormals, saturation,
      divide-by-zero, and exceptional inputs
- [x] Verify scalar lane selection, vector source masks, and source hazards for
      every EFU instruction form
- [x] Verify exact P write cycles, `WAITP` release timing, and shared-unit
      throughput overlap for every instruction latency class
- [x] Cover repeated and mixed EFU operation sequences, including normal
      pipeline drain and Force Break cancellation

### Instruction Integration Programs

- [x] Cover the common IALU family through decoded integer, memory, and
      `MFIR` instruction streams
- [x] Cover VU load/store addressing variants and masked lane behavior through
      a decoded memory integration program
- [x] Cover mixed integer, memory, branch, and `MFIR` instruction streams
- [x] Cover `MTIR`, `MOVE`, and `MR32` dependencies and lane behavior
- [x] Cover clipping flag tests, reads, and setters
- [x] Cover MAC flag tests
- [x] Cover status flag tests and setters
- [x] Cover deterministic R-register initialization, advancement, and transfer
- [x] Cover `MFP`, P visibility, and `WAITP` synchronization through a decoded
      VU1 program
- [x] Cover every EFU producer and shared-unit throughput hazard through
      decoded VU1 instructions
- [x] Cover VU1 `XGKICK` packet initiation, completion stalls, address
      wrapping, and pipeline drain behavior
- [x] Run progressively larger fragments of naken_asm's `rotation_vu1.asm`
- [x] Route the unmodified `rotation_vu1.asm` through concrete PATH1 and
      render its Gouraud triangle in GS local memory

## Milestone 3: VIF and Graphics Path

- [x] Implement VIF0/VIF1 configuration state and strict command decoding
- [x] Add streaming VIF packet ingestion with partial-payload and alignment
      tracking
- [x] Support MPG microprogram upload
- [x] Support `STMASK`, `STROW`, and `STCOL` payloads and complete `UNPACK`
      transfer semantics, including CYCLE, MASK, MODE, TOPS, and wrapping
- [x] Support `MSCAL`, `MSCALF`, and `MSCNT` execution control together with
      TOP/TOPS/DBF transitions and VU `XTOP`/`XITOP`
- [x] Decode stateful GIF packets, including PACKED, REGLIST, IMAGE, register
      descriptors, A+D writes, and packet termination
- [x] Route VIF1 `DIRECT` and `DIRECTHL` payloads into GIF PATH2
- [x] Implement minimal GS drawing registers, local memory, and PSMCT32
      framebuffer addressing
- [x] Render a flat untextured triangle with GS fixed-point coordinates,
      scissoring, and top-left coverage rules
- [x] Interpolate per-vertex RGBA values for Gouraud-shaded triangles
- [x] Route VU1 `XGKICK` output into GIF PATH1
- [x] Add packet-boundary GIF path arbitration, PATH2 retry stalls, VIF
      synchronization commands, `MSKPATH3`, and command interrupts

### PATH3 Transport and Arbitration

Establish host-fed PATH3 transport before EE DMAC integration in Milestone 4.

- [x] Add a host-fed PATH3 qword transport with explicit ready/stall results,
      partial submission support, and counters for attempted and transferred
      qwords
- [x] Route PATH3 PACKED, REGLIST, and IMAGE packets through the shared GIF
      decoder without consuming source data while arbitration blocks progress
- [x] Apply `MSKPATH3` at the transport boundary and resume a masked transfer
      without losing packet, descriptor, or payload position
- [x] Verify packet-boundary arbitration under simultaneous PATH1, PATH2, and
      PATH3 demand, including the established PATH1 > PATH2 > PATH3 priority
- [x] Implement PATH3 IMAGE slicing and interruption at documented boundaries,
      preserving decoder state across preemption and resumption
- [x] Enforce `DIRECTHL` non-preemption while its PATH2 packet owns the GIF
- [x] Expose `GIF_MODE` IMT/M3R control, `GIF_STAT` PATH3 status, and
      interrupted `GIF_P3CNT`/`GIF_P3TAG` state through a privileged-register
      boundary
- [x] Model and test documented GIF arbitration transition and idle-cycle
      timing rather than treating ownership changes as instantaneous
- [x] Add deterministic contention tests covering fragmented input, masking,
      backpressure, preemption, EOP completion, and zero qword duplication or
      loss

- [x] Implement host-to-local and local-to-host GS image transfers
  - [x] Implement PSMCT32 host-to-local transfer registers and `HWREG` uploads
  - [x] Implement PSMCT32 local-to-host readback and bus-direction integration
- [x] Render points, lines, sprites, strips, and fans with the applicable
      fixed-point coverage and vertex-reuse rules
  - [x] Render points and sprites and expose them in `neko_desktop`
  - [x] Render independent lines and line strips and expose them in
        `neko_desktop`
  - [x] Render triangle strips and fans and expose them in `neko_desktop`
- [x] Upload and sample a PSMCT32 texture with nearest-neighbor filtering
- [x] Handle fixed `UV` and perspective-correct `STQ` texture coordinates
  and expose PATH3-uploaded textured drawing in `neko_desktop`
- [x] Implement destination-alpha testing and configurable alpha blending
  and expose translucent layered drawing in `neko_desktop`
- [x] Add PSMZ32 Z-buffer storage, depth testing, and depth writes,
  with visibly depth-ordered geometry in `neko_desktop`

### Graphics Path Integration Programs

- [x] Stream fragmented VIF packets and verify payload length and alignment
      handling
- [x] Upload and execute a VU1 program through VIF `MPG` and `MSCAL`
- [x] Verify VIF TOP/ITOP values through decoded `XTOP`/`XITOP` instructions
- [x] Transfer masked, cycled, and TOPS-relative vertex data through VIF
      `UNPACK`
- [x] Route a synthetic GIF packet through VIF `DIRECT` into GS registers
- [x] Render a PATH2 synthetic triangle and assert its GS framebuffer hash
- [x] Route a VU1 `XGKICK` packet through GIF PATH1 into the same GS path
- [x] Render a complete `MPG` -> `UNPACK` -> `MSCAL` -> `XGKICK` triangle and
      assert its framebuffer hash
- [x] Present the synthetic framebuffer through an optional SDL3 desktop
      frontend without adding a window-system dependency to `neko_core`
- [x] Present the external `rotation_vu1.asm` Gouraud triangle through the
      desktop frontend using a rotation-specific host setup
- [x] Animate the rotation demo with deterministic sine/cosine inputs while
      rerunning the authentic VU1 transform for every displayed frame
- [x] Add textured, blended, and depth-tested scenes as GS support expands

### Graphics Path Diagnostics Foundation

- [x] Add a structured GIF/GS trace in `neko_diagnostics` that decodes GIFtags
      (`NLOOP`, `EOP`, `FLG`, `NREG`, and descriptors), path ownership, and GS
      register writes instead of requiring raw qword inspection
- [x] Add a compact GIF transfer summary covering path requests, selections,
      stalls, transferred qwords, decoded tags and register writes, primitive
      and packet completion, and PATH3 mask transitions
- [x] Add deterministic trace contract tests and keep formatting, streams, and
      file output out of hardware classes

## Milestone 4: System-Level Core

- [x] Add a `NekoSystem` owner for hardware components and global state
- [x] Establish an integer master-clock scheduler
- [x] Run VUs at 147.456 MHz relative to the 294.912 MHz EE clock
- [x] Add EE, memory-map, DMA, GIF, GS, and interrupt coordination
- [x] Connect GIF DMAC channel 2 to the proven PATH3 transport, including
      normal and chain-mode progress, backpressure, completion, and interrupts
- [x] Model GS display circuits, vertical-blank timing, and presentation
      boundaries
- [x] Define reset, frame execution, input, video, and audio interfaces
- [x] Add deterministic save-state serialization
- [x] Add frame hashes and subsystem traces for regression testing

### EE Core Foundation

- [x] Add the R5900 architectural state: 32 128-bit general-purpose registers,
      `PC`, `HI`, `LO`, `HI1`, `LO1`, and `SA`, with immutable register zero
- [x] Fetch aligned 32-bit instructions through the EE memory map and reject
      unmapped or misaligned instruction accesses through architectural
      exceptions
- [x] Add table-driven EE instruction decoding with explicit rejection of
      reserved and unsupported encodings
- [x] Register the EE with the master scheduler at one instruction cycle per
      294.912 MHz master cycle
- [x] Implement integer arithmetic, comparison, logic, and shift instructions
      with the documented 32-bit sign-extension and 64-bit result rules
- [x] Implement multiply, divide, `HI`/`LO`, `HI1`/`LO1`, and shift-amount
      register behavior with instruction-accurate latency where observable
- [x] Implement jumps, branches, likely branches, link behavior, and the
      architectural branch delay slot
- [x] Implement signed and unsigned byte loads and byte stores
- [x] Implement signed and unsigned halfword loads and halfword stores
- [x] Implement signed and unsigned aligned word loads and word stores
- [x] Implement unaligned word merge operations
- [x] Implement aligned doubleword loads and stores
- [x] Implement unaligned doubleword merge loads and stores
- [x] Implement 128-bit `LQ`/`SQ` transfers and preserve full-width GPR values
      across the current integer and memory operations
- [x] Add bounded EE stepping and execution APIs that report stop reason,
      current `PC`, cycle count, and pending exception state

### EE COP0, Exceptions, and Interrupts

- [x] Add the initial COP0 register set, including `Status`, `Cause`, `EPC`,
      `ErrorEPC`, `BadVAddr`, `Count`, and `Compare`
- [x] Implement architectural Reset exception entry
- [x] Implement interrupt, address-error, arithmetic-overflow,
      reserved-instruction, syscall, and breakpoint exception entry
- [x] Preserve branch-delay exception state through `Cause.BD` and the
      documented `EPC` value
- [x] Deliver INTC and DMAC interrupt lines at EE instruction boundaries
- [x] Implement `ERET`
- [x] Add the direct-mapped kernel segments needed by initial guest programs;
      defer TLB, cache, and scratchpad timing until selected software requires
      them

### EE Determinism and Conformance

- [x] Add structured EE issue, branch, memory, exception, and interrupt traces
      stamped with the master cycle
- [x] Include all EE and COP0 architectural and in-flight state in reset,
      save-state, frame-hash, and subsystem-trace contracts
- [x] Add focused hand-encoded conformance programs for arithmetic, delay
      slots, memory access, exceptions, and interrupt entry/return
- [x] Compare repeated and save-state-resumed EE executions for identical
      registers, memory, stop reasons, and trace hashes

### Guest ELF Loading Foundation

- [x] Load PS2 ELF program segments into EE memory with validated addresses,
      sizes, permissions, BSS initialization, and entry-point setup
- [x] Define the initial bare-metal guest runtime contract, including reset
      state, stack placement, bounded execution, and how completion or failure
      is reported to the host
- [x] Add a generic `--elf <path>` desktop argument that executes the loaded
      program through `NekoSystem`
- [x] Keep executable loading, emulation, and presentation separate so the
      desktop does not contain guest-specific drawing or setup behavior
- [x] Run focused EE conformance programs and independently authored scalar
      homebrew ELFs as the first real guests

### Guest MMIO and DMA Foundation

- [x] Extend checked EE data accesses from RAM into mapped device registers
      with explicit access-width, alignment, read/write, and side-effect
      contracts
- [x] Support guest writes to the VIF and GIF FIFO boundaries without routing
      ordinary RAM accesses through host-only bus helpers
- [x] Implement VIF1 DMAC channel 1, including normal and source-chain modes,
      backpressure, completion, and interrupt state
- [x] Add a deterministic guest-driven transfer program that configures DMA,
      feeds VIF1, and observes completion and interrupt behavior

### EE COP2 and VU Macro Mode

- [x] Implement EE COP2 vector transfers: `LQC2`, `QMFC2`, `QMTC2`, and
      `SQC2`
- [x] Implement EE COP2 control transfers and branches: `CFC2`, `CTC2`,
      `BC2F`, `BC2FL`, `BC2T`, and `BC2TL`
- [x] Implement `VCALLMS` and `VCALLMSR` microprogram initiation
- [x] Audit the completed COP2/VU initiation boundary, including interlock
      save-state resume, reset invariants, Stop-state startup, and VIF
      synchronization
- [x] Decode and execute the first VU macro arithmetic slice: `VADD`,
      `VADDi`, `VADDq`, `VADDx/y/z/w`, `VSUB`, `VSUBi`, `VSUBq`, and
      `VSUBx/y/z/w`
- [x] Decode the remaining VU macro arithmetic, conversion, memory, transfer,
      random, and synchronization instruction families and reuse the
      corresponding microinstruction execution semantics where their
      architectural behavior agrees
- [x] Audit every macro-mode opcode in the official table and reject reserved
      COP2 encodings deterministically
- [x] Audit macro execution conformance, including whole-register VF/VI
      hazards, transfer timing, divider serialization, and save-state
      continuation

### Guest Graphics Workloads

- [x] Convert the rotating-triangle demo into an independently authored guest
      program that drives its VU1, VIF, GIF, and GS work through emulated
      hardware
- [x] Convert the POINT/SPRITE demo into an independently authored guest
      program that submits its drawing work through emulated hardware

## Milestone 5: EE COP1

Implement the R5900 floating-point coprocessor from the official EE Core
manuals. Preserve raw guest values and reproduce documented EE behavior rather
than delegating architectural results to host floating-point defaults.

### Architectural Audit and Execution Contract

- [x] Reconcile all 34 concrete COP1 mnemonics in the official instruction
      table with their encodings, fixed fields, operand roles, and reserved
      patterns
- [x] Define the architectural contract for 32 32-bit FPRs, the 32-bit ACC,
      read-only FCR0, writable FCR31 fields, and reserved FCR1-FCR30
- [x] Define deterministic reset values for architecturally undefined COP1
      state without presenting those values as hardware guarantees
- [x] Define COP1 usability checks and Coprocessor Unusable exception behavior
      for operations, transfers, branches, loads, and stores
- [x] Map COP1 operation and move instructions onto the documented C1 and
      integer pipelines, including issue-pair restrictions and resource stalls
- [x] Record which instruction timing is explicitly documented locally and
      pause before assigning any unresolved latency or throughput

### Register, Control, and Memory Transfers

- [x] Add FPR, ACC, FCR0, and FCR31 state with checked architectural accessors
- [x] Implement `MFC1` and `MTC1`, including 32-bit sign extension into EE
      GPRs and lower-word writes into FPRs
- [x] Implement `CFC1` and `CTC1`, including the FCR0 implementation/revision
      value, writable FCR31 fields, hardwired rounding bits, and deterministic
      rejection of reserved control registers
- [x] Implement aligned `LWC1` and `SWC1` through the EE memory and exception
      paths
- [x] Model FPR load availability and dependent-use interlocks consistently
      with the existing non-blocking EE load contract
- [x] Add decode, access, alignment, exception, and transfer conformance tests

### Raw Floating-Point Foundation

- [x] Add raw-bit COP1 value classification for signed zero, normal values,
      exponent-255 finite values, and unsupported IEEE encodings
- [x] Implement EE single-precision normalization and truncation without host
      NaN, infinity, denormal, or rounding-mode behavior leaking into results
- [x] Implement documented signed-zero results for arithmetic, min/max,
      division, square root, and reciprocal square root
- [x] Implement exponent overflow saturation to signed maximum and exponent
      underflow flushing to signed zero
- [x] Implement FCR31 current and sticky `I`, `D`, `O`, and `U` flag updates,
      including instructions that explicitly clear current flags
- [x] Add fixed raw-bit reference vectors derived from the official abnormal
      computation, signed-zero, rounding, and IEEE-difference tables

### Movement, Selection, and Conversion

- [x] Implement `MOV.S`, `ABS.S`, and `NEG.S` with exact raw-bit behavior and
      documented flag effects
- [x] Implement `MAX.S` and `MIN.S`, including signed-zero selection and
      operand-order behavior
- [x] Implement `CVT.S.W` with deterministic signed fixed-point conversion and
      EE truncation
- [x] Implement `CVT.W.S`, including truncation, clamping, and out-of-range
      behavior
- [x] Verify source dependencies, destination hazards, scalar result visibility,
      and fixed-field rejection for each instruction form

### Addition, Subtraction, and Accumulator

- [x] Implement `ADD.S` and `SUB.S` with documented signed-zero, overflow,
      underflow, and flag behavior
- [x] Implement `ADDA.S` and `SUBA.S` with ACC as the architectural
      destination
- [x] Lock the original immediate scalar scheduler's back-to-back FPR RAW
      forwarding, FPR and ACC WAW ordering, and independence between FPR and
      ACC destinations as the pre-pipeline baseline
- [x] Add dependent, independent, exceptional, and save-resume timing tests

### Multiplication and Multiply-Accumulate

- [x] Implement `MUL.S` and `MULA.S`
- [x] Implement `MADD.S` and `MADDA.S`, including their distinct FPR and ACC
      destinations
- [x] Implement `MSUB.S` and `MSUBA.S`, including their distinct FPR and ACC
      destinations
- [x] Preserve the documented intermediate product, ACC, saturation, and
      underflow rules rather than reducing multiply-add to host arithmetic
- [x] Model multiply pipeline occupancy, FPR and ACC dependencies, ACC RAW
      forwarding from accumulator and compound writes, and issue restrictions
- [x] Add dependent, independent, ACC-chained, exceptional, and save-resume
      timing tests
- [x] Add raw-bit compound-operation vectors that distinguish intermediate
      precision and flag behavior from separate host operations

### Comparison and Branch Control

- [x] Implement `C.F.S`, `C.EQ.S`, `C.LT.S`, and `C.LE.S`, including exact
      comparison and equality of positive and negative zero
- [x] Update and expose the FCR31 condition bit with documented visibility
      timing
- [x] Implement `BC1F`, `BC1T`, `BC1FL`, and `BC1TL` through the existing EE
      branch, delay-slot, and likely-annulment machinery
- [x] Verify comparison-to-branch dependencies, taken and untaken paths,
      delay-slot exceptions, and save-state continuation

### Division and Square Root

- [x] Exhaustively audit the local manuals and toolchain documentation for
      numeric timing and branch-adjacent pipeline restrictions
- [x] Implement `DIV.S`, including signed saturation for division by zero and
      distinct `0/0` invalid-operation behavior
- [x] Implement `SQRT.S`, including negative-input absolute-value results and
      signed-zero preservation
- [x] Implement `RSQRT.S`, including numerator sign, negative radicands,
      division by zero, overflow, and underflow
- [x] Introduce a replaceable timing policy and pending multicycle divider
      engine, including initiation restrictions, structural occupancy,
      dependency interlocks, result retirement, and documented `1S`/`2S`
      completion visibility
- [x] Preserve divider continuation and determinism across reset, halt/resume,
      exception entry, state hashing, save states, and execution drain
- [x] Track branch-delay and branch-target proximity and emit a deterministic
      diagnostic for the documented GNUPro pipeline-bug regions
- [x] Complete cross-operation raw-bit, flag, overlap, timing, drain,
      save-resume, and legal/hazardous branch-placement validation

### C1 Pipeline State and Scoreboard Foundation

- [x] Audit the local manual's C1 stages, result visibility, logical/physical
      routing, Table 1-3 issue rules, and the current scalar EE scheduler
- [x] Lock the EE-cycle mapping for `R`, `T`, `X`, `Y`, `Z`, `1S`, and `2S`,
      including which boundary captures operands, exposes bypass values, and
      commits architectural state; explicitly anchor provisional divider
      latency and initiation intervals while either preserving the completed
      block's `N+8`/`N+14` architectural retirement contract or explicitly
      re-baselining that contract and its tests
- [x] Classify every COP1 instruction by Move, Operate, or Branch routing;
      source dependencies; and FPR, ACC, FCR31, condition, GPR, or memory
      destinations; record the section 4.4 evidence for treating `MOV.S` as a
      Move despite its FPR-to-FPR data path
- [x] Add a single-instruction decoded issue latch so scalar dependency and
      resource stalls retain the already-fetched instruction instead of
      re-fetching and re-decoding the same PC each cycle; define whether this
      latch is canonical saved/hashed state or a derived cache, and which
      redirects flush it
- [x] Introduce a general in-flight COP1 operation/result representation with
      program order, current stage, captured operands, destination metadata,
      raw result, affected flags, and raised sticky flags
- [x] Define precise exception and interrupt ownership for in-flight C1 work:
      which older results continue, which younger work is discarded, and what
      handler execution and `ERET` may observe
- [x] Introduce a COP1 scoreboard that distinguishes unavailable operands,
      `1S` bypass-ready values, and `2S` committed values for FPRs, ACC, and
      FCR31
- [x] Adapt the existing pending load and divider units to the shared
      dependency and ordered-writeback interfaces without changing their
      established timing contracts after the divider anchor is explicit
- [x] Route ELF guest-return drain through the shared in-flight interface while
      preserving the existing pending-load and divider completion behavior
- [x] Define deterministic per-cycle trace ordering and sufficient event
      capacity for multiple C1 stage transitions and retirements
- [x] Add a timing-independent PS2DEV COP1 semantic guest as a before/after
      pipeline-migration baseline for raw registers and FCR31
- [x] Add focused state-transition, hash, trace, malformed-save-state, and
      save/resume tests, then complete an independent foundation review

### Representative C1 ALU Pipeline

- [x] Move `ADD.S` and `SUB.S` through `1T` operand selection, immutable `2T`
      capture, their documented `Y/Z` work, `1S` result availability, and `2S`
      architectural writeback
- [x] Add generic per-cycle C1 stage advancement and program-ordered `2S`
      retirement for FPR results and their arithmetic/sticky FCR31 flags
- [x] Implement the `1S`-to-`2T` bypass for dependent add/subtract operations,
      while interlocking pre-`1S` reads and preserving WAW order
- [x] Re-baseline add/subtract issue, dependency, raw-result, flag, hash, trace,
      malformed-save-state, halt/resume, exception, and save/restore coverage
- [x] Complete an independent review of the representative staged path before
      migrating another operation family

### C1 Unary and Min/Max Migration

- [x] Migrate `ABS.S` and `NEG.S` through their documented stages, preserving
      signed-zero and raw extended-finite behavior
- [x] Migrate `MAX.S` and `MIN.S`, including selected-operand normalization and
      ordered FCR31 overflow/underflow updates
- [x] Cover cross-family forwarding, same-register read/write aliases, WAW
      ordering, traces, hashes, exceptions, and save-state continuation
- [x] Complete an independent unary/min/max migration review

### C1 Conversion and Comparison Migration

- [x] Migrate `CVT.S.W` and `CVT.W.S` through their documented `Y/Z` work and
      ordered FPR/FCR31 result delivery
- [x] Migrate `C.F.S`, `C.EQ.S`, `C.LT.S`, and `C.LE.S`, retaining condition
      results in flight until their documented visibility and commit boundaries
- [x] Interlock or forward comparison-to-branch and `CFC1` dependencies without
      exposing a younger condition or FCR31 value early
- [x] Verify conversion truncation, comparison edge cases, ordered flags and
      condition writes, traces, hashes, exceptions, and save/restore behavior
- [x] Complete an independent conversion/comparison migration review

### C1 Multiply and Accumulator Migration

- [x] Define a replaceable ACC capture, forwarding, and commit timing policy
- [x] Migrate `MUL.S` and `MULA.S` through captured `T` operands, documented
      `X/Y` multiply work, and ordered FPR or ACC result delivery
- [x] Migrate accumulator add/subtract and compound
      `MADD`/`MSUB`/`MADDA`/`MSUBA` families through the proven multiply and
      accumulator paths
- [x] Interlock or forward FPR, ACC, FCR31, WAW, and read-modify-write
      dependencies at their documented FPR/FCR31 and provisional ACC
      visibility boundaries
- [x] Preserve product and accumulation arithmetic/sticky flags in program
      order across overlap, exceptions, halt/resume, hashes, traces, and
      save-state restore
- [x] Complete an independent multiply/accumulator migration review

### C1 Divider Stage Integration

- [x] Deliver `DIV.S`, `SQRT.S`, and `RSQRT.S` results through the shared
      `1S`/`2S` writeback contract using the foundation's explicit latency
      anchor while retaining the isolated provisional initiation policy
- [x] Preserve two-result overlap, destination and FCR31 dependencies,
      older-before-younger retirement, and the documented completion-cycle
      issue behavior
- [x] Re-baseline divider traces, hashes, branch-proximity diagnostics,
      exceptions, halt/resume, malformed save states, and save/restore coverage
- [x] Complete an independent divider-stage integration review

### C1 Operate Integration and Final Review

- [x] Generalize ELF guest-return drain from pending loads and divider results
      to every in-flight C1 Operate family, without adding emulated cycles
- [x] Run the timing-independent PS2DEV COP1 semantic guest before and after
      migration and retain identical raw FPR and FCR31 results
- [x] Validate cross-family forwarding, ordered FPR/ACC/flag/condition
      retirement, exceptions, `ERET`, reset, halt/resume, state hashes,
      deterministic traces, malformed save states, and save-state continuation
- [x] Complete an independent final C1 Operate review

### COP1 Move and Memory Timing

- [x] Model `MFC1`, `MTC1`, `CFC1`, `CTC1`, and `MOV.S` as COP1 Move
      instructions routed through logical Pipe 1 and the LS/C1 physical pipes
- [x] Model `LWC1` and `SWC1` operand transfer at the documented integer
      `D/W` boundaries while retaining the current synchronous hit-path memory
      policy
- [x] Replace the scalar one-bit Operate-to-Move approximation with stage-aware
      C1 resource occupancy that remains correct before concurrent issue is
      enabled
- [x] Ensure load-use, store-read, move-read/write, control-register, and
      exception dependencies share the COP1 scoreboard
- [x] Preserve precise memory side effects and in-flight Move state across bus
      stalls, interrupts, `ERET`, reset, halt/resume, hashing, and save states
- [x] Complete focused Move/Operate/Load/Store timing and continuation review

### EE Two-Wide Issue Foundation

- [x] Classify every implemented EE instruction by logical-pipe eligibility
      and physical resources, independent of COP1 execution semantics
- [x] Define how older/younger program order maps to fixed and flexible logical
      pipes, including whether a Pipe-1-only older instruction may pair with a
      Pipe-0-only younger instruction
- [x] Add two-instruction fetch/decode and the documented staging register for
      an instruction that cannot issue from `Q` to `R`, widening the scalar
      decoded-instruction latch without reintroducing fetch side effects
- [x] Select zero, one, or two candidate instructions in program order while
      respecting same-pair data dependencies, Pipe 0/Pipe 1 routing, and
      Table 1-3 `O`/`X`/`Y` classification without changing scalar retirement
- [x] Define the architectural issue-group contract before activation:
      pair admission is atomic, no half-issued pair is externally observable,
      architectural acceptance remains in older-to-younger program order, and
      stepping and instruction counts include both members of an inseparable
      group without conflating issue with delayed pipeline completion
- [x] Implement and classify `SYNC.P` and `SYNC.L` independently of scheduler
      activation, including deterministic scalar execution and reserved-field
      handling
- [x] Resolve the documented illegal sequences for Branch-Branch,
      Branch-ERET, Branch-SYNC.P/SYNC.L, and
      Branch-Likely-MTSA/MTSAB/MTSAH against the manual, including whether each
      sequence is undefined or merely unpairable; make legality sensitive to
      older/younger operations rather than only pipe-numbered categories, and
      explicitly define whether a branch may issue with its own delay-slot
      instruction while preventing an older `ERET` or other no-delay-slot
      redirect from admitting its sequential successor
- [x] Combine same-pair selection with cross-cycle producer and structural
      readiness from the existing scoreboards; refuse pairs involving deferred
      younger faults, unresolved dependencies, unsafe memory ordering, or
      unresolved control-flow and branch-likely annulment
- [x] Replace single-instruction retirement bookkeeping with ordered
      zero/one/two architectural acceptance records, distinct from issue-trace
      events and delayed pipeline completion or retirement, defining execution
      counts, stop reasons, exception ownership, deterministic per-cycle trace
      ordering, and sufficient trace-event capacity for both instructions and
      their memory, interlock, exception, and hazard events
- [x] Update `stepEEInstruction()`, `runEE()`, `runELF()`, guest-return
      detection, and instruction counts to consume ordered acceptance records
      and stop only after every successfully accepted member has produced its
      record, independently of delayed pipeline completion
- [x] Advance SA-ordering and divider branch-proximity instruction windows from
      ordered acceptance records so the younger instruction observes the
      older instruction's program-order effects independently of pipe number
- [x] Implement ordered per-instruction commit boundaries within an atomic
      issue group, ensuring an older exception suppresses all younger effects,
      a younger synchronous exception preserves completed older effects, and
      older-before-younger register, control, and stop-reason behavior remains
      precise
- [x] Activate only ordinary Table 1-3 `O` register-only pairs; keep every `Y`
      selection on the scalar fallback path until its owning pipeline block
      implements the required stall, and assert that concurrently issued
      physical-resource masks are compatible
- [x] Extend ordinary `O` activation to memory and SA pairs while preserving
      precise memory side effects, bus-stall continuation, and SA ordering
- [x] Extend ordinary `O` activation to branch, branch-likely, and resolved
      delay-slot pairs while preserving delay-slot ownership, annulment,
      redirects, exceptions, and divider branch-proximity windows
- [x] Persist concrete post-activation front-end entries,
      admitted-but-incomplete pipeline work including `Y` stalls, and
      continuation state through reset, interrupts, halt/resume, canonical
      hashes, and transactional save states; do not serialize per-cycle issue
      acceptance records or recomputable selection and assignment state
- [x] Validate single-issue fallback, deferred `Y` behavior, both permitted
      older/younger pipe arrangements, and every implemented
      instruction-category pairing before an independent EE scheduler review

### COP1 Concurrent Issue and Pipeline Integration

- [x] Enable COP1 Operate on Pipe 0 and COP1 Move on Pipe 1 using the completed
      C1 scoreboard and two-wide EE scheduler
- [x] Implement Table 1-3's `Y` combination: both instructions enter `R`, then
      the Pipe 1 COP1 Move stalls for one cycle at `A/T` on the shared C1
      resource; resolve completion ordering when the stalled Move is older,
      and cover every program-order arrangement permitted by the pipe-assignment
      and atomic issue-group contracts
- [x] Enforce cross-cycle FPR, ACC, FCR31, load, store, comparison/branch, and
      C1 structural readiness using the foundation's same-pair dependency
      contract without losing legal concurrent issue
- [x] Verify `1S`/`2T` bypassing, `2S` writeback, divider overlap, and Move
      timing in mixed one-wide and two-wide instruction streams
- [x] Preserve in-flight C1 and issue-queue work across unrelated EE
      execution, interrupts, `ERET`, reset, halt/resume, and save-state restore
- [x] Verify repeated and save-state-resumed executions produce identical
      registers, flags, memory, stop reasons, retirement traces, and trace
      hashes
- [x] Complete an independent final review of the integrated COP1 pipeline

### Guest Integration and Final Conformance Audit

- [x] Add an independently authored transfer and memory program covering
      `MFC1`, `MTC1`, `MOV.S`, `LWC1`, and `SWC1`, including raw-bit
      preservation and memory round trips
- [x] Add an independently authored control-state program covering `CFC1`,
      `CTC1`, writable and fixed `FCR31` fields, condition state, current
      causes, and sticky flags
- [x] Add an independently authored comparison and branch program covering
      every comparison plus `BC1F`, `BC1T`, `BC1FL`, and `BC1TL`, including
      taken, untaken, and annulled delay-slot behavior
- [x] Add an independently authored conversion and unary program covering
      `CVT.S.W`, `CVT.W.S`, `ABS.S`, and `NEG.S`, including zero, sign,
      saturation, and exceptional-flag cases
- [x] Add an independently authored basic arithmetic program covering add,
      subtract, multiply, minimum, and maximum with representative normal and
      exceptional operands
- [x] Add an independently authored accumulator and compound program covering
      the `ADDA`, `SUBA`, `MULA`, `MADD`, and `MSUB` families, including ACC
      forwarding and ordered flag behavior
- [x] Add an independently authored divider program covering `DIV.S`,
      `SQRT.S`, and `RSQRT.S`, including result visibility and initiation
      overlap
- [x] Add an independently authored mixed concurrent-issue program covering
      Operate/Move pairing, dependencies, forwarding, ordered memory effects,
      and deterministic final state
- [x] For each focused COP1 program, assert its expected registers, `FCR31`,
      memory outputs, instruction count, and termination reason; also assert
      retirement and stage traces where timing is part of the contract
- [x] Expand the foundation's PS2DEV semantic baseline into a timing,
      forwarding, branch, and concurrent-issue COP1 capstone
- [x] Audit every defined COP1 opcode for decode, execution, flags, timing,
      hazards, exceptions, tracing, reset, and save-state coverage
- [x] Reject every reserved COP1 encoding and invalid fixed-field combination
      deterministically
- [x] Perform an independent final review of the completed COP1 milestone

### Post-COP1 Architecture and Maintainability Review

#### Architecture Baseline and Risk Map

- [x] Inventory ownership and dependency direction across system orchestration,
      EE execution, VU execution, DMA, VIF, GIF, GS, tracing, and persistence
- [x] Map the cycle-sensitive and allocation-sensitive paths, including EE
      issue and C1 advancement, VU pipeline advancement, DMA transport, GIF
      arbitration, and GS rasterization
- [x] Catalogue duplicated policy and weak control vocabulary, including
      instruction-family classification, program-order traversal, cancellation,
      derived occupancy, save-state reachability, and boolean mode parameters
- [x] Identify mixed-policy methods and classify their extraction boundaries
- [x] Audit focused method purpose without fragmenting coherent hardware owners
- [x] Identify ambiguous behavioral booleans separately from hardware-state flags
- [x] Rank the findings by correctness risk, change amplification, and
      obstruction to MMI work rather than by file size alone
- [x] Record justified non-refactors where direct ownership, fixed-capacity
      storage, or explicit sequencing is clearer than an additional abstraction
- [x] Identify the focused tests, hashes, traces, and save-state cases that
      protect each selected refactor before production code changes begin

#### Shared EE Instruction Metadata

- [x] Define one authoritative metadata contract for each `EEOperation`,
      covering dependencies, issue routing, memory behavior, COP1 family,
      managed-pipeline participation, and operation-specific timing
- [x] Add table-driven completeness tests proving that every defined operation,
      including future MMI operations, has deliberate metadata
- [x] Move COP1 family and timing classification out of `EECore` without
      changing decode, issue, scoreboard, execution, or trace behavior
- [x] Make issue selection, execution, save-state validation, and diagnostics
      consume the shared metadata instead of maintaining parallel classifiers
- [x] Remove superseded classifiers only after focused pairing, hazard,
      retirement, and reserved-operation tests remain unchanged

#### EE Operation Dispatch and Focused Execution Methods

- [x] Classify every `EECore::executeInstruction()` case into a coherent
      execution family with one architectural purpose
- [x] Define an explicit execution outcome that distinguishes completed,
      delayed, faulted, halted, and rejected work without behavioral booleans
- [x] Add a table-driven completeness test proving every value below
      `EE_OPERATION_COUNT` has an execution-family and dispatch classification,
      with invalid enum values handled separately
- [x] Retain one exhaustive operation dispatch boundary while moving operand
      handling, computation, memory access, and side effects into focused
      family methods
- [x] Keep delayed MAC, C1, COP2, branch, and exception ownership at their
      existing lifecycle boundaries rather than hiding timing in handlers
- [x] Avoid per-instruction allocation and unnecessary virtual dispatch in the
      execution path
- [x] Migrate operation families incrementally:
  - [x] Extract scalar arithmetic and special-register families with integer
        and overflow coverage
  - [x] Extract scalar memory width and merge families with alignment, bus,
        trace, and retry coverage
  - [x] Extract remaining C1 admission families with pipeline and retirement
        coverage
  - [x] Extract remaining C2 admission families with retry and VU coupling
        coverage
  - [x] Extract integer branches with link, likely, and delay-slot coverage
  - [x] Re-run guest and cross-family regression coverage after all families
        use focused execution methods
- [x] Remove obsolete execution branches and boolean modes only after every
      defined operation remains exhaustively dispatched

#### EE Front-End and Issue Lifecycle

- [x] Specify the invariant owned by fetch, decode, issue-latch, staging-latch,
      selection, acceptance, and PC-advance state
- [x] Reshape each front-end method around one decision or state transition,
      even when the resulting methods remain private to `EECore`
- [x] Reconcile the stateful public `fetchInstruction()` API with decoded
      front-end continuation so it cannot leave `pc` and live latches in an
      unreachable save-state configuration
- [x] Separate issue-candidate construction from readiness and structural-pair
      policy while retaining the fixed two-entry front end
- [x] Replace boolean controls that select issue-member position, pipeline
      identity, or acceptance mode with explicit enums or result types
- [x] Make issue-group outcomes explicit for accepted, stalled, faulted, and
      cancelled members without changing program-order effects
- [x] Keep delay-slot validation and branch-likely annulment at one documented
      ownership boundary
- [x] Preserve explicit run-control ownership for same-PC host-halt resume,
      different-address restart, and external program-counter mutation
- [x] Cover one-wide, two-wide, stalled younger, faulted older, faulted younger,
      delay-slot, public-fetch, and run-control transitions with focused tests

#### EE C1 Pipeline Lifecycle and Ordered Work

- [x] Introduce one allocation-free program-order traversal primitive for the
      fixed-capacity in-flight C1 array
- [x] Use the traversal primitive for oldest-operation selection, producer
      lookup, ready-prefix retirement, stage transition ordering, and
      cancellation ranges
- [x] Use the same read-only order and reachability predicates for malformed
      save-state validation, or document and test any invariant that must remain
      independently represented by the non-mutating validator
- [x] Define one lifecycle contract for C1 allocation, source capture,
      advancement, completion, retirement, and slot release
- [x] Split C1 methods that currently combine traversal, eligibility,
      transition, side effects, and tracing into focused stages with explicit
      inputs and outcomes
- [x] Centralize divider occupancy derivation and reconciliation so runtime,
      reset, hashing, and save-state validation cannot disagree
- [x] Centralize exception and flush cancellation of in-flight C1 work while
      preserving older completed effects and discarding younger work
- [x] Preserve load interlocks, `1S`/`2T` bypasses, `2S` writeback, Move
      blocking, divider overlap, and ordered memory exceptions
- [x] Prove repeated and save-state-resumed mixed C1 workloads retain identical
      retirement traces, hashes, register state, and memory state

#### Architectural Events, Exceptions, and Observation

- [x] Separate architectural state transitions from trace publication so trace
      enablement cannot participate in execution decisions
- [x] Define typed EE events for acceptance, memory access, branch scheduling,
      exception entry, interrupt delivery, C1 stage transition, and retirement
- [x] Give exception entry one owner for CP0 updates, branch-delay handling,
      fault metadata, issue-front-end cleanup, and younger-work cancellation
- [x] Replace ambiguous trace and exception boolean parameters with typed event
      payloads or explicit modes
- [x] Preserve cycle-local fixed-capacity event storage and deterministic event
      ordering
- [x] Audit synchronous VU and GIF trace callbacks as observation dependencies;
      either route them through the event boundary or explicitly defer them
      with traced-versus-untraced equivalence coverage
- [x] Define callback-exception containment or post-commit publication
      semantics for VU and GIF observation, and cover throwing callbacks so an
      observer cannot make a completed hardware transition appear retryable
- [x] Verify tracing off and tracing on produce identical architectural hashes,
      save states, stop reasons, and guest results

#### Persistence and Derived-State Reconciliation

- [x] Separate save-state container framing from per-component serialization,
      validation, reconciliation, and transactional commit
- [x] Keep format order and the current save-state version stable during
      behavior-preserving source decomposition
- [x] Define which fields are serialized architectural state, serialized
      runtime state, derived state, or host wiring
- [x] Consolidate pure, non-mutating invariant predicates shared by runtime
      assertions and load validation, retaining separate adapters where live
      state and malformed parsed state require different handling
- [x] Complete an independent read-only review of the shared invariant boundary
      before changing reconciliation or component ownership
- [x] Reconstruct pointer topology and derived occupancy through explicit
      reconciliation steps rather than scattered commit-time assignments
- [x] Make cross-component transactional ownership explicit, then split the
      save-state implementation by coherent component boundaries
- [x] Preserve canonical bytes, strict malformed-state rejection, atomic load
      failure, and byte-identical repeated saves throughout reconciliation and
      source decomposition
- [x] Complete an independent read-only review of reconciliation, transactional
      commit, and the resulting component boundaries

#### System Orchestration and Remaining Subsystem Boundaries

- [x] Audit `NekoSystem` construction, clock ordering, interrupt
      synchronization, run control, guest outcome classification, tracing, and
      frontend-facing output as distinct responsibilities
- [x] Confirm system-owned hardware and deterministic master-cycle sequencing
- [x] Separate host-control/observation helpers without duplicating hardware owners
- [x] Audit VU issue, pipeline orchestration, execution units, writeback,
      flags, and macro/micro coordination for duplicated lifecycle policy
- [x] Replace positional VU admission and behavioral booleans with typed requests
- [x] Centralize VU start, handoff/takeover, Force Break, reset, and failure policy
- [x] Separate VU callbacks and paired-lower release into focused transitions
- [x] Audit GS register handling, transfer, primitive assembly, rasterization,
      texture sampling, and presentation for boundaries with independent
      invariants
- [x] Separate GS vertex assembly from drawing-kick and rasterizer dispatch
- [x] Audit EEBus, DMA, VIF, and GIF transport for duplicated address,
      readiness, transfer-completion, or arbitration policy
- [x] Move common DMAC control, status, completion, and interrupts out of GIF
- [x] Share GIF/VIF1 normal and source-chain lifecycle without merging endpoints
- [x] Replace the PATH3 interruption Boolean with typed DIRECT/DIRECTHL policy
- [x] Verify CLI, ELF, frame, trace, reset, and save-state workflows remain
      behaviorally identical after the selected boundary changes

#### Final Architecture Review

- [x] Re-audit dependency direction and duplicated policy after all selected
      refactors
- [x] Run the complete optimized repository check and compare representative
      trace, state, framebuffer, and save-state hashes with the baseline
- [x] Complete an independent read-only review of the resulting architecture
      and resolve concrete correctness or maintainability findings
- [x] Record durable contracts/deferrals and confirm readiness for MMI

## Milestone 6: EE Multimedia Extensions

Implement and validate the 91 MMI semantic operations beyond the existing
scalar multiply/divide, memory, and SA-register prerequisites.

### MMI Audit and Architecture Contract

- [x] Inventory and classify all 91 missing semantic operations
- [x] Reconcile direct/nested encodings, reserved cells, and fixed fields
- [x] Confirm existing quadword-memory and SA-register prerequisites
- [x] Resolve Wide routing and younger LZC/ALU/MAC1 `Y` pairing
- [x] Resolve packed multiply/divide timing and asynchronous HI/LO interlocks
- [x] Define core-owned MMI state using existing GPR and HI/LO storage
- [x] Define dedicated packed-MAC continuation and its persistence boundary
- [x] Define deterministic stops for manual-undefined operands and zero divisors
- [x] Identify evidence gaps for `PHMADH`/`PHMSBH` and `PDIVBW`
- [x] Define reviewable implementation, integration, and conformance blocks
- [x] Complete an independent audit and architecture review

### Nested Decode, Metadata, and Wide-Issue Foundation

- [x] Add explicit nested MMI0, MMI1, MMI2, and MMI3 decode tables while
      preserving the existing primary MMI table for direct functions and
      already implemented scalar operations
- [x] Distinguish every defined-but-unimplemented encoding from every reserved
      encoding before enabling semantic execution
- [x] Validate each instruction's required-zero fields and all five defined
      `PMFHL` format selectors; reject unused selectors and reserved table
      cells deterministically
- [x] Classify Table 1-3 `Y` pairs with an explicit continuation policy:
      Wide Operate with a younger LZC, ALU, or MAC1 requires a one-cycle
      younger A-stage continuation, while existing C1 and C2 `Y` pairs do not
      use that front-end continuation
- [x] Add exhaustive direct/nested decode, reserved-field, `PMFHL` format,
      pair-policy, and metadata-completeness tests; defer live Wide routing,
      physical-resource, and continuation tests to the first enabled family
- [x] Complete an independent review of the nested decoder and pair-policy
      foundation before adding instruction semantics

### Packed Logical, Comparison, Absolute, and Count Operations

- [x] Implement `PAND`, `POR`, `PXOR`, and `PNOR` over the complete 128-bit
      operands
- [x] Implement `PCEQB`, `PCEQH`, and `PCEQW` with all-ones or zero results in
      each compared lane
- [x] Implement signed `PCGTB`, `PCGTH`, and `PCGTW` lane comparisons
- [x] Implement signed `PMAXH`, `PMAXW`, `PMINH`, and `PMINW`, including equal
      operands and signed extrema
- [x] Implement `PABSH` and `PABSW`, including the documented minimum-value
      clamp to the corresponding signed maximum without an exception
- [x] Implement `PLZCW` over the two low 32-bit words by counting leading bits
      equal to each word's sign bit and subtracting one, preserving the
      documented 64-bit destination behavior
- [x] Cover register-zero destinations, source/destination aliases, mixed lane
      signs, extrema, decode fields, routing, `Y` issue, traces, hashes, and
      repeated execution
- [x] Complete an independent review of the logical/comparison family

### Packed Addition, Subtraction, and Saturation

- [x] Implement wrapping `PADDB`, `PADDH`, `PADDW`, `PSUBB`, `PSUBH`, and
      `PSUBW` with independent modulo arithmetic in every lane
- [x] Implement mixed `PADSBH`, subtracting the low four halfword lanes and
      adding the high four lanes
- [x] Implement signed-saturating `PADDSB`, `PADDSH`, `PADDSW`, `PSUBSB`,
      `PSUBSH`, and `PSUBSW`
- [x] Implement unsigned-saturating `PADDUB`, `PADDUH`, `PADDUW`, `PSUBUB`,
      `PSUBUH`, and `PSUBUW`
- [x] Cover every positive and negative saturation boundary, carry and borrow
      isolation, source/destination aliases, register zero, routing, pairing,
      traces, hashes, and repeated execution
- [x] Complete an independent review of packed arithmetic

### Packed Interleave, Extend, and Pack Operations

- [x] Implement lower interleave `PEXTLB`, `PEXTLH`, and `PEXTLW`
- [x] Implement upper interleave `PEXTUB`, `PEXTUH`, and `PEXTUW`
- [x] Implement truncating pack `PPACB`, `PPACH`, and `PPACW`
- [x] Implement `PINTH` and `PINTEH` with the manual's exact source-lane
      ordering
- [x] Cover lane-order sentinels, source/destination aliases, register zero,
      decode fields, routing, pairing, traces, hashes, and repeated execution
- [x] Complete an independent review of the interleave/pack family

### Packed Copy, Exchange, and Pixel-Format Operations

- [x] Implement `PCPYH`, `PCPYLD`, and `PCPYUD`
- [x] Implement `PEXEH`, `PEXCH`, `PEXEW`, and `PEXCW`
- [x] Implement `PREVH` and `PROT3W`
- [x] Implement the exact documented 1-5-5-5 to 8-8-8-8 `PEXT5` expansion
      and inverse `PPAC5` truncation, including alpha-bit placement
- [x] Cover asymmetric lane markers, aliases, register zero, format round
      trips where defined, routing, pairing, traces, hashes, and repeated
      execution
- [x] Complete an independent review of copy/exchange/format operations

### Packed Shifts and SA Funnel Shift

- [x] Implement immediate `PSLLH`, `PSRLH`, and `PSRAH`, enforcing the
      documented 0-15 shift-field restriction
- [x] Implement immediate `PSLLW`, `PSRLW`, and `PSRAW`
- [x] Implement `PSLLVW`, `PSRLVW`, and `PSRAVW` using the low five bits of
      the corresponding source word and the documented sign-extended
      doubleword results
- [x] Implement `QFSRV` over the 256-bit `rs || rt` concatenation using the SA
      register, including zero, byte, halfword, and rotate-style cases
- [x] Extend the existing three-instruction SA ordering window so `QFSRV`
      counts as an SA read for `MTSA`, `MTSAB`, and `MTSAH` restrictions
- [x] Cover source/destination aliases, immediate reserved values, SA
      save/restore, branch-likely restrictions, traces, hashes, and
      deterministic continuation
- [x] Complete an independent review of packed shifts and SA ordering

### Parallel HI/LO Transfers

- [x] Implement full-width `PMFHI`, `PMFLO`, `PMTHI`, and `PMTLO`
- [x] Implement `PMFHL.LW`, `PMFHL.UW`, and `PMFHL.LH` with the exact
      documented HI/LO word or halfword selection order
- [x] Implement saturating `PMFHL.SLW` and `PMFHL.SH`, including signed
      boundary values
- [x] Implement `PMTHL.LW`, preserving the HI/LO words that the manual marks
      unchanged
- [x] Model dependencies against all affected `HI`, `LO`, `HI1`, and `LO1`
      resources so scalar and packed MAC work interlocks without false
      independence
- [x] Cover all format selectors, partial-register preservation, aliases,
      register zero, pending-MAC interlocks, traces, hashes, and save states
- [x] Complete an independent review of parallel HI/LO transfers

### Packed MAC Continuation and Word Operations

- [x] Add a fixed-capacity, allocation-free in-flight MMI MAC representation
      with operation, program order, captured operands/results, destination,
      remaining latency, and initiation occupancy
- [x] Permit the documented 4-cycle latency and 2-cycle throughput without
      allowing a third overlapping packed multiply/accumulate operation or a
      conflicting scalar MAC0/MAC1 access
- [x] Implement signed `PMULTW` and unsigned `PMULTUW`, atomically delivering
      the two 64-bit products to the full HI/LO state and `rd`
- [x] Implement `PMADDW`, `PMADDUW`, and `PMSUBW` using the corresponding
      64-bit HI/LO accumulators and documented modulo results
- [x] Enforce the manual's sign-extended-word operand restriction through the
      deterministic undefined-operation policy before allocating delayed work
- [x] Define completion, bypass/interlock, exception, interrupt, host halt,
      guest-return drain, reset, and external-PC-mutation behavior for accepted
      packed MAC operations
- [x] Introduce one save-state version transition for reachable MMI
      continuation, with strict validation, canonical hashing, transactional
      load, byte-stable repeated saves, and malformed-state coverage
- [x] Cover overlapping initiation, HI/LO and `rd` hazards, older-before-younger
      retirement, trace ordering, save/resume, and mixed scalar/packed MAC use
- [x] Complete an independent review of the MMI continuation foundation and
      packed word operations

### Packed Halfword and Horizontal MAC Operations

- [x] Implement `PMULTH`, `PMADDH`, and `PMSUBH` with the documented eight
      16-bit products, HI/LO lane placement, and selected `rd` lanes
- [x] Before implementing `PHMADH` or `PHMSBH`, resolve the manual's undefined
      alternating HI/LO destination words using approved evidence or an
      explicitly accepted deterministic policy; cover the chosen preservation
      or canonicalization behavior in hashes and save states
- [x] Implement `PHMADH` and `PHMSBH` with exact horizontal pair grouping,
      add/subtract direction, defined HI/LO placement, `rd` result selection,
      and the resolved policy for undefined HI/LO words
- [x] Reuse the packed-MAC latency, throughput, occupancy, ordering, and
      persistence owner without duplicating a halfword-specific lifecycle
- [x] Cover signed extrema, modulo accumulation, horizontal lane sentinels,
      aliases, interlocks, overlap, traces, hashes, and save/resume
- [x] Complete an independent review of packed halfword MAC behavior

### Packed Divide Operations

- [x] Implement signed `PDIVW` and unsigned `PDIVUW` over the low word of each
      64-bit half, storing sign-extended quotient and remainder results in the
      corresponding LO and HI halves
- [x] Resolve the `PDIVBW` remainder-extension conflict before implementation
- [x] Implement signed broadcast `PDIVBW` over all four source words using the
      low halfword divisor and the resolved remainder-extension policy
- [x] Model 37-cycle latency and 37-cycle throughput with asynchronous HI/LO
      interlocks and no architectural arithmetic exception
- [x] Enforce manual-defined operand-shape restrictions and the milestone's
      deterministic policy for otherwise undefined zero-divisor results
- [x] Cover signed minimum divided by minus one, signed remainder rules,
      unsigned high-bit inputs, all four broadcast lanes, scalar/packed MAC
      conflicts, interrupts, halt/resume, traces, hashes, and save/resume
- [x] Complete an independent review of packed divide behavior

### MMI Guest Integration and Final Conformance Audit

- [x] Add independently authored PS2DEV semantic guests for packed arithmetic
      and comparisons, shifts and permutations, and HI/LO multiply/divide
      behavior; record fixture provenance and generated hashes
- [x] Add a mixed MMI guest that exercises ordinary `O` pairs, every Wide
      Operate `Y` pairing, SA ordering, scalar/packed MAC interlocks, and
      asynchronous completion
- [x] Assert guest registers, full-width HI/LO and SA state, memory outputs,
      instruction counts, stop reasons, deterministic traces, and state hashes
- [x] Round-trip save states with two overlapping packed MAC operations and
      with a packed divide in flight; verify byte-identical consecutive saves
      after reconciliation
- [x] Audit all 91 MMI semantic operations for decode, operands, lane order,
      arithmetic edge cases, routing, dependencies, timing, exceptions,
      tracing, reset, halt/resume, hashing, and persistence coverage
- [x] Exhaustively reject every reserved primary MMI and nested
      MMI0/MMI1/MMI2/MMI3 encoding, invalid `PMFHL` format, and nonzero
      fixed-field combination
- [x] Run the complete optimized repository check and compare representative
      trace, state, guest-output, and save-state hashes with the pre-MMI
      baseline
- [x] Complete an independent final review of the full MMI milestone and
      resolve every concrete finding
- [x] Complete optimized AddressSanitizer and macOS leak checks

## Milestone 7: EE Virtual Memory and Memory-System Foundation

Support privileged EE software with TLB translation, scratchpad RAM,
guest-visible caches, precise exceptions, and deterministic continuation.
Exact whole-system contention timing remains outside this milestone.

### Evidence and Architecture Plan

- [x] Audit the local manuals and define independently reviewable blocks
- [x] Resolve the 48-entry TLB layout and seven supported page sizes
- [x] Resolve instruction/data cache-array operation encodings
- [x] Classify COP0 writable masks, fixed bits, reset values, and evidence gaps
- [x] Define virtual-segment privileges, cache attributes, and protection faults
- [x] Define TLB probe/replacement behavior and derived ITLB/DTLB accelerators
- [x] Resolve memory-exception priority, vectors, and architectural side effects
- [x] Classify cache-maintenance corner cases and unsupported behavior
- [x] Establish `EEMemorySystem` ownership of translation, caches, and scratchpad
      while retaining core-owned exceptions and a physical-only bus
- [x] Define typed fetch, data, prefetch, and cache-maintenance boundaries
- [x] Define persistence for COP0/MMU, TLB, scratchpad, caches, and continuations
- [x] Keep memory hot paths allocation-free and timing policies replaceable

### COP0 and TLB Management

- [x] Add translation/cache-control COP0 registers with masks, reset, and hashing
- [x] Implement `MFC0`/`MTC0` transfer, usability, and update-order semantics
- [x] Implement `TLBR`, `TLBWI`, `TLBWR`, and `TLBP` across all 48 TLB entries
- [x] Audit COP0 encodings, fixed fields, reserved cells, and valid deferred work
- [x] Cover issue/pairing, delay slots, visibility, exceptions, reset, and hashes
- [x] Complete an independent COP0/TLB-management review

### Functional Address Translation

- [x] Add typed translation and segment classification for all privilege modes
- [x] Implement full TLB translation, permissions, faults, and derived ITLB/DTLB
      acceleration across all supported page sizes and mapping types
- [x] Route public/front-end fetch and every data-access family through
      translation; remove virtual-address interpretation from `EEBus`
- [x] Complete translation conformance, persistence compatibility, optimized and
      sanitizer checks, and independent foundation/integration reviews

### Precise Memory Exceptions

- [x] Distinguish TLB modified, refill, and invalid faults internally
- [x] Commit selected memory-fault register updates atomically through `EECore`
- [x] Implement refill/general vectors, bootstrap selection, and nested exceptions
- [x] Cover alignment, protection, translation, cache, and bus-fault priority
- [x] Integrate precise fetch faults with public fetch and two-wide issue
- [x] Integrate integer, merge, and COP2 data faults without partial effects
- [x] Integrate delayed COP1 faults with ordered completion and cancellation
- [x] Cover interrupts, `ERET`, halt/restart, reset, and deterministic save/restore
- [x] Complete optimized/sanitizer validation and independent exception review

### Scratchpad RAM and DMA Visibility

- [x] Add fixed-capacity 16 KiB scratchpad storage and checked scalar/qword access
- [x] Route scalar and merge accesses through the scratchpad boundary
- [x] Route GPR/COP2 quadwords and delayed COP1 accesses through the same boundary
- [x] Add physical DMAC scratchpad access with functional CPU/DMA visibility
- [x] Map channel-8/channel-9 registers and integrate common DMAC control/reset
- [x] Implement normal `fromSPR`/`toSPR` transfers, wrapping, retry, and interrupts
- [x] Implement `D_SQWC` interleave transfers and reject unsupported channel modes
- [x] Add scratchpad/DMA hashing and transactional continuation persistence
- [x] Complete CPU/DMA width, alias, overlap, reset, and non-coherence conformance
- [x] Complete an independent scratchpad/DMA review

### Guest-Visible Cache Access Foundation

- [x] Add fixed-capacity 16 KiB instruction and 8 KiB data caches with inspection
- [x] Define typed cache access, synchronous refill/writeback, and fault boundaries
- [x] Implement cached fetch, refill, replacement, enable control, and aliases
- [x] Implement cached loads for every access family, including early restart
- [x] Implement write allocation, partial stores, dirty eviction, and writeback
- [x] Implement line locking and a replaceable all-ways-locked miss policy
- [x] Complete an independent cache-access and DMA-visibility review

### Cache Maintenance, Ordering, and Conformance

- [x] Add typed `CACHE` decode/admission; keep branch-predictor work unsupported
- [x] Implement instruction-cache index tag/data load and store
- [x] Implement instruction-cache index fill/invalidate and translated invalidate
- [x] Implement data-cache index tag/data load and store
- [x] Implement atomic data-cache index writeback/invalidate
- [x] Implement translated data-cache hit writeback/invalidate and precise faults
- [x] Implement `PREF` and exhaustively validate `CACHE`/`PREF` encodings
- [x] Preserve uncached access and `SYNC`/`SYNC.L` ordering without cache snooping
- [x] Complete cache, self-modifying-code, DMA-coherence, and lifecycle conformance
- [x] Complete independent instruction-integration and final block reviews

### Determinism, Persistence, and Diagnostics

#### Memory Observation Contract

- [x] Define typed virtual/physical memory observations with routing and faults
- [x] Observe all fetch/data paths through versioned, encoding-tested traces
      without changing execution behavior
- [x] Complete an independent observation-schema and trace-compatibility review

#### Memory-State Hashing and Transactional Validation

- [x] Audit memory/core/DMA state against canonical hashes and persistence
- [x] Share memory-state invariants and reject malformed state before commit
- [x] Prove deterministic saves, dirty-destination loads, and continuation across
      faults, cache transfers, DMA, interrupts, halts, and restarts

#### Schema-Aware Save-State Diagnostics

- [x] Add optional codec observers with field paths, byte ranges, and decode errors
      while preserving unobserved save/load behavior
- [x] Observe the complete container/payload without changing bytes or transactions
- [x] Retain a diagnostic schema with stable formatting and hashed large ranges
- [x] Add exact-path lookup and deterministic semantic diff without a second parser
- [x] Expose `neko save-state inspect|diff|locate` with opt-in byte expansion
- [x] Add bounded, path-based test mutation with checksum repair
- [x] Migrate malformed-state and transactional-load tests to schema paths
- [x] Remove corruption offsets while retaining independent layout guards

#### Deterministic Diagnostic Closure

- [x] Audit memory-system failures for demonstrated diagnostic gaps
- [x] Complete deterministic trace, hash, save/load, and lifecycle conformance
- [x] Complete an independent observation, persistence, and diagnostics review

### Guest Conformance and Milestone Closure

Validate the completed memory system with independently authored guests and a
bounded BIOS experiment.

- [x] Audit host conformance and identify missing end-to-end guest evidence
- [x] Add a COP0/TLB-management guest with probe/read/write result blocks
- [x] Add a mapped-memory guest covering page sizes, mappings, permissions,
      and precise exceptions through a guest handler
- [x] Add a scratchpad guest covering CPU, normal/interleave DMA, and interrupts
- [x] Add a cache-workflow guest covering maintenance, aliases, locking,
      DMA visibility, cache controls, and self-modifying code
- [x] Pin guest results, deterministic hashes, provenance, and regeneration;
      keep PS2DEV optional for ordinary builds
- [x] Complete optimized conformance and pre-milestone compatibility comparisons
- [x] Complete the memory-system architecture and maintainability review
- [x] Complete independent final review, sanitizer, and native leak checks
- [x] Add a bounded `--bios` startup diagnostic and use its first unsupported
      dependency to scope the next milestone

## Milestone 8: IOP Execution and Platform Foundation

Build an independently testable R3000A IOP subsystem with memory, caches,
exceptions, interrupts, timers, and persistence. BIOS startup and endpoint DMA
remain with their owning milestones.

### Evidence Closure and EE Startup Prerequisite

- [x] Audit the local manuals and define reviewable IOP blocks and ownership
- [x] Resolve the EE `PRId` contract exposed by the BIOS experiment
- [x] Implement/test read-only EE `PRId`; rerun the bounded BIOS experiment and
      defer its reserved-MMIO dependency to Milestone 9
- [x] Archive an authoritative R3000A architecture source with provenance
- [x] Produce a source-linked R3000A contract and deferred-work matrix
- [x] Resolve the retail CPU, COP0, reset, and privileged-state profile
- [x] Define the cache/scratchpad profile and replaceable provisional geometry
- [x] Resolve RAM, reset vector, shared ROM, scratchpad, and physical aliases
- [x] Resolve INTC/timer register maps, reset, masks, acknowledgements, and causes

### IOP Core and Physical-Memory Foundation

- [x] Add `IOPCore` architectural state, continuations, reset, and inspection
- [x] Add typed `IOPBus` access to RAM/mirrors, shared immutable ROM, and scratchpad
- [x] Connect core fetch/data access through segment classification and the bus,
      retaining core-owned exceptions and commit
- [x] Complete an independent state-ownership and memory-boundary review

### R3000A Decode and Integer Execution

- [x] Add fixed-size decode and audit supported, reserved, and deferred encodings
- [x] Add the core fetch/decode/effect/commit path and execution accounting
- [x] Implement NOP, logical, shift, comparison, and immediate operations
- [x] Implement trapping/non-trapping add/subtract and precise overflow
- [x] Implement multiply/divide, HI/LO transfers, interlocks, and edge cases
- [x] Implement jumps, links, branches, and precise delay-slot continuation
- [x] Add a bounded host runner with result/exception metadata; defer IRX linking
- [x] Complete an independent decode, execution, control-flow, and runner review

### Loads, Stores, COP0, and Precise Exceptions

- [x] Implement aligned loads, extension, one-instruction delay, and write conflicts
- [x] Implement aligned stores with ordered admission and IOP bus-failure policy
- [x] Implement `LWL`/`LWR` and `SWL`/`SWR`, bypass, and fault atomicity
- [x] Implement IOP COP0 registers, masks, reset, transfers, and privilege
- [x] Implement precise synchronous exceptions, delay-slot metadata, and vectors
- [x] Implement `RFE`, KU/IE stacking, nested exceptions, and interrupt admission
- [x] Complete exception/lifecycle conformance and independent correctness review

### IOP Guest-Visible Cache Foundation

- [x] Define cache ownership, provisional geometry, routing, and lifecycle
- [x] Add fixed-capacity storage, typed admission, reset, and ROM reconfiguration;
      complete an independent ownership/routing review
- [x] Implement/review cached fetch, bypass, aliases, stale code, and failed fills
- [x] Implement/review cached loads with delayed results, merges, and precise faults
- [x] Implement/review ordered write-through stores, merges, and external visibility
- [x] Implement/review `Status.IsC`/`SwC`, probing, and isolated invalidation
- [x] Complete cache/alias/fault/lifecycle conformance; defer persistence and traces
      to their integration blocks
- [x] Complete independent block review and optimized AddressSanitizer checks

### IOP Interrupt Controller

- [x] Add/review the 26-cause controller, masks, acknowledgement, reset, and output
- [x] Map INTC registers with side-effecting reads and atomic access rejection
- [x] Connect/review aggregate delivery through `Cause.IP2` and core-owned admission
- [x] Add guest conformance for concurrent causes, masks, acknowledgement,
      exception ordering, reset, and pause/resume
- [x] Complete independent block review and optimized AddressSanitizer checks

### IOP Timers and Clock Integration

- [x] Resolve timer modes, gates, rearming, writes, and coincident-event contracts
- [x] Add/review six fixed-capacity counters, register lifecycle, and inspection
- [x] Implement/review typed clock/gate progression, prescalers, target/overflow,
      and interrupt requests to INTC
- [x] Map count/mode/target with atomic MMIO admission and decoded guest coverage;
      leave unsupported hold registers unmapped
- [x] Add/review deterministic pixel/display timing, edge ordering, reset,
      and persistence
- [x] Persist/hash complete IOP state and continuations with ROM compatibility,
      system ownership, and transactional loading; complete persistence review
- [x] Schedule IOP at one cycle per eight master cycles with deterministic
      device/CPU ordering, shared immutable BIOS configuration, and EE-only
      startup compatibility; complete integration review
- [x] Add end-to-end timer/CPU conformance for target/overflow coincidence,
      gate edges, prescaler phase, simultaneous interrupts, independent INTC
      acknowledgement, halted CPU versus advancing devices, reset while
      active, deterministic repetition, and save/load continuation. Do not
      claim undocumented retail contention or interrupt-pulse timing.
- [x] Complete independent block review and optimized AddressSanitizer checks

### Diagnostics and IOP Conformance

- [x] Define/test append-only IOP trace identities and CPU, memory, timer, and
      INTC payload encodings through the existing stable transport.
- [x] Add/review fixed-capacity CPU instruction, retirement, branch, memory,
      exception, interrupt, and execution-state observations, including failed
      accesses and delayed-result/delay-slot boundaries.
- [x] Add/review timer and INTC observations with deterministic master-cycle
      publication, preserving device/CPU order and distinct same-cycle changes.
- [x] Prove tracing observational across disabled/enabled collection, reset,
      stop/start/clear, and save/load; retain existing EE-only trace encodings.
- [x] Establish shared host-authored semantic fixtures using the existing IOP
      runner; cover decode, arithmetic, control flow, loads/stores, and COP0
      without a BIOS, IRX services, or an external compiler as the oracle.
- [x] Add whole-system exception/INTC/timer programs with reset, deterministic
      repetition, and mid-execution save-state continuation.
- [x] Run focused tests followed by the complete optimized repository check.
      Compare repeated-run instruction totals, cycle totals where modeled,
      traces, hashes, and byte-identical save-state continuation.
- [x] Complete the bounded architecture and maintainability review. Inspect
      CPU/bus/device ownership, decode/exception policy, and scheduler boundaries.
- [x] Complete an independent final review, resolve every concrete finding,
      and run optimized AddressSanitizer and native macOS leak checks.

## Milestone 9: EE-IOP Interconnect and Deterministic BIOS Startup

Connect the completed EE and IOP foundations through the documented SIF and
boot-control boundaries, then advance a real BIOS through a bounded,
deterministic startup sequence. Treat the BIOS as an integration workload and
stop at explicit missing-device boundaries rather than silently stubbing
required hardware.

The target is the first guest-executed EE-IOP BIOS startup request/response,
with independently authored guests proving bidirectional DMA. A browser,
game boot, and host-implemented SIF CMD/RPC services are outside this milestone.
An earlier unsupported dependency is a scope checkpoint, not completion.

### Evidence Closure and Interconnect Contract

- [x] Audit local SCEI manuals and approved local secondary sources; assess
      dependencies and split the milestone into reviewable blocks.
- [x] Select the BIOS startup target, state owners, extension boundaries,
      diagnostic direction, and risk-based validation/review plan.
- [x] Resolve source-linked normal-mode SIF register views, widths, masks,
      reset values, signal/interrupt acknowledgements, and boot-control semantics.
- [x] Resolve SIF stream/tag framing, word/qword conversion, FIFO capacity,
      partial-packet behavior, and atomic backpressure/retry boundaries.
- [x] Resolve IOP SIF DMA channel controls, tag/count semantics, priority
      enables, completion flags, and aggregate interrupt acknowledgement.
- [x] Classify bounded BIOS startup prerequisites, including reserved EE MMIO
      at `0x1000f500`; separate reserved-access policy from functional controls.
- [x] Pin cross-clock visibility and lifecycle contracts; register adopted
      provisional policies and complete independent architecture review.

### SIF Registers and Signal Foundation

- [x] Add one shared SIF state owner with typed EE/IOP register views,
      fixed-capacity transport storage, checked inspection, and reset.
- [x] Map resolved MMIO widths and side effects through both physical buses;
      reject unsupported accesses atomically without broad MMIO fallbacks.
- [x] Route resolved SBUS signals through the existing interrupt controllers,
      separately from DMA completion, masks, and CPU exception admission.
- [x] Compose the SIF owner/router into `NekoSystem`, persist/hash the new
      state transactionally, and append opt-in register/signal observations
      without changing existing trace identities.
- [x] Add two-CPU register/signal conformance, acknowledgement, active reset,
      and restore coverage; complete independent review and sanitizer checks.

### Boot Lifecycle and Bounded Startup Diagnostics

- [x] Admit/test only exact full-word writes to reserved `0x1000f500` as a
      stateless no-op; reject reads, other widths, partial lanes, and unrelated
      MMIO without adding conditional startup devices.
- [x] Add an explicit dual-CPU BIOS startup/run boundary with shared ROM and a
      master-cycle budget; preserve existing EE-only startup behavior.
- [x] Define/test whole-machine reset and host CPU halt/resume scopes;
      keep unevidenced guest reset and mode operations explicitly unsupported.
- [x] Add an on-demand/budget-boundary startup summary for both CPUs, SIF,
      implemented DMA, and interrupts; report unsupported accesses and waits
      without side-effecting diagnostic reads or per-poll logging.
- [x] Cover one CPU halted/faulted while devices and its peer advance, budget
      exhaustion, reset, repetition, and restore; complete independent review
      and sanitizer checks.

### SIF1: EE-to-IOP DMA

- [x] Add focused IOP DMA control/interrupt ownership for the first endpoint;
      map common controls and channel-10 register/start admission without dummy
      engines; include reset, hashing, and save/load from the first state.
- [x] Extend existing EE DMAC ownership for channel 6 and source-chain state;
      include CTRL lifecycle effects and persistence, reusing valid shared
      transitions rather than parallel policy.
- [x] Implement EE source admission and SIF framing, including physical
      scratchpad sources, tag/count/termination, atomic backpressure/retry,
      persistence, and EE-phase scheduling with local completion/error effects.
- [x] Implement IOP header/payload admission and physical destination writes
      without cache snooping; include count/termination, atomic retry,
      persistence, and IOP-phase scheduling with local completion effects.
- [x] Cover endpoint completion through EE DMAC and IOP DMA/INTC; distinguish
      sender completion from receiver drain, independent acknowledgement,
      masks, and rearming.
- [x] Append bounded transfer/wait/completion observations; cover reset,
      save/load, and malformed partial state at each acceptance boundary.
- [x] Add authored EE-to-IOP guest conformance and regress existing DMA paths;
      complete independent review and sanitizer checks.

### SIF0: IOP-to-EE DMA

- [x] Extend IOP DMA ownership for channel 9 source tags and EE DMAC ownership
      for channel 5 destination-chain state without duplicating common control;
      map register/start admission and include reset, hashing, and transactional
      persistence from the first state, without adding transfer engines.
- [x] Implement IOP source admission, transmitted EE tags, word/qword packing,
      and normal-mode EE physical destinations, including resolved scratchpad
      addressing, through checked single-channel grants.
- [x] Add common EE DMAC priority eligibility through `D_PCR` and accepted-tag
      PCE controls across implemented channels, including atomic retry,
      reset, hashing, and transactional persistence.
- [x] Add common `D_STADR`, `D_CTRL.STS/STD`, and `D_STAT.SIS` stall control
      across implemented producers and drains, including normal transfers,
      source-chain REFS packets, slice admission, and resumable persistence.
- [x] Implement destination tags, zero/partial packets, termination, and
      backpressure/retry using common priority/stall control, including CNTS
      publication and explicit policy for unresolved residual data.
- [x] Route endpoint-local completion, masks, acknowledgements, and receiver
      rearming independently of SIF1 and SBUS signals; integrate both SIF0
      endpoints into the existing common-controller scheduling boundaries.
- [x] Audit framing and accepted-continuation persistence/hashing; cover
      partial tags, partial qwords, cached aliases, active reset, and save/load.
- [x] Add authored IOP-to-EE guest conformance and bounded observations;
      complete independent review and sanitizer checks.

### Cross-Channel Scheduling and Lifecycle Conformance

- [x] Audit the integrated 8:1 master/IOP schedule and pin both directions'
      CPU/device admission, same-cycle transfer visibility, and interrupt
      ordering with focused regressions, without claiming retail bus timing.
- [x] Exercise simultaneous directions, unequal producer/consumer progress,
      full/empty transport, receiver-late start, channel disable/restart,
      and independent completion/INTC acknowledgement.
- [x] Cover same-cycle CPU/DMA memory overlap, exceptions, interrupts, CPU
      halt/resume, whole-machine reset, and evidenced guest-reset scope.
- [x] Prove repeated-run equivalence, observational tracing, canonical combined
      state comparison, and byte-identical restore at active transfer boundaries;
      reject malformed cross-owner continuation without destination mutation.
- [x] Complete independent cross-component review and optimized sanitizer checks.

### BIOS Startup Diagnostics and Attribution

- [x] Pin the supplied BIOS identity, bounded discovery budget, and cold-start
      configuration; record the first CPU stop or budget-boundary state without
      claiming a handshake. Retain ROM bytes outside tracked fixtures.
- [x] Add opt-in ordered BIOS trace export with an explicit collection limit
      and truncation reporting, preserving startup, budgets, and guest
      exception-handler execution.
- [x] Decode the first failed access/rejected instruction with CPU, instruction,
      delay-slot, address/route, and exception context; retain available evidence
      on runtime errors and identify incomplete-cycle prefixes.
- [x] Complete side-effect-free SIF snapshots for EE DMA5/6 and IOP DMA9/10,
      including continuation, common-controller eligibility, and arbitration
      state without inferring readiness or deadlock.
- [x] Add BIOS-free diagnostic regressions for attribution, ordering, limits,
      errors, and observational equivalence; complete independent review and
      the optimized repository check before dependency classification.
- [x] Run the complete optimized and AddressSanitizer checks to close the
      diagnostic block independently of reaching the BIOS handshake.

### Staged BIOS Discovery

- [x] Add failure-only scouting and opt-in complete-cycle early stopping,
      preserving existing failure attribution and guest exception behavior.
- [x] Add periodic same-build scout checkpoints, targeted replay, local
      trace/plain verification, and stage timing/progress; retain conformance mode.
- [x] Validate the staged workflow with BIOS-free regression and integration
      tests, independent review, and complete optimized/AddressSanitizer checks.

### BIOS Campaign Driving

- [x] Add durable campaign ownership, deterministic execution status and an
      explicit evidence-inspection gate without changing BIOS execution modes.
- [x] Add a maintained launch/monitor/reconcile/finalize procedure with
      completion monitoring, session recovery and no implicit relaunch.
- [x] Validate lifecycle/error handling with BIOS-free regressions and authored
      integration coverage; obtain independent correctness review.

### BIOS Prerequisite Discovery and Bounded Repairs

- [x] Classify the initial discovered pre-handshake dependencies against the approved
      contract; obtain scope approval and split prerequisite work into testable
      items before extending hardware.
- [x] Repair EE cached installed-ROM fills across instruction/data and related
      CACHE/PREF paths; retain immutable backing, MMIO rejection, and atomic
      failure behavior with authored BIOS-free regressions.
- [x] Classify the newly exposed EE startup TLB-store exception by reconstructing
      guest mapping and CPU-state provenance; define any justified repair as a
      separately approved, testable item before changing translation or reset.
- [x] Repair EE scratchpad TLB matching/selection for approved PageMask `0` and
      16 KiB profiles; preserve ordinary mappings and canonical state, with
      authored guest, boundary, permission, probe/accelerator, and restore tests.
- [x] Add the approved exact-word IOP SSBUS configuration profile at
      `0x1f801010`, with bounded access admission, reset/readback, canonical
      hashing, atomic save/load, and authored BIOS-free regressions.
- [x] Re-run bounded discovery after the approved repairs; classify the next
      responsible CPU stops or waits without treating consequential handler
      failures as new hardware requirements.
- [x] Reassess the resulting pre-handshake dependencies; obtain approval for
      additional bounded prerequisite blocks or an explicit milestone-scope
      change before extending hardware beyond this tranche.
- [x] Complete individual repair reviews and independent block review, retain
      authored BIOS-free regression coverage, and run the complete optimized
      and AddressSanitizer checks for the bounded prerequisite tranche.

### IOP Platform Initialization: Bounded Evidence

- [x] Compare approved local PCSX2, DobieStation, and Play handling of the
      observed `0x1f801020` access and directly shared ownership with
      `0x1f801010`; distinguish supported facts, disagreements, and evidence gaps.
- [x] Propose the smallest defensible access/reset/readback/timing contract
      and owner boundary; assess persistence, trace, hot-path, and BIOS-free
      validation effects without implementing hardware.
- [x] Propose a finite prerequisite repair block and obtain explicit
      implementation approval, or revise milestone scope; leave unsupported
      accesses unchanged until that decision.

### IOP Platform Initialization: Two-Word Configuration Repair

- [x] Implement the approved exact-word `0x1f801020` profile alongside preserved
      `0x1f801010` behavior under one named IOPBus state, including reset, hashing,
      versioned atomic persistence, authored red/green regressions, and individual review.
- [x] Re-run bounded BIOS discovery with repeated traced/untraced comparisons;
      classify the next responsible stop or wait without extending hardware or
      claiming a handshake from configuration progress alone.
- [x] Reconcile provisional policies and the dependency record, complete independent
      block review, and run complete optimized and AddressSanitizer checks.

### IOP Platform Initialization: Follow-Up Scope

- [x] Resolve the scope decision for the observed `0x1f80101c` initialization
      store; obtain approval for bounded evidence/repair work or an explicit
      milestone-scope change before extending hardware.

### IOP Platform Initialization: `0x1f80101c` Evidence

- [x] Identify the observed address and compare manual evidence, approved local
      references, and public register documentation; distinguish PS2 applicability,
      supported behavior, disagreements, and gaps.
- [x] Propose the smallest defensible access/reset/readback/timing contract and
      owner boundary; assess persistence, trace, hot-path, and BIOS-free validation
      effects without implementing hardware.
- [x] Propose a finite prerequisite repair block and obtain explicit
      implementation approval, or revise milestone scope; leave unsupported
      accesses unchanged until that decision.

### IOP Platform Initialization: PIO-Delay Configuration Repair

- [x] Implement the approved exact-word `0x1f80101c` profile under existing named
      `IOPBus` state, preserving both prior words; include schema-47 atomic
      persistence/hash, authored red/green coverage, focused integration, and individual review.
- [x] Re-run bounded BIOS discovery with repeated traced/untraced comparisons;
      classify the next responsible stop or wait without extending hardware or
      claiming a handshake from configuration progress alone.
- [x] Reconcile provisional policies and the dependency record, complete independent
      block review, and run complete optimized and AddressSanitizer checks.

### IOP Platform Initialization: `0x1f801004` Scope

- [x] Resolve the scope decision for the observed `0x1f801004` initialization
      store; obtain approval for grouped configuration-family evidence work or an explicit
      milestone-scope change before extending hardware.

### IOP Platform Initialization: Bus-Configuration Family Evidence

- [x] Inventory the related configuration registers together using manuals and
      approved secondary sources; distinguish PS2 applicability, shared behavior,
      outliers, disagreements, and evidence gaps.
- [x] Propose coherent access/reset/readback and decode/timing groups, existing
      owner boundaries, persistence/trace effects, and authored regression needs;
      preserve already implemented policies and unsupported accesses.
- [x] Propose finite grouped repair batches and obtain explicit implementation
      approval, or revise milestone scope, before extending hardware.

### IOP Platform Initialization: Grouped SSBUS Configuration Repair

- [x] Extend the existing `IOPBus` configuration image to all 18 identified
      words (15 additions), with retention-only base/delay policies, schema-48
      atomic persistence/hash, authored red/green coverage, focused integration,
      and individual independent review.
- [x] Re-run bounded BIOS discovery with repeated traced/untraced comparisons;
      classify the next responsible dependency without extending hardware or
      claiming a handshake from configuration progress alone.
- [x] Reconcile provisional policies and the dependency record, complete
      independent block review, and run complete optimized and AddressSanitizer
      checks.

### IOP Platform Initialization: `0x1f802070` Scope

- [x] Resolve the scope decision for the observed `0x1f802070` initialization
      store; obtain approval for bounded evidence/repair work or an explicit
      milestone-scope change before extending hardware.

### IOP Platform Initialization: `0x1f802070` Family Evidence

- [x] Identify the observed word and evidence-backed related controls using
      local manuals and approved secondary sources; separate PS2 applicability,
      shared behavior, distinct owners, disagreements, and evidence gaps.
- [x] Propose coherent behavior/owner groups and finite repair batches, including
      access, reset, persistence, trace, and authored regression requirements;
      leave unsupported accesses and existing policies unchanged.
- [x] Obtain explicit implementation approval or revise milestone scope before
      extending hardware; preserve the pending grouped-SSBUS closure gate.

### IOP Platform Initialization: Diagnostic Endpoint Repair

- [x] After grouped-SSBUS closure, implement stateless POST2 byte/word and POST3
      byte write-only policies with authored bus/core/trace/lifecycle and
      save-state regression coverage and individual correctness review.
- [x] Re-run bounded BIOS discovery with repeated traced/untraced comparisons;
      classify the next dependency without extending the approved endpoints or
      mistaking diagnostic output for the guest handshake.
- [x] Reconcile provisional policies and the dependency record, complete
      independent block review, and run complete optimized and AddressSanitizer
      checks.

### IOP Platform Initialization: High-Address Control Evidence

- [x] Obtain explicit bounded research approval for observed `0xfffe0140`
      and possible `0xfffe0144` relative; retain current unsupported behavior.
- [x] Establish evidence-backed identity and owner groups using local manuals
      and approved secondary sources; propose finite access/state/validation scope.
- [x] Obtain explicit implementation approval or revise milestone scope before
      extending hardware; preserve the unchanged guest-handshake target.

### IOP Platform Initialization: High-Address Control Repair

- [x] Implement stateless exact-base full-word writes for `0xfffe0140` and
      `0xfffe0144`, with authored access/state/trace/lifecycle regressions and
      individual correctness review; retain unsupported reads and mapping effects.
- [x] Re-run bounded BIOS discovery with repeated traced/untraced comparisons;
      classify the next dependency without extending hardware or mistaking
      initialization progress for the guest handshake.
- [x] Reconcile provisional policies and the dependency record, complete
      independent block review, and run complete optimized and AddressSanitizer
      checks.

### IOP CP0 Initialization: Register-7 Scope

- [x] After high-address repair closure, resolve the scope decision for the
      observed `MTC0 r0,$7` initialization transfer; obtain approval for bounded
      evidence/repair work or an explicit milestone-scope change before extending
      the selected CP0 profile.

### IOP CP0 Initialization: Register-7 Family Evidence

- [x] Establish register-7 identity and directly related debug-register groups
      using local manuals and approved current secondary sources; distinguish
      PS2 applicability, disagreements, and evidence gaps.
- [x] Propose finite behavior/owner and repair groups covering transfer access,
      reset, exceptions, persistence, trace, and authored validation requirements.
- [x] Obtain explicit implementation approval or revise milestone scope before
      extending CP0; preserve the unchanged guest-handshake target.

### IOP CP0 Initialization: Minimal DCIC Initialization Repair

- [x] Admit correctly decoded zero-valued `MTC0` writes to `$7` without state
      or debug/cache effects; retain privilege checks and unsupported accesses,
      with authored red/green, lifecycle/persistence coverage and individual review.
- [x] Re-run bounded BIOS discovery with repeated traced/untraced comparisons;
      classify the next originating dependency without extending hardware or
      claiming the guest handshake from initialization progress.
- [x] Reconcile provisional policies and the dependency record, complete
      independent block review, and run complete optimized and AddressSanitizer
      checks.

### IOP CP0 Initialization: Debug Address/Mask Scope

- [x] Assess the observed `MTC0 r0,$3` and related named debug address/mask
      group using retained family evidence; obtain an explicit finite repair
      approval or milestone-scope decision before extending the CP0 profile.

### IOP CP0 Initialization: Debug Address/Mask Initialization Repair

- [x] Admit correctly decoded zero-valued `MTC0` writes to `$3/$5/$9/$11`
      without state or debug/cache effects; preserve unsupported reads/nonzero
      writes, with authored red/green, lifecycle coverage and individual review.
- [x] Re-run bounded BIOS discovery with repeated traced/untraced comparisons;
      classify the next originating dependency without extending hardware or
      claiming the guest handshake from initialization progress.
- [x] Reconcile provisional policies and the dependency record, complete
      independent block review, and run complete optimized and AddressSanitizer
      checks.

### IOP CP0 Initialization: Selector-6 Scope

- [x] After debug address/mask repair closure, resolve the observed
      `MTC0 r0,$6` dependency through an explicitly approved bounded evidence
      scope or milestone-scope decision before extending the CP0 profile.

### Bounded BIOS Prerequisite Discovery Loop

- [x] Establish and implement the bounded selector-6 initialization contract,
      preserving unsupported accesses and authored CPU/lifecycle/trace coverage.
- [x] Repeat first-failure discovery, manual-first research, bounded repair and
      BIOS rediscovery toward the guest handshake; retain explicit provisional
      policies and stop rather than fabricate readiness or device completion.
- [x] Run focused regressions per implementation and complete optimized and
      AddressSanitizer checks every five implementations; retain immediate
      broader validation for ownership/lifetime or cross-cutting persistence
      changes and final handshake conformance.

### EE Timers: BIOS Startup Prerequisite

- [x] Add one fixed-capacity owner for all four EE timers, with documented
      count/mode/compare/hold registers, strict word admission, reserved-bit
      handling and authored register/flag regressions.
- [x] Implement bus-clock divisors, H-BLANK counting, gate modes, compare/
      overflow requests and SBUS holds, with focused edge/phase/rearm tests.
- [x] Integrate EE MMIO, display-clock ordering, INTC, reset, canonical
      hashing and versioned save/load; validate nonzero-phase continuation,
      atomic rejection and the complete optimized/AddressSanitizer checks.
- [x] Complete independent timer correctness review and bounded real-BIOS
      rediscovery; retain the actual next dependency without claiming handshake
      success from timer initialization.

### IOP Auxiliary DMA Control: BIOS Startup Prerequisite

- [x] Admit the observed auxiliary DMA word at `0x1f801578` through the
      common IOP DMA owner, with isolated provisional retention and strict
      word access; do not infer global enables or unrelated register behavior.
- [x] Preserve its reset value, canonical identity and transactional save/load,
      with focused CPU/bus and persistence regressions and complete checks.
- [x] Complete independent review and bounded BIOS rediscovery, retaining
      the earliest originating dependency rather than later exception fallout.

### IOP Cache/BIU Control: Readback Scope

- [x] Resolve the observed full-word read of `0xfffe0130` against the existing
      write-only policy; select a bounded state/readback and owner contract
      before implementing cache effects or extending persistence.

### IOP Cache/BIU Control: Bounded Readback Repair

- [x] Retain exact full-word initialization writes and expose word readback
      with zero reset at the existing bus owner; preserve access rejection,
      CPU protection and cache behavior with authored red/green regressions.
- [x] Include canonical hashing and versioned transactional persistence;
      validate lifecycle, load-delay continuation and observational traces,
      and run complete optimized and AddressSanitizer checks.
- [x] Complete independent correctness review and bounded BIOS rediscovery;
      record the earliest originating dependency without claiming a handshake.

### EE Conditional Move: BIOS Startup Prerequisite

- [x] Audit the observed `MOVN` encoding and documented conditional-move
      semantics against existing EE decode, effects and issue ownership;
      define a bounded repair with authored CPU and integration regressions.
- [x] Implement and independently review the evidenced repair, then repeat
      bounded BIOS rediscovery and retain the next originating dependency.

### EE Serial-Port Initialization: Scope

- [x] Resolve the observed word store of `0x20` to `0x1000f100` against
      local manuals and approved secondary evidence; choose bounded access,
      effects and lifecycle ownership before implementing serial behavior.

### EE Serial-Port Initialization: Bounded LCR Repair

- [x] Admit only exact full-word writes of `0x20` without retained state
      or UART effects; preserve all other access rejection with authored
      CPU, merge-store, lifecycle and observational trace regressions.
- [x] Validate and independently review the bounded repair, then repeat
      unchanged-budget BIOS rediscovery and retain the next dependency.

### EE Serial-Port Interrupt Initialization: Scope

- [x] Resolve the observed zero word store to `0x1000f120` against
      local manuals and approved secondary evidence; define bounded access,
      effects and ownership before extending serial behavior.

### EE Serial-Port Interrupt Initialization: Bounded IER Repair

- [x] Admit only exact full-word zero writes without retained state or
      UART effects; share the LCR access, guest, merge and trace regression
      matrix while preserving endpoint-specific values and rejection.
- [x] Validate and independently review the bounded repair, then repeat
      unchanged-budget BIOS rediscovery and retain the next dependency.

### EE Serial-Port FIFO Initialization: Scope

- [x] Resolve the observed word store of `0x7` to `0x1000f140` against
      local manuals and approved secondary evidence; define FIFO/reset
      effects and ownership before extending serial behavior.

### EE Serial-Port FIFO Initialization: Bounded FCR Repair

- [x] Admit only exact full-word writes of `0x7` without retained state
      or FIFO effects; extend shared serial access, guest, merge and trace
      regressions while retaining rejection of zero and other values.
- [x] Validate and independently review the bounded repair, then repeat
      unchanged-budget BIOS rediscovery and retain the next dependency.

### EE Serial-Port FIFO Reset Release: Scope

- [x] Resolve the subsequent observed zero word store to `0x1000f140`
      against the established reset-sequence evidence; define bounded
      access and effects before extending the exact-`0x7` policy.

### EE Serial-Port FIFO Reset Release: Bounded Repair

- [x] Admit exact full-word zero alongside `0x7` independently, without
      retained state or FIFO effects; extend the shared serial regression
      matrix while preserving all other access rejection.
- [x] Independently review the repair, run the due complete optimized and
      AddressSanitizer checks, and repeat unchanged-budget BIOS rediscovery
      to retain the next originating dependency.

### EE Serial-Port Baud-Rate Initialization: Scope

- [x] Resolve the observed zero word store to `0x1000f150` against
      local manuals and approved secondary evidence; define bounded
      clock/divisor effects and ownership before extending serial admission.

### EE Serial-Port Baud-Rate Initialization: Bounded BGR Repair

- [x] Admit only exact full-word zero writes without retained state
      or baud/traffic effects; extend shared serial access, guest, merge
      and branch-delay trace regressions while retaining other rejection.
- [x] Validate and independently review the bounded repair, then repeat
      unchanged-budget BIOS rediscovery and retain the next dependency.

### EE Serial-Port Status: Scope

- [x] Resolve the observed word read at `0x1000f130` against local
      manuals and approved evidence; define status/readiness ownership
      and reset/traffic semantics without fabricated ready responses.

### EE Serial-Port Status: Quiescent-Only Read

- [x] Admit only aligned word reads of `0x1000f130` as provisional
      quiescent zero status through EEBus; preserve rejection of ISR writes,
      other widths and all serial traffic, without new state or timing.
- [x] Cover host aliases, guest loads and merge paths, rejection boundaries,
      reset and observational traces; independently review and repeat
      unchanged-budget BIOS rediscovery to retain the next dependency.

### EE Serial-Port Transmit: Scope

- [x] Resolve the observed byte store to `0x1000f180` against local
      manuals and approved evidence; reopen quiescent-only status ownership
      and define transmit acceptance, timing and reset before admitting traffic.

### EE Serial-Port Transmit: Single-Slot Provisional Model

- [x] Implement one serial owner with byte acceptance, occupancy-derived
      status, fixed-profile timed completion and active FCR reset; pin exact
      timing, rejection and cancellation boundaries with authored tests
      and independently review the state machine.
- [x] Route existing serial MMIO through that owner, integrate master-clock
      completion observations, and include pending transmission/reset state
      in canonical hashing, transactional save/load and system reset;
      independently review and run immediate optimized/ASan gates.
- [x] Validate guest polling, access boundaries, completion traces and restored
      continuations; independently review the completed block and repeat
      unchanged-budget BIOS rediscovery, rerunning broader gates after repairs.

### Late-Failure Startup Diagnostics

- [x] Define an opt-in bounded first-failure recorder independent of prefix
      truncation, preserving hardware behavior and the existing BIOS limits.
- [x] Add bounded preceding/failure-cycle context with shared C++ attribution,
      CLI export and maintained inspector support; cover late failures,
      capture limits, lifecycle and trace-mode equivalence with authored tests.
- [x] Independently review, run ownership-related optimized/ASan gates and
      repeat unchanged-budget discovery to attribute the next dependency.

### EE Startup Store at Physical 0x1000f410

- [x] Investigate the endpoint identity and select a bounded access contract from the
      originating zero-valued word store; select ownership and research scope
      before admitting new behavior.
- [x] Implement the selected contract through its existing owner with
      red/green access-boundary and guest-fault coverage; preserve unsupported
      behavior outside the approved scope.
- [x] Independently review and repeat unchanged-budget BIOS discovery,
      applying persistence and broader validation gates when required.

### EE DMA Hold Controls

- [x] Investigate the originating word load and establish its raw access
      contract, evidence limits and state owner before selecting a bounded
      implementation scope.
- [x] Implement paired hold-state/control MMIO through the common EE DMAC
      owner and all implemented channel gates, with reset, canonical
      persistence and startup-summary coverage; independently review and run
      immediate optimized/ASan persistence gates.
- [x] Validate guest hold/readback/resume and abort boundaries, pending
      channel/stream continuations, restored execution and trace-mode
      equivalence; independently review the completed hardware capability.
- [x] Repeat unchanged-budget BIOS discovery to verify the originating
      load and attribute the next dependency without fabricating a handshake.

### EE Startup Store at Physical 0x1000f480

- [x] Investigate the originating word store and document identity/evidence
      limits; select the bounded access contract, state owner and owning
      milestone before implementation.
- [x] Implement the selected exact-word stateless admission through EEBus
      with red/green access-mask, guest-fault and trace-mode coverage.
- [x] Independently review and repeat unchanged-budget BIOS discovery,
      applying persistence and broader validation gates when required.

### EE Startup Store at Physical 0x1000f490

- [x] Investigate the originating word store and related register-family
      hypothesis; document evidence limits and select the bounded access
      contract, owner and milestone before implementation.
- [x] Implement the selected exact-word stateless admission through EEBus
      with red/green access-mask, guest-fault and trace-mode coverage.
- [x] Independently review and repeat unchanged-budget BIOS discovery,
      applying persistence and broader validation gates when required.

### EE RDRAM Initialization Control: Bounded Protocol

- [x] Investigate the originating word read and related control protocol;
      establish evidence limits, read/reset/effect contract, owner and
      milestone before selecting a bounded implementation policy.
- [x] Implement a provisional paired command/data owner with deterministic
      reset, synchronous completion, explicit supported operations and
      bounded retail responses; test rejected-operation nonmutation.
- [x] Integrate exact-word MMIO and full-mask merge stores, reset and
      canonical transactional persistence; independently review and run
      immediate complete optimized/ASan lifecycle gates.
- [x] Validate guest polling, command/data ordering, enumeration/reinitialization,
      restored continuation and trace-mode equivalence; independently review
      the completed capability.
- [x] Repeat unchanged-budget BIOS discovery to verify the originating
      read and attribute the next dependency without fabricating a handshake.

### EE RDRAM Initialization: SA0x4d Write Dependency

- [x] Investigate the observed `0x804d0060` command with zero input; establish
      evidence limits and the admission/effect contract before extending policy.
- [x] Implement the selected bounded contract through the existing paired
      owner; validate rejection nonmutation and persistence implications.
- [x] Validate guest ordering, restored continuation and trace equivalence;
      independently review the extension and apply required broader gates.
- [x] Repeat unchanged-budget BIOS discovery to verify the command and
      attribute the next dependency without fabricating a handshake.

### EE CP0 Count/Compare: Startup Calibration Dependency

- [x] Trace the zero-valued parameter tested before the originating guest
      breakpoint; distinguish CPU/data derivation from missing hardware behavior
      and establish the responsible owner and evidence before selecting a repair.
- [x] Implement active-clock Count advancement and Compare timer interrupt
      semantics through `EECore`; validate clock/write ordering, interrupt
      integration and persistence with focused red/green and required broader gates.
- [x] Validate authored timer/calibration guests and restored/trace-mode
      continuations; independently review the completed capability.
- [x] Repeat unchanged-budget BIOS discovery to verify the originating
      path and attribute the next dependency without bypassing the guest guard.

### EE Serial-Port Nonzero Baud Rate: Calibration Dependency

- [x] Investigate the calibration-derived BGR word write of `0x1d`; establish
      manual evidence, encoding/clock effects, owner and persistence contract
      before selecting a bounded extension to the existing serial profile.
- [x] Implement exact BGR `0`/`0x1d` profiles through the serial owner, preserving
      active-frame deadlines and baud selection across FIFO reset; validate
      access rejection, timing and canonical persistence with required gates.
- [x] Validate authored calibration/transmit and restored/trace continuations;
      independently review, then repeat unchanged-budget discovery to verify
      baud progress and bound remaining guard/dependency evidence.

### Bounded Post-Baud BIOS Observation

- [x] Establish bounded observation beyond calibrated serial polling; revisit
      the former guard and attribute the next normal-path failure or wait,
      keeping budget/trace limits distinct from handshake evidence.

### IOP DPCR3 Startup Initialization

- [x] Investigate the normal-path word store of `0x777` to `0x1f8015f0`;
      establish manual evidence, identity/effects and the responsible owner
      before selecting a bounded hardware contract.
- [x] Implement exact-word `0x777` initialization through the common IOP DMA
      owner; validate access rejection and unchanged DMA/lifecycle state with
      focused red/green coverage and required gates.
- [x] Validate guest and affected restored/trace continuations; independently
      review, then repeat discovery under an explicitly selected budget to
      verify this path and attribute the next prerequisite.

### BIOS Discovery Checkpoints and Compact Evidence

- [x] Add whole-machine checkpoint/resume with absolute cycle ceilings and
      identity-checked reuse; stream capped trace reports directly to gzip
      while retaining bounded failure evidence and cold-start workflows.
- [x] Prove cold/restored canonical-state and traced/untraced equivalence;
      independently review, run required checks, and document checkpoint
      invalidation and evidence retention.

### IOP DMA Channel-0 Startup Initialization

- [x] Investigate the zero word store to `0x1f801080`; establish evidence
      and select a bounded channel-0 MADR/BCR/CHCR initialization contract
      through the existing IOP DMA owner.
- [x] Implement the selected channel-register contract with focused red/green
      access and state coverage; preserve existing SIF, interrupt, scheduling
      and persistence contracts except for explicitly scoped changes.
- [x] Validate guest and affected restored/trace continuations; independently
      review, then repeat discovery under an explicitly selected budget to
      verify this path and attribute the next prerequisite.

### IOP DMA Channel-1 Startup Initialization

- [x] Investigate the zero word store to `0x1f801090`; establish evidence
      and select a bounded channel-1 MADR/BCR/CHCR zero-initialization
      contract through the existing IOP DMA owner.
- [x] Implement the selected contract with focused red/green access and state
      coverage; preserve existing DMA, interrupt and lifecycle boundaries
      except for explicitly scoped changes.
- [x] Validate guest and affected restored/trace continuations; independently
      review, then repeat discovery under an explicitly selected budget to
      verify this path and attribute the next prerequisite.

### IOP DMA Channel-2 Startup Initialization

- [x] Investigate the zero word store to `0x1f8010a0`; establish evidence
      and select a bounded channel-2 MADR/BCR/CHCR zero-initialization
      contract through the existing IOP DMA owner.
- [x] Implement the selected contract with focused red/green access and state
      coverage; preserve existing DMA, interrupt and lifecycle boundaries
      except for explicitly scoped changes.
- [x] Validate guest and affected restored/trace continuations; independently
      review, then repeat discovery under an explicitly selected budget to
      verify this path and attribute the next prerequisite.

### IOP DMA Channel-3 Startup Initialization

- [x] Investigate the zero word store to `0x1f8010b0`; establish evidence
      and select a bounded channel-3 MADR/BCR/CHCR zero-initialization
      contract through the existing IOP DMA owner.
- [x] Implement the selected contract with focused red/green access and state
      coverage; preserve existing DMA, interrupt and lifecycle boundaries
      except for explicitly scoped changes.
- [x] Validate guest and affected restored/trace continuations; independently
      review, then repeat discovery under an explicitly selected budget to
      verify this path and attribute the next prerequisite.

### Bounded IOP DMA Initialization Planning

- [x] Inventory the finite remaining startup-initialization family from local
      manuals and retained approved evidence; distinguish shared contracts,
      existing owners, unsupported effects and evidence/approval gaps.
- [x] Group justified capabilities into independently testable work items;
      define regression and BIOS-discovery checkpoints, stop boundaries and
      their relationship to the exact guest-produced EE-IOP handshake target.

### Remaining IOP DMA Channel-Register Initialization

- [x] Select the bounded contract for the 21 SDK-initialized MADR/BCR/CHCR
      words of channels 4-8 and 11-12; distinguish prospective coverage from
      BIOS-observed writes and preserve active SIF ownership.
- [x] Implement the selected exact-word profile through the common DMA owner
      with focused red/green access, guest-store and no-effects coverage;
      keep readback, active transfers and supplemental words out of scope.
- [x] Validate restored/trace and live-SIF continuations; independently review
      the capability, run the due complete checks, then use separately
      authorized discovery to verify observed writes and the next boundary.

### Supplemental IOP DMA Initialization Scope

- [x] Assess channel-4 TADR and the three SDK supplemental words as one
      candidate batch; select justified per-endpoint contracts, distinguishing
      BIOS-observed demand from prospective coverage and preserving SIF ownership.
- [x] Implement the four selected zero-only endpoints with focused red/green,
      guest, no-effects and affected lifecycle coverage; preserve existing
      channel-9 TADR and shared DMA controls.
- [x] Independently review and validate the selected capability, then use
      separately authorized discovery to attribute the next dependency;
      reopen scope for readback, nonzero controls or active effects.

### IOP CD/DVD Startup Device Boundary Scope

- [x] Identify the `0x1f402005` readiness load and related command family;
      route functional CD/DVD behavior to Milestone 10 before implementation.

### EE-Side IOP RAM Aperture

- [x] Establish the normal-path `0x1c0003f0` load and select a bounded
      provisional aperture after manual-first and approved-secondary research.
- [x] Route 8/16/32/64/128-bit CPU data accesses over `0x1c000000–0x1c1fffff`
      to the existing IOP RAM owner; exclude mirrors, fetches, cache fills
      and EE DMA without new storage or fabricated peer responses.
- [x] Validate guest accesses, partial stores, two-way visibility, reset and
      canonical restore; independently review integration, run full gates,
      and verify bounded BIOS progress.

### Real BIOS Handshake Prerequisites

These prerequisite blocks continued discovery after the selected Milestone 10
CD/DVD startup prerequisite and EE MCH-window implementation, leading to the
real BIOS handshake conformance block below.

- [x] Restore and independently review the existing exact f500 stateless
      word-write contract removed by MCH expansion; verify BIOS progress and
      attribute the next originating dependency in the bounded matrix.
- [x] Investigate the observed f410 bit31 polling dependency and select any
      justified special-register contract; distinguish hardware effects from
      provisional backing before continuing bounded handshake discovery.
- [x] Resolve the IOP unread multiply/divide pair-overwrite admission exposed
      by BIOS execution; preserve documented HI/LO read hazards and validate
      the complete pair-writer family before resuming handshake observation.
- [x] Investigate the observed IOP SIF `+0x60` load and select its exact
      access/reset/side-effect contract through the existing shared owner
      before continuing real-handshake discovery.

### IOP Cache-Control and Code Replacement

- [x] Attribute the post-SIF stale-instruction failure and identify the
      instruction/data flush sequences using offline RAM/cache evidence.
- [x] Select the coherent IOP cache-control contract: bank selection, tag
      granularity, isolation/swap interaction, enable behavior and supported
      access shapes; distinguish PS2 evidence from provisional policy.
- [x] Implement the selected instruction/data control family through existing
      owners, with red/green code-replacement and indexed-invalidation coverage.
- [x] Resolve the early-reset combined BCC `0x0cc4` maintenance sequence; select
      its bank effects and partial-store admission before implementing cold-start
      compatibility through the existing cache owners.
- [x] Validate reset, restore, unaffected cache paths and guest flush sequences;
      independently review, run required full gates, and verify progress with
      separately authorized BIOS discovery.

### EE MCH Halfword Reads

- [x] Research and select the window-wide halfword-read contract, including
      lane selection, f410 readback and RICM/DRD access exclusions; distinguish
      documented behavior from provisional policy.
- [x] Extend generic MCH backing reads through the existing owner without
      admitting other widths, partial writes or protocol side effects.
- [x] Validate alignment, boundaries, guest signed/unsigned loads, trace and
      restored continuation; independently review the extension.

### EE `0x1a000008` Startup Boundary

- [x] Attribute the halfword store to its guest construction and physical-bus
      rejection; search local manuals for the endpoint and access contract.
- [x] Compare separately approved secondary evidence and its identity/semantic
      gaps; select a bounded provisional contract and owner before defining
      implementation and validation work.
- [x] Implement the selected exact halfword-backed endpoint through EEBus;
      preserve unsupported widths, aliases, neighbors and device effects.
- [x] Validate guest trace, reset/hash and versioned atomic persistence;
      independently review and pass complete optimized/AddressSanitizer checks.
- [x] With separate BIOS-run authorization, verify the originating store
      using a fresh cold scout and attribute the next dependency.

### EE `0x1a000004` Startup Boundary

- [x] Attribute the guest halfword store, research its raw contract and select
      bounded independent backing through the existing EEBus owner.
- [x] Implement exact halfword read/write backing without widening other
      access routes or introducing command, status or cross-register effects.
- [x] Validate offset04/08 independence, guest trace, reset/hash and versioned
      atomic persistence; independently review and pass optimized/ASan gates.
- [x] With separate BIOS-run authorization, verify the originating store
      using a fresh cold scout and attribute the next dependency.

### GIF Control Reset Startup Boundary

- [x] Define GIF reset's documented core and provisional in-flight policy;
      preserve external VIF/VU/DMAC and GS state, and keep pause unsupported.
- [x] Route exact word reset/no-op controls through GIFRegisters; reset packet
      and arbiter state without stale VIF masks or producer cancellation.
- [x] Validate active/interrupted packets, FIFO discard, access rejection,
      observer preservation and restored continuation; independently review
      and pass complete optimized/AddressSanitizer checks.
- [x] With separate BIOS-run authorization, verify the originating reset write
      using a fresh cold scout and attribute the next dependency.

### EE Byte Read at Physical `0x1f80146e`

- [x] Attribute the guest byte read and existing owner boundary; search local
      manuals for its identity, access shape and readback/effect contract.
- [x] Compare separately approved secondary evidence if needed; select a bounded
      contract and owner before defining implementation and validation work.
- [x] Implement exact stateless EE byte readback of zero for the selected
      absent-DEV9 profile; preserve other access and device rejections.
- [x] Validate bus boundaries, guest loads and trace-mode agreement;
      independently review the bounded extension.
- [x] With separate BIOS-run authorization, verify the originating byte read
      using a fresh cold scout and attribute the next dependency.

### EE `0x1a000006` Startup Boundary

- [x] Attribute the guest halfword store and existing owner boundary; search
      local manuals for its identity, access and effect contract.
- [x] Compare approved secondary evidence if needed; select the bounded
      contract and ownership before defining implementation and validation work.
- [x] Admit only the observed exact halfword write of 3, stateless and
      write-only; preserve neighboring BA0 and unsupported-access behavior.
- [x] Validate bus boundaries, guest stores and trace-mode agreement;
      independently review the bounded extension.
- [x] With separate BIOS-run authorization, verify the originating halfword
      store using a fresh cold scout and attribute the next dependency.

### EE `0x1a000002` Startup Boundary

- [x] Attribute the guest halfword store and existing owner boundary; search
      local manuals for its identity, access and effect contract.
- [x] Compare approved coupled-family evidence if needed; select the bounded
      contract and any shared BA0 ownership before defining implementation work.
- [x] Consolidate BA0 startup state and admissions in one owner; retain only
      the observed offset02 write of `0x42`, without readback or commands.
- [x] Validate reset, hashing, atomic persistence and guest access boundaries;
      pass complete optimized/ASan checks and independent correctness review.
- [x] Under the active BIOS-run authorization, verify the originating halfword
      store using a fresh cold scout and attribute the next dependency.

### EE BA0 Transfer/Control Boundary

- [x] Attribute the offset10 store and inspect the related BA0 register family
      in local manuals, then approved secondary evidence; distinguish passive
      backing from command/status/data-transfer behavior.
- [x] Select the coherent family contract and existing-owner extension;
      pause before active protocol behavior rather than admitting data writes
      through an isolated latch or generic MMIO fallback.
- [x] Retain/read back the eight aligned transfer-staging halfwords at
      offsets10-1e in the existing BA0 owner; preserve active-register rejection.
- [x] Validate access boundaries, reset, guest trace/restore and transactional
      persistence; pass complete optimized/ASan checks and independent review.
- [x] Use a fresh authorized cold scout to verify the originating staging write
      and identify the next dependency without inventing command/status effects.

### EE BA0 Command Execution Boundary

- [x] Attribute the command00 write of `0x81` and its control/staging/status
      dependencies; establish the bounded active-protocol evidence.
- [x] Add bounded offline EE RAM inspection with provenance and input validation;
      inspect the retained guest routine without executing or restoring it.
- [x] Select a bounded selector-write contract with provisional synchronous
      completion; defer device-read transactions without fabricated replies.

### EE BA0 Selector-Write Transactions

- [x] Implement exact command81/control42 byte-selector writes and status06
      acknowledgement/readback in the existing BA0 owner, including reset,
      canonical hashing and versioned transactional persistence; preserve
      read-command rejection and validate atomic rejection and pure polling.
- [x] Pass complete optimized/ASan checks and independent review, then use a
      fresh authorized cold scout to verify the selector-write boundary and
      attribute the next unsupported access.

### EE BA0 Device-Read Contract

- [x] Select an explicit provisional0x7e-only response profile and bounded
      read-transfer contract; retain rejection of all other device reads.

### EE BA0 Bounded Device Reads

- [x] Implement selected control43/command81 reads of latched0x7e as0x1c
      in the existing BA0 owner, including result lifetime, reset/hash and
      versioned transactional restore; reject unknown selectors atomically.
- [x] Validate access boundaries, pure polling, repeat reads and guest
      trace/restore; obtain independent review and use a fresh authorized cold
      scout to verify the result transfer and attribute the next dependency.

### EE BA0 Additional Device-Response Contract

- [x] Select an evidence-backed or explicitly approved provisional0x7f response
      and transfer effects; keep unsupported reads rejected until contract
      selection is complete.

### EE BA0 Additional Device Response

- [x] Extend the immutable BA0 profile with selected0x7f=0x00, preserving
      latched selection, result lifetime, atomic rejection and complete
      reset/hash/transactional restore behavior.
- [x] Validate both profile entries and unknown selectors through owner/access
      and guest trace/restore tests; obtain independent review and use a fresh
      authorized cold scout to attribute the next dependency.

### EE BA0 Command-82 Contract

- [x] Select an evidence-backed or explicitly approved bounded command82/
      control42 contract for the observed staging, device effects and
      completion lifecycle; retain rejection until contract selection.

### EE BA0 Bounded Device Writes

- [x] Implement bounded command82/control42 register-b1 byte writes in the
      existing owner, including reset, selector/status, canonical hashing
      and versioned transactional persistence; reject other writes atomically.
- [x] Validate access/rejection boundaries and guest trace/restore; pass
      complete optimized/ASan checks and independent review, then use a fresh
      authorized cold scout to verify the write and attribute the next dependency.

### GS Startup Transfer Boundary Contract

- [x] Identify the guest GS transaction behind the startup destination-width
      runtime error and select an evidence-backed bounded contract through
      the existing GS/GIF owners; do not infer valid transfer setup.

### GS Bounded Empty Image Transfers

- [x] Keep zero-width or zero-height PSMCT32 host-to-local/local-to-host
      requests inactive through the existing GS owner; reset progress without
      validating unused buffer width or relaxing other transfer rejection.
- [x] Validate restart, inactive data access and packed A+D PATH3 completion
      with BIOS-free regressions and independent review, then use a fresh
      authorized cold scout to verify progress and attribute the next dependency.

### EE BA0 Register-33 Write Contract

- [x] Attribute the new command82 request and inspect the containing encoder
      register family through local manuals, client evidence and refreshed
      reference emulators; retain the existing shared BA0 owner.
- [x] Select the bounded register-33 write/effect and lifecycle contract before
      implementing another device-write target; preserve rejection meanwhile.

### EE BA0 Bounded Register-33 Writes

- [x] Extend the existing bounded device-write owner with independent
      register-33 byte backing, reset/hash and versioned transactional
      persistence, preserving B1 and unsupported-target/readback rejection.
- [x] Validate access and restored traced/plain guest continuation, pass
      complete optimized/ASan checks and independent review, then verify the
      original write and next dependency through a fresh authorized cold scout.

### EE DMAC Absent Optional Register Slots

- [x] Establish a bounded access policy for absent TADR, ASR and SADR slots
      using the channel-specific manual inventory and local secondary evidence.
- [x] Implement the selected exact-slot policy through shared DMAC decoding,
      preserving real-register behavior and rejection of unrelated accesses.
- [x] Validate access boundaries and unchanged DMA state, obtain independent
      review, then verify progress through a fresh authorized cold scout.

### EE VIF0 DMA Stopped-Channel Registers

- [x] Establish the six-register stopped initialization contract and reuse the
      shared channel-state owner without admitting DMA execution.
- [x] Add exact word access, reset/hash and versioned transactional persistence;
      reject starts and unsupported access/field combinations before mutation.
- [x] Validate state and guest continuation, pass complete optimized/ASan
      checks and independent review, then run a fresh authorized cold scout.

### EE VIF1 DMA Stopped-Direction Programming

- [x] Align stopped CHCR direction programming with the manual and existing
      restore contract while preserving rejection of unsupported DMA starts.
- [x] Validate shared-state and VIF1 guest/restored behavior, retain active
      transfer guards, and obtain independent correctness review.
- [x] Verify the original clear and next dependency through a fresh authorized
      cold scout without adding reverse DMA execution.

### EE IPU DMA Stopped-Channel Registers

- [x] Establish both channels' real-register inventory and shared stopped-state
      policy without admitting IPU DMA execution.
- [x] Add exact word access, reset/hash and versioned transactional persistence
      for channel3 CHCR/MADR/QWC and channel4 CHCR/MADR/QWC/TADR.
- [x] Validate access and restored guest behavior, pass complete optimized/ASan
      checks and independent review, then verify progress with a fresh cold scout.

### EE DMAC Inactive Ring-Buffer Registers

- [x] Establish the RBSR/RBOR pair's fields and inactive programming policy
      within the existing global DMAC owner; keep MFIFO selection unsupported.
- [x] Add exact word access, reset/hash and versioned transactional persistence
      for the independent size-mask and base-address fields.
- [x] Validate access and restored guest behavior, pass complete optimized/ASan
      checks and independent review, then verify progress with a fresh cold scout.

### EE CPU VU Data-Memory Quadword Access

- [x] Establish the documented CPU quadword access and mapping contract for
      both VU data memories through the existing EEBus/VPU owners.
- [x] Add bounded CPU quadword reads/writes without parallel backing,
      preserving VIF/VU coherence and unsupported access-route rejection.
- [x] Validate guest and restored continuation, obtain independent review,
      then verify progress through a fresh authorized cold scout.

### EE CPU VU Micro-Memory Doubleword Access

- [x] Establish both CPU micro-memory mappings, 64-bit access and stopped-unit
      requirements through the existing VPU instruction-memory owner.
- [x] Add bounded CPU doubleword reads/writes with VIF MPG coherence,
      preserving rejection of unsupported widths, aliases and routes.
- [x] Validate guest and restored continuation, obtain independent review,
      then verify progress through a fresh authorized cold scout.

### VIF FBRST Reset Contract

- [x] Establish both VIF units' reset scope and shared-owner boundaries from
      local manuals and secondary evidence.
- [x] Select the bounded active-reset contract before implementation,
      separately from force-break, stop and stall-cancel.

### Bounded VIF FBRST Reset

- [x] Implement the selected reset through the existing VIF and GIF owners,
      preserving unrelated VU, DMA, interrupt-controller and graphics state.
- [x] Validate access, payload/FIFO lifecycle and restored guest continuation,
      obtain independent review, then verify progress with a fresh cold scout.

### Bounded VIF Status Programming

- [x] Establish writable status fields and direction boundaries from local
      manuals, distinguishing VIF0 from VIF1 and excluding reverse transfers.
- [x] Admit supported status writes through the existing VIF owner without
      clearing live flags or enabling DMA/graphics readback.
- [x] Validate register and guest integration, obtain independent review,
      then verify progress through a fresh authorized cold scout.

### Bounded EE f510 Startup Writes

- [x] Establish the observed f510 store contract and distinguish it from
      the neighboring DMAC hold registers through manual-first research.
- [x] Admit exact word writes without inventing readback or DMA effects,
      preserving the existing f500 contract and rejecting other routes.
- [x] Validate state preservation and guest continuation, obtain independent
      review, then verify progress through a fresh authorized cold scout.

### EE CPU VU Micro-Memory Quadword Access

- [x] Establish CPU quadword access and instruction ordering for both exact
      micro-memory windows, retaining stopped-unit admission boundaries.
- [x] Extend the existing CPU/VPU memory path without parallel backing,
      preserving doubleword/MPG coherence and unsupported-route rejection.
- [x] Validate guest and restored continuation, obtain independent review,
      then verify progress through a fresh authorized cold scout.

### VIF MARK Register Programming

- [x] Establish both units' MARK read/write fields and detection-flag
      semantics through manual-first research and the existing VIF owner.
- [x] Add exact CPU word access without parallel state, preserving MARK
      command coherence and rejecting unsupported widths and routes.
- [x] Validate guest and restored continuation, obtain independent review,
      then verify progress through a fresh authorized cold scout.

### VIF ERR All-Unmasked Programming

- [x] Establish both units' ERR fields, access and reset semantics from local
      manuals, separating mask programming from active error handling.
- [x] Add bounded exact CPU word programming through the existing VIF owner,
      with explicit unsupported-field, width and route boundaries.
- [x] Validate guest and persistence integration, complete required full gates
      and independent review, then verify progress with a fresh cold scout.

### IPU Control/Reset Contract

- [x] Establish IPU_CTRL fields, reset scope and adjacent register boundaries
      from local manuals and secondary evidence, separating reset from commands.
- [x] Select the bounded reset contract and device-state ownership before
      implementation, including DMA, persistence and unsupported-access guards.

### Bounded IPU Control Reset

- [x] Implement the selected exact CPU control/reset contract through one IPU
      owner without enabling command decoding, DMA execution or invented status.
- [x] Validate reset lifecycle and guest/persistence boundaries, obtain
      independent review and required full gates, then run a fresh cold scout.

### IPU BCLR Command Contract

- [x] Establish BCLR bit-position, input-FIFO and completion/IRQ semantics
      through local manuals and secondary evidence, retaining stopped-DMA scope.
- [x] Select bounded command admission, timing and sole-owner state/persistence
      before implementation; keep decoding and FIFO/DMA transport separate.

### Bounded IPU BCLR Command

- [x] Implement the selected command and observable bit-position state through
      the existing IPU owner with exact CPU access and transactional persistence.
- [x] Validate command/reset/IRQ lifecycle and guest boundaries, pass required
      full gates and independent review, then verify progress with a fresh scout.

### IPU CPU Input-FIFO Contract

- [x] Establish CPU quadword input admission, FIFO capacity/status and BP
      interaction through local manuals and secondary evidence.
- [x] Select FIFO ownership, command/DMA boundaries, timing and persistence
      before implementation; keep decoding and active DMA separately scoped.

### Bounded IPU CPU Input FIFO

- [x] Retain exact CPU quadwords through the existing IPU owner with selected
      capacity/status, full-write behavior and BCLR/reset clearing.
- [x] Validate access, backpressure, queued-data persistence and IRQ/peer
      preservation; pass full gates/review and verify with a fresh cold scout.

### IPU SETIQ Command Contract

- [x] Establish table selection, bitstream consumption, FIFO/BP status and
      completion behavior through local manuals and secondary evidence.
- [x] Select bounded command/consumer ownership, starvation/reset policy,
      timing and persistence before implementation; keep decoding/DMA separate.

### Bounded IPU SETIQ Command

- [x] Load the selected IQ table from retained CPU input through the existing
      IPU owner with selected bitstream and command-lifecycle behavior.
- [x] Validate consumption, status, reset/IRQ and queued-data persistence;
      pass required full gates/review and verify progress with a fresh scout.

### IPU SETVQ Command Contract

- [x] Establish CLUT layout, stream-position/consumption and completion behavior
      through local manuals and secondary evidence.
- [x] Select bounded table/reset/timing policy and shared consumer/persistence
      ownership before implementation; keep quantization and DMA separate.

### Bounded IPU SETVQ Command

- [x] Load the selected CLUT from real CPU input through the existing IPU
      stream/command owner with bounded admission and lifecycle behavior.
- [x] Validate table order, consumption, starvation/reset/IRQ and persistence;
      pass required full gates/review and verify progress with a fresh scout.

### IPU SETTH Command Contract

- [x] Establish threshold fields, input independence, reset and completion
      behavior through local manuals and secondary evidence.
- [x] Select bounded threshold/timing policy and existing IPU state ownership
      before implementation; keep color conversion and decoding separate.

### Bounded IPU SETTH Command

- [x] Retain the selected threshold fields through the existing IPU command
      owner without consuming input or enabling conversion.
- [x] Validate exact admission, threshold retention, reset/IRQ and persistence;
      pass required full gates/review and verify progress with a fresh scout.

### EE BFH Startup Contract

- [x] Identify the observed BTAC-flush opcode through local manuals and
      refreshed secondary evidence; select bounded empty-predictor behavior
      through existing CACHE ownership without enabling other BTAC operations.

### Bounded EE BFH Maintenance

- [x] Admit BFH through ordinary CACHE usability, issue and retirement, without
      address translation, I/D-cache mutation or invented predictor effects.
- [x] Validate decoder, cache and guest exception boundaries; independently
      review, pass optimized/ASan gates and verify with fresh bounded BIOS evidence.

### EE Startup Halfword-Read Contract at 0x1f803800

- [x] Research the observed EE halfword load and containing access family;
      select an evidence-backed exact read policy before implementation,
      without generic IOP-register backing.

### Bounded EE Startup Halfword-Read Admission

- [x] Admit only an exact EE halfword zero-read through the existing bus, with
      atomic rejection and focused host/guest access-boundary coverage.
- [x] Review and validate the selected capability, then verify progress with
      fresh authorized BIOS evidence before resuming handshake discovery.

### EE SIF Register60 Initialization Contract

- [x] Compare the observed EE word store with local manuals and secondary
      evidence; select its scope through the existing shared SIF owner,
      without inferring a handshake or generic register-bank semantics.

### Bounded EE SIF Register60 Initialization

- [x] Implement the selected initialization write with exact access/value
      admission, preserving the existing IOP read and other SIF behavior.
- [x] Validate host/guest and state/trace boundaries, independently review,
      and verify progress with fresh authorized BIOS evidence.

### EE SIF2 DMAC Initialization Contract

- [x] Research channel-7 CHCR initialization and its containing register family;
      select an inactive initialization boundary without enabling SIF2 transfers.

### Bounded EE SIF2 DMAC Initialization

- [x] Implement the selected initialization access through the existing DMAC
      owner, with exact value/width admission and no fabricated DMA activity.
- [x] Validate host/guest and state/trace boundaries, independently review,
      and verify progress with fresh authorized BIOS evidence.

### IOP SIF DMA BCR Halfword Contract

- [x] Research the observed channel-10 BCR store and equivalent implemented
      SIF DMA lanes; select widths and lane semantics through existing owners.

### IOP SIF DMA BCR Halfword Access

- [x] Implement the selected lane accesses without parallel register state,
      preserving active-transfer rejection and all other DMA MMIO boundaries.
- [x] Validate guest stores and transport/state integration, independently
      review, and verify progress with fresh authorized BIOS evidence.

### IOP SIF1 CHCR Startup Contract

- [x] Resolve the observed channel-10 control fields and receive mode through
      existing IOP DMA ownership; select any required execution scope.

### IOP SIF1 CHCR Startup Support

- [x] Implement the selected control/mode contract with atomic rejection and
      coherent receiver continuation, persistence and completion behavior.
- [x] Validate focused integration and the required full gates, independently
      review, and verify progress with fresh authorized BIOS evidence.

### EE COP0 Interrupt Control

- [x] Establish the observed opcode and companion interrupt-control contract,
      including privilege, status fields and instruction-ordering boundaries.
- [x] Implement the selected controls through existing EE decode/COP0 ownership,
      with explicit deferred-effect lifecycle where selected, preserving precise
      exceptions, retirement, persistence and pending-interrupt delivery.
- [x] Validate focused decode/system/state integration, independently review,
      and verify progress with fresh authorized BIOS evidence.

### Real BIOS Handshake and Boundary Conformance

- [x] With separate BIOS-run authorization, verify the MCH halfword-read
      extension and classify the resulting failure or cycle-ceiling activity.
- [x] With separate cycle-budget authorization, observe beyond the former
      21M ceiling without bypassing real serial-transmit polling; attribute
      the first normal-path failure in the authorized 50M observation.
- [x] Pin the exact guest-produced request/peer-response checkpoint, including
      flags or packets and CPU PCs; keep the handshake target incomplete until
      its guest behavior is identified.
- [x] Demonstrate that checkpoint through both CPUs' real startup/SIF execution,
      without fabricated readiness, host RPC responses, skipped instructions,
      or generic device stubs.
- [x] Record the first subsequent out-of-scope device dependency with
      CPU/access/wait evidence for Milestone 10 or its owning later milestone,
      without implementing that device here.
- [x] Reproduce the BIOS checkpoint and boundary across bounded repetitions,
      trace modes, and a fresh cold-validated pre-handshake save/load point;
      retain authored BIOS-free regressions for every implemented prerequisite.

### Milestone Conformance and Closure

- [x] Audit the completed authored two-CPU transport/lifecycle matrix for
      startup-prerequisite coverage gaps; add only missing cases and run the
      complete optimized check, preserving existing EE/IOP/graphics workflows.
- [x] Complete the bounded architecture and maintainability review of shared
      ownership, DMA transitions, scheduling, reset, diagnostics, and persistence.
- [x] Reconcile provisional policies and the BIOS dependency record; retain the
      demonstrated device boundary for Milestone 10 regrouping.
- [x] Complete independent final startup/interconnect review, resolve concrete
      findings, and revalidate any accepted repairs.
- [x] Pass the final complete optimized, AddressSanitizer and native macOS
      leak checks; close the real-BIOS conformance block and milestone.

## Milestone 10: Reach a Responsive BIOS Shell

Boot the selected BIOS through real guest execution to an identified,
responsive browser/menu or equivalent shell checkpoint under an explicit
hardware/input profile. A repeated loop, absence of faults, or one repaired
device boundary is not completion: demonstrate ongoing system activity and
an expected guest response to deterministic controller input.

Use BIOS discovery to select the next coherent, evidence-backed hardware
capability. Implement only the boot services, media, input, ROM and module
support required by the selected checkpoint, preserving deterministic device
time independently of host I/O speed. Defer unused hardware breadth, not the
fault handling, reset, persistence, determinism or regression coverage of
implemented behavior; do not manufacture readiness or replace guest services.

Milestone 9 is complete, including real BIOS SIF flag exchange and repeated
trace/save-load conformance. The completed dependency blocks below retain
their status; unfinished device scopes remain conditional on the shell target,
not requirements for exhaustive subsystem coverage.

### Desktop BIOS Presentation

- [x] Run both CPUs incrementally in the SDL window under one finite cycle
      budget; present core GS output without frontend hardware policy and keep
      the final view visible on a CPU stop or budget boundary.
- [x] Report execution and disabled/active video status, support a bounded
      window smoke run and optional screenshot, and preserve demo/ELF modes.
- [x] Validate sliced execution and precise stops, desktop presentation and
      fresh unchanged-input BIOS output; independently review and pass the
      optimized/ASan gates without claiming a visible shell from a blank view.

### BIOS Shell Target and Acceptance Profile

- [x] Select SCPH-70012's Browser/System Configuration main menu: no disc/cards,
      one port-1 DualShock 2; port 2/expansion devices absent. Retain the ROM1/RTC
      profile; complete required setup in the guest, never bypass it.
- [x] Select recognizable core-rendered menu evidence plus an expected response
      to deterministic controller input; corroborate with core activity and
      replay evidence, without requiring an interactive frontend.
- [x] Define a six-emulated-second window: two seconds neutral, two one-second
      navigation slots (100 ms press/900 ms release), then two seconds neutral;
      require change-and-return, a neutral control and normal core activity.

### CD/DVD Startup Dependency Planning

- [x] Select the exact startup register/command batch from the demonstrated
      CD/DVD boundary, manuals and approved research; resolve access, reset,
      media, clock/configuration and timing policies before implementation.
- [x] Define controller ownership, N/S transport, buffers, errors/interrupts,
      persistence and diagnostics; split the selected behavior into reviewable
      blocks, separating command transport from required DMA/media services.

### CD/DVD Startup Controller and Deterministic RTC

- [x] Implement the standalone controller for selected byte registers,
      N-NOP and S-ReadClock, with deterministic RTC, bounded results,
      next-pulse completion and atomic unsupported admission; use red/green tests.
- [x] Independently review command/RTC state transitions and boundary coverage,
      then run complete optimized and AddressSanitizer checks before integration.

### CD/DVD Startup System Integration and Persistence

- [x] Compose the controller through exact IOP byte dispatch and existing clock/
      INTC boundaries; add transactional persistence and hashing in the same
      change, preserving SIF, channel-3 initialization and independent IRQ latches.
- [x] Validate guest access, completion ordering, reset, pending/result restore
      and malformed-state atomicity; independently review integration and run
      immediate complete optimized and AddressSanitizer checks.

### CD/DVD Startup Diagnostics and Authored Conformance

- [x] Add trace-enabled command acceptance/completion observations and maintained
      exporter/inspector support; validate ordering, capacity, disabled tracing
      and prefix/streaming behavior without per-tick or per-byte trace noise.
- [x] Complete authored startup/IRQ/RTC continuation and trace-mode conformance,
      bounded maintainability and independent correctness review; run complete
      optimized and AddressSanitizer checks before BIOS verification.
- [x] With separate BIOS-run authorization, verify the selected readiness
      boundary and attribute the next dependency; reopen device scope rather
      than inventing additional command, configuration or media responses.

### EE RDRAM SA0x4c Startup Dependency

- [x] Resolve the observed `0x804c0060` command and `0x27` input against
      available evidence; assess whether a wider command batch is justified.
- [x] Implement the selected exact command through the existing owner, with
      atomic rejection, persistence and guest integration coverage.
- [x] Independently review and validate the extension; separately authorize
      BIOS verification before attributing the next dependency.

### EE RDRAM Command Construction Startup Dependency

- [x] Investigate the observed `0xb021f01f` command store and its upstream
      construction; distinguish CPU or state errors from a hardware-contract
      gap, and assess the complete supported command family before selecting
      implementation scope.
- [x] Extend supported read and reinitialization selections consistently,
      retaining unrelated command bits and preserving bounded responses.
- [x] Validate guest, trace and transactional persistence continuations and
      rejection boundaries; independently review the extension.
- [x] With separate BIOS-run authorization, verify the expanded selector
      behavior and attribute the next startup dependency.

### EE RDRAM SA0x40 Write Startup Dependency

- [x] Investigate the observed `0x80400040` command and input against
      available evidence; select a coherent write-command scope before
      changing the RDRAM owner.
- [x] Implement the selected command behavior through the existing owner,
      with focused rejection and continuation coverage.
- [x] Validate guest, trace and persistence integration and independently
      review the extension.
- [x] With separate BIOS-run authorization, verify the selected write and
      attribute the next startup dependency.

### EE RDRAM Write-Command Startup Scope

- [x] Investigate the observed `0x80460060` command with input `1` against
      available evidence and the broader write-command family; select a
      coherent policy before changing the RDRAM owner.
- [x] Implement the selected policy through the existing owner with focused
      admission, effect and rejection coverage.
- [x] Validate guest, trace and persistence continuation and independently
      review the extension.
- [x] With separate BIOS-run authorization, verify the selected write family
      and attribute the next startup dependency.

### EE RDRAM Operation-Field Startup Scope

- [x] Search local manuals and retained startup evidence for the observed
      `0x800000a0` write and other operation-field values.
- [x] Compare the operation-2 command with approved refreshed emulator
      behavior and installed SDK evidence; identify the limits of the
      resulting compatibility evidence.
- [x] Select and implement a provisional operation-field write boundary
      through the existing owner: admit all started commands and retain them
      with the start bit cleared, while keeping SA4c/SA4d exact-only, the exact
      initialization input gate, SA21 reset behavior, and operation-0 reads.
- [x] Validate guest, trace and persistence continuation and independently
      review the extension.
- [x] With separate BIOS-run authorization, verify the selected operation
      family and attribute the next startup dependency.

### EE MCH Register Window

- [x] Search local manuals and compare the approved refreshed emulator/SDK
      sources; record the missing register contract and the limits of the
      provisional MCH-window policy.
- [x] Implement and independently review the initial exact f400 zero-read
      admission, then verify it in the bounded BIOS run and attribute the
      following same-address store.
- [x] With separate BIOS-run authorization, verify the read and attribute the
      next startup dependency: a full-word store to the same f400 address.
- [x] Replace the exact startup exceptions with a word-backed
      `0x1000f400–0x1000f4ff` MCH window, preserving the RICM/DRD protocol
      handlers and superseding the f410/f480/f490 exact-value filters;
      preserve the separate exact f500 reserved-write exception.
- [x] Persist and test the window through reset, host/guest access, trace,
      save/restore, access boundaries and the existing MCH protocol owner;
      independently review and pass optimized/ASan gates.

### CD/DVD NREADY Startup Wait Scope

- [x] Compare the observed NREADY bit-3 wait/countdown with local manuals;
      retain the selected no-media `0x40` profile because the manuals specify
      library-level drive readiness, not raw NREADY semantics. The timeout
      remains unobserved; further BIOS observation requires authorization.

### EE-Side CD/DVD Access Contract

- [x] Resolve the observed EE byte read at `0x1f402005` using local manuals,
      then authorized secondary evidence; distinguish access routing from
      readiness values, command execution and media presence.
- [x] Assess the related register family and select a coherent CPU-data
      direction/width/endpoint policy without assuming a generic IOP MMIO bridge.
- [x] Define shared controller wiring, access effects, completion/IRQ ordering,
      persistence and trace requirements; select the bounded implementation.

### EE-Side CD/DVD Integration

- [x] Implement the selected access policy through EEBus and the existing
      system-owned CDVDController, without parallel backing or new ready state.
- [x] Use red/green guest and cross-CPU tests for shared state, exact admission
      and atomic rejection; cover reset, trace and save/load where affected.
- [x] Independently review access and lifecycle integration; pass the required
      validation gates without expanding the selected device behavior.

### CD/DVD Boundary Verification

- [x] With separate BIOS-run authorization, verify progress beyond the EE
      access and attribute the next originating access, command or wait.
- [x] Compare bounded traced/plain and canonical continuations; preserve the
      demonstrated handshake and distinguish compatibility from completed boot.
- [x] Regroup the next block around that evidence before implementing commands,
      configuration, transfers or other boot devices.

### SIF RPC Service Availability

- [x] Attribute the empty IOP lookup for RPC request `0x80000006`: identify
      the expected server, registration path and startup/module prerequisites.
- [x] Separate guest initialization from DMA/cache/interrupt delivery errors;
      select a bounded next capability and its trace/continuation acceptance
      before implementing new device behavior.

### IOP Scratchpad-Mode Data Routing

- [x] Honor the approved DS+RAM mode for ordinary unswapped data accesses
      through the existing cache owner; preserve fetch, isolation, tag
      maintenance and physical DMA semantics.
- [x] Verify stale DMA-buffer reuse and restored traced/plain execution;
      independently review the cache boundary and pass broader checks.
- [x] Verify a fresh cold BIOS continuation through RPC-ready dispatch and
      LOADFILE registration, or attribute the next dependency without faking it.

### Post-LOADFILE Dependency Attribution

- [x] Inspect the validated 105M endpoint and bounded subsequent execution;
      attribute the next originating command, access or wait without assuming
      a CD/DVD dependency.
- [x] Verify the selected boundary with traced/plain and canonical continuation
      comparisons, then regroup the next capability around the evidence.

### EE DMA Active-Stop Boundary

- [x] Resolve the demonstrated `SifStopDma` zero-CHCR write against manual
      suspension rules and the existing active-channel policy; select bounded
      family-wide stop/reprogramming semantics before implementation.
- [x] Implement the selected policy through shared channel state, scratchpad
      DMA and existing device adapters; cover atomic rejection, continuation,
      transport and peer-state boundaries.
- [x] Independently review and validate the selected lifecycle, then verify
      fresh BIOS progress with traced/plain and canonical comparisons.

### IOP SIF DMA Active-Stop Boundary

- [x] Resolve the demonstrated IOP SIF1 zero-CHCR stop against local manuals
      and the existing shared SIF DMA owner; select whole-word stops for both
      implemented IOP SIF channels, with explicit cancellation and framing bounds.
- [x] Implement the selected bounded policy with atomic rejection and
      stopped/restored continuation through the bus; distinguish externally
      realigned restart from residual queued data without adding recovery.
- [x] Independently review the lifecycle and verify fresh BIOS progress with
      cold/restored traced/plain and canonical comparisons.

### Post-SIF-Stop Dependency Attribution

- [x] Resolve the channel-0 CHCR read through local manuals and existing
      initialization policy; select zero word readback for the 33 existing
      zero-only channel words, without transfer or readiness behavior.
- [x] Implement the selected reads through the common DMA owner and exact
      channel-word map; preserve fetch/supplemental/SIF boundaries and other
      access rejection, with guest trace/alias and reset/restored coverage.
- [x] Independently review the boundary and verify fresh BIOS continuation
      with cold/restored traced/plain and canonical comparisons.

### Post-Readback Dependency Attribution

- [x] Attribute the IOP word load at physical address 0x1e000000 through local
      manuals and required image/device inputs; select bounded absent-ROM1
      data reads through the IOP bus before expanding implementation.

### IOP Absent-ROM1 Data Profile

- [x] Admit aligned IOP CPU byte, halfword and word data reads as zero within
      the selected 4 MiB ROM1 window; retain rejection of fetch, writes, DMA,
      host loading/inspection and adjacent ranges without backing or readiness.
- [x] Cover aliases, load-delay and merge-load behavior, cache/isolation
      boundaries, reset and restored traced/plain execution; independently
      review the memory-access boundary and run affected validation gates.
- [x] Verify fresh unchanged-input BIOS progress with bounded discovery and
      cold/restored traced/plain and canonical comparisons; attribute the next
      originating dependency without claiming ROM1 content or completed boot.

### SIO2 Control Startup Scope

- [x] Resolve the observed `0x1f808268` control write and directly related
      register family through local manuals and authorized secondary evidence;
      distinguish initialization/readback from transfer, status and IRQ effects.
- [x] Select a bounded contract and owner boundary with reset, persistence,
      trace and authored validation requirements; obtain explicit approval
      before implementing command execution, progression, IRQs, DMA or timing.

### SIO2 Initialization-Only Control Profile

- [x] Implement selected CTRL initialization/readback through a dedicated owner,
      with exact bus admission, cold reset and canonical transactional restore;
      validate integration and independently review the new state boundary.
- [x] Cover guest aliases, protection, load delay, merge masks and cache
      boundaries; compare restored traced/plain execution around initialization
      and readback without enabling neighboring registers or active effects.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; attribute the next originating
      dependency without claiming SIO2 transfers, readiness or completed boot.

### SIO2 Follow-On Control Scope

- [x] Resolve the observed CTRL `0x0c` write using local manuals and the
      existing startup-family evidence; distinguish retained configuration,
      reset and active effects without inferring semantics from the value.
- [x] Select a bounded follow-on contract through the existing SIO2 owner,
      including lifecycle, readback and persistence; split implementation and
      conformance work after selection and obtain any required active-policy
      approval.

### SIO2 Passive Control Programming Profile

- [x] Admit selected CTRL `0x0c` programming through the existing owner, with
      exact access admission and versioned transactional persistence;
      independently review the boundary and run required persistence gates.
- [x] Cover guest control transitions, load delay, merge/cache boundaries and
      restored traced/plain execution without enabling neighboring registers
      or active effects.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; attribute the next originating
      dependency without claiming active reset, transfers or completed boot.

### SIO2 Port-Control Programming Dependency

- [x] Research the observed `0x1f808240` write and its port-control family;
      select and implement a bounded contract with guest/restored coverage,
      required persistence gates and independent review before the commit.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; attribute the next originating
      dependency without claiming active transfers, readiness or completed boot.

### SIO2 Command-Descriptor Programming Dependency

- [x] Research the observed `0x1f808200` write and its descriptor family;
      select and implement a bounded programming/lifecycle contract with guest/
      restored coverage, required persistence gates and independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; attribute the next originating
      dependency without claiming active transfers, readiness or completed boot.

### SIO2 Byte-Write Dependency

- [x] Research the byte write at `0x1f808260` and its containing family;
      obtain any required active-policy approval before implementing a bounded
      contract with guest/restored coverage, gates and independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; attribute the next originating
      dependency without claiming active transfers, readiness or completed boot.

### SIO2 Active-Control Dependency

- [x] Research the SIO2 CTRL `0xb1` write at `0x1f808268`; obtain explicit
      approval for a bounded active-control/transfer contract before implementing
      it with guest/restored coverage, gates and independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; attribute the next originating
      dependency without claiming completed boot.

### SIO2 Successive Infrared Transfers

- [x] Research descriptor `0x001c0741` and successive START without device-cause
      acknowledgement; select an evidence-backed descriptor/lifecycle contract
      before implementation with guest/restored coverage and independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 S/PDIF Configuration Initialization

- [x] Research the halfword write of `0x0900` at `0x1f9007c6` and its containing
      register family; select a bounded contract and coherent owner before
      implementation with guest/restored coverage, gates and independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 `0x1f9007ca` Initialization Dependency

- [x] Research the halfword write of `0x0008` at `0x1f9007ca` and its containing
      family; distinguish passive configuration from active effects before
      selecting a bounded extension with guest/restored coverage and review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 Fixed Master-Volume Controls

- [x] Research and implement fixed-mode master-volume control halfwords for both
      cores/channels through the SPU2 owner; reject sweeps, preserve distinct
      gain/current-volume boundaries, and add guest/restored coverage and review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 `0x1f900b60` Initialization Dependency

- [x] Research the zero halfword write at `0x1f900b60` and its containing
      family; distinguish unknown initialization from master controls and add
      bounded owner admission with guest/restored coverage and independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 `0x1f900b62` Initialization Dependency

- [x] Research the zero halfword write at `0x1f900b62` and reassess the shared
      unknown-initialization boundary before a bounded implementation with
      guest/restored coverage and independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 `0x1f90019a` Read Dependency

- [x] Research and implement unprogrammed core ATTR halfword readback for both
      cores through the SPU2 owner; reject writes and active effects, with
      guest/restored coverage and independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 Core ATTR Programming Dependency

- [x] Research the observed zero halfword write and shared core ATTR family;
      select zero-only halfword initialization for both unprogrammed cores,
      with no active effects.
- [x] Implement the approved zero-only ATTR policy through the existing owner;
      reject nonzero writes and other transports, with focused guest/restored
      integration coverage and independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 Auto DMA Zero-Initialization Dependency

- [x] Research the observed zero halfword write and shared Auto DMA family;
      select zero-only halfword initialization for both unprogrammed cores,
      with no active effects.
- [x] Implement the selected zero-only Auto DMA policy through the existing
      owner; reject reads, nonzero writes and other transports, with focused
      guest/restored integration coverage and independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 Nonzero Core ATTR Programming Dependency

- [x] Research the observed ATTR `0x8000` write and shared-core effects;
      select retained per-core `0`/`0x8000` images with matching readback,
      without active hardware effects.
- [x] Implement the selected policy through the existing SPU2 owner, with
      focused guest/restored integration coverage, excluded-access and
      side-effect checks, and independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 `0x1f900344` Read Dependency

- [x] Research the originating halfword read and its containing register
      family; select paired quiescent-only STATX halfword reads, without
      active status, transfer, IRQ or timing behavior.
- [x] Implement the selected bounded policy through the existing owner with
      focused red/green, guest/restored and excluded-access coverage, plus
      independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 `0x1f9001a4` Store Dependency

- [x] Research the originating `0xffff` halfword store and its containing
      register family; select an evidence-backed owner policy and identify
      any active-effect approval boundary.
- [x] Implement the selected bounded policy through the existing owner with
      focused red/green, guest/restored and excluded-access coverage, plus
      independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 S/PDIF Output Control (`0x1f9007c0`)

- [x] Research the originating `0xc032` halfword store and its containing
      register family; select passive output-control retention without
      activating audio, transfers or interrupts.
- [x] Implement passive S/PDIF output-control retention through the existing
      owner, with focused red/green, persistence/guest and excluded-access
      coverage, plus independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 Core Attribute `0xc000` Programming

- [x] Research the originating `0xc000` halfword store and the existing ATTR
      family; select passive bits14-15 retention with lower bits rejected and
      no active effects.
- [x] Implement the selected bounded policy through the existing owner with
      focused red/green, guest/restored and excluded-access coverage, plus
      independent review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 Core Attribute `0xc001` Programming

- [x] Research the originating core1 `0xc001` halfword store and ATTR bit0;
      select passive bit0 retention alongside bits14-15, with bits1-13
      rejected and no active effects.
- [x] Implement the selected policy through the existing owner with focused
      red/green, guest/restored and excluded-access coverage, plus independent
      review.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 Passive Voice-Mix Routing Masks

- [x] Research the originating `0xffff` halfword store and the paired-core
      VMIXL/VMIXEL/VMIXR/VMIXER family; select passive halfword retention
      without activating voices, mixing, effects or signals.
- [x] Implement all sixteen independent halfword images through the existing
      owner with red/green, guest, excluded-access and canonical persistence
      coverage; independently review and run required optimized/ASan gates.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 Passive Main-Mix Routing Controls

- [x] Research the originating `0x0ff0` halfword store and paired-core MMIX
      family; select passive halfword retention without activating source
      routing, mixing, effects or output.
- [x] Implement both independent MMIX halfword images through the existing
      owner with red/green, guest, excluded-access and canonical persistence
      coverage; independently review and run required optimized/ASan gates.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 Passive Effect and Input Volumes

- [x] Research the originating zero halfword store at `0x1f900764` and the
      paired-core EVOL/AVOL/BVOL family; select passive full-halfword retention
      without effect processing, external input or sound-data transfer.
- [x] Implement all twelve independent halfword images through the existing
      owner with red/green, guest, excluded-access and canonical persistence
      coverage; independently review and run required optimized/ASan gates.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 Passive Effect-End Address Programming

- [x] Research the originating `0x000e` halfword store at `0x1f90033c` and
      paired-core EEA family; select passive high-halfword retention without
      effect execution, work-area clearing or sound-memory access.
- [x] Implement both independent EEA high-halfword images through the existing
      owner with red/green, guest, excluded-access and canonical persistence
      coverage; independently review and run required optimized/ASan gates.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 Passive Transfer-Start Address Programming

- [x] Research the originating zero halfword store at `0x1f9001a8` and
      paired-core TSA high/low family; select passive programmed-halfword
      retention without sound-memory access, transfer, IRQ or readiness effects.
- [x] Implement all four independent TSA halfword images through the existing owner with
      red/green, guest, excluded-access and required persistence coverage;
      independently review and run required validation gates.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### SPU2 Passive Auto DMA Write-Selection Programming

- [x] Research the originating `0x0001` halfword write to `0x1f9001b0` and
      paired-core family; select approved passive write-selection while
      keeping ADMAS reads and active transfer/status effects unsupported.
- [x] Implement the approved paired write-selection through the existing SPU2 owner
      with red/green, excluded-access and required lifecycle/persistence coverage;
      independently review and run required validation gates.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### IOP SPU DMA Nonzero Address Programming Dependency

- [x] Research the originating word write to channel 4 MADR and paired
      channel 4/7 programming family; select the existing-owner boundary,
      keeping active transfer/start behavior separately gated.
- [x] Implement paired passive MADR programming through the existing DMA owner;
      preserve zero-only BCR/CHCR and excluded transports, with red/green
      and required lifecycle/persistence coverage;
      independently review and run required validation gates.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### IOP SPU DMA Passive Block-Count Programming Dependency

- [x] Research the originating `0x0010` halfword store to channel 4 BCR
      and paired channel 4/7 count family; select passive ownership,
      access widths and lane/readback policy, keeping active starts gated.
- [x] Implement paired passive BCR words and lane-preserving halfwords through
      the existing DMA owner; preserve zero-only CHCR and excluded transports,
      with red/green, lifecycle/persistence coverage, independent review and validation.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### IOP SPU DMA Active-Start Contract Dependency

- [x] Research the originating channel-4 CHCR `0x01000201` store and paired
      channel-4/7 DMA/SPU2 boundary; distinguish buffered Auto DMA input from
      normal TSA transfers and split independently testable prerequisites.
- [x] Obtain consequential-policy approval for bounded PCM input, requests,
      shared DMA grants, cancellation, completion/IRQs and timing before implementation.

### SPU2 Shared Sound Memory and Buffered Input Dependency

- [x] Add shared 2 MiB sound memory through the existing SPU2 owner, with
      explicit address units, reset, canonical persistence and transactional adoption;
      keep voice, effect and normal TSA transfer execution unsupported.
- [x] Implement paired bounded PCM input buffers, real capacity/backpressure
      and deterministic 48 kHz consumption through that owner; cover startup,
      starvation, partial fills, cancellation and restored phase.

### IOP SPU DMA Granted Transfer Dependency

- [x] Extend the existing DMA owner with paired channel-4/7 programmed/current
      state, physical RAM source beats and bank-correct masked completion;
      cover count/address progress, rejected beats and active-write guards.
- [x] Integrate SPU and SIF endpoints into one priority-ordered IOP DMA grant
      budget, preserving SIF-only behavior and deterministic ties, clock ordering
      and restored continuation under contention.

### IOP SPU DMA CHCR Lifecycle and Conformance Dependency

- [x] Admit the approved bounded CHCR profile through exact CPU access routes;
      integrate start, backpressure, abort and final-beat completion without
      fabricated live status or unsupported-mode success.
- [x] Export approved bounded SPU beat and request/buffer transitions through
      typed trace/failure-probe observations and maintained inspectors;
      preserve ordering, fixed capacity and tracing-independent execution.
- [x] Add authored CPU-driven lifecycle and cold/restored traced/plain
      conformance covering both cores, buffer boundaries, contention and faults;
      independently review and run required validation gates.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### IOP SPU Auto DMA ADMAS Readback Dependency

- [x] Research and admit bounded halfword ADMAS reads through the existing SPU2
      owner for both cores; distinguish control readback from live request/status,
      and pin CPU-load and restored-continuation behavior.
- [x] Verify fresh unchanged-input BIOS progress with cold/restored
      traced/plain and canonical comparisons; identify the next originating
      dependency without claiming completed boot.

### EE COP0/Wide Physical-Pipeline Pairing Regression

- [x] Reproduce and repair COP0/wide-operation concurrent selection under the
      local-manual routing contract and existing EE issue owner; cover both
      instruction orders, physical exclusions and staged continuation.
- [x] Verify fresh unchanged-input BIOS progress beyond the originating pairing
      failure with cold/restored traced/plain and canonical comparisons;
      identify the next dependency without claiming completed boot.

### EE Reserved COP0 Read Dependency

- [x] Admit bounded canonical MFC0 reads for the official reserved COP0 family
      through the existing decode/register owner; pin the zero-read policy,
      excluded writes/selectors, CPU ordering and restored continuation.
- [x] Verify fresh unchanged-input BIOS progress beyond the originating read
      with cold/restored traced/plain and canonical comparisons; identify the
      next dependency without claiming completed boot.

### EE Debug COP0 Read Dependency

- [x] Research and implement the bounded debug COP0 read required by BIOS through
      existing decode/register ownership; distinguish selector validity and
      passive readback from active breakpoint behavior.
- [x] Verify fresh unchanged-input BIOS progress beyond the originating debug
      read with cold/restored traced/plain and canonical comparisons; identify
      the next dependency without claiming completed boot.

### EE Performance COP0 Read Dependency

- [x] Research and admit bounded performance COP0 reads required by BIOS through
      existing ownership; separate event-specifier readback from counter
      execution, and cover selector validity and restored continuation.
- [x] Verify fresh unchanged-input BIOS progress beyond the originating
      performance read with cold/restored traced/plain and canonical comparisons;
      identify the next dependency without claiming completed boot.

### EE Performance COP0 Zero-Control Write Dependency

- [x] Research and admit bounded MTPS zero-control writes through the existing
      performance/COP0 owner; exclude nonzero control and counter programming,
      and cover usability, ordered issue and restored continuation.
- [x] Verify fresh unchanged-input BIOS progress beyond the originating
      zero-control write with cold/restored traced/plain and canonical comparisons;
      identify the next dependency without claiming completed boot.

### EE Debug COP0 Zero-Control Write Dependency

- [x] Research and admit bounded MTBPC zero-control writes through existing
      debug/COP0 ownership; exclude other debug programming and cover
      usability, ordered issue and restored continuation.
- [x] Verify fresh unchanged-input BIOS progress beyond the originating
      breakpoint-control write with cold/restored traced/plain and canonical
      comparisons; identify the next dependency without claiming completed boot.

### CD/DVD Subcommand Parameter Transport Dependency

- [x] Research the observed byte-zero subcommand-parameter write and its
      containing command sequence; define bounded staging, capacity, phase,
      reset and persistence contracts through the existing CDVD owner.
- [x] Implement the approved bounded parameter transport with focused CPU/bus,
      command-boundary and restored-continuation coverage; keep unsupported
      commands and identity/NVM/media policies explicit.
- [x] Verify fresh unchanged-input BIOS progress beyond the originating
      parameter write with cold/restored traced/plain and canonical comparisons;
      identify the next dependency without claiming completed boot.

### CD/DVD MECHACON Version Query Dependency

- [x] Research the staged SCMD03/00 four-byte version query and select an
      explicit deterministic identity, exact request/result shape, command
      lifecycle and replacement evidence before implementing a response.
- [x] Implement the approved version query through the existing CDVD owner
      with malformed-request rejection, clocked completion, result draining
      and canonical restored-continuation coverage.
- [x] Verify the demonstrated query with fresh unchanged-input cold/restored
      traced/plain and canonical comparisons; identify the next dependency
      without claiming completed boot or measured console identity.

### CD/DVD SCMD36 Command Dependency

- [x] Research the observed parameterless SCMD36 request and its containing
      family; select explicit response inputs, request/result shape, lifecycle
      and replacement evidence before implementing a response.
- [x] Implement the approved bounded command through the existing CDVD owner,
      with exact response draining, malformed-request rejection, trace and
      canonical restored-continuation coverage.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress beyond SCMD36; identify the next originating dependency without
      claiming measured configuration fidelity or completed boot.

### IOP SIO2 DMA Passive Programming Dependency

- [x] Research channel11/12 MADR and BCR mapping; select passive access,
      retention and reset semantics through the existing DMA owner, keeping
      CHCR zero-only and active transfers outside this block.
- [x] Implement paired MADR/BCR programming with exact CPU access widths,
      atomic rejected accesses and canonical restored-continuation coverage;
      preserve unrelated zero-only registers and existing SIO2 behavior.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress beyond the originating write; identify the next dependency
      without claiming active SIO2 DMA, peripheral support or completed boot.

### IOP SIO2 DMA Stopped Channel Control Dependency

- [x] Research channel11/12 CHCR and the demonstrated0x201 write; distinguish
      stopped configuration from active lifecycle, and select bounded
      ownership, access, retention and reset policy before implementation.
- [x] Implement only the approved stopped-control contract with atomic
      rejection and canonical restored-continuation coverage; keep active
      DMA, peripheral effects, IRQs and timing outside this dependency.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress beyond the CHCR origin; identify the next dependency without
      claiming active SIO2 DMA or completed boot.

### IOP SIO2 DMA Input Start Dependency

- [x] Research the demonstrated channel11 start and its SIO2 transport dependency;
      separate padded input staging from serial execution, then obtain approval
      for ownership, progress, completion, cancellation, IRQ and timing policy.
- [x] Implement the approved single-block input transport and CPU lifecycle
      through existing SIO2 storage and shared DMA grants, with atomic rejection,
      canonical continuation and no synthetic peripheral results or completion.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress beyond the start origin; identify the next dependency without
      claiming broader peripheral support or completed boot.

### IOP SIO2 DMA Output Control Dependency

- [x] Research channel12 CHCR0x40000200 after output MADR/BCR programming;
      distinguish control configuration from active output transport and select
      bounded access, ownership and lifecycle policy before implementation.
- [x] Implement the selected stopped output-control family through the existing
      DMA owner, retaining bit30 without active effects, with atomic rejection
      and canonical continuation; keep output transfers and serial results excluded.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress beyond the control origin; identify the next dependency without
      claiming peripheral replies or completed boot.

### IOP SIO2 DMA Output Start Arming Dependency

- [x] Research the demonstrated channel12 CHCR0x41000200 start and its SIO2
      dependency; distinguish output DMA requests from serial execution, then
      obtain approval for bounded arming, waiting, cancellation and reset.
- [x] Implement the approved pending output request through the existing DMA
      owner, with atomic rejection, canonical continuation and diagnostics;
      no result consumption, RAM transfer, completion or fabricated replies.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress beyond the start origin; identify the next dependency without
      claiming card success or completed boot.

### IOP SIO2 DMA-backed Serial Start Dependency

- [x] Research the demonstrated CTRL0x3bd start with sealed DMA input and an
      armed output request; select endpoint, result, IRQ and lifecycle contracts
      and obtain approval before active serial behavior.
- [x] Implement the approved bounded absent-card probe through the SIO2 owner,
      with next-pulse result production, independent status/IRQ, atomic
      lifecycle guards and canonical continuation; keep CPU infrared distinct.
- [x] Connect real SIO2 results to channel12 shared DMA grants with explicit
      padding, backpressure, write-before-consume progress, cancellation and
      masked completion; never manufacture results for an armed request.
- [x] Validate authored serial/DMA continuation across pending, ready, partial
      and cancelled states, including transactional canonical rejection and
      independent serial/DMA interrupts.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress; distinguish negative peripheral results from fabricated success
      or completed boot.

### IOP SIO2 Completed-Probe Control Dependency

- [x] Research the demonstrated completed-state CTRL|0x0c rewrite with empty
      ingress/results and inactive output DMA; select bounded retained-control,
      restart and IRQ semantics before admitting the new lifecycle case.
- [x] Implement the selected bounded contract through the existing SIO2 owner,
      with atomic lifecycle guards and authored canonical, trace and interrupt
      coverage; distinguish completed rewrites from DMA-backed probe starts.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress without fabricating input, results, completion or peer replies.

### IOP SIO2 Command 0xf3 Dependency

- [x] Research the demonstrated descriptor0x00140572 and sealed `81 f3` input;
      select endpoint, absent-device/error result shape, lifecycle and IRQ
      contracts, with approval before active command behavior.
- [x] Implement the selected bounded command through the existing SIO2 owner,
      with atomic admission, real output-DMA results and authored canonical,
      trace and interrupt coverage; preserve the existing probe contracts.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress without fabricating peer replies, readiness or card success.

### IOP SIO2 CPU Command 0x52 Dependency

- [x] Research descriptor0x000c0342, controls0xffc00505/0x000201f4 and CPU-staged
      `81 52 00`; select endpoint, absent-device results, lifecycle and IRQ
      contracts, with approval before active command behavior.
- [x] Implement the selected bounded command through the existing CPU-ingress
      and receive owner, with atomic admission and authored canonical, trace
      and interrupt coverage; preserve DMA-backed and infrared profiles.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress without fabricating peer replies, readiness or card success.

### IOP SIO2 CPU Multitap Support Query Dependency

- [x] Research descriptor0x00180642, controls0xff020505/0x00030064 and CPU-staged
      `21 12 00 00 00 00`; select endpoint, absent-device results, lifecycle
      and IRQ contracts, with approval before active command behavior.
- [x] Implement the selected bounded request through the existing SIO2 owner,
      with atomic admission and authored canonical, trace and interrupt
      coverage; preserve CPU-card, infrared and DMA-backed profiles.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress without fabricating peer replies, readiness or device success.

### IOP SIO2 CPU Multitap Memory-Card Support Query Dependency

- [x] Research descriptor0x00180642, controls0xff020505/0x00030064 and CPU-staged
      `21 13 00 00 00 00`; select command identity, absent-device results,
      lifecycle and IRQ contracts, with approval before active behavior.
- [x] Implement the selected bounded request through the existing SIO2 owner,
      with atomic admission and authored canonical, trace and interrupt
      coverage; preserve CPU-card, infrared and DMA-backed profiles.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress without fabricating peer replies, readiness or device success.

### IOP DEV9 Revision Halfword Read Dependency

- [x] Research the originating IOP halfword read at0x1f80146e and its register
      family; select evidence-backed identity, owner, read value and side
      effects without inferring readiness or generic MMIO fallback.
- [x] Implement the selected bounded register contract with focused CPU/bus
      and immediate-family regressions through one shared revision policy;
      preserve existing EE access limits and initialization contracts.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress, identifying the next originating dependency without a boot claim.

### IOP SPU2 Post-Input Core Status Read Dependency

- [x] Research the originating core-0 status halfword read at0x1f900344 after
      Auto DMA input use; select both-core lifecycle/status semantics through
      the existing owner without assuming zero or fabricated status progression.
- [x] Implement the selected bounded status contract through SPU2Controller
      with CPU/bus, lifecycle, trace and restored-continuation coverage;
      preserve existing input/DMA ownership and unsupported execution limits.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress, identifying the next originating dependency without a boot claim.

### IOP SPU2 Pitch-Modulation and Noise Mask Programming

- [x] Research the originating IOP halfword zero-store at0x1f900180 and its
      containing register family; select bounded semantics and the existing
      owner without assuming inactive voices or authorizing audio execution.
- [x] Implement paired-core passive PMON/NON mask programming with CPU/bus,
      lifecycle and persistence coverage; preserve unsupported voice execution.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress, identifying the next originating dependency without a boot claim.

### IOP SPU2 Unnamed Halfword Zero Initialization at0x1f9007b8

- [x] Research the originating IOP halfword zero-store at0x1f9007b8 and its
      containing register family; select evidence-backed identity, owner and
      bounded semantics without inferring audio execution or readiness.
- [x] Implement exact zero halfword writes through the existing SPU2 owner with
      focused CPU/bus and lifecycle coverage, including persistence if affected;
      preserve unsupported neighboring accesses and execution limits.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress, identifying the next originating dependency without a boot claim.

### IOP SPU2 Unnamed Halfword Zero Initialization at0x1f9007ba

- [x] Research the originating IOP halfword zero-store at0x1f9007ba and its
      containing unnamed range; reassess bounded semantics through the existing
      owner without assuming adjacent registers share a hardware function.
- [x] Implement the selected bounded contract with focused CPU/bus and
      lifecycle coverage, including persistence if affected; retain unsupported
      execution, neighboring accesses and transports.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress, identifying the next originating dependency without a boot claim.

### IOP SPU2 Halfword Programming at0x1f9007bc

- [x] Research the originating IOP halfword zero-store at0x1f9007bc and its
      containing unnamed range; reassess bounded semantics through the existing
      owner without assuming adjacent registers share a hardware function.
- [x] Implement the selected bounded contract with focused CPU/bus and
      lifecycle coverage, including persistence if affected; retain unsupported
      execution, neighboring accesses and transports.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress, identifying the next originating dependency without a boot claim.

### IOP SPU2 Halfword Programming at0x1f9007be

- [x] Research the originating IOP delay-slot halfword zero-store at0x1f9007be
      and the containing unnamed range; reassess bounded semantics through the
      existing owner without assuming adjacent registers share a function.
- [x] Implement the selected bounded contract with focused CPU/bus and
      lifecycle coverage, including persistence if affected; retain unsupported
      execution, neighboring accesses and transports.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical
      progress, identifying the next originating dependency without a boot claim.

### IOP SPU2 Halfword Store at0x1f9001ac

- [x] Research the originating IOP halfword store of `0x0707` and its
      containing transfer-data family; identify the existing owner and gaps
      in RAM-transfer, cursor, FIFO timing and IRQ evidence.
- [x] Approve a bounded active-transfer contract for the paired CPU data
      ports, including RAM mutation, cursor/readback, concurrency and timing
      limits, before implementation.
- [x] Implement the selected contract with focused red/green CPU/bus and
      affected lifecycle coverage; include persistence if its contract changes
      and preserve unsupported accesses and execution limits.
- [x] Independently review and validate the capability.
- [x] Verify fresh unchanged-input cold/restored traced/plain and canonical progress,
      attributing the next originating dependency without a boot claim.

### SPU2 Core Attribute `0x8010` Programming

- [x] Research the originating halfword write to `0x1f90019a` and the
      containing transfer-mode family; define RAM/cursor, status, timing,
      IRQ and DMA boundaries through the existing SPU2 owner.
- [x] Select the bounded transfer-mode contract, including effects on queued
      data, cursor progress, status, IRQ and DMA execution.
- [x] Implement the selected contract with focused red/green
      CPU/bus and lifecycle coverage, preserving unsupported active behavior
      and including persistence if its contract changes.
- [ ] Independently review and verify fresh unchanged-input cold/restored
      traced/plain and canonical progress, identifying the next originating
      dependency without a boot claim.

### Additional CD/DVD Command and Configuration Scope

- [ ] Expand command dependencies required by the selected shell profile into
      independent transport and command-family blocks; select parameters,
      results, busy/error behavior, IRQs, cancellation and timing.
- [ ] Select explicit deterministic inputs for any required identity, NVM,
      region, configuration or RTC programming; do not substitute zero-success
      responses or fabricate authentication and media readiness.

### Optical Media and DMA Scope

- [ ] If required by the selected shell profile, define media-image inputs,
      supported sector formats and no-media/error behavior, keeping host file
      I/O outside the device core; otherwise defer media transfer breadth.
- [ ] Plan buffered sector production and channel-3 DMA through their existing
      owners, including backpressure, transfer counts, completion, cancellation,
      emulated deadlines and deterministic restored continuation.
- [ ] Split the selected seek/read/transfer lifecycle into reviewable blocks
      with authored fixtures before implementing a media boot path.

### SIO2, Input and Memory-Card Scope

- [ ] Expand demonstrated SIO2 register, command, DMA and IRQ dependencies into
      reviewable blocks; implement controller or memory-card protocols only as
      required by the selected shell profile and responsiveness demonstration.
- [ ] Define deterministic controller-input sampling and separate memory-card
      protocol state from host persistence; select absent-device/error behavior.
- [ ] With a supported serial result producer, integrate output DMA into shared
      grants with real FIFO backpressure, packing, progress and masked completion;
      keep DMA completion distinct from serial completion and peer success.

### ROM and Module-Loading Scope

- [ ] Classify demonstrated ROM/module-loading failures as missing hardware,
      image mapping or guest software before selecting support; preserve real
      guest SIF execution rather than introducing host RPC/module shortcuts.

### Boot-Service Conformance and Closure

- [ ] Demonstrate the selected shell checkpoint, ongoing system activity and
      expected response to the defined controller-input sequence; a fault-free
      loop or unsupported-device wait does not satisfy acceptance.
- [ ] Validate the selected flows with authored regressions and bounded BIOS
      repeated cold-boot, traced/plain and canonical/save-load continuation
      comparisons under the same recorded inputs.
- [ ] Complete bounded maintainability and independent correctness review,
      reconcile provisional policies, and pass optimized, ASan and native leak
      closure gates.
- [ ] Move unused conditional device scopes into post-BIOS completion blocks
      and inventory partial contracts/provisional policies; distinguish
      boot-supported behavior from broader subsystem coverage.

## Post-BIOS Hardware Completion

After Milestone 10, revisit partial subsystems in separately scoped completion
blocks rather than extending BIOS discovery into an unbounded manual audit.
Prioritize shared architectural gaps, unresolved provisional policies and
likely game requirements; BIOS success is integration evidence, not proof of
complete hardware behavior.

- [ ] Audit partial DMA, cache/memory, CD/DVD, SIO2/input and ROM/platform
      contracts against local manuals and recorded evidence; distinguish
      correctness defects, unsupported breadth and provisional policy gaps.
- [ ] Select and expand prioritized completion work into reviewable subsystem
      blocks with explicit behavioral acceptance and owner/extension boundaries;
      route audio and title-specific dependencies to their existing milestones.
- [ ] Implement and validate the selected completion blocks with authored
      conformance, reset/restored determinism and risk-appropriate review/gates;
      retain the responsive BIOS shell as an integration regression, not the
      sole correctness oracle.

## Milestone 11: SPU2 and Deterministic Audio

Implement the PS2 audio hardware as a deterministic core subsystem that
produces PCM without owning host audio devices or pacing. Cover both SPU2
cores, voice processing, streaming, effects, DMA, interrupts, and continuation.

- [ ] Audit the local SCEI manuals and expand this milestone into independently
      reviewable blocks and items before implementation. Inventory register
      state, two-core and 48-voice organization, ADPCM, looping, pitch, ADSR,
      mixing, effects and reverb, streaming, DMA, interrupts, sample timing,
      persistence, PCM validation, unresolved evidence, and frontend
      boundaries.
- [ ] Implement voice decoding and lifecycle, pitch, envelopes, looping, and
      deterministic per-sample mixing for both cores.
- [ ] Add effects, streaming, DMA, interrupt, timing, reset, and save-state
      behavior required by authored guests and the selected system workload.
- [ ] Add headless PCM hashes and reference fixtures, then complete optimized,
      sanitizer, leak, and independent-review validation.

## Milestone 12: Boot a Selected Game

Boot one deliberately selected title through the BIOS using the real EE, IOP,
media, input, graphics, and audio paths. Expand hardware only from concrete
guest evidence and define a bounded success point before implementation.

- [ ] Select the target title and startup checkpoint, capture its known boot
      requirements, and expand this milestone into independently reviewable
      blocks and items before implementation. Inventory the required DMAC
      channels, GS features, IPU behavior, timers, media commands, modules,
      synchronization, diagnostics, persistence, and acceptance artifacts.
- [ ] Before title-specific expansion, complete a bounded retrospective review
      of VU pipeline reset, Force Break, halt/resume, save-state continuation,
      branch and termination drain ordering, simultaneous-write priority,
      forwarding, and VIF/VU/GIF assumptions. Exercise EE, DMA, VIF, VU1, GIF,
      and GS together with deterministic clock, interrupt, DMA, reset, trace,
      framebuffer-hash, and continuation assertions.
- [ ] Advance the title incrementally through the normal BIOS boot path,
      recording each new hardware dependency and moving broadly reusable work
      into the appropriate subsystem block.
- [ ] Add demand-driven transport and graphics behavior only when the selected
      title demonstrates it: VIF0 DMAC channel 0; additional EE DMAC priority,
      slicing, stall, MFIFO, hold, or bus-error facilities; GS alpha-test and
      `AFAIL`; and GS `FINISH`, CSR acknowledgement, IMR, interrupt, and
      local-to-host transfer synchronization.
- [ ] Add graphics diagnostics only when existing traces cannot localize a
      failure. Candidate signals include decoded unsupported-feature reports,
      draw-kick state and vertex snapshots, primitive and pixel summaries,
      draw bounds, framebuffer hashes, and dependency-free framebuffer-region
      export.
- [ ] Reach the selected stable checkpoint with deterministic frame, audio,
      trace, state, save-state, reset, and repeated-run results.
- [ ] Complete focused compatibility audits and an independent milestone
      review without generalizing from one title beyond demonstrated behavior.

## Milestone 13: Playable-System Integration

Advance the selected workload from boot success to a defined playable
checkpoint with coherent input, audio, graphics, timing, persistence, reset,
and host control. Use additional titles as targeted compatibility evidence,
not as an unbounded completeness requirement.

- [ ] Define the playable checkpoint and expand this milestone into
      independently reviewable blocks and items before implementation.
      Inventory gameplay-time hardware dependencies, performance limits,
      synchronization risks, input and presentation requirements, save-state
      boundaries, compatibility probes, diagnostics, and final acceptance
      artifacts.
- [ ] Complete the input, audio, graphics, media, timing, and system behavior
      required to reach and repeat the selected playable checkpoint.
- [ ] Establish deterministic frame and PCM hashes, stable save/load and reset,
      bounded performance targets, and divergence diagnostics over extended
      execution.
- [ ] Complete cross-subsystem conformance, optimized, sanitizer, leak, and
      independent final review before frontend packaging becomes the focus.

## Milestone 14: Frontends and libretro

Package the deterministic core through maintained debugging, desktop, and
libretro consumers without moving hardware, timing, persistence, or
compatibility policy into a frontend.

- [ ] Audit the completed core APIs and expand this milestone into independently
      reviewable blocks and items before implementation. Define debugger,
      checkpoint and replay, input, video, audio, logging, save-state, desktop,
      libretro, packaging, compatibility, and integration-test boundaries;
      identify which existing items below remain justified by the final core
      architecture.
- [ ] Present completed guest-ELF framebuffers through the optional SDL desktop
      while retaining headless `--elf` execution as the diagnostic and
      automation path.

### Command-Line Debugger

- [ ] Keep the command-line debugger as a separate core consumer
- [ ] Add breakpoints, watchpoints, register views, and memory inspection
- [ ] Add named checkpoints and a bounded automatic checkpoint ring
- [ ] Record and replay deterministic input from a selected checkpoint
- [ ] Implement reverse-step and reverse-continue by restoring the nearest
      checkpoint and replaying to the target master cycle
- [ ] Break on interrupts, DMA completion and stalls, GIF tags, presentation
      boundaries, and selected subsystem trace events
- [ ] Add conditional register, memory, and MMIO watchpoints
- [ ] Diff registers, memory ranges, pipelines, and subsystem state between
      two checkpoints
- [ ] Add trace export without coupling presentation to hardware classes
- [ ] Filter trace inspection and export by subsystem, event type, and master
      cycle range
- [ ] Compare trace hashes and bisect the first divergent master cycle between
      two deterministic executions
- [ ] Export reproducible bug bundles containing a save state, subsequent
      inputs, expected hashes, and focused trace excerpts
- [ ] Add a timeline view for VU execution, DMA transfers, GIF arbitration,
      interrupts, and presentation boundaries

### Libretro Adapter

- [ ] Create a thin `neko_libretro` C ABI adapter
- [ ] Map `retro_run()` to deterministic frame execution
- [ ] Connect RetroArch input, video, audio, logging, and save-state callbacks
- [ ] Add core options without placing frontend policy inside `neko_core`

### Frontend Integration Tests

- [ ] Run the same deterministic workload through CLI and libretro adapters
- [ ] Verify input, video, audio, and logging callback boundaries
- [ ] Verify save-state compatibility through the public frontend interfaces
- [ ] Keep frontend output hashes identical for equivalent core configuration

## Performance Strategy

1. Preserve correctness and determinism.
2. Profile realistic programs.
3. Predecode microinstructions and remove avoidable allocation.
4. Use compact array-based pipeline storage and efficient dispatch.
5. Consider an optional recompiler only if the reference interpreter cannot
   meet the required performance.

- [ ] Compare every optimized execution path continuously against the
      cycle-oriented interpreter, including the floating-point integration
      corpus
