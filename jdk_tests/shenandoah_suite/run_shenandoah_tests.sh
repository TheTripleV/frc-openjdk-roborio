#!/bin/bash
# Run Shenandoah GC hotspot tests on the RoboRIO
# Adapted from jdk17u-local/test/hotspot/jtreg/gc/shenandoah/
# Heap sizes scaled down for 497MB RAM system (using 150m max)

JAVA=/usr/local/frc/JRE/bin/java
TESTDIR=/home/lvuser/shenandoah_tests
PASS=0
FAIL=0
SKIP=0
ERRORS=""

# Common flags
COMMON="-XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC"

run_test() {
    local name="$1"
    shift
    local tmout="${TIMEOUT:-120}"
    echo -n "  $name ... "
    
    # Run with shell-based timeout (RoboRIO lacks coreutils timeout)
    local output rc
    $JAVA "$@" > /tmp/_test_out 2>&1 &
    local pid=$!
    
    # Background watchdog
    ( sleep "$tmout"; kill -9 $pid 2>/dev/null ) &
    local wdog=$!
    
    wait $pid 2>/dev/null
    rc=$?
    
    # Kill watchdog if test finished before timeout
    kill $wdog 2>/dev/null
    wait $wdog 2>/dev/null
    
    output=$(cat /tmp/_test_out)
    
    if [ $rc -eq 0 ]; then
        echo "PASS"
        PASS=$((PASS + 1))
    elif [ $rc -eq 137 ]; then
        echo "TIMEOUT (${tmout}s)"
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  TIMEOUT: $name"
    else
        echo "FAIL (exit=$rc)"
        # Print last 10 lines of output for diagnosis
        echo "$output" | tail -10 | sed 's/^/    /'
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  FAIL: $name (exit=$rc)"
    fi
}

echo "=== Shenandoah GC Test Suite for RoboRIO ==="
echo "Java: $($JAVA -version 2>&1 | head -1)"
echo "Test dir: $TESTDIR"
echo ""

cd "$TESTDIR" || { echo "Cannot cd to $TESTDIR"; exit 1; }

# ============================================================
# 1. TestAllocObjects - basic allocation stress
# ============================================================
echo "[1/14] TestAllocObjects (allocation stress)"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=1500 -cp . TestAllocObjects

run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -Dtarget=500 -cp . TestAllocObjects

run_test "static" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=static \
    -Dtarget=1500 -cp . TestAllocObjects

run_test "compact" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=compact \
    -Dtarget=1500 -cp . TestAllocObjects

run_test "passive+degenGC" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCMode=passive -XX:+ShenandoahDegeneratedGC \
    -Dtarget=1500 -cp . TestAllocObjects

run_test "passive-degenGC" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCMode=passive -XX:-ShenandoahDegeneratedGC \
    -Dtarget=1500 -cp . TestAllocObjects

# ============================================================
# 2. TestLotsOfCycles - many GC cycles
# ============================================================
echo ""
echo "[2/14] TestLotsOfCycles (many GC cycles with sleeps)"
TIMEOUT=240 run_test "adaptive" $COMMON -Xmx16m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=1500 -cp . TestLotsOfCycles

TIMEOUT=240 run_test "aggressive" $COMMON -Xmx16m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -Dtarget=500 -cp . TestLotsOfCycles

TIMEOUT=240 run_test "passive" $COMMON -Xmx16m \
    -XX:ShenandoahGCMode=passive -XX:+ShenandoahDegeneratedGC \
    -Dtarget=500 -cp . TestLotsOfCycles

# ============================================================
# 3. TestRetainObjects - retained object sliding window
# ============================================================
echo ""
echo "[3/14] TestRetainObjects (retained objects)"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestRetainObjects

run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -cp . TestRetainObjects

run_test "passive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCMode=passive -XX:+ShenandoahDegeneratedGC \
    -cp . TestRetainObjects

# ============================================================
# 4. TestRefprocSanity - reference processing
# ============================================================
echo ""
echo "[4/14] TestRefprocSanity (reference processing)"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestRefprocSanity

run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -cp . TestRefprocSanity

# ============================================================
# 5. TestParallelRefprocSanity - parallel reference processing
# ============================================================
echo ""
echo "[5/14] TestParallelRefprocSanity"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -XX:+ParallelRefProcEnabled \
    -cp . TestParallelRefprocSanity

run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -XX:+ParallelRefProcEnabled \
    -cp . TestParallelRefprocSanity

# ============================================================
# 6. TestGCThreadGroups - GC thread groups
# ============================================================
echo ""
echo "[6/14] TestGCThreadGroups"
TIMEOUT=180 run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=500 -cp . TestGCThreadGroups

# ============================================================
# 7. TestSmallHeap - tiny heaps
# ============================================================
echo ""
echo "[7/14] TestSmallHeap (tiny heaps)"
run_test "4m" $COMMON -Xmx4m \
    -cp . TestSmallHeap

run_test "8m" $COMMON -Xmx8m \
    -cp . TestSmallHeap

run_test "16m" $COMMON -Xmx16m \
    -cp . TestSmallHeap

run_test "64m" $COMMON -Xmx64m \
    -cp . TestSmallHeap

# ============================================================
# 8. TestStringInternCleanup - string intern table
# ============================================================
echo ""
echo "[8/14] TestStringInternCleanup"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestStringInternCleanup

run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -cp . TestStringInternCleanup

# ============================================================
# 9. TestArrayCopyCheckCast - arraycopy type checks
# ============================================================
echo ""
echo "[9/14] TestArrayCopyCheckCast"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestArrayCopyCheckCast

run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -cp . TestArrayCopyCheckCast

# ============================================================
# 10. TestWrongArrayMember - wrong array member
# ============================================================
echo ""
echo "[10/14] TestWrongArrayMember"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestWrongArrayMember

# ============================================================
# 11. TestVerifyJCStress - concurrent StampedLock stress
# ============================================================
echo ""
echo "[11/14] TestVerifyJCStress (concurrent stress)"
TIMEOUT=180 run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestVerifyJCStress

TIMEOUT=180 run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -cp . TestVerifyJCStress

# ============================================================
# 12. TestSieveObjects - sieve with payload verification
# ============================================================
echo ""
echo "[12/14] TestSieveObjects (sieve with payload)"
TIMEOUT=180 run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestSieveObjects

TIMEOUT=180 run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -cp . TestSieveObjects

# ============================================================
# 13. TestAllocIntArrays - random int array allocation
# ============================================================
echo ""
echo "[13/14] TestAllocIntArrays (random int arrays)"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=500 -cp . TestAllocIntArrays

run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -Dtarget=200 -cp . TestAllocIntArrays

# ============================================================
# 14. TestRegionSampling - region sampling
# ============================================================
echo ""
echo "[14/14] TestRegionSampling"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=500 -cp . TestRegionSampling

echo ""
echo "============================================"
echo "RESULTS: $PASS passed, $FAIL failed, $SKIP skipped"
if [ $FAIL -gt 0 ]; then
    echo -e "FAILURES:$ERRORS"
fi
echo "============================================"

exit $FAIL
