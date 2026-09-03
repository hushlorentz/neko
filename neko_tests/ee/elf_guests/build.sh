#!/bin/sh

set -eu

if [ -z "${PS2DEV:-}" ]; then
  echo "PS2DEV is not set. Source local_integration/ps2dev-env.sh first." >&2
  exit 1
fi

compiler="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc"
directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

for guest in arithmetic branches memory mmio fifo vif1_dma cop2_transfer cop2_control vcallms vu_macro_arithmetic vu_macro_families rotation_vu1; do
  object="$directory/$guest.o"
  (
    cd "$directory"
    "$compiler" \
      -x assembler-with-cpp \
      -c \
      -o "$object" \
      "$guest.S"
  )
  "$compiler" \
    -nostdlib \
    -Wl,-e,_start \
    -Wl,--build-id=none \
    -o "$directory/$guest.elf" \
    "$object"
  rm -f "$object"
done
