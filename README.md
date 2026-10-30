# Neko

Neko is an experimental, deterministic PlayStation 2 emulator written in
C++14. It is under active development and currently focuses on independently
tested EE, IOP, VU, VIF, GIF, GS, tracing, save-state, and ELF-loading foundations.
It does not yet boot commercial games or complete PlayStation 2 BIOS startup.

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

The headless executable requires a subcommand. Invoking it without arguments
prints usage and exits 2; `--help` and `-h` are not supported.

## Save-State Diagnostics

Inspect the authoritative versioned schema of a save-state file:

```sh
./out/build/neko save-state inspect state.neko
```

Locate one exact schema path or compare two states semantically:

```sh
./out/build/neko save-state locate state.neko system.input.buttons
./out/build/neko save-state diff before.neko after.neko
```

Compare backing IOP RAM with the saved instruction/data cache words and tags:

```sh
./out/build/neko save-state iop-memory state.neko 0x17f00 --words 32
```

This offline view accepts 1-256 aligned words inside physical IOP RAM; it
does not execute instructions or require a BIOS. `match` means the saved
cache line is valid with the requested physical tag, not that it matches RAM.

Inspect bounded EE RAM backing words with saved schema, checksum, cycle and
ROM-fingerprint provenance:

```sh
./out/build/neko save-state ee-memory state.neko 0x7c9c --words 64
```

This read-only view accepts 1-256 aligned physical EE RAM words. It neither
executes instructions nor models the saved instruction-cache/fetch view.

Byte ranges show their size and FNV-1a hash by default. Add
`--bytes <path>` to `inspect` or `diff`, or `--bytes` to `locate`, to include
the selected range's bytes. Invalid files and arguments return status 2;
missing fields and states with semantic differences return status 1.
Inspection does not require external ROM files. Restoring a machine snapshot
requires matching EE and IOP ROM presence and contents independently; ROM bytes
are not embedded. Only the current save-state schema is accepted; schema 48
does not load older snapshots.

Host-side dual-CPU startup snapshots and opt-in bounded-run summaries are
available through the [startup diagnostics API](neko_diagnostics/startup_diagnostics.md).

Run a bounded dual-CPU cold-start diagnostic with an external, legally dumped
4 MiB BIOS:

```sh
./out/check/neko bios-startup path/to/bios.bin --cycles 1000000
```

This headless command releases both reset-vector CPUs and emits JSON lines
identifying the ROM and reporting the actual startup state. Status 0 means
the master-cycle ceiling was reached, 1 means a CPU host stop, and 2 means
an argument, file, or runtime error. None establishes a successful BIOS
handshake or boot. A zero ceiling releases both CPUs without executing.

Add `--trace-events <count>` to export an ordered prefix of the execution trace:

```sh
./out/check/neko bios-startup path/to/bios.bin --cycles 1000000 --trace-events 10000
```

The event count is an explicit collection ceiling, not a CPU execution limit.
Additional JSON lines report capture size/truncation and the retained raw
events. Tracing is off by default; reaching its ceiling does not stop execution.

Inspect saved reports offline with Python 3 (no BIOS or emulator build needed):

```sh
python3 -B tools/inspect_startup.py summary run.jsonl
python3 -B tools/inspect_startup.py events run.jsonl --cpu ee --cycle 41
python3 -B tools/inspect_startup.py instructions run.jsonl --cpu iop
python3 -B tools/inspect_startup.py compare run.jsonl repeat.jsonl --mode trace
```

See the [inspection commands and comparison semantics](neko_diagnostics/startup_diagnostics.md#offline-inspection).

## Tests

Run the complete optimized, assertion-enabled test and repository check:

```sh
cmake -P cmake/Check.cmake
```

The check uses an isolated `out/check` directory and works with single- and
multi-configuration CMake generators. Checks build with four jobs by default
and run the complete suite in four process shards with separate working
directories. Set `NEKO_CHECK_JOBS` (1-64) to change the concurrency, including
fully serial validation:

```sh
cmake -DNEKO_CHECK_JOBS=1 -P cmake/Check.cmake
```

`CMAKE_BUILD_PARALLEL_LEVEL`, when set, overrides build concurrency only.
For ordinary configured builds, `-DNEKO_TEST_JOBS=<count>` controls the shards
used by the `check` target and registered with CTest.

Run focused tests or collect a complete timing/assertion profile:

```sh
./out/check/neko_tests "[test-runner]"
./out/check/neko_tests --reporter profile --out out/check/test-profile.txt
```

Use `./out/check/neko_tests --help` for test-runner options, or
`./out/check/neko_tests --list-test-names-only "[test-runner]"` to preview a
selection. Listing returns the number of listed items, not a test result.
Quote filters and verify that the intended cases run; the bundled matcher
supports `*` only at pattern edges, not inside a name.

The profile lists every test case and source file, sorted by elapsed time,
with assertion totals. Profiling runs serially unless `--shard-count <count>`
and `--shard-index <zero-based-index>` explicitly select one shard.
With multi-configuration generators, use `out/check/Debug/neko_tests`
instead (and append `.exe` on Windows).

Run the memory-safety workflow:

```sh
cmake -P cmake/Sanitize.cmake
```

This runs the complete optimized, assertion-enabled test suite with
AddressSanitizer, using the same concurrency controls.

At milestone completion on macOS, run the complete test suite with the native
`leaks` tool:

```sh
cmake -P cmake/LeakCheck.cmake
```

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

Run a freestanding PS2 ELF without opening an SDL window:

```sh
./out/desktop/neko_desktop --elf path/to/guest.elf
```

The default execution budget is 1,000,000 EE master cycles. Override it with
`--cycles <count>`. The command prints the guest outcome, exit code, cycle and
instruction counts, and final program counter; only a completed guest returns a
successful host exit status.

Run a legally dumped 4 MiB PlayStation 2 BIOS as a bounded startup diagnostic:

```sh
./out/desktop/neko_desktop --bios path/to/bios.bin --cycles 1000000
```

The BIOS image remains external and is never embedded in Neko. The command
installs the image for both processors but starts only the EE; the IOP remains
halted while its timers advance. It prints the first execution stop, including
the final PC, rejected instruction, and pending exception. This is an
architecture-development diagnostic, not a complete BIOS boot path.

Generate tiny synthetic guests for CLI diagnostics:

```sh
python3 tools/create_sample_elf.py return --exit-code 0
python3 tools/create_sample_elf.py return --exit-code 7
python3 tools/create_sample_elf.py loop
python3 tools/create_sample_elf.py exception
```

Generated files are written under the ignored `out/sample_elves/` directory.
They exercise Neko's host runtime contract and are not substitutes for
PS2DEV-generated compatibility fixtures.

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
