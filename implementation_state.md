# Implementation State - Save Point

## Already completed (saved in files):
1. arraycopy_prologue fix - in shenandoahBarrierSetAssembler_arm.cpp  
2. resolve_oop_handle fix - in macroAssembler_arm.cpp/.hpp (uses access_load_at now with tmp=Rtemp default)
3. cmpxchg_oop dmb fix - in shenandoahBarrierSetAssembler_arm.cpp
4. shenandoah_plan.md and todo.md updated

## nmethod barrier implementation - files and changes needed:

### A. stubRoutines_arm.hpp (jdk17u-local/src/hotspot/cpu/arm/stubRoutines_arm.hpp)
Add to class Arm (after _partial_subtype_check):
```cpp
  static address _method_entry_barrier;
```
And public accessor:
```cpp
  static address method_entry_barrier() { return _method_entry_barrier; }
```

### B. stubRoutines_arm.cpp (jdk17u-local/src/hotspot/cpu/arm/stubRoutines_arm.cpp) 
Add after _partial_subtype_check:
```cpp
address StubRoutines::Arm::_method_entry_barrier = NULL;
```

### C. barrierSetAssembler_arm.hpp (jdk17u-local/src/hotspot/cpu/arm/gc/shared/barrierSetAssembler_arm.hpp)
Add before `virtual void barrier_stubs_init()`:
```cpp
  virtual void nmethod_entry_barrier(MacroAssembler* masm);
  virtual void c2i_entry_barrier(MacroAssembler* masm);
```

### D. barrierSetAssembler_arm.cpp (jdk17u-local/src/hotspot/cpu/arm/gc/shared/barrierSetAssembler_arm.cpp)
Add implementations. The nmethod_entry_barrier emits:
- ldr Rtemp, [PC, #offset_to_guard]  ; load guard value
- dmb ish                             ; LoadLoad barrier
- ldr LR, [Rthread, #disarmed_off]   ; load thread disarmed value (LR saved)
- cmp Rtemp, LR                      ; compare  
- beq skip                           ; fast path
- movw Rtemp, #stub_lo               ; load stub address
- movt Rtemp, #stub_hi
- blx Rtemp                          ; call entry barrier stub
- b skip                             ; skip guard data
- guard: .word 0                     ; guard value data
- skip:
Total 9 instrs + 1 data word = 10 words = 40 bytes  
entry_barrier_offset_in_bytes = -40

### E. barrierSetNMethod_arm.cpp (jdk17u-local/src/hotspot/cpu/arm/gc/shared/barrierSetNMethod_arm.cpp)
Replace ShouldNotReachHere() stubs with real implementation:
- NativeNMethodBarrier class: guard at offset 9*4=36 from barrier start
- deoptimize: write frame {sp, fp, lr, pc} for unwinding
- disarm: Atomic::release_store on guard word  
- is_armed: Atomic::load_acquire != disarmed_value

### F. stubGenerator_arm.cpp
Add generate_method_entry_barrier() and call in generate_all()
- Save R0-R3, R12, LR (RegisterSet)
- Allocate 4 words for deopt frame
- Call BarrierSetNMethod::nmethod_stub_entry_barrier
- Restore and return or jump to deopt

### G. c1_MacroAssembler_arm.cpp  
In build_frame(), add after sub_slow(SP, SP, frame_size_in_bytes):
```cpp
  BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
  bs->nmethod_entry_barrier(this);
```

### H. sharedRuntime_arm.cpp
In generate_native_wrapper, after frame setup, add:
```cpp
  BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
  bs->nmethod_entry_barrier(masm);
```

### I. macroAssembler_arm.hpp and .cpp
Add load_method_holder, load_method_holder_cld, resolve_weak_handle methods

### J. shenandoahArguments.cpp
Remove: FLAG_SET_DEFAULT(ShenandoahNMethodBarrier, false);

### K. shenandoahSATBMode.cpp
Change #ifndef ARM32 to only guard ShenandoahStackWatermarkBarrier (keep NMethodBarrier check)

## Stack watermark implementation (Phase 3):
- vm_version_arm.hpp: add supports_stack_watermark_barrier() { return true; }
- frame_arm.cpp: split sender() into sender_raw() + watermark callback
- shenandoahArguments.cpp: remove ShenandoahStackWatermarkBarrier=false
- shenandoahSATBMode.cpp: remove remaining ARM32 guard

## Key ARM32 register info:
- Rthread = R10, Rtemp = R12 (scratch), FP = R11, SP = R13, LR = R14, PC = R15
- R0-R3: caller-saved args, R4-R11: callee-saved
- R9ifScratched: conditionally added to register save sets
- RegisterSet syntax: RegisterSet(R0, R3) | RegisterSet(R12) | RegisterSet(LR)
- dmb(DMB_all, Rtemp) for full barrier
- raw_push(FP, LR) for frame setup
- movw/movt for loading 32-bit immediates
