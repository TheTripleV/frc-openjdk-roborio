/*
 * Copyright (c) 2018, 2021, Red Hat, Inc. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "gc/shenandoah/shenandoahBarrierSet.hpp"
#include "gc/shenandoah/shenandoahBarrierSetAssembler.hpp"
#include "gc/shenandoah/shenandoahForwarding.hpp"
#include "gc/shenandoah/shenandoahHeap.inline.hpp"
#include "gc/shenandoah/shenandoahHeapRegion.hpp"
#include "gc/shenandoah/shenandoahRuntime.hpp"
#include "gc/shenandoah/shenandoahThreadLocalData.hpp"
#include "gc/shenandoah/heuristics/shenandoahHeuristics.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interp_masm.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/thread.hpp"
#include "utilities/macros.hpp"
#ifdef COMPILER1
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "gc/shenandoah/c1/shenandoahBarrierSetC1.hpp"
#endif

#define __ masm->

void ShenandoahBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, bool is_oop,
                                                       Register addr, Register count, int callee_saved_regs) {
  if (is_oop) {
    bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
    if ((ShenandoahSATBBarrier && !dest_uninitialized) || ShenandoahIUBarrier || ShenandoahLoadRefBarrier) {

      Label done;

      // Avoid calling runtime if count == 0
      __ cbz(count, done);

      // Is GC active?
      Address gc_state(Rthread, in_bytes(ShenandoahThreadLocalData::gc_state_offset()));
      __ ldrb(Rtemp, gc_state);
      if (ShenandoahSATBBarrier && dest_uninitialized) {
        __ tst(Rtemp, ShenandoahHeap::HAS_FORWARDED);
        __ b(done, eq);
      } else {
        __ tst(Rtemp, ShenandoahHeap::HAS_FORWARDED | ShenandoahHeap::MARKING);
        __ b(done, eq);
      }

      // Save all caller-saved registers including VFP D0-D7 so the C++
      // arraycopy barrier cannot corrupt floating-point state.
      // R0 (src), R1/addr (dst), R2/count are all caller-saved GPRs and are
      // automatically preserved by push_call_clobbered_registers().
      assert(addr->encoding() < callee_saved_regs, "addr must be saved");
      assert(count->encoding() < callee_saved_regs, "count must be saved");
      assert(callee_saved_regs >= 3, "must save R0 (src), R1 (dst), R2 (count)");
      assert(addr == R1, "arraycopy dst must be R1");
      assert(count == R2, "arraycopy count must be R2");

      __ push_call_clobbered_registers();

      // R0=src, R1=dst, R2=count are already in the correct registers.
      // No UseCompressedOops on ARM32.
      __ call(CAST_FROM_FN_PTR(address, ShenandoahRuntime::arraycopy_barrier_oop_entry));

      __ pop_call_clobbered_registers();
      __ bind(done);
    }
  }
}

// Shenandoah write barrier pre.
// Adapted from G1BarrierSetAssembler::g1_write_barrier_pre on ARM32.
// Blows all volatile registers (R0-R3, Rtemp, LR).
// If store_addr != noreg, previous value is loaded from [store_addr];
//   in this case store_addr and new_val are preserved.
// Otherwise pre_val register is preserved.
void ShenandoahBarrierSetAssembler::shenandoah_write_barrier_pre(MacroAssembler* masm,
                                                                  Register store_addr,
                                                                  Register new_val,
                                                                  Register pre_val,
                                                                  Register tmp1,
                                                                  Register tmp2) {
  if (ShenandoahSATBBarrier) {
    satb_write_barrier_pre(masm, store_addr, new_val, pre_val, tmp1, tmp2);
  }
}

void ShenandoahBarrierSetAssembler::satb_write_barrier_pre(MacroAssembler* masm,
                                                            Register store_addr,
                                                            Register new_val,
                                                            Register pre_val,
                                                            Register tmp1,
                                                            Register tmp2) {
  Label done;
  Label runtime;

  if (store_addr != noreg) {
    assert_different_registers(store_addr, new_val, pre_val, tmp1, tmp2, noreg);
  } else {
    assert(new_val == noreg, "should be");
    assert_different_registers(pre_val, tmp1, tmp2, noreg);
  }

  Address gc_state(Rthread, in_bytes(ShenandoahThreadLocalData::gc_state_offset()));
  Address in_progress(Rthread, in_bytes(ShenandoahThreadLocalData::satb_mark_queue_active_offset()));
  Address index(Rthread, in_bytes(ShenandoahThreadLocalData::satb_mark_queue_index_offset()));
  Address buffer(Rthread, in_bytes(ShenandoahThreadLocalData::satb_mark_queue_buffer_offset()));

  // Is marking active?
  assert(in_bytes(SATBMarkQueue::byte_width_of_active()) == 1, "Assumption");
  __ ldrb(tmp1, in_progress);
  __ cbz(tmp1, done);

  // Do we need to load the previous value?
  if (store_addr != noreg) {
    // Use AS_RAW to perform a raw load without triggering the Shenandoah LRB.
    // The SATB pre-barrier only needs the old value for enqueueing; applying the
    // full load_reference_barrier here would create a nested LRB (with its own
    // register save/restore of {R0-R3, R12, LR}) inside this barrier, which is
    // both unnecessary and risks register corruption. This matches aarch64.
    __ load_heap_oop(pre_val, Address(store_addr, 0), noreg, noreg, noreg, AS_RAW);
  }

  // Is the previous value null?
  __ cbz(pre_val, done);

  // Can we store original value in the thread's buffer?
  // Is index == 0?
  __ ldr(tmp1, index);           // tmp1 := *index_adr
  __ ldr(tmp2, buffer);

  __ subs(tmp1, tmp1, wordSize); // tmp1 := tmp1 - wordSize
  __ b(runtime, lt);             // If negative, goto runtime

  __ str(tmp1, index);           // *index_adr := tmp1

  // Record the previous value
  __ str(pre_val, Address(tmp2, tmp1));
  __ b(done);

  __ bind(runtime);

  // Save all caller-saved registers including VFP D0-D7.  We use
  // push_call_clobbered_registers() so that the runtime call cannot corrupt
  // any floating-point value that was live in the surrounding JIT code.
  // The input registers (store_addr / new_val / pre_val) are among the
  // caller-saved GPRs, so they are automatically preserved.
  __ push_call_clobbered_registers();

  // pre_val must be in R0 for the call.
  if (pre_val != R0) {
    __ mov(R0, pre_val);
  }
  __ mov(R1, Rthread);

  __ call_VM_leaf(CAST_FROM_FN_PTR(address, ShenandoahRuntime::write_ref_field_pre_entry), R0, R1);

  __ pop_call_clobbered_registers();

  __ bind(done);
}

void ShenandoahBarrierSetAssembler::resolve_forward_pointer(MacroAssembler* masm, Register dst, Register tmp) {
  assert(ShenandoahLoadRefBarrier || ShenandoahCASBarrier, "Should be enabled");
  Label is_null;
  __ cbz(dst, is_null);
  resolve_forward_pointer_not_null(masm, dst, tmp);
  __ bind(is_null);
}

// Resolve forward pointer: if the mark word indicates forwarding, extract the
// forwarding pointer. Otherwise leave dst alone.
// Clobbers tmp. Preserves dst if not forwarded.
void ShenandoahBarrierSetAssembler::resolve_forward_pointer_not_null(MacroAssembler* masm, Register dst, Register tmp) {
  assert(ShenandoahLoadRefBarrier || ShenandoahCASBarrier, "Should be enabled");
  // Load the mark word
  // The mark word encodes forwarding: if lowest 2 bits == 11 (marked_value),
  // the rest is the forwarding pointer (with low 2 bits masked off).
  //
  // Strategy (same as aarch64):
  // - Invert the mark word
  // - Test lowest two bits == 0 (which means original had 11)
  // - If so, set lowest two bits and invert back = forwarding pointer
  // - Otherwise leave dst alone

  assert_different_registers(tmp, dst);

  Label done;
  __ ldr(tmp, Address(dst, oopDesc::mark_offset_in_bytes()));
  // Invert: tmp = ~tmp
  __ mvn(tmp, tmp);
  // Test lowest two bits
  __ tst(tmp, markWord::lock_mask_in_place);
  __ b(done, ne);
  // Lowest 2 bits of inverted were 00, meaning original was 11 (forwarded)
  // Set the lowest 2 bits: tmp |= marked_value
  __ orr(tmp, tmp, markWord::marked_value);
  // Invert back: dst = ~tmp = forwarding pointer
  __ mvn(dst, tmp);
  __ bind(done);
}

void ShenandoahBarrierSetAssembler::load_reference_barrier(MacroAssembler* masm, Register dst, Address load_addr, DecoratorSet decorators) {
  assert(ShenandoahLoadRefBarrier, "Should be enabled");

  // Fast path: if gc_state has no relevant bits, skip barrier
  Address gc_state(Rthread, in_bytes(ShenandoahThreadLocalData::gc_state_offset()));

  bool is_strong  = ShenandoahBarrierSet::is_strong_access(decorators);
  bool is_weak    = ShenandoahBarrierSet::is_weak_access(decorators);
  bool is_phantom = ShenandoahBarrierSet::is_phantom_access(decorators);

  int flags = ShenandoahHeap::HAS_FORWARDED;
  if (!is_strong) {
    flags |= ShenandoahHeap::WEAK_ROOTS;
  }

  Label done;

  // Use R3 (saved/restored) instead of Rtemp for gc_state check,
  // because load_addr.base() may be Rtemp (flattened address from load_at).
  // ARM ldm (pop) does NOT modify condition flags, so tst flags survive the pop.
  __ push(RegisterSet(R3));
  __ ldrb(R3, gc_state);
  __ tst(R3, flags);
  __ pop(RegisterSet(R3));
  __ b(done, eq);

  // Null check
  __ cbz(dst, done);

  // Slow path: save all caller-saved registers (GPRs + VFP D0-D7).
  // push_call_clobbered_registers() pushes VFP first then GPRs, so the GPR
  // save-slot offsets used by the result-patching code below are stable and
  // start at SP+0 after the push (VFP is further down the stack).
  __ push_call_clobbered_registers();

  // Fix #8D: Handle the case where dst == R1. The load_addr computation below
  // writes into R1, which would clobber dst before we copy it to R0.
  // The aarch64 version handles this explicitly with rscratch1; on ARM32 we
  // use R3 (already saved on stack) as a temporary to hold the oop value.
  // We track original_dst to correctly write the result to the right stack slot.
  Register original_dst = dst;
  if (dst == R1) {
    __ mov(R3, dst);
    dst = R3;
  }

  // Compute load address into R1 BEFORE moving dst to R0,
  // because load_addr.base() might be R0 (== dst after aliased load_at path).
  if (load_addr.base() != R1) {
    if (load_addr.index() != noreg) {
      AsmOperand idx(load_addr.index(), load_addr.shift(), load_addr.shift_imm());
      if (load_addr.disp() != 0) {
        __ add(R1, load_addr.base(), load_addr.disp());
        __ add(R1, R1, idx);
      } else {
        __ add(R1, load_addr.base(), idx);
      }
    } else if (load_addr.disp() != 0) {
      __ add(R1, load_addr.base(), load_addr.disp());
    } else {
      __ mov(R1, load_addr.base());
    }
  } else {
    if (load_addr.index() != noreg) {
      AsmOperand idx(load_addr.index(), load_addr.shift(), load_addr.shift_imm());
      __ add(R1, R1, idx);
    }
    if (load_addr.disp() != 0) {
      __ add(R1, R1, load_addr.disp());
    }
  }

  if (dst != R0) {
    __ mov(R0, dst);
  }

  // For strong references, check if the object is in the collection set.
  // Non-cset objects don't need evacuation and can be returned as-is.
  // This matches the aarch64 and C1 implementations.
  Label done_call;
  if (is_strong) {
    // ARM32 Safety: heap bounds check before cset_map access.
    // NULL is already handled by the cbz(dst, done) check above.
    // If R0 is an out-of-heap garbage oop, R0 >> region_shift may be enormous,
    // causing an out-of-bounds cset_map byte read.  A random OOB byte of 0
    // would silently pass the garbage oop through; non-zero would call the
    // runtime with a garbage oop.  Fix: validate R0 ∈ [heap_base, heap_base+max_capacity)
    // before the cset_map check.  Out-of-heap oops are never in cset; branch
    // directly to done_call (returns R0 unchanged).
    {
      ShenandoahHeap* heap = ShenandoahHeap::heap();
      __ mov_address(R2, (address)heap->base());
      __ sub(R2, R0, R2);  // R2 = R0 - heap_base (wraps if R0 < heap_base)
      __ mov_address(R3, (address)(uintptr_t)heap->max_capacity());
      __ cmp(R2, R3);
      __ b(done_call, hs);  // unsigned >=: outside heap -> skip cset check and runtime
    }
    // R0 is within heap bounds: inline cset check is safe
    __ mov_address(R2, (address)ShenandoahHeap::in_cset_fast_test_addr());
    __ mov(R3, AsmOperand(R0, lsr, ShenandoahHeapRegion::region_size_bytes_shift_jint()));
    __ ldrb(R2, Address(R2, R3));
    __ cmp(R2, 0);
    __ b(done_call, eq);  // Not in cset, skip runtime call
  }

  if (is_strong) {
    __ call(CAST_FROM_FN_PTR(address, ShenandoahRuntime::load_reference_barrier_strong), relocInfo::runtime_call_type);
  } else if (is_weak) {
    __ call(CAST_FROM_FN_PTR(address, ShenandoahRuntime::load_reference_barrier_weak), relocInfo::runtime_call_type);
  } else {
    assert(is_phantom, "only remaining option");
    __ call(CAST_FROM_FN_PTR(address, ShenandoahRuntime::load_reference_barrier_phantom), relocInfo::runtime_call_type);
  }

  __ bind(done_call);

  // Result is in R0. We need to get it into original_dst, but pop_call_clobbered
  // will restore R0-R3 from the stack, overwriting any register we set.
  // Solution: write the result into original_dst's GPR save slot before popping.
  // We use original_dst (not dst) because dst may have been reassigned from R1 to R3
  // in the Fix #8D case; the pop will restore original_dst from its save slot.
  //
  // Stack layout after push_call_clobbered_registers():
  //   SP+0=R0, SP+4=R1, SP+8=R2, SP+12=R3, [SP+16=R9,] SP+N=R12, SP+N+4=LR
  //   below LR: D0-D7 VFP save area (64 bytes when VFP present)
  // GPR save slots start at SP+0 regardless of VFP.
  if (original_dst->encoding() <= 3) {
    // original_dst is R0-R3: update its save slot so pop restores the new value
    __ str(R0, Address(SP, original_dst->encoding() * wordSize));
  } else if (original_dst == R12) {
    // R12 is in save_regs too; update its save slot.
    // Stack layout: SP+0=R0, SP+4=R1, SP+8=R2, SP+12=R3, [SP+16=R9 if scratched], SP+N=R12
#if R9_IS_SCRATCHED
    __ str(R0, Address(SP, 5 * wordSize));
#else
    __ str(R0, Address(SP, 4 * wordSize));
#endif
  } else if (original_dst != R0) {
    // original_dst is callee-saved (R4-R11), not in save_regs, won't be clobbered
    __ mov(original_dst, R0);
  }
  // else dst == R0 && encoding > 3 can't happen

  __ pop_call_clobbered_registers();

  __ bind(done);
}

void ShenandoahBarrierSetAssembler::iu_barrier(MacroAssembler* masm, Register dst, Register tmp) {
  if (ShenandoahIUBarrier) {
    // Save all caller-saved registers including VFP D0-D7, matching AArch64's
    // push_call_clobbered_registers() pattern.
    __ push_call_clobbered_registers();

    // satb_write_barrier_pre requires pre_val, tmp1, tmp2 to all be distinct.
    // If dst (pre_val) is R0 or Rtemp, it would conflict with our scratch regs.
    Register pre_val = dst;
    if (dst == R0 || dst == Rtemp) {
      pre_val = R3;  // R3 is saved on stack, safe to use
      __ mov(pre_val, dst);
    }
    satb_write_barrier_pre(masm, noreg, noreg, pre_val, Rtemp, R0);
    __ pop_call_clobbered_registers();
  }
}

void ShenandoahBarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                             Register dst, Address src, Register tmp1, Register tmp2, Register tmp3) {
  // 1: non-reference load, no additional barrier is needed
  if (!is_reference_type(type)) {
    BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp2, tmp3);
    return;
  }

  // 2: load a reference from src location and apply LRB if needed
  if (ShenandoahBarrierSet::need_load_reference_barrier(decorators, type)) {
    // Handle address aliasing: if dst overlaps src address registers,
    // the load clobbers the address. We need to preserve it for the LRB.
    //
    // Strategy: compute flat address into Rtemp before loading. Rtemp (R12)
    // is a designated scratch register on ARM32 and is safe to clobber.
    // After loading into dst, Rtemp still holds the address (the load only
    // writes to dst, not Rtemp). We pass Address(Rtemp) to LRB.
    //
    // For non-aliased case, we can use the original address directly.
    if (dst == src.base() || dst == src.index()) {
      Register scaled_index = src.index();
      bool restore_r3 = false;

      if (scaled_index == Rtemp) {
        __ push(RegisterSet(R3));
        __ mov(R3, Rtemp);
        scaled_index = R3;
        restore_r3 = true;
      }

      if (src.index() != noreg) {
        AsmOperand idx(scaled_index, src.shift(), src.shift_imm());
        if (src.disp() != 0) {
          __ add(Rtemp, src.base(), src.disp());
          __ add(Rtemp, Rtemp, idx);
        } else {
          __ add(Rtemp, src.base(), idx);
        }
      } else if (src.disp() != 0) {
        __ add(Rtemp, src.base(), src.disp());
      } else {
        __ mov(Rtemp, src.base());
      }
      // Load from Rtemp (flat address), result in dst. Rtemp survives.
      BarrierSetAssembler::load_at(masm, decorators, type, dst, Address(Rtemp), tmp1, tmp2, tmp3);

      load_reference_barrier(masm, dst, Address(Rtemp), decorators);

      if (restore_r3) {
        __ pop(RegisterSet(R3));
      }
    } else {
      BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp2, tmp3);
      load_reference_barrier(masm, dst, src, decorators);
    }
  } else {
    BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp2, tmp3);
  }

  // 3: apply keep-alive barrier if needed
  if (ShenandoahBarrierSet::need_keep_alive_barrier(decorators, type)) {
    __ push_call_clobbered_registers();
    Register keepalive_tmp1 = (tmp1 != noreg) ? tmp1 : Rtemp;
    Register keepalive_tmp2 = (tmp2 != noreg) ? tmp2 : ((dst != R0) ? R0 : R1);
    satb_write_barrier_pre(masm /* masm */,
                           noreg /* store_addr */,
                           noreg /* new_val */,
                           dst /* pre_val */,
                           keepalive_tmp1 /* tmp1 */,
                           keepalive_tmp2 /* tmp2 */);
    __ pop_call_clobbered_registers();
  }
}

void ShenandoahBarrierSetAssembler::store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                              Address obj, Register new_val, Register tmp1, Register tmp2, Register tmp3,
                                              bool is_null) {
  bool on_oop = is_reference_type(type);
  if (!on_oop) {
    BarrierSetAssembler::store_at(masm, decorators, type, obj, new_val, tmp1, tmp2, tmp3, is_null);
    return;
  }

  // flatten object address if needed
  assert(obj.mode() == basic_offset, "pre- or post-indexing is not supported here");

  const Register store_addr = obj.base();
  if (obj.index() != noreg) {
    assert(obj.disp() == 0, "index or displacement, not both");
    assert(obj.offset_op() == add_offset, "addition is expected");
    __ add(store_addr, obj.base(), AsmOperand(obj.index(), obj.shift(), obj.shift_imm()));
  } else if (obj.disp() != 0) {
    __ add(store_addr, obj.base(), obj.disp());
  }

  shenandoah_write_barrier_pre(masm, store_addr, new_val, tmp1, tmp2, tmp3);

  if (is_null) {
    BarrierSetAssembler::store_at(masm, decorators, type, Address(store_addr), new_val, tmp1, tmp2, tmp3, true);
  } else {
    // Fix #8A: UNCONDITIONALLY resolve forwarding pointer on stored value.
    // A mutator may hold a stale from-space reference (e.g. from a stack slot
    // not yet updated by the watermark, or surviving from a previous cycle).
    // Previously this only fired when HAS_FORWARDED was set, but stale refs
    // can appear between GC cycles when HAS_FORWARDED is false. Making this
    // unconditional is safe: normal mark words (low bits != 11) cause
    // resolve_forward_pointer to be a no-op; forwarding pointers (low bits
    // == 11) are correctly resolved. Cost: ~4 extra instructions per oop store.
    if (ShenandoahLoadRefBarrier) {
      resolve_forward_pointer(masm, new_val, tmp1);
    }
    iu_barrier(masm, new_val, tmp1);
    BarrierSetAssembler::store_at(masm, decorators, type, Address(store_addr), new_val, tmp1, tmp2, tmp3, false);
  }
}

void ShenandoahBarrierSetAssembler::try_resolve_jobject_in_native(MacroAssembler* masm, Register jni_env,
                                                                   Register obj, Register tmp, Label& slowpath) {
  Label done;
  // Resolve jobject
  BarrierSetAssembler::try_resolve_jobject_in_native(masm, jni_env, obj, tmp, slowpath);

  // Check for null
  __ cbz(obj, done);

  Address gc_state(jni_env, ShenandoahThreadLocalData::gc_state_offset() - JavaThread::jni_environment_offset());
  __ ldrb(tmp, gc_state);

  // Check for heap in evacuation phase.
  // Note: we do NOT check for MARKING here, matching the AArch64 and x86 reference
  // implementations.  A JNI object handle resolved during concurrent marking may hold
  // a pre-value that has not yet been enqueued in the SATB queue, but the slow-path
  // (BarrierSetAssembler::try_resolve_jobject_in_native) already handles the critical
  // cases, and the SATB pre-barrier in the caller covers the rest.
  __ tst(tmp, ShenandoahHeap::EVACUATION);
  __ b(slowpath, ne);

  __ bind(done);
}

// Special Shenandoah CAS implementation that handles false negatives due
// to concurrent evacuation.
void ShenandoahBarrierSetAssembler::cmpxchg_oop(MacroAssembler* masm,
                                                  Register addr,
                                                  Register expected,
                                                  Register new_val,
                                                  bool is_cae,
                                                  Register tmp1,
                                                  Register tmp2,
                                                  Register tmp3,
                                                  Register result) {
  assert(ShenandoahCASBarrier, "Should only be used when CAS barrier is enabled");
  assert_different_registers(addr, expected, new_val, tmp1, tmp2, tmp3, result);

  // Fix #8B: UNCONDITIONALLY resolve forwarding pointer on new_val before CAS.
  // Same reasoning as Fix #8A in store_at: stale from-space references can
  // appear even when HAS_FORWARDED is false (between GC cycles).
  // resolve_forward_pointer already handles null.
  if (ShenandoahLoadRefBarrier) {
    resolve_forward_pointer(masm, new_val, tmp1);
  }

  Label step4, done_step1, done_step3, L_failure, exit;

  // Step 1. Fast-path. Try to CAS with given arguments.
  // ARM32 ldrex/strex only provide atomicity, not ordering.
  // We need full fence semantics for CAS (acquire+release), matching
  // aarch64's ldaxr/stlxr behavior.
  // Release fence before the CAS; acquire fence at success (done label).
  __ bind(step4);

  // DMB for release semantics before CAS
  __ membar(MacroAssembler::Membar_mask_bits(MacroAssembler::StoreStore | MacroAssembler::LoadStore), Rtemp);

  // On ARM32, we use atomic_cas_bool which sets flags:
  // eq if CAS succeeded, ne if failed. tmp1 is scratched.
  __ atomic_cas_bool(expected, new_val, addr, 0, tmp1);
  // dmb does not clobber condition flags, so Z survives
  __ b(done_step1, eq);

  // Step 2. CAS has failed. This may be a false negative.
  // The value read from memory is in expected (cmpxchg semantics put fetched value there
  // on ARM32, but atomic_cas_bool doesn't return the fetched value easily).
  // Instead, load the current value from addr to check.
  __ ldr(tmp2, Address(addr));

  // If the current value in memory is NULL, failure is legitimate
  __ cbz(tmp2, L_failure);

  // If heap is stable, failure is legitimate
  Address gc_state(Rthread, in_bytes(ShenandoahThreadLocalData::gc_state_offset()));
  __ ldrb(tmp3, gc_state);
  __ tst(tmp3, ShenandoahHeap::HAS_FORWARDED);
  __ b(L_failure, eq);

  // Try to resolve the forwarding pointer of the value read from memory.
  // tmp2 currently has the value from memory.
  __ mov(tmp1, tmp2);  // save original in-memory value

  // Resolve forward pointer of tmp1
  // Load mark word from the object pointed to by tmp1
  __ ldr(tmp3, Address(tmp1, oopDesc::mark_offset_in_bytes()));
  // Check if forwarded: low 2 bits == 11 (markWord::marked_value)
  // Use tmp1 for the tag check instead of Rtemp, because C1 may pass
  // Rtemp as tmp3 (register aliasing). tmp1 is free here since it was
  // only needed as a base for loading the mark word above.
  __ and_32(tmp1, tmp3, markWord::lock_mask_in_place);
  __ cmp(tmp1, markWord::marked_value);
  __ b(L_failure, ne); // Not forwarded, failure is real

  // Extract forwarding pointer: clear low 2 bits of mark word
  __ bic(tmp3, tmp3, markWord::lock_mask_in_place);
  // tmp3 now has the forwarded (to-space) address

  // Compare forwarded address with expected
  __ cmp(tmp3, expected);
  __ b(L_failure, ne); // Different objects, failure is real

  // Step 3. The in-memory value (tmp2) is a from-space pointer to the same
  // object as expected. Try CAS with the from-space pointer as expected.
  // Release fence already provided by step4's dmb on initial entry.
  __ atomic_cas_bool(tmp2, new_val, addr, 0, tmp3);
  // If this CAS also fails, another thread may have healed the pointer.
  // Retry from step 1 (via step4 which issues the release DMB).
  // Note: no acquire fence is needed on this retry path — we are not consuming
  // any data from the failing CAS result.  The DMB at step4 provides release
  // before the next CAS attempt, and the DMB at done_step3/done_step1 provides
  // acquire after success.  This matches AArch64's acquire-release semantics
  // embedded per-attempt via ldaxr/stlxr.
  __ b(step4, ne);

  // Step 3 success: in-memory value was tmp2 (from-space pointer of expected).
  __ bind(done_step3);
  __ membar(MacroAssembler::Membar_mask_bits(MacroAssembler::LoadLoad | MacroAssembler::LoadStore), Rtemp);
  if (is_cae) {
    __ mov(result, tmp2);  // return the actual in-memory value that was swapped out
  } else {
    __ mov(result, 1);
  }
  __ b(exit);

  // Step 1 success: in-memory value was expected.
  __ bind(done_step1);
  __ membar(MacroAssembler::Membar_mask_bits(MacroAssembler::LoadLoad | MacroAssembler::LoadStore), Rtemp);
  if (is_cae) {
    __ mov(result, expected);  // return the actual in-memory value that was swapped out
  } else {
    __ mov(result, 1);
  }
  __ b(exit);

  __ bind(L_failure);
  // Invariant: tmp2 holds the last value loaded from addr (or 0 when reached via
  // the null-check cbz path).  For is_cae=true, returning 0 (null) is semantically
  // correct because the in-memory value IS null.  Note that tmp1 may be stale at
  // this point (the mov(tmp1, tmp2) on the forwarding path runs before L_failure is
  // reached via forwarding but NOT via the null-check path); tmp1 is not consumed
  // at L_failure, so there is no bug — this comment documents the invariant.
  if (is_cae) {
    __ mov(result, tmp2);  // return the witness value (what is actually in memory)
  } else {
    __ mov(result, 0);
  }

  __ bind(exit);
}

#undef __

#ifdef COMPILER1

#define __ ce->masm()->

void ShenandoahBarrierSetAssembler::gen_pre_barrier_stub(LIR_Assembler* ce, ShenandoahPreBarrierStub* stub) {
  ShenandoahBarrierSetC1* bs = (ShenandoahBarrierSetC1*)BarrierSet::barrier_set()->barrier_set_c1();

  __ bind(*stub->entry());

  assert(stub->pre_val()->is_register(), "Precondition.");

  Register pre_val_reg = stub->pre_val()->as_register();

  if (stub->do_load()) {
    ce->mem2reg(stub->addr(), stub->pre_val(), T_OBJECT, stub->patch_code(), stub->info(), false /*wide*/, false /*unaligned*/);
  }

  __ cbz(pre_val_reg, *stub->continuation());
  ce->verify_reserved_argument_area_size(1);
  __ str(pre_val_reg, Address(SP));
  __ call(bs->pre_barrier_c1_runtime_code_blob()->code_begin(), relocInfo::runtime_call_type);

  __ b(*stub->continuation());
}

void ShenandoahBarrierSetAssembler::gen_load_reference_barrier_stub(LIR_Assembler* ce, ShenandoahLoadReferenceBarrierStub* stub) {
  ShenandoahBarrierSetC1* bs = (ShenandoahBarrierSetC1*)BarrierSet::barrier_set()->barrier_set_c1();
  __ bind(*stub->entry());

  DecoratorSet decorators = stub->decorators();
  bool is_strong  = ShenandoahBarrierSet::is_strong_access(decorators);
  bool is_weak    = ShenandoahBarrierSet::is_weak_access(decorators);
  bool is_phantom = ShenandoahBarrierSet::is_phantom_access(decorators);
  bool is_native  = ShenandoahBarrierSet::is_native_access(decorators);

  Register obj = stub->obj()->as_register();
  Register res = stub->result()->as_register();
  Register addr = stub->addr()->as_pointer_register();
  Register tmp1 = stub->tmp1()->as_register();
  Register tmp2 = stub->tmp2()->as_register();

  // res may be R0 (from result_register_for, used by load barriers) or
  // a virtual-allocated register (used by store-value barriers where a
  // fixed R0 would clobber the store's base register).

  if (res != obj) {
    __ mov(res, obj);
  }

  if (is_strong) {
    // ARM32 Safety: heap bounds check before cset_map access.
    // If res is an out-of-heap garbage oop, (res >> region_shift) may be enormous,
    // causing an out-of-bounds cset_map byte read.  That byte is uninitialized/random;
    // if it happens to be 0 the barrier is silently skipped and the garbage oop is
    // forwarded into the Java stack, eventually causing a SIGSEGV.
    // Fix: verify res is within [heap_base, heap_base+max_capacity) before the
    // inline cset check.  Out-of-heap oops are routed to the runtime which logs
    // the situation and returns the oop unchanged (safe, since it cannot be in cset).
    //
    // NULL oops must be handled specially: NULL is below the heap base, so the
    // unsigned subtraction wraps and the bounds check would route NULL to do_runtime.
    // But NULL can never be in cset, so just skip to continuation for NULL.
    __ cbz(res, *stub->continuation());
    Label do_runtime;
    {
      ShenandoahHeap* heap = ShenandoahHeap::heap();
      __ mov_address(tmp1, (address)heap->base());
      __ sub(tmp1, res, tmp1);  // tmp1 = res - heap_base (wraps if res < heap_base)
      __ mov_address(tmp2, (address)(uintptr_t)heap->max_capacity());
      __ cmp(tmp1, tmp2);
      __ b(do_runtime, hs);  // unsigned >=: outside heap -> skip inline check, call runtime
    }
    // res is within heap bounds: inline cset check is safe
    __ mov_address(tmp2, (address)ShenandoahHeap::in_cset_fast_test_addr());
    __ mov(tmp1, AsmOperand(res, lsr, ShenandoahHeapRegion::region_size_bytes_shift_jint()));
    __ ldrb(tmp2, Address(tmp2, tmp1));
    __ cbz(tmp2, *stub->continuation());
    __ bind(do_runtime);
  }

  ce->verify_reserved_argument_area_size(2);
  __ str(res, Address(SP, 0));
  __ str(addr, Address(SP, wordSize));
  if (is_strong) {
    if (is_native) {
      __ call(bs->load_reference_barrier_strong_native_rt_code_blob()->code_begin(), relocInfo::runtime_call_type);
    } else {
      __ call(bs->load_reference_barrier_strong_rt_code_blob()->code_begin(), relocInfo::runtime_call_type);
    }
  } else if (is_weak) {
    __ call(bs->load_reference_barrier_weak_rt_code_blob()->code_begin(), relocInfo::runtime_call_type);
  } else {
    assert(is_phantom, "only remaining strength");
    __ call(bs->load_reference_barrier_phantom_rt_code_blob()->code_begin(), relocInfo::runtime_call_type);
  }

  // The runtime stub always returns the resolved oop in R0.
  // Move to the result register if it was allocated elsewhere.
  if (res != R0) {
    __ mov(res, R0);
  }

  __ b(*stub->continuation());
}

#undef __

#define __ sasm->

void ShenandoahBarrierSetAssembler::generate_c1_pre_barrier_runtime_stub(StubAssembler* sasm) {
  // Input:
  // - pre_val pushed on the stack

  __ set_info("shenandoah_pre_barrier_slow_id", false);

  // Save GPR caller-saved registers for the fast path.  VFP is NOT saved here:
  // the fast path is a pure inline sequence that writes only to the SATB queue
  // buffer and index (memory, not registers) and does NOT call into C++.
  // Since no C++ function is invoked on the fast path, D0-D7 cannot be clobbered.
  // The slow (runtime) path uses save_live_registers() which saves VFP there.
  const RegisterSet saved_regs = RegisterSet(R0, R3) | RegisterSet(R12) | RegisterSet(LR);
  const int nb_saved_regs = 6;
  assert(nb_saved_regs == saved_regs.size(), "fix nb_saved_regs");
  __ push(saved_regs);

  const Register r_pre_val_0  = R0; // must be R0, to be ready for the runtime call
  const Register r_index_1    = R1;
  const Register r_buffer_2   = R2;

  Address gc_state(Rthread, in_bytes(ShenandoahThreadLocalData::gc_state_offset()));
  Address queue_index(Rthread, in_bytes(ShenandoahThreadLocalData::satb_mark_queue_index_offset()));
  Address buffer(Rthread, in_bytes(ShenandoahThreadLocalData::satb_mark_queue_buffer_offset()));

  Label done;
  Label runtime;

  // Is marking still active?
  __ ldrb(R1, gc_state);
  __ tst(R1, ShenandoahHeap::MARKING);
  __ b(done, eq);

  __ ldr(r_index_1, queue_index);
  __ ldr(r_pre_val_0, Address(SP, nb_saved_regs * wordSize));
  __ ldr(r_buffer_2, buffer);

  __ subs(r_index_1, r_index_1, wordSize);
  __ b(runtime, lt);

  __ str(r_index_1, queue_index);
  __ str(r_pre_val_0, Address(r_buffer_2, r_index_1));

  __ bind(done);

  __ pop(saved_regs);

  __ ret();

  __ bind(runtime);

  __ save_live_registers();

  assert(r_pre_val_0 == c_rarg0, "pre_val should be in R0");
  __ mov(c_rarg1, Rthread);
  __ call_VM_leaf(CAST_FROM_FN_PTR(address, ShenandoahRuntime::write_ref_field_pre_entry), c_rarg0, c_rarg1);

  __ restore_live_registers_without_return();

  __ b(done);
}

void ShenandoahBarrierSetAssembler::generate_c1_load_reference_barrier_runtime_stub(StubAssembler* sasm, DecoratorSet decorators) {
  // Input:
  // - arg0 at Address(SP, 0): object to be resolved
  // - arg1 at Address(SP, wordSize): load address
  // These are in the reserved argument area, stored by gen_load_reference_barrier_stub.

  __ set_info("shenandoah_load_reference_barrier_slow_id", false);

  bool is_strong  = ShenandoahBarrierSet::is_strong_access(decorators);
  bool is_weak    = ShenandoahBarrierSet::is_weak_access(decorators);
  bool is_phantom = ShenandoahBarrierSet::is_phantom_access(decorators);
  bool is_native  = ShenandoahBarrierSet::is_native_access(decorators);

  // Save ALL caller-saved registers (GPRs + VFP D0-D7) using the same helper
  // as the other barrier paths.  push_call_clobbered_registers() pushes VFP
  // first, then GPRs, so the GPR block sits at the top of the saved area.
  //
  // Stack layout after push_call_clobbered_registers():
  //   SP+0               : R0  (lowest GPR encoding)
  //   SP+4               : R1
  //   SP+8               : R2
  //   SP+12              : R3
  //   SP+16[+4 if R9]    : R12
  //   SP+20[+4 if R9]    : LR
  //   [SP+24 if R9]      : R9  (when R9_IS_SCRATCHED)
  //   below              : D0-D7  (64 bytes when VFP present)
  //   original SP        : param[0] (obj), param[1] (addr)  -- stored by gen stub
  //
  // param_offset = total bytes pushed = GPR count * 4 + VFP bytes
  const RegisterSet gpr_save = RegisterSet(R0, R3) | RegisterSet(R12) | RegisterSet(LR) | R9ifScratched;
  const int vfp_save_bytes = VM_Version::has_vfp() ? 8 * 8 : 0;
  const int param_offset = gpr_save.size() * wordSize + vfp_save_bytes;

  __ push_call_clobbered_registers();
  __ ldr(R0, Address(SP, param_offset));              // obj
  __ ldr(R1, Address(SP, param_offset + wordSize));   // addr

  // No UseCompressedOops on ARM32 - always use wide variants.
  if (is_strong) {
    __ call(CAST_FROM_FN_PTR(address, ShenandoahRuntime::load_reference_barrier_strong));
  } else if (is_weak) {
    __ call(CAST_FROM_FN_PTR(address, ShenandoahRuntime::load_reference_barrier_weak));
  } else {
    assert(is_phantom, "only remaining strength");
    __ call(CAST_FROM_FN_PTR(address, ShenandoahRuntime::load_reference_barrier_phantom));
  }

  // Store result to the parameter area (first param slot, at original SP).
  __ str(R0, Address(SP, param_offset));

  // Restore all caller-saved registers (including VFP).  After this SP == original SP.
  __ pop_call_clobbered_registers();

  // Load the barrier result from the parameter area (now at SP + 0).
  __ ldr(R0, Address(SP, 0));

  __ ret();
}

#undef __

#endif // COMPILER1
