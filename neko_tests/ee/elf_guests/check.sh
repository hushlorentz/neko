#!/bin/sh

set -eu

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 <guest-name> <Catch2-filter>" >&2
  exit 2
fi

guest=$1
test_filter=$2
directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository=$(CDPATH= cd -- "$directory/../../.." && pwd)
build_directory="$repository/out/check"

"$directory/build.sh" "$guest"

object="$directory/$guest.o"
if [ -e "$object" ]; then
  echo "Temporary object was not removed: $object" >&2
  exit 1
fi

fixture="$directory/$guest.elf"
if command -v shasum >/dev/null 2>&1; then
  shasum -a 256 "$fixture"
elif command -v sha256sum >/dev/null 2>&1; then
  sha256sum "$fixture"
else
  echo "Neither shasum nor sha256sum is available." >&2
  exit 1
fi

if [ ! -f "$build_directory/CMakeCache.txt" ]; then
  cmake \
    -S "$repository" \
    -B "$build_directory" \
    -D CMAKE_BUILD_TYPE=Debug \
    -D NEKO_OPTIMIZE_CHECKS=ON
fi

cmake \
  --build "$build_directory" \
  --target neko_tests \
  --config Debug \
  --parallel 2

test_binary="$build_directory/neko_tests"
if [ ! -x "$test_binary" ]; then
  test_binary="$build_directory/Debug/neko_tests"
fi
if [ ! -x "$test_binary" ]; then
  echo "Could not find the built neko_tests executable." >&2
  exit 1
fi

"$test_binary" "$test_filter"
