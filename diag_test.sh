#!/bin/bash
# Test robot code with the oop table fix (NO CompileCommand exclude)
cd /home/lvuser
killall -9 java 2>/dev/null
sleep 1

echo "=== Testing robot code with oop table fix ==="
/usr/local/frc/JRE/bin/java \
  -Djava.lang.invoke.stringConcat=BC_SB \
  -Djava.library.path=/usr/local/frc/third-party/lib \
  -XX:+UnlockDiagnosticVMOptions \
  -XX:+UnlockExperimentalVMOptions \
  -XX:+UseShenandoahGC \
  -jar /home/lvuser/2026-Delta.jar > /home/lvuser/stdout.log 2>&1 &
JAVA_PID=$!
echo "Started java PID=$JAVA_PID"

# Wait up to 180 seconds, check if still alive
for i in $(seq 1 180); do
  sleep 1
  if ! kill -0 $JAVA_PID 2>/dev/null; then
    echo "CRASHED after ${i}s"
    echo "=== CRASH LOG HEAD ==="
    cat /home/lvuser/hs_err_pid${JAVA_PID}.log 2>/dev/null | head -100
    echo "=== CRASH LOG - Events ==="
    grep -n "Event:" /home/lvuser/hs_err_pid${JAVA_PID}.log 2>/dev/null | tail -30
    echo "=== CRASH LOG - Compilation events ==="
    sed -n '/Compilation events/,/^$/p' /home/lvuser/hs_err_pid${JAVA_PID}.log 2>/dev/null | grep -i "CharacterData" | head -10
    echo "=== GC LOG - Last 50 lines ==="
    tail -50 /home/lvuser/gc.log 2>/dev/null
    echo "=== STDOUT - Last 30 lines ==="
    tail -30 /home/lvuser/stdout.log 2>/dev/null
    exit 1
  fi
done

echo "SURVIVED 180s without crash!"
kill -9 $JAVA_PID 2>/dev/null
exit 0
