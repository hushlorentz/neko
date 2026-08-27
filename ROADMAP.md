# Neko Roadmap

This is a living plan for evolving Neko from its current Vector Unit foundation
into a deterministic PlayStation 2 emulation core. Tasks are ordered by
dependency rather than estimated completion date.

## Guiding Principles

- Keep a cycle-oriented interpreter as the reference implementation.
- Prefer documented hardware behavior and independently verified test vectors.
- Add functionality with red-green TDD: first demonstrate the missing behavior
  with a failing test, then implement it and run the full regression suite.
- Keep emulated hardware deterministic and separate from user interfaces.
- Preserve raw guest data and avoid relying on host floating-point behavior.
- Add optimization only after profiling demonstrates a need.

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

Complete this focused pass before expanding the lower execution unit. Derive
cycle expectations from the VU manual and assert observable issue, writeback,
PC, register, flag, and trace behavior rather than internal container or stage
implementation details.

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

External source and generated fixtures require a license review before being
committed. The expected output should be defined independently of the emulator.
The external program has passed locally; the committed integration fixture will
use independently authored source and expected output.

### Integration Harness and Diagnostics

Build a thin reusable diagnostic layer before the integration programs become
complex, without making a full debugger a prerequisite:

- [x] Add a reusable program runner with a cycle budget and final checks for
      execution state, PC, termination position, cycle count, and data memory
- [x] Capture the existing issue, stall, writeback, and Force Break trace events
- [x] Add optional newline-delimited JSON trace output in the runner/frontend,
      keeping serialization, files, and streams outside the VPU core
- [x] Allow traces to be restricted to a cycle range for focused deterministic
      reruns
- [x] Keep tracing disabled during normal passing runs and make diagnostic
      output available on demand or after a failure

Add richer events such as branch decisions, stall reasons, memory accesses, and
pipeline-stage transitions only when a failing integration test demonstrates a
specific observability gap.

### Original Integration Suite

Prefer several focused programs with attributable failures, followed by one
capstone program that proves the components work together:

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

Build these in the order: integer fill, lane masks, branch paths, vector math,
then the dual-issue capstone. Initially use exactly representable floating-point
inputs such as integers, halves, and powers of two.

Each committed fixture should include:

- Independently authored assembly source
- Its generated raw microprogram binary
- The assembler version and exact regeneration command
- A host-side oracle written independently from the assembly
- Expected output memory and termination PC
- An exact expected cycle count where timing is stable

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

Add arithmetic observability after the operation model is stable and before
floating-point assembly integration:

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
- [ ] After the milestone-driven instruction families are complete, perform
      the final implementation audit: verify decoding, execution, timing,
      hazards, and integration coverage for every architecturally meaningful
      VU0/VU1 microinstruction and deterministic rejection of every reserved
      encoding

### Lower Data Movement and Special State

Keep instructions with external subsystem dependencies in their owning
milestones rather than grouping every non-arithmetic operation as a
"special-register transfer."

- [x] Implement `MFIR`
- [x] Complete basic register movement with `MTIR`, `MOVE`, and `MR32`
- [ ] Complete clipping-flag access with `FCAND`, `FCEQ`, `FCGET`, `FCOR`,
      and `FCSET`
- [ ] Complete MAC-flag access with `FMAND`, `FMEQ`, and `FMOR`
- [ ] Complete status-flag access with `FSAND`, `FSEQ`, `FSOR`, and `FSSET`
- [ ] Complete R-register operations with `RGET`, `RINIT`, `RNEXT`, and
      `RXOR`
- [ ] Add the VU1 P pipeline and EFU instructions together with `MFP` and
      `WAITP`

### Instruction Integration Programs

- [x] Cover the common IALU family through decoded integer, memory, and
      `MFIR` instruction streams
- [x] Cover VU load/store addressing variants and masked lane behavior through
      a decoded memory integration program
- [x] Cover mixed integer, memory, branch, and `MFIR` instruction streams
- [x] Cover `MTIR`, `MOVE`, and `MR32` dependencies and lane behavior
- [ ] Cover clipping, MAC, and status flag tests and setters
- [ ] Cover deterministic R-register initialization, advancement, and transfer
- [ ] Cover Q/P producers and `WAITQ`/`WAITP` synchronization
- [ ] Cover FDIV/EFU resource hazards through decoded instructions
- [x] Cover VU1 `XGKICK` packet initiation, completion stalls, address
      wrapping, and pipeline drain behavior
- [ ] Run progressively larger fragments of naken_asm's `rotation_vu1.asm`

## Milestone 3: VIF and Graphics Path

- [ ] Implement VIF0/VIF1 state and command decoding
- [ ] Support MPG microprogram upload
- [ ] Support UNPACK data transfer into VU memory
- [ ] Support MSCAL/MSCALF execution control
- [ ] Model VIF TOP/ITOP state and implement VU `XTOP`/`XITOP`
- [ ] Decode GIF tags and packed/reglist/image data
- [ ] Route VU1 `XGKICK` output into the GIF path
- [ ] Implement a minimal GS register model
- [ ] Render basic points, lines, and triangles into a software framebuffer
- [ ] Expand toward textures, blending, depth, and display timing

### Graphics Path Integration Programs

- [ ] Upload and execute a VU1 program through VIF `MPG` and `MSCAL`
- [ ] Verify VIF TOP/ITOP values through decoded `XTOP`/`XITOP` instructions
- [ ] Transfer vertex data through VIF `UNPACK`
- [ ] Route a VU1 `XGKICK` packet through GIF into GS registers
- [ ] Render a synthetic triangle and assert its framebuffer hash
- [ ] Add textured, blended, and depth-tested scenes as GS support expands

## Milestone 4: System-Level Core

- [ ] Add a `NekoSystem` owner for hardware components and global state
- [ ] Establish an integer master-clock scheduler
- [ ] Run VUs at 147.456 MHz relative to the 294.912 MHz EE clock
- [ ] Add EE, memory-map, DMA, GIF, GS, and interrupt coordination
- [ ] Add IOP and SPU2 only when required by selected software
- [ ] Define reset, frame execution, input, video, and audio interfaces
- [ ] Add deterministic save-state serialization
- [ ] Add frame hashes and subsystem traces for regression testing

### EE COP2 and VU Macro Mode

- [ ] Implement EE COP2 branches and transfers: `BC2F`, `BC2FL`, `BC2T`,
      `BC2TL`, `CFC2`, `CTC2`, `LQC2`, `QMFC2`, `QMTC2`, and `SQC2`
- [ ] Implement `VCALLMS` and `VCALLMSR` microprogram initiation
- [ ] Decode the VU macro arithmetic, conversion, memory, transfer, random,
      and synchronization instruction families and reuse the corresponding
      microinstruction execution semantics where their architectural behavior
      agrees
- [ ] Audit every macro-mode opcode in the official table and reject reserved
      COP2 encodings deterministically

### System Integration Programs

- [ ] Exercise EE, DMA, VIF, VU1, GIF, and GS in one deterministic workload
- [ ] Cover cross-component clock ratios, interrupts, and DMA completion ordering
- [ ] Cover reset and restart while multiple hardware components are active
- [ ] Round-trip save states during representative workloads
- [ ] Assert stable frame hashes and subsystem traces across repeated runs

The emulation core must not open windows, poll host controllers, sleep, or print
directly. Frontends provide those services through callbacks and buffers.

## Milestone 5: Frontends and libretro

- [ ] Keep the command-line debugger as a separate core consumer
- [ ] Add breakpoints, watchpoints, register views, and memory inspection
- [ ] Add trace export without coupling presentation to hardware classes
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

## Maintaining This File

- Check off work only after tests demonstrate the expected behavior.
- After completing each roadmap subsection or substantial implementation
  block, run `cmake -P cmake/Sanitize.cmake` before moving to the next block.
- Run the sanitizer workflow immediately after changes to ownership, lifetime,
  allocation, or container invalidation behavior.
- Add newly discovered prerequisites to the milestone they block.
- Record major architectural decisions in the relevant section.
- Keep immediate work near the top and long-term ideas intentionally broad.
