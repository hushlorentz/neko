# VU Integration Fixtures

The assembly programs in this directory are original Neko test fixtures. Their
raw binaries are committed so the test suite does not require an assembler.

The binaries are generated with `naken_asm` commit
`247c23706909f09bac77c587780b8a826bbda27c` (version `May 30, 2026`).

From the repository root, regenerate `integer_fill.bin` with:

```sh
local_integration/tools/naken_asm/naken_asm \
  -b \
  -o neko_tests/vpu/integration/integer_fill.bin \
  neko_tests/vpu/integration/integer_fill.asm
```

Expected SHA-256:

```text
45d00e599bdcfe54128bc58cba4d565c851bb523e0ba3a637c1a6782550fae52
```

Regenerate `lane_masks.bin` with:

```sh
local_integration/tools/naken_asm/naken_asm \
  -b \
  -o neko_tests/vpu/integration/lane_masks.bin \
  neko_tests/vpu/integration/lane_masks.asm
```

Expected SHA-256:

```text
41e29c6379fbbd55747539b13b467cb8e6248b45ae712de9968965a471b9c23a
```

The C++ test computes expected memory independently rather than reproducing the
assembly implementation.
