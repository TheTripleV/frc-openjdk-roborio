# Fix #7 State - Forwarding Resolution in Store Barriers (UPDATED)

## CRITICAL: Fix #7c BROKE JVM BOOT!
- `-Xint -version` hangs/dies — JVM cannot even boot with Fix #7c applied
- NullPointerException: Cannot invoke "ModuleDescriptor.name()" because "local2" is null
- The resolve_forwarded() in oop_store_common is being called when objects are NOT forwarded
- During early JVM boot, mark words may look like forwarding pointers due to:
  - Objects in recycled/zeroed regions reading garbage mark words
  - Or some other corruption
- **MUST REVERT Fix #7c** from shenandoahBarrierSet.inline.hpp
- Fix #7 (ASM store_at) and Fix #7b (ASM cmpxchg_oop) are OK — they check HAS_FORWARDED gc_state
- Fix #7c doesn't check gc_state, only ShenandoahLoadRefBarrier compile-time flag + null check
- **FIX: Add has_forwarded_objects() check to Fix #7c, OR revert and only use ASM fixes**

## Test Results WITH Fix #7c (broken):
- 22/33 pass (same as before)
- ALL aggressive crash/timeout
- -Xint -version: HANGS/CRASHES — JVM BOOT BROKEN
- -Xint aggressive: NullPointerException

## FIX APPLIED:
- Added `ShenandoahHeap::heap()->has_forwarded_objects()` guard to ALL three resolve_forwarded calls
- All 3 now: `if (ShenandoahLoadRefBarrier && value != NULL && ShenandoahHeap::heap()->has_forwarded_objects())`
- Need to BUILD, DEPLOY, TEST

## CONTINUATION PLAN:
1. Build: `bash build-fast.sh 2>&1; echo "Exit: $? Elapsed: ${SECONDS}s"`
2. Deploy: `scp frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/tmp/ && ssh admin@10.59.40.2 "opkg install /tmp/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite"`
3. Verify boot: `ssh admin@10.59.40.2 "timeout 10 /usr/local/frc/JRE/bin/java -Xint -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -Xmx150m -version 2>&1"`
4. Run full suite: `ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && bash run_shenandoah_tests.sh 2>&1"`

## ALL FIXES IN CODE:
- Fix#1: AS_RAW in satb_write_barrier_pre (shenandoahBarrierSetAssembler_arm.cpp)
- Fix#2: cmpxchg_oop register aliasing (shenandoahBarrierSetAssembler_arm.cpp)
- Fix#3: atos_merged_with_itos (templateInterpreterGenerator_arm.cpp)
- Fix#4: markWord has_monitor bit mask (markWord.hpp, synchronizer.cpp)
- Fix#5: C1 store-value LRB in store_at_resolved (c1_LIRAssembler_arm.cpp)
- Fix#5a: use_fixed_result=false register clobber (c1_LIRAssembler_arm.cpp)
- Fix#6A: safepoint_poll at_return overload (macroAssembler_arm.hpp/cpp)
- Fix#6B: interpreter native return watermark (templateInterpreterGenerator_arm.cpp)
- Fix#6D: sharedRuntime native wrapper watermark (sharedRuntime_arm.cpp)
- Fix#7: ASM store_at resolve_forward_pointer (shenandoahBarrierSetAssembler_arm.cpp ~line 480)
- Fix#7b: ASM cmpxchg_oop resolve_forward_pointer (shenandoahBarrierSetAssembler_arm.cpp ~line 526)
- Fix#7c: C++ oop_store_common/oop_cmpxchg/oop_xchg with has_forwarded_objects guard (shenandoahBarrierSet.inline.hpp)

## REVERTED:
- Fix#6C: C1 return_op watermark (REVERTED - caused SIGILL)

## KEY DIAGNOSTIC RESULTS:
1. **Passive mode PASSES ALL tests** → bug is ONLY in concurrent phases
2. **-Xlog:gc shows crash after GC(8) cleanup** (t=0.657s) → stale ref survives past recycling
3. **Error types**: IncompatibleClassChangeError (ConcHashMap$Node != ModuleFinder), ClassCastException (KeyValueHolder != ModuleReference), NullPointerException, SIGSEGV (null+offset)
4. **All aggressive crashes during module bootstrap** (0.6-0.9s)
5. **resolve_forwarded on stores NOT ENOUGH** — forwarding ptrs gone after recycling

## MOST LIKELY ROOT CAUSES (prioritized):
1. **Concurrent update thread roots** — may miss ARM32-specific root processing
2. **Stack watermark NOT processing current frame at STW** — stale refs in current frame persist
3. **ARM32 weak memory ordering** — SATB buffer / barrier writes may be reordered
4. **LRB cset check** — region_size_shift or in_cset_fast_test_addr might be wrong on ARM32
5. **Concurrent marking** — missing live objects on ARM32 due to weak memory model

## INVESTIGATION PLAN:
1. Check ShenandoahRootProcessor and thread root processing for ARM32 specifics
2. Check StackWatermark::on_safepoint / start_processing — does it process ALL frames?
3. Check ARM32 memory barrier instructions in SATB pre-barrier
4. Check region_size_bytes_shift_jint() value at runtime (should be 18 for 256K regions)
5. Try running with -XX:ShenandoahRegionSize=256K (explicit) or larger regions
6. Try running with -XX:+ShenandoahStoreValEnqueueBarrier if it exists
7. Try adding dmb after SATB buffer write in shenandoah_write_barrier_pre

## STACK WALKER INVESTIGATION RESULT (DISPROVEN):
- Stack walker early termination at frame_size=0 stubs is LEGITIMATE (bottom-of-stack)
- No corrupt frame_size warnings during aggressive GC
- Walk stops at stub frames at the BOTTOM of each thread's stack
- Walking from top to bottom, so frames above stubs ARE processed
- **Stack walker is NOT the cause of aggressive crashes**

## REMAINING HYPOTHESES (prioritized):
### H1: ARM32 Weak Memory Model — SATB Pre-Barrier
- ARM32 has weak memory ordering (writes can be reordered)
- The SATB pre-barrier writes old value to SATB buffer then increments index
- WITHOUT a DMB (data memory barrier), the GC marking thread may not see the buffered value
- This would cause concurrent marking to MISS some live objects
- Those objects get collected → memory reused → stale references
- **FIX**: Add `__ dmb(DMB_ish)` after SATB buffer store in shenandoah_write_barrier_pre

### H2: LRB in_cset_fast_test Check
- The LRB uses `in_cset_fast_test_addr()` with `region_size_bytes_shift`
- If either is wrong on ARM32, objects in cset won't be resolved by LRB
- From-space refs would escape into local variables → stored into heap → stale
- **CHECK**: Print in_cset_fast_test_addr and region_size_bytes_shift at JVM startup

### H3: Concurrent Marking Missing Objects
- Related to H1 — if SATB buffers aren't visible, live objects marked as dead
- Also: ARM32 frame walking during concurrent marking may skip oop map entries
- **CHECK**: Run with -XX:+ShenandoahVerify -Xmx48m to catch at marking time

### H4: RawAccess in Module/Class Loading
- Some internal JVM code might use RawAccess<>::oop_store() bypassing ALL barriers
- Module system startup creates/stores many objects rapidly
- **CHECK**: grep for RawAccess.*oop_store in jdk source, especially modules code

## IMMEDIATE NEXT STEP (IMPLEMENT NOW):
Add DMB after SATB pre-barrier buffer write in shenandoahBarrierSetAssembler_arm.cpp.
Location: shenandoah_write_barrier_pre() function, after the store to SATB queue buffer.
Look for: `__ str(val, Address(tmp1, -wordSize, pre_indexed))` or similar.
Add: `__ dmb(DMB_ish)` immediately after that store.
Also check C1 barriers for the same issue.

## ALL MODIFIED FILES AND LOCATIONS:
1. shenandoahBarrierSetAssembler_arm.cpp (cpu/arm/gc/shenandoah/)
   - shenandoah_write_barrier_pre: Fix#1 (AS_RAW), TODO: add DMB
   - store_at ~line 480: Fix#7 (resolve_forward_pointer on new_val)
   - cmpxchg_oop ~line 526: Fix#7b (resolve_forward_pointer on new_val)
   - load_reference_barrier: original cset check code

2. shenandoahBarrierSet.inline.hpp (share/gc/shenandoah/)
   - oop_store_common ~line 262: Fix#7c (resolve_forwarded + has_forwarded_objects)
   - oop_cmpxchg ~line 192: Fix#7c (resolve_forwarded + has_forwarded_objects)
   - oop_xchg ~line 213: Fix#7c (resolve_forwarded + has_forwarded_objects)

3. markWord.hpp: Fix#4 (has_monitor: (value()&3)==2 instead of (value()&2)!=0)
4. synchronizer.cpp: Fix#4 related
5. c1_LIRAssembler_arm.cpp: Fix#5 (store_at_resolved LRB), Fix#5a (use_fixed_result)
6. macroAssembler_arm.hpp/cpp: Fix#6A (safepoint_poll at_return overload)
7. templateInterpreterGenerator_arm.cpp: Fix#3 (atos_merged), Fix#6B (native watermark)
8. sharedRuntime_arm.cpp: Fix#2 (cmpxchg alias), Fix#6D (native watermark)

## BUILD/DEPLOY/TEST:
```bash
# Build (~90-150s incremental):
bash build-fast.sh 2>&1; echo "Exit: $?"

# Deploy:
scp frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/tmp/
ssh admin@10.59.40.2 "opkg install /tmp/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite"

# Quick verify boot:
ssh admin@10.59.40.2 "/usr/local/frc/JRE/bin/java -version 2>&1"

# Quick aggressive test:
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && /usr/local/frc/JRE/bin/java -XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -Xmx150m -Xms150m -XX:ShenandoahGCHeuristics=aggressive -Dtarget=500 TestAllocObjects 2>&1 | tail -5"

# Full suite (33 tests):
ssh admin@10.59.40.2 "killall -9 java 2>/dev/null; sleep 1; cd /home/lvuser/shenandoah_tests && bash run_shenandoah_tests.sh 2>&1"
```

## All Fixes Applied (in code currently)

### Fix #7: shenandoahBarrierSetAssembler_arm.cpp store_at (~line 480)
- Added resolve_forward_pointer on new_val before iu_barrier
- Guarded by ShenandoahLoadRefBarrier && HAS_FORWARDED gc_state check
- This protects interpreter putfield/putstatic/aastore

### Fix #7b: shenandoahBarrierSetAssembler_arm.cpp cmpxchg_oop (~line 526)
- Added resolve_forward_pointer on new_val before CAS loop
- Guarded by ShenandoahLoadRefBarrier && HAS_FORWARDED && cbz(null check)
- This protects interpreter and C1 CAS paths

### Fix #7c: shenandoahBarrierSet.inline.hpp shared C++ barriers
- Added resolve_forwarded(value) in oop_store_common before iu_barrier
- Added resolve_forwarded(new_value) in oop_cmpxchg before iu_barrier
- Added resolve_forwarded(new_value) in oop_xchg before iu_barrier
- All guarded by ShenandoahLoadRefBarrier && value != NULL
- This protects ALL C++ runtime oop stores (class loading, JNI, reflection, Unsafe)

## Current Test Results
- Build succeeds (~90-130s incremental)
- Deploy succeeds
- Aggressive mode STILL CRASHES:
  - ClassCastException: KeyValueHolder → ModuleReference (oop confusion from stale refs)
  - NULL klass at region 187 (corrupted heap)
  - ShenandoahVerify: "After Updating References, Object should be in active region" (ref to TR/trash region)

## Root Cause Analysis

The resolve_forwarded() approach works ONLY when the forwarding pointer is still intact in the from-space copy's mark word. The forwarding pointer is set during evacuation and stays until the region is recycled.

Timeline of the bug:
1. Object A at addr X is evacuated to Y, mark word at X = forwarded(Y)
2. Concurrent update_refs sweeps heap up to watermark, updating X→Y
3. Thread T's current frame has stale ref to X (stack watermark hasn't processed current frame)
4. Thread T stores X into a newly-allocated object via putfield
5. Fix #7 resolves: reads mark word at X → sees forwarded(Y) → stores Y ✓
6. Final Update Refs (STW): regions recycled
7. THEORETICALLY THIS SHOULD WORK

But crashes suggest either:
a) Some store path bypasses our fix (arraycopy? clone? JIT intrinsic?)
b) Timing issue where region recycling happens before the store
c) Multi-cycle issue where the stale ref survived from one cycle to the next

## NEXT STEPS TO INVESTIGATE

1. **ArrayCopy paths**: System.arraycopy might copy oop values without going through store barriers
   - Check: shenandoahBarrierSetAssembler_arm.cpp arraycopy_prologue/epilogue
   - Check: stubGenerator_arm.cpp arraycopy stubs
   - Check: shenandoahBarrierSet.inline.hpp clone_in_heap, obj_copy

2. **Clone paths**: Object.clone() may bypass store barriers
   - Check: shenandoahBarrierSet.inline.hpp clone_in_heap

3. **Verify our fixes are taking effect**: The shared C++ oop_store_common is a template function.
   Check that ALL template instantiations pick up the fix. The header file might be included
   in many compilation units - make sure the build recompiled ALL affected .o files.

4. **Current frame processing**: Maybe the fix should be in the stack watermark,
   not the store barrier. Force processing of current frame at safepoint polls
   (at bytecode dispatch, not just method return).

5. **The oop_store_in_heap debug assertion**: The existing assertion
   `shenandoah_assert_not_forwarded_except` only fires in debug builds.
   Consider adding actual resolve in its place.

6. **Consider if this is actually a LOAD barrier issue, not store**: If the load_at
   barrier on ARM32 has a bug that sometimes returns from-space refs (instead of to-space),
   that would explain local variables having stale refs AND the resolve_forwarded
   not helping (because the object was never evacuated - it was the wrong object).

## KEY FILES

- shenandoahBarrierSetAssembler_arm.cpp: store_at, cmpxchg_oop, load_at, load_reference_barrier, resolve_forward_pointer
- shenandoahBarrierSet.inline.hpp: oop_store_common, oop_cmpxchg, oop_xchg, clone_in_heap
- c1_LIRAssembler_arm.cpp: C1 store_at_resolved (Fix #5)
- shenandoahBarrierSetC1.cpp: C1 barrier set
- interp_masm_arm.cpp: remove_activation (watermark check), dispatch_base (safepoint poll)

## IMPORTANT INSIGHT

The ClassCastException (KeyValueHolder vs ModuleReference) means a memory address that
used to hold a ModuleReference was recycled, then a KeyValueHolder was allocated at the
same address. Some reference still points to the old address and sees the wrong object.
This is EXACTLY what happens when a stale from-space reference persists past region
recycling and reuse.

## Build/Deploy/Test Commands
```
bash build-fast.sh 2>&1; echo "Exit: $? Elapsed: ${SECONDS}s"
scp frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/tmp/
ssh admin@10.59.40.2 "opkg install /tmp/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite"
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && /usr/local/frc/JRE/bin/java -XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -Xmx48m -Xms48m -XX:ShenandoahGCHeuristics=aggressive TestRefprocSanity 2>&1"
```
