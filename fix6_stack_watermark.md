# FIX #6: Stack Watermark Barrier on ARM32

## Root Cause of Aggressive Mode Crashes
ARM32 declares `supports_stack_watermark_barrier() = true` but 3 of 4 critical
paths fail to check the actual watermark address. When only a watermark is set 
(no global safepoint), `polling_word = watermark_address` (word-aligned, bit 0=0).
ARM32's `tst(tmp, 1)` only checks poll_bit and never triggers for watermarks.

All aggressive-mode Shenandoah crashes are caused by this: GC concurrent thread
root processing sets watermarks, oops on stack never get updated, from-space
regions get recycled, threads access stale/garbage oops.

Crash functions: Klass::method_at_vtable(), oopDesc::klass(), Klass::is_subtype_of()
All during JVM bootstrap (~0.6s) with aggressive GC.

## Fix locations (4 changes):

### FIX 6A: macroAssembler_arm - Add at_return overload
Files to change:
- macroAssembler_arm.hpp (line ~1073): Add declaration
- macroAssembler_arm.cpp (line ~1904): Add implementation

Current declaration in .hpp:
```cpp
  void safepoint_poll(Register tmp1, Label& slow_path);
```
Add after it:
```cpp
  void safepoint_poll(Register tmp1, Label& slow_path, bool at_return);
```

Current implementation in .cpp:
```cpp
void MacroAssembler::safepoint_poll(Register tmp1, Label& slow_path) {
  ldr_u32(tmp1, Address(Rthread, JavaThread::polling_word_offset()));
  tst(tmp1, SafepointMechanism::poll_bit());
  b(slow_path, ne);
}
```
Add new overload after it:
```cpp
void MacroAssembler::safepoint_poll(Register tmp1, Label& slow_path, bool at_return) {
  ldr_u32(tmp1, Address(Rthread, JavaThread::polling_word_offset()));
  if (at_return) {
    // Stack watermark check: if FP > polling_word, process the watermark.
    // When only a watermark is set (no global safepoint), polling_word holds
    // the watermark address (word-aligned, bit 0 = 0). The old tst-based
    // check misses this because it only tests the poll bit.
    cmp(FP, tmp1);
    b(slow_path, hi);
  } else {
    tst(tmp1, SafepointMechanism::poll_bit());
    b(slow_path, ne);
  }
}
```

### FIX 6B: templateInterpreterGenerator_arm.cpp - native method return
File: jdk17u-local/src/hotspot/cpu/arm/templateInterpreterGenerator_arm.cpp
Around line 919 (in generate_native_entry, after thread state transition):
Find: `__ safepoint_poll(Rtemp, call);`
Replace with `__ safepoint_poll(Rtemp, call, true);`

### FIX 6C: c1_LIRAssembler_arm.cpp - C1 return_op
File: jdk17u-local/src/hotspot/cpu/arm/c1_LIRAssembler_arm.cpp
Around line 287:
Current:
```cpp
void LIR_Assembler::return_op(LIR_Opr result, C1SafepointPollStub* code_stub) {
  __ remove_frame(initial_frame_size_in_bytes());
  __ read_polling_page(Rtemp, relocInfo::poll_return_type);
  __ ret();
}
```
Replace with:
```cpp
void LIR_Assembler::return_op(LIR_Opr result, C1SafepointPollStub* code_stub) {
  __ remove_frame(initial_frame_size_in_bytes());
  code_stub->set_safepoint_offset(__ offset());
  __ relocate(relocInfo::poll_return_type);
  __ safepoint_poll(Rtemp, *code_stub->entry(), true /* at_return */);
  __ ret();
}
```

### FIX 6D: sharedRuntime_arm.cpp - native wrapper return
File: jdk17u-local/src/hotspot/cpu/arm/sharedRuntime_arm.cpp
Around line 1235 (in generate_native_wrapper, safepoint check):
Find: `__ safepoint_poll(R2, call_safepoint_runtime);`
Replace with: `__ safepoint_poll(R2, call_safepoint_runtime, true);`

## Interpreter return (ALREADY CORRECT)
interp_masm_arm.cpp line ~717 uses cmp(FP, Rtemp); b(slow_path, hi); ✅

## Test results before fix #6:
- 23/33 shenandoah_suite tests PASS (all adaptive/static/compact/passive)
- 10/33 FAIL (all aggressive mode)
- Robot code runs 64s before system OOM (not GC bug)

## Current build state:
- Fixes 1-5a deployed (register clobbering fix working)
- Fix 6 (this file) needs implementation
