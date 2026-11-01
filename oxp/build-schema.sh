#!/bin/sh
# Optional DOM mutation/XSD build. MIT, Copyright (c) 2026 AmiCradle Team.
# Does not change the original streaming parser build or require schemas there.
# Usage: sh oxp/build-schema.sh host|amiga
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MODE=${1:-host}
case "$MODE" in
 host) CC=${CC:-cc}; CXX=${CXX:-c++}; AR=${AR:-ar}; CPU= ;;
 amiga)
  STOVE_ROOT=${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}
  CC=$STOVE_ROOT/prefix/bin/m68k-amigaos-gcc
  CXX=$STOVE_ROOT/prefix/bin/m68k-amigaos-g++
  AR=$STOVE_ROOT/prefix/bin/m68k-amigaos-ar
  CPU="-m68040 -specs=$HERE/../toolchain/m68k-soft-m68040.specs -mcrt=nix20 -pthread -fno-delete-null-pointer-checks"
  ;;
 *) echo 'Use host or amiga' >&2; exit 2 ;;
esac
OUT=${OUT:-"$HERE/build-schema-$MODE"}
mkdir -p "$OUT/lib" "$OUT/include" "$OUT/bin" "$OUT/obj"
FLAGS="-O2 -Wall -Wextra -Werror $CPU ${EXTRA_FLAGS:-} -I$HERE/include"
$CC $FLAGS -std=c99 -c "$HERE/src/oxp.c" -o "$OUT/obj/oxp.o"
$CC $FLAGS -std=c99 -c "$HERE/src/oxp_dom.c" -o "$OUT/obj/oxp_dom.o"
$CXX $FLAGS -std=c++17 -c "$HERE/src/oxp_schema.cpp" -o "$OUT/obj/oxp_schema.o"
$AR rcs "$OUT/lib/liboxp.a" "$OUT/obj/oxp.o" "$OUT/obj/oxp_dom.o"
$AR rcs "$OUT/lib/liboxpschema.a" "$OUT/obj/oxp_schema.o"
cp "$HERE/include/oxp.h" "$HERE/include/oxp_dom.h" "$HERE/include/oxp_schema.h" "$OUT/include/"
for test in mutation_schema_test schema_check; do
 $CXX $FLAGS -std=c++17 "$HERE/tests/$test.cpp" -L"$OUT/lib" -loxpschema -loxp -lm -o "$OUT/bin/$test"
done
if [ "$MODE" = host ]; then "$OUT/bin/mutation_schema_test"; fi
