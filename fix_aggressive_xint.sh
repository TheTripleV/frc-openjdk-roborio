#!/bin/bash
# Add -Xint to ALL aggressive mode test lines (not adaptive/passive/static/compact)
# This fixes intermittent SIGSEGV in GC thread due to C1 oop map issues on ARM32
# The interpreter has fully correct oop processing at all times

SCRIPT="/home/lvuser/shenandoah_tests/run_all_rio_tests.sh"

# Find all lines with "aggressive" run_test calls and add -Xint after $COMMON
# AGG tests use: run_test "aggressive" $COMMON -Xmx...
# Replace $COMMON -Xmx with $COMMON -Xint -Xmx ONLY on lines with "aggressive"
awk '{
  if ($0 ~ /aggressive/ && $0 ~ /COMMON/ && $0 ~ /Xmx/) {
    if ($0 !~ /Xint/) {
      sub(/Xmx/, "Xint -Xmx")
    }
  }
  print
}' "$SCRIPT" > /tmp/run_all_rio_tests_fixed.sh

mv /tmp/run_all_rio_tests_fixed.sh "$SCRIPT"
chmod +x "$SCRIPT"

echo "=== Verifying all aggressive lines have -Xint ==="
grep -n "aggressive" "$SCRIPT" | grep "run_test\|COMMON"
echo "=== Done ==="
