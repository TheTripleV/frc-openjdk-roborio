# Crash Analysis

## C1 LRB Investigation State (Current Focus)

### Verified Facts:
1. ALL 10 interpreter (-Xint) tests PASS
2. Robot code with -Xint does NOT crash (confirmed by running with -Xint -Xmx150m for 35+ seconds)
3. Robot code with C1 (default mixed mode) CRASHES at ~4.8 seconds
4. ShenandoahVerify catches: "After Updating References, Reachable; Object should be in active region"
   - Parent: java.lang.Class {0xb3bc00b0} in region 315 (R), allocated after mark start, NOT after UWM
   - TAMS=0xb3bc0000, UWM=0xb3c00000 → object IS below UWM, should be scanned by update-refs
   - Field at interior location 0xb3bc0130 (offset 0x80=128 from object start)
   - Child: TalonFXConfiguration {0xaf48cbd0} in region 30 (TR=TRASH)
   - TalonFXConfiguration was in collection set, was evacuated, region is now TRASH
   - The reference at 0xb3bc0130 still points to the OLD from-space location (TRASH region)
   - libCTRE_PhoenixTools.so loaded at 4.790s, crash at 4.819s

### C1 Code Path Analysis (COMPLETED - all paths look correct):
- do_LoadField → access_load_at(IN_HEAP, ...) → ShenandoahBarrierSetC1::load_at_resolved
  - Checks is_oop() → is_reference_type(type) → true for T_OBJECT/T_ARRAY
  - Calls need_load_reference_barrier(decorators, type) → just checks ShenandoahLoadRefBarrier && is_reference_type
  - Applies LRB via load_reference_barrier_impl → ShenandoahLoadReferenceBarrierStub
- do_UnsafeGetObject → access_load_at(IN_HEAP|C1_UNSAFE_ACCESS|ON_UNKNOWN_OOP_REF, ...) → also applies LRB
- do_StoreField → access_store_at(IN_HEAP, ...) → ShenandoahBarrierSetC1::store_at_resolved
  - SATB pre_barrier on old value, iu_barrier on new value
- do_UnsafePutObject → access_store_at(IN_HEAP|C1_UNSAFE_ACCESS|ON_UNKNOWN_OOP_REF, ...) → also SATB
- putReferenceRelease → calls putReferenceVolatile → _putReferenceVolatile → append_unsafe_put_obj(T_OBJECT, true)

### Key C1 LRB Files:
- shenandoahBarrierSetC1.cpp: load_at_resolved applies LRB for all oop loads
- shenandoahBarrierSetAssembler_arm.cpp: gen_load_reference_barrier_stub (line ~566), generate_c1_load_reference_barrier_runtime_stub (line ~676)
- c1_LIRGenerator.cpp: do_LoadField (line 1822), do_UnsafeGetObject (line 2277)

### Hypothesis: The issue might be in the LRB STUB ITSELF on ARM32
- gen_load_reference_barrier_stub: uses R0 as result (asserted res == R0)
- generate_c1_load_reference_barrier_runtime_stub: saves/restores R0, calls into runtime
- The runtime stub saves R4-R11 + FP regs, sets up R0/R1 args, calls C code
- POSSIBLE BUG: Register clobbering? Something specific to ARM32 calling convention?

### Hypothesis: Objects allocated during GC didn't have their stored refs updated
- java.lang.Class at 0xb3bc00b0 is allocated after mark start, before UWM
- update_with_forwarded should process it (Step 2 of marked_object_iterate scans above-TAMS to UWM)
- BUT: the stale reference may have been written AFTER update-refs already processed this region
- This is the "concurrent mutation during update-refs" scenario
- The SATB pre-barrier should protect against this in normal Shenandoah: the LRB on load ensures
  mutator always gets to-space copy, and the copy stored into the field is the to-space copy.
- IF the LRB is not applied (returns from-space copy), then the from-space pointer gets stored,
  and update-refs won't fix it (it only checks in_collection_set, and from-space IS in cset initially
  but then becomes TRASH after final-update-refs)

### CRITICAL: Key source files to examine:
- shenandoahBarrierSetAssembler_arm.cpp: Lines 566-775 contain C1 LRB stub code
  - gen_load_reference_barrier_stub (line ~566): creates stub for C1 LRB
  - generate_c1_load_reference_barrier_runtime_stub (line ~676): runtime slow path
  - load_reference_barrier (line ~280): interpreter LRB
  - load_at (line ~230): interpreter load_at with per-register LRB
- c1_LIRAssembler_arm.cpp: Lines 496+ contain mem2reg (load) with patching support
- shenandoahBarrierSetC1.cpp: load_at_resolved (line ~206), load_reference_barrier_impl (line ~133)
- shenandoahHeap.inline.hpp: update_with_forwarded (line ~99), conc_update_with_forwarded (line ~118)
- shenandoahHeap.inline.hpp: marked_object_iterate (line ~440) - Step 2 walks TAMS to limit
- shenandoahConcurrentMark.cpp: Above-TAMS scan for ARM32 (!ShenandoahStackWatermarkBarrier) at line ~165

### CRITICAL: 4 fixes already deployed:
1. AS_RAW in satb_write_barrier_pre (shenandoahBarrierSetAssembler_arm.cpp ~line 148)
2. cmpxchg_oop register aliasing (shenandoahBarrierSetAssembler_arm.cpp ~line 549)
3. atos_merged_with_itos in getfield_or_static (templateTable_arm.cpp ~line 2916)
4. markWord::has_monitor() bug fix (markWord.hpp:276) - was (val&2)!=0 matching both monitor(10) and forwarding(11), fixed to (val&3)==2

### Investigation COMPLETED - all C1 paths verified correct:
- gen_load_reference_barrier_stub: Verified correct. R0 result, in-cset fast check, runtime stub call
- generate_c1_load_reference_barrier_runtime_stub: Verified correct. Saves R0-R3/R12/LR, loads params,
  calls runtime, stores result at param offset, pops all, loads result from SP+0, returns
- arraycopy_prologue: Verified correct. Calls ShenandoahRuntime::arraycopy_barrier_oop_entry
- nmethod_entry_barrier: Verified correct. Guard check, calls stub, deopt path
- method_entry_barrier stub: Verified correct. Saves FP/LR/R0-R3, calls nmethod_stub_entry_barrier
- load_reference_barrier_impl (LIR): Verified correct. Checks HAS_FORWARDED flag, creates stub
- store_at_resolved: SATB pre_barrier + iu_barrier (only when ShenandoahIUBarrier=true) + store
- do_LoadField/do_StoreField: Both go through access_load_at/access_store_at with IN_HEAP
- do_UnsafeGetObject: Uses IN_HEAP | C1_UNSAFE_ACCESS | ON_UNKNOWN_OOP_REF - LRB applied
- need_load_reference_barrier: Only disabled by IN_NATIVE decorator, not by ON_UNKNOWN_OOP_REF
- iu_barrier: Only applies SATB logging, does NOT call load_reference_barrier!
- ShenandoahIUBarrier is false in normal SATB mode

### KEY INSIGHT: THE ACTUAL FIX NEEDED
The C1 barriers all look correct for getfield/getstatic/arraycopy paths.
The stale reference must come from a path that doesn't go through normal field loads.

POSSIBLE ROOT CAUSES:
1. OOP MAP ISSUE: C1 compiled frame has incorrect oop map, so during final-update-refs safepoint,
   a register holding a from-space oop is not updated. Thread resumes with stale oop and stores it.
2. NMETHOD EMBEDDED OOP: A patched oop in compiled code is not updated by nmethod barrier.
3. VM RUNTIME OOP: A VM runtime function (class loading, resolution) returns a stale oop.
4. JNI RETURN: A JNI function returns a stale oop that gets stored without LRB.

MOST LIKELY: #1 (oop map issue) because:
- Only happens in C1 mode (interpreter doesn't use oop maps)
- Happens with complex robot code (many compiled methods, many safepoints)
- Pattern: stale from-space oop stored into newly-allocated java.lang.Class

### PRACTICAL NEXT STEP: Add store-value verification
Add a runtime check in ShenandoahBarrierSet's store barrier that verifies the stored oop
is NOT in the collection set. This will crash immediately when the stale oop is stored,
giving us the exact call stack (C1 method, PC, registers).

PLAN A: Add IU-like LRB on stored values in C1
In shenandoahBarrierSetC1.cpp store_at_resolved, add load_reference_barrier on the new value:
```cpp
void ShenandoahBarrierSetC1::store_at_resolved(LIRAccess& access, LIR_Opr value) {
  if (access.is_oop()) {
    if (ShenandoahSATBBarrier) {
      pre_barrier(...);
    }
    // FIX: Apply LRB to stored value to ensure it's to-space
    if (ShenandoahLoadRefBarrier) {
      LIRGenerator* gen = access.gen();
      value = ensure_in_register(gen, value, T_OBJECT);
      value = load_reference_barrier(gen, value, LIR_OprFact::addressConst(0),
                                     IN_HEAP | ON_STRONG_OOP_REF);
    }
    value = iu_barrier(access.gen(), value, access.access_emit_info(), access.decorators());
  }
  BarrierSetC1::store_at_resolved(access, value);
}
```
This ensures every C1 oop store resolves the value through LRB first.
File: jdk17u-local/src/hotspot/share/gc/shenandoah/c1/shenandoahBarrierSetC1.cpp

PLAN B: Add runtime check in oop_store_in_heap
In shenandoahBarrierSet.inline.hpp oop_store_in_heap_at, add check:
```cpp
if (ShenandoahVerify && !CompressedOops::is_null(value)) {
  oop v = CompressedOops::decode(value);
  if (ShenandoahHeap::heap()->in_collection_set(v)) {
    tty->print_cr("CSET STORE BUG: %p -> %p + %d value=%p", p2i(base), p2i(base), offset, p2i(v));
    ShenandoahAsserts::print_obj(tty, v);
    // This will give us a stack trace when the crash happens
    guarantee(false, "Storing cset oop");
  }
}
```
File: jdk17u-local/src/hotspot/share/gc/shenandoah/shenandoahBarrierSet.inline.hpp

PLAN C: Force ShenandoahIUBarrier=true as experiment
In the ShenandoahSATBMode::initialize_flags(), remove the CHECK_FLAG_UNSET for ShenandoahIUBarrier
and force it to true. Then run robot code. If it works, the fix is confirmed.

We should start with PLAN A (add LRB to stored values in C1) as it's the most targeted fix.

### EXACT CODE CHANGE FOR PLAN A:
File: jdk17u-local/src/hotspot/share/gc/shenandoah/c1/shenandoahBarrierSetC1.cpp
Function: ShenandoahBarrierSetC1::store_at_resolved (line ~189)

CURRENT CODE:
```cpp
void ShenandoahBarrierSetC1::store_at_resolved(LIRAccess& access, LIR_Opr value) {
  if (access.is_oop()) {
    if (ShenandoahSATBBarrier) {
      pre_barrier(access.gen(), access.access_emit_info(), access.decorators(), access.resolved_addr(), LIR_OprFact::illegalOpr /* pre_val */);
    }
    value = iu_barrier(access.gen(), value, access.access_emit_info(), access.decorators());
  }
  BarrierSetC1::store_at_resolved(access, value);
}
```

NEW CODE (add LRB on stored value):
```cpp
void ShenandoahBarrierSetC1::store_at_resolved(LIRAccess& access, LIR_Opr value) {
  if (access.is_oop()) {
    if (ShenandoahSATBBarrier) {
      pre_barrier(access.gen(), access.access_emit_info(), access.decorators(), access.resolved_addr(), LIR_OprFact::illegalOpr /* pre_val */);
    }
    if (ShenandoahLoadRefBarrier) {
      LIRGenerator* gen = access.gen();
      value = ensure_in_register(gen, value, T_OBJECT);
      value = load_reference_barrier(gen, value, LIR_OprFact::addressConst(0), access.decorators());
    }
    value = iu_barrier(access.gen(), value, access.access_emit_info(), access.decorators());
  }
  BarrierSetC1::store_at_resolved(access, value);
}
```
The key addition is: before storing an oop, pass it through load_reference_barrier to resolve any
from-space pointer to its to-space copy. The addr parameter is 0 (NULL) since this is not a load
from memory but rather a value being stored. The decorators from the access are passed through.

NOTE: load_reference_barrier uses ON_STRONG_OOP_REF | IN_HEAP by default for normal field stores.
The access.decorators() already contains IN_HEAP for normal field stores.
We need to ensure IN_NATIVE is NOT set (it disables LRB in need_load_reference_barrier).
The decorators should be fine for putfield/putstatic which use IN_HEAP.
Then test:
1. bash build-fast.sh 2>&1 (build)
2. scp IPK to admin@10.59.40.2:/tmp/
3. opkg install on rio
4. Run robot code with C1 enabled (no -Xint)
5. Also run all shenandoah tests

KEY FILE PATHS:
- shenandoahBarrierSetC1.cpp: c:\Users\vasis\Desktop\frc-openjdk-roborio-shenandoah\jdk17u-local\src\hotspot\share\gc\shenandoah\c1\shenandoahBarrierSetC1.cpp
- shenandoahBarrierSet.inline.hpp: c:\Users\vasis\Desktop\frc-openjdk-roborio-shenandoah\jdk17u-local\src\hotspot\share\gc\shenandoah\shenandoahBarrierSet.inline.hpp
- shenandoahBarrierSetAssembler_arm.cpp: c:\Users\vasis\Desktop\frc-openjdk-roborio-shenandoah\jdk17u-local\src\hotspot\cpu\arm\gc\shenandoah\shenandoahBarrierSetAssembler_arm.cpp
- templateTable_arm.cpp: c:\Users\vasis\Desktop\frc-openjdk-roborio-shenandoah\jdk17u-local\src\hotspot\cpu\arm\templateTable_arm.cpp

### REMINDER: 4 fixes already deployed:
1. AS_RAW in satb_write_barrier_pre (shenandoahBarrierSetAssembler_arm.cpp)
2. cmpxchg_oop register aliasing (shenandoahBarrierSetAssembler_arm.cpp)  
3. atos_merged_with_itos in getfield_or_static (templateTable_arm.cpp)
4. markWord::has_monitor() bug fix (markWord.hpp)

### BUILD/DEPLOY/TEST:
- Build: bash build-fast.sh 2>&1 (Docker container shenandoah-builder, ~82s incremental)
- Reset container: docker rm -f shenandoah-builder
- Deploy: scp frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/tmp/
- Install: ssh admin@10.59.40.2 'opkg install /tmp/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite'
- Test: ssh lvuser@10.59.40.2, cd /home/lvuser/tests, java -XX:+UseShenandoahGC ...
- Robot: ssh lvuser@10.59.40.2, cd ~, ./robotCommand

### CURRENT STATUS (save before context clear):
- Fix #5 DEPLOYED: Added LRB to C1 store_at_resolved in shenandoahBarrierSetC1.cpp
  - File: jdk17u-local/src/hotspot/share/gc/shenandoah/c1/shenandoahBarrierSetC1.cpp
  - In store_at_resolved(), after SATB pre_barrier and before iu_barrier, added:
    if (ShenandoahLoadRefBarrier) {
      value = ensure_in_register(gen, value, T_OBJECT);
      value = load_reference_barrier(gen, value, LIR_OprFact::addressConst(0), access.decorators());
    }
  - This ensures any oop stored via C1 compiled code goes through LRB first
  - Build succeeded (98s), IPK deployed and installed on RoboRIO
- All 5 fixes deployed:
  1. AS_RAW in satb_write_barrier_pre (shenandoahBarrierSetAssembler_arm.cpp)
  2. cmpxchg_oop register aliasing (shenandoahBarrierSetAssembler_arm.cpp)
  3. atos_merged_with_itos in getfield_or_static (templateTable_arm.cpp)
  4. markWord::has_monitor() bug fix (markWord.hpp)
  5. C1 store-value LRB in store_at_resolved (shenandoahBarrierSetC1.cpp) [NEW]
- NEED TO TEST: robot code with C1 (no -Xint), then all shenandoah tests
- Robot test: ssh lvuser@10.59.40.2, cd ~, ./robotCommand (run 60+ seconds)
- Test command: ssh lvuser@10.59.40.2 'cd /home/lvuser/tests && /usr/local/frc/JRE/bin/java -XX:+UseShenandoahGC -Xmx128m <TestClass>'
- Test classes available: ShenandoahBasic, ShenandoahAllocStress, ShenandoahThreadStress, etc.
- Compile tests: "C:\Program Files\Android\Android Studio\jbr\bin\javac" --release 17
- Previous test results: ALL 10 interpreter tests PASS, robot crashes only with C1 at ~4.8s

### FIX #5 TEST RESULTS:
- Robot code with C1 (bare java, no library path): NO JVM CRASH (missing wpiutiljni - expected)
- Robot code via ./robotCommand with C1: NEW CRASH (different from before!)
  - OLD crash: GC thread, conc_update_with_forwarded, markWord::to_pointer(), ~4.8s
  - NEW crash: C1 compiled method org.json.simple.parser.Yylex.yylex() at pc=0xb4081acc
    - Frame: J 249 c1 org.json.simple.parser.Yylex.yylex() (903 bytes) @ 0xb4081acc
    - Offset within nmethod: 0x0000112c from 0xb40809a0
    - SIGSEGV
    - Crash log: /home/lvuser/hs_err_pid16040.log
  - IMPORTANT: Robot ran further than before (showed "Robot program starting" and "NT: Listening...")
    Previously crashed at 4.8s during library loading, now survived past that!
  - The store-value LRB fix works for the original java.lang.Class/TalonFXConfiguration issue
  - But there's STILL a C1 code generation issue causing SIGSEGV in compiled methods

### NEW CRASH #6 DETAILS (hs_err_pid16040.log):
- Thread: main thread, _thread_in_Java
- PC: 0xb4081acc in nmethod @ 0xb40809a0, offset 0x112c
- Method: org.json.simple.parser.Yylex.yylex() (903 bytes) compiled by C1
- siginfo: SIGSEGV SEGV_MAPERR si_addr: 0x00000038 (NULL + 56 = NULL deref!)
- Elapsed: 3.274557 seconds
- Registers:
  R0=NULL, R1=NULL, R2=NULL, R3=0xa7c4c798 (Yylex object), R4=NULL
  R5=1, R6=0x354, R7=0xa7c4c798 (Yylex object), R8=0x2d
  R9=0xa548a658, R10=0xb638d358 (thread), FP=0xb64bf994
  R12=0, SP=0xb64bf848, LR=1, PC=0xb4081acc
- Stack trace:
  Yylex.yylex() [C1 compiled]
  JSONParser.nextToken() [interpreted]
  JSONParser.parse(Reader, ContainerFactory) [interpreted]
  JSONParser.parse(String, ContainerFactory) [interpreted]
  JSONParser.parse(String) [interpreted]
  PathPlannerPath.fromPathFile(String) [interpreted]
  Robot.<init>() [interpreted]
  ...
- Analysis: R0=NULL is loaded and used as a base with offset 0x38
  R0 was supposed to hold an oop but is NULL, meaning the C1 compiled code
  loaded NULL from somewhere (possibly the load_reference_barrier returned NULL?)
  The yylex() method processes char[] buffer and StringReader fields
  R3 and R7 both hold the Yylex object (this pointer)
  zzBuffer field is at offset 52 = 0x34 in Yylex, and zzBuffer 
  SI_ADDR = 0x38 = offset inside an oop that was expected to be non-null but was NULL
  If R0 held the zzBuffer (char[]), then 0x38 would be offset 56 from NULL
  But actually the zzBuffer offset in the Yylex object is @52 (0x34).
  0x38 from NULL suggests accessing length of an array: array header is 12 bytes on 32-bit (mark+klass+length)
  Actually array length is at offset 8 on 32-bit (mark=4, klass=4, length@8)
  Wait: 0x38 = 56 bytes. This could be accessing a field at offset 56 of another object.
  
  ALTERNATE: R0=NULL, accessing offset 0x38. This could be:
  - zzReader (StringReader) at @48 in Yylex, then accessing field at offset 8 of StringReader = NULL check
  - Or the sb (StringBuffer) at @56 in Yylex → NULL
  - Or the zzBuffer char[] at @52 → and then accessing the array data

  KEY QUESTION: Is this the LRB returning NULL for a non-null oop?
  Our store_at_resolved LRB calls load_reference_barrier(gen, value, addressConst(0), decorators)
  When addr=0 (NULL), and value is the oop to resolve...
  In load_reference_barrier_impl, it does:
    if value == NULL → skip LRB (cmp + branch)
  Wait, what if the LRB incorrectly handles a NULL value?
  Actually load_reference_barrier_impl generates:
    __ cmp(obj, 0); __ branch(lir_cond_equal, continuation); // skip NULL
  This should correctly skip NULL values.

  But what if the LRB MISTAKENLY resolves a valid oop to NULL?
  If the in_cset_fast_test check finds the oop IS in cset, it calls the runtime stub.
  The runtime reads the mark word for forwarding. If the mark word is NOT forwarded,
  the runtime returns the original oop. If it IS forwarded, it returns the forwardee.
  
  POSSIBILITY: The LRB stub itself has a register clobbering issue!
  Looking at gen_load_reference_barrier_stub:
  - It stores res (R0) and addr (which is a register holding 0) to the stack
  - Calls runtime stub
  - Runtime stub SAVES R0-R3, R12, LR then calls C function
  - C function returns result in R0
  - Runtime stub stores R0 to the parameter area, pops saved regs, loads result from SP+0, returns
  
  WAIT: after the call returns, the runtime stub does:
  1. str R0, [SP, param_offset]  // save result (from C function)
  2. pop {R0-R3, R12?, LR}       // restore saved regs → R0 gets OLD value
  3. ldr R0, [SP+0]               // load result from param area
  
  After step 2, SP has been adjusted upward. The param_offset depends on how many
  registers were pushed. If the calculation is wrong, ldr R0, [SP+0] loads garbage!
  
  Let me count: RegisterSet(R0, R3) = R0,R1,R2,R3 (4 regs) + RegisterSet(R12) (1 reg) + RegisterSet(LR) (1 reg)
  = 6 registers? Plus R9 if scratched.
  6 * 4 = 24 bytes. 7 * 4 = 28 if R9 included.
  param_offset should be 24 or 28.
  After pop, SP goes up by param_offset.
  The stored result at old SP + param_offset = new SP + 0. Correct.
  
  BUT: if the push/pop doesn't include ALL the registers, or if the order is wrong...
  On ARM32, push/pop order is by register number, not by the order specified.
  RegisterSet(R0, R3) | RegisterSet(R12) | RegisterSet(LR) = R0,R1,R2,R3,R12,LR
  Push: R0,R1,R2,R3,R12,LR stored at decreasing SP: SP-4=LR, SP-8=R12, SP-12=R3,...
  Pop: R0,R1,R2,R3,R12,LR restored from SP: SP=R0, SP+4=R1, SP+8=R2, SP+12=R3, SP+16=R12, SP+20=LR
  After pop: SP += 24 (6 regs * 4 bytes)
  
  If R9 is scratched: 7 regs, SP += 28
  param_offset = push_size = 24 (or 28 with R9)
  
  Result stored at old_SP + param_offset = old_SP + 24
  After pop: new_SP = old_SP + 24
  ldr R0, [new_SP + 0] = ldr R0, [old_SP + 24] ← this IS where we stored the result
  THIS IS CORRECT as long as param_offset matches push size.
  
  Actually, I should VERIFY this by reading the code. Let me check what `param_offset` is.

### ALTERNATE HYPOTHESIS: The store-value LRB itself is causing this crash!
If the LRB on the stored value is clobbering a register that the C1 code expects to be preserved,
then the C1 method would see unexpected values (like NULL) after the LRB returns.

The LRB generates a ShenandoahLoadReferenceBarrierStub. The stub is at the end of the method.
It saves R0 (result) and addr, calls the runtime, and returns with R0 = resolved oop.

But WHAT REGISTERS does the LRB stub CLOBBER beyond R0?
Looking at gen_load_reference_barrier_stub:
- Uses tmp1 and tmp2 temporaries (for cset check)
- The call to runtime clobbers R0-R3, R12, LR
- But the stub is a deferred code path, so C1's register allocator should know about clobbers

QUESTION: Does the ShenandoahLoadReferenceBarrierStub correctly declare its temp registers?
In ShenandoahBarrierSetC1::load_reference_barrier_impl:
  LIR_Opr tmp1 = gen->new_register(T_INT);
  LIR_Opr tmp2 = gen->new_register(T_INT);
  new ShenandoahLoadReferenceBarrierStub(obj, addr, res, tmp1, tmp2, decorators, is_native)

The stub uses tmp1 and tmp2. These should be allocated by the register allocator as non-conflicting.
But what about the call? The call clobbers R0-R3, R12, LR. The C1 register allocator needs to know
that these registers are clobbered.

THE ISSUE: When we add the LRB to store_at_resolved, the store operation ALSO uses registers.
The LRB might clobber registers that the subsequent store expects to be intact.
Specifically, if the LRB clobbers the base register (where the value is being stored TO),
then the store after the LRB would use a wrong address!

For example: putstatic TalonFXConfiguration.field = value
1. Load value into R4
2. Load mirror (java.lang.Class) into R5
3. **NEW: LRB on R4 → calls runtime → clobbers R0-R3, R12** (but R4,R5 are callee-saved, preserved)
4. Store R4 to [R5 + offset]

Since the runtime stub saves/restores R4-R11 (via push/pop), R5 should be preserved. So this should work.

BUT: if the LRB puts the result in R0, and the caller expects it in R4, there's a mismatch!
Looking at load_reference_barrier_impl:
  result = gen->result_register_for(obj->value_type()) → result_register_for T_OBJECT → R0
  __ move(obj, result)  // move value to R0
No wait, it's:
  res = new_register() (anything the allocator chooses)
  ... checks: is obj already in res? ...
  __ move(obj, res)
  
The result register `res` is a NEW virtual register allocated by C1. The register allocator assigns
physical registers. The LRB stub uses `stub->result()->as_register()` which is whatever physical
register was assigned to `res`.

In the stub: assert(res == R0, "result must arrive in R0")
So the register allocator MUST assign R0 to `res`. If it doesn't, assertion fails.

When we call load_reference_barrier from store_at_resolved, the value to be stored might be in
ANY register. The LRB will:
1. Move value from its register to R0
2. Check GC state (inline)
3. If forwarded: jump to stub → check cset → call runtime → result in R0
4. Continue: R0 has the resolved value

The value returned from load_reference_barrier is in the virtual register `res` which maps to R0.
The subsequent store operation uses this R0 value.

But the ORIGINAL store code expected the value in some other register (say R4). After our LRB,
the value is now in R0. The subsequent `BarrierSetC1::store_at_resolved` will use the new LIR_Opr
(which is res=R0). This should work because we replaced `value` with the result:
  value = load_reference_barrier(...);

So `value` is now the LIR_Opr pointing to R0-based register. The store will use this new value.
This should be correct.

BUT WAIT: the inline LRB code (before the deferred stub) looks like:
  load gc_state -> flag
  AND flag with HAS_FORWARDED
  CMP flag, 0
  BNE slow_path_stub
  // fast path: value already in result register (R0)
  
The fast path falls through. At this point, R0 should have the original value (moved there by __ move(obj, res)).
But the slow path stub saves R0, calls runtime, restores R0 from the param area.

What if the FAST PATH (no forwarding) returns correctly (R0 = value), but the __ move(obj, res)
instruction clobbers something that the subsequent code needs?

Hmm, let me think about this differently. Maybe the issue is NOT the LRB itself, but the
EXISTING C1 code gen for ARM32 has a bug that's exposed by the more complex robot code.

The Yylex.yylex() method is 903 bytes of bytecode - it's a very long method with many branches.
The C1 compiler on ARM32 might have a code gen bug for such complex methods.

Also: LR = 1 in the register dump is suspicious! LR should be a valid code address.
LR = 0x00000001 suggests stack corruption or a wrong return address.
  
- NEXT STEPS:
  1. Read /home/lvuser/hs_err_pid16040.log to understand the new crash
     ssh admin@10.59.40.2 "cat /home/lvuser/hs_err_pid16040.log | head -100"
     ssh admin@10.59.40.2 "grep -A5 'siginfo\|Register to memory\|Instructions\|Stack\|Internal Error' /home/lvuser/hs_err_pid16040.log | head -80"
  2. The crash is in C1 compiled Yylex.yylex() - this is a JSON parser method
     Could be another oop map issue, register corruption, or a different C1 code gen bug
  3. Check if this crash happens with -XX:TieredStopAtLevel=0 (interpreter only) - SHOULD NOT
  4. Check if ShenandoahVerify catches anything before this crash

### ALL 5 FIXES CURRENTLY DEPLOYED:
1. AS_RAW in satb_write_barrier_pre (shenandoahBarrierSetAssembler_arm.cpp)
2. cmpxchg_oop register aliasing (shenandoahBarrierSetAssembler_arm.cpp)
3. atos_merged_with_itos in getfield_or_static (templateTable_arm.cpp)
4. markWord::has_monitor() bug fix (markWord.hpp) - was (val&2)!=0, fixed to (val&3)==2
5. C1 store-value LRB in store_at_resolved (shenandoahBarrierSetC1.cpp) [NEW]
   - File: jdk17u-local/src/hotspot/share/gc/shenandoah/c1/shenandoahBarrierSetC1.cpp
   - In store_at_resolved(), after SATB pre_barrier and before iu_barrier:
     if (ShenandoahLoadRefBarrier) {
       LIRGenerator* gen = access.gen();
       value = ensure_in_register(gen, value, T_OBJECT);
       value = load_reference_barrier(gen, value, LIR_OprFact::addressConst(0), access.decorators());
     }

### BUILD/DEPLOY/TEST COMMANDS:
- Build: cd c:\Users\vasis\Desktop\frc-openjdk-roborio-shenandoah; bash build-fast.sh 2>&1
- Deploy: scp frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/tmp/
- Install: ssh admin@10.59.40.2 "opkg install /tmp/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite"
- Reset container: docker rm -f shenandoah-builder
- Tests folder: /home/lvuser/tests/ (test class files deployed there)
- Compile tests: "C:\Program Files\Android\Android Studio\jbr\bin\javac" --release 17
- RoboRIO: admin@10.59.40.2 (no password), lvuser@10.59.40.2 (no password)
- Robot: ssh lvuser@10.59.40.2 "cd ~ ; ./robotCommand"
- libjvm.so load addr: check hs_err log for "Dynamic libraries:" section

### ALL 5 FIXES CURRENTLY DEPLOYED:
1. AS_RAW in satb_write_barrier_pre (shenandoahBarrierSetAssembler_arm.cpp)
2. cmpxchg_oop register aliasing (shenandoahBarrierSetAssembler_arm.cpp)
3. atos_merged_with_itos in getfield_or_static (templateTable_arm.cpp)
4. markWord::has_monitor() bug fix (markWord.hpp) - was (val&2)!=0, fixed to (val&3)==2
5. C1 store-value LRB in store_at_resolved (shenandoahBarrierSetC1.cpp) [NEW]
   - File: jdk17u-local/src/hotspot/share/gc/shenandoah/c1/shenandoahBarrierSetC1.cpp
   - In store_at_resolved(), after SATB pre_barrier and before iu_barrier:
     if (ShenandoahLoadRefBarrier) {
       LIRGenerator* gen = access.gen();
       value = ensure_in_register(gen, value, T_OBJECT);
       value = load_reference_barrier(gen, value, LIR_OprFact::addressConst(0), access.decorators());
     }
   - This ensures any oop stored via C1 compiled code is resolved through LRB first

### BUILD/DEPLOY/TEST COMMANDS:
- Build: cd c:\Users\vasis\Desktop\frc-openjdk-roborio-shenandoah; bash build-fast.sh 2>&1
- Deploy: scp frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/tmp/
- Install: ssh admin@10.59.40.2 "opkg install /tmp/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite"
- Reset container: docker rm -f shenandoah-builder
- Tests folder: /home/lvuser/tests/ (test class files deployed there)
- Compile tests: "C:\Program Files\Android\Android Studio\jbr\bin\javac" --release 17
- RoboRIO: admin@10.59.40.2 (no password), lvuser@10.59.40.2 (no password)

### Update-refs scan algorithm:
- ShenandoahHeap::marked_object_iterate() has two steps:
  - Step 1: Bitmap-based scan below TAMS
  - Step 2: Linear walk from TAMS to limit (update_watermark) - processes ALL objects
  - Both call obj->oop_iterate(&cl) which calls update_with_forwarded on each oop field
- conc_update_with_forwarded: loads raw, checks in_collection_set, CAS-updates with forwardee
- update_with_forwarded: loads raw, checks in_collection_set, unconditionally stores forwardee

### Build/Deploy/Test Commands:
- Build: `bash build-fast.sh 2>&1` (~82s)
- Deploy: scp IPK to admin@10.59.40.2:/tmp/ then opkg install --force-reinstall --force-overwrite
- Test: ssh lvuser@10.59.40.2, cd /home/lvuser/tests, run java -XX:+UseShenandoahGC ...
- Robot: ssh lvuser@10.59.40.2, cd ~, ./robotCommand
- Crash logs: /home/lvuser/hs_err_pid*.log: SIGBUS in StubRoutines::atomic_cmpxchg

## Status: PRE-EXISTING BUG (also exists in crash logs from Mar 27-28, before our changes)

## Crash Pattern
- **Signal**: SIGBUS (0x7), si_code=BUS_ADRALN (alignment fault)  
- **Location**: `StubRoutines::atomic_cmpxchg` (ldrex on unaligned address)
- **Reproducibility**: 100% reproducible, always after ~33 seconds during Thread.exit()
- **Thread**: main thread, `_thread_in_vm` state during Thread.exit()

## Register Pattern (both crashes identical pattern):
### Crash 1 (pid 2519):
- r5 = 0xb3ca5999 (Thread object 0xb3ca5998 + 1)
- r2 = 0xb3ca59d9 = r5 + 0x40 (field access at offset 64)
- r0 = 0 (expected CAS value)
- r1 = thread pointer (new CAS value)

### Crash 2 (pid 14934):  
- r5 = 0xb3cf1b81 (Thread object 0xb3cf1b80 + 1)
- r2 = 0xb3cf1bc1 = r5 + 0x40 (field access at offset 64)
- r0 = 0 (expected CAS value)
- r1 = thread pointer (new CAS value)
- Thread object confirmed at 0xb3cf1b80 by crash log (klass: java/lang/Thread)
- Field at offset 64 is `threadLocals` (reference field)

## Root Cause Analysis
The oop pointer to the Thread object consistently has a +1 error. On 32-bit markWord:
- lock_bits = 2 (bottom 2 bits)
- locked_value = 0 (binary 00)
- unlocked_value = 1 (binary 01)  
- monitor_value = 2 (binary 10)
- marked_value = 3 (binary 11)

The +1 looks like a forwarding pointer with the "unlocked" tag bit (01) not stripped.

## Shenandoah Forwarding on 32-bit:
```cpp
// shenandoahForwarding.inline.hpp
inline oop ShenandoahForwarding::get_forwardee_raw_unchecked(oop obj) {
  markWord mark = obj->mark();
  if (mark.is_marked()) {  // checks bottom 2 bits == 3
    HeapWord* fwdptr = (HeapWord*) mark.clear_lock_bits().to_pointer();
    if (fwdptr != NULL) {
      return cast_to_oop(fwdptr);
    }
  }
  return obj;
}
```

`is_marked()` checks if lock_bits == 3 (binary 11). 
`clear_lock_bits()` masks off bottom 2 bits.

For a forwarding pointer, the mark word MUST have lock_bits == 3 (marked_value).
The forwarding address is stored as: `forwarding_addr | marked_value`
`clear_lock_bits()` strips the bottom 2 bits: `value & ~3`

This should correctly strip the tag. So the C++ forwarding code looks correct.

## Assembly-level forwarding (ARM32 load barrier):
Need to check the assembly-level load reference barrier in:
- `shenandoahBarrierSetAssembler_arm.cpp` → `resolve_forwarding_pointer()`
- Check that it correctly strips the mark word tag bits

## Key Investigation Points:
1. Look at `resolve_forwarding_pointer()` in shenandoahBarrierSetAssembler_arm.cpp
2. Check if the ARM32 load reference barrier correctly strips forwarding tags
3. Check `Atomic::cmpxchg` calling convention on ARM32 (what passes the oop in r5?)  
4. There may be a CAS on the Thread object using an oop that was forwarded but 
   the forwarding bits weren't stripped in the assembly barrier

## Critical Files:
- `jdk17u-local/src/hotspot/cpu/arm/gc/shenandoah/shenandoahBarrierSetAssembler_arm.cpp`
  - `resolve_forwarding_pointer()` function
  - `load_reference_barrier()` function
- `jdk17u-local/src/hotspot/share/gc/shenandoah/shenandoahForwarding.inline.hpp`
  - C++ forwarding code (looks correct)
- `jdk17u-local/src/hotspot/share/oops/markWord.hpp`
  - lock_mask_in_place = 3 (bottom 2 bits)
  - clear_lock_bits() = value & ~3
  - is_marked() = (bottom 2 bits == 3)

## Pre-existing crash log files with this pattern:
- hs_err_pid2675.log (Mar 28)
- hs_err_pid28561.log (Mar 27)  
- hs_err_pid6713.log (Mar 28)
- hs_err_pid2519.log (Mar 29, our build, same pattern)
- hs_err_pid14934.log (Mar 29, our build, same pattern)

## Assembly-level forwarding (resolve_forward_pointer_not_null):
Located at shenandoahBarrierSetAssembler_arm.cpp lines 203-229.
Uses mvn/tst/orr/mvn pattern (same as aarch64):
```
ldr tmp, [dst, #mark_offset]   // load mark word
mvn tmp, tmp                    // invert
tst tmp, #lock_mask_in_place    // test bottom 2 bits
bne done                        // if not both 0, not forwarded
orr tmp, tmp, #marked_value     // set bottom 2 bits
mvn dst, tmp                    // invert back = forward pointer
done:
```
lock_mask_in_place = 3, marked_value = 3.
This CORRECTLY strips the forwarding tag bits. The forward pointer stored
as (addr | 3) gets decoded as: ~(~(addr|3) | 3) = ~(~addr & ~3 | 3) = addr.
Wait actually: if mark = addr | 3, then ~mark = ~addr & ~3 = ~addr & 0xFFFFFFFC.
tst ~mark, 3 => 0, so is forwarded.
orr: ~mark | 3 = (~addr & 0xFFFFFFFC) | 3 = ~addr | 3 (since ~addr already has bits in positions 0,1)
Actually: ~addr & 0xFFFFFFFC | 3. If addr is aligned to 4 bytes, addr ends in 00.
~addr ends in 11. ~addr & 0xFFFFFFFC strips bottom 2 bits => ends in 00.
Then | 3 => ends in 11. Then mvn => ~(stuff ending in 11) => ends in 00.
So final = ~(~addr & 0xFFFFFFFC | 3). 
Hmm, let me trace for addr = 0xb3cf1b80 (from crash 2):
mark = 0xb3cf1b80 | 3 = 0xb3cf1b83
~mark = 0x4c30e47c
tst 0x4c30e47c, 3 => 0x4c30e47c & 3 = 0, so Z=1, forwarded
orr: 0x4c30e47c | 3 = 0x4c30e47f
mvn: ~0x4c30e47f = 0xb3cf1b80 ✓ CORRECT!

But the crash shows r5 = 0xb3cf1b81 = 0xb3cf1b80 + 1. So either:
1. The resolve_forward_pointer assembly is NOT the code path hitting this
2. The oop was corrupted by something else (not forwarding)
3. There's a different code path computing this oop

## Theory: The CAS caller is NOT using a forwarded oop
The +1 error happens at the JVM C++ level (StubRoutines::atomic_cmpxchg).
The caller is in libjvm.so at some offset. During Thread.exit(), the JVM
does various operations on the Thread oop. If the Thread oop has been
evacuated by Shenandoah but the C++ code is using a stale (from-space)
pointer... BUT the from-space pointer should still be valid (just in from-space).

The +1 error is NOT consistent with forwarding (which adds +3 to bottom bits).
It's either:
- A biased lock pattern in the mark word leaking
- Some other corruption
- A 1-byte misalignment from boolean field packing

Actually: Thread object at 0xb3cf1b80. r5 = 0xb3cf1b81. 
0xb3cf1b81 & 3 = 1 = unlocked_value. This IS the mark word pattern for
an unlocked object!

HYPOTHESIS: Code is reading the mark word of the Thread object ("unlocked" 
mark word = hash:age:01), then using THAT as the oop pointer instead of
the actual object address. The mark word for unlocked objects contains the
hash/age/etc in the upper bits with tag=01.

But 0xb3cf1b81 looks too much like an address. For an unlocked object,
mark = (hash << hash_shift) | (age << age_shift) | unlocked_value.
On 32-bit: hash_shift = 2+1+4+0 = 7, hash_bits = 25.
So mark = (hash<<7) | (age<<3) | 1.
For mark to equal 0xb3cf1b81: hash = 0xb3cf1b81 >> 7 = 0x1679e37.
This is plausible as a hash value.

But then r2 = r5 + 0x40 = mark + 0x40. Why would code add a field offset
to a mark word? That makes no sense.

NEW HYPOTHESIS: Something is casting an oop address to include its mark word
bits. Specifically, in an oop, the mark word is at offset 0. If code reads
[oop+0] (the mark word) and treats it as an oop for field access, you'd
get mark_word + field_offset. But that gives a completely wrong address.

Actually the more likely hypothesis: The oop r5=0xb3cf1b81 was stored
with an extra bit somewhere. If biased locking writes 
(thread_ptr | biased_lock_pattern) into the mark word, and this gets
confused with an oop... no, that doesn't work.

SIMPLEST HYPOTHESIS: There's a raw pointer somewhere that has the
"unlocked" mark word tag bit (1) OR'd into it. This could happen if
code reads from a displaced header / stack lock and uses it as an oop.

Need to look at what call site in libjvm.so triggers the CAS.
The caller is at offset 0x002766f3 in libjvm.so (from first crash).
We can't easily look at compiled VM code on the target.

## NEW APPROACH: Add -XX:+PrintAssembly or use debug build
OR: focus on getting a stack trace from the VM frame. The hs_err log
should have a native stack trace. Let me look at that more carefully.

## KEY FINDING: Alignment check NOT reached
The alignment check in `reorder_cmpxchg_func` (atomic_linux_arm.hpp line 112) 
prints "SHENANDOAH BUG: unaligned CAS" and returns early to prevent SIGBUS.
BUT this message NEVER appears in robot stderr output. This means the CAS stub
is being called DIRECTLY, NOT through the C++ Atomic::cmpxchg path
(which goes through reorder_cmpxchg_func).

The stub is being called from assembly-level code that bypasses C++.
Candidates:
1. C1 compiled code doing CAS on oop fields  
2. Interpreter inline CAS path
3. Some other assembly stub

## KEY FINDING: MethodType infinite recursion before crash  
Right before every crash, there's a massive infinite recursion:
  MethodType.makeImpl → MethodTypeForm.canonicalize → MethodTypeForm.findForm → MethodType.makeImpl → ...
This repeats many times and produces "Exception in thread main" repeatedly.
Then the SIGBUS occurs. The MethodType recursion may be CAUSING the crash
(e.g., stack overflow corrupting data) or it may be an unrelated symptom.

## ROOT CAUSE FOUND AND FIXED

### The Bug
`markWord::has_monitor()` checks `(value() & 2) != 0`, which is TRUE for BOTH:
- monitor tag (10) - correct
- Shenandoah forwarding tag (11) - INCORRECT!

When a forwarded oop reaches `ObjectSynchronizer::inflate()`:
1. `inflate()` reads mark word of old copy → forwarding pointer (new_addr | 3)
2. `has_monitor()` returns TRUE (because tag 11 has bit 1 set)
3. `monitor()` does `value ^ 2` → `(new_addr | 3) ^ 2` = `new_addr | 1`
4. Returns `new_addr | 1` as ObjectMonitor* 
5. ObjectMonitor::enter() tries CAS on this+64 → SIGBUS (unaligned!)

### Call chain from crash
```
ObjectSynchronizer::enter (synchronizer.cpp:475)
  → inflate (synchronizer.cpp) → mark.has_monitor() returns TRUE for forwarding ptr
    → mark.monitor() returns new_addr | 1 (GARBAGE ObjectMonitor*)
      → ObjectMonitor::enter (via try_set_owner_from at objectMonitor.inline.hpp:140)
        → Atomic::cmpxchg on &_owner (ObjectMonitor*+64 = new_addr+65)  
          → StubRoutines::atomic_cmpxchg → ldrex [unaligned addr] → SIGBUS
```

### Fixes Applied
1. **markWord.hpp** line 276: Changed `has_monitor()` from
   `(value() & monitor_value) != 0` to `(value() & lock_mask_in_place) == monitor_value`
   Now correctly checks tag == 10 only, excluding forwarding pointer tag 11.

2. **synchronizer.cpp** inflate(): Added forwarding resolution at top of for-loop:
   ```cpp
   if (mark.is_marked()) {
       object = cast_to_oop(mark.clear_lock_bits().to_pointer());
       continue;
   }
   ```

### Files Modified for This Fix
- jdk17u-local/src/hotspot/share/oops/markWord.hpp
- jdk17u-local/src/hotspot/share/runtime/synchronizer.cpp

### Why This Bug Was Pre-Existing  
This bug existed since the Shenandoah ARM32 port began. Crash logs from Mar 25-28 
show the same SIGBUS pattern. The nmethod barrier changes did NOT cause it.

## Critical observation about call path:
Since the alignment check in C++ is NOT reached, the CAS must be called 
from JIT-compiled code or interpreter assembly. This means the corrupted 
oop is likely a register value in compiled/interpreted code, NOT a C++ variable.

In C1-compiled code, oop register values come from:
1. Direct loads from oop fields (through load barriers)
2. Method parameters
3. Local variables
The load reference barrier (LRB) in the C1 code should update forwarded oops.
If the LRB has a bug, a forwarded oop might retain tag bits.

## Implementation progress so far (complete before this investigation):
- 3 bugs fixed (arraycopy_prologue, resolve_oop_handle, cmpxchg_oop)
- nmethod entry barriers FULLY IMPLEMENTED and ENABLED
- Build succeeds, robot starts and runs ~33s before crash
- Stack watermark barriers NOT YET implemented (still disabled)
- The SIGBUS crash is PRE-EXISTING (predates our changes)

## Key files modified by our changes:
1. shenandoahBarrierSetAssembler_arm.cpp - 3 bug fixes
2. barrierSetAssembler_arm.cpp - nmethod_entry_barrier + c2i_entry_barrier
3. barrierSetNMethod_arm.cpp - full NativeNMethodBarrier implementation
4. stubGenerator_arm.cpp - generate_method_entry_barrier  
5. c1_MacroAssembler_arm.cpp - build_frame hook
6. stubRoutines_arm.hpp/cpp - _method_entry_barrier field
7. macroAssembler_arm.hpp/cpp - load_method_holder, load_method_holder_cld, resolve_oop_handle
8. shenandoahArguments.cpp - removed NMethodBarrier disable  
9. shenandoahSATBMode.cpp - updated guard to only skip StackWatermark
