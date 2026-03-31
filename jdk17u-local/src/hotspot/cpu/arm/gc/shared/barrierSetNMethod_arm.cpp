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
#include "code/codeCache.hpp"
#include "code/nativeInst.hpp"
#include "gc/shared/barrierSetNMethod.hpp"
#include "logging/log.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/registerMap.hpp"
#include "runtime/thread.hpp"
#include "utilities/align.hpp"
#include "utilities/debug.hpp"

// The nmethod entry barrier for ARM32 consists of the following instruction
// sequence emitted by BarrierSetAssembler::nmethod_entry_barrier():
//
//   ldr   Rtemp, [PC, #28]    ; PC-relative load of guard value
//   dmb                       ; memory barrier (LoadLoad)
//   ldr   LR, [Rthread, #off] ; load thread disarmed value
//   cmp   Rtemp, LR           ; compare guard with disarmed
//   beq   skip                ; skip to normal path if equal
//   movw  Rtemp, #lo          ; load stub address (low 16 bits)
//   movt  Rtemp, #hi          ; load stub address (high 16 bits)
//   blx   Rtemp               ; call barrier stub
//   b     skip                ; branch to normal path
//   .word 0                   ; guard data (at offset 9*4 = 36)
//   skip:                     ; frame_complete
//
// Total: 9 instructions + 1 data word = 10 words = 40 bytes.

class NativeNMethodBarrier: public NativeInstruction {
  address instruction_address() const { return addr_at(0); }

  int *guard_addr() {
    // Guard data is at offset 9 * 4 = 36 from barrier start.
    return reinterpret_cast<int*>(instruction_address() + 9 * 4);
  }

public:
  int get_value() {
    return Atomic::load_acquire(guard_addr());
  }

  void set_value(int value) {
    Atomic::release_store(guard_addr(), value);
  }

  void verify() const {
    // Verify the first instruction is an LDR from PC (PC-relative literal load).
    // ARM encoding: cond 01 I P U B W L Rn Rd offset
    // For ldr Rd, [PC, #imm]: Rn=1111(PC), L=1, I=0, B=0
    // Mask: check bits [27:26]=01, [20]=1(L), [19:16]=1111(PC)
    uint32_t insn = *(uint32_t*)instruction_address();
    uint32_t masked = insn & 0x0F7F0000;
    guarantee(masked == 0x051F0000 || masked == 0x059F0000,
              "nmethod entry barrier: first instruction must be ldr from PC, found 0x%08x", insn);
  }
};

// This is the offset of the entry barrier from where the frame is completed.
// If any code changes between the end of the verified entry where the entry
// barrier resides, and the completion of the frame, then
// NativeNMethodBarrier::verify() will immediately complain when it does
// not find the expected native instruction at this offset, which needs updating.
//
// ARM32 barrier: 9 instructions + 1 data word = 10 words = 40 bytes.
static const int entry_barrier_offset = -4 * 10;

static NativeNMethodBarrier* native_nmethod_barrier(nmethod* nm) {
  address barrier_address = nm->code_begin() + nm->frame_complete_offset() + entry_barrier_offset;
  NativeNMethodBarrier* barrier = reinterpret_cast<NativeNMethodBarrier*>(barrier_address);
  debug_only(barrier->verify());
  return barrier;
}

// Deoptimize the nmethod by tearing down the nmethod's frame and jumping
// to the handle_wrong_method_stub.  This looks like there has been an IC miss
// at the entry of the nmethod, so we resolve the call, which will fall back
// to the interpreter if the nmethod has been unloaded.
//
// The stub has reserved 4 words (16 bytes) of "deopt space" on the stack
// at (return_address_ptr - 5) where we write the sender's frame info.
// On return from the stub, the deopt path loads these values and jumps
// to handle_wrong_method_stub.
void BarrierSetNMethod::deoptimize(nmethod* nm, address* return_address_ptr) {

  typedef struct {
    intptr_t *sp; intptr_t *fp; address lr; address pc;
  } frame_pointers_t;

  // return_address_ptr points to saved LR in the stub's frame.
  // On ARM32 (32-bit pointers): return_address_ptr - 5 is 20 bytes earlier,
  // which is exactly the start of the deopt space reserved by the stub.
  frame_pointers_t *new_frame = (frame_pointers_t *)(return_address_ptr - 5);

  JavaThread *thread = JavaThread::current();
  RegisterMap reg_map(thread, false);
  frame frame = thread->last_frame();

  assert(frame.is_compiled_frame() || frame.is_native_frame(), "must be");
  assert(frame.cb() == nm, "must be");
  frame = frame.sender(&reg_map);

  LogTarget(Trace, nmethod, barrier) out;
  if (out.is_enabled()) {
    ResourceMark mark;
    log_trace(nmethod, barrier)("deoptimize(nmethod: %s(%p), return_addr: %p, osr: %d, thread: %p(%s), making rsp: %p) -> %p",
                                nm->method()->name_and_sig_as_C_string(),
                                nm, *(address *) return_address_ptr, nm->is_osr_method(), thread,
                                thread->get_thread_name(), frame.sp(), nm->verified_entry_point());
  }

  new_frame->sp = frame.sp();
  new_frame->fp = frame.fp();
  new_frame->lr = frame.pc();
  new_frame->pc = SharedRuntime::get_handle_wrong_method_stub();
}

void BarrierSetNMethod::disarm(nmethod* nm) {
  if (!supports_entry_barrier(nm)) {
    return;
  }

  // Disarms the nmethod guard emitted by BarrierSetAssembler::nmethod_entry_barrier.
  // Symmetric "LDR; DMB" is in the nmethod barrier.
  NativeNMethodBarrier* barrier = native_nmethod_barrier(nm);

  barrier->set_value(disarmed_value());
}

bool BarrierSetNMethod::is_armed(nmethod* nm) {
  if (!supports_entry_barrier(nm)) {
    return false;
  }

  NativeNMethodBarrier* barrier = native_nmethod_barrier(nm);
  return barrier->get_value() != disarmed_value();
}
