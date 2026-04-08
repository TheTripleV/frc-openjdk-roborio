/*
 * Copyright (c) 2021, Red Hat, Inc. All rights reserved.
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

#ifndef SHARE_GC_SHENANDOAH_SHENANDOAHGC_HPP
#define SHARE_GC_SHENANDOAH_SHENANDOAHGC_HPP

#include "memory/allocation.hpp"
#include "gc/shared/gcCause.hpp"

/*
 * Base class of three Shenandoah GC modes
 *
 * The relationship of the GCs:
 *
 * ("normal" mode) ----> Concurrent GC ----> (finish)
 *                            |
 *                            | <upgrade>
 *                            v
 * ("passive" mode) ---> Degenerated GC ---> (finish)
 *                            |
 *                            | <upgrade>
 *                            v
 *                         Full GC --------> (finish)
 */

class ShenandoahGC : public StackObj {
public:
  // Fail point from concurrent GC
  enum ShenandoahDegenPoint {
    _degenerated_unset,
    _degenerated_outside_cycle,
    _degenerated_mark,
    _degenerated_evac,
    _degenerated_updaterefs,
    _DEGENERATED_LIMIT
  };

  virtual bool collect(GCCause::Cause cause) = 0;
  static const char* degen_point_to_string(ShenandoahDegenPoint point);

protected:
  // Update roots for full-GC or degenerated-GC (asserts gc mode).
  static void update_roots(bool full_gc);
  // Update roots for any GC phase, using a phase identifier passed as int.
  // This overload is intended for concurrent GC during final-update-refs.
  // Caller must cast ShenandoahPhaseTimings::Phase to int at the call site
  // to avoid including shenandoahPhaseTimings.hpp (which drags in JFR events)
  // into every file that includes shenandoahGC.hpp.
  static void update_roots(int phase_as_int, bool check_alive);
};

#endif  // SHARE_GC_SHENANDOAH_SHENANDOAHGC_HPP
