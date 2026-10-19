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
- [ ] Define/test whole-machine reset and host CPU halt/resume scopes;
      keep unevidenced guest reset and mode operations explicitly unsupported.
- [ ] Add an on-demand/budget-boundary startup summary for both CPUs, SIF,
      implemented DMA, and interrupts; report unsupported accesses and waits
      without side-effecting diagnostic reads or per-poll logging.
- [ ] Cover one CPU halted/faulted while devices and its peer advance, budget
      exhaustion, reset, repetition, and restore; complete independent review
      and sanitizer checks.

### SIF1: EE-to-IOP DMA

- [ ] Add focused IOP DMA control/interrupt ownership for the first endpoint;
      map required common controls and channel 10 without dummy device engines.
- [ ] Extend existing EE DMAC ownership for channel 6 and source-chain state;
      reuse shared transitions only where their invariants apply.
- [ ] Implement EE source admission, SIF framing, and IOP destination writes
      through physical DMA boundaries without CPU cache snooping, including
      resolved scratchpad-source addressing.
- [ ] Implement resolved tag/count/termination behavior, backpressure, and
      retry; failed admission must not consume data or advance continuation.
- [ ] Publish endpoint-local completion through EE DMAC and IOP DMA/INTC;
      distinguish sender completion from receiver drain and rearming.
- [ ] Persist/hash partial transfers and append bounded transfer/wait/completion
      observations; cover reset and save/load at each acceptance boundary.
- [ ] Add authored EE-to-IOP guest conformance and regress existing DMA paths;
      complete independent review and sanitizer checks.

### SIF0: IOP-to-EE DMA

- [ ] Extend IOP DMA ownership for channel 9 source tags and EE DMAC ownership
      for channel 5 destination-chain state without duplicating common control.
- [ ] Implement IOP source admission, transmitted EE tags, word/qword packing,
      and EE physical destinations, including resolved scratchpad addressing.
- [ ] Implement destination tags, zero/partial packets, termination, and
      backpressure/retry with explicit policy for unresolved residual data.
- [ ] Route endpoint-local completion, masks, acknowledgements, and receiver
      rearming independently of SIF1 and SBUS signals.
- [ ] Persist/hash framing and accepted continuation; cover partial tags,
      partial qwords, cached aliases, active reset, and save/load.
- [ ] Add authored IOP-to-EE guest conformance and bounded observations;
      complete independent review and sanitizer checks.

### Cross-Channel Scheduling and Lifecycle Conformance

- [ ] Integrate both directions into the existing 8:1 master/IOP schedule;
      define CPU/device admission, transfer visibility, and interrupt ordering
      without claiming undocumented retail bus timing.
- [ ] Exercise simultaneous directions, unequal producer/consumer progress,
      full/empty transport, receiver-late start, channel disable/restart,
      and independent completion/INTC acknowledgement.
- [ ] Cover same-cycle CPU/DMA memory overlap, exceptions, interrupts, CPU
      halt/resume, whole-machine reset, and evidenced guest-reset scope.
- [ ] Prove repeated-run equivalence, observational tracing, canonical combined
      state comparison, and byte-identical restore at active transfer boundaries;
      reject malformed cross-owner continuation without destination mutation.
- [ ] Complete independent cross-component review and optimized sanitizer checks.

### Deterministic BIOS Startup Boundary

- [ ] Pin the supplied BIOS identity, cycle budget, and exact guest-produced
      request/response checkpoint; retain ROM bytes outside tracked fixtures.
- [ ] Advance both CPUs through real startup/SIF execution without fabricated
      readiness, RPC responses, skipped instructions, or generic device stubs.
- [ ] Triage each earlier stop against the approved contract; explicitly review
      scope before adding prerequisite hardware or moving the target.
- [ ] Demonstrate the selected handshake, then record the first out-of-scope
      device dependency with CPU/access/wait evidence for Milestone 10 or its
      owning later milestone.
- [ ] Reproduce the BIOS checkpoint and boundary across bounded repetitions,
      trace modes, and save/load; retain authored BIOS-free regressions for
      every implemented prerequisite.
- [ ] Complete independent startup/integration review and sanitizer checks.

### Milestone Conformance and Closure

- [ ] Complete the authored two-CPU transport/lifecycle matrix and the complete
      optimized repository check, preserving existing EE/IOP/graphics workflows.
- [ ] Complete the bounded architecture and maintainability review of shared
      ownership, DMA transitions, scheduling, reset, diagnostics, and persistence.
- [ ] Reconcile provisional policies and the BIOS dependency record; expand
      Milestone 10 only from demonstrated boot-service needs.
- [ ] Complete independent final review, resolve concrete findings, and run
      optimized AddressSanitizer and native macOS leak checks.

## Milestone 10: Boot Services, Media, and Input

Implement the concrete boot-service hardware exposed by BIOS progress,
including optical-media commands and data flow, SIO2-facing input, controller
and memory-card behavior, and any required ROM or module-loading support.
Preserve deterministic emulated device time independently of host I/O speed.

- [ ] Use the Milestone 9 BIOS dependency record and local SCEI manuals to
      expand this milestone into independently reviewable blocks and items
      before implementation. Identify the exact CD/DVD, SIO2, controller,
      memory-card, ROM/module, DMA, interrupt, timing, persistence, and
      diagnostic behavior required by the selected boot path; defer unrelated
      peripheral breadth.
- [ ] Implement the required CD/DVD command, interrupt, DMA, sector, seek, and
      transfer-continuation behavior with deterministic emulated timing.
- [ ] Implement the required SIO2, controller, and memory-card protocols with
      deterministic input and persistent-media boundaries.
- [ ] Complete the selected BIOS boot-service flows with reproducible traces,
      hashes, save states, failure diagnostics, and independent review.

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
