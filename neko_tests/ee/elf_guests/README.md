# EE ELF Guest Fixtures

These original Neko assembly programs are compiled as freestanding PS2 EE
executables. Their generated ELF files are committed so normal builds and tests
do not require PS2DEV.

The fixtures exercise:

- `arithmetic.elf`: integer arithmetic, signed comparison, and shifts
- `branches.elf`: taken delay slots and branch-likely annulment
- `memory.elf`: stack placement and 32/64-bit loads and stores
- `mmio.elf`: 32-bit INTC/DMAC registers and a 64-bit GS privileged write
- `fifo.elf`: 128-bit `SQ` writes to the VIF0, VIF1, and GIF FIFOs
- `vif1_dma.elf`: guest-configured VIF1 DMA completion and interrupt status
- `cop1_semantics.elf`: timing-independent scalar COP1 raw results and FCR31
- `cop1_transfer_memory.elf`: raw COP1 register transfers, moves, loads, stores,
  and memory round trips
- `cop1_control_state.elf`: `FCR31` condition, cause, sticky, fixed, and
  writable-field control transfers
- `cop1_comparison_branches.elf`: every scalar comparison and taken, untaken,
  delay-slot, and branch-likely-annulled COP1 paths
- `cop1_conversion_unary.elf`: signed-zero and finite unary operations, signed
  word conversions, clamp saturation, and conversion flag transitions
- `cop1_basic_arithmetic.elf`: normal and exceptional add, subtract, multiply,
  minimum, and maximum results with ordered overflow/underflow flags
- `cop1_accumulator_compound.elf`: all accumulator and compound arithmetic
  forms, immediate ACC forwarding, and ordered product/final flags
- `cop1_dividers.elf`: overlapping `RSQRT.S`, `DIV.S`, and `SQRT.S`
  initiation with delayed result retirement
- `cop2_transfer.elf`: 128-bit EE memory/GPR transfers through VU0 registers
- `cop2_control.elf`: VU0 control transfers and VU1 status branches
- `vcallms.elf`: VU0 microprogram initiation through `VCALLMS` and `VCALLMSR`
- `vu_macro_arithmetic.elf`: pipelined VU0 `VADD`/`VSUB` macro arithmetic
- `vu_macro_families.elf`: VU0 macro multiply, min/max, conversion, movement,
  and integer arithmetic families
- `rotation_vu1.elf`: guest-configured VIF1 DMA uploads a VU1 transform,
  unpacks a rotated triangle and GIF packet, starts it through `MSCAL`, and
  renders through `XGKICK` and GIF PATH1
- `point_sprite.elf`: guest-generated POINT and SPRITE packets render through
  the mapped GIF PATH3 FIFO

Each guest returns zero through `$v0` on success or a small diagnostic code on
failure, then returns through `$ra` to Neko's host sentinel.

The current binaries were generated with:

- `mips64r5900el-ps2-elf-gcc` 15.2.0
- GNU Binutils 2.45.1

Regenerate them from the repository root:

```sh
source local_integration/ps2dev-env.sh
neko_tests/ee/elf_guests/build.sh
```

Expected SHA-256 hashes:

```text
46c95fa1436048f03def12419bd264f5e60da6b4a27bd1c90fef248219f211ff  arithmetic.elf
d1aaa13f446f6f04d0f16eb929ced8d5f74d39bc0bc099ee158158c386940d8f  branches.elf
459306d7cbfd0d4d52b8fc969e89b1be0e17f2b25ec616afe4e9440197aaeca8  memory.elf
6cee1dbb9db0d422882d981a351516d6ac8e170e88736a89ef26f9ff3ecd0488  mmio.elf
0649d2f7dd8dd396ff1f45d4fa962b1bed53044a9972dbc97f7e3fb846161fae  fifo.elf
df6b2b4ff832f6fe4b9d701306e6077673d85d6daea2b319dcc0fb8af12fef20  vif1_dma.elf
b6a47315e2de5b39315b88a1e0fdc7a4d2aeceb1a7654a430f4f9b3b6f9de07d  cop1_semantics.elf
7203bc4bde83e64f41fe9dafe88c1992ee4ec5b21e052df3d8321270ea9a223f  cop1_transfer_memory.elf
05655052ceeb43a33457433795b6144ef4da47c2b6e6dd2106ef8192a0154470  cop1_control_state.elf
4af9a966074b2323a43cd34c4b7e24e6a139af37900edc94ee602647dede61a1  cop1_comparison_branches.elf
780183dbbabdea2f939e321eaf5cb464821e764fcced5c2906a96a9a4ade05cc  cop1_conversion_unary.elf
ac805378054f1fcb762f1d8d428f71a4710da04dd587ccc606ec34175d47ac78  cop1_basic_arithmetic.elf
763c6ebcaa23d8acce500062aea7e559c0f07d48b3d2b2696a2c8540f85b1c07  cop1_accumulator_compound.elf
e9a77add85add831a78dba34ec341af8afbc4b63add48b188860113a8ab4f148  cop1_dividers.elf
9e570b58b1fc785330632b45ea8ef26eb6a21c86d06dabc6bd2140993eee4707  cop2_transfer.elf
f15ea0eed6405daad9b672575db828e686987be85dabd73cc05bdb0798c4b386  cop2_control.elf
d19d78bccfe393bcacc222a82cdd06954e5c7fd4b897fc105ed475dacde986f7  vcallms.elf
7fd2baf8ba09bc8c43dff19d6fa0d8e48cff9f182911dbf0334a8fd673aa5c2d  vu_macro_arithmetic.elf
f09d304e80cc06bef0648a2012b6fd18767c85f43db582c2079d8f02f2b96d88  vu_macro_families.elf
15a69f9505959eb2be7c52f38a525465ac26ce3d55b353e8a23eda6d91478adf  rotation_vu1.elf
40ceb21e5e43c654098b08ea7c81e74a287048a8272cbbd8abb0d3c4eb61a403  point_sprite.elf
```

Inspect a generated fixture with:

```sh
mips64r5900el-ps2-elf-readelf -h -l \
  neko_tests/ee/elf_guests/arithmetic.elf
mips64r5900el-ps2-elf-objdump -d \
  neko_tests/ee/elf_guests/arithmetic.elf
```
