# Implementation Context - NMethod Barriers for ARM32

## Key Architecture Details (ARM32)
- Rthread=R10, Rtemp=R12(scratch), FP=R11, SP=R13, LR=R14, Rmethod=R9
- ARM32 register for ldr on targets: `ldr Rd, [PC, #offset]` (PC = current_instr + 8)
- `movw(rd, imm16)` and `movt(rd, imm16)` available on ARMv7+
- `dmb` on ARMv7 emits single instruction `0xF57FF050 | opt`, does NOT clobber any register
- `membar(LoadLoad, Rtemp)` → `dmb(DMB_all, Rtemp)` → single `dmb` instruction on ARMv7
- `cbz(Rtemp, label)` on ARM32 = `cmp(rt, 0) + b(L, eq)` = 2 instructions
- `cbnz(Rtemp, label)` on ARM32 = `cmp(rt, 0) + b(L, ne)` = 2 instructions
- No `ldr(Register, Label)` overload exists! Must use `InlinedAddress` + `ldr_literal` + `bind_literal`
- `mov_address` may emit only 1 instruction if upper 16 bits are 0; use explicit movw+movt for deterministic size

## Literal Load Pattern on ARM32
```cpp
InlinedAddress guard_literal((address)0);  // Initial guard value, relocInfo::none
__ ldr_literal(Rtemp, guard_literal);  // Forward reference, patched on bind
// ... code ...
__ bind_literal(guard_literal);  // Binds label + emits emit_address((address)0)
```
Forward references work: ldr_literal emits ldr with placeholder, pd_patch_instruction patches
when bind() resolves all pending references.

## NMethod Entry Barrier ARM32 Instruction Sequence (10 words = 40 bytes)
```
Offset  Instr
0       ldr Rtemp, [PC, #28]        ; ldr_literal (guard_literal) -- offset to guard = 36-8=28
4       dmb                          ; membar(LoadLoad)
8       ldr LR, [Rthread, #off]      ; thread disarmed value
12      cmp Rtemp, LR               
16      beq skip                     
20      movw Rtemp, #lo              ; stub address low
24      movt Rtemp, #hi              ; stub address high
28      blx Rtemp                    ; call barrier stub
32      b skip                       
36      .word 0                      ; guard data (bind_literal)
40      skip:                        ; frame_complete offset
```
- guard_addr = barrier_start + 36 = barrier_start + 9*4
- entry_barrier_offset = -40 = -(10*4) from frame_complete

## Key NativeNMethodBarrier offsets
- guard_addr offset = 9 * 4 = 36 from barrier start
- BARRIER_TOTAL_SIZE = 10 words = 40 bytes
- entry_barrier_offset = -4 * 10 = -40

## barrierSetNMethod_arm.cpp Structure
NativeNMethodBarrier class:
- guard_addr() = instruction_address() + 9*4
- get_value() = Atomic::load_acquire(guard_addr())
- set_value(int) = Atomic::release_store(guard_addr(), value)
- verify() - check instruction patterns

deoptimize():
- return_address_ptr points to saved LR in stub frame
- frame_pointers_t *new_frame = (frame_pointers_t*)(return_address_ptr - 5)
  On 32-bit: -5 * 4 = -20 bytes before return_address_ptr = start of deopt space
- frame_pointers_t { intptr_t *sp; intptr_t *fp; address lr; address pc; }
- new_frame->sp = sender.sp(), fp = sender.fp(), lr = sender.pc(), 
  pc = SharedRuntime::get_handle_wrong_method_stub()

native_nmethod_barrier():
- barrier_addr = nm->code_begin() + nm->frame_complete_offset() + entry_barrier_offset
- entry_barrier_offset = -4 * 10 = -40

## Stub Generator (generate_method_entry_barrier) for ARM32
Stack layout after stub setup:
```
[SP+36] = saved LR (return to nmethod) ← return_address_ptr
[SP+32] = saved FP
[SP+16..31] = deopt space (4 words)
[SP+0..15]  = R0-R3 (saved args)
```
Stub flow:
1. stmdb SP!, {FP, LR}   // push FP,LR. SP-=8
2. mov FP, SP             // save frame pointer
3. sub SP, SP, #16        // reserve deopt space (4 words)
4. stmdb SP!, {R0-R3}     // save args. SP-=16
5. set_last_Java_frame(SP, FP, true)  — with save_last_java_pc=true
6. add R0, SP, #36        // return_address_ptr = &saved_LR
7. mov_address + blx to nmethod_stub_entry_barrier
8. reset_last_Java_frame()
9. mov Rtemp, R0          // save deopt flag
10. ldmia SP!, {R0-R3}    // restore args. SP+=16 → now at deopt space
11. cmp Rtemp, #0; beq normal
Deopt path:
12. ldr R1, [SP, #0]      // new SP (sender sp)
13. ldr FP, [SP, #4]      // new FP (sender fp)
14. ldr LR, [SP, #8]      // new LR (sender pc)
15. ldr Rtemp, [SP, #12]  // handle_wrong_method_stub addr
16. mov SP, R1             // restore sender SP
17. bx Rtemp              // jump to wrong_method_stub
Normal path:
12. mov SP, FP             // skip deopt space
13. ldmia SP!, {FP, LR}   // restore saved FP+LR
14. bx LR                  // return to nmethod

## C1 build_frame Hook
In c1_MacroAssembler_arm.cpp::build_frame():
After `sub_slow(SP, SP, frame_size_in_bytes);` add:
```cpp
  // Insert nmethod entry barrier into frame.
  BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
  bs->nmethod_entry_barrier(this);
```
Include: gc/shared/barrierSet.hpp, gc/shared/barrierSetAssembler.hpp

## Files to Modify
1. barrierSetAssembler_arm.cpp - fix ldr to ldr_literal, fix mov_address to explicit movw/movt
2. barrierSetNMethod_arm.cpp - full rewrite with NativeNMethodBarrier class
3. stubGenerator_arm.cpp - add generate_method_entry_barrier, hook in generate_all
4. c1_MacroAssembler_arm.cpp - add nmethod_entry_barrier call in build_frame
5. shenandoahArguments.cpp - remove ShenandoahNMethodBarrier disable

## set_last_Java_frame signature
```cpp
int MacroAssembler::set_last_Java_frame(Register last_java_sp, Register last_java_fp, 
                                         bool save_last_java_pc, Register tmp);
```
- Saves fp (if != noreg), saves PC (if save_last_java_pc), saves sp (last or SP)
- reset_last_Java_frame(Register tmp) zeroes saved fields

## pd_patch_instruction handles
- B/BL instructions
- address_placeholder_instruction (== 0xFFFFFFFF)
- LDR Rd, [PC, offset] — assertion: `(instr & 0x0f7f0000) == 0x051f0000`
