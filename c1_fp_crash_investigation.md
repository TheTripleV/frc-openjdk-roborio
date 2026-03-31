# C1 FP=0 Crash Investigation

## Status
- **Cset check fix**: DONE and VERIFIED. All 5 interpreter-only tests pass with concurrent Shenandoah.
  - File: `jdk17u-local/src/hotspot/cpu/arm/gc/shenandoah/shenandoahBarrierSetAssembler_arm.cpp`
  - Lines ~297-307: Added in-collection-set check in `load_reference_barrier()` 
  - Uses R2/R3 as temps (already saved on stack), pattern copied from C1 code at line 613

## C1 FP=0 Crash Details
- SIGSEGV at "return entry points" interpreter codelet, FP(R11)=0
- Instruction: `ldr sp, [fp, #-8]` with fp=0 → accesses 0xfffffff8
- Happens ~0.7-0.9s in, before any GC cycle starts
- Thread: main thread, _thread_in_Java

## What Works and What Doesn't
| Config | Result |
|--------|--------|
| C1 + SerialGC | WORKS |
| C1 + Shenandoah passive | WORKS |  
| Interpreter + Shenandoah concurrent | WORKS |
| C1 + Shenandoah concurrent (SATB) | CRASHES |

**Conclusion: The bug is in C1 + SATB barriers specifically.**

## Key Register Definitions
- FP = R11, altFP_7_11 = R7 (since FP_REG_NUM=11)
- Rthread = R10, Rtemp = R12
- C1 allocable: R0-R9 (10 regs). R10-R15 are reserved.
- Rbcp = altFP_7_11 = R7 (interpreter), Rmethod = R9, Rlocals = R8

## Files Already Examined
1. `shenandoahBarrierSetAssembler_arm.cpp` (767 lines) - ALL barriers
2. `c1_MacroAssembler_arm.cpp` line 57-77 - build_frame: raw_push(FP,LR); sub SP; nmethod_entry_barrier(). remove_frame: add SP; raw_pop(FP,LR).
3. `c1_FrameMap_arm.cpp` - R0-R9 allocatable, R10+ reserved
4. `c1_Runtime1_arm.cpp` lines 188-242 - save_live_registers pushes FP+LR then R0-R6,R7(altFP),R8-R10,R12
5. `barrierSetAssembler_arm.cpp` lines 259-300 - nmethod_entry_barrier uses Rtemp+LR only
6. `stubGenerator_arm.cpp` lines 2976-3040 - method_entry_barrier stub saves/restores FP properly
7. `c1_Defs_arm.hpp` - pd_nof_cpu_regs_reg_alloc=10
8. `register_arm.hpp` - FP=R11, altFP=R7

## Hypothesis: SATB Pre-Barrier C1 Stub
The SATB pre-barrier is the ONLY thing different between passive (works) and SATB mode (crashes).
- `gen_pre_barrier_stub` (line 585): pushes pre_val to SP, calls pre_barrier_c1_runtime_code_blob
- `generate_c1_pre_barrier_runtime_stub` (line 650): 
  - Pushes ONLY R0-R3, R12, LR (saved_regs). **Does NOT save R4-R11!**
  - Fast path: checks marking, inserts into SATB buffer → returns
  - Slow path: calls save_live_registers() which DOES save all regs including FP

**POTENTIAL BUG**: The fast path uses registers R0, R1, R2 to manage the SATB queue:
```cpp
const Register r_pre_val_0 = R0;
const Register r_index_1 = R1;  
const Register r_buffer_2 = R2;
```
These are in the saved_regs set (R0-R3, R12, LR). After the fast path returns, these are popped correctly.

But wait - the gen_pre_barrier_stub calls this code blob via `__ call()`. A call on ARM sets LR. But LR is in saved_regs and gets saved/restored. So that's fine.

## Next Steps to Investigate
1. Look at shenandoahBarrierSetC1_arm.cpp - how C1 LIR generates the pre-barrier
2. Check if the pre-barrier stub's "call" instruction clobbers something via the call trampoline
3. Check if there's an issue with the C1 LIR register allocation for Shenandoah barriers
4. Try running with `-XX:-ShenandoahSATBBarrier` (may not be allowed)
5. Try adding FP to saved_regs in the pre-barrier runtime stub as a test fix

## Build/Deploy/Test Commands
```bash
# Build (in workspace root, uses Docker)
bash build-fast.sh 2>&1

# Deploy IPK
scp frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/tmp/

# Install
ssh admin@10.59.40.2 "opkg install /tmp/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite"

# Test with C1 + concurrent
ssh admin@10.59.40.2 "/usr/local/frc/JRE/bin/java -XX:+UseShenandoahGC -XX:+UnlockExperimentalVMOptions -XX:ShenandoahGCMode=satb -Xmx128m -Xms64m -cp /home/lvuser/tests ShenandoahBasic 2>&1"

# Test with interpreter only (as fallback)
ssh admin@10.59.40.2 "/usr/local/frc/JRE/bin/java -XX:+UseShenandoahGC -XX:+UnlockExperimentalVMOptions -XX:ShenandoahGCMode=satb -Xmx128m -Xms64m -XX:TieredStopAtLevel=0 -cp /home/lvuser/tests ShenandoahBasic 2>&1"
```
