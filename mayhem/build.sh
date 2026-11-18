#!/usr/bin/env bash
#
# mayhem/build.sh — builds the intel-cmt-cat fuzz targets and its unit-test suite.
#
#   1. upstream's cmocka unit tests (the oracle mayhem/test.sh runs) and the pqos binary, with the
#      project's normal flags, in the tree (their objects are all gitignored upstream);
#   2. libpqos.a with $SANITIZER_FLAGS + $DEBUG_FLAGS, built in a copy under mayhem/build/ (gitignored),
#      linked into the libFuzzer harnesses fuzz_pci_ids, fuzz_erdt, fuzz_mrrm and their -standalone twins;
#   3. libpqos.a and the pqos command-line tool built with afl-clang-fast and the same sanitizers, the AFL
#      target /mayhem/pqos_config (`pqos -f <config file>`).
#
# Runs offline and can be run again on a built tree.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
B="$SRC/mayhem/build"

# 1) The test suite, with the project's normal flags. unit-test/ builds libpqos.so in lib/ and its own
#    objects and binaries under unit-test/*/{obj,bin}; the pqos tool is built against that libpqos.so.
TEST_CC="$CC${COVERAGE_FLAGS:+ $COVERAGE_FLAGS}"
make -C unit-test -j"$MAYHEM_JOBS" all CC="$TEST_CC"
make -C pqos -j"$MAYHEM_JOBS" CC="$TEST_CC"

# 2) libpqos.a for the libFuzzer harnesses. Upstream's own Makefile, on a copy of the sources with the
#    build products of step 1 removed, so nothing of that normal-flags build ends up in the sanitized one.
copy_sources() {
  rm -rf "$1"
  mkdir -p "$1"
  cp -a lib pqos pre-build.mk flags-stamp.mk "$1"/
  find "$1" \( -name '*.o' -o -name '*.d' -o -name '*.a' -o -name '*.so' -o -name '*.so.*' \
       -o -name .build-flags -o -name obj \) -prune -exec rm -rf {} +
  rm -f "$1/pqos/pqos"
}

# -U_FORTIFY_SOURCE does not survive the Makefile's own -D_FORTIFY_SOURCE=2, which comes after
# EXTRA_CFLAGS; it is harmless with the sanitizers and open() is not rewritten without O_CREAT.
mkdir -p "$B"
LSAN_OFF="$B/lsan_off.o"
"$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$LSAN_OFF"

copy_sources "$B/san"
make -C "$B/san/lib" -j"$MAYHEM_JOBS" SHARED=n CC="$CC" \
  EXTRA_CFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS" libpqos.a

WRAP="-Wl,--wrap=access -Wl,--wrap=open -Wl,--wrap=__open_2"
for t in fuzz_pci_ids fuzz_erdt fuzz_mrrm; do
  extra=()
  [ "$t" = fuzz_pci_ids ] || extra=("$SRC/mayhem/acpi_mock.c" $WRAP)
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS -I"$B/san/lib" -I"$SRC/mayhem" $LIB_FUZZING_ENGINE \
      "$SRC/mayhem/$t.c" "${extra[@]}" "$LSAN_OFF" "$B/san/lib/libpqos.a" -lpthread -o "$SRC/$t"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS -I"$B/san/lib" -I"$SRC/mayhem" "$STANDALONE_FUZZ_MAIN" \
      "$SRC/mayhem/$t.c" "${extra[@]}" "$LSAN_OFF" "$B/san/lib/libpqos.a" -lpthread -o "$SRC/$t-standalone"
done

# 3) The AFL target: the library and the tool both with afl-clang-fast.
copy_sources "$B/afl"
make -C "$B/afl/lib" -j"$MAYHEM_JOBS" SHARED=n CC=afl-clang-fast \
  EXTRA_CFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS" libpqos.a
# afl-clang-fast turns every object into LLVM bitcode as soon as -fcf-protection is on its command line,
# and pqos/Makefile always passes it; so the tool's sources are compiled and linked here, not by that Makefile.
afl-clang-fast $SANITIZER_FLAGS $DEBUG_FLAGS -O2 -fPIE -pie -I"$B/afl/lib" -pthread \
  "$B"/afl/pqos/*.c "$LSAN_OFF" "$B/afl/lib/libpqos.a" -lpthread -o "$SRC/pqos_config"
