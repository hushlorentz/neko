# EE ELF Guest Fixtures

These original Neko assembly programs are compiled as freestanding PS2 EE
executables. Their generated ELF files are committed so normal builds and tests
do not require PS2DEV.

The fixtures exercise:

- `arithmetic.elf`: integer arithmetic, signed comparison, and shifts
- `branches.elf`: taken delay slots and branch-likely annulment
- `memory.elf`: stack placement and 32/64-bit loads and stores
- `mmio.elf`: 32-bit INTC/DMAC registers and a 64-bit GS privileged write

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
```

Inspect a generated fixture with:

```sh
mips64r5900el-ps2-elf-readelf -h -l \
  neko_tests/ee/elf_guests/arithmetic.elf
mips64r5900el-ps2-elf-objdump -d \
  neko_tests/ee/elf_guests/arithmetic.elf
```
