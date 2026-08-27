# neko
Just a fun project to work on

See [ROADMAP.md](ROADMAP.md) for the current implementation plan.

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
