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

#ifndef CPU_ARM_GC_SHENANDOAH_SHENANDOAHBARRIERSETASSEMBLER_ARM_HPP
#define CPU_ARM_GC_SHENANDOAH_SHENANDOAHBARRIERSETASSEMBLER_ARM_HPP

#include "asm/macroAssembler.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "gc/shenandoah/shenandoahBarrierSet.hpp"

#ifdef COMPILER1
class LIR_Assembler;
class ShenandoahPreBarrierStub;
class ShenandoahLoadReferenceBarrierStub;
class StubAssembler;
#endif

class ShenandoahBarrierSetAssembler: public BarrierSetAssembler {
private:

  void satb_write_barrier_pre(MacroAssembler* masm,
                              Register store_addr,
                              Register new_val,
                              Register pre_val,
                              Register tmp1,
                              Register tmp2);

  void shenandoah_write_barrier_pre(MacroAssembler* masm,
                                    Register store_addr,
                                    Register new_val,
                                    Register pre_val,
                                    Register tmp1,
                                    Register tmp2);

  void resolve_forward_pointer(MacroAssembler* masm, Register dst, Register tmp);
  void resolve_forward_pointer_not_null(MacroAssembler* masm, Register dst, Register tmp);
  void load_reference_barrier(MacroAssembler* masm, Register dst, Address load_addr, DecoratorSet decorators);

public:

  void iu_barrier(MacroAssembler* masm, Register dst, Register tmp);

#ifdef COMPILER1
  void gen_pre_barrier_stub(LIR_Assembler* ce, ShenandoahPreBarrierStub* stub);
  void gen_load_reference_barrier_stub(LIR_Assembler* ce, ShenandoahLoadReferenceBarrierStub* stub);
  void generate_c1_pre_barrier_runtime_stub(StubAssembler* sasm);
  void generate_c1_load_reference_barrier_runtime_stub(StubAssembler* sasm, DecoratorSet decorators);
#endif

  virtual void arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, bool is_oop,
                                  Register addr, Register count, int callee_saved_regs);
  virtual void load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                       Register dst, Address src, Register tmp1, Register tmp2, Register tmp3);
  virtual void store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                        Address obj, Register new_val, Register tmp1, Register tmp2, Register tmp3, bool is_null);

  virtual void try_resolve_jobject_in_native(MacroAssembler* masm, Register jni_env,
                                             Register obj, Register tmp, Label& slowpath);

  void cmpxchg_oop(MacroAssembler* masm, Register addr, Register expected, Register new_val,
                   bool is_cae, Register tmp1, Register tmp2, Register tmp3, Register result);
};

#endif // CPU_ARM_GC_SHENANDOAH_SHENANDOAHBARRIERSETASSEMBLER_ARM_HPP
