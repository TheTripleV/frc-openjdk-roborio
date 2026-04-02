#!/bin/bash
# Shenandoah GC QEMU ARM32 Adaptive Test Runner
set -u

JRE=/jdk17u-local-build/build/linux-arm-client-release/images/jre
SYSROOT=/usr/local/arm-nilrt-linux-gnueabi/sysroot
OUTDIR=/tmp/shenandoah_test_classes

run_one() {
    local test_name="$1"
    local heap="${2:-128m}"
    local extra="${3:-}"
    local outfile="/tmp/ada_${test_name}.txt"
    
    timeout 180 qemu-arm-static -L "$SYSROOT" "$JRE/bin/java" \
        -Xint \
        -XX:+UnlockDiagnosticVMOptions \
        -XX:+UnlockExperimentalVMOptions \
        -XX:+UseShenandoahGC \
        "-Xmx${heap}" \
        -Xms64m \
        $extra \
        -cp "$OUTDIR" \
        "$test_name" > "$outfile" 2>&1
    local rc=$?
    
    if [ "$rc" -eq 0 ]; then
        echo "PASS $test_name"
    elif [ "$rc" -eq 124 ]; then
        echo "TIMEOUT $test_name"
    else
        echo "FAIL $test_name rc=$rc"
        tail -3 "$outfile"
    fi
}

echo "=== Shenandoah Adaptive Mode Tests (QEMU ARM32) ==="
echo ""

run_one TestAllocObjects 128m
run_one TestAllocIntArrays 128m
run_one TestAllocObjectArrays 150m
run_one TestAllocHumongousFragment 256m "-Doccupancy=100 -Dtarget=1000"
run_one TestArrayCopyCheckCast 128m
run_one TestArrayCopyStress 128m
run_one TestElasticTLAB 128m
run_one TestGCThreadGroups 128m
run_one TestHumongousThreshold 150m
run_one TestLargeObjectAlignment 128m
run_one TestLotsOfCycles 128m
run_one TestParallelRefprocSanity 128m
run_one TestRefprocSanity 128m
run_one TestRegionSampling 128m
run_one TestResizeTLAB 128m
run_one TestRetainObjects 128m
run_one TestSieveObjects 150m
run_one TestSmallHeap 128m
run_one TestStringInternCleanup 128m
run_one TestWithLogLevel 512m

echo ""
echo "=== Done ==="
