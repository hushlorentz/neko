#!/bin/sh

set -eu

if [ -z "${PS2DEV:-}" ]; then
  echo "PS2DEV is not set. Source local_integration/ps2dev-env.sh first." >&2
  exit 1
fi

compiler="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc"
directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
guests="arithmetic branches memory mmio fifo vif1_dma cop0_tlb_management mapped_memory scratchpad_dma cache_workflow cop1_semantics cop1_transfer_memory cop1_control_state cop1_comparison_branches cop1_conversion_unary cop1_basic_arithmetic cop1_accumulator_compound cop1_dividers cop1_mixed_concurrent cop2_transfer cop2_control vcallms vu_macro_arithmetic vu_macro_families mmi_arithmetic mmi_permutations mmi_hilo mmi_mixed rotation_vu1 point_sprite"

if [ "$#" -eq 0 ]; then
  set -- $guests
fi

for guest do
  case " $guests " in
    *" $guest "*)
      ;;
    *)
      echo "Unknown EE ELF guest: $guest" >&2
      exit 2
      ;;
  esac

  object="$directory/$guest.o"
  cleanup()
  {
    rm -f "$object"
  }
  trap cleanup 0
  trap 'exit 1' HUP INT TERM

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
  cleanup
  trap - 0 HUP INT TERM
done
