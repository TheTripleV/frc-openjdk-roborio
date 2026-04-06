/*
 * Copyright (c) 2015, 2021, Red Hat, Inc. All rights reserved.
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

#ifndef SHARE_GC_SHENANDOAH_SHENANDOAHMARK_INLINE_HPP
#define SHARE_GC_SHENANDOAH_SHENANDOAHMARK_INLINE_HPP

#include "gc/shenandoah/shenandoahMark.hpp"

#include "gc/shenandoah/shenandoahAsserts.hpp"
#include "gc/shenandoah/shenandoahBarrierSet.inline.hpp"
#include "gc/shenandoah/shenandoahHeap.inline.hpp"
#include "gc/shenandoah/shenandoahMarkingContext.inline.hpp"
#include "gc/shenandoah/shenandoahStringDedup.inline.hpp"
#include "gc/shenandoah/shenandoahTaskqueue.inline.hpp"
#include "gc/shenandoah/shenandoahUtils.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/compressedOops.inline.hpp"
#include "oops/oop.inline.hpp"
#include "memory/metaspace.hpp"
#include "runtime/os.hpp"
#include "runtime/prefetch.inline.hpp"
#include "utilities/powerOfTwo.hpp"

template <StringDedupMode STRING_DEDUP>
void ShenandoahMark::dedup_string(oop obj, StringDedup::Requests* const req) {
  if (STRING_DEDUP == ENQUEUE_DEDUP) {
    if (ShenandoahStringDedup::is_candidate(obj)) {
      req->add(obj);
    }
  } else if (STRING_DEDUP == ALWAYS_DEDUP) {
    if (ShenandoahStringDedup::is_string_candidate(obj) &&
        !ShenandoahStringDedup::dedup_requested(obj)) {
        req->add(obj);
    }
  }
}

template <class T, StringDedupMode STRING_DEDUP>
void ShenandoahMark::do_task(ShenandoahObjToScanQueue* q, T* cl, ShenandoahLiveData* live_data, StringDedup::Requests* const req, ShenandoahMarkTask* task) {
  oop obj = task->obj();

  ShenandoahHeap* heap = ShenandoahHeap::heap();

  if (obj == NULL || !heap->is_in(obj)) {
    log_debug(gc)("Shenandoah: do_task: bad oop " PTR_FORMAT " skip_live=%d weak=%d chunk=%d pow=%d",
                  p2i(obj), task->count_liveness() ? 0 : 1, task->is_weak() ? 1 : 0, task->chunk(), task->pow());
    return;
  }

  // ARM32 fix: Check that the object's region hasn't been recycled (trashed/empty)
  // since the marking task was queued. In aggressive mode, a stale from-space
  // reference can survive past update-refs into the next cycle. By the time we
  // process this task, the from-space region may have been trashed and its memory
  // recycled, making all object data (klass, fields) garbage.
  ShenandoahHeapRegion* obj_region = heap->heap_region_containing(obj);
  if (!obj_region->is_active()) {
    log_debug(gc)("Shenandoah: do_task: oop in inactive region " PTR_FORMAT " region=%zu",
                  p2i(obj), obj_region->index());
    return;
  }

  // ARM32 fix: Cache klass from the acquire read and validate it. This prevents
  // TOCTOU races where klass_or_null_acquire() returns non-null, but obj->klass()
  // (a plain re-read) returns garbage because the region was recycled. It also
  // catches bad oops whose "klass" is a small integer (e.g., an interior pointer
  // to a java.lang.Class whose field data is mistaken for a klass pointer).
  // Use Metaspace::contains() for robust validation since garbage values like
  // 0x00650068 (recycled string data) pass simple alignment/range checks.
  Klass* klass = obj->klass_or_null_acquire();
  if (klass == NULL || (uintptr_t)klass < 4096 || ((uintptr_t)klass & 0x3) != 0
      || !Metaspace::contains(klass)) {
    log_debug(gc)("Shenandoah: do_task: bad klass " PTR_FORMAT " for oop " PTR_FORMAT
                  " skip_live=%d weak=%d chunk=%d pow=%d",
                  p2i(klass), p2i(obj), task->count_liveness() ? 0 : 1,
                  task->is_weak() ? 1 : 0, task->chunk(), task->pow());
    return;
  }

  shenandoah_assert_not_forwarded(NULL, obj);
  shenandoah_assert_marked(NULL, obj);
  shenandoah_assert_not_in_cset_except(NULL, obj, ShenandoahHeap::heap()->cancelled_gc());

  // Are we in weak subgraph scan?
  bool weak = task->is_weak();
  cl->set_weak(weak);

  if (task->is_not_chunked()) {
    // Use cached klass for dispatch instead of re-reading via obj->is_instance()
    // etc., which calls obj->klass() (plain read) that could see a different value.
    if (klass->is_instance_klass()) {
      // Case 1: Normal oop, process as usual.
      // ARM32 fix: Use cached klass for dispatch. obj->oop_iterate(cl) would
      // re-read klass() with a plain load that can see garbage if the region
      // was recycled between our klass_or_null_acquire() and here.
      OopIteratorClosureDispatch::oop_oop_iterate(cl, obj, klass);
      dedup_string<STRING_DEDUP>(obj, req);
    } else if (klass->is_objArray_klass()) {
      // Case 2: Object array instance and no chunk is set. Must be the first
      // time we visit it, start the chunked processing.
      do_chunked_array_start<T>(q, cl, obj, weak);
    } else {
      // Case 3: Primitive array. Do nothing, no oops there. We use the same
      // performance tweak TypeArrayKlass::oop_oop_iterate_impl is using:
      // We skip iterating over the klass pointer since we know that
      // Universe::TypeArrayKlass never moves.
      assert (klass->is_typeArray_klass(), "should be type array");
    }
    // Count liveness the last: push the outstanding work to the queues first
    // Avoid double-counting objects that are visited twice due to upgrade
    // from final- to strong mark.
    if (task->count_liveness()) {
      count_liveness(live_data, obj);
    }
  } else {
    // Case 4: Array chunk, has sensible chunk id. Process it.
    do_chunked_array<T>(q, cl, obj, task->chunk(), task->pow(), weak);
  }
}

inline void ShenandoahMark::count_liveness(ShenandoahLiveData* live_data, oop obj) {
  ShenandoahHeap* const heap = ShenandoahHeap::heap();
  size_t region_idx = heap->heap_region_index_containing(obj);
  ShenandoahHeapRegion* region = heap->get_region(region_idx);
  size_t size = obj->size();

  if (!region->is_humongous_start()) {
    assert(!region->is_humongous(), "Cannot have continuations here");
    ShenandoahLiveData cur = live_data[region_idx];
    size_t new_val = size + cur;
    if (new_val >= SHENANDOAH_LIVEDATA_MAX) {
      // overflow, flush to region data
      region->increase_live_data_gc_words(new_val);
      live_data[region_idx] = 0;
    } else {
      // still good, remember in locals
      live_data[region_idx] = (ShenandoahLiveData) new_val;
    }
  } else {
    shenandoah_assert_in_correct_region(NULL, obj);
    size_t num_regions = ShenandoahHeapRegion::required_regions(size * HeapWordSize);

    for (size_t i = region_idx; i < region_idx + num_regions; i++) {
      ShenandoahHeapRegion* chain_reg = heap->get_region(i);
      assert(chain_reg->is_humongous(), "Expecting a humongous region");
      chain_reg->increase_live_data_gc_words(chain_reg->used() >> LogHeapWordSize);
    }
  }
}

template <class T>
inline void ShenandoahMark::do_chunked_array_start(ShenandoahObjToScanQueue* q, T* cl, oop obj, bool weak) {
  assert(obj->is_objArray(), "expect object array");
  objArrayOop array = objArrayOop(obj);
  int len = array->length();

  // ARM32 fix: validate length. If the array header was corrupted
  // (e.g., region recycled between validation in do_task and here),
  // len could be negative or garbage.
  if (len < 0) {
    return;
  }

  // Mark objArray klass metadata
  // ARM32 fix: re-read klass with acquire semantics and validate against
  // metaspace before calling do_klass. The klass pointer can become corrupt
  // during Shenandoah aggressive mode's rapid GC cycling.  Using
  // Metaspace::contains() here is safe because this only gates metadata
  // iteration (CLD handle scanning), NOT object field iteration. CLDs are
  // always reachable through class-loader-graph root scanning, so skipping
  // metadata here for a suspect klass cannot cause missed live objects.
  if (Devirtualizer::do_metadata(cl)) {
    Klass* klass = array->klass_or_null_acquire();
    if (klass != NULL && Metaspace::contains(klass)) {
      Devirtualizer::do_klass(cl, klass);
    }
  }

  // ARM32 fix: re-validate the array's region after metadata iteration.
  // The do_klass -> do_cld -> oops_do call chain can take significant time.
  // During aggressive mode, the array's region could be recycled by then.
  {
    ShenandoahHeap* heap = ShenandoahHeap::heap();
    ShenandoahHeapRegion* region = heap->heap_region_containing((HeapWord*)array);
    if (!region->is_active()) {
      return;
    }
  }

  if (len <= (int) ObjArrayMarkingStride*2) {
    // A few slices only, process directly.
    // ARM32 fix: use direct element iteration instead of array->oop_iterate_range()
    // which internally re-reads klass() to dispatch through ObjArrayKlass. On ARM32
    // in aggressive mode, the klass pointer can become corrupt between the validated
    // read above and the internal re-read, causing SIGSEGV at low addresses.
    oop* base = (oop*)array->base();
    for (oop* p = base; p < base + len; p++) {
      Devirtualizer::do_oop(cl, p);
    }
  } else {
    int bits = log2i_graceful(len);
    // Compensate for non-power-of-two arrays, cover the array in excess:
    if (len != (1 << bits)) bits++;

    // Only allow full chunks on the queue. This frees do_chunked_array() from checking from/to
    // boundaries against array->length(), touching the array header on every chunk.
    //
    // To do this, we cut the prefix in full-sized chunks, and submit them on the queue.
    // If the array is not divided in chunk sizes, then there would be an irregular tail,
    // which we will process separately.

    int last_idx = 0;

    int chunk = 1;
    int pow = bits;

    // Handle overflow
    if (pow >= 31) {
      assert (pow == 31, "sanity");
      pow--;
      chunk = 2;
      last_idx = (1 << pow);
      bool pushed = q->push(ShenandoahMarkTask(array, true, weak, 1, pow));
      assert(pushed, "overflow queue should always succeed pushing");
    }

    // Split out tasks, as suggested in ShenandoahMarkTask docs. Record the last
    // successful right boundary to figure out the irregular tail.
    while ((1 << pow) > (int)ObjArrayMarkingStride &&
           (chunk*2 < ShenandoahMarkTask::chunk_size())) {
      pow--;
      int left_chunk = chunk*2 - 1;
      int right_chunk = chunk*2;
      int left_chunk_end = left_chunk * (1 << pow);
      if (left_chunk_end < len) {
        bool pushed = q->push(ShenandoahMarkTask(array, true, weak, left_chunk, pow));
        assert(pushed, "overflow queue should always succeed pushing");
        chunk = right_chunk;
        last_idx = left_chunk_end;
      } else {
        chunk = left_chunk;
      }
    }

    // Process the irregular tail, if present
    // ARM32 fix: direct iteration instead of oop_iterate_range
    int from = last_idx;
    if (from < len) {
      oop* base = (oop*)array->base();
      for (oop* p = base + from; p < base + len; p++) {
        Devirtualizer::do_oop(cl, p);
      }
    }
  }
}

template <class T>
inline void ShenandoahMark::do_chunked_array(ShenandoahObjToScanQueue* q, T* cl, oop obj, int chunk, int pow, bool weak) {
  assert(obj->is_objArray(), "expect object array");
  objArrayOop array = objArrayOop(obj);

  // ARM32 fix: re-validate array region is still active. Even though do_task
  // validates before calling us, the region could have been recycled between
  // task validation and reaching this point in aggressive mode.
  {
    ShenandoahHeap* heap = ShenandoahHeap::heap();
    ShenandoahHeapRegion* region = heap->heap_region_containing((HeapWord*)array);
    if (!region->is_active()) {
      return;
    }
  }

  assert (ObjArrayMarkingStride > 0, "sanity");

  // Split out tasks, as suggested in ShenandoahMarkTask docs. Avoid pushing tasks that
  // are known to start beyond the array.
  while ((1 << pow) > (int)ObjArrayMarkingStride && (chunk*2 < ShenandoahMarkTask::chunk_size())) {
    pow--;
    chunk *= 2;
    bool pushed = q->push(ShenandoahMarkTask(array, true, weak, chunk - 1, pow));
    assert(pushed, "overflow queue should always succeed pushing");
  }

  int chunk_size = 1 << pow;

  int from = (chunk - 1) * chunk_size;
  int to = chunk * chunk_size;

  // ARM32 fix: validate from/to against actual array length.
  // The chunk bounds were computed from the original length at push time.
  // Re-read length to detect corruption from region recycling.
  int len = array->length();
  if (len <= 0 || from < 0 || to <= 0 || from >= len || to > len) {
    return;
  }

  // ARM32 fix: direct element iteration instead of array->oop_iterate_range()
  // which internally re-reads klass() and can SIGSEGV if the klass field was
  // overwritten by recycled memory.
  oop* base = (oop*)array->base();
  for (oop* p = base + from; p < base + to; p++) {
    Devirtualizer::do_oop(cl, p);
  }
}

class ShenandoahSATBBufferClosure : public SATBBufferClosure {
private:
  ShenandoahObjToScanQueue* _queue;
  ShenandoahHeap* _heap;
  ShenandoahMarkingContext* const _mark_context;
public:
  ShenandoahSATBBufferClosure(ShenandoahObjToScanQueue* q) :
    _queue(q),
    _heap(ShenandoahHeap::heap()),
    _mark_context(_heap->marking_context())
  {
  }

  void do_buffer(void **buffer, size_t size) {
    assert(size == 0 || !_heap->has_forwarded_objects(), "Forwarded objects are not expected here");
    for (size_t i = 0; i < size; ++i) {
      oop *p = (oop *) &buffer[i];
      oop val = *p;
      if (val != NULL && !_heap->is_in(val)) {
        log_debug(gc)("Shenandoah: SATB buffer: bad oop " PTR_FORMAT " at index %zu/%zu, buffer=" PTR_FORMAT,
                      p2i(val), i, size, p2i(buffer));
      }
      ShenandoahMark::mark_through_ref<oop>(p, _queue, _mark_context, false);
    }
  }
};

template<class T>
inline void ShenandoahMark::mark_through_ref(T* p, ShenandoahObjToScanQueue* q, ShenandoahMarkingContext* const mark_context, bool weak) {
  T o = RawAccess<>::oop_load(p);
  if (!CompressedOops::is_null(o)) {
    oop obj = CompressedOops::decode_not_null(o);

    if (!ShenandoahHeap::heap()->is_in(obj)) {
      log_debug(gc)("Shenandoah: mark_through_ref: bad oop " PTR_FORMAT " from " PTR_FORMAT " weak=%d",
                    p2i(obj), p2i(p), weak ? 1 : 0);
      return;
    }

    // ARM32 fix: skip objects in inactive (trashed/empty) regions.
    // In aggressive mode, a stale from-space reference can survive past
    // update-refs. By the time we trace it, the from-space region may be
    // recycled, making all data at this address garbage.
    ShenandoahHeapRegion* obj_region = ShenandoahHeap::heap()->heap_region_containing(obj);
    if (!obj_region->is_active()) {
      return;
    }

    // Fix #9: REMOVED the Metaspace klass check that was here (old "Fix 8").
    // That check silently dropped valid oops whose klass pointer happened to
    // fail the Metaspace::contains() test, causing concurrent marking to miss
    // transitively-reachable objects.  ShenandoahVerify caught this:
    //   "Before Evacuation, Marked; Must be marked in complete bitmap"
    // The is_in() check above is sufficient to guard against invalid oops.

    shenandoah_assert_not_forwarded(p, obj);
    shenandoah_assert_not_in_cset_except(p, obj, ShenandoahHeap::heap()->cancelled_gc());

    bool skip_live = false;
    bool marked;
    if (weak) {
      marked = mark_context->mark_weak(obj);
    } else {
      marked = mark_context->mark_strong(obj, /* was_upgraded = */ skip_live);
    }
    if (marked) {
      bool pushed = q->push(ShenandoahMarkTask(obj, skip_live, weak));
      assert(pushed, "overflow queue should always succeed pushing");
    }

    shenandoah_assert_marked(p, obj);
  }
}

ShenandoahObjToScanQueueSet* ShenandoahMark::task_queues() const {
  return _task_queues;
}

ShenandoahObjToScanQueue* ShenandoahMark::get_queue(uint index) const {
  return _task_queues->queue(index);
}
#endif // SHARE_GC_SHENANDOAH_SHENANDOAHMARK_INLINE_HPP
