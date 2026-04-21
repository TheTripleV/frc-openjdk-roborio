#!/bin/bash
# Comprehensive Shenandoah GC test runner for RoboRIO
# Runs tests in IU mode, SATB aggressive, and SATB adaptive modes
# Heap sizes tuned for 497MB RAM system

JAVA=/usr/local/frc/JRE/bin/java
TESTDIR=/home/lvuser/shenandoah_tests
PASS=0
FAIL=0
ERRORS=""
TOTAL=0

COMMON="-XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC"

run_test() {
    local name="$1"
    shift
    local tmout="${TIMEOUT:-120}"
    TOTAL=$((TOTAL + 1))
    echo -n "  $name ... "

    $JAVA "$@" > /tmp/_test_out 2>&1 &
    local pid=$!

    ( sleep "$tmout"; kill -9 $pid 2>/dev/null ) &
    local wdog=$!

    wait $pid 2>/dev/null
    local rc=$?

    kill $wdog 2>/dev/null
    wait $wdog 2>/dev/null

    if [ $rc -eq 0 ]; then
        echo "PASS"
        PASS=$((PASS + 1))
    elif [ $rc -eq 137 ]; then
        echo "TIMEOUT (${tmout}s)"
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  TIMEOUT: $name"
    else
        echo "FAIL (exit=$rc)"
        cat /tmp/_test_out | tail -15 | sed 's/^/    /'
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  FAIL: $name (exit=$rc)"
    fi
}

echo "=== Comprehensive Shenandoah GC Test Suite for RoboRIO ==="
echo "Java: $($JAVA -version 2>&1 | head -1)"
echo ""

cd "$TESTDIR" || { echo "Cannot cd to $TESTDIR"; exit 1; }

# ============================================================
echo "=== IU (INCREMENTAL-UPDATE) MODE TESTS ==="
# ============================================================

IU="-XX:ShenandoahGCMode=iu"

echo ""
echo "[IU-1] TestAllocObjects"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=1500 -cp . TestAllocObjects

echo ""
echo "[IU-2] TestAllocIntArrays"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=500 -cp . TestAllocIntArrays

echo ""
echo "[IU-3] TestAllocObjectArrays"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=500 -cp . TestAllocObjectArrays

echo ""
echo "[IU-4] TestAllocHumongousFragment"
TIMEOUT=300 run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Doccupancy=50 -Dtarget=1000 -cp . TestAllocHumongousFragment

echo ""
echo "[IU-5] TestArrayCopyStress"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestArrayCopyStress

echo ""
echo "[IU-6] TestArrayCopyCheckCast"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestArrayCopyCheckCast

echo ""
echo "[IU-7] TestElasticTLAB"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestElasticTLAB

echo ""
echo "[IU-8] TestGCThreadGroups"
TIMEOUT=180 run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=500 -cp . TestGCThreadGroups

echo ""
echo "[IU-9] TestHumongousThreshold"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestHumongousThreshold

echo ""
echo "[IU-10] TestLargeObjectAlignment"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestLargeObjectAlignment

echo ""
echo "[IU-11] TestLotsOfCycles"
TIMEOUT=240 run_test "iu-adaptive" $COMMON $IU -Xmx16m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=1500 -cp . TestLotsOfCycles

echo ""
echo "[IU-12] TestRefprocSanity"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestRefprocSanity

echo ""
echo "[IU-13] TestParallelRefprocSanity"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -XX:+ParallelRefProcEnabled \
    -cp . TestParallelRefprocSanity

echo ""
echo "[IU-14] TestRegionSampling"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=500 -cp . TestRegionSampling

echo ""
echo "[IU-15] TestResizeTLAB"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestResizeTLAB

echo ""
echo "[IU-16] TestRetainObjects"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestRetainObjects

echo ""
echo "[IU-17] TestSieveObjects"
TIMEOUT=180 run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestSieveObjects

echo ""
echo "[IU-18] TestSmallHeap"
run_test "iu-4m" $COMMON $IU -Xmx4m -cp . TestSmallHeap
run_test "iu-16m" $COMMON $IU -Xmx16m -cp . TestSmallHeap
run_test "iu-64m" $COMMON $IU -Xmx64m -cp . TestSmallHeap

echo ""
echo "[IU-19] TestStringInternCleanup"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestStringInternCleanup

echo ""
echo "[IU-20] TestWithLogLevel"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -DallocMB=50 \
    -cp . TestWithLogLevel

echo ""
echo "[IU-21] TestVerifyJCStress"
TIMEOUT=600 run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -DouterCount=1000 -DinnerCount=5000 \
    -cp . TestVerifyJCStress

echo ""
echo "[IU-22] TestWrongArrayMember"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestWrongArrayMember

echo ""
echo "[IU-23] TestVerifyLevels"
run_test "iu-adaptive" $COMMON $IU -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestVerifyLevels

echo ""
echo "--- IU Mode Results: $PASS passed, $FAIL failed out of $TOTAL tests ---"
IU_PASS=$PASS
IU_FAIL=$FAIL
IU_TOTAL=$TOTAL

# Reset counters for SATB modes
PASS=0
FAIL=0
TOTAL=0

# ============================================================
echo ""
echo "=== SATB AGGRESSIVE MODE TESTS ==="
# ============================================================

echo ""
echo "[AGG-1] TestAllocObjects"
run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -Dtarget=500 -cp . TestAllocObjects

echo ""
echo "[AGG-2] TestAllocIntArrays"
run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -Dtarget=200 -cp . TestAllocIntArrays

echo ""
echo "[AGG-3] TestAllocObjectArrays"
run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -Dtarget=200 -cp . TestAllocObjectArrays

echo ""
echo "[AGG-4] TestAllocHumongousFragment"
TIMEOUT=300 run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -Doccupancy=50 -Dtarget=1000 -cp . TestAllocHumongousFragment

echo ""
echo "[AGG-5] TestElasticTLAB"
run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -cp . TestElasticTLAB

echo ""
echo "[AGG-6] TestGCThreadGroups"
TIMEOUT=180 run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -Dtarget=500 -cp . TestGCThreadGroups

echo ""
echo "[AGG-7] TestLotsOfCycles"
TIMEOUT=240 run_test "aggressive" $COMMON -Xmx16m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -Dtarget=500 -cp . TestLotsOfCycles

echo ""
echo "[AGG-8] TestRetainObjects"
run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -cp . TestRetainObjects

echo ""
echo "[AGG-9] TestSieveObjects"
TIMEOUT=180 run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -cp . TestSieveObjects

echo ""
echo "[AGG-10] TestSmallHeap"
run_test "aggressive" $COMMON -Xmx128m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -cp . TestSmallHeap

echo ""
echo "[AGG-11] TestStringInternCleanup"
run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -cp . TestStringInternCleanup

echo ""
echo "[AGG-12] TestRefprocSanity"
run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -cp . TestRefprocSanity

echo ""
echo "[AGG-13] TestParallelRefprocSanity"
run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -XX:+ParallelRefProcEnabled \
    -cp . TestParallelRefprocSanity

echo ""
echo "[AGG-14] TestArrayCopyCheckCast"
run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -cp . TestArrayCopyCheckCast

echo ""
echo "[AGG-15] TestVerifyJCStress"
TIMEOUT=600 run_test "aggressive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=aggressive \
    -DouterCount=200 -DinnerCount=2000 \
    -cp . TestVerifyJCStress

# ============================================================
echo ""
echo "=== ADAPTIVE MODE TESTS ==="
# ============================================================

echo ""
echo "[ADA-1] TestAllocObjects"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=1500 -cp . TestAllocObjects

echo ""
echo "[ADA-2] TestAllocIntArrays"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=500 -cp . TestAllocIntArrays

echo ""
echo "[ADA-3] TestAllocObjectArrays"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=500 -cp . TestAllocObjectArrays

echo ""
echo "[ADA-4] TestAllocHumongousFragment"
TIMEOUT=300 run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Doccupancy=50 -Dtarget=1000 -cp . TestAllocHumongousFragment

echo ""
echo "[ADA-5] TestArrayCopyCheckCast"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestArrayCopyCheckCast

echo ""
echo "[ADA-6] TestArrayCopyStress"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestArrayCopyStress

echo ""
echo "[ADA-7] TestElasticTLAB"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestElasticTLAB

echo ""
echo "[ADA-8] TestGCThreadGroups"
TIMEOUT=180 run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=500 -cp . TestGCThreadGroups

echo ""
echo "[ADA-9] TestHumongousThreshold"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestHumongousThreshold

echo ""
echo "[ADA-10] TestLargeObjectAlignment"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestLargeObjectAlignment

echo ""
echo "[ADA-11] TestLotsOfCycles"
TIMEOUT=240 run_test "adaptive" $COMMON -Xmx16m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=1500 -cp . TestLotsOfCycles

echo ""
echo "[ADA-12] TestParallelRefprocSanity"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -XX:+ParallelRefProcEnabled \
    -cp . TestParallelRefprocSanity

echo ""
echo "[ADA-13] TestRefprocSanity"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestRefprocSanity

echo ""
echo "[ADA-14] TestRegionSampling"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -Dtarget=500 -cp . TestRegionSampling

echo ""
echo "[ADA-15] TestResizeTLAB"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestResizeTLAB

echo ""
echo "[ADA-16] TestRetainObjects"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestRetainObjects

echo ""
echo "[ADA-17] TestSieveObjects"
TIMEOUT=180 run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestSieveObjects

echo ""
echo "[ADA-18] TestSmallHeap"
run_test "adaptive-4m" $COMMON -Xmx4m -cp . TestSmallHeap
run_test "adaptive-8m" $COMMON -Xmx8m -cp . TestSmallHeap
run_test "adaptive-16m" $COMMON -Xmx16m -cp . TestSmallHeap
run_test "adaptive-64m" $COMMON -Xmx64m -cp . TestSmallHeap

echo ""
echo "[ADA-19] TestStringInternCleanup"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestStringInternCleanup

echo ""
echo "[ADA-20] TestWithLogLevel"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -DallocMB=50 \
    -cp . TestWithLogLevel

echo ""
echo "[ADA-21] TestVerifyJCStress"
TIMEOUT=600 run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -DouterCount=1000 -DinnerCount=5000 \
    -cp . TestVerifyJCStress

echo ""
echo "[ADA-22] TestWrongArrayMember"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestWrongArrayMember

echo ""
echo "[ADA-23] TestVerifyLevels"
run_test "adaptive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=adaptive \
    -cp . TestVerifyLevels

# ============================================================
echo ""
echo "=== PASSIVE MODE TESTS ==="
# ============================================================

echo ""
echo "[PAS-1] TestAllocObjects +DegenGC"
run_test "passive+degen" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCMode=passive -XX:+ShenandoahDegeneratedGC \
    -Dtarget=1500 -cp . TestAllocObjects

echo ""
echo "[PAS-2] TestAllocObjects -DegenGC"
run_test "passive-degen" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCMode=passive -XX:-ShenandoahDegeneratedGC \
    -Dtarget=1500 -cp . TestAllocObjects

echo ""
echo "[PAS-3] TestLotsOfCycles"
TIMEOUT=240 run_test "passive" $COMMON -Xmx16m \
    -XX:ShenandoahGCMode=passive -XX:+ShenandoahDegeneratedGC \
    -Dtarget=500 -cp . TestLotsOfCycles

echo ""
echo "[PAS-4] TestRetainObjects"
run_test "passive" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCMode=passive -XX:+ShenandoahDegeneratedGC \
    -cp . TestRetainObjects

# ============================================================
echo ""
echo "=== STATIC / COMPACT MODE TESTS ==="
# ============================================================

echo ""
echo "[STA-1] TestAllocObjects static"
run_test "static" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=static \
    -Dtarget=1500 -cp . TestAllocObjects

echo ""
echo "[COM-1] TestAllocObjects compact"
run_test "compact" $COMMON -Xmx150m -Xms150m \
    -XX:ShenandoahGCHeuristics=compact \
    -Dtarget=1500 -cp . TestAllocObjects

# ============================================================
echo ""
echo "============================================"
SATB_PASS=$PASS
SATB_FAIL=$FAIL
SATB_TOTAL=$TOTAL
ALL_PASS=$((IU_PASS + SATB_PASS))
ALL_FAIL=$((IU_FAIL + SATB_FAIL))
ALL_TOTAL=$((IU_TOTAL + SATB_TOTAL))
echo "IU MODE:   $IU_PASS passed, $IU_FAIL failed out of $IU_TOTAL tests"
echo "SATB MODE: $SATB_PASS passed, $SATB_FAIL failed out of $SATB_TOTAL tests"
echo "OVERALL:   $ALL_PASS passed, $ALL_FAIL failed out of $ALL_TOTAL tests"
if [ $ALL_FAIL -gt 0 ]; then
    echo -e "FAILURES:$ERRORS"
fi
echo "============================================"

exit $ALL_FAIL
