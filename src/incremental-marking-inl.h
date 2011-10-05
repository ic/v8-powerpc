// Copyright 2011 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_INCREMENTAL_MARKING_INL_H_
#define V8_INCREMENTAL_MARKING_INL_H_

#include "incremental-marking.h"

namespace v8 {
namespace internal {


bool IncrementalMarking::BaseRecordWrite(HeapObject* obj,
                                         Object** slot,
                                         Object* value) {
  if (IsMarking() && value->IsHeapObject()) {
    MarkBit value_bit = Marking::MarkBitFrom(HeapObject::cast(value));
    if (Marking::IsWhite(value_bit)) {
      MarkBit obj_bit = Marking::MarkBitFrom(obj);
      if (Marking::IsBlack(obj_bit)) {
        BlackToGreyAndUnshift(obj, obj_bit);
        RestartIfNotMarking();
      }

      // Object is either grey or white it will be scanned if survives.
      return false;
    }
    return true;
  }
  return false;
}


void IncrementalMarking::RecordWrite(HeapObject* obj,
                                     Object** slot,
                                     Object* value) {
  if (BaseRecordWrite(obj, slot, value) && is_compacting_ && slot != NULL) {
    MarkBit obj_bit = Marking::MarkBitFrom(obj);
    if (Marking::IsBlack(obj_bit)) {
      // Object is not going to be rescanned we need to record the slot.
      heap_->mark_compact_collector()->RecordSlot(
          HeapObject::RawField(obj, 0), slot, value);
    }
  }
}


void IncrementalMarking::RecordWriteIntoCode(HeapObject* obj,
                                             RelocInfo* rinfo,
                                             Object* value) {
  if (IsMarking() && value->IsHeapObject()) {
    MarkBit value_bit = Marking::MarkBitFrom(HeapObject::cast(value));
    if (Marking::IsWhite(value_bit)) {
      MarkBit obj_bit = Marking::MarkBitFrom(obj);
      if (Marking::IsBlack(obj_bit)) {
        BlackToGreyAndUnshift(obj, obj_bit);
        RestartIfNotMarking();
      }

      // Object is either grey or white it will be scanned if survives.
      return;
    }

    if (is_compacting_) {
      MarkBit obj_bit = Marking::MarkBitFrom(obj);
      if (Marking::IsBlack(obj_bit)) {
        // Object is not going to be rescanned we need to record the slot.
        heap_->mark_compact_collector()->RecordRelocSlot(rinfo,
                                                         Code::cast(value));
      }
    }
  }
}


void IncrementalMarking::RecordWrites(HeapObject* obj) {
  if (IsMarking()) {
    MarkBit obj_bit = Marking::MarkBitFrom(obj);
    if (Marking::IsBlack(obj_bit)) {
      BlackToGreyAndUnshift(obj, obj_bit);
      RestartIfNotMarking();
    }
  }
}


void IncrementalMarking::BlackToGreyAndUnshift(HeapObject* obj,
                                               MarkBit mark_bit) {
  ASSERT(Marking::MarkBitFrom(obj) == mark_bit);
  ASSERT(obj->Size() >= 2*kPointerSize);
  ASSERT(IsMarking());
  Marking::BlackToGrey(mark_bit);
  int obj_size = obj->Size();
  MemoryChunk::IncrementLiveBytes(obj->address(), -obj_size);
  int64_t old_bytes_rescanned = bytes_rescanned_;
  bytes_rescanned_ = old_bytes_rescanned + obj_size;
  if ((bytes_rescanned_ >> 20) != (old_bytes_rescanned >> 20)) {
    if (bytes_rescanned_ > 2 * heap_->PromotedSpaceSize()) {
      // If we have queued twice the heap size for rescanning then we are
      // going around in circles, scanning the same objects again and again
      // as the program mutates the heap faster than we can incrementally
      // trace it.  In this case we switch to non-incremental marking in
      // order to finish off this marking phase.
      if (FLAG_trace_gc) {
        PrintF("Hurrying incremental marking because of lack of progress\n");
      }
      allocation_marking_factor_ = kMaxAllocationMarkingFactor;
    }
  }

  marking_deque_.UnshiftGrey(obj);
}


void IncrementalMarking::WhiteToGreyAndPush(HeapObject* obj, MarkBit mark_bit) {
  WhiteToGrey(obj, mark_bit);
  marking_deque_.PushGrey(obj);
}


void IncrementalMarking::WhiteToGrey(HeapObject* obj, MarkBit mark_bit) {
  ASSERT(Marking::MarkBitFrom(obj) == mark_bit);
  ASSERT(obj->Size() >= 2*kPointerSize);
  ASSERT(IsMarking());
  Marking::WhiteToGrey(mark_bit);
}


} }  // namespace v8::internal

#endif  // V8_INCREMENTAL_MARKING_INL_H_
