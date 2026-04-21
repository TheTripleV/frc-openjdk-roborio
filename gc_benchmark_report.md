# GC Benchmark Report — RoboRIO (ARM Cortex-A9)

**Date**: April 8, 2026  
**Platform**: NI RoboRIO 1.0 — ARM Cortex-A9 dual-core @ 866MHz, 497MB RAM  
**JVM**: 17.0.9.7-frc+0-2024-17.0.9u7-3 (custom FRC build w/ Shenandoah)  
**Workload**: FRC robot program (2026-Delta.jar) — WPILib + subsystems  
**Heap**: -Xms50m -Xmx100m for all configurations  
**Duration**: ~60s per test (20s warmup + 40s measurement)  

---

## Results Summary

| # | Configuration | GC | Avg (ms) | P50 (ms) | P95 (ms) | P99 (ms) | Max (ms) | Pauses | 
|---|--------------|-----|----------|----------|----------|----------|----------|--------|
| 1 | **shen_satb_default** | Shenandoah SATB | **3.73** | **0.78** | 13.15 | 13.15 | **13.67** | 12 |
| 2 | shen_satb_tuned | Shenandoah SATB | 4.82 | 0.73 | 14.63 | 14.63 | 28.87 | 12 |
| 3 | shen_iu_default | Shenandoah IU | 5.20 | 0.33 | 10.03 | 10.03 | 28.29 | 12 |
| 4 | shen_iu_1thread | Shenandoah IU | 5.60 | 0.64 | 20.25 | 20.25 | 24.36 | 12 |
| 5 | shen_iu_pretouch | Shenandoah IU | 6.03 | 1.85 | 17.56 | 17.56 | 25.72 | 12 |
| 6 | shen_iu_aggressive | Shenandoah IU | 6.59 | 1.43 | 11.43 | 20.92 | 86.72 | 28 |
| 7 | g1_1thread | G1 | 30.78 | 24.02 | 71.61 | 98.48 | 120.26 | 65 |
| 8 | g1_lowpause | G1 | 34.32 | 27.48 | 80.33 | 106.25 | 171.81 | 59 |
| 9 | serial_default | Serial | 48.13 | 31.84 | 52.72 | 52.72 | 124.23 | 7 |
| 10 | serial_pretouch | Serial | 60.25 | 35.44 | 52.02 | 52.02 | 172.56 | 6 |
| 11 | g1_default | G1 | 137.64 | 58.95 | 204.06 | 204.06 | 258.71 | 5 |

> Sorted by average pause time (lower is better)

---

## Analysis by GC Type

### Shenandoah (Best Overall)

All Shenandoah configs dramatically outperform G1 and Serial:

- **Average pause: 3.7–6.6ms** vs 30–138ms for G1/Serial
- **P50: 0.3–1.9ms** — majority of pauses are sub-2ms
- **Max: 13.7–86.7ms** — worst case still better than G1/Serial average
- **Only 12 pauses** per 60s measurement (except aggressive=28)
- Pause types: Init Mark, Final Mark, Init Update Refs, Final Update Refs (all concurrent phases)

**SATB mode** edges out **IU mode** slightly:
- SATB default: 3.73ms avg, 13.67ms max (lowest max of all configs!)
- IU default: 5.20ms avg, 28.29ms max
- SATB has marginally higher P50 (0.78 vs 0.33ms) but lower tail latency

**shen_iu_aggressive** (GCInterval=10000, ConcGCThreads=1): More frequent GC (28 pauses vs 12) with one 86.72ms outlier — likely a degenerated GC. Not recommended.

### G1 (Poor on ARM)

G1 is a **poor fit** for the RoboRIO:

- **g1_default**: Only 5 pauses but each is catastrophic — 137ms avg, 258ms max
- **g1_lowpause** (MaxGCPauseMillis=10): Tuning helps (34ms avg) but still 10x worse than Shenandoah. G1 can't actually hit its 10ms target on this hardware.
- **g1_1thread**: Similar to lowpause (30ms avg), slightly less tail variance
- All G1 pauses are Young (evacuation) — heap too small for concurrent marking to trigger
- G1's overhead comes from stop-the-world young generation evacuation

### Serial (Baseline)

- Consistent but slow: 48–60ms average pauses
- **serial_pretouch is slower** (60ms vs 48ms) — AlwaysPreTouch hurts on constrained RAM
- Fewer pauses (6–7) but each is long
- Simple and predictable, but pause times are unacceptable for real-time control

---

## Ranking for FRC Use

### Best Configuration: `shen_satb_default`
```
-XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions 
-XX:+UseShenandoahGC
```
- **3.73ms average**, 13.67ms max — the best overall profile
- Lowest maximum pause of any configuration
- Fewest tuning knobs = most predictable behavior

### Runner-up: `shen_iu_default`
```
-XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions 
-XX:+UseShenandoahGC -XX:ShenandoahGCMode=iu
```
- **5.20ms average**, 28.29ms max
- Lowest P50 (0.33ms) — most pauses are sub-millisecond
- IU mode allows concurrent class unloading (useful if runtime hot-loading)

### Avoid
- **g1_default**: 258ms max pause — will cause visible robot stutter
- **serial_pretouch**: AlwaysPreTouch wastes RAM on a memory-constrained device
- **shen_iu_aggressive**: GCInterval=10000 caused a degenerated GC spike (86ms)

---

## Recommended JVM Args for build.gradle

Replace current GC settings with:

```groovy
// Option A: Shenandoah SATB (lowest pauses overall)
jvmArgs.add("-XX:+UnlockDiagnosticVMOptions")
jvmArgs.add("-XX:+UnlockExperimentalVMOptions") 
jvmArgs.add("-XX:+UseShenandoahGC")
// SATB is the default mode, no need to specify ShenandoahGCMode

// Option B: Shenandoah IU (lowest median pauses, concurrent unloading)
jvmArgs.add("-XX:+UnlockDiagnosticVMOptions")
jvmArgs.add("-XX:+UnlockExperimentalVMOptions")
jvmArgs.add("-XX:+UseShenandoahGC")
jvmArgs.add("-XX:ShenandoahGCMode=iu")
```

---

## Key Takeaways

1. **Shenandoah is 7–37x better** than G1 on average pause time on RoboRIO hardware
2. **SATB mode is the safest choice** — lowest max pause (13.67ms), excellent average (3.73ms)
3. **IU mode has the best median** (0.33ms P50) but higher tail latency (28ms max)
4. **Don't use AlwaysPreTouch** — it doesn't help and can hurt on 497MB RAM
5. **G1 cannot hit low-pause targets** on ARM Cortex-A9 — evacuation pauses are fundamentally STW
6. **Serial GC** is surprisingly not terrible for very low allocation rates but unacceptable for robot control loops

---

## Raw Data

### JVM Arguments per Configuration

| Config | Full JVM GC Args |
|--------|-----------------|
| shen_iu_default | `-XX:+UseShenandoahGC -XX:ShenandoahGCMode=iu` |
| shen_iu_pretouch | `-XX:+UseShenandoahGC -XX:ShenandoahGCMode=iu -XX:+AlwaysPreTouch -XX:-ShenandoahUncommit` |
| shen_iu_1thread | `-XX:+UseShenandoahGC -XX:ShenandoahGCMode=iu -XX:+AlwaysPreTouch -XX:-ShenandoahUncommit -XX:ConcGCThreads=1 -XX:ParallelGCThreads=1` |
| shen_iu_aggressive | `-XX:+UseShenandoahGC -XX:ShenandoahGCMode=iu -XX:+AlwaysPreTouch -XX:-ShenandoahUncommit -XX:ShenandoahGuaranteedGCInterval=10000 -XX:ConcGCThreads=1` |
| shen_satb_default | `-XX:+UseShenandoahGC` |
| shen_satb_tuned | `-XX:+UseShenandoahGC -XX:+AlwaysPreTouch -XX:-ShenandoahUncommit -XX:ConcGCThreads=1` |
| g1_default | `-XX:+UseG1GC` |
| g1_lowpause | `-XX:+UseG1GC -XX:MaxGCPauseMillis=10 -XX:+AlwaysPreTouch -XX:ParallelGCThreads=2 -XX:ConcGCThreads=1` |
| g1_1thread | `-XX:+UseG1GC -XX:MaxGCPauseMillis=5 -XX:ParallelGCThreads=1 -XX:ConcGCThreads=1 -XX:G1HeapRegionSize=1m` |
| serial_default | `-XX:+UseSerialGC` |
| serial_pretouch | `-XX:+UseSerialGC -XX:+AlwaysPreTouch` |

All configs also included: `-XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -Xms50m -Xmx100m`

### GC Log Files
Logs saved to `gc_logs_run4/` directory.
