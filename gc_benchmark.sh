#!/bin/bash
# GC Benchmark Script for RoboRIO
# Tests multiple GC configurations and collects pause time data
# Run from a machine that can SSH to the RIO at 10.59.40.2
#
# Launches java directly as lvuser via temp scripts to avoid
# quoting issues and FRC launcher dependencies.

set -u  # Exit on unset vars, but NOT on command failure (SSH returns non-zero due to stderr banner)

RIO="10.59.40.2"
SSH="ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10"
SCP="scp -o StrictHostKeyChecking=no -o ConnectTimeout=10"
DURATION=60        # total seconds the robot runs per test
SETTLE_TIME=20     # seconds to wait for JVM warmup before measuring
JAR="/home/lvuser/2026-Delta.jar"
RESULTS_DIR="/tmp/gc_bench"
JAVA="/usr/local/frc/JRE/bin/java"
COMMON_ARGS="-Djava.lang.invoke.stringConcat=BC_SB -Djava.library.path=/usr/local/frc/third-party/lib"
LOCAL_RESULTS="/tmp/gc_results_local.csv"

# ---- GC configurations to test ----
# Format: "CONFIG_NAME|JVM_ARGS"
# Heap kept small: 50m-100m range so we don't OOM the 497MB RIO
CONFIGS=(
  # === SHENANDOAH IU MODE (concurrent, ultra-low pause) ===
  "shen_iu_default|-XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -XX:ShenandoahGCMode=iu -Xms50m -Xmx100m"

  "shen_iu_pretouch|-XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -XX:ShenandoahGCMode=iu -Xms50m -Xmx100m -XX:+AlwaysPreTouch -XX:-ShenandoahUncommit"

  "shen_iu_1thread|-XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -XX:ShenandoahGCMode=iu -Xms50m -Xmx100m -XX:+AlwaysPreTouch -XX:-ShenandoahUncommit -XX:ConcGCThreads=1 -XX:ParallelGCThreads=1"

  "shen_iu_aggressive|-XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -XX:ShenandoahGCMode=iu -Xms50m -Xmx100m -XX:+AlwaysPreTouch -XX:-ShenandoahUncommit -XX:ShenandoahGuaranteedGCInterval=10000 -XX:ConcGCThreads=1"

  # === SHENANDOAH SATB (normal/default) MODE ===
  "shen_satb_default|-XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -Xms50m -Xmx100m"

  "shen_satb_tuned|-XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -Xms50m -Xmx100m -XX:+AlwaysPreTouch -XX:-ShenandoahUncommit -XX:ConcGCThreads=1"

  # === G1 GC ===
  "g1_default|-XX:+UseG1GC -Xms50m -Xmx100m"

  "g1_lowpause|-XX:+UseG1GC -Xms50m -Xmx100m -XX:MaxGCPauseMillis=10 -XX:+AlwaysPreTouch -XX:ParallelGCThreads=2 -XX:ConcGCThreads=1"

  "g1_1thread|-XX:+UseG1GC -Xms50m -Xmx100m -XX:MaxGCPauseMillis=5 -XX:+AlwaysPreTouch -XX:ParallelGCThreads=1 -XX:ConcGCThreads=1 -XX:G1HeapRegionSize=1m"

  # === SERIAL GC ===
  "serial_default|-XX:+UseSerialGC -Xms50m -Xmx100m"

  "serial_pretouch|-XX:+UseSerialGC -Xms50m -Xmx100m -XX:+AlwaysPreTouch"
)

echo "============================================="
echo "  GC Benchmark Suite for RoboRIO"
echo "  $(date)"
echo "  Duration per test: ${DURATION}s (settle: ${SETTLE_TIME}s)"
echo "  Configs to test: ${#CONFIGS[@]}"
echo "============================================="

# Verify RIO connectivity
echo "Checking RIO connectivity..."
if ! $SSH admin@$RIO "echo ok" >/dev/null 2>&1; then
  echo "ERROR: Cannot connect to RIO at $RIO"
  exit 1
fi
echo "RIO is reachable."

# We launch java directly via temp scripts, so robotCommand is NOT modified.
# Just clean up any previous backup files.
$SSH admin@$RIO "rm -f /home/lvuser/robotCommand.bak.bench" 2>/dev/null || true

# Kill everything first: stop auto-restarter AND java
echo "Stopping all robot processes..."
$SSH admin@$RIO '
  PF=/var/run/natinst/FRC_UserProgram.pid
  if [ -f "$PF" ]; then
    PID=$(cat "$PF")
    if [ -d "/proc/$PID" ]; then
      PGRP=$(ps -o pgid= -p "$PID" | tr -d " ")
      kill -TERM -- -"$PGRP" 2>/dev/null
      sleep 1
      kill -9 -- -"$PGRP" 2>/dev/null
    fi
    rm -f "$PF"
  fi
  killall -9 java 2>/dev/null
  killall -9 frcRunRobot.sh 2>/dev/null
' 2>/dev/null || true
sleep 3

# Setup results dir on RIO - writable by lvuser!
$SSH admin@$RIO "mkdir -p $RESULTS_DIR; chmod 777 $RESULTS_DIR; rm -rf $RESULTS_DIR/*"

# Verify java is dead
remaining=$($SSH admin@$RIO "pgrep java 2>/dev/null || true")
if [ -n "$remaining" ]; then
  echo "WARNING: Java still running (PID $remaining), force killing..."
  $SSH admin@$RIO "kill -9 $remaining" 2>/dev/null || true
  sleep 2
fi

echo "System clear. Free memory:"
$SSH admin@$RIO "free -m"
echo ""

kill_robot() {
  # Kill all java and robot processes
  $SSH admin@$RIO '
    killall -9 java 2>/dev/null
    killall -9 frcRunRobot.sh 2>/dev/null
    PF=/var/run/natinst/FRC_UserProgram.pid
    if [ -f "$PF" ]; then
      PID=$(cat "$PF")
      [ -d "/proc/$PID" ] && kill -9 "$PID" 2>/dev/null
      rm -f "$PF"
    fi
  ' 2>/dev/null || true
  sleep 2
  # Verify java is truly dead
  local retries=0
  while [ $retries -lt 5 ]; do
    local remaining=$($SSH admin@$RIO "pgrep java 2>/dev/null | wc -l" 2>/dev/null)
    if [ "$remaining" = "0" ] || [ -z "$remaining" ]; then
      break
    fi
    echo "  Waiting for java to die ($remaining processes remaining)..."
    $SSH admin@$RIO "killall -9 java 2>/dev/null" 2>/dev/null || true
    sleep 2
    retries=$((retries + 1))
  done
}

launch_java() {
  # Launch java with given args by creating a temp script on the RIO
  # This avoids all quoting issues with su -c and nested quotes
  local gc_args="$1"
  local gc_log="$2"
  local log_args="-Xlog:gc*:file=$gc_log:time,uptime,level,tags"
  
  # Create a launch script on the RIO
  $SSH admin@$RIO "cat > /tmp/gc_bench/launch.sh" << LAUNCH_EOF
#!/bin/bash
cd /home/lvuser
exec $JAVA $COMMON_ARGS $gc_args $log_args -jar $JAR
LAUNCH_EOF
  
  $SSH admin@$RIO "chmod 755 /tmp/gc_bench/launch.sh; chown lvuser /tmp/gc_bench/launch.sh"
  
  # Launch as lvuser
  $SSH admin@$RIO 'su - lvuser -c "nohup /tmp/gc_bench/launch.sh > /tmp/gc_bench/stdout.log 2>&1 &"'
}

run_test() {
  local name="$1"
  local gc_args="$2"
  local gc_log="/tmp/gc_bench/${name}_gc.log"

  echo ""
  echo "=========================================="
  echo "  Testing: $name"
  echo "  Args: $gc_args"
  echo "=========================================="

  # Kill any existing robot code
  kill_robot

  # Check memory before starting
  echo "  Pre-test memory:"
  $SSH admin@$RIO "free -m | grep -E 'Mem|buffers'"

  # Clear old GC log and stdout
  $SSH admin@$RIO "rm -f $gc_log /tmp/gc_bench/stdout.log" 2>/dev/null || true

  # Launch java using the temp-script method
  echo "  Launching java..."
  launch_java "$gc_args" "$gc_log"

  # Wait for java to start
  echo "  Waiting for JVM to start..."
  sleep 8
  local waited=8
  local pid=""
  while [ $waited -lt 45 ]; do
    pid=$($SSH admin@$RIO "pgrep java | head -1" 2>/dev/null || true)
    if [ -n "$pid" ]; then
      local check=$($SSH admin@$RIO "test -d /proc/$pid && echo YES || echo NO" 2>/dev/null || true)
      if [ "$check" = "YES" ]; then
        echo "  Java PID: $pid (after ${waited}s)"
        break
      fi
      pid=""
    fi
    sleep 3
    waited=$((waited + 3))
  done

  if [ -z "$pid" ]; then
    echo "  FAILED TO START after ${waited}s!"
    echo "  stdout/stderr:"
    $SSH admin@$RIO "cat /tmp/gc_bench/stdout.log 2>/dev/null | tail -20" || true
    echo "$name|FAILED|0|0|0|0|0|0|0|FAILED_TO_START" >> "$LOCAL_RESULTS"
    return 1
  fi
  echo "  Started PID $pid (after ${waited}s)"

  # Settle time - let JVM warm up
  echo "  Settling for ${SETTLE_TIME}s..."
  sleep $SETTLE_TIME

  # Check still alive after settle (use /proc check, not kill -0 which fails cross-user)
  local alive_check=$($SSH admin@$RIO "test -d /proc/$pid && echo ALIVE || echo DEAD" 2>/dev/null)
  if [ "$alive_check" != "ALIVE" ]; then
    echo "  CRASHED during warmup! (status: $alive_check)"
    $SSH admin@$RIO "cat /tmp/gc_bench/stdout.log 2>/dev/null | tail -30" || true
    echo "$name|CRASHED_WARMUP|0|0|0|0|0|0|0|DIED_IN_WARMUP" >> "$LOCAL_RESULTS"
    return 1
  fi
  echo "  Still alive after warmup."

  local measure_time=$((DURATION - SETTLE_TIME))
  echo "  Measuring for ${measure_time}s..."

  # Collect memory stats periodically
  $SSH admin@$RIO "for i in \$(seq 1 $((measure_time / 5))); do
    if ! test -d /proc/$pid; then echo 'PROCESS_DEAD'; break; fi
    free -m | grep Mem | awk '{print \"MEM \"\$3\" \"\$4}'
    sleep 5
  done" > "/tmp/gc_bench_${name}_memstats.txt" 2>/dev/null &
  local stats_pid=$!

  sleep $measure_time

  # Check if java survived the test (use /proc check, not kill -0 which fails cross-user)
  local alive=$($SSH admin@$RIO "test -d /proc/$pid && echo yes || echo no" 2>/dev/null)

  # Wait for stats collection to finish
  wait $stats_pid 2>/dev/null || true

  # Kill the robot
  kill_robot

  if [ "$alive" != "yes" ]; then
    echo "  CRASHED during measurement!"
    $SSH admin@$RIO "cat /tmp/gc_bench/stdout.log 2>/dev/null | tail -20" || true
    echo "$name|CRASHED|0|0|0|0|0|0|0|PROCESS_DIED" >> "$LOCAL_RESULTS"
    return 1
  fi

  echo "  Completed successfully. Pulling GC log..."

  # Pull the GC log locally
  local local_gc="/tmp/gc_bench_${name}_gc.log"
  $SCP admin@$RIO:$gc_log "$local_gc" 2>/dev/null || true

  if [ ! -f "$local_gc" ] || [ ! -s "$local_gc" ]; then
    echo "  No/empty GC log!"
    echo "$name|NO_LOG|0|0|0|0|0|0|0|NO_GC_LOG" >> "$LOCAL_RESULTS"
    return 1
  fi

  echo "  GC log size: $(wc -c < "$local_gc") bytes, $(wc -l < "$local_gc") lines"

  # Parse all pause durations (ms) from the GC log  
  # Unified logging format: lines containing "Pause" with duration like "1.234ms"
  local pause_data=$(grep -i 'pause' "$local_gc" | grep -oP '\d+\.\d+ms' | grep -oP '[\d.]+(?=ms)' || true)
  
  if [ -z "$pause_data" ]; then
    echo "  No pause data found in GC log. Checking format..."
    head -5 "$local_gc"
    echo "$name|NO_PAUSES|0|0|0|0|0|0|0|NO_PAUSE_DATA" >> "$LOCAL_RESULTS"
    return 1
  fi

  # Calculate stats
  local stats=$(echo "$pause_data" | awk '
    BEGIN { min=999999; max=0; sum=0; count=0; }
    {
      val = $1 + 0;
      if (val > 0) {
        sum += val; count++;
        if (val < min) min = val;
        if (val > max) max = val;
        vals[count] = val;
      }
    }
    END {
      if (count == 0) { print "0|0|0|0|0|0|0"; exit }
      avg = sum / count;
      # Sort for percentiles
      for (i = 1; i <= count; i++)
        for (j = i+1; j <= count; j++)
          if (vals[i] > vals[j]) { tmp=vals[i]; vals[i]=vals[j]; vals[j]=tmp; }
      p50 = vals[int(count*0.5) > 0 ? int(count*0.5) : 1];
      p95 = vals[int(count*0.95) > 0 ? int(count*0.95) : 1];
      p99 = vals[int(count*0.99) > 0 ? int(count*0.99) : 1];
      printf "%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%d", avg, min, max, p50, p95, p99, count;
    }')

  local avg=$(echo "$stats" | cut -d'|' -f1)
  local min_p=$(echo "$stats" | cut -d'|' -f2)
  local max_p=$(echo "$stats" | cut -d'|' -f3)
  local p50=$(echo "$stats" | cut -d'|' -f4)
  local p95=$(echo "$stats" | cut -d'|' -f5)
  local p99=$(echo "$stats" | cut -d'|' -f6)
  local num_pauses=$(echo "$stats" | cut -d'|' -f7)

  # Count pause types
  local init_mark=$(grep -c "Pause Init Mark" "$local_gc" 2>/dev/null || echo 0)
  local final_mark=$(grep -c "Pause Final Mark" "$local_gc" 2>/dev/null || echo 0)
  local init_update=$(grep -c "Pause Init Update" "$local_gc" 2>/dev/null || echo 0)
  local final_update=$(grep -c "Pause Final Update" "$local_gc" 2>/dev/null || echo 0)
  local degenerated=$(grep -c "Pause Degenerated" "$local_gc" 2>/dev/null || echo 0)
  local full_gc=$(grep -c "Pause Full" "$local_gc" 2>/dev/null || echo 0)
  local young_gc=$(grep -c "Pause Young" "$local_gc" 2>/dev/null || echo 0)

  local breakdown="IM=${init_mark},FM=${final_mark},IU=${init_update},FU=${final_update},DG=${degenerated},FULL=${full_gc},YNG=${young_gc}"

  echo "  ┌─────────────────────────────────────"
  echo "  │ Pauses:  $num_pauses total"
  echo "  │ Avg:     ${avg}ms"
  echo "  │ Min:     ${min_p}ms"
  echo "  │ Max:     ${max_p}ms"
  echo "  │ P50:     ${p50}ms"
  echo "  │ P95:     ${p95}ms"
  echo "  │ P99:     ${p99}ms"
  echo "  │ Types:   $breakdown"
  echo "  └─────────────────────────────────────"

  echo "$name|OK|$avg|$min_p|$max_p|$p50|$p95|$p99|$num_pauses|$breakdown" >> "$LOCAL_RESULTS"

  echo "  Done: $name"
  return 0
}

# Initialize CSV
echo "config|status|avg_ms|min_ms|max_ms|p50_ms|p95_ms|p99_ms|num_pauses|breakdown" > "$LOCAL_RESULTS"

# Run all tests
total=${#CONFIGS[@]}
current=0
for config_line in "${CONFIGS[@]}"; do
  current=$((current + 1))
  IFS='|' read -r cfg_name cfg_args <<< "$config_line"
  echo ""
  echo ">>>>> TEST $current / $total <<<<<"
  run_test "$cfg_name" "$cfg_args"
done

# Clean up: kill benchmark java, restore original state
echo ""
echo "Cleaning up..."
kill_robot
# Remove backup file if it exists (robotCommand was never modified)
$SSH admin@$RIO "rm -f /home/lvuser/robotCommand.bak.bench /tmp/gc_bench/launch.sh" 2>/dev/null || true
echo "Benchmark complete. Robot code is stopped. Redeploy or restart manually."

echo ""
echo "============================================="
echo "  RESULTS SUMMARY"
echo "============================================="
echo ""
# Pretty print results
printf "%-22s %-8s %8s %8s %8s %8s %8s %8s %6s %s\n" \
  "CONFIG" "STATUS" "AVG_ms" "MIN_ms" "MAX_ms" "P50_ms" "P95_ms" "P99_ms" "COUNT" "BREAKDOWN"
echo "-----------------------------------------------------------------------------------------------------------"
tail -n +2 "$LOCAL_RESULTS" | while IFS='|' read -r cfg st avg mn mx p50 p95 p99 cnt bkd; do
  printf "%-22s %-8s %8s %8s %8s %8s %8s %8s %6s %s\n" \
    "$cfg" "$st" "$avg" "$mn" "$mx" "$p50" "$p95" "$p99" "$cnt" "$bkd"
done
echo ""
echo "Raw CSV: $LOCAL_RESULTS"
echo "GC logs: /tmp/gc_bench_*_gc.log"
echo "============================================="
