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

Regenerate `branch_paths.bin` with:

```sh
local_integration/tools/naken_asm/naken_asm \
  -b \
  -o neko_tests/vpu/integration/branch_paths.bin \
  neko_tests/vpu/integration/branch_paths.asm
```

Expected SHA-256:

```text
ee4643e517cb599fc0d654e74bd126bf0e980434ac545333b81ce6f34b992c45
```

Regenerate `indirect_calls.bin` with:

```sh
local_integration/tools/naken_asm/naken_asm \
  -b \
  -o neko_tests/vpu/integration/indirect_calls.bin \
  neko_tests/vpu/integration/indirect_calls.asm
```

Expected SHA-256:

```text
dbde6ab0815c053b766f3aa57a84fe47c45ec89a20c32833c05a9fad130828df
```

Regenerate `vector_math.bin` with:

```sh
local_integration/tools/naken_asm/naken_asm \
  -b \
  -o neko_tests/vpu/integration/vector_math.bin \
  neko_tests/vpu/integration/vector_math.asm
```

Expected SHA-256:

```text
2cf3f043920d723f82d3b8157abe4f6909e3e4566baff3d533c2a491a97b28d0
```

Regenerate `dual_issue.bin` with:

```sh
local_integration/tools/naken_asm/naken_asm \
  -b \
  -o neko_tests/vpu/integration/dual_issue.bin \
  neko_tests/vpu/integration/dual_issue.asm
```

Expected SHA-256:

```text
70acf8379442ac7f545d3273bbb5c34cdcf9cdceff74aed87efc3dd2b9cb5bb9
```

Regenerate `termination.bin` with:

```sh
local_integration/tools/naken_asm/naken_asm \
  -b \
  -o neko_tests/vpu/integration/termination.bin \
  neko_tests/vpu/integration/termination.asm
```

Expected SHA-256:

```text
3618324b61cedc436579ec6a9de2f29be3440185fc2cbdf99e780c4bfdfbbb21
```

The C++ test computes expected memory independently rather than reproducing the
assembly implementation.
