#!/bin/sh

set -eu

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 <guest-name> [symbol ...]" >&2
  exit 2
fi

if [ -z "${PS2DEV:-}" ]; then
  echo "PS2DEV is not set. Source local_integration/ps2dev-env.sh first." >&2
  exit 1
fi

guest=$1
shift
case "$guest" in
  *[!A-Za-z0-9_]*)
    echo "Invalid EE ELF guest name: $guest" >&2
    exit 2
    ;;
esac

directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_file="$directory/$guest.S"
fixture="$directory/$guest.elf"
readelf="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-readelf"

if [ ! -f "$source_file" ]; then
  echo "Missing EE ELF guest source: $source_file" >&2
  exit 1
fi
if [ ! -f "$fixture" ]; then
  echo "Missing EE ELF guest fixture: $fixture" >&2
  exit 1
fi
if [ ! -x "$readelf" ]; then
  echo "Missing PS2DEV readelf: $readelf" >&2
  exit 1
fi

if [ "$#" -eq 0 ]; then
  set -- _start
fi
for symbol do
  case "$symbol" in
    *[!A-Za-z0-9_.$]*)
      echo "Invalid ELF symbol name: $symbol" >&2
      exit 2
      ;;
  esac
done

printf '%s\n' "ELF: $fixture"
printf '\nProgram headers:\n'
"$readelf" -lW "$fixture"

printf '\nRelevant sections:\n'
"$readelf" -SW "$fixture" |
  awk '
    /Section Headers:/ ||
    /\.text([[:space:]]|$)/ ||
    /\.data([[:space:]]|$)/ ||
    /\.bss([[:space:]]|$)/ ||
    /Key to Flags:/
  '

requested_symbols=$(printf '%s ' "$@")
printf '\nRequested symbols:\n'
"$readelf" -sW "$fixture" |
  awk -v requested="$requested_symbols" '
    BEGIN {
      count = split(requested, names, /[[:space:]]+/)
      for (i = 1; i <= count; ++i) {
        wanted[names[i]] = 1
      }
    }
    ($NF in wanted) {
      print
      found[$NF] = 1
    }
    END {
      missing = 0
      for (name in wanted) {
        if (!(name in found)) {
          print "Missing ELF symbol: " name > "/dev/stderr"
          missing = 1
        }
      }
      exit missing
    }
  '
