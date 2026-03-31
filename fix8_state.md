# Fix #8 State - Unconditional resolve_forward_pointer

## Status: DEPLOYED AND TESTED - DIFFERENT FAILURE

### New ShenandoahVerify Failure (after Fix #8)
- Referencing object: in region 1 (R = regular, active)  
- Referenced object: FileInputStream at 0xafdc1080 in region 3 (CS = collection set!)
  - mark: is_neutral (NOT forwarded!) 
  - "Forwardee: the object itself" 
  - "not marked strong", "not marked weak", "in collection set"
  - Region 3: afdc0000-afe00000 (CS state)
- **Meaning**: During the verify, a reference points to a cset object that was NEVER evacuated. Its mark word is normal (no forwarding pointer). This is DIFFERENT from the previous error (stale ref to empty region).
- **Possible causes**: (1) Evacuation failure - not enough space to evacuate, (2) Object was missed during evacuation, (3) The verify phase might be "Before Evacuation" which expects cset refs to exist (NOT a bug)
- **CRITICAL CHECK NEEDED**: Read hs_err_pid31246.log to see which verify phase triggered this. If it's "Before Evacuation", then cset references are EXPECTED and the real error is something else (perhaps the referencing object itself is the issue, or the cset object should have been marked but wasn't).
- Also run test WITHOUT ShenandoahVerify to see if basic functionality works now.

### Detailed New Error Analysis
Error: "Before Evacuation, Marked; Must be marked in complete bitmap, except j.l.r.Reference referents"
- Referencing: FileInputStream$1 at 0xafd40438 in region 1 (R, active)
  - "allocated after mark start" (above TAMS=afd40000)
  - "marked strong", "marked weak"
  - Interior location: 0xafd40440
- Referenced: FileInputStream at 0xafdc1080 in region 3 (CS = collection set!)
  - "not allocated after mark start" (below TAMS=afdfeb18)
  - "not marked strong", "not marked weak" ← THIS IS THE BUG
  - mark: is_neutral (NOT forwarded)
  - Forwardee: the object itself (never evacuated because not marked)

This is a MARKING bug, not a reference update bug. The above-TAMS scanning code in
ShenandoahFinalMarkingTask (shenandoahConcurrentMark.cpp ~lines 170-199) should handle this:
it scans above-TAMS objects, marks them in bitmap, pushes onto work queue, then mark_loop traces
their references. But somehow FileInputStream referenced by the above-TAMS FileInputStream$1 is
NOT getting marked.

### TAMS scan code analysis (shenandoahConcurrentMark.cpp)
The ShenandoahFinalMarkingTask::work() method does:
1. Drains SATB buffers
2. Scans thread stacks (since !ShenandoahStackWatermarkBarrier on ARM32)
3. Scans above-TAMS objects (!ShenandoahStackWatermarkBarrier): mark_strong_in_bitmap + push
4. Runs mark_loop to process queued objects

IMPORTANT: SHENANDOAH_OPTIMIZED_MARKTASK=0 on ARM32 (no oop truncation, full 32-bit pointers stored)
- shenandoahTaskqueue.hpp lines 125-133: #ifdef _LP64 → 1, #else → 0

### Key files to investigate next
- shenandoahMarkingContext.inline.hpp line 40: mark_strong_in_bitmap implementation
- shenandoahMark.inline.hpp: mark_through_ref, mark_loop - how references are traced
- shenandoahMark.inline.hpp line 326: another mark_strong_in_bitmap call

### DEEP ANALYSIS OF MARKING FAILURE

The above-TAMS scan WORKS for the referencing object (FileInputStream$1 IS marked strong).
But mark_loop doesn't trace its reference to FileInputStream. 

Key code flow:
1. ShenandoahFinalMarkingTask::work() at shenandoahConcurrentMark.cpp lines 125-210
2. Above-TAMS scan (lines 170-199): uses mark_strong_in_bitmap (bypasses allocated_after_mark_start check)
3. mark_loop uses mark_through_ref which calls mark_strong (WITH allocated_after_mark_start check)
4. For FileInputStream below TAMS: allocated_after_mark_start returns FALSE → proceeds to bitmap marking

The FileInputStream SHOULD be marked by mark_loop tracing. BUT IT'S NOT.

Possible causes:
1. ShenandoahMarkBitMap::mark_strong has ARM32 atomic bit operation bug
2. mark_through_ref is not being called (object iteration bug)
3. The task queue push/pop loses tasks somehow on 32-bit
4. The marking closure doesn't iterate all fields of the above-TAMS object

INVESTIGATION PLAN:
1. Read ShenandoahMarkBitMap::mark_strong (shenandoahMarkBitMap.inline.hpp)
2. Read mark_through_ref (shenandoahMark.inline.hpp) 
3. Add diagnostic prints to the above-TAMS scan and mark_through_ref
4. Run WITHOUT ShenandoahVerify to see if Fix #8 resolved the actual crashes

KEY MARKING CONTEXT CODE (shenandoahMarkingContext.inline.hpp):
- Line 33: mark_strong = !allocated_after_mark_start(obj) && bitmap.mark_strong() → for normal marking
- Line 42: mark_strong_in_bitmap = bitmap.mark_strong() → for above-TAMS explicit marking
- Line 46: is_marked(obj) = allocated_after_mark_start(obj) || bitmap.is_marked() → for checking

CRITICAL OBSERVATION: The verify says FileInputStream$1 is "marked strong" AND "marked weak". 
AND FileInputStream is "not marked strong" AND "not marked weak". This means mark_loop 
definitely processed FileInputStream$1 enough to mark it, but didn't trace its fields to 
find FileInputStream.

ALTERNATIVE THEORY: Maybe the issue is with the TASK QUEUE. On 32-bit, the non-optimized 
ShenandoahMarkTask uses separate fields. If the BufferedOverflowTaskQueue has a bug on 32-bit
(e.g., sizeof(ShenandoahMarkTask) doesn't fit the queue slot), tasks could be corrupted.
Check: sizeof(ShenandoahMarkTask) on 32-bit = sizeof(oop) + 2*bool + 2*int = 4+2+8 = 14 bytes.
Queue may expect power-of-2 aligned entries.

BUILD/DEPLOY/TEST COMMANDS:
```bash
# Build
cd /d c:\Users\vasis\Desktop\frc-openjdk-roborio-shenandoah
bash build-fast.sh 2>&1

# Deploy  
scp frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/tmp/
ssh admin@10.59.40.2 "opkg install /tmp/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite"

# Test WITHOUT verify (check if crashes are fixed)
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && /usr/local/frc/JRE/bin/java -Xint -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -Xmx64m -Xms64m -XX:ShenandoahGCHeuristics=aggressive TestAllocObjects 2>&1 | tail -20"

# Test WITH verify (check marking)
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && /usr/local/frc/JRE/bin/java -Xint -XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -XX:+ShenandoahVerify -Xmx64m -Xms64m -XX:ShenandoahGCHeuristics=aggressive TestAllocObjects 2>&1 | head -100"

# Full test suite
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && bash run_shenandoah_tests.sh 2>&1"
```

ALL FIXES APPLIED: #1-#5a, #6A/6B/6D, #7/7b/7c, #8A/8B/8C/8D. Fix #6C REVERTED.
Source files modified: shenandoahBarrierSetAssembler_arm.cpp, shenandoahBarrierSet.inline.hpp,
shenandoahConcurrentMark.cpp, plus all prior fix files (markWord.hpp, synchronizer.cpp, etc.)

## What Fix #8 Does
Makes resolve_forward_pointer UNCONDITIONAL on all store paths (previously only fired when HAS_FORWARDED gc_state was set). Also fixes LRB dst==R1 register conflict bug.

### Root Cause
ShenandoahVerify showed "Before Evacuation: Object start should be within the region" - a ConcurrentHashMap$Node allocated after mark start contained a stale from-space reference to an empty (recycled) region. The stale ref survived between GC cycles because:
1. After cycle N-1's final_updaterefs, HAS_FORWARDED is cleared
2. A mutator still has a stale from-space ref on its stack (not updated by thread root update)
3. Between cycles, HAS_FORWARDED is false, so store_at's resolve_forward_pointer doesn't fire
4. Mutator stores stale ref into new object -> stale ref in heap -> crash in next cycle

### Fix Details
- **Fix #8A**: `shenandoahBarrierSetAssembler_arm.cpp` store_at: Remove gc_state/HAS_FORWARDED check, make resolve_forward_pointer unconditional
- **Fix #8B**: Same file, cmpxchg_oop: Same treatment
- **Fix #8C**: `shenandoahBarrierSet.inline.hpp` oop_store_common, oop_cmpxchg, oop_xchg: Remove has_forwarded_objects() guard
- **Fix #8D**: Same assembler file, load_reference_barrier: Handle dst==R1 by saving to R3 (already on stack), track original_dst for correct save slot write

## All Fixes Applied
- #1: satb AS_RAW
- #2: cmpxchg aliasing
- #3: atos_merged_with_itos
- #4: markWord has_monitor (was (value()&2)!=0, fixed to (value()&3)==2)
- #5/5a: C1 store_at_resolved LRB + use_fixed_result=false
- #6A/6B/6D: Stack watermark for interpreter/sharedRuntime native return
- #6C: REVERTED (caused SIGILL in C1 return_op)
- #7/7b/7c: Conditional resolve_forward_pointer (superseded by #8)
- #8A/8B/8C/8D: UNCONDITIONAL resolve_forward_pointer + LRB dst==R1

## Test Results Before Fix #8
22/33 PASS, 5 aggressive CRASH, 6 TIMEOUT
- CRASH: TestAllocObjects, TestLotsOfCycles, TestRetainObjects, TestRefprocSanity, TestParallelRefprocSanity (all aggressive, exit=134)
- TIMEOUT: TestStringInternCleanup(aggressive), TestArrayCopyCheckCast(aggressive), TestVerifyJCStress(adaptive+aggressive), TestSieveObjects(aggressive), TestAllocIntArrays(aggressive)

## Next Commands
```bash
# 1. Install on RIO
ssh admin@10.59.40.2 "opkg install /tmp/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite"

# 2. Quick verify test
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && /usr/local/frc/JRE/bin/java -Xint -XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -XX:+ShenandoahVerify -Xmx64m -Xms64m -XX:ShenandoahGCHeuristics=aggressive TestAllocObjects 2>&1 | head -100"

# 3. Full test suite
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && bash run_shenandoah_tests.sh 2>&1"
```

## Build Commands
```bash
bash build-fast.sh 2>&1  # ~139s incremental
docker rm -f shenandoah-builder  # reset container if needed
scp frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/tmp/
```

## Key Files Modified
- `jdk17u-local/src/hotspot/cpu/arm/gc/shenandoah/shenandoahBarrierSetAssembler_arm.cpp`
- `jdk17u-local/src/hotspot/share/gc/shenandoah/shenandoahBarrierSet.inline.hpp`
- Plus all prior fixes in: markWord.hpp, synchronizer.cpp, c1_LIRAssembler_arm.cpp, macroAssembler_arm.*, templateInterpreterGenerator_arm.cpp, sharedRuntime_arm.cpp, stackWatermark.cpp, templateTable_arm.cpp
