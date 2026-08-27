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

Regenerate `common_integer.bin` with:

```sh
local_integration/tools/naken_asm/naken_asm \
  -b \
  -o neko_tests/vpu/integration/common_integer.bin \
  neko_tests/vpu/integration/common_integer.asm
```

Expected SHA-256:

```text
4c5f247ab39053142ca9be9652caffd98d38a5417e8655856fea1d52073fb46d
```

Regenerate `memory_variants.bin` with:

```sh
local_integration/tools/naken_asm/naken_asm \
  -b \
  -o neko_tests/vpu/integration/memory_variants.bin \
  neko_tests/vpu/integration/memory_variants.asm
```

Expected SHA-256:

```text
d0be0bcc8129c3e592282807c242468b9daa5afda9a919d7e31edf84ea0ad786
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

Regenerate `branch_family.bin` with:

```sh
local_integration/tools/naken_asm/naken_asm \
  -b \
  -o neko_tests/vpu/integration/branch_family.bin \
  neko_tests/vpu/integration/branch_family.asm
```

Expected SHA-256:

```text
bada7ef8bea9dc2e3d2a363b2906f64ad5d539cc0a3afa9780d6510de6aa1964
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

Regenerate `vector_kernel.bin` with:

```sh
local_integration/tools/naken_asm/naken_asm \
  -b \
  -o neko_tests/vpu/integration/vector_kernel.bin \
  neko_tests/vpu/integration/vector_kernel.asm
```

Expected SHA-256:

```text
e5294f1866952531f320fa7306a4a772a80500bb6020d18f822656da295cb971
```

The C++ test computes expected memory independently rather than reproducing the
assembly implementation.

Regenerate the pipeline integration fixtures with:

```sh
for fixture in \
  pipeline_acc_overlap \
  pipeline_integer_control \
  pipeline_loi_timing \
  pipeline_termination_drain
do
  local_integration/tools/naken_asm/naken_asm \
    -b \
    -o "neko_tests/vpu/integration/${fixture}.bin" \
    "neko_tests/vpu/integration/${fixture}.asm"
done
```

Expected SHA-256 values:

```text
pipeline_acc_overlap.bin       e1eca2dab10db0aa967f4e2c8044e0dd3508acfd9ac688838f852ff2ff300a27
pipeline_integer_control.bin   7321275edc8c4cfd331d7350f06b0e0ad2c781d783534b5f849ce65be4a5b216
pipeline_loi_timing.bin        01a78ff81e6dea8e2d8993c87d7105d002a085235c6d67082add9a3fa7223227
pipeline_termination_drain.bin f0b165309aaab04653ba8dbedd9a3d8360500eb46d3e130342bbf7ea2d949466
```

Regenerate the floating-point integration fixtures with:

```sh
for fixture in \
  floating_point_truncation \
  floating_point_exceptions \
  floating_point_compound \
  fixed_point_conversions \
  q_pipeline
do
  local_integration/tools/naken_asm/naken_asm \
    -b \
    -o "neko_tests/vpu/integration/${fixture}.bin" \
    "neko_tests/vpu/integration/${fixture}.asm"
done
```

Expected SHA-256 values:

```text
floating_point_truncation.bin b5e0ab57cda49059f30205cd4cd001aeb55dff820424e898f6031304ecb59940
floating_point_exceptions.bin dad420fa5f4c134fbfe12308be2a79dbadfc9e72b810754c97cde7b8a27d79c2
floating_point_compound.bin   d8fdaf73e48383eb5a857ce6159420871591ed884ae9d78971f4d5059abab2f4
fixed_point_conversions.bin   3c1594daf9c09d02701979295ccee63c7c06caa722bf6ca88410ab403261ec6f
q_pipeline.bin                6421afcad9ae775630c93af16320369c6984dae51286ca89027ee404bf2d73d6
```
