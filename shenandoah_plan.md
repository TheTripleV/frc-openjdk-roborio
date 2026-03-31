# Shenandoah GC ARM32 Port - Implementation Plan

## Overview
Port Shenandoah GC to ARM32v7 (soft-float) for FRC RoboRIO with **full concurrent mode**
and all runtime barriers enabled. This gives low pause times through concurrent marking
and evacuation, which is the key advantage of Shenandoah over other collectors.

## Architecture

### ARM32 Specifics
- **No CompressedOops**: 32-bit platform, `UseCompressedOops` is always false
- **Registers**: Rthread=R10, Rtemp=R12 (scratch), c_rarg0-3=R0-R3, FP=R11, SP=R13, LR=R14
- **Callee-saved**: R4-R11; Caller-saved: R0-R3, R12, LR
- **No `far_call`**: Use `call(address, relocInfo::runtime_call_type)` instead
- **No `load_parameter` on StubAssembler**: Load from stack with `ldr(reg, Address(SP, offset))`
- **`store_parameter`**: Only `jint` or `Metadata*`, NOT Register - use `str()` directly
- **RegisterSet** for push/pop: `RegisterSet(R0) | RegisterSet(R1)` instead of `RegSet::of()`
  - No `operator-` on RegisterSet; build sets conditionally
- **3 tmp registers**: ARM32 `load_at`/`store_at` takes (tmp1, tmp2, tmp3) not (tmp1, tmp_thread)
- **`arraycopy_prologue`**: Takes `int callee_saved_regs`, not `RegSet saved_regs`
- **C1-only**: RoboRIO builds with C1 compiler only; C2 AD file exists for build system
  compatibility but is not exercised at runtime

### Concurrent Mode (Full Barriers)
All barriers are **enabled** for low-pause concurrent operation:
- **ShenandoahSATBBarrier**: true - SATB snapshot-at-the-beginning write barrier for concurrent marking
- **ShenandoahLoadRefBarrier**: true - Load reference barrier resolves forwarding pointers
- **ShenandoahIUBarrier**: false by default (only used in IU mode, not default SATB mode)
- **ShenandoahCASBarrier**: true - CAS barrier handles forwarding pointer resolution in CAS operations
- **ShenandoahCloneBarrier**: true - Clone barrier (implemented in shared code, no arch-specific asm)
- **ShenandoahGCMode**: "satb" (default concurrent mode)

Only `ShenandoahVerifyOptoBarriers` is disabled on ARM32 (no C2 verification needed).

## Files Created/Modified

### 1. Build System Changes

#### `make/autoconf/jvm-features.m4` (MODIFIED)
- Added `test "x$OPENJDK_TARGET_CPU" = "xarm"` to the shenandoahgc platform check

#### `src/hotspot/share/gc/shenandoah/shenandoahArguments.cpp` (MODIFIED)
- Added `ARM32` to the `#if !(defined ...)` platform guard
- ARM32 block only disables `ShenandoahVerifyOptoBarriers` (C2 verification)
- All barriers remain at their default enabled state for concurrent operation

### 2. New Platform Files

#### `src/hotspot/cpu/arm/gc/shenandoah/shenandoahBarrierSetAssembler_arm.hpp` (CREATED)
Header declaring `ShenandoahBarrierSetAssembler` for ARM32 with ARM32-specific signatures.

#### `src/hotspot/cpu/arm/gc/shenandoah/shenandoahBarrierSetAssembler_arm.cpp` (CREATED)
Full implementation of all barriers (~700 lines):

- **`arraycopy_prologue`**: Checks gc_state for HAS_FORWARDED|MARKING, calls
  `ShenandoahRuntime::arraycopy_barrier_oop_entry` when active.
- **`satb_write_barrier_pre`**: Adapted from G1's `g1_write_barrier_pre` on ARM32.
  Checks marking active, loads previous value, tries SATB queue buffer fast-path,
  falls to runtime via `ShenandoahRuntime::write_ref_field_pre_entry`.
- **`resolve_forward_pointer_not_null`**: Uses mvn/tst/orr/mvn pattern to check mark
  word forwarding bits (low 2 bits == 11 → forwarded).
- **`load_reference_barrier`** (interpreter path): Checks gc_state, saves caller registers,
  computes address before modifying R0 (avoids base register clobbering), performs cset
  test for strong refs, calls appropriate `ShenandoahRuntime::load_reference_barrier_*`.
- **`iu_barrier`**: Saves all caller-saved registers, handles register conflicts when
  dst is R0 or Rtemp, calls `satb_write_barrier_pre`.
- **`load_at`**: Base load + optional LRB + optional keep-alive barrier.
- **`store_at`**: Flattens address, SATB pre-barrier + IU barrier + base store.
- **`try_resolve_jobject_in_native`**: Delegates to base class, checks EVACUATION gc_state.
- **`cmpxchg_oop`**: 4-step CAS with forwarding pointer resolution using
  `atomic_cas_bool`. Handles concurrent evacuation false negatives.
- **C1 stubs**:
  - `gen_pre_barrier_stub`: Stores pre_val to reserved area, calls pre-barrier blob
  - `gen_load_reference_barrier_stub`: Cset test, stores params, calls LRB blob
  - `generate_c1_pre_barrier_runtime_stub`: Fast-path SATB enqueue, slow-path runtime
  - `generate_c1_load_reference_barrier_runtime_stub`: Saves all caller-saved regs,
    loads params via frame offset, calls runtime, returns result through parameter area

#### `src/hotspot/cpu/arm/gc/shenandoah/c1/shenandoahBarrierSetC1_arm.cpp` (CREATED)
C1 compiler integration:
- `LIR_OpShenandoahCompareAndSwap::emit_code()` - CAS with IU barrier
- `ShenandoahBarrierSetC1::atomic_cmpxchg_at_resolved()` - C1 CAS codegen with SATB pre-barrier
- `ShenandoahBarrierSetC1::atomic_xchg_at_resolved()` - C1 atomic exchange with IU + LRB

#### `src/hotspot/cpu/arm/gc/shenandoah/shenandoah_arm.ad` (CREATED)
C2 instruction definitions (for build system compatibility):
- `compareAndSwapP_shenandoah` / `ShenandoahWeakCompareAndSwapP`
- `compareAndExchangeP_shenandoah`

### 3. Shared Code Changes

#### `src/hotspot/cpu/arm/gc/shared/barrierSetAssembler_arm.hpp` (MODIFIED)
- Added `try_resolve_jobject_in_native` virtual method declaration (was missing on ARM32)

#### `src/hotspot/cpu/arm/gc/shared/barrierSetAssembler_arm.cpp` (MODIFIED)
- Added base implementation of `try_resolve_jobject_in_native` (strips weak tag, loads)

## Key Design Decisions

### Register Management in LRB Runtime Stub (C1)
The C1 load reference barrier runtime blob uses a "save-all + parameter area" pattern
(same as aarch64):
1. Push ALL caller-saved registers (R0-R3, R12, LR)
2. Load parameters from below the save frame (offset = save_count * wordSize)
3. Call runtime
4. Store result to parameter area
5. Pop all registers (restoring original values)
6. Load result from parameter area into R0
This ensures all registers are preserved except R0 (result).

### Address Computation Order in LRB (Interpreter)
The load address is computed into R1 BEFORE moving the object to R0, because
`load_addr.base()` might be R0. Computing the address first avoids clobbering
the base register.

### IU Barrier Register Conflict Handling
When `dst` (pre_val) is R0 or Rtemp, it conflicts with scratch registers used by
`satb_write_barrier_pre`. The IU barrier moves dst to R3 (which is saved on stack)
before calling the write barrier.

### Forward Pointer Resolution
Uses the same mvn/tst/orr/mvn pattern as aarch64:
- Invert mark word
- Test low 2 bits (00 in inverted = 11 in original = forwarded)
- Set low 2 bits and invert back = forwarding pointer

## Reference Implementations
- **Primary**: AArch64 Shenandoah (closest ARM architecture)
- **32-bit reference**: x86_32 Shenandoah (handles 32-bit oop paths)
- **ARM32 patterns**: G1 ARM32 barrier set (ARM32 assembly idioms)

## Bugs Found in Code Review — ALL FIXED

### BUG 1: arraycopy_prologue passes dst as both src and dst — FIXED
- The function received only one address register `addr` (which is `to`/R1)
- It set R0 = addr, R1 = addr, so `arraycopy_barrier_oop_entry(to, to, count)` was called
- **Fix**: R0 already contains `from` at the call site (callee_saved_regs >= 3),
  so we just set R1 = addr (dst) and count from the correct register

### BUG 2: resolve_oop_handle not using Access API — FIXED
- `macroAssembler_arm.cpp::resolve_oop_handle` used raw `ldr` instead of `access_load_at`
- **Fix**: Changed to `access_load_at(T_OBJECT, IN_NATIVE, result, Address(result, 0), tmp, noreg, noreg)`

### BUG 3: cmpxchg_oop missing memory barriers — FIXED
- ARM32 `atomic_cas_bool` uses `ldrex/strex` which provide atomicity but NO ordering
- **Fix**: Added `dmb ish` after the CAS loop for release semantics

## Missing ARM32 Prerequisites (from Martin's PPC64 analysis)

Three features that Shenandoah/ZGC require are missing or stubbed on ARM32:

### 1. OopHandle Access API (JDK-8260369) — FIXED
- `resolve_oop_handle` now uses `access_load_at(T_OBJECT, IN_NATIVE, ...)`
- GC barriers properly applied on OopHandle loads

### 2. nmethod entry barriers (JDK-8260372) — COMPLETE
- Fully implemented in `barrierSetAssembler_arm.cpp`, `barrierSetNMethod_arm.cpp`,
  `stubGenerator_arm.cpp`, `c1_MacroAssembler_arm.cpp`, `stubRoutines_arm.hpp/cpp`
- `ShenandoahNMethodBarrier` is now enabled (removed the `false` override)
- Guard at offset 36, entry_barrier_offset=-40, 40 bytes per nmethod entry
- Uses InlinedAddress + ldr_literal pattern (ARM32 has no `ldr(Register, Label)`)

### 3. Stack watermark barriers (JDK-8253180) — MUST IMPLEMENT (NEXT PRIORITY)
- ARM32 does not declare `supports_stack_watermark_barrier()`
- Currently disabled: `ShenandoahStackWatermarkBarrier=false` in `shenandoahArguments.cpp`
- **Without stack watermarks, forwarded oops on Java thread stacks cause:**
  - SIGBUS: markWord::has_monitor() misinterprets forwarding pointers as inflated monitors (FIXED)
  - NULL klass: zeroed-out objects encountered during above-TAMS scanning
  - Null field values: stale references to evacuated objects cause toString() to return null
- Stack scanning currently happens at safepoints instead of concurrently
- **Required implementation**:
  - Add `constexpr static bool supports_stack_watermark_barrier() { return true; }` to `vm_version_arm.hpp`
  - Refactor `frame_arm.cpp::sender()` to split into `sender_raw()` + watermark callback
  - Remove the `ShenandoahStackWatermarkBarrier=false` override from `shenandoahArguments.cpp`
  - Remove the `#ifndef ARM32` guard from `shenandoahSATBMode.cpp`

## Critical Bug: markWord::has_monitor() vs Shenandoah Forwarding — FIXED

### Root Cause
On ARM32, markWord low 2 bits encode lock state:
- `00` = thin lock (locked_value)
- `01` = unlocked (unlocked_value) 
- `10` = inflated monitor (monitor_value)
- `11` = Shenandoah forwarding pointer (marked_value)

`markWord::has_monitor()` was implemented as `(value() & monitor_value) != 0` which is
`(value & 2) != 0`. This matches **both** tag 10 (monitor) and tag 11 (forwarding).

When a forwarded oop reaches `ObjectSynchronizer::inflate()`, the forwarding pointer
`new_addr | 3` passes `has_monitor()`, then `monitor()` does `value ^ 2` = `new_addr | 1`,
returning a garbage ObjectMonitor* that's misaligned by 1 byte. CAS on `_owner` (offset 64)
gives an unaligned address → `ldrex` on ARM32 → SIGBUS.

### Fix
1. **markWord.hpp**: Changed `has_monitor()` to `(value() & lock_mask_in_place) == monitor_value`
   — checks `(value & 3) == 2` exactly, excluding tag 11
2. **synchronizer.cpp**: Added forwarding resolution at top of `inflate()` loop:
   ```cpp
   if (mark.is_marked()) {
     object = cast_to_oop(mark.clear_lock_bits().to_pointer());
     continue;
   }
   ```

### Verification
Robot ran for >2 minutes with the fix (previously crashed at ~33s). No new hs_err crash logs.

## Implementation Priority

1. ~~**Fix Bug 1** (arraycopy_prologue)~~ — DONE
2. ~~**Fix Bug 2** (resolve_oop_handle)~~ — DONE
3. ~~**Fix Bug 3** (cmpxchg_oop DMB)~~ — DONE
4. ~~**Implement nmethod entry barriers**~~ — DONE
5. ~~**Fix markWord::has_monitor() SIGBUS**~~ — DONE
6. ~~**Implement stack watermark barriers**~~ — DONE (support + sender hook + flag enable)
7. **Fix concurrent-mode invokehandle crash** — NEXT

## Latest Runtime Findings (Mar 29)

- Concurrent Shenandoah (`satb`) still crashes very early with:
  - `SIGSEGV` in `java.lang.invoke.BootstrapMethodInvoker.invoke(...)+319`
  - problematic frame in interpreter codelet `invokehandle`
  - invalid access address pattern near `0x40xxxxxx`
- Crash reproduces with `-Xint`, so it is not C1 nmethod codegen specific.
- `ShenandoahGCMode=passive` (with `-XX:+UnlockDiagnosticVMOptions`) runs stably, indicating
  the failure is tied to concurrent evacuation/forwarding behavior.
- Flags verified on target: `ShenandoahLoadRefBarrier=true`, `ShenandoahNMethodBarrier=true`,
  `ShenandoahStackWatermarkBarrier=true`.
- Targeted fixes attempted (not yet sufficient):
  - `templateTable_arm.cpp`: forwarded-oop healing for receiver loaded from interpreter stack.
  - `methodHandles_arm.cpp`: forwarded-oop healing for MH receiver stack load and popped trailing `MemberName`.
  - `shenandoahBarrierSetAssembler_arm.cpp`:
    - fixed alias-load flattening to preserve index shift/scale
    - fixed alias-load flattening when source index aliases `Rtemp`
    - fixed LRB slow-path load-address recomputation to preserve index shift/scale
  - `interp_masm_arm.cpp`: flattened `load_resolved_reference_at_index` address computation
    to avoid base/dst aliasing in `load_heap_oop` call shape.
  - Diagnostic-only experiment (`AS_RAW` on resolved-reference load) changed codelet layout
    but did not remove crash; reverted.

### Current working hypothesis
- The failing address is formed in `invokehandle` while loading appendix from
  `resolved_references` (`objArray` element load), with a corrupted index path before dereference.
- Remaining issue is likely in ARM32 invokehandle/cpCache-to-appendix index handling under
  concurrent mode (or a still-missing ARM32-specific invariant/ordering step in this interpreter path),
  rather than only in one LRB call-site.

## Testing Strategy
1. Build with `--with-jvm-features=shenandoahgc`
2. Run with `-XX:+UseShenandoahGC` (default concurrent/SATB mode)
3. Target: `hotspot_gc_shenandoah` test suite
4. Target: `tier1` with `-XX:+UseShenandoahGC`
5. Verify pause times are low with concurrent marking/evacuation
