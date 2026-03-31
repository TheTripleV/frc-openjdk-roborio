# Aggressive Mode Debug State

## Rtemp Clobber Hypothesis: DISPROVED
Read load_reference_barrier at lines 242-360. The code computes R1=src_addr at lines
297-316 BEFORE the cset check (which uses R2/R3 as temps via mov_address/ldrb, NOT Rtemp).
The load_addr.base() register value is used immediately after push - push doesn't clear
registers, it only saves them. So Rtemp/R12 still has the original value when computing R1.
The cset check at lines 321-328 uses R2/R3 (already saved on stack), not Rtemp.
The load_reference_barrier implementation LOOKS CORRECT.

## NEXT HYPOTHESIS TO CHECK: Verifier false positive?
When the verifier reports "Must be marked in complete bitmap" for an object referenced
from an after-TAMS ConcurrentHashMap$Node, maybe the GC CORRECTLY doesn't mark that object
(because after-TAMS objects' fields aren't scanned during marking) but the VERIFIER
incorrectly expects it to be marked. Check ShenandoahVerifier code for this case.
Also, the ModuleReferenceImpl SHOULD be reachable via OTHER paths too (module system root).
If those other paths were broken during marking and SATB correctly recorded them,
the object should still be marked. If NOT, the SATB barrier DID miss a store.

## LATEST STATUS
- ShenandoahIUBarrier=true NOT compatible with SATB mode (tested, error)
- Both fixes applied: (1) AS_RAW SATB pre-barrier, (2) cmpxchg_oop tmp1 not Rtemp
- STILL CRASHES in both -Xint and C1 with aggressive mode
- Build: bash build-fast.sh 2>&1  
- Deploy: scp IPK to admin@10.59.40.2:/tmp/ then ssh admin opkg install
- Test: ssh lvuser@10.59.40.2 'cd /home/lvuser/tests && /usr/local/frc/JRE/bin/java -Xint -XX:+UseShenandoahGC -XX:+UnlockDiagnosticVMOptions -XX:ShenandoahGCHeuristics=aggressive -Xmx128m TestSmallHeap 2>&1'

## Bug Summary
ALL aggressive heuristic tests fail on ARM32. The ShenandoahVerifier catches:
```
Error: Before Mark, Reachable; Object start should be within the region
```
A ConcurrentHashMap$Node field references an object in a recycled (Empty Committed) region.
The stale reference was NOT updated during the previous cycle's update-refs phase.

## Test Results (Baseline)
- 22 passed (all adaptive/passive/static/compact tests)
- 11 failed (ALL aggressive tests)
- Exit 137 (SIGKILL): TestAllocObjects, TestRetainObjects, TestRefprocSanity, TestParallelRefprocSanity, TestStringInternCleanup, TestArrayCopyCheckCast, TestVerifyJCStress, TestSieveObjects
- Exit 134 (SIGABRT/crash): TestLotsOfCycles aggressive, TestAllocIntArrays aggressive
- Crashes in oopDesc::klass() at oop.inline.hpp:87, called from LinkInfo::LinkInfo at linkResolver.cpp:257
- Crashes happen in BOTH -Xint and C1 modes

## Root Cause Analysis

### ShenandoahVerify Output
With `-XX:+ShenandoahVerify -Xmx64m -XX:ShenandoahGCHeuristics=aggressive`:
- Reference at 0xafdc18f4 (in ConcurrentHashMap$Node field) points to 0xb3cc0a30
- Region 255 containing 0xb3cc0a30 is Empty Committed (recycled)
- This means update-refs phase did NOT update this reference before the region was recycled

### GC Cycle Flow (from shenandoahConcurrentGC.cpp)
1. op_init_mark (STW) - ShenandoahStackWatermark::change_epoch_id()
2. concurrent mark
3. op_final_mark (STW) - ShenandoahStackWatermark::change_epoch_id() (if cset non-empty)
4. concurrent evacuation
5. op_init_updaterefs (STW) - sets evacuation=false, weak_root=false, update_refs=true
6. concurrent updaterefs - walks entire heap, updates all refs
7. op_update_thread_roots - Handshake::execute with ShenandoahUpdateRefsClosure
8. op_final_updaterefs (STW) - verify_after_updaterefs, recycles regions, clear has_forwarded
9. entry_cleanup_complete - recycle_trash()

## ARM32 vs AArch64 Differences Found

### CRITICAL: Missing AS_RAW in SATB Pre-Barrier Load
File: shenandoahBarrierSetAssembler_arm.cpp line ~130
```cpp
// ARM32 (WRONG):
__ load_heap_oop(pre_val, Address(store_addr, 0));
// AArch64 (CORRECT):
__ load_heap_oop(pre_val, Address(obj, 0), noreg, noreg, AS_RAW);
```
Without AS_RAW, the load goes through the full ShenandoahBarrierSetAssembler::load_at(),
which applies a NESTED LRB inside the SATB barrier. The nested LRB pushes/pops
{R0-R3, R12, LR} which are the same registers the outer satb_write_barrier_pre uses.

### Missing Frame in LRB
AArch64 uses enter()/leave() for a proper frame in load_reference_barrier.
ARM32 only does push/pop without creating a frame.

### Missing VFP Register Save
AArch64 uses push_call_clobbered_registers() (saves ALL GP+FP regs).
ARM32 only saves {R0-R3, R12, LR} - missing VFP/float registers.

## Interpreter Oop Load Audit (All paths checked)
- getstatic/getfield: fast_version bypass FIXED with `!UseShenandoahGC` at line 2748
- aaload: uses do_oop_load → load_heap_oop → LRB ✓
- fast_agetfield: uses do_oop_load → LRB ✓
- fast_xaccess atos: uses do_oop_load → LRB ✓
- fast_aldc: uses load_resolved_reference_at_index → load_heap_oop → LRB ✓
- ldc (class): goes to InterpreterRuntime → LRB via resolve_oop_handle ✓
- prepare_invoke receiver: inline forwarding check (NOT full LRB, but receiver from stack)
- prepare_invoke appendix: load_resolved_reference_at_index → LRB ✓

## Fixes Applied

### Fix 1: AS_RAW for SATB pre-barrier load [APPLIED]
In shenandoahBarrierSetAssembler_arm.cpp line ~148-152, satb_write_barrier_pre function:
Changed `load_heap_oop(pre_val, Address(store_addr, 0))` to 
`load_heap_oop(pre_val, Address(store_addr, 0), noreg, noreg, noreg, AS_RAW)`.
This avoids the nested LRB and matches aarch64 behavior.

## After AS_RAW Fix - NEW Error
With AS_RAW fix applied, the verifier error changed:
- OLD: "Before Mark, Reachable; Object start should be within the region" (stale ref to recycled region)
- NEW: "Before Evacuation, Reachable; Must be marked in complete bitmap" (reachable but unmarked object)

New error details:
- ConcurrentHashMap$Node at 0xafe00450 (allocated AFTER mark start, marked strong+weak, NOT in cset)
- Its val field at 0xafe00460 references ModuleReferenceImpl at 0xb3ddeba0
- ModuleReferenceImpl is NOT marked (strong or weak), IS in collection set
- Forwardee: (the object itself) = not yet evacuated

Analysis: This is an SATB barrier failure. The ModuleReferenceImpl was reachable at mark start
but became unreachable from marked roots because some reference chain was broken during marking
WITHOUT the SATB pre-barrier recording the old value.

Key hypothesis: ConcurrentHashMap uses CAS (compareAndSwapObject via Unsafe) heavily.
The CAS operation replaces old reference chains with new ones. If the CAS SATB pre-barrier
doesn't properly record the old value being replaced, the marker loses the reference chain
and the object becomes unmarked.

The cmpxchg_oop in shenandoahBarrierSetAssembler_arm.cpp does NOT have an explicit SATB
pre-barrier for the old value! It only handles forwarding resolution for the CAS comparison.
The SATB barrier for the old value must come from the caller (C1's LIR_OpShenandoahCompareAndSwap
or the interpreter's runtime call).

## Key Investigation: CAS SATB Pre-barrier
In ConcurrentHashMap, when an entry is replaced via CAS:
1. Unsafe.compareAndSwapObject(table, offset, oldNode, newNode) 
2. The old value (oldNode, which references ModuleReferenceImpl) is being removed
3. The SATB pre-barrier MUST record oldNode to keep its reference tree alive during marking

Check where the CAS SATB barrier is applied:
- Interpreter: `Unsafe_CompareAndSwapObject` native method → runtime C++ code
- C1: LIR_OpShenandoahCompareAndSwap → ShenandoahBarrierSetC1::atomic_cmpxchg_in_heap
  The C1 barrier set should add SATB barriers around the CAS.
  
The SATB pre-barrier for CAS should load the OLD VALUE at the memory location BEFORE
attempting the CAS, and enqueue it. This is different from a simple store's pre-barrier.

## Fixes Still to Try

### Fix 2: CAS SATB Pre-barrier Investigation [HIGH PRIORITY]
Check if CAS operations properly fire the SATB pre-barrier.
- `shenandoahBarrierSetAssembler_arm.cpp` `cmpxchg_oop()` does NOT have an SATB pre-barrier
- It only handles forwarding resolution for CAS retry
- The SATB pre-barrier for CAS must come from the caller (C1 or runtime)
- C1: `shenandoahBarrierSetC1_arm.cpp` `LIR_OpShenandoahCompareAndSwap::emit_code()`
  Check if C1 emits the SATB pre-barrier BEFORE the CAS
- Runtime: `Unsafe_CompareAndSwapObject` goes to `ShenandoahBarrierSet::oop_store_in_heap_at()`
  or directly to the raw CAS. CHECK if runtime CAS has SATB barrier.
- Key files to check:
  - `jdk17u-local/src/hotspot/share/gc/shenandoah/shenandoahBarrierSet.hpp` (oop_store, cmpxchg methods)
  - `jdk17u-local/src/hotspot/share/gc/shenandoah/shenandoahBarrierSet.inline.hpp`
  - `jdk17u-local/src/hotspot/cpu/arm/gc/shenandoah/c1/shenandoahBarrierSetC1_arm.cpp`
  - `jdk17u-local/src/hotspot/share/gc/shenandoah/c1/shenandoahBarrierSetC1.cpp`

### Fix 3: getstatic - fast_version bypass for putfield_or_static
The getfield_or_static already has `!UseShenandoahGC` in fast_version check.
Check if putfield_or_static also needs this fix. Currently:
```cpp
bool fast_version = (is_static || !RewriteBytecodes) && !VerifyOops;
```
For putstatic: fast_version = true. The atos case is LAST in the table, so it
doesn't overflow (no FixedSizeCodeBlock wrapper for atos). BUT the add(PC,...) jump 
table calculation might be wrong if earlier blocks overflow due to barriers.
Actually, the atos case doesn't have FixedSizeCodeBlock and is at the END, so table overflow
is not an issue. The fast_version for putfield_or_static is OK.

### Fix 4: Check arraycopy SATB barrier
File: `shenandoahBarrierSetAssembler_arm.cpp` arraycopy_prologue/epilogue
Ensure array copies properly fire SATB barriers for overwritten references.

### Fix 5: Add runtime verification for stores
Add a debug-only check in store_at: verify that the new_val and old_val point to
valid regions (not empty/trash). This would catch the exact moment stale refs are stored.

## Build/Deploy Commands
```bash
# Build
bash build-fast.sh 2>&1; echo "Exit: $? Elapsed: ${SECONDS}s"
# Deploy  
scp frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/tmp/
ssh admin@10.59.40.2 'opkg install /tmp/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite'
# Test aggressive mode (note: UnlockDiagnosticVMOptions BEFORE other diagnostic flags)
ssh lvuser@10.59.40.2 'cd /home/lvuser/tests && /usr/local/frc/JRE/bin/java -XX:+UseShenandoahGC -XX:+UnlockDiagnosticVMOptions -XX:ShenandoahGCHeuristics=aggressive -XX:+ShenandoahVerify -Xmx64m TestSmallHeap'
# Test without verifier
ssh lvuser@10.59.40.2 'cd /home/lvuser/tests && /usr/local/frc/JRE/bin/java -XX:+UseShenandoahGC -XX:+UnlockDiagnosticVMOptions -XX:ShenandoahGCHeuristics=aggressive -Xmx128m TestSmallHeap 2>&1'
```

## Code Changes Applied So Far
1. AS_RAW in SATB pre-barrier load (shenandoahBarrierSetAssembler_arm.cpp line ~148-152)
   Changed `load_heap_oop(pre_val, Address(store_addr, 0))` to
   `load_heap_oop(pre_val, Address(store_addr, 0), noreg, noreg, noreg, AS_RAW)`
   This avoids nested LRB inside SATB barrier, matching aarch64.

2. cmpxchg_oop register aliasing fix (shenandoahBarrierSetAssembler_arm.cpp lines ~549-555)
   Changed `and_32(Rtemp, tmp3, markWord::lock_mask_in_place)` to
   `and_32(tmp1, tmp3, markWord::lock_mask_in_place)` and
   `cmp(Rtemp, markWord::marked_value)` to `cmp(tmp1, markWord::marked_value)`.
   C1 passes Rtemp(R12) as tmp3 via shenandoahBarrierSetC1_arm.cpp line 47.
   Old code: and_32(R12, R12, 0x3) destroyed mark word in tmp3(R12), making
   bic(R12, R12, 0x3) produce 0 instead of forwarding pointer. CAS forwarding
   retry always failed. Uses tmp1 which is free at that point.

Build2 succeeded 76s. Ready to deploy and test.

## Test Results After Both Fixes

### Test 1: Without verifier, -Xmx128m
```
Error occurred during initialization of boot layer
java.lang.ClassCastException: class java.lang.Class cannot be cast to class 
java.lang.module.ModuleReference (java.lang.Class and java.lang.module.ModuleReference 
are in module java.base of loader 'bootstrap')
sh: line 1:  7031 Killed
```
Still crashes. The GC is corrupting references - a Class object is found where a 
ModuleReference should be. This is type confusion caused by stale/swapped references.

### Analysis of ClassCastException
When Shenandoah evacuates objects, it copies them to new locations and installs forwarding 
pointers. If a reference is not updated (points to old location), and the old region is 
reused for new allocations, the old reference now points to whatever new object was allocated 
at that address. This causes ClassCastException when the new object's klass doesn't match.

The fact that both the AS_RAW and cmpxchg_oop fixes didn't resolve this suggests the root 
cause is elsewhere. Possibilities:
1. C1 compiler missing load/store barriers for some operations
2. Interpreter missing barriers for some bytecode (unlikely - audited extensively)
3. Runtime C++ code missing barriers for some path
4. Stack watermark barrier not processing all frames correctly
5. The IU (Incremental Update) barrier or some other concurrent marking barrier issue

### Next investigation targets
1. ~~Run with -Xint (interpreter only) to eliminate C1 as suspect~~ DONE - crashes in -Xint too!
   With -Xint: NullPointerException: Cannot invoke "ModuleDescriptor.name()" because "<local2>" is null
   With C1: ClassCastException: class java.lang.Class cannot be cast to ModuleReference
   CONCLUSION: Bug is in interpreter or shared GC code, NOT in C1
   
2. Run with ShenandoahVerify to get precise error location
3. Check C1 LIR barrier emission for ALL oop operations (not just CAS)
4. Check arraycopy barriers (System.arraycopy with reference arrays)
5. Check clone barriers
6. Consider adding runtime store verification: before storing an oop, check it 
   points to a valid non-empty region
   
## CRITICAL FINDING: Bug is in interpreter, not C1
Both -Xint and C1 modes crash with aggressive heuristics.
The interpreter's oop load paths were all audited and appear correct.
The shared GC code is architecturally independent.

Remaining suspects for interpreter:
- The `load_reference_barrier()` implementation might have a subtle register corruption
  when saving/restoring {R0-R3, R12, LR}. If the dst register overlaps with one 
  of the saved registers and the fixup code has a bug, the resolved value won't 
  reach the caller.
- The stack watermark barrier might not process all interpreter frame slots correctly
- The `resolve_oop_handle` or `load_resolved_reference_at_index` might have edge cases
- Concurrent oop_iterate for marking might have ARM32-specific field offset calculation issues

## TOP HYPOTHESIS: load_at + load_reference_barrier register aliasing

In shenandoahBarrierSetAssembler_arm.cpp `load_at()` function (lines ~370-434):

When `dst == src.base()`:
1. Code does `mov(Rtemp, src.base())` to save the base address
2. Then `BarrierSetAssembler::load_at(dst, Address(Rtemp))` - raw load, now dst is overwritten
3. Then `load_reference_barrier(dst, Address(Rtemp))` - but Rtemp(R12) is SAVED as part of
   {R0-R3, R12, LR} push at the start of load_reference_barrier!
4. Inside load_reference_barrier, when checking cset or calling runtime:
   - src_addr is reconstructed from Address(Rtemp) -> R1 = Rtemp
   - But Rtemp was pushed to stack, and any write to R12 inside the barrier would
     modify the value that later gets popped back
5. After the pop, Rtemp might have a different value than what was pushed

SPECIFIC BUG PATH:
- `load_at` with dst=R0, src=Address(R0, offset) (common for getfield: object in R0, load field into R0)
- `load_at` aliases: dst==src.base(), so `mov(Rtemp, R0)`, then `raw_load(R0, Address(Rtemp, offset))`
- `load_reference_barrier(R0, Address(Rtemp, offset))`:
  - push {R0, R1, R2, R3, R12, LR}  -- saves R0 (the loaded oop), R12 (=Rtemp=src base addr)
  - ldrb(R3, gc_state)  or similar check
  - If HAS_FORWARDED: checks if oop is in cset
  - For strong ref: loads cset table addr, checks cset
  - If in cset: R1 = src_addr (reconstructed from Address(Rtemp) = Rtemp + offset)
    But Rtemp on stack was saved, and R12/Rtemp might be modified by later code...
  - Call ShenandoahRuntime::load_reference_barrier_strong
  - Result in R0
  - Since dst=R0, writes R0 result to stack slot [sp+0] (first pushed reg)
  - pop {R0, R1, R2, R3, R12, LR} restores all regs including resolved R0

Wait - this actually looks correct. The push saves original values, the resolved result
is stored to R0's stack slot, and pop restores the resolved R0 + original other regs.

BUT: What about R12/Rtemp? After the pop, Rtemp is restored to its saved value (original
src.base()). But load_at doesn't use Rtemp after the LRB call... or does it?

After load_reference_barrier returns to load_at, the code checks need_keep_alive_barrier.
If keep_alive is needed, it calls satb_write_barrier_pre again, which pushes {R0-R3,R12,LR}
AGAIN. At this point, dst=R0 has the resolved value. This second push/pop should be fine.

So the aliasing path might actually be correct. Let me look for other issues...

## OTHER THEORIES TO CHECK

### Theory 2: The oop_iterate for concurrent marking on ARM32
The GC threads use `oop_oop_iterate()` to scan object fields during concurrent marking.
This uses OopMapBlock (oop maps in InstanceKlass) to find reference fields.
On ARM32 without CompressedOops, each oop is 4 bytes.
The oop_iterate code should handle uncompressed oops correctly.
BUT: Check if there's any 8-byte alignment assumption that breaks on ARM32.

### Theory 3: Missing SATB barrier in some shared code path
Some shared C++ code might store an oop without going through the barrier.
For example, ConcurrentHashMap's internal operations go through Unsafe methods.
The Unsafe.putObject (not putObjectVolatile) might bypass the barrier on some path.

### Theory 4: The SATB buffer is not being processed completely
The marker processes SATB buffers from each thread. If a buffer is lost or partially
processed, some old references won't be seen by the marker, causing objects to be
missed during marking. Check if thread buffer handoff is correct on ARM32.

### Theory 5: The gc_state check in the LRB has a timing issue
The gc_state byte is read without memory barriers in the fast path. On ARM32 with
weak memory ordering, a stale gc_state value could cause the LRB to skip processing
when it should be active. The `ldrb(R3, Address(Rthread, ShenandoahThreadLocalData::gc_state_offset()))`
reads gc_state without a memory barrier. If the GC thread sets gc_state to HAS_FORWARDED
but the mutator thread sees the old gc_state (without HAS_FORWARDED), the LRB won't
resolve forwarding, leaving stale references.

THIS IS POTENTIALLY THE KEY BUG! On ARM32, memory ordering is weaker than x86.
The gc_state update by the GC thread might not be visible to mutator threads
without a proper memory barrier.

CHECK: Is there a DMB barrier between the GC setting gc_state and mutator threads
reading it? On aarch64, the LoadLoad barrier after the gc_state read might be implicit.
On ARM32, it needs an explicit DMB.


## Files to Investigate Next
1. `shenandoahBarrierSet.inline.hpp` - shared SATB barrier for CAS in runtime
2. `shenandoahBarrierSetC1.cpp` (shared) - C1 barrier set for CAS
3. `shenandoahBarrierSetC1_arm.cpp` - ARM32 C1 CAS implementation
4. `c1_LIRGenerator_arm.cpp` - ARM32 C1 LIR generation for Unsafe CAS
