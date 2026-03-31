#!/bin/bash
# Compile and deploy Shenandoah GC tests to the RoboRIO
# Run from the project root directory

set -e

PROJDIR="/mnt/c/Users/vasis/Desktop/frc-openjdk-roborio-shenandoah"
JDKTEST="$PROJDIR/jdk17u-local/test/hotspot/jtreg/gc/shenandoah"
TESTDIR="$PROJDIR/jdk_tests/shenandoah_suite"
RIO="lvuser@10.59.40.2"
RIOTESTDIR="/home/lvuser/shenandoah_tests"

echo "=== Setting up test directory ==="
rm -rf "$TESTDIR"
mkdir -p "$TESTDIR"

# Copy self-contained tests (no external deps)
for f in TestAllocObjects.java TestLotsOfCycles.java TestRetainObjects.java \
         TestRefprocSanity.java TestParallelRefprocSanity.java \
         TestGCThreadGroups.java TestSmallHeap.java TestStringInternCleanup.java \
         TestArrayCopyCheckCast.java TestWrongArrayMember.java \
         TestVerifyJCStress.java TestRegionSampling.java \
         TestVerifyLevels.java TestWithLogLevel.java; do
    if [ -f "$JDKTEST/$f" ]; then
        cp "$JDKTEST/$f" "$TESTDIR/"
        echo "  Copied $f"
    else
        echo "  MISSING $f"
    fi
done

# Create adapted versions of tests that use Utils.getRandomInstance()
# TestSieveObjects - replace Utils.getRandomInstance() with new Random()
echo "  Creating standalone TestSieveObjects.java"
sed 's/import jdk.test.lib.Utils;//; s/Utils.getRandomInstance()/new Random(42)/' \
    "$JDKTEST/TestSieveObjects.java" > "$TESTDIR/TestSieveObjects.java"

echo "  Creating standalone TestAllocIntArrays.java"
sed 's/import jdk.test.lib.Utils;//; s/Utils.getRandomInstance()/new Random(42)/' \
    "$JDKTEST/TestAllocIntArrays.java" > "$TESTDIR/TestAllocIntArrays.java"

echo "  Creating standalone TestAllocObjectArrays.java"
sed 's/import jdk.test.lib.Utils;//; s/Utils.getRandomInstance()/new Random(42)/' \
    "$JDKTEST/TestAllocObjectArrays.java" > "$TESTDIR/TestAllocObjectArrays.java"

echo "  Creating standalone TestArrayCopyStress.java"
sed 's/import jdk.test.lib.Utils;//; s/Utils.getRandomInstance()/new Random(42)/' \
    "$JDKTEST/TestArrayCopyStress.java" > "$TESTDIR/TestArrayCopyStress.java"

echo "  Creating standalone TestElasticTLAB.java"
sed 's/import jdk.test.lib.Utils;//; s/Utils.getRandomInstance()/new Random(42)/' \
    "$JDKTEST/TestElasticTLAB.java" > "$TESTDIR/TestElasticTLAB.java"

echo "  Creating standalone TestResizeTLAB.java"
sed 's/import jdk.test.lib.Utils;//; s/Utils.getRandomInstance()/new Random(42)/' \
    "$JDKTEST/TestResizeTLAB.java" > "$TESTDIR/TestResizeTLAB.java"

echo "  Creating standalone TestHumongousThreshold.java"
sed 's/import jdk.test.lib.Utils;//; s/Utils.getRandomInstance()/new Random(42)/' \
    "$JDKTEST/TestHumongousThreshold.java" > "$TESTDIR/TestHumongousThreshold.java"

echo "  Creating standalone TestAllocHumongousFragment.java"
sed 's/import jdk.test.lib.Utils;//; s/Utils.getRandomInstance()/new Random(42)/' \
    "$JDKTEST/TestAllocHumongousFragment.java" > "$TESTDIR/TestAllocHumongousFragment.java"

echo "  Creating standalone TestLargeObjectAlignment.java"
sed 's/import jdk.test.lib.Utils;//; s/Utils.getRandomInstance()/new Random(42)/' \
    "$JDKTEST/TestLargeObjectAlignment.java" > "$TESTDIR/TestLargeObjectAlignment.java"

# Copy test runner
cp "$PROJDIR/jdk_tests/run_shenandoah_tests.sh" "$TESTDIR/"

echo ""
echo "=== Compiling tests ==="
cd "$TESTDIR"
javac -source 11 -target 11 *.java 2>&1 || {
    echo "Compilation failed, trying individual files..."
    for f in *.java; do
        echo -n "  Compiling $f ... "
        if javac -source 11 -target 11 "$f" 2>/dev/null; then
            echo "OK"
        else
            echo "FAIL"
        fi
    done
}
echo "Compiled $(ls *.class 2>/dev/null | wc -l) class files"

echo ""
echo "=== Deploying to RIO ==="
ssh -o StrictHostKeyChecking=no "$RIO" "mkdir -p $RIOTESTDIR" 2>/dev/null
scp -o StrictHostKeyChecking=no *.class run_shenandoah_tests.sh "$RIO:$RIOTESTDIR/" 2>&1
ssh -o StrictHostKeyChecking=no "$RIO" "chmod +x $RIOTESTDIR/run_shenandoah_tests.sh"

echo ""
echo "=== Deploy complete ==="
echo "Run tests with: ssh lvuser@10.59.40.2 'bash /home/lvuser/shenandoah_tests/run_shenandoah_tests.sh'"
