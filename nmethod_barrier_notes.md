# nmethod Entry Barrier Implementation Notes for ARM32

## What was already done (before nmethod barriers):
- Bug fixes: arraycopy_prologue (src==dst fixed), resolve_oop_handle (now uses access_load_at), cmpxchg_oop (added dmb barriers)
- Files modified: shenandoahBarrierSetAssembler_arm.cpp, macroAssembler_arm.cpp, macroAssembler_arm.hpp

## Files to modify for nmethod barriers:

### 1. barrierSetAssembler_arm.hpp - Add declarations
```cpp
  virtual void nmethod_entry_barrier(MacroAssembler* masm);
  virtual void c2i_entry_barrier(MacroAssembler* masm);
```

### 2. barrierSetAssembler_arm.cpp - Add implementations
ARM32 nmethod_entry_barrier sequence (8 instructions + 1 data word = 9 words = 36 bytes):
```
instr 0: ldr Rtemp, [PC, #guard_offset]    ; load guard value (PC-relative literal)
instr 1: dmb ish                            ; LoadLoad barrier  
instr 2: ldr R3(scratch), [Rthread, #disarmed_offset]  ; load thread disarmed value
instr 3: cmp Rtemp, R3                      ; compare guard vs disarmed
instr 4: beq skip                           ; fast path
instr 5: movw Rtemp, #stub_lo              ; load stub address low 16 bits
instr 6: movt Rtemp, #stub_hi              ; load stub address high 16 bits
instr 7: blx Rtemp                          ; call slow-path stub
instr 8: b skip                             ; jump over data
guard:   .word 0                            ; guard value (data)
skip:
```

Registers safe to use: Rtemp (R12, scratch, never callee-saved) and some temp we save/restore.
Must NOT clobber: R0-R3 (args), LR (return addr), Rthread (R10).
Problem: `cmp` clobbers flags. `blx` sets LR. But after nmethod_entry_barrier returns, we continue normally so LR isn't needed (we have it on stack from frame setup).

Actually, looking more carefully - the barrier is emitted INSIDE build_frame/method prologue. At that point LR has been saved to stack. So clobbering LR during blx is ok because it'll be restored from stack.

For Rtemp: ARM32 macroassembler uses Rtemp=R12 as scratch. It's caller-saved/scratch. Safe to use.
For the second register for thread disarmed load: We can't use R0-R3 (arg registers). We can temporarily use LR (R14) since it's been saved already.

Revised sequence using Rtemp and LR:
```
instr 0: ldr Rtemp, [PC, #guard_offset]    ; PC-relative literal load
instr 1: dmb ish                            ; memory barrier
instr 2: ldr LR, [Rthread, #disarmed_offset] ; load thread disarmed value (LR already saved)
instr 3: cmp Rtemp, LR                      ; compare
instr 4: beq skip                           ; fast path
instr 5: movw Rtemp, #stub_lo              ; load stub address
instr 6: movt Rtemp, #stub_hi              ; load stub address  
instr 7: blx Rtemp                          ; call stub (clobbers LR, but already saved)
instr 8: b skip                             ; skip guard data
guard:   .word 0                            ; guard value
skip:
```
Total: 9 instructions + 1 data word = 10 words = 40 bytes

### 3. barrierSetNMethod_arm.cpp - Full NativeNMethodBarrier implementation
Key info:
- entry_barrier_offset = -4 * 10 (10 words back from frame_complete)  
- Guard is at instruction_address() + 9 * 4 = 36 bytes from barrier start
- Use Atomic::load_acquire / release_store on guard word
- deoptimize(): Modify return address to SharedRuntime::get_handle_wrong_method_stub()
- ARM32 frame layout: {fp, lr} are pushed by enter()

### 4. stubGenerator_arm.cpp - generate_method_entry_barrier
- Save all call-clobbered registers (R0-R3, R12, LR, and FP regs on hard-float)
- Allocate 4 words for deopt frame info {sp, fp, lr, pc}  
- Call BarrierSetNMethod::nmethod_stub_entry_barrier(address* return_address_ptr)
- If returns 0: restore regs, return
- If non-zero: load deopt frame info and jump to handle_wrong_method_stub

### 5. C1 hook - c1_MacroAssembler_arm.cpp
In build_frame(), after MacroAssembler::build_frame(framesize):
```cpp
BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
bs->nmethod_entry_barrier(this);
```

### 6. sharedRuntime_arm.cpp - native wrapper
After nmethod_entry_barrier call, set frame_complete

### 7. c2i_entry_barrier implementation
- Check rmethod != NULL
- Chase to ClassLoaderData
- Check keep_alive
- Check weak holder
- If dead: jump to handle_wrong_method_stub

ARM32 ARM register conventions:
- R0-R3: caller-saved args
- R4-R11: callee-saved (R10=Rthread, R11=FP)
- R12: scratch (Rtemp)
- R13: SP
- R14: LR
- R15: PC

## c2i_entry_barrier helpers needed on ARM32
Need to add to macroAssembler_arm.cpp:
- load_method_holder_cld(Register result, Register method)
- load_method_holder(Register result, Register method)  - already partially exists via load_mirror pattern
- resolve_weak_handle(Register result, Register tmp)

## shenandoahArguments.cpp change
Remove: FLAG_SET_DEFAULT(ShenandoahNMethodBarrier, false);

## shenandoahSATBMode.cpp change  
Remove the #ifndef ARM32 guard around ShenandoahNMethodBarrier check
