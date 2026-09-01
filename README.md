# neko
Just a fun project to work on

See [ROADMAP.md](ROADMAP.md) for the current implementation plan.

`NekoSystem` is the host-facing machine boundary. It accepts a latched
controller state, executes to the next GS presentation boundary with
`runFrame()`, returns video and 48 kHz stereo audio payloads, and resets all
hardware and internal wiring together with `reset()`. Audio remains empty
until SPU2 is required and implemented.

The initial EE Core foundation models all 32 128-bit R5900 general-purpose
registers, the 32-bit program counter, both `HI`/`LO` pairs, and the `SA`
register. Register zero is immutable, and the complete architectural state
participates in reset and save-state restoration. Instruction fetches use
little-endian EE RAM and its aliases; misaligned and unmapped fetches become
typed pending EE exceptions rather than host errors. A table-driven decoder
currently recognizes the base integer arithmetic, comparison, logic, and shift
families and distinguishes reserved encodings from deferred instruction
families. The EE is a halted-by-default master-clock component; while running,
each 294.912 MHz master cycle performs one fetch/decode step and records an
explicit stop reason if execution cannot continue. The first execution family
implements integer arithmetic, comparisons, logic, and 32/64-bit shifts while
preserving the upper 64 bits of each 128-bit GPR. Trapping overflow and
manual-defined undefined word operands stop without writing the destination.
The integer MAC slice implements signed and unsigned `MULT`, `MADD`, and
`DIV` families on both `HI`/`LO` pipelines, three-operand low-product
writeback, register transfers, and `SA` moves/count calculations. Multiply
results become visible after the documented 4 cycles and divide results after
37 cycles, including the signed-minimum divide case. The current single-issue
reference model conservatively blocks later issue during those cycles;
independent instruction overlap and throughput belong to the future
superscalar timing model. In-flight results and their remaining latency are
part of save states.
Base EE control flow now includes PC-relative conditional branches, branch-
likely annulment, region-relative jumps, register jumps, and all corresponding
link forms. Every taken branch and every non-likely fallthrough executes one
architectural delay-slot instruction before changing control flow. Forbidden
branch-in-delay-slot and branch-likely/`SA` combinations stop explicitly, while
misaligned register targets raise the fetch exception only when target fetch
begins. Pending delay-slot state is serialized so save-state continuation
cannot skip or repeat the slot.
The first EE data-memory instructions implement `LB`, `LBU`, and `SB`.
Effective addresses use the low 32 bits of the base-plus-signed-offset result,
matching the EE's 32-bit virtual address implementation. Byte loads sign- or
zero-extend into GPR bits 63..0, byte stores use the source's least-significant
byte, and all preserve unrelated register bits. RAM aliases participate in
data access, while unmapped loads and stores stop with distinct typed data-bus
exceptions and preserve faulting architectural state. Save-state format
version 6 includes those exception values. A fault in a branch delay slot
rewinds execution to the preceding restartable branch while retaining the
faulting data address.
Aligned halfword access adds `LH`, `LHU`, and `SH`. Halfword loads assemble
little-endian data and sign- or zero-extend it through GPR bits 63..0; stores
write only the least-significant 16 bits. Odd effective addresses stop before
the bus access with the appropriate load or store address-error state, so
faulting stores cannot partially modify memory. Save-state format version 7
includes the new store-address exception.
Aligned word access adds `LW`, `LWU`, and `SW` through the same checked RAM
data boundary. Loads assemble little-endian words and apply the documented
sign or zero extension; stores write only GPR bits 31..0. Four-byte alignment,
RAM-end boundaries, delay-slot execution, and fault continuation are covered
without changing the version 7 save-state layout.
Unaligned 32-bit transfers add `LWL`, `LWR`, `SWL`, and `SWR`. Their merge
behavior follows the manual's little-endian byte-position tables for all four
effective-address offsets. `LWL` sign-extends the merged word, while `LWR`
preserves GPR bits 63..32 unless it loads the word's sign bit. Paired merge
instructions can transfer an arbitrary unaligned word, and save states preserve
the intermediate destination between the pair.
Aligned doubleword access adds `LD` and `SD` through checked little-endian
64-bit RAM operations. Eight-byte alignment and RAM bounds are validated
before access; loads replace GPR bits 63..0 while preserving bits 127..64, and
stores use only the source's low doubleword. Address and data-bus faults remain
restartable without changing the version 7 save-state layout.

Each `runFrame()` result includes a canonical video hash. Optional
`NekoSystem` regression tracing records ordered, master-cycle-stamped input,
VU, VIF, GIF, DMA, GS, interrupt, and presentation transitions; trace hashes
can compare repeated runs or save-state continuations without frontend state.

`NekoSystem::saveState()` returns a canonical, versioned byte vector containing
the complete deterministic machine state, and `loadState()` restores one
transactionally. The checksummed format is explicit little-endian data rather
than an object-memory dump; internal pointers and diagnostic callbacks are
never serialized. Invalid, corrupted, truncated, trailing, or incompatible
input throws without changing the live machine.

Configure, build, and run the complete developer validation workflow with:

```sh
cmake -P cmake/Check.cmake
```

The command uses the platform's default CMake generator and an isolated
`out/check` directory. It builds and runs the test suite, checks staged and
unstaged Git diffs for whitespace errors, and verifies the committed VU
integration fixture hashes.

Run the memory-safety workflow with:

```sh
cmake -P cmake/Sanitize.cmake
```

This uses an isolated `out/sanitize` build with AddressSanitizer. On macOS it
also runs the test suite under the native `leaks` tool. Keep the normal check
as the fast development loop and run the sanitizer workflow before merging
memory-management changes and in continuous integration.

Assemble the external rotation sample, then build and show its
VU1/XGKICK/GIF/GS output in the optional SDL3 desktop frontend with:

```sh
(
  cd local_integration/tools/naken_asm/samples/playstation2
  ../../naken_asm -b -I../../include \
    -o rotation_vu1.bin rotation_vu1.asm
)
cmake -S . -B out/desktop \
  -DCMAKE_BUILD_TYPE=Release \
  -DNEKO_BUILD_DESKTOP=ON
cmake --build out/desktop --target neko_desktop
./out/desktop/neko_desktop
```

Run the animated primitive scene without the external VU binary:

```sh
./out/desktop/neko_desktop --scene primitives
```

SDL3 is fetched at a pinned release only when the desktop option is enabled.
The external sample and assembler remain outside the committed source tree.
The default desktop scene supplies deterministic sine/cosine inputs and reruns
the VU1 workload for every animation frame. The `primitives` scene submits
deterministic host-fed PATH3 packets for POINT, LINE, LINESTRIP, and SPRITE
rasterization, plus strips, fans, and a PATH3 IMAGE-uploaded PSMCT32 texture.
Content-specific scene names preserve earlier versions:

```sh
./out/desktop/neko_desktop --scene points-sprites
./out/desktop/neko_desktop --scene points-lines-sprites
./out/desktop/neko_desktop --scene points-lines-sprites-strips-fans
./out/desktop/neko_desktop --scene points-lines-sprites-strips-fans-textures
./out/desktop/neko_desktop --scene points-lines-sprites-strips-fans-textures-alpha
./out/desktop/neko_desktop --scene points-lines-sprites-strips-fans-textures-alpha-depth
```

The `primitives` name tracks the newest graphics showcase as more primitive
families are added. The latest version adds crossing translucent ribbons whose
interpolated Z values determine which surface appears in front. Normal builds
and `neko_core` do not depend on SDL.
