# Current Debugging State - MUST READ FIRST

## ROOT CAUSE FOUND: Metaspace klass check in mark_through_ref

### THE BUG
In `shenandoahMark.inline.hpp` lines 291-300, there is a custom klass check that was added:
```cpp
Klass* k = obj->klass_or_null_acquire();
if (k == NULL || !Metaspace::contains(k)) {
  log_debug(gc)("Shenandoah: mark_through_ref: oop ...");
  return;  // <-- SILENTLY DROPS VALID OOPS
}
```
This check causes mark_through_ref to SILENTLY RETURN without marking valid objects whose
klass pointer doesn't pass Metaspace::contains(). This prevents the FileInputStream from
being marked, causing the verify failure and crashes.

### FIX: Remove or weaken the Metaspace klass check
Remove lines 291-300 in shenandoahMark.inline.hpp. The check was too aggressive and
incorrectly filters out valid oops. The original HotSpot code doesn't have this check.

### Files to modify
- `jdk17u-local/src/hotspot/share/gc/shenandoah/shenandoahMark.inline.hpp` lines 291-300
  Remove the Metaspace::contains klass check block

### Build/Deploy/Test
```bash
cd /d c:\Users\vasis\Desktop\frc-openjdk-roborio-shenandoah
bash build-fast.sh 2>&1
scp frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/tmp/
ssh admin@10.59.40.2 "opkg install /tmp/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite"
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && /usr/local/frc/JRE/bin/java -Xint -XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -XX:+ShenandoahVerify -Xmx64m -Xms64m -XX:ShenandoahGCHeuristics=aggressive TestAllocObjects 2>&1 | head -100"
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && bash run_shenandoah_tests.sh 2>&1"
```

### Symptoms
1. ShenandoahVerify: "Before Evacuation, Marked; Must be marked in complete bitmap"
   - FileInputStream$1 (above TAMS, IS marked → above-TAMS scan WORKS)
   - FileInputStream (below TAMS, NOT marked → mark_loop DOESN'T trace references)
2. Without verify: NullPointerException during boot layer init (consequence of marking failure)

### The Bug
The above-TAMS scan in ShenandoahFinalMarkingTask::work() (shenandoahConcurrentMark.cpp lines 170-199) correctly marks above-TAMS objects and pushes them onto the work queue. But mark_loop doesn't properly trace their references to mark below-TAMS objects.

### Investigation Plan (in order)
1. Read shenandoahMarkBitMap.inline.hpp - check mark_strong() atomics on ARM32
2. Read shenandoahMark.inline.hpp - check mark_through_ref and how marking closure traces oop fields
3. Check sizeof(ShenandoahMarkTask) on 32-bit - might not fit queue entry slots
4. Add diagnostic prints to verify above-TAMS scan pushes correctly and mark_loop processes tasks

### Key Code Locations
- shenandoahConcurrentMark.cpp:170-199 - Above-TAMS scan code
- shenandoahMarkingContext.inline.hpp:33 - mark_strong (with allocated_after_mark_start check)
- shenandoahMarkingContext.inline.hpp:42 - mark_strong_in_bitmap (WITHOUT check, used by above-TAMS scan)
- shenandoahMark.inline.hpp - mark_through_ref, mark_loop
- shenandoahMarkBitMap.inline.hpp - bitmap atomic operations
- shenandoahTaskqueue.hpp:125-133 - SHENANDOAH_OPTIMIZED_MARKTASK=0 on 32-bit

### All Fixes Applied
#1(satb AS_RAW), #2(cmpxchg aliasing), #3(atos_merged), #4(markWord has_monitor),
#5/5a(C1 store LRB), #6A/6B/6D(watermarks), #6C REVERTED, #7/7b/7c+#8A/8B/8C/8D(unconditional resolve_forward_pointer + LRB dst==R1)

### Build/Deploy/Test Commands
```bash
# Build
cd /d c:\Users\vasis\Desktop\frc-openjdk-roborio-shenandoah
bash build-fast.sh 2>&1

# Deploy
scp frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/tmp/
ssh admin@10.59.40.2 "opkg install /tmp/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite"

# Test with verify
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && /usr/local/frc/JRE/bin/java -Xint -XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -XX:+ShenandoahVerify -Xmx64m -Xms64m -XX:ShenandoahGCHeuristics=aggressive TestAllocObjects 2>&1 | head -100"

# Test without verify
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && /usr/local/frc/JRE/bin/java -Xint -XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -Xmx64m -Xms64m -XX:ShenandoahGCHeuristics=aggressive TestAllocObjects 2>&1 | tail -30"

# Full test suite
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && bash run_shenandoah_tests.sh 2>&1"
```

### Pre-Fix8 Test Results: 22/33 PASS, 5 CRASH(aggressive), 6 TIMEOUT
CRASH: TestAllocObjects, TestLotsOfCycles, TestRetainObjects, TestRefprocSanity, TestParallelRefprocSanity
TIMEOUT: TestStringInternCleanup, TestArrayCopyCheckCast, TestVerifyJCStress(x2), TestSieveObjects, TestAllocIntArrays

### Modified Source Files
- jdk17u-local/src/hotspot/cpu/arm/gc/shenandoah/shenandoahBarrierSetAssembler_arm.cpp (Fixes 8A/8B/8D)
- jdk17u-local/src/hotspot/share/gc/shenandoah/shenandoahBarrierSet.inline.hpp (Fix 8C)
- jdk17u-local/src/hotspot/share/gc/shenandoah/shenandoahConcurrentMark.cpp (above-TAMS scan, prior session)
- Plus all prior fix files (markWord.hpp, synchronizer.cpp, c1_LIRAssembler_arm.cpp, etc.)
