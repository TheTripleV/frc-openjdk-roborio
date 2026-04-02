#!/bin/bash
# Shenandoah GC QEMU ARM32 Test Runner
# Runs inside the Docker build container with qemu-arm-static
set -euo pipefail

JRE=/jdk17u-local-build/build/linux-arm-client-release/images/jre
SYSROOT=/usr/local/arm-nilrt-linux-gnueabi/sysroot
OUTDIR=/tmp/shenandoah_test_classes
TIMEOUT=180

QEMU="qemu-arm-static -L $SYSROOT"
JAVA="$JRE/bin/java"
COMMON="-Xint -XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC"

run_test() {
    local test_name=$1
    local mode=$2
    local heap=$3
    local flags=""
    
    case "$mode" in
        aggressive) flags="$COMMON -XX:ShenandoahGCHeuristics=aggressive -Xmx${heap} -Xms64m" ;;
        adaptive)   flags="$COMMON -Xmx${heap} -Xms64m" ;;
        passive)    flags="$COMMON -XX:ShenandoahGCHeuristics=passive -Xmx${heap} -Xms64m" ;;
        static)     flags="$COMMON -XX:ShenandoahGCHeuristics=static -Xmx${heap} -Xms64m" ;;
        compact)    flags="$COMMON -XX:ShenandoahGCHeuristics=compact -Xmx${heap} -Xms64m" ;;
    esac
    
    local outfile="/tmp/test_${mode}_${test_name}.txt"
    timeout $TIMEOUT $QEMU $JAVA $flags -cp "$OUTDIR" "$test_name" > "$outfile" 2>&1
    local exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        printf "  %-40s %s\n" "$test_name" "PASS"
        return 0
    elif [ $exit_code -eq 124 ]; then
        printf "  %-40s %s\n" "$test_name" "TIMEOUT"
        return 1
    else
        local last_line=$(tail -1 "$outfile" 2>/dev/null || echo "unknown")
        printf "  %-40s %s (exit=%d: %s)\n" "$test_name" "FAIL" "$exit_code" "$last_line"
        return 1
    fi
}

# Compile
echo "=== Compiling test files ==="
/usr/bin/javac -d "$OUTDIR" /artifacts/jdk_tests/shenandoah_suite/*.java 2>/dev/null
echo "Compiled $(ls $OUTDIR/*.class 2>/dev/null | wc -l) classes"
echo ""

# ============================================================
# AGGRESSIVE MODE TESTS
# ============================================================
echo "========================================"
echo "  AGGRESSIVE MODE TESTS (QEMU ARM32)"
echo "========================================"
AGG_PASS=0
AGG_FAIL=0

for t in \
    TestAllocObjects \
    TestAllocIntArrays \
    TestAllocObjectArrays \
    TestAllocHumongousFragment \
    TestElasticTLAB \
    TestGCThreadGroups \
    TestLotsOfCycles \
    TestRetainObjects \
    TestSieveObjects \
    TestSmallHeap \
    TestStringInternCleanup \
; do
    heap="128m"
    case "$t" in
        TestSieveObjects|TestAllocHumongousFragment|TestAllocObjectArrays) heap="150m" ;;
    esac
    if run_test "$t" aggressive "$heap"; then
        AGG_PASS=$((AGG_PASS + 1))
    else
        AGG_FAIL=$((AGG_FAIL + 1))
    fi
done

echo ""
echo "Aggressive: $AGG_PASS passed, $AGG_FAIL failed"
echo ""

# ============================================================
# ADAPTIVE MODE TESTS
# ============================================================
echo "========================================"
echo "  ADAPTIVE MODE TESTS (QEMU ARM32)"
echo "========================================"
ADA_PASS=0
ADA_FAIL=0

for t in \
    TestAllocObjects \
    TestAllocIntArrays \
    TestAllocObjectArrays \
    TestAllocHumongousFragment \
    TestArrayCopyCheckCast \
    TestArrayCopyStress \
    TestElasticTLAB \
    TestGCThreadGroups \
    TestHumongousThreshold \
    TestLargeObjectAlignment \
    TestLotsOfCycles \
    TestParallelRefprocSanity \
    TestRefprocSanity \
    TestRegionSampling \
    TestResizeTLAB \
    TestRetainObjects \
    TestSieveObjects \
    TestSmallHeap \
    TestStringInternCleanup \
    TestWithLogLevel \
; do
    heap="128m"
    case "$t" in
        TestSieveObjects|TestAllocHumongousFragment|TestAllocObjectArrays|TestHumongousThreshold) heap="150m" ;;
    esac
    if run_test "$t" adaptive "$heap"; then
        ADA_PASS=$((ADA_PASS + 1))
    else
        ADA_FAIL=$((ADA_FAIL + 1))
    fi
done

echo ""
echo "Adaptive: $ADA_PASS passed, $ADA_FAIL failed"
echo ""

# ============================================================
# SUMMARY
# ============================================================
echo "========================================"
echo "  SUMMARY"
echo "  Aggressive: $AGG_PASS/$((AGG_PASS+AGG_FAIL))"
echo "  Adaptive:   $ADA_PASS/$((ADA_PASS+ADA_FAIL))"
echo "========================================"
