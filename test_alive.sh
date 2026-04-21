#!/bin/bash
SSH="ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10"
RIO="10.59.40.2"

echo "=== Getting PID ==="
pid=$($SSH admin@$RIO "pgrep -f '2026-Delta.jar' | head -1" 2>/dev/null)
echo "Got PID: [$pid]"

echo "=== Checking /proc/$pid ==="
alive=$($SSH admin@$RIO "test -d /proc/$pid && echo ALIVE || echo DEAD" 2>/dev/null)
echo "Alive check: [$alive]"

echo "=== Direct check ==="
$SSH admin@$RIO "ls -d /proc/*/status 2>/dev/null | head -5; echo '---'; cat /proc/$pid/comm 2>/dev/null; echo '---'; test -d /proc/$pid; echo EXIT_CODE=\$?" 2>/dev/null
