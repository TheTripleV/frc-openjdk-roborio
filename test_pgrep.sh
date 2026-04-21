#!/bin/bash
# Debug: launch java and test pgrep variants
ssh -o StrictHostKeyChecking=no admin@10.59.40.2 'bash -s' << 'REMOTE'
# Kill any existing
killall -9 java 2>/dev/null
sleep 1

# Create launch script
mkdir -p /tmp/gc_bench
cat > /tmp/gc_bench/launch.sh << 'SCRIPT'
#!/bin/bash
cd /home/lvuser
exec /usr/local/frc/JRE/bin/java \
  -Djava.lang.invoke.stringConcat=BC_SB \
  -Djava.library.path=/usr/local/frc/third-party/lib \
  -XX:+UnlockDiagnosticVMOptions \
  -XX:+UnlockExperimentalVMOptions \
  -XX:+UseShenandoahGC \
  -XX:ShenandoahGCMode=iu \
  -Xms50m -Xmx100m \
  -jar /home/lvuser/2026-Delta.jar
SCRIPT
chmod 755 /tmp/gc_bench/launch.sh
chown lvuser /tmp/gc_bench/launch.sh
chmod 777 /tmp/gc_bench

su - lvuser -c "nohup /tmp/gc_bench/launch.sh > /tmp/gc_bench/stdout.log 2>&1 &"
echo "Launched, waiting 10s..."
sleep 10

echo "=== pgrep variants ==="
echo -n "pgrep java: "; pgrep java || echo "(empty)"
echo -n "pgrep -x java: "; pgrep -x java || echo "(empty)"
echo -n "pgrep -a java: "; pgrep -a java || echo "(empty)"
echo -n "pgrep -f java: "; pgrep -f java || echo "(empty)"

echo "=== ps output ==="
ps aux | grep java | grep -v grep

echo "=== /proc comm ==="
for pid in $(pgrep java 2>/dev/null); do
  echo "PID $pid comm=$(cat /proc/$pid/comm 2>/dev/null)"
done

echo "=== Kill ==="
killall -9 java 2>/dev/null
echo "Done"
REMOTE
