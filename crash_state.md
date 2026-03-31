# AGGRESSIVE CRASH ROOT CAUSE: SATB BARRIER BUG (Latest Finding)

## VERIFIED ROOT CAUSE (from ShenandoahVerify)
**"Before Evacuation, Reachable; Must be marked in complete bitmap"**
- Object: 0xaa800718 ModuleReferenceImpl - in collection set, NOT marked
- Referenced from: ConcurrentHashMap$Node at 0xaa840450 - allocated AFTER mark start
- The SATB write barrier FAILED to record the store of this reference
- This means the concurrent marking phase missed this reachable object
- When evacuation starts, it can't evacuate an unmarked object → crash

## What This Means
The SATB (Snapshot-At-The-Beginning) write barrier on ARM32 is not working correctly.
When the interpreter or C1 stores an oop into an object field, the SATB barrier should:
1. Load the OLD value from the field
2. If old value is not NULL and marking is in progress, enqueue old value to SATB buffer
This ensures all objects reachable at mark-start are found during marking.

If the barrier misses a store, the object referenced by the OLD value won't be marked.
When that object ends up in the collection set, it's unmarked and can't be evacuated.
Post-evacuation, accessing it crashes because from-space is trashed.

## Investigation Targets
1. **shenandoahBarrierSetAssembler_arm.cpp** - satb_write_barrier_pre()
   - Check if the SATB buffer enqueue is correct
   - Fix 1 added AS_RAW to prevent recursive barriers - may have broken SATB
2. **templateTable_arm.cpp** - putfield/putstatic templates
   - Check if SATB pre-barrier is called before stores
   - Check if interpreter correctly reads old value and enqueues it
3. **shenandoahBarrierSetC1_arm.cpp** - C1 SATB barrier (less likely since crash is at 0.5s bootstrap = all interpreted)
4. **Important**: The ConcurrentHashMap$Node was allocated AFTER mark start.
   Objects allocated after mark start are considered implicitly live for this cycle.
   BUT their field STORES still need SATB barriers for future references.
   When the Node's field is written with the ModuleReferenceImpl reference:
   - The OLD value (possibly null from initialization) should be enqueued
   - Actually wait: if old value is NULL, SATB doesn't enqueue
   - The issue might be different: the ModuleReferenceImpl was already alive at mark-start
     but became unreachable from the root set. The SATB should have caught the
     store that OVERWROTE the last reference to it.

## Current Fix Status
- Fix 1: AS_RAW in SATB barrier - APPLIED (may be buggy)
- Fix 2-5a: Other fixes - APPLIED
- Fix 6A/B/D: Stack watermark at_return - APPLIED
- Fix 6C: C1 return_op - REVERTED (caused SIGILL)

## Test Results: 22/33 pass
- All adaptive/static/compact/passive PASS (except 1 timeout)
- All aggressive FAIL (3 crash + 8 timeout)

## SATB Barrier Code Locations
In shenandoahBarrierSetAssembler_arm.cpp:
- Line 115: satb_write_barrier_pre called from store_at_resolved
- Line 119: satb_write_barrier_pre function definition
- Line 358: satb_write_barrier_pre called from oop_store_at_resolved
- Line 365: satb_write_barrier_pre called for interpreter pre-barrier
- Line 435: satb_write_barrier_pre called from cmpxchg_oop

KEY THEORY: ConcurrentHashMap uses CAS (compareAndSwapObject) not putfield.
The CAS path goes through cmpxchg_oop at line 435. Check if the SATB
pre-barrier in cmpxchg_oop correctly enqueues the old value.

Also check: store_at_resolved (line 115) for interpreter putfield/putstatic path.

## All Applied Fixes
1. AS_RAW in SATB (shenandoahBarrierSetAssembler_arm.cpp)
2. cmpxchg_oop register aliasing (same file)
3. atos_merged_with_itos (templateTable_arm.cpp)
4. markWord::has_monitor mask 3 not 2 (markWord.hpp)
5+5a. C1 store-value LRB with use_fixed_result (shenandoahBarrierSetC1.hpp/cpp)
6A. macroAssembler_arm safepoint_poll at_return overload
6B. templateInterpreterGenerator native return at_return=true
6C. REVERTED (c1_LIRAssembler return_op caused SIGILL)
6D. sharedRuntime native wrapper at_return=true

## Commands
```bash
bash build-fast.sh 2>&1
scp frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/tmp/
ssh admin@10.59.40.2 "opkg install /tmp/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite"
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && bash run_shenandoah_tests.sh 2>&1"
ssh admin@10.59.40.2 "cd /home/lvuser/shenandoah_tests && /usr/local/frc/JRE/bin/java -XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -Xmx150m -Xms150m -XX:ShenandoahGCHeuristics=aggressive -XX:+ShenandoahVerify -Xlog:gc=info TestRefprocSanity 2>&1"
docker exec shenandoah-builder arm-frc2024-linux-gnueabi-addr2line -e /jdk17u-local-build/build/linux-arm-client-release/jdk/lib/client/libjvm.so -f -C OFFSET
```
