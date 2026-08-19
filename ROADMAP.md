# Neko Roadmap

This is a living plan for evolving Neko from its current Vector Unit foundation
into a deterministic PlayStation 2 emulation core. Tasks are ordered by
dependency rather than estimated completion date.

## Guiding Principles

- Keep a cycle-oriented interpreter as the reference implementation.
- Prefer documented hardware behavior and independently verified test vectors.
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

- [ ] Verify independent FMAC instructions issue every cycle without stalls
- [ ] Verify dependent instructions wait through S-stage writeback
- [ ] Verify hazards are independent across x/y/z/w fields
- [ ] Verify `VF00` never generates a data hazard
- [ ] Verify issue and writeback trace ordering for overlapping pipelines
- [ ] Verify flags become visible at the documented writeback stage
- [ ] Verify E delay-slot behavior with multiple active pipelines

### Lower Execution Unit

- [ ] Add lower-instruction decoding and dispatch
- [ ] Model IALU, LSU, and branch execution separately
- [ ] Enforce upper/lower dual-issue restrictions
- [ ] Add integer-register hazard timing

### Branch Control

- [ ] Implement relative and register-based branch targets
- [ ] Implement the one-instruction branch delay slot
- [ ] Reuse deferred-control timing where E termination and branches overlap
- [ ] Verify branch timing alongside integer-register hazards

### Instructions Required by `test_vu0`

- [ ] `IADD`
- [ ] `ISUBIU`
- [ ] `MFIR`
- [ ] `ILW`
- [ ] `LQ`
- [ ] `SQI`
- [ ] `IBNE`

### Integration Test

- [ ] Assemble the source into a raw binary with `naken_asm`
- [ ] Verify instruction byte order and pair layout
- [ ] Load input parameters through the checked data-memory API
- [ ] Execute from address zero with a cycle budget
- [ ] Compare the final VU memory with hand-defined expected qwords
- [ ] Optionally compare a trace with PCSX2 or real hardware

External source and generated fixtures require a license review before being
committed. The expected output should be defined independently of the emulator.

## Milestone 2: VU Execution Accuracy

### Pipeline Model

- [ ] Replace the monolithic run loop with a system-schedulable clock interface
- [ ] Give FMAC stages explicit timing behavior
- [ ] Add IALU, LSU, branch, FDIV, EFU, and XGKICK pipelines
- [ ] Model register, flag, and special-register availability timing
- [ ] Validate stalls and forwarding behavior from manual examples
- [ ] Add structural hazards and pipeline-specific synchronization

### VU Floating Point

The current host-`double` compatibility layer is useful scaffolding but is not
bit-accurate VU arithmetic.

- [ ] Build raw-bit operation APIs returning result bits and exception flags
- [ ] Treat exponent-zero inputs as signed zero during calculations
- [ ] Treat exponent-255 encodings as finite VU values
- [ ] Correct exponent overflow and underflow detection
- [ ] Implement VU 24-bit truncating add, subtract, and multiply
- [ ] Implement division and square-root exception behavior
- [ ] Correct `0 / 0` to return signed `MAX` with the I flag
- [ ] Implement truncating fixed-point conversions
- [ ] Validate edge cases against the VU manual and hardware-derived vectors
- [ ] Keep the interpreter implementation as an oracle for any future fast path

### Instruction Coverage

- [ ] Complete commonly used lower integer instructions
- [ ] Complete VU memory load/store variants and lane masks
- [ ] Complete branches, jumps, and special-register transfers
- [ ] Add Q pipeline division and square-root operations
- [ ] Add the VU1 P pipeline and EFU instructions
- [ ] Add VU1 `XGKICK`

## Milestone 3: VIF and Graphics Path

- [ ] Implement VIF0/VIF1 state and command decoding
- [ ] Support MPG microprogram upload
- [ ] Support UNPACK data transfer into VU memory
- [ ] Support MSCAL/MSCALF execution control
- [ ] Decode GIF tags and packed/reglist/image data
- [ ] Route VU1 `XGKICK` output into the GIF path
- [ ] Implement a minimal GS register model
- [ ] Render basic points, lines, and triangles into a software framebuffer
- [ ] Expand toward textures, blending, depth, and display timing

## Milestone 4: System-Level Core

- [ ] Add a `NekoSystem` owner for hardware components and global state
- [ ] Establish an integer master-clock scheduler
- [ ] Run VUs at 147.456 MHz relative to the 294.912 MHz EE clock
- [ ] Add EE, memory-map, DMA, GIF, GS, and interrupt coordination
- [ ] Add IOP and SPU2 only when required by selected software
- [ ] Define reset, frame execution, input, video, and audio interfaces
- [ ] Add deterministic save-state serialization
- [ ] Add frame hashes and subsystem traces for regression testing

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

## Performance Strategy

1. Preserve correctness and determinism.
2. Profile realistic programs.
3. Predecode microinstructions and remove avoidable allocation.
4. Use compact array-based pipeline storage and efficient dispatch.
5. Consider an optional recompiler only if the reference interpreter cannot
   meet the required performance.

Any optimized execution path must be continuously compared against the
cycle-oriented interpreter.

## Maintaining This File

- Check off work only after tests demonstrate the expected behavior.
- Add newly discovered prerequisites to the milestone they block.
- Record major architectural decisions in the relevant section.
- Keep immediate work near the top and long-term ideas intentionally broad.
