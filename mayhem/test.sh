#!/usr/bin/env bash
#
# mayhem/test.sh — RUNS upstream's cmocka unit tests and checks the pqos tool's output (built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# Upstream's cmocka suites, built by build.sh against the mock libc in unit-test/mock. Each binary prints
# one "[==========] <group>: N test(s) run." and "[  PASSED  ] P test(s)." line per group, and one
# "[       OK ] <case>" / "[  SKIPPED ] <case>" / "[  FAILED  ] <case>" line per case.
#
# HOST_CPU_CASES are excluded by name because their outcome depends on the CPU of the host, not on the
# code. Both test_hybrid cases execute the real CPUID instruction: they call cpuinfo_discover(PQOS_INTER_MSR),
# which reads the vendor (leaf 0) and the APIC/cache topology leaves (0xB and 4, or 0x8000001D on AMD)
# of the machine the test runs on. Where that topology cannot be built, upstream calls skip(). So they
# pass on one host and are skipped on another (the Mayhem CI runner: rc=0, ran=18, passed=16). They are
# always reported as skipped, so the counts are the same on every host. A FAILED result from them still
# counts as a failure. Any other skipped case still counts as a failure, as before.
HOST_CPU_CASES=" test_hybrid:test_a_topology_read_that_loses_the_affinity_says_so test_hybrid:test_a_lost_affinity_is_not_answered_from_the_next_source "
passed=0; failed=0; skipped=0
run_suite() {   # <libpath> <binary>...
  local libpath="$1" b name out rc ran ok c excl_ok excl_skip bad; shift
  for b in "$@"; do
    [ -x "$b" ] || { echo "missing test binary: $b (build.sh did not build it)" >&2; failed=$((failed + 1)); continue; }
    name=$(basename "$b")
    out=$(LD_LIBRARY_PATH="$libpath" timeout 300 "$b" 2>&1); rc=$?
    ran=$(printf '%s\n' "$out" | sed -n 's/^\[==========\] [A-Za-z0-9_]*: \([0-9][0-9]*\) test.*run\./\1/p' | awk '{s+=$1} END{print s+0}')
    ok=$(printf '%s\n' "$out" | sed -n 's/^\[  PASSED  \] \([0-9][0-9]*\) test.*/\1/p' | awk '{s+=$1} END{print s+0}')
    ran=${ran:-0}; ok=${ok:-0}
    excl_ok=0; excl_skip=0
    for c in $(printf '%s\n' "$out" | sed -n 's/^\[       OK \] \([A-Za-z0-9_]*\)$/\1/p' | sort -u); do
      case "$HOST_CPU_CASES" in *" $name:$c "*) excl_ok=$((excl_ok + 1)) ;; esac
    done
    for c in $(printf '%s\n' "$out" | sed -n 's/^\[  SKIPPED \] \([A-Za-z0-9_]*\)$/\1/p' | sort -u); do
      case "$HOST_CPU_CASES" in *" $name:$c "*) excl_skip=$((excl_skip + 1)) ;; esac
    done
    bad=$((ran - ok - excl_skip))   # failed, plus any case skipped that is not in HOST_CPU_CASES
    if [ "$rc" -ne 0 ] || [ "$ran" -eq 0 ] || [ "$bad" -ne 0 ]; then
      echo "FAILED: $b (rc=$rc, ran=$ran, passed=$ok, host-cpu skipped=$excl_skip)" >&2
      printf '%s\n' "$out" | grep -v '^\[ \(RUN     \|      OK\) \]' | tail -20 >&2
      failed=$((failed + (bad > 0 ? bad : 1)))
    fi
    passed=$((passed + ok - excl_ok))
    skipped=$((skipped + excl_ok + excl_skip))
  done
}

shopt -s nullglob
lib_tests=(unit-test/lib/bin/test_*)
pqos_tests=(unit-test/pqos/bin/test_*)
[ "${#lib_tests[@]}" -ge 50 ] || { echo "only ${#lib_tests[@]} lib test binaries" >&2; failed=$((failed + 1)); }
[ "${#pqos_tests[@]}" -ge 15 ] || { echo "only ${#pqos_tests[@]} pqos test binaries" >&2; failed=$((failed + 1)); }
run_suite unit-test/mock "${lib_tests[@]}"
run_suite unit-test/mock:lib:unit-test/output "${pqos_tests[@]}"

# Known-answer check on the real, dynamically linked tool: `pqos -H` lists the built-in allocation profiles
# from profiles.c with no hardware needed; the exact lines are asserted.
help_out=$(LD_LIBRARY_PATH=lib timeout 60 pqos/pqos -H 2>/dev/null); rc=$?
expect() {
  if printf '%s\n' "$help_out" | grep -qxF -- "$1"; then passed=$((passed + 1))
  else echo "pqos -H: line not found: $1" >&2; failed=$((failed + 1)); fi
}
[ "$rc" -eq 0 ] || { echo "pqos -H exited $rc" >&2; failed=$((failed + 1)); }
for n in 0 1 2 3; do expect "      Config ID: CFG$n"; done
expect "    Description: non-overlapping, ways equally divided"
expect "    Description: non-overlapping, ways unequally divided"
expect "	number of classes = 4, number of cache ways = 11"
expect "	number of classes = 4, number of cache ways = 20"
[ "$(printf '%s\n' "$help_out" | grep -c 'Config ID')" -eq 4 ] && passed=$((passed + 1)) || { echo "pqos -H: expected 4 profiles" >&2; failed=$((failed + 1)); }

emit_ctrf "cmocka+pqos" "$passed" "$failed" "$skipped"
