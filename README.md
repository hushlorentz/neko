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
