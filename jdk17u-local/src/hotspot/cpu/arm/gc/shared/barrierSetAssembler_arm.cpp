/*
 * Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
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
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "gc/shared/barrierSetNMethod.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "classfile/classLoaderData.hpp"
#include "memory/universe.hpp"
#include "oops/instanceKlass.hpp"
#include "runtime/jniHandles.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/thread.hpp"

#define __ masm->

void BarrierSetAssembler::try_resolve_jobject_in_native(MacroAssembler* masm, Register jni_env,
                                                        Register obj, Register tmp, Label& slowpath) {
  // Resolve jobject: strip the weak tag and load
  STATIC_ASSERT(JNIHandles::weak_tag_mask == 1);
  __ bic(obj, obj, JNIHandles::weak_tag_mask);
  __ ldr(obj, Address(obj, 0));             // *obj
}

void BarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                  Register dst, Address src, Register tmp1, Register tmp2, Register tmp3) {
  bool in_heap = (decorators & IN_HEAP) != 0;
  bool in_native = (decorators & IN_NATIVE) != 0;
  switch (type) {
  case T_OBJECT:
  case T_ARRAY: {
    if (in_heap) {
      {
        __ ldr(dst, src);
      }
    } else {
      assert(in_native, "why else?");
      __ ldr(dst, src);
    }
    break;
  }
  case T_BOOLEAN: __ ldrb      (dst, src); break;
  case T_BYTE:    __ ldrsb     (dst, src); break;
  case T_CHAR:    __ ldrh      (dst, src); break;
  case T_SHORT:   __ ldrsh     (dst, src); break;
  case T_INT:     __ ldr_s32   (dst, src); break;
  case T_ADDRESS: __ ldr       (dst, src); break;
  case T_LONG:
    assert(dst == noreg, "only to ltos");
    __ add                     (src.index(), src.index(), src.base());
    __ ldmia                   (src.index(), RegisterSet(R0_tos_lo) | RegisterSet(R1_tos_hi));
    break;
#ifdef __SOFTFP__
  case T_FLOAT:
    assert(dst == noreg, "only to ftos");
    __ ldr                     (R0_tos, src);
    break;
  case T_DOUBLE:
    assert(dst == noreg, "only to dtos");
    __ add                     (src.index(), src.index(), src.base());
    __ ldmia                   (src.index(), RegisterSet(R0_tos_lo) | RegisterSet(R1_tos_hi));
    break;
#else
  case T_FLOAT:
    assert(dst == noreg, "only to ftos");
    __ add(src.index(), src.index(), src.base());
    __ ldr_float               (S0_tos, src.index());
    break;
  case T_DOUBLE:
    assert(dst == noreg, "only to dtos");
    __ add                     (src.index(), src.index(), src.base());
    __ ldr_double              (D0_tos, src.index());
    break;
#endif
  default: Unimplemented();
  }

}

void BarrierSetAssembler::store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                   Address obj, Register val, Register tmp1, Register tmp2, Register tmp3, bool is_null) {
  bool in_heap = (decorators & IN_HEAP) != 0;
  bool in_native = (decorators & IN_NATIVE) != 0;
  switch (type) {
  case T_OBJECT:
  case T_ARRAY: {
    if (in_heap) {
      {
      __ str(val, obj);
      }
    } else {
      assert(in_native, "why else?");
      __ str(val, obj);
    }
    break;
  }
  case T_BOOLEAN:
    __ and_32(val, val, 1);
    __ strb(val, obj);
    break;
  case T_BYTE:    __ strb      (val, obj); break;
  case T_CHAR:    __ strh      (val, obj); break;
  case T_SHORT:   __ strh      (val, obj); break;
  case T_INT:     __ str       (val, obj); break;
  case T_ADDRESS: __ str       (val, obj); break;
  case T_LONG:
    assert(val == noreg, "only tos");
    __ add                     (obj.index(), obj.index(), obj.base());
    __ stmia                   (obj.index(), RegisterSet(R0_tos_lo) | RegisterSet(R1_tos_hi));
    break;
#ifdef __SOFTFP__
  case T_FLOAT:
    assert(val == noreg, "only tos");
    __ str (R0_tos,  obj);
    break;
  case T_DOUBLE:
    assert(val == noreg, "only tos");
    __ add                     (obj.index(), obj.index(), obj.base());
    __ stmia                   (obj.index(), RegisterSet(R0_tos_lo) | RegisterSet(R1_tos_hi));
    break;
#else
  case T_FLOAT:
    assert(val == noreg, "only tos");
    __ add                     (obj.index(), obj.index(), obj.base());
    __ str_float               (S0_tos,  obj.index());
    break;
  case T_DOUBLE:
    assert(val == noreg, "only tos");
    __ add                     (obj.index(), obj.index(), obj.base());
    __ str_double              (D0_tos,  obj.index());
    break;
#endif
  default: Unimplemented();
  }
}

// Puts address of allocated object into register `obj` and end of allocated object into register `obj_end`.
void BarrierSetAssembler::eden_allocate(MacroAssembler* masm, Register obj, Register obj_end, Register tmp1, Register tmp2,
                                 RegisterOrConstant size_expression, Label& slow_case) {
  if (!Universe::heap()->supports_inline_contig_alloc()) {
    __ b(slow_case);
    return;
  }

  CollectedHeap* ch = Universe::heap();

  const Register top_addr = tmp1;
  const Register heap_end = tmp2;

  if (size_expression.is_register()) {
    assert_different_registers(obj, obj_end, top_addr, heap_end, size_expression.as_register());
  } else {
    assert_different_registers(obj, obj_end, top_addr, heap_end);
  }

  bool load_const = VM_Version::supports_movw();
  if (load_const) {
    __ mov_address(top_addr, (address)Universe::heap()->top_addr());
  } else {
    __ ldr(top_addr, Address(Rthread, JavaThread::heap_top_addr_offset()));
  }
  // Calculate new heap_top by adding the size of the object
  Label retry;
  __ bind(retry);
  __ ldr(obj, Address(top_addr));
  __ ldr(heap_end, Address(top_addr, (intptr_t)ch->end_addr() - (intptr_t)ch->top_addr()));
  __ add_rc(obj_end, obj, size_expression);
  // Check if obj_end wrapped around, i.e., obj_end < obj. If yes, jump to the slow case.
  __ cmp(obj_end, obj);
  __ b(slow_case, lo);
  // Update heap_top if allocation succeeded
  __ cmp(obj_end, heap_end);
  __ b(slow_case, hi);

  __ atomic_cas_bool(obj, obj_end, top_addr, 0, heap_end/*scratched*/);
  __ b(retry, ne);

  incr_allocated_bytes(masm, size_expression, tmp1);
}

// Puts address of allocated object into register `obj` and end of allocated object into register `obj_end`.
void BarrierSetAssembler::tlab_allocate(MacroAssembler* masm, Register obj, Register obj_end, Register tmp1,
                                 RegisterOrConstant size_expression, Label& slow_case) {
  const Register tlab_end = tmp1;
  assert_different_registers(obj, obj_end, tlab_end);

  __ ldr(obj, Address(Rthread, JavaThread::tlab_top_offset()));
  __ ldr(tlab_end, Address(Rthread, JavaThread::tlab_end_offset()));
  __ add_rc(obj_end, obj, size_expression);
  __ cmp(obj_end, tlab_end);
  __ b(slow_case, hi);
  __ str(obj_end, Address(Rthread, JavaThread::tlab_top_offset()));
}

void BarrierSetAssembler::incr_allocated_bytes(MacroAssembler* masm, RegisterOrConstant size_in_bytes, Register tmp) {
  // Bump total bytes allocated by this thread
  Label done;

  // Borrow the Rthread for alloc counter
  Register Ralloc = Rthread;
  __ add(Ralloc, Ralloc, in_bytes(JavaThread::allocated_bytes_offset()));
  __ ldr(tmp, Address(Ralloc));
  __ adds(tmp, tmp, size_in_bytes);
  __ str(tmp, Address(Ralloc), cc);
  __ b(done, cc);

  // Increment the high word and store single-copy atomically (that is an unlikely scenario on typical embedded systems as it means >4GB has been allocated)
  // To do so ldrd/strd instructions used which require an even-odd pair of registers. Such a request could be difficult to satisfy by
  // allocating those registers on a higher level, therefore the routine is ready to allocate a pair itself.
  Register low, high;
  // Select ether R0/R1 or R2/R3

  if (size_in_bytes.is_register() && (size_in_bytes.as_register() == R0 || size_in_bytes.as_register() == R1)) {
    low = R2;
    high  = R3;
  } else {
    low = R0;
    high  = R1;
  }
  __ push(RegisterSet(low, high));

  __ ldrd(low, Address(Ralloc));
  __ adds(low, low, size_in_bytes);
  __ adc(high, high, 0);
  __ strd(low, Address(Ralloc));

  __ pop(RegisterSet(low, high));

  __ bind(done);

  // Unborrow the Rthread
  __ sub(Rthread, Ralloc, in_bytes(JavaThread::allocated_bytes_offset()));
}

void BarrierSetAssembler::nmethod_entry_barrier(MacroAssembler* masm) {
  BarrierSetNMethod* bs_nm = BarrierSet::barrier_set()->barrier_set_nmethod();

  if (bs_nm == NULL) {
    return;
  }

  Label skip;
  InlinedAddress guard_literal((address)0);
  Address thread_disarmed_addr(Rthread, in_bytes(bs_nm->thread_disarmed_offset()));

  // Load guard value (PC-relative literal).
  // ldr_literal generates: ldr Rtemp, [PC, #offset_to_guard]
  // The literal is bound below (bind_literal) and patched by the assembler.
  __ ldr_literal(Rtemp, guard_literal);

  // Subsequent loads of oops must occur after load of guard value.
  // BarrierSetNMethod::disarm sets guard with release semantics.
  // On ARMv7 this emits a single dmb instruction (does NOT clobber Rtemp).
  __ membar(MacroAssembler::Membar_mask_bits(MacroAssembler::LoadLoad), Rtemp);

  // Load thread's disarmed value and compare with guard
  __ ldr(LR, thread_disarmed_addr);
  __ cmp(Rtemp, LR);
  __ b(skip, eq);

  // Slow path: call the method entry barrier stub.
  // LR has been saved to the stack by the frame setup code (raw_push(FP, LR)),
  // so we can clobber it here with the blx.
  // Emit exactly movw+movt (2 instructions) for deterministic barrier size,
  // even if the stub address fits in 16 bits (mov_address may skip movt).
  address stub = StubRoutines::Arm::method_entry_barrier();
  int stub_addr = (int)(intptr_t)stub;
  __ movw(Rtemp, stub_addr & 0xffff);
  __ movt(Rtemp, ((unsigned int)stub_addr >> 16) & 0xffff);
  __ blx(Rtemp);
  __ b(skip);

  __ bind_literal(guard_literal);  // emits guard data word inline

  __ bind(skip);
}

void BarrierSetAssembler::c2i_entry_barrier(MacroAssembler* masm) {
  BarrierSetNMethod* bs = BarrierSet::barrier_set()->barrier_set_nmethod();
  if (bs == NULL) {
    return;
  }

  Label bad_call;
  __ cbz(Rmethod, bad_call);

  // Pointer chase to the method holder's ClassLoaderData to check if the method is
  // concurrently unloading.
  Label method_live;

  // load_method_holder_cld: method → ConstMethod → ConstantPool → InstanceKlass → CLD
  __ load_method_holder_cld(Rtemp, Rmethod);

  // Is it a strong CLD?
  __ ldr(LR, Address(Rtemp, ClassLoaderData::keep_alive_offset()));
  __ cbnz(LR, method_live);

  // Is it a weak but alive CLD?
  // Load the holder (an OopHandle)
  __ ldr(Rtemp, Address(Rtemp, ClassLoaderData::holder_offset()));

  // Resolve weak handle: NULL or cleared weak → method is dead
  __ cbz(Rtemp, bad_call);

  // WeakHandle::resolve is an indirection like jweak. Use access_load_at to go
  // through the GC load barrier (needed for Shenandoah LRB + phantom ref).
  // loads *Rtemp into Rtemp, applying IN_NATIVE | ON_PHANTOM_OOP_REF barriers.
  __ access_load_at(T_OBJECT, IN_NATIVE | ON_PHANTOM_OOP_REF,
                    Address(Rtemp), Rtemp, LR, noreg, noreg);
  __ cbnz(Rtemp, method_live);

  __ bind(bad_call);
  __ jump(SharedRuntime::get_handle_wrong_method_stub(), relocInfo::runtime_call_type, Rtemp);

  __ bind(method_live);
}
