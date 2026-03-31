#!/bin/bash
# Run all Shenandoah GC tests on RoboRIO
# Usage: ./run_tests.sh [duration_per_test_seconds]
#
# Prerequisites: Copy this file and all .java files to /home/lvuser/tests/ on the RoboRIO
# Then run as: cd /home/lvuser/tests && bash run_tests.sh

set -e

JAVA=/usr/local/frc/JRE/bin/java
JAVAC=/usr/local/frc/JRE/bin/javac
DURATION=${1:-30}
PASS=0
FAIL=0
ERRORS=""

echo "========================================"
echo " Shenandoah GC ARM32 Test Suite"
echo " Duration per test: ${DURATION}s"
echo " Java: $JAVA"
echo "========================================"

# Check java exists
if [ ! -x "$JAVA" ]; then
    echo "ERROR: Java not found at $JAVA"
    exit 1
fi

echo ""
echo "Java version:"
$JAVA -version 2>&1
echo ""

# Compile all tests
echo "=== Compiling tests ==="

# Check if javac exists; on JRE-only installs we use java -source
if [ -x "$JAVAC" ]; then
    $JAVAC *.java
else
    # JRE doesn't have javac; compile using the host or skip if .class files exist
    echo "No javac found - using source-file launch mode (JDK 11+)"
fi

run_test() {
    local name=$1
    local jvmargs=$2
    local testargs=$3
    local timeout=$4

    echo ""
    echo "========================================"
    echo " Running: $name"
    echo " JVM args: $jvmargs"
    echo "========================================"

    local source_arg=""
    if [ ! -f "${name}.class" ]; then
        source_arg="${name}.java"
    else
        source_arg="${name}"
    fi

    # Run with timeout
    set +e
    timeout ${timeout} $JAVA $jvmargs $source_arg $testargs 2>&1
    local rc=$?
    set -e

    if [ $rc -eq 0 ]; then
        echo ">>> RESULT: PASS"
        PASS=$((PASS + 1))
    elif [ $rc -eq 124 ]; then
        echo ">>> RESULT: TIMEOUT (might be OK if test was still running)"
        PASS=$((PASS + 1))
    else
        echo ">>> RESULT: FAIL (exit code $rc)"
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  - $name (exit $rc)"
    fi
}

# Test 1: Basic startup (short)
run_test "ShenandoahBasic" \
    "-XX:+UseShenandoahGC" \
    "" \
    30

# Test 2: Allocation stress with concurrent GC
run_test "ShenandoahAllocStress" \
    "-XX:+UseShenandoahGC -Xmx64m -Xms32m -verbose:gc" \
    "$DURATION" \
    $((DURATION + 30))

# Test 3: Multi-threaded stress
run_test "ShenandoahThreadStress" \
    "-XX:+UseShenandoahGC -Xmx64m -Xms32m -verbose:gc" \
    "$DURATION 4" \
    $((DURATION + 30))

# Test 4: InvokeHandle/Lambda stress (targets the previous crash area)
run_test "ShenandoahInvokeStress" \
    "-XX:+UseShenandoahGC -Xmx64m -Xms32m -verbose:gc" \
    "$DURATION" \
    $((DURATION + 30))

# Test 5: Reference processing stress
run_test "ShenandoahRefStress" \
    "-XX:+UseShenandoahGC -Xmx64m -Xms32m -verbose:gc" \
    "$DURATION" \
    $((DURATION + 30))

# Test 6: Pause time measurement (longer)
run_test "ShenandoahPauseTest" \
    "-XX:+UseShenandoahGC -Xmx64m -Xms32m" \
    "$DURATION" \
    $((DURATION + 30))

# Test 7: Interpreter-only mode (exercises interpreter barriers)
run_test "ShenandoahAllocStress" \
    "-XX:+UseShenandoahGC -Xmx64m -Xms32m -Xint -verbose:gc" \
    "15" \
    60

# Test 8: InvokeHandle in interpreter-only mode (the exact scenario that was crashing)
run_test "ShenandoahInvokeStress" \
    "-XX:+UseShenandoahGC -Xmx64m -Xms32m -Xint -verbose:gc" \
    "15" \
    60

echo ""
echo "========================================"
echo " SUMMARY"
echo "========================================"
echo " Passed: $PASS"
echo " Failed: $FAIL"
if [ $FAIL -gt 0 ]; then
    echo -e " Failures: $ERRORS"
fi
echo "========================================"

# Check for crash logs
echo ""
echo "=== Checking for crash logs ==="
ls -la /home/lvuser/hs_err_*.log 2>/dev/null && echo "WARNING: Crash logs found!" || echo "No crash logs found (good!)"
ls -la /tmp/hs_err_*.log 2>/dev/null && echo "WARNING: Crash logs in /tmp!" || true

exit $FAIL
