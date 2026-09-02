# Neko

Neko is an experimental, deterministic PlayStation 2 emulator written in
C++14. It is under active development and currently focuses on independently
tested EE, VU, VIF, GIF, GS, tracing, save-state, and ELF-loading foundations.
It does not yet boot commercial games or a PlayStation 2 BIOS.

See [ROADMAP.md](ROADMAP.md) for implementation status and planned work.

## Requirements

- CMake 3.20 or newer
- A C++14 compiler:
  - Apple Clang on macOS
  - Visual Studio 2022 on Windows
  - Clang or GCC on Linux
- Git

The optional desktop frontend downloads a pinned SDL3 release during its first
CMake configuration.

## Build

Configure and build the default targets:

```sh
cmake -S . -B out/build -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --config Release
```

The basic `neko` executable is located at:

- macOS/Linux: `./out/build/neko`
- Windows with Visual Studio: `.\out\build\Release\neko.exe`

## Tests

Run the complete optimized, assertion-enabled test and repository check:

```sh
cmake -P cmake/Check.cmake
```

The check uses an isolated `out/check` directory and works with single- and
multi-configuration CMake generators.

Run the memory-safety workflow:

```sh
cmake -P cmake/Sanitize.cmake
```

This uses AddressSanitizer. On macOS it also runs the tests with the native
`leaks` tool.

## Desktop Demo

Build the optional SDL3 frontend:

```sh
cmake -S . -B out/desktop \
  -DCMAKE_BUILD_TYPE=Release \
  -DNEKO_BUILD_DESKTOP=ON
cmake --build out/desktop --target neko_desktop --config Release
```

Run the self-contained graphics showcase:

```sh
# macOS/Linux
./out/desktop/neko_desktop --scene primitives

# Windows with Visual Studio
.\out\desktop\Release\neko_desktop.exe --scene primitives
```

Use `--frames <count>` to stop automatically after a fixed number of frames:

```sh
./out/desktop/neko_desktop --scene primitives --frames 120
```

The `primitives` scene exercises points, lines, strips, fans, sprites, textures,
alpha blending, and depth testing without an external guest binary.

## Optional VU Rotation Sample

The default `rotation` scene uses the external `naken_asm` integration checkout.
If that checkout is available, assemble its VU1 sample first:

```sh
(
  cd local_integration/tools/naken_asm/samples/playstation2
  ../../naken_asm -b -I../../include \
    -o rotation_vu1.bin rotation_vu1.asm
)
./out/desktop/neko_desktop --scene rotation
```

The external assembler and sample remain outside Neko's committed source tree.
