#!/bin/bash
# Quick test: launch java directly as lvuser
ssh -o StrictHostKeyChecking=no admin@10.59.40.2 'bash -s' << 'REMOTE'
# Kill any existing java
killall -9 java 2>/dev/null
killall -9 frcRunRobot.sh 2>/dev/null
rm -f /var/run/natinst/FRC_UserProgram.pid
sleep 2

# Read what's in robotCommand
CMD=$(cat /home/lvuser/robotCommand)
echo "Command: $CMD"

# Launch java as lvuser directly, redirecting output
su - lvuser -c "cd /home/lvuser && $CMD > /tmp/gc_bench/test_stdout.log 2>&1 &"
sleep 8

# Check if java is running
echo "Java processes:"
pgrep -a java || echo "NONE"
echo "PID file:"
cat /var/run/natinst/FRC_UserProgram.pid 2>/dev/null || echo "none"
REMOTE
