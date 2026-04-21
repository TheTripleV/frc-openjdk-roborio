#!/bin/bash
# Check GC logs on RIO
ssh -o StrictHostKeyChecking=no admin@10.59.40.2 'bash -s' << 'REMOTE'
for f in /tmp/gc_bench/*.log; do
    name=$(basename "$f")
    lines=$(wc -l < "$f")
    pauses=$(grep -ci pause "$f" || true)
    echo "$name: ${lines} lines, ${pauses} pauses"
done
echo "---"
# Check if g1_lowpause exists (it was config 8)
ls -la /tmp/gc_bench/ 2>/dev/null
REMOTE
