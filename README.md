# neko
Just a fun project to work on

See [ROADMAP.md](ROADMAP.md) for the current implementation plan.

`NekoSystem` is the host-facing machine boundary. It accepts a latched
controller state, executes to the next GS presentation boundary with
`runFrame()`, returns video and 48 kHz stereo audio payloads, and resets all
hardware and internal wiring together with `reset()`. Audio remains empty
until SPU2 is required and implemented.

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
