#!/bin/bash
# Test: launch java directly  
ssh -o StrictHostKeyChecking=no admin@10.59.40.2 'bash -s' << 'REMOTE'
killall -9 java 2>/dev/null
rm -f /var/run/natinst/FRC_UserProgram.pid
sleep 1

# Run java directly as lvuser without nested quotes issue
# Use a temp script to avoid quoting hell
cat > /tmp/run_java_test.sh << 'SCRIPT'
#!/bin/bash
cd /home/lvuser
exec /usr/local/frc/JRE/bin/java \
  -Djava.lang.invoke.stringConcat=BC_SB \
  -Djava.library.path=/usr/local/frc/third-party/lib \
  -XX:+UnlockDiagnosticVMOptions \
  -XX:+UnlockExperimentalVMOptions \
  -XX:+UseShenandoahGC \
  -XX:ShenandoahGCMode=iu \
  -jar /home/lvuser/2026-Delta.jar
SCRIPT
chmod 755 /tmp/run_java_test.sh
chown lvuser /tmp/run_java_test.sh

su - lvuser -c "nohup /tmp/run_java_test.sh > /tmp/gc_bench/test_stdout.log 2>&1 &"
echo "Launched, waiting..."
sleep 10

echo "Java processes:"
pgrep -a java || echo "NONE"
echo "---"
echo "Output:"
head -5 /tmp/gc_bench/test_stdout.log 2>/dev/null || echo "no output"
REMOTE
