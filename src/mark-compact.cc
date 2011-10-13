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

#include "v8.h"

#include "code-stubs.h"
#include "compilation-cache.h"
#include "deoptimizer.h"
#include "execution.h"
#include "gdb-jit.h"
#include "global-handles.h"
#include "heap-profiler.h"
#include "ic-inl.h"
#include "incremental-marking.h"
#include "liveobjectlist-inl.h"
#include "mark-compact.h"
#include "objects-visiting.h"
#include "objects-visiting-inl.h"
#include "stub-cache.h"

namespace v8 {
namespace internal {


const char* Marking::kWhiteBitPattern = "00";
const char* Marking::kBlackBitPattern = "10";
const char* Marking::kGreyBitPattern = "11";
const char* Marking::kImpossibleBitPattern = "01";


// -------------------------------------------------------------------------
// MarkCompactCollector

MarkCompactCollector::MarkCompactCollector() :  // NOLINT
#ifdef DEBUG
      state_(IDLE),
#endif
      sweep_precisely_(false),
      compacting_(false),
      was_marked_incrementally_(false),
      collect_maps_(FLAG_collect_maps),
      tracer_(NULL),
      migration_slots_buffer_(NULL),
#ifdef DEBUG
      live_young_objects_size_(0),
      live_old_pointer_objects_size_(0),
      live_old_data_objects_size_(0),
      live_code_objects_size_(0),
      live_map_objects_size_(0),
      live_cell_objects_size_(0),
      live_lo_objects_size_(0),
      live_bytes_(0),
#endif
      heap_(NULL),
      code_flusher_(NULL),
      encountered_weak_maps_(NULL) { }


#ifdef DEBUG
class VerifyMarkingVisitor: public ObjectVisitor {
 public:
  void VisitPointers(Object** start, Object** end) {
    for (Object** current = start; current < end; current++) {
      if ((*current)->IsHeapObject()) {
        HeapObject* object = HeapObject::cast(*current);
        ASSERT(HEAP->mark_compact_collector()->IsMarked(object));
      }
    }
  }
};


static void VerifyMarking(Address bottom, Address top) {
  VerifyMarkingVisitor visitor;
  HeapObject* object;
  Address next_object_must_be_here_or_later = bottom;

  for (Address current = bottom;
       current < top;
       current += kPointerSize) {
    object = HeapObject::FromAddress(current);
    if (MarkCompactCollector::IsMarked(object)) {
      ASSERT(current >= next_object_must_be_here_or_later);
      object->Iterate(&visitor);
      next_object_must_be_here_or_later = current + object->Size();
    }
  }
}


static void VerifyMarking(NewSpace* space) {
  Address end = space->top();
  NewSpacePageIterator it(space->bottom(), end);
  // The bottom position is at the start of its page. Allows us to use
  // page->body() as start of range on all pages.
  ASSERT_EQ(space->bottom(),
            NewSpacePage::FromAddress(space->bottom())->body());
  while (it.has_next()) {
    NewSpacePage* page = it.next();
    Address limit = it.has_next() ? page->body_limit() : end;
    ASSERT(limit == end || !page->Contains(end));
    VerifyMarking(page->body(), limit);
  }
}


static void VerifyMarking(PagedSpace* space) {
  PageIterator it(space);

  while (it.has_next()) {
    Page* p = it.next();
    VerifyMarking(p->ObjectAreaStart(), p->ObjectAreaEnd());
  }
}


static void VerifyMarking(Heap* heap) {
  VerifyMarking(heap->old_pointer_space());
  VerifyMarking(heap->old_data_space());
  VerifyMarking(heap->code_space());
  VerifyMarking(heap->cell_space());
  VerifyMarking(heap->map_space());
  VerifyMarking(heap->new_space());

  VerifyMarkingVisitor visitor;

  LargeObjectIterator it(heap->lo_space());
  for (HeapObject* obj = it.Next(); obj != NULL; obj = it.Next()) {
    if (MarkCompactCollector::IsMarked(obj)) {
      obj->Iterate(&visitor);
    }
  }

  heap->IterateStrongRoots(&visitor, VISIT_ONLY_STRONG);
}


class VerifyEvacuationVisitor: public ObjectVisitor {
 public:
  void VisitPointers(Object** start, Object** end) {
    for (Object** current = start; current < end; current++) {
      if ((*current)->IsHeapObject()) {
        HeapObject* object = HeapObject::cast(*current);
        CHECK(!MarkCompactCollector::IsOnEvacuationCandidate(object));
      }
    }
  }
};


static void VerifyEvacuation(Address bottom, Address top) {
  VerifyEvacuationVisitor visitor;
  HeapObject* object;
  Address next_object_must_be_here_or_later = bottom;

  for (Address current = bottom;
       current < top;
       current += kPointerSize) {
    object = HeapObject::FromAddress(current);
    if (MarkCompactCollector::IsMarked(object)) {
      ASSERT(current >= next_object_must_be_here_or_later);
      object->Iterate(&visitor);
      next_object_must_be_here_or_later = current + object->Size();
    }
  }
}


static void VerifyEvacuation(NewSpace* space) {
  NewSpacePageIterator it(space->bottom(), space->top());
  VerifyEvacuationVisitor visitor;

  while (it.has_next()) {
    NewSpacePage* page = it.next();
    Address current = page->body();
    Address limit = it.has_next() ? page->body_limit() : space->top();
    ASSERT(limit == space->top() || !page->Contains(space->top()));
    while (current < limit) {
      HeapObject* object = HeapObject::FromAddress(current);
      object->Iterate(&visitor);
      current += object->Size();
    }
  }
}


static void VerifyEvacuation(PagedSpace* space) {
  PageIterator it(space);

  while (it.has_next()) {
    Page* p = it.next();
    if (p->IsEvacuationCandidate()) continue;
    VerifyEvacuation(p->ObjectAreaStart(), p->ObjectAreaEnd());
  }
}


static void VerifyEvacuation(Heap* heap) {
  VerifyEvacuation(heap->old_pointer_space());
  VerifyEvacuation(heap->old_data_space());
  VerifyEvacuation(heap->code_space());
  VerifyEvacuation(heap->cell_space());
  VerifyEvacuation(heap->map_space());
  VerifyEvacuation(heap->new_space());

  VerifyEvacuationVisitor visitor;
  heap->IterateStrongRoots(&visitor, VISIT_ALL);
}
#endif


void MarkCompactCollector::AddEvacuationCandidate(Page* p) {
  p->MarkEvacuationCandidate();
  evacuation_candidates_.Add(p);
}


bool MarkCompactCollector::StartCompaction() {
  if (!compacting_) {
    ASSERT(evacuation_candidates_.length() == 0);

    CollectEvacuationCandidates(heap()->old_pointer_space());
    CollectEvacuationCandidates(heap()->old_data_space());

    if (FLAG_compact_code_space) {
      CollectEvacuationCandidates(heap()->code_space());
    }

    heap()->old_pointer_space()->EvictEvacuationCandidatesFromFreeLists();
    heap()->old_data_space()->EvictEvacuationCandidatesFromFreeLists();
    heap()->code_space()->EvictEvacuationCandidatesFromFreeLists();

    compacting_ = evacuation_candidates_.length() > 0;
  }

  return compacting_;
}


void MarkCompactCollector::CollectGarbage() {
  // Make sure that Prepare() has been called. The individual steps below will
  // update the state as they proceed.
  ASSERT(state_ == PREPARE_GC);
  ASSERT(encountered_weak_maps_ == Smi::FromInt(0));

  MarkLiveObjects();
  ASSERT(heap_->incremental_marking()->IsStopped());

  if (collect_maps_) ClearNonLiveTransitions();

  ClearWeakMaps();

#ifdef DEBUG
  if (FLAG_verify_heap) {
    VerifyMarking(heap_);
  }
#endif

  SweepSpaces();

  if (!collect_maps_) ReattachInitialMaps();

  heap_->isolate()->inner_pointer_to_code_cache()->Flush();

  Finish();

  tracer_ = NULL;
}


#ifdef DEBUG
void MarkCompactCollector::VerifyMarkbitsAreClean(PagedSpace* space) {
  PageIterator it(space);

  while (it.has_next()) {
    Page* p = it.next();
    CHECK(p->markbits()->IsClean());
    CHECK_EQ(0, p->LiveBytes());
  }
}

void MarkCompactCollector::VerifyMarkbitsAreClean(NewSpace* space) {
  NewSpacePageIterator it(space->bottom(), space->top());

  while (it.has_next()) {
    NewSpacePage* p = it.next();
    CHECK(p->markbits()->IsClean());
    CHECK_EQ(0, p->LiveBytes());
  }
}

void MarkCompactCollector::VerifyMarkbitsAreClean() {
  VerifyMarkbitsAreClean(heap_->old_pointer_space());
  VerifyMarkbitsAreClean(heap_->old_data_space());
  VerifyMarkbitsAreClean(heap_->code_space());
  VerifyMarkbitsAreClean(heap_->cell_space());
  VerifyMarkbitsAreClean(heap_->map_space());
  VerifyMarkbitsAreClean(heap_->new_space());

  LargeObjectIterator it(heap_->lo_space());
  for (HeapObject* obj = it.Next(); obj != NULL; obj = it.Next()) {
    MarkBit mark_bit = Marking::MarkBitFrom(obj);
    ASSERT(Marking::IsWhite(mark_bit));
  }
}
#endif


static void ClearMarkbits(PagedSpace* space) {
  PageIterator it(space);

  while (it.has_next()) {
    Bitmap::Clear(it.next());
  }
}


static void ClearMarkbits(NewSpace* space) {
  NewSpacePageIterator it(space->ToSpaceStart(), space->ToSpaceEnd());

  while (it.has_next()) {
    Bitmap::Clear(it.next());
  }
}


static void ClearMarkbits(Heap* heap) {
  ClearMarkbits(heap->code_space());
  ClearMarkbits(heap->map_space());
  ClearMarkbits(heap->old_pointer_space());
  ClearMarkbits(heap->old_data_space());
  ClearMarkbits(heap->cell_space());
  ClearMarkbits(heap->new_space());

  LargeObjectIterator it(heap->lo_space());
  for (HeapObject* obj = it.Next(); obj != NULL; obj = it.Next()) {
    MarkBit mark_bit = Marking::MarkBitFrom(obj);
    mark_bit.Clear();
    mark_bit.Next().Clear();
  }
}


bool Marking::TransferMark(Address old_start, Address new_start) {
  // This is only used when resizing an object.
  ASSERT(MemoryChunk::FromAddress(old_start) ==
         MemoryChunk::FromAddress(new_start));

  // If the mark doesn't move, we don't check the color of the object.
  // It doesn't matter whether the object is black, since it hasn't changed
  // size, so the adjustment to the live data count will be zero anyway.
  if (old_start == new_start) return false;

  MarkBit new_mark_bit = MarkBitFrom(new_start);
  MarkBit old_mark_bit = MarkBitFrom(old_start);

#ifdef DEBUG
  ObjectColor old_color = Color(old_mark_bit);
#endif

  if (Marking::IsBlack(old_mark_bit)) {
    old_mark_bit.Clear();
    ASSERT(IsWhite(old_mark_bit));
    Marking::MarkBlack(new_mark_bit);
    return true;
  } else if (Marking::IsGrey(old_mark_bit)) {
    ASSERT(heap_->incremental_marking()->IsMarking());
    old_mark_bit.Clear();
    old_mark_bit.Next().Clear();
    ASSERT(IsWhite(old_mark_bit));
    heap_->incremental_marking()->WhiteToGreyAndPush(
        HeapObject::FromAddress(new_start), new_mark_bit);
    heap_->incremental_marking()->RestartIfNotMarking();
  }

#ifdef DEBUG
  ObjectColor new_color = Color(new_mark_bit);
  ASSERT(new_color == old_color);
#endif

  return false;
}


const char* AllocationSpaceName(AllocationSpace space) {
  switch (space) {
    case NEW_SPACE: return "NEW_SPACE";
    case OLD_POINTER_SPACE: return "OLD_POINTER_SPACE";
    case OLD_DATA_SPACE: return "OLD_DATA_SPACE";
    case CODE_SPACE: return "CODE_SPACE";
    case MAP_SPACE: return "MAP_SPACE";
    case CELL_SPACE: return "CELL_SPACE";
    case LO_SPACE: return "LO_SPACE";
    default:
      UNREACHABLE();
  }

  return NULL;
}


void MarkCompactCollector::CollectEvacuationCandidates(PagedSpace* space) {
  ASSERT(space->identity() == OLD_POINTER_SPACE ||
         space->identity() == OLD_DATA_SPACE ||
         space->identity() == CODE_SPACE);

  PageIterator it(space);
  int count = 0;
  if (it.has_next()) it.next();  // Never compact the first page.
  while (it.has_next()) {
    Page* p = it.next();
    bool evacuate = false;
    if (FLAG_stress_compaction) {
      int counter = space->heap()->ms_count();
      uintptr_t page_number = reinterpret_cast<uintptr_t>(p) >> kPageSizeBits;
      if ((counter & 1) == (page_number & 1)) evacuate = true;
    } else {
      if (space->IsFragmented(p)) evacuate = true;
    }
    if (evacuate) {
      AddEvacuationCandidate(p);
      count++;
    } else {
      p->ClearEvacuationCandidate();
    }
  }

  if (count > 0 && FLAG_trace_fragmentation) {
    PrintF("Collected %d evacuation candidates for space %s\n",
           count,
           AllocationSpaceName(space->identity()));
  }
}


void MarkCompactCollector::AbortCompaction() {
  if (compacting_) {
    int npages = evacuation_candidates_.length();
    for (int i = 0; i < npages; i++) {
      Page* p = evacuation_candidates_[i];
      slots_buffer_allocator_.DeallocateChain(p->slots_buffer_address());
      p->ClearEvacuationCandidate();
      p->ClearFlag(MemoryChunk::RESCAN_ON_EVACUATION);
    }
    compacting_ = false;
    evacuation_candidates_.Rewind(0);
    invalidated_code_.Rewind(0);
  }
  ASSERT_EQ(0, evacuation_candidates_.length());
}


void MarkCompactCollector::Prepare(GCTracer* tracer) {
  was_marked_incrementally_ = heap()->incremental_marking()->IsMarking();

  // Disable collection of maps if incremental marking is enabled.
  // Map collection algorithm relies on a special map transition tree traversal
  // order which is not implemented for incremental marking.
  collect_maps_ = FLAG_collect_maps && !was_marked_incrementally_;

  // Rather than passing the tracer around we stash it in a static member
  // variable.
  tracer_ = tracer;

#ifdef DEBUG
  ASSERT(state_ == IDLE);
  state_ = PREPARE_GC;
#endif

  ASSERT(!FLAG_never_compact || !FLAG_always_compact);

  if (collect_maps_) CreateBackPointers();
#ifdef ENABLE_GDB_JIT_INTERFACE
  if (FLAG_gdbjit) {
    // If GDBJIT interface is active disable compaction.
    compacting_collection_ = false;
  }
#endif

  // Clear marking bits for precise sweeping to collect all garbage.
  if (was_marked_incrementally_ && PreciseSweepingRequired()) {
    heap()->incremental_marking()->Abort();
    ClearMarkbits(heap_);
    AbortCompaction();
    was_marked_incrementally_ = false;
  }

  // Don't start compaction if we are in the middle of incremental
  // marking cycle. We did not collect any slots.
  if (!FLAG_never_compact && !was_marked_incrementally_) {
    StartCompaction();
  }

  PagedSpaces spaces;
  for (PagedSpace* space = spaces.next();
       space != NULL;
       space = spaces.next()) {
    space->PrepareForMarkCompact();
  }

#ifdef DEBUG
  if (!was_marked_incrementally_) {
    VerifyMarkbitsAreClean();
  }
#endif

#ifdef DEBUG
  live_bytes_ = 0;
  live_young_objects_size_ = 0;
  live_old_pointer_objects_size_ = 0;
  live_old_data_objects_size_ = 0;
  live_code_objects_size_ = 0;
  live_map_objects_size_ = 0;
  live_cell_objects_size_ = 0;
  live_lo_objects_size_ = 0;
#endif
}


void MarkCompactCollector::Finish() {
#ifdef DEBUG
  ASSERT(state_ == SWEEP_SPACES || state_ == RELOCATE_OBJECTS);
  state_ = IDLE;
#endif
  // The stub cache is not traversed during GC; clear the cache to
  // force lazy re-initialization of it. This must be done after the
  // GC, because it relies on the new address of certain old space
  // objects (empty string, illegal builtin).
  heap()->isolate()->stub_cache()->Clear();

  heap()->external_string_table_.CleanUp();
}


// -------------------------------------------------------------------------
// Phase 1: tracing and marking live objects.
//   before: all objects are in normal state.
//   after: a live object's map pointer is marked as '00'.

// Marking all live objects in the heap as part of mark-sweep or mark-compact
// collection.  Before marking, all objects are in their normal state.  After
// marking, live objects' map pointers are marked indicating that the object
// has been found reachable.
//
// The marking algorithm is a (mostly) depth-first (because of possible stack
// overflow) traversal of the graph of objects reachable from the roots.  It
// uses an explicit stack of pointers rather than recursion.  The young
// generation's inactive ('from') space is used as a marking stack.  The
// objects in the marking stack are the ones that have been reached and marked
// but their children have not yet been visited.
//
// The marking stack can overflow during traversal.  In that case, we set an
// overflow flag.  When the overflow flag is set, we continue marking objects
// reachable from the objects on the marking stack, but no longer push them on
// the marking stack.  Instead, we mark them as both marked and overflowed.
// When the stack is in the overflowed state, objects marked as overflowed
// have been reached and marked but their children have not been visited yet.
// After emptying the marking stack, we clear the overflow flag and traverse
// the heap looking for objects marked as overflowed, push them on the stack,
// and continue with marking.  This process repeats until all reachable
// objects have been marked.

class CodeFlusher {
 public:
  explicit CodeFlusher(Isolate* isolate)
      : isolate_(isolate),
        jsfunction_candidates_head_(NULL),
        shared_function_info_candidates_head_(NULL) {}

  void AddCandidate(SharedFunctionInfo* shared_info) {
    SetNextCandidate(shared_info, shared_function_info_candidates_head_);
    shared_function_info_candidates_head_ = shared_info;
  }

  void AddCandidate(JSFunction* function) {
    ASSERT(function->unchecked_code() ==
           function->unchecked_shared()->unchecked_code());

    SetNextCandidate(function, jsfunction_candidates_head_);
    jsfunction_candidates_head_ = function;
  }

  void ProcessCandidates() {
    ProcessSharedFunctionInfoCandidates();
    ProcessJSFunctionCandidates();
  }

 private:
  void ProcessJSFunctionCandidates() {
    Code* lazy_compile = isolate_->builtins()->builtin(Builtins::kLazyCompile);

    JSFunction* candidate = jsfunction_candidates_head_;
    JSFunction* next_candidate;
    while (candidate != NULL) {
      next_candidate = GetNextCandidate(candidate);

      SharedFunctionInfo* shared = candidate->unchecked_shared();

      Code* code = shared->unchecked_code();
      MarkBit code_mark = Marking::MarkBitFrom(code);
      if (!code_mark.Get()) {
        shared->set_code(lazy_compile);
        candidate->set_code(lazy_compile);
      } else {
        candidate->set_code(shared->unchecked_code());
      }

      // We are in the middle of a GC cycle so the write barrier in the code
      // setter did not record the slot update and we have to do that manually.
      Address slot = candidate->address() + JSFunction::kCodeEntryOffset;
      Code* target = Code::cast(Code::GetObjectFromEntryAddress(slot));
      isolate_->heap()->mark_compact_collector()->
          RecordCodeEntrySlot(slot, target);

      candidate = next_candidate;
    }

    jsfunction_candidates_head_ = NULL;
  }


  void ProcessSharedFunctionInfoCandidates() {
    Code* lazy_compile = isolate_->builtins()->builtin(Builtins::kLazyCompile);

    SharedFunctionInfo* candidate = shared_function_info_candidates_head_;
    SharedFunctionInfo* next_candidate;
    while (candidate != NULL) {
      next_candidate = GetNextCandidate(candidate);
      SetNextCandidate(candidate, NULL);

      Code* code = candidate->unchecked_code();
      MarkBit code_mark = Marking::MarkBitFrom(code);
      if (!code_mark.Get()) {
        candidate->set_code(lazy_compile);
      }

      candidate = next_candidate;
    }

    shared_function_info_candidates_head_ = NULL;
  }

  static JSFunction** GetNextCandidateField(JSFunction* candidate) {
    return reinterpret_cast<JSFunction**>(
        candidate->address() + JSFunction::kCodeEntryOffset);
  }

  static JSFunction* GetNextCandidate(JSFunction* candidate) {
    return *GetNextCandidateField(candidate);
  }

  static void SetNextCandidate(JSFunction* candidate,
                               JSFunction* next_candidate) {
    *GetNextCandidateField(candidate) = next_candidate;
  }

  static SharedFunctionInfo** GetNextCandidateField(
      SharedFunctionInfo* candidate) {
    Code* code = candidate->unchecked_code();
    return reinterpret_cast<SharedFunctionInfo**>(
        code->address() + Code::kNextCodeFlushingCandidateOffset);
  }

  static SharedFunctionInfo* GetNextCandidate(SharedFunctionInfo* candidate) {
    return *GetNextCandidateField(candidate);
  }

  static void SetNextCandidate(SharedFunctionInfo* candidate,
                               SharedFunctionInfo* next_candidate) {
    *GetNextCandidateField(candidate) = next_candidate;
  }

  Isolate* isolate_;
  JSFunction* jsfunction_candidates_head_;
  SharedFunctionInfo* shared_function_info_candidates_head_;

  DISALLOW_COPY_AND_ASSIGN(CodeFlusher);
};


MarkCompactCollector::~MarkCompactCollector() {
  if (code_flusher_ != NULL) {
    delete code_flusher_;
    code_flusher_ = NULL;
  }
}


static inline HeapObject* ShortCircuitConsString(Object** p) {
  // Optimization: If the heap object pointed to by p is a non-symbol
  // cons string whose right substring is HEAP->empty_string, update
  // it in place to its left substring.  Return the updated value.
  //
  // Here we assume that if we change *p, we replace it with a heap object
  // (ie, the left substring of a cons string is always a heap object).
  //
  // The check performed is:
  //   object->IsConsString() && !object->IsSymbol() &&
  //   (ConsString::cast(object)->second() == HEAP->empty_string())
  // except the maps for the object and its possible substrings might be
  // marked.
  HeapObject* object = HeapObject::cast(*p);
  if (!FLAG_clever_optimizations) return object;
  Map* map = object->map();
  InstanceType type = map->instance_type();
  if ((type & kShortcutTypeMask) != kShortcutTypeTag) return object;

  Object* second = reinterpret_cast<ConsString*>(object)->unchecked_second();
  Heap* heap = map->GetHeap();
  if (second != heap->empty_string()) {
    return object;
  }

  // Since we don't have the object's start, it is impossible to update the
  // page dirty marks. Therefore, we only replace the string with its left
  // substring when page dirty marks do not change.
  Object* first = reinterpret_cast<ConsString*>(object)->unchecked_first();
  if (!heap->InNewSpace(object) && heap->InNewSpace(first)) return object;

  *p = first;
  return HeapObject::cast(first);
}


class StaticMarkingVisitor : public StaticVisitorBase {
 public:
  static inline void IterateBody(Map* map, HeapObject* obj) {
    table_.GetVisitor(map)(map, obj);
  }

  static void Initialize() {
    table_.Register(kVisitShortcutCandidate,
                    &FixedBodyVisitor<StaticMarkingVisitor,
                                      ConsString::BodyDescriptor,
                                      void>::Visit);

    table_.Register(kVisitConsString,
                    &FixedBodyVisitor<StaticMarkingVisitor,
                                      ConsString::BodyDescriptor,
                                      void>::Visit);

    table_.Register(kVisitSlicedString,
                    &FixedBodyVisitor<StaticMarkingVisitor,
                                      SlicedString::BodyDescriptor,
                                      void>::Visit);

    table_.Register(kVisitFixedArray,
                    &FlexibleBodyVisitor<StaticMarkingVisitor,
                                         FixedArray::BodyDescriptor,
                                         void>::Visit);

    table_.Register(kVisitGlobalContext, &VisitGlobalContext);

    table_.Register(kVisitFixedDoubleArray, DataObjectVisitor::Visit);

    table_.Register(kVisitByteArray, &DataObjectVisitor::Visit);
    table_.Register(kVisitFreeSpace, &DataObjectVisitor::Visit);
    table_.Register(kVisitSeqAsciiString, &DataObjectVisitor::Visit);
    table_.Register(kVisitSeqTwoByteString, &DataObjectVisitor::Visit);

    table_.Register(kVisitJSWeakMap, &VisitJSWeakMap);

    table_.Register(kVisitOddball,
                    &FixedBodyVisitor<StaticMarkingVisitor,
                                      Oddball::BodyDescriptor,
                                      void>::Visit);
    table_.Register(kVisitMap,
                    &FixedBodyVisitor<StaticMarkingVisitor,
                                      Map::BodyDescriptor,
                                      void>::Visit);

    table_.Register(kVisitCode, &VisitCode);

    table_.Register(kVisitSharedFunctionInfo,
                    &VisitSharedFunctionInfoAndFlushCode);

    table_.Register(kVisitJSFunction,
                    &VisitJSFunctionAndFlushCode);

    table_.Register(kVisitJSRegExp,
                    &VisitRegExpAndFlushCode);

    table_.Register(kVisitPropertyCell,
                    &FixedBodyVisitor<StaticMarkingVisitor,
                                      JSGlobalPropertyCell::BodyDescriptor,
                                      void>::Visit);

    table_.RegisterSpecializations<DataObjectVisitor,
                                   kVisitDataObject,
                                   kVisitDataObjectGeneric>();

    table_.RegisterSpecializations<JSObjectVisitor,
                                   kVisitJSObject,
                                   kVisitJSObjectGeneric>();

    table_.RegisterSpecializations<StructObjectVisitor,
                                   kVisitStruct,
                                   kVisitStructGeneric>();
  }

  INLINE(static void VisitPointer(Heap* heap, Object** p)) {
    MarkObjectByPointer(heap->mark_compact_collector(), p, p);
  }

  INLINE(static void VisitPointers(Heap* heap, Object** start, Object** end)) {
    // Mark all objects pointed to in [start, end).
    const int kMinRangeForMarkingRecursion = 64;
    if (end - start >= kMinRangeForMarkingRecursion) {
      if (VisitUnmarkedObjects(heap, start, end)) return;
      // We are close to a stack overflow, so just mark the objects.
    }
    MarkCompactCollector* collector = heap->mark_compact_collector();
    for (Object** p = start; p < end; p++) {
      MarkObjectByPointer(collector, start, p);
    }
  }

  static void VisitGlobalPropertyCell(Heap* heap, RelocInfo* rinfo) {
    ASSERT(rinfo->rmode() == RelocInfo::GLOBAL_PROPERTY_CELL);
    JSGlobalPropertyCell* cell =
        JSGlobalPropertyCell::cast(rinfo->target_cell());
    MarkBit mark = Marking::MarkBitFrom(cell);
    heap->mark_compact_collector()->MarkObject(cell, mark);
  }

  static inline void VisitEmbeddedPointer(Heap* heap, RelocInfo* rinfo) {
    ASSERT(rinfo->rmode() == RelocInfo::EMBEDDED_OBJECT);
    // TODO(mstarzinger): We do not short-circuit cons strings here, verify
    // that there can be no such embedded pointers and add assertion here.
    HeapObject* object = HeapObject::cast(rinfo->target_object());
    heap->mark_compact_collector()->RecordRelocSlot(rinfo, object);
    MarkBit mark = Marking::MarkBitFrom(object);
    heap->mark_compact_collector()->MarkObject(object, mark);
  }

  static inline void VisitCodeTarget(Heap* heap, RelocInfo* rinfo) {
    ASSERT(RelocInfo::IsCodeTarget(rinfo->rmode()));
    Code* target = Code::GetCodeFromTargetAddress(rinfo->target_address());
    if (FLAG_cleanup_code_caches_at_gc && target->is_inline_cache_stub()) {
      IC::Clear(rinfo->pc());
      // Please note targets for cleared inline cached do not have to be
      // marked since they are contained in HEAP->non_monomorphic_cache().
      target = Code::GetCodeFromTargetAddress(rinfo->target_address());
    } else {
      if (FLAG_cleanup_code_caches_at_gc &&
          target->kind() == Code::STUB &&
          target->major_key() == CodeStub::CallFunction &&
          target->has_function_cache()) {
        CallFunctionStub::Clear(heap, rinfo->pc());
      }
      MarkBit code_mark = Marking::MarkBitFrom(target);
      heap->mark_compact_collector()->MarkObject(target, code_mark);
    }
    heap->mark_compact_collector()->RecordRelocSlot(rinfo, target);
  }

  static inline void VisitDebugTarget(Heap* heap, RelocInfo* rinfo) {
    ASSERT((RelocInfo::IsJSReturn(rinfo->rmode()) &&
            rinfo->IsPatchedReturnSequence()) ||
           (RelocInfo::IsDebugBreakSlot(rinfo->rmode()) &&
            rinfo->IsPatchedDebugBreakSlotSequence()));
    Code* target = Code::GetCodeFromTargetAddress(rinfo->call_address());
    MarkBit code_mark = Marking::MarkBitFrom(target);
    heap->mark_compact_collector()->MarkObject(target, code_mark);
    heap->mark_compact_collector()->RecordRelocSlot(rinfo, target);
  }

  // Mark object pointed to by p.
  INLINE(static void MarkObjectByPointer(MarkCompactCollector* collector,
                                         Object** anchor_slot,
                                         Object** p)) {
    if (!(*p)->IsHeapObject()) return;
    HeapObject* object = ShortCircuitConsString(p);
    collector->RecordSlot(anchor_slot, p, object);
    MarkBit mark = Marking::MarkBitFrom(object);
    collector->MarkObject(object, mark);
  }


  // Visit an unmarked object.
  INLINE(static void VisitUnmarkedObject(MarkCompactCollector* collector,
                                         HeapObject* obj)) {
#ifdef DEBUG
    ASSERT(Isolate::Current()->heap()->Contains(obj));
    ASSERT(!HEAP->mark_compact_collector()->IsMarked(obj));
#endif
    Map* map = obj->map();
    Heap* heap = obj->GetHeap();
    MarkBit mark = Marking::MarkBitFrom(obj);
    heap->mark_compact_collector()->SetMark(obj, mark);
    // Mark the map pointer and the body.
    MarkBit map_mark = Marking::MarkBitFrom(map);
    heap->mark_compact_collector()->MarkObject(map, map_mark);
    IterateBody(map, obj);
  }

  // Visit all unmarked objects pointed to by [start, end).
  // Returns false if the operation fails (lack of stack space).
  static inline bool VisitUnmarkedObjects(Heap* heap,
                                          Object** start,
                                          Object** end) {
    // Return false is we are close to the stack limit.
    StackLimitCheck check(heap->isolate());
    if (check.HasOverflowed()) return false;

    MarkCompactCollector* collector = heap->mark_compact_collector();
    // Visit the unmarked objects.
    for (Object** p = start; p < end; p++) {
      Object* o = *p;
      if (!o->IsHeapObject()) continue;
      collector->RecordSlot(start, p, o);
      HeapObject* obj = HeapObject::cast(o);
      MarkBit mark = Marking::MarkBitFrom(obj);
      if (mark.Get()) continue;
      VisitUnmarkedObject(collector, obj);
    }
    return true;
  }

  static inline void VisitExternalReference(Address* p) { }
  static inline void VisitRuntimeEntry(RelocInfo* rinfo) { }

 private:
  class DataObjectVisitor {
   public:
    template<int size>
    static void VisitSpecialized(Map* map, HeapObject* object) {
    }

    static void Visit(Map* map, HeapObject* object) {
    }
  };

  typedef FlexibleBodyVisitor<StaticMarkingVisitor,
                              JSObject::BodyDescriptor,
                              void> JSObjectVisitor;

  typedef FlexibleBodyVisitor<StaticMarkingVisitor,
                              StructBodyDescriptor,
                              void> StructObjectVisitor;

  static void VisitJSWeakMap(Map* map, HeapObject* object) {
    MarkCompactCollector* collector = map->GetHeap()->mark_compact_collector();
    JSWeakMap* weak_map = reinterpret_cast<JSWeakMap*>(object);

    // Enqueue weak map in linked list of encountered weak maps.
    ASSERT(weak_map->next() == Smi::FromInt(0));
    weak_map->set_next(collector->encountered_weak_maps());
    collector->set_encountered_weak_maps(weak_map);

    // Skip visiting the backing hash table containing the mappings.
    int object_size = JSWeakMap::BodyDescriptor::SizeOf(map, object);
    BodyVisitorBase<StaticMarkingVisitor>::IteratePointers(
        map->GetHeap(),
        object,
        JSWeakMap::BodyDescriptor::kStartOffset,
        JSWeakMap::kTableOffset);
    BodyVisitorBase<StaticMarkingVisitor>::IteratePointers(
        map->GetHeap(),
        object,
        JSWeakMap::kTableOffset + kPointerSize,
        object_size);

    // Mark the backing hash table without pushing it on the marking stack.
    ASSERT(!MarkCompactCollector::IsMarked(weak_map->unchecked_table()));
    ASSERT(MarkCompactCollector::IsMarked(weak_map->unchecked_table()->map()));

    HeapObject* unchecked_table = weak_map->unchecked_table();
    MarkBit mark_bit = Marking::MarkBitFrom(unchecked_table);
    collector->SetMark(unchecked_table, mark_bit);
  }

  static void VisitCode(Map* map, HeapObject* object) {
    reinterpret_cast<Code*>(object)->CodeIterateBody<StaticMarkingVisitor>(
        map->GetHeap());
  }

  // Code flushing support.

  // How many collections newly compiled code object will survive before being
  // flushed.
  static const int kCodeAgeThreshold = 5;

  static const int kRegExpCodeThreshold = 5;

  inline static bool HasSourceCode(Heap* heap, SharedFunctionInfo* info) {
    Object* undefined = heap->undefined_value();
    return (info->script() != undefined) &&
        (reinterpret_cast<Script*>(info->script())->source() != undefined);
  }


  inline static bool IsCompiled(JSFunction* function) {
    return function->unchecked_code() !=
        function->GetIsolate()->builtins()->builtin(Builtins::kLazyCompile);
  }

  inline static bool IsCompiled(SharedFunctionInfo* function) {
    return function->unchecked_code() !=
        function->GetIsolate()->builtins()->builtin(Builtins::kLazyCompile);
  }

  inline static bool IsFlushable(Heap* heap, JSFunction* function) {
    SharedFunctionInfo* shared_info = function->unchecked_shared();

    // Code is either on stack, in compilation cache or referenced
    // by optimized version of function.
    MarkBit code_mark =
        Marking::MarkBitFrom(function->unchecked_code());
    if (code_mark.Get()) {
      shared_info->set_code_age(0);
      return false;
    }

    // We do not flush code for optimized functions.
    if (function->code() != shared_info->unchecked_code()) {
      return false;
    }

    return IsFlushable(heap, shared_info);
  }

  inline static bool IsFlushable(Heap* heap, SharedFunctionInfo* shared_info) {
    // Code is either on stack, in compilation cache or referenced
    // by optimized version of function.
    MarkBit code_mark =
        Marking::MarkBitFrom(shared_info->unchecked_code());
    if (code_mark.Get()) {
      shared_info->set_code_age(0);
      return false;
    }

    // The function must be compiled and have the source code available,
    // to be able to recompile it in case we need the function again.
    if (!(shared_info->is_compiled() && HasSourceCode(heap, shared_info))) {
      return false;
    }

    // We never flush code for Api functions.
    Object* function_data = shared_info->function_data();
    if (function_data->IsFunctionTemplateInfo()) return false;

    // Only flush code for functions.
    if (shared_info->code()->kind() != Code::FUNCTION) return false;

    // Function must be lazy compilable.
    if (!shared_info->allows_lazy_compilation()) return false;

    // If this is a full script wrapped in a function we do no flush the code.
    if (shared_info->is_toplevel()) return false;

    // Age this shared function info.
    if (shared_info->code_age() < kCodeAgeThreshold) {
      shared_info->set_code_age(shared_info->code_age() + 1);
      return false;
    }

    return true;
  }


  static bool FlushCodeForFunction(Heap* heap, JSFunction* function) {
    if (!IsFlushable(heap, function)) return false;

    // This function's code looks flushable. But we have to postpone the
    // decision until we see all functions that point to the same
    // SharedFunctionInfo because some of them might be optimized.
    // That would make the nonoptimized version of the code nonflushable,
    // because it is required for bailing out from optimized code.
    heap->mark_compact_collector()->code_flusher()->AddCandidate(function);
    return true;
  }

  static inline bool IsValidNotBuiltinContext(Object* ctx) {
    return ctx->IsContext() &&
        !Context::cast(ctx)->global()->IsJSBuiltinsObject();
  }


  static void VisitSharedFunctionInfoGeneric(Map* map, HeapObject* object) {
    SharedFunctionInfo* shared = reinterpret_cast<SharedFunctionInfo*>(object);

    if (shared->IsInobjectSlackTrackingInProgress()) shared->DetachInitialMap();

    FixedBodyVisitor<StaticMarkingVisitor,
                     SharedFunctionInfo::BodyDescriptor,
                     void>::Visit(map, object);
  }


  static void UpdateRegExpCodeAgeAndFlush(Heap* heap,
                                          JSRegExp* re,
                                          bool is_ascii) {
    // Make sure that the fixed array is in fact initialized on the RegExp.
    // We could potentially trigger a GC when initializing the RegExp.
    if (HeapObject::cast(re->data())->map()->instance_type() !=
            FIXED_ARRAY_TYPE) return;

    // Make sure this is a RegExp that actually contains code.
    if (re->TypeTagUnchecked() != JSRegExp::IRREGEXP) return;

    Object* code = re->DataAtUnchecked(JSRegExp::code_index(is_ascii));
    if (!code->IsSmi() &&
        HeapObject::cast(code)->map()->instance_type() == CODE_TYPE) {
      // Save a copy that can be reinstated if we need the code again.
      re->SetDataAtUnchecked(JSRegExp::saved_code_index(is_ascii),
                             code,
                             heap);
      // Set a number in the 0-255 range to guarantee no smi overflow.
      re->SetDataAtUnchecked(JSRegExp::code_index(is_ascii),
                             Smi::FromInt(heap->sweep_generation() & 0xff),
                             heap);
    } else if (code->IsSmi()) {
      int value = Smi::cast(code)->value();
      // The regexp has not been compiled yet or there was a compilation error.
      if (value == JSRegExp::kUninitializedValue ||
          value == JSRegExp::kCompilationErrorValue) {
        return;
      }

      // Check if we should flush now.
      if (value == ((heap->sweep_generation() - kRegExpCodeThreshold) & 0xff)) {
        re->SetDataAtUnchecked(JSRegExp::code_index(is_ascii),
                               Smi::FromInt(JSRegExp::kUninitializedValue),
                               heap);
        re->SetDataAtUnchecked(JSRegExp::saved_code_index(is_ascii),
                               Smi::FromInt(JSRegExp::kUninitializedValue),
                               heap);
      }
    }
  }


  // Works by setting the current sweep_generation (as a smi) in the
  // code object place in the data array of the RegExp and keeps a copy
  // around that can be reinstated if we reuse the RegExp before flushing.
  // If we did not use the code for kRegExpCodeThreshold mark sweep GCs
  // we flush the code.
  static void VisitRegExpAndFlushCode(Map* map, HeapObject* object) {
    Heap* heap = map->GetHeap();
    MarkCompactCollector* collector = heap->mark_compact_collector();
    if (!collector->is_code_flushing_enabled()) {
      VisitJSRegExpFields(map, object);
      return;
    }
    JSRegExp* re = reinterpret_cast<JSRegExp*>(object);
    // Flush code or set age on both ascii and two byte code.
    UpdateRegExpCodeAgeAndFlush(heap, re, true);
    UpdateRegExpCodeAgeAndFlush(heap, re, false);
    // Visit the fields of the RegExp, including the updated FixedArray.
    VisitJSRegExpFields(map, object);
  }


  static void VisitSharedFunctionInfoAndFlushCode(Map* map,
                                                  HeapObject* object) {
    MarkCompactCollector* collector = map->GetHeap()->mark_compact_collector();
    if (!collector->is_code_flushing_enabled()) {
      VisitSharedFunctionInfoGeneric(map, object);
      return;
    }
    VisitSharedFunctionInfoAndFlushCodeGeneric(map, object, false);
  }


  static void VisitSharedFunctionInfoAndFlushCodeGeneric(
      Map* map, HeapObject* object, bool known_flush_code_candidate) {
    Heap* heap = map->GetHeap();
    SharedFunctionInfo* shared = reinterpret_cast<SharedFunctionInfo*>(object);

    if (shared->IsInobjectSlackTrackingInProgress()) shared->DetachInitialMap();

    if (!known_flush_code_candidate) {
      known_flush_code_candidate = IsFlushable(heap, shared);
      if (known_flush_code_candidate) {
        heap->mark_compact_collector()->code_flusher()->AddCandidate(shared);
      }
    }

    VisitSharedFunctionInfoFields(heap, object, known_flush_code_candidate);
  }


  static void VisitCodeEntry(Heap* heap, Address entry_address) {
    Code* code = Code::cast(Code::GetObjectFromEntryAddress(entry_address));
    MarkBit mark = Marking::MarkBitFrom(code);
    heap->mark_compact_collector()->MarkObject(code, mark);
    heap->mark_compact_collector()->
        RecordCodeEntrySlot(entry_address, code);
  }

  static void VisitGlobalContext(Map* map, HeapObject* object) {
    FixedBodyVisitor<StaticMarkingVisitor,
                     Context::MarkCompactBodyDescriptor,
                     void>::Visit(map, object);

    MarkCompactCollector* collector = map->GetHeap()->mark_compact_collector();
    for (int idx = Context::FIRST_WEAK_SLOT;
         idx < Context::GLOBAL_CONTEXT_SLOTS;
         ++idx) {
      Object** slot =
          HeapObject::RawField(object, FixedArray::OffsetOfElementAt(idx));
      collector->RecordSlot(slot, slot, *slot);
    }
  }

  static void VisitJSFunctionAndFlushCode(Map* map, HeapObject* object) {
    Heap* heap = map->GetHeap();
    MarkCompactCollector* collector = heap->mark_compact_collector();
    if (!collector->is_code_flushing_enabled()) {
      VisitJSFunction(map, object);
      return;
    }

    JSFunction* jsfunction = reinterpret_cast<JSFunction*>(object);
    // The function must have a valid context and not be a builtin.
    bool flush_code_candidate = false;
    if (IsValidNotBuiltinContext(jsfunction->unchecked_context())) {
      flush_code_candidate = FlushCodeForFunction(heap, jsfunction);
    }

    if (!flush_code_candidate) {
      Code* code = jsfunction->unchecked_shared()->unchecked_code();
      MarkBit code_mark = Marking::MarkBitFrom(code);
      heap->mark_compact_collector()->MarkObject(code, code_mark);

      if (jsfunction->unchecked_code()->kind() == Code::OPTIMIZED_FUNCTION) {
        // For optimized functions we should retain both non-optimized version
        // of it's code and non-optimized version of all inlined functions.
        // This is required to support bailing out from inlined code.
        DeoptimizationInputData* data =
            reinterpret_cast<DeoptimizationInputData*>(
                jsfunction->unchecked_code()->unchecked_deoptimization_data());

        FixedArray* literals = data->UncheckedLiteralArray();

        for (int i = 0, count = data->InlinedFunctionCount()->value();
             i < count;
             i++) {
          JSFunction* inlined = reinterpret_cast<JSFunction*>(literals->get(i));
          Code* inlined_code = inlined->unchecked_shared()->unchecked_code();
          MarkBit inlined_code_mark =
              Marking::MarkBitFrom(inlined_code);
          heap->mark_compact_collector()->MarkObject(
              inlined_code, inlined_code_mark);
        }
      }
    }

    VisitJSFunctionFields(map,
                          reinterpret_cast<JSFunction*>(object),
                          flush_code_candidate);
  }


  static void VisitJSFunction(Map* map, HeapObject* object) {
    VisitJSFunctionFields(map,
                          reinterpret_cast<JSFunction*>(object),
                          false);
  }


#define SLOT_ADDR(obj, offset) \
  reinterpret_cast<Object**>((obj)->address() + offset)


  static inline void VisitJSFunctionFields(Map* map,
                                           JSFunction* object,
                                           bool flush_code_candidate) {
    Heap* heap = map->GetHeap();

    VisitPointers(heap,
                  HeapObject::RawField(object, JSFunction::kPropertiesOffset),
                  HeapObject::RawField(object, JSFunction::kCodeEntryOffset));

    if (!flush_code_candidate) {
      VisitCodeEntry(heap, object->address() + JSFunction::kCodeEntryOffset);
    } else {
      // Don't visit code object.

      // Visit shared function info to avoid double checking of it's
      // flushability.
      SharedFunctionInfo* shared_info = object->unchecked_shared();
      MarkBit shared_info_mark = Marking::MarkBitFrom(shared_info);
      if (!shared_info_mark.Get()) {
        Map* shared_info_map = shared_info->map();
        MarkBit shared_info_map_mark =
            Marking::MarkBitFrom(shared_info_map);
        heap->mark_compact_collector()->SetMark(shared_info, shared_info_mark);
        heap->mark_compact_collector()->MarkObject(shared_info_map,
                                                   shared_info_map_mark);
        VisitSharedFunctionInfoAndFlushCodeGeneric(shared_info_map,
                                                   shared_info,
                                                   true);
      }
    }

    VisitPointers(
        heap,
        HeapObject::RawField(object,
                             JSFunction::kCodeEntryOffset + kPointerSize),
        HeapObject::RawField(object,
                             JSFunction::kNonWeakFieldsEndOffset));

    // Don't visit the next function list field as it is a weak reference.
    Object** next_function =
        HeapObject::RawField(object, JSFunction::kNextFunctionLinkOffset);
    heap->mark_compact_collector()->RecordSlot(
        next_function, next_function, *next_function);
  }

  static inline void VisitJSRegExpFields(Map* map,
                                         HeapObject* object) {
    int last_property_offset =
        JSRegExp::kSize + kPointerSize * map->inobject_properties();
    VisitPointers(map->GetHeap(),
                  SLOT_ADDR(object, JSRegExp::kPropertiesOffset),
                  SLOT_ADDR(object, last_property_offset));
  }


  static void VisitSharedFunctionInfoFields(Heap* heap,
                                            HeapObject* object,
                                            bool flush_code_candidate) {
    VisitPointer(heap, SLOT_ADDR(object, SharedFunctionInfo::kNameOffset));

    if (!flush_code_candidate) {
      VisitPointer(heap, SLOT_ADDR(object, SharedFunctionInfo::kCodeOffset));
    }

    VisitPointers(heap,
                  SLOT_ADDR(object, SharedFunctionInfo::kScopeInfoOffset),
                  SLOT_ADDR(object, SharedFunctionInfo::kSize));
  }

  #undef SLOT_ADDR

  typedef void (*Callback)(Map* map, HeapObject* object);

  static VisitorDispatchTable<Callback> table_;
};


VisitorDispatchTable<StaticMarkingVisitor::Callback>
  StaticMarkingVisitor::table_;


class MarkingVisitor : public ObjectVisitor {
 public:
  explicit MarkingVisitor(Heap* heap) : heap_(heap) { }

  void VisitPointer(Object** p) {
    StaticMarkingVisitor::VisitPointer(heap_, p);
  }

  void VisitPointers(Object** start, Object** end) {
    StaticMarkingVisitor::VisitPointers(heap_, start, end);
  }

 private:
  Heap* heap_;
};


class CodeMarkingVisitor : public ThreadVisitor {
 public:
  explicit CodeMarkingVisitor(MarkCompactCollector* collector)
      : collector_(collector) {}

  void VisitThread(Isolate* isolate, ThreadLocalTop* top) {
    for (StackFrameIterator it(isolate, top); !it.done(); it.Advance()) {
      Code* code = it.frame()->unchecked_code();
      MarkBit code_bit = Marking::MarkBitFrom(code);
      collector_->MarkObject(it.frame()->unchecked_code(), code_bit);
    }
  }

 private:
  MarkCompactCollector* collector_;
};


class SharedFunctionInfoMarkingVisitor : public ObjectVisitor {
 public:
  explicit SharedFunctionInfoMarkingVisitor(MarkCompactCollector* collector)
      : collector_(collector) {}

  void VisitPointers(Object** start, Object** end) {
    for (Object** p = start; p < end; p++) VisitPointer(p);
  }

  void VisitPointer(Object** slot) {
    Object* obj = *slot;
    if (obj->IsSharedFunctionInfo()) {
      SharedFunctionInfo* shared = reinterpret_cast<SharedFunctionInfo*>(obj);
      MarkBit shared_mark = Marking::MarkBitFrom(shared);
      MarkBit code_mark = Marking::MarkBitFrom(shared->unchecked_code());
      collector_->MarkObject(shared->unchecked_code(), code_mark);
      collector_->MarkObject(shared, shared_mark);
    }
  }

 private:
  MarkCompactCollector* collector_;
};


void MarkCompactCollector::PrepareForCodeFlushing() {
  ASSERT(heap() == Isolate::Current()->heap());

  // TODO(1609) Currently incremental marker does not support code flushing.
  if (!FLAG_flush_code || was_marked_incrementally_) {
    EnableCodeFlushing(false);
    return;
  }

#ifdef ENABLE_DEBUGGER_SUPPORT
  if (heap()->isolate()->debug()->IsLoaded() ||
      heap()->isolate()->debug()->has_break_points()) {
    EnableCodeFlushing(false);
    return;
  }
#endif

  EnableCodeFlushing(true);

  // Ensure that empty descriptor array is marked. Method MarkDescriptorArray
  // relies on it being marked before any other descriptor array.
  HeapObject* descriptor_array = heap()->empty_descriptor_array();
  MarkBit descriptor_array_mark = Marking::MarkBitFrom(descriptor_array);
  MarkObject(descriptor_array, descriptor_array_mark);

  // Make sure we are not referencing the code from the stack.
  ASSERT(this == heap()->mark_compact_collector());
  for (StackFrameIterator it; !it.done(); it.Advance()) {
    Code* code = it.frame()->unchecked_code();
    MarkBit code_mark = Marking::MarkBitFrom(code);
    MarkObject(code, code_mark);
  }

  // Iterate the archived stacks in all threads to check if
  // the code is referenced.
  CodeMarkingVisitor code_marking_visitor(this);
  heap()->isolate()->thread_manager()->IterateArchivedThreads(
      &code_marking_visitor);

  SharedFunctionInfoMarkingVisitor visitor(this);
  heap()->isolate()->compilation_cache()->IterateFunctions(&visitor);
  heap()->isolate()->handle_scope_implementer()->Iterate(&visitor);

  ProcessMarkingDeque();
}


// Visitor class for marking heap roots.
class RootMarkingVisitor : public ObjectVisitor {
 public:
  explicit RootMarkingVisitor(Heap* heap)
    : collector_(heap->mark_compact_collector()) { }

  void VisitPointer(Object** p) {
    MarkObjectByPointer(p);
  }

  void VisitPointers(Object** start, Object** end) {
    for (Object** p = start; p < end; p++) MarkObjectByPointer(p);
  }

 private:
  void MarkObjectByPointer(Object** p) {
    if (!(*p)->IsHeapObject()) return;

    // Replace flat cons strings in place.
    HeapObject* object = ShortCircuitConsString(p);
    MarkBit mark_bit = Marking::MarkBitFrom(object);
    if (mark_bit.Get()) return;

    Map* map = object->map();
    // Mark the object.
    collector_->SetMark(object, mark_bit);

    // Mark the map pointer and body, and push them on the marking stack.
    MarkBit map_mark = Marking::MarkBitFrom(map);
    collector_->MarkObject(map, map_mark);
    StaticMarkingVisitor::IterateBody(map, object);

    // Mark all the objects reachable from the map and body.  May leave
    // overflowed objects in the heap.
    collector_->EmptyMarkingDeque();
  }

  MarkCompactCollector* collector_;
};


// Helper class for pruning the symbol table.
class SymbolTableCleaner : public ObjectVisitor {
 public:
  explicit SymbolTableCleaner(Heap* heap)
    : heap_(heap), pointers_removed_(0) { }

  virtual void VisitPointers(Object** start, Object** end) {
    // Visit all HeapObject pointers in [start, end).
    for (Object** p = start; p < end; p++) {
      Object* o = *p;
      if (o->IsHeapObject() &&
          !Marking::MarkBitFrom(HeapObject::cast(o)).Get()) {
        // Check if the symbol being pruned is an external symbol. We need to
        // delete the associated external data as this symbol is going away.

        // Since no objects have yet been moved we can safely access the map of
        // the object.
        if (o->IsExternalString()) {
          heap_->FinalizeExternalString(String::cast(*p));
        }
        // Set the entry to null_value (as deleted).
        *p = heap_->null_value();
        pointers_removed_++;
      }
    }
  }

  int PointersRemoved() {
    return pointers_removed_;
  }

 private:
  Heap* heap_;
  int pointers_removed_;
};


// Implementation of WeakObjectRetainer for mark compact GCs. All marked objects
// are retained.
class MarkCompactWeakObjectRetainer : public WeakObjectRetainer {
 public:
  virtual Object* RetainAs(Object* object) {
    if (Marking::MarkBitFrom(HeapObject::cast(object)).Get()) {
      return object;
    } else {
      return NULL;
    }
  }
};


void MarkCompactCollector::ProcessNewlyMarkedObject(HeapObject* object) {
  ASSERT(IsMarked(object));
  ASSERT(HEAP->Contains(object));
  if (object->IsMap()) {
    Map* map = Map::cast(object);
    if (FLAG_cleanup_code_caches_at_gc) {
      map->ClearCodeCache(heap());
    }

    // When map collection is enabled we have to mark through map's transitions
    // in a special way to make transition links weak.
    // Only maps for subclasses of JSReceiver can have transitions.
    STATIC_ASSERT(LAST_TYPE == LAST_JS_RECEIVER_TYPE);
    if (collect_maps_ && map->instance_type() >= FIRST_JS_RECEIVER_TYPE) {
      MarkMapContents(map);
    } else {
      marking_deque_.PushBlack(map);
    }
  } else {
    marking_deque_.PushBlack(object);
  }
}


void MarkCompactCollector::MarkMapContents(Map* map) {
  // Mark prototype transitions array but don't push it into marking stack.
  // This will make references from it weak. We will clean dead prototype
  // transitions in ClearNonLiveTransitions.
  FixedArray* prototype_transitions = map->prototype_transitions();
  MarkBit mark = Marking::MarkBitFrom(prototype_transitions);
  if (!mark.Get()) {
    mark.Set();
    MemoryChunk::IncrementLiveBytes(prototype_transitions->address(),
                                    prototype_transitions->Size());
  }

  Object** raw_descriptor_array_slot =
      HeapObject::RawField(map, Map::kInstanceDescriptorsOrBitField3Offset);
  Object* raw_descriptor_array = *raw_descriptor_array_slot;
  if (!raw_descriptor_array->IsSmi()) {
    MarkDescriptorArray(
        reinterpret_cast<DescriptorArray*>(raw_descriptor_array));
  }

  // Mark the Object* fields of the Map.
  // Since the descriptor array has been marked already, it is fine
  // that one of these fields contains a pointer to it.
  Object** start_slot = HeapObject::RawField(map,
                                             Map::kPointerFieldsBeginOffset);

  Object** end_slot = HeapObject::RawField(map, Map::kPointerFieldsEndOffset);

  StaticMarkingVisitor::VisitPointers(map->GetHeap(), start_slot, end_slot);
}


void MarkCompactCollector::MarkDescriptorArray(
    DescriptorArray* descriptors) {
  MarkBit descriptors_mark = Marking::MarkBitFrom(descriptors);
  if (descriptors_mark.Get()) return;
  // Empty descriptor array is marked as a root before any maps are marked.
  ASSERT(descriptors != heap()->empty_descriptor_array());
  SetMark(descriptors, descriptors_mark);

  FixedArray* contents = reinterpret_cast<FixedArray*>(
      descriptors->get(DescriptorArray::kContentArrayIndex));
  ASSERT(contents->IsHeapObject());
  ASSERT(!IsMarked(contents));
  ASSERT(contents->IsFixedArray());
  ASSERT(contents->length() >= 2);
  MarkBit contents_mark = Marking::MarkBitFrom(contents);
  SetMark(contents, contents_mark);
  // Contents contains (value, details) pairs.  If the details say that the type
  // of descriptor is MAP_TRANSITION, CONSTANT_TRANSITION,
  // EXTERNAL_ARRAY_TRANSITION or NULL_DESCRIPTOR, we don't mark the value as
  // live.  Only for MAP_TRANSITION, EXTERNAL_ARRAY_TRANSITION and
  // CONSTANT_TRANSITION is the value an Object* (a Map*).
  for (int i = 0; i < contents->length(); i += 2) {
    // If the pair (value, details) at index i, i+1 is not
    // a transition or null descriptor, mark the value.
    PropertyDetails details(Smi::cast(contents->get(i + 1)));

    Object** slot = contents->data_start() + i;
    Object* value = *slot;
    if (!value->IsHeapObject()) continue;

    RecordSlot(slot, slot, *slot);

    PropertyType type = details.type();
    if (type < FIRST_PHANTOM_PROPERTY_TYPE) {
      HeapObject* object = HeapObject::cast(value);
      MarkBit mark = Marking::MarkBitFrom(HeapObject::cast(object));
      if (!mark.Get()) {
        SetMark(HeapObject::cast(object), mark);
        marking_deque_.PushBlack(object);
      }
    } else if (type == ELEMENTS_TRANSITION && value->IsFixedArray()) {
      // For maps with multiple elements transitions, the transition maps are
      // stored in a FixedArray. Keep the fixed array alive but not the maps
      // that it refers to.
      HeapObject* object = HeapObject::cast(value);
      MarkBit mark = Marking::MarkBitFrom(HeapObject::cast(object));
      if (!mark.Get()) {
        SetMark(HeapObject::cast(object), mark);
      }
    }
  }
  // The DescriptorArray descriptors contains a pointer to its contents array,
  // but the contents array is already marked.
  marking_deque_.PushBlack(descriptors);
}


void MarkCompactCollector::CreateBackPointers() {
  HeapObjectIterator iterator(heap()->map_space());
  for (HeapObject* next_object = iterator.Next();
       next_object != NULL; next_object = iterator.Next()) {
    if (next_object->IsMap()) {  // Could also be FreeSpace object on free list.
      Map* map = Map::cast(next_object);
      STATIC_ASSERT(LAST_TYPE == LAST_JS_RECEIVER_TYPE);
      if (map->instance_type() >= FIRST_JS_RECEIVER_TYPE) {
        map->CreateBackPointers();
      } else {
        ASSERT(map->instance_descriptors() == heap()->empty_descriptor_array());
      }
    }
  }
}


// Fill the marking stack with overflowed objects returned by the given
// iterator.  Stop when the marking stack is filled or the end of the space
// is reached, whichever comes first.
template<class T>
static void DiscoverGreyObjectsWithIterator(Heap* heap,
                                            MarkingDeque* marking_deque,
                                            T* it) {
  // The caller should ensure that the marking stack is initially not full,
  // so that we don't waste effort pointlessly scanning for objects.
  ASSERT(!marking_deque->IsFull());

  Map* filler_map = heap->one_pointer_filler_map();
  for (HeapObject* object = it->Next();
       object != NULL;
       object = it->Next()) {
    MarkBit markbit = Marking::MarkBitFrom(object);
    if ((object->map() != filler_map) && Marking::IsGrey(markbit)) {
      Marking::GreyToBlack(markbit);
      MemoryChunk::IncrementLiveBytes(object->address(), object->Size());
      marking_deque->PushBlack(object);
      if (marking_deque->IsFull()) return;
    }
  }
}


static inline int MarkWordToObjectStarts(uint32_t mark_bits, int* starts);


static void DiscoverGreyObjectsOnPage(MarkingDeque* marking_deque, Page* p) {
  ASSERT(strcmp(Marking::kWhiteBitPattern, "00") == 0);
  ASSERT(strcmp(Marking::kBlackBitPattern, "10") == 0);
  ASSERT(strcmp(Marking::kGreyBitPattern, "11") == 0);
  ASSERT(strcmp(Marking::kImpossibleBitPattern, "01") == 0);

  MarkBit::CellType* cells = p->markbits()->cells();

  int last_cell_index =
      Bitmap::IndexToCell(
          Bitmap::CellAlignIndex(
              p->AddressToMarkbitIndex(p->ObjectAreaEnd())));

  int cell_index = Page::kFirstUsedCell;
  Address cell_base = p->ObjectAreaStart();

  for (cell_index = Page::kFirstUsedCell;
       cell_index < last_cell_index;
       cell_index++, cell_base += 32 * kPointerSize) {
    ASSERT((unsigned)cell_index ==
        Bitmap::IndexToCell(
            Bitmap::CellAlignIndex(
                p->AddressToMarkbitIndex(cell_base))));

    const MarkBit::CellType current_cell = cells[cell_index];
    if (current_cell == 0) continue;

    const MarkBit::CellType next_cell = cells[cell_index + 1];
    MarkBit::CellType grey_objects = current_cell &
        ((current_cell >> 1) | (next_cell << (Bitmap::kBitsPerCell - 1)));

    int offset = 0;
    while (grey_objects != 0) {
      int trailing_zeros = CompilerIntrinsics::CountTrailingZeros(grey_objects);
      grey_objects >>= trailing_zeros;
      offset += trailing_zeros;
      MarkBit markbit(&cells[cell_index], 1 << offset, false);
      ASSERT(Marking::IsGrey(markbit));
      Marking::GreyToBlack(markbit);
      Address addr = cell_base + offset * kPointerSize;
      HeapObject* object = HeapObject::FromAddress(addr);
      MemoryChunk::IncrementLiveBytes(object->address(), object->Size());
      marking_deque->PushBlack(object);
      if (marking_deque->IsFull()) return;
      offset += 2;
      grey_objects >>= 2;
    }

    grey_objects >>= (Bitmap::kBitsPerCell - 1);
  }
}


static void DiscoverGreyObjectsInSpace(Heap* heap,
                                       MarkingDeque* marking_deque,
                                       PagedSpace* space) {
  if (!space->was_swept_conservatively()) {
    HeapObjectIterator it(space);
    DiscoverGreyObjectsWithIterator(heap, marking_deque, &it);
  } else {
    PageIterator it(space);
    while (it.has_next()) {
      Page* p = it.next();
      DiscoverGreyObjectsOnPage(marking_deque, p);
      if (marking_deque->IsFull()) return;
    }
  }
}


bool MarkCompactCollector::IsUnmarkedHeapObject(Object** p) {
  Object* o = *p;
  if (!o->IsHeapObject()) return false;
  HeapObject* heap_object = HeapObject::cast(o);
  MarkBit mark = Marking::MarkBitFrom(heap_object);
  return !mark.Get();
}


void MarkCompactCollector::MarkSymbolTable() {
  SymbolTable* symbol_table = heap()->symbol_table();
  // Mark the symbol table itself.
  MarkBit symbol_table_mark = Marking::MarkBitFrom(symbol_table);
  SetMark(symbol_table, symbol_table_mark);
  // Explicitly mark the prefix.
  MarkingVisitor marker(heap());
  symbol_table->IteratePrefix(&marker);
  ProcessMarkingDeque();
}


void MarkCompactCollector::MarkRoots(RootMarkingVisitor* visitor) {
  // Mark the heap roots including global variables, stack variables,
  // etc., and all objects reachable from them.
  heap()->IterateStrongRoots(visitor, VISIT_ONLY_STRONG);

  // Handle the symbol table specially.
  MarkSymbolTable();

  // There may be overflowed objects in the heap.  Visit them now.
  while (marking_deque_.overflowed()) {
    RefillMarkingDeque();
    EmptyMarkingDeque();
  }
}


void MarkCompactCollector::MarkObjectGroups() {
  List<ObjectGroup*>* object_groups =
      heap()->isolate()->global_handles()->object_groups();

  int last = 0;
  for (int i = 0; i < object_groups->length(); i++) {
    ObjectGroup* entry = object_groups->at(i);
    ASSERT(entry != NULL);

    Object*** objects = entry->objects_;
    bool group_marked = false;
    for (size_t j = 0; j < entry->length_; j++) {
      Object* object = *objects[j];
      if (object->IsHeapObject()) {
        HeapObject* heap_object = HeapObject::cast(object);
        MarkBit mark = Marking::MarkBitFrom(heap_object);
        if (mark.Get()) {
          group_marked = true;
          break;
        }
      }
    }

    if (!group_marked) {
      (*object_groups)[last++] = entry;
      continue;
    }

    // An object in the group is marked, so mark as grey all white heap
    // objects in the group.
    for (size_t j = 0; j < entry->length_; ++j) {
      Object* object = *objects[j];
      if (object->IsHeapObject()) {
        HeapObject* heap_object = HeapObject::cast(object);
        MarkBit mark = Marking::MarkBitFrom(heap_object);
        MarkObject(heap_object, mark);
      }
    }

    // Once the entire group has been colored grey, set the object group
    // to NULL so it won't be processed again.
    entry->Dispose();
    object_groups->at(i) = NULL;
  }
  object_groups->Rewind(last);
}


void MarkCompactCollector::MarkImplicitRefGroups() {
  List<ImplicitRefGroup*>* ref_groups =
      heap()->isolate()->global_handles()->implicit_ref_groups();

  int last = 0;
  for (int i = 0; i < ref_groups->length(); i++) {
    ImplicitRefGroup* entry = ref_groups->at(i);
    ASSERT(entry != NULL);

    if (!IsMarked(*entry->parent_)) {
      (*ref_groups)[last++] = entry;
      continue;
    }

    Object*** children = entry->children_;
    // A parent object is marked, so mark all child heap objects.
    for (size_t j = 0; j < entry->length_; ++j) {
      if ((*children[j])->IsHeapObject()) {
        HeapObject* child = HeapObject::cast(*children[j]);
        MarkBit mark = Marking::MarkBitFrom(child);
        MarkObject(child, mark);
      }
    }

    // Once the entire group has been marked, dispose it because it's
    // not needed anymore.
    entry->Dispose();
  }
  ref_groups->Rewind(last);
}


// Mark all objects reachable from the objects on the marking stack.
// Before: the marking stack contains zero or more heap object pointers.
// After: the marking stack is empty, and all objects reachable from the
// marking stack have been marked, or are overflowed in the heap.
void MarkCompactCollector::EmptyMarkingDeque() {
  while (!marking_deque_.IsEmpty()) {
    while (!marking_deque_.IsEmpty()) {
      HeapObject* object = marking_deque_.Pop();
      ASSERT(object->IsHeapObject());
      ASSERT(heap()->Contains(object));
      ASSERT(Marking::IsBlack(Marking::MarkBitFrom(object)));

      Map* map = object->map();
      MarkBit map_mark = Marking::MarkBitFrom(map);
      MarkObject(map, map_mark);

      StaticMarkingVisitor::IterateBody(map, object);
    }

    // Process encountered weak maps, mark objects only reachable by those
    // weak maps and repeat until fix-point is reached.
    ProcessWeakMaps();
  }
}


// Sweep the heap for overflowed objects, clear their overflow bits, and
// push them on the marking stack.  Stop early if the marking stack fills
// before sweeping completes.  If sweeping completes, there are no remaining
// overflowed objects in the heap so the overflow flag on the markings stack
// is cleared.
void MarkCompactCollector::RefillMarkingDeque() {
  ASSERT(marking_deque_.overflowed());

  SemiSpaceIterator new_it(heap()->new_space());
  DiscoverGreyObjectsWithIterator(heap(), &marking_deque_, &new_it);
  if (marking_deque_.IsFull()) return;

  DiscoverGreyObjectsInSpace(heap(),
                             &marking_deque_,
                             heap()->old_pointer_space());
  if (marking_deque_.IsFull()) return;

  DiscoverGreyObjectsInSpace(heap(),
                             &marking_deque_,
                             heap()->old_data_space());
  if (marking_deque_.IsFull()) return;

  DiscoverGreyObjectsInSpace(heap(),
                             &marking_deque_,
                             heap()->code_space());
  if (marking_deque_.IsFull()) return;

  DiscoverGreyObjectsInSpace(heap(),
                             &marking_deque_,
                             heap()->map_space());
  if (marking_deque_.IsFull()) return;

  DiscoverGreyObjectsInSpace(heap(),
                             &marking_deque_,
                             heap()->cell_space());
  if (marking_deque_.IsFull()) return;

  LargeObjectIterator lo_it(heap()->lo_space());
  DiscoverGreyObjectsWithIterator(heap(),
                                  &marking_deque_,
                                  &lo_it);
  if (marking_deque_.IsFull()) return;

  marking_deque_.ClearOverflowed();
}


// Mark all objects reachable (transitively) from objects on the marking
// stack.  Before: the marking stack contains zero or more heap object
// pointers.  After: the marking stack is empty and there are no overflowed
// objects in the heap.
void MarkCompactCollector::ProcessMarkingDeque() {
  EmptyMarkingDeque();
  while (marking_deque_.overflowed()) {
    RefillMarkingDeque();
    EmptyMarkingDeque();
  }
}


void MarkCompactCollector::ProcessExternalMarking() {
  bool work_to_do = true;
  ASSERT(marking_deque_.IsEmpty());
  while (work_to_do) {
    MarkObjectGroups();
    MarkImplicitRefGroups();
    work_to_do = !marking_deque_.IsEmpty();
    ProcessMarkingDeque();
  }
}


void MarkCompactCollector::MarkLiveObjects() {
  GCTracer::Scope gc_scope(tracer_, GCTracer::Scope::MC_MARK);
  // The recursive GC marker detects when it is nearing stack overflow,
  // and switches to a different marking system.  JS interrupts interfere
  // with the C stack limit check.
  PostponeInterruptsScope postpone(heap()->isolate());

  bool incremental_marking_overflowed = false;
  IncrementalMarking* incremental_marking = heap_->incremental_marking();
  if (was_marked_incrementally_) {
    // Finalize the incremental marking and check whether we had an overflow.
    // Both markers use grey color to mark overflowed objects so
    // non-incremental marker can deal with them as if overflow
    // occured during normal marking.
    // But incremental marker uses a separate marking deque
    // so we have to explicitly copy it's overflow state.
    incremental_marking->Finalize();
    incremental_marking_overflowed =
        incremental_marking->marking_deque()->overflowed();
    incremental_marking->marking_deque()->ClearOverflowed();
  } else {
    // Abort any pending incremental activities e.g. incremental sweeping.
    incremental_marking->Abort();
  }

#ifdef DEBUG
  ASSERT(state_ == PREPARE_GC);
  state_ = MARK_LIVE_OBJECTS;
#endif
  // The to space contains live objects, a page in from space is used as a
  // marking stack.
  Address marking_deque_start = heap()->new_space()->FromSpacePageLow();
  Address marking_deque_end = heap()->new_space()->FromSpacePageHigh();
  if (FLAG_force_marking_deque_overflows) {
    marking_deque_end = marking_deque_start + 64 * kPointerSize;
  }
  marking_deque_.Initialize(marking_deque_start,
                            marking_deque_end);
  ASSERT(!marking_deque_.overflowed());

  if (incremental_marking_overflowed) {
    // There are overflowed objects left in the heap after incremental marking.
    marking_deque_.SetOverflowed();
  }

  PrepareForCodeFlushing();

  RootMarkingVisitor root_visitor(heap());
  MarkRoots(&root_visitor);

  // The objects reachable from the roots are marked, yet unreachable
  // objects are unmarked.  Mark objects reachable due to host
  // application specific logic.
  ProcessExternalMarking();

  // The objects reachable from the roots or object groups are marked,
  // yet unreachable objects are unmarked.  Mark objects reachable
  // only from weak global handles.
  //
  // First we identify nonlive weak handles and mark them as pending
  // destruction.
  heap()->isolate()->global_handles()->IdentifyWeakHandles(
      &IsUnmarkedHeapObject);
  // Then we mark the objects and process the transitive closure.
  heap()->isolate()->global_handles()->IterateWeakRoots(&root_visitor);
  while (marking_deque_.overflowed()) {
    RefillMarkingDeque();
    EmptyMarkingDeque();
  }

  // Repeat host application specific marking to mark unmarked objects
  // reachable from the weak roots.
  ProcessExternalMarking();

  AfterMarking();
}


void MarkCompactCollector::AfterMarking() {
  // Object literal map caches reference symbols (cache keys) and maps
  // (cache values). At this point still useful maps have already been
  // marked. Mark the keys for the alive values before we process the
  // symbol table.
  ProcessMapCaches();

  // Prune the symbol table removing all symbols only pointed to by the
  // symbol table.  Cannot use symbol_table() here because the symbol
  // table is marked.
  SymbolTable* symbol_table = heap()->symbol_table();
  SymbolTableCleaner v(heap());
  symbol_table->IterateElements(&v);
  symbol_table->ElementsRemoved(v.PointersRemoved());
  heap()->external_string_table_.Iterate(&v);
  heap()->external_string_table_.CleanUp();

  // Process the weak references.
  MarkCompactWeakObjectRetainer mark_compact_object_retainer;
  heap()->ProcessWeakReferences(&mark_compact_object_retainer);

  // Remove object groups after marking phase.
  heap()->isolate()->global_handles()->RemoveObjectGroups();
  heap()->isolate()->global_handles()->RemoveImplicitRefGroups();

  // Flush code from collected candidates.
  if (is_code_flushing_enabled()) {
    code_flusher_->ProcessCandidates();
  }

  // Clean up dead objects from the runtime profiler.
  heap()->isolate()->runtime_profiler()->RemoveDeadSamples();
}


void MarkCompactCollector::ProcessMapCaches() {
  Object* raw_context = heap()->global_contexts_list_;
  while (raw_context != heap()->undefined_value()) {
    Context* context = reinterpret_cast<Context*>(raw_context);
    if (IsMarked(context)) {
      HeapObject* raw_map_cache =
          HeapObject::cast(context->get(Context::MAP_CACHE_INDEX));
      // A map cache may be reachable from the stack. In this case
      // it's already transitively marked and it's too late to clean
      // up its parts.
      if (!IsMarked(raw_map_cache) &&
          raw_map_cache != heap()->undefined_value()) {
        MapCache* map_cache = reinterpret_cast<MapCache*>(raw_map_cache);
        int existing_elements = map_cache->NumberOfElements();
        int used_elements = 0;
        for (int i = MapCache::kElementsStartIndex;
             i < map_cache->length();
             i += MapCache::kEntrySize) {
          Object* raw_key = map_cache->get(i);
          if (raw_key == heap()->undefined_value() ||
              raw_key == heap()->null_value()) continue;
          STATIC_ASSERT(MapCache::kEntrySize == 2);
          Object* raw_map = map_cache->get(i + 1);
          if (raw_map->IsHeapObject() && IsMarked(raw_map)) {
            ++used_elements;
          } else {
            // Delete useless entries with unmarked maps.
            ASSERT(raw_map->IsMap());
            map_cache->set_null_unchecked(heap(), i);
            map_cache->set_null_unchecked(heap(), i + 1);
          }
        }
        if (used_elements == 0) {
          context->set(Context::MAP_CACHE_INDEX, heap()->undefined_value());
        } else {
          // Note: we don't actually shrink the cache here to avoid
          // extra complexity during GC. We rely on subsequent cache
          // usages (EnsureCapacity) to do this.
          map_cache->ElementsRemoved(existing_elements - used_elements);
          MarkBit map_cache_markbit = Marking::MarkBitFrom(map_cache);
          MarkObject(map_cache, map_cache_markbit);
        }
      }
    }
    // Move to next element in the list.
    raw_context = context->get(Context::NEXT_CONTEXT_LINK);
  }
  ProcessMarkingDeque();
}


#ifdef DEBUG
void MarkCompactCollector::UpdateLiveObjectCount(HeapObject* obj) {
  live_bytes_ += obj->Size();
  if (heap()->new_space()->Contains(obj)) {
    live_young_objects_size_ += obj->Size();
  } else if (heap()->map_space()->Contains(obj)) {
    ASSERT(obj->IsMap());
    live_map_objects_size_ += obj->Size();
  } else if (heap()->cell_space()->Contains(obj)) {
    ASSERT(obj->IsJSGlobalPropertyCell());
    live_cell_objects_size_ += obj->Size();
  } else if (heap()->old_pointer_space()->Contains(obj)) {
    live_old_pointer_objects_size_ += obj->Size();
  } else if (heap()->old_data_space()->Contains(obj)) {
    live_old_data_objects_size_ += obj->Size();
  } else if (heap()->code_space()->Contains(obj)) {
    live_code_objects_size_ += obj->Size();
  } else if (heap()->lo_space()->Contains(obj)) {
    live_lo_objects_size_ += obj->Size();
  } else {
    UNREACHABLE();
  }
}
#endif  // DEBUG


void MarkCompactCollector::ReattachInitialMaps() {
  HeapObjectIterator map_iterator(heap()->map_space());
  for (HeapObject* obj = map_iterator.Next();
       obj != NULL;
       obj = map_iterator.Next()) {
    if (obj->IsFreeSpace()) continue;
    Map* map = Map::cast(obj);

    STATIC_ASSERT(LAST_TYPE == LAST_JS_RECEIVER_TYPE);
    if (map->instance_type() < FIRST_JS_RECEIVER_TYPE) continue;

    if (map->attached_to_shared_function_info()) {
      JSFunction::cast(map->constructor())->shared()->AttachInitialMap(map);
    }
  }
}


void MarkCompactCollector::ClearNonLiveTransitions() {
  HeapObjectIterator map_iterator(heap()->map_space());
  // Iterate over the map space, setting map transitions that go from
  // a marked map to an unmarked map to null transitions.  At the same time,
  // set all the prototype fields of maps back to their original value,
  // dropping the back pointers temporarily stored in the prototype field.
  // Setting the prototype field requires following the linked list of
  // back pointers, reversing them all at once.  This allows us to find
  // those maps with map transitions that need to be nulled, and only
  // scan the descriptor arrays of those maps, not all maps.
  // All of these actions are carried out only on maps of JSObjects
  // and related subtypes.
  for (HeapObject* obj = map_iterator.Next();
       obj != NULL; obj = map_iterator.Next()) {
    Map* map = reinterpret_cast<Map*>(obj);
    MarkBit map_mark = Marking::MarkBitFrom(map);
    if (map->IsFreeSpace()) continue;

    ASSERT(map->IsMap());
    // Only JSObject and subtypes have map transitions and back pointers.
    STATIC_ASSERT(LAST_TYPE == LAST_JS_OBJECT_TYPE);
    if (map->instance_type() < FIRST_JS_OBJECT_TYPE) continue;

    if (map_mark.Get() &&
        map->attached_to_shared_function_info()) {
      // This map is used for inobject slack tracking and has been detached
      // from SharedFunctionInfo during the mark phase.
      // Since it survived the GC, reattach it now.
      map->unchecked_constructor()->unchecked_shared()->AttachInitialMap(map);
    }

    // Clear dead prototype transitions.
    int number_of_transitions = map->NumberOfProtoTransitions();
    FixedArray* prototype_transitions = map->prototype_transitions();

    int new_number_of_transitions = 0;
    const int header = Map::kProtoTransitionHeaderSize;
    const int proto_offset =
        header + Map::kProtoTransitionPrototypeOffset;
    const int map_offset = header + Map::kProtoTransitionMapOffset;
    const int step = Map::kProtoTransitionElementsPerEntry;
    for (int i = 0; i < number_of_transitions; i++) {
      Object* prototype = prototype_transitions->get(proto_offset + i * step);
      Object* cached_map = prototype_transitions->get(map_offset + i * step);
      if (IsMarked(prototype) && IsMarked(cached_map)) {
        if (new_number_of_transitions != i) {
          prototype_transitions->set_unchecked(
              heap_,
              proto_offset + new_number_of_transitions * step,
              prototype,
              UPDATE_WRITE_BARRIER);
          prototype_transitions->set_unchecked(
              heap_,
              map_offset + new_number_of_transitions * step,
              cached_map,
              SKIP_WRITE_BARRIER);
        }
      }

      // Fill slots that became free with undefined value.
      Object* undefined = heap()->undefined_value();
      for (int i = new_number_of_transitions * step;
           i < number_of_transitions * step;
           i++) {
        // The undefined object is on a page that is never compacted and never
        // in new space so it is OK to skip the write barrier.  Also it's a
        // root.
        prototype_transitions->set_unchecked(heap_,
                                             header + i,
                                             undefined,
                                             SKIP_WRITE_BARRIER);

        Object** undefined_slot =
            prototype_transitions->data_start() + i;
        RecordSlot(undefined_slot, undefined_slot, undefined);
      }
      map->SetNumberOfProtoTransitions(new_number_of_transitions);
    }

    // Follow the chain of back pointers to find the prototype.
    Map* current = map;
    while (current->IsMap()) {
      current = reinterpret_cast<Map*>(current->prototype());
      ASSERT(current->IsHeapObject());
    }
    Object* real_prototype = current;

    // Follow back pointers, setting them to prototype,
    // clearing map transitions when necessary.
    current = map;
    bool on_dead_path = !map_mark.Get();
    Object* next;
    while (current->IsMap()) {
      next = current->prototype();
      // There should never be a dead map above a live map.
      MarkBit current_mark = Marking::MarkBitFrom(current);
      bool is_alive = current_mark.Get();
      ASSERT(on_dead_path || is_alive);

      // A live map above a dead map indicates a dead transition.
      // This test will always be false on the first iteration.
      if (on_dead_path && is_alive) {
        on_dead_path = false;
        current->ClearNonLiveTransitions(heap(), real_prototype);
      }
      *HeapObject::RawField(current, Map::kPrototypeOffset) =
          real_prototype;

      if (is_alive) {
        Object** slot = HeapObject::RawField(current, Map::kPrototypeOffset);
        RecordSlot(slot, slot, real_prototype);
      }
      current = reinterpret_cast<Map*>(next);
    }
  }
}


void MarkCompactCollector::ProcessWeakMaps() {
  Object* weak_map_obj = encountered_weak_maps();
  while (weak_map_obj != Smi::FromInt(0)) {
    ASSERT(MarkCompactCollector::IsMarked(HeapObject::cast(weak_map_obj)));
    JSWeakMap* weak_map = reinterpret_cast<JSWeakMap*>(weak_map_obj);
    ObjectHashTable* table = weak_map->unchecked_table();
    for (int i = 0; i < table->Capacity(); i++) {
      if (MarkCompactCollector::IsMarked(HeapObject::cast(table->KeyAt(i)))) {
        Object* value = table->get(table->EntryToValueIndex(i));
        StaticMarkingVisitor::VisitPointer(heap(), &value);
        table->set_unchecked(heap(),
                             table->EntryToValueIndex(i),
                             value,
                             UPDATE_WRITE_BARRIER);
      }
    }
    weak_map_obj = weak_map->next();
  }
}


void MarkCompactCollector::ClearWeakMaps() {
  Object* weak_map_obj = encountered_weak_maps();
  while (weak_map_obj != Smi::FromInt(0)) {
    ASSERT(MarkCompactCollector::IsMarked(HeapObject::cast(weak_map_obj)));
    JSWeakMap* weak_map = reinterpret_cast<JSWeakMap*>(weak_map_obj);
    ObjectHashTable* table = weak_map->unchecked_table();
    for (int i = 0; i < table->Capacity(); i++) {
      if (!MarkCompactCollector::IsMarked(HeapObject::cast(table->KeyAt(i)))) {
        table->RemoveEntry(i, heap());
      }
    }
    weak_map_obj = weak_map->next();
    weak_map->set_next(Smi::FromInt(0));
  }
  set_encountered_weak_maps(Smi::FromInt(0));
}


// We scavange new space simultaneously with sweeping. This is done in two
// passes.
//
// The first pass migrates all alive objects from one semispace to another or
// promotes them to old space.  Forwarding address is written directly into
// first word of object without any encoding.  If object is dead we write
// NULL as a forwarding address.
//
// The second pass updates pointers to new space in all spaces.  It is possible
// to encounter pointers to dead new space objects during traversal of pointers
// to new space.  We should clear them to avoid encountering them during next
// pointer iteration.  This is an issue if the store buffer overflows and we
// have to scan the entire old space, including dead objects, looking for
// pointers to new space.
void MarkCompactCollector::MigrateObject(Address dst,
                                         Address src,
                                         int size,
                                         AllocationSpace dest) {
  HEAP_PROFILE(heap(), ObjectMoveEvent(src, dst));
  if (dest == OLD_POINTER_SPACE || dest == LO_SPACE) {
    Address src_slot = src;
    Address dst_slot = dst;
    ASSERT(IsAligned(size, kPointerSize));

    for (int remaining = size / kPointerSize; remaining > 0; remaining--) {
      Object* value = Memory::Object_at(src_slot);

      Memory::Object_at(dst_slot) = value;

      if (heap_->InNewSpace(value)) {
        heap_->store_buffer()->Mark(dst_slot);
      } else if (value->IsHeapObject() && IsOnEvacuationCandidate(value)) {
        SlotsBuffer::AddTo(&slots_buffer_allocator_,
                           &migration_slots_buffer_,
                           reinterpret_cast<Object**>(dst_slot),
                           SlotsBuffer::IGNORE_OVERFLOW);
      }

      src_slot += kPointerSize;
      dst_slot += kPointerSize;
    }

    if (compacting_ && HeapObject::FromAddress(dst)->IsJSFunction()) {
      Address code_entry_slot = dst + JSFunction::kCodeEntryOffset;
      Address code_entry = Memory::Address_at(code_entry_slot);

      if (Page::FromAddress(code_entry)->IsEvacuationCandidate()) {
        SlotsBuffer::AddTo(&slots_buffer_allocator_,
                           &migration_slots_buffer_,
                           SlotsBuffer::CODE_ENTRY_SLOT,
                           code_entry_slot,
                           SlotsBuffer::IGNORE_OVERFLOW);
      }
    }
  } else if (dest == CODE_SPACE) {
    PROFILE(heap()->isolate(), CodeMoveEvent(src, dst));
    heap()->MoveBlock(dst, src, size);
    SlotsBuffer::AddTo(&slots_buffer_allocator_,
                       &migration_slots_buffer_,
                       SlotsBuffer::RELOCATED_CODE_OBJECT,
                       dst,
                       SlotsBuffer::IGNORE_OVERFLOW);
    Code::cast(HeapObject::FromAddress(dst))->Relocate(dst - src);
  } else {
    ASSERT(dest == OLD_DATA_SPACE || dest == NEW_SPACE);
    heap()->MoveBlock(dst, src, size);
  }
  Memory::Address_at(src) = dst;
}


// Visitor for updating pointers from live objects in old spaces to new space.
// It does not expect to encounter pointers to dead objects.
class PointersUpdatingVisitor: public ObjectVisitor {
 public:
  explicit PointersUpdatingVisitor(Heap* heap) : heap_(heap) { }

  void VisitPointer(Object** p) {
    UpdatePointer(p);
  }

  void VisitPointers(Object** start, Object** end) {
    for (Object** p = start; p < end; p++) UpdatePointer(p);
  }

  void VisitEmbeddedPointer(RelocInfo* rinfo) {
    ASSERT(rinfo->rmode() == RelocInfo::EMBEDDED_OBJECT);
    Object* target = rinfo->target_object();
    VisitPointer(&target);
    rinfo->set_target_object(target);
  }

  void VisitCodeTarget(RelocInfo* rinfo) {
    ASSERT(RelocInfo::IsCodeTarget(rinfo->rmode()));
    Object* target = Code::GetCodeFromTargetAddress(rinfo->target_address());
    VisitPointer(&target);
    rinfo->set_target_address(Code::cast(target)->instruction_start());
  }

  void VisitDebugTarget(RelocInfo* rinfo) {
    ASSERT((RelocInfo::IsJSReturn(rinfo->rmode()) &&
            rinfo->IsPatchedReturnSequence()) ||
           (RelocInfo::IsDebugBreakSlot(rinfo->rmode()) &&
            rinfo->IsPatchedDebugBreakSlotSequence()));
    Object* target = Code::GetCodeFromTargetAddress(rinfo->call_address());
    VisitPointer(&target);
    rinfo->set_call_address(Code::cast(target)->instruction_start());
  }

  static inline void UpdateSlot(Heap* heap, Object** slot) {
    Object* obj = *slot;

    if (!obj->IsHeapObject()) return;

    HeapObject* heap_obj = HeapObject::cast(obj);

    MapWord map_word = heap_obj->map_word();
    if (map_word.IsForwardingAddress()) {
      ASSERT(heap->InFromSpace(heap_obj) ||
             MarkCompactCollector::IsOnEvacuationCandidate(heap_obj));
      HeapObject* target = map_word.ToForwardingAddress();
      *slot = target;
      ASSERT(!heap->InFromSpace(target) &&
             !MarkCompactCollector::IsOnEvacuationCandidate(target));
    }
  }

 private:
  inline void UpdatePointer(Object** p) {
    UpdateSlot(heap_, p);
  }

  Heap* heap_;
};


static void UpdatePointer(HeapObject** p, HeapObject* object) {
  ASSERT(*p == object);

  Address old_addr = object->address();

  Address new_addr = Memory::Address_at(old_addr);

  // The new space sweep will overwrite the map word of dead objects
  // with NULL. In this case we do not need to transfer this entry to
  // the store buffer which we are rebuilding.
  if (new_addr != NULL) {
    *p = HeapObject::FromAddress(new_addr);
  } else {
    // We have to zap this pointer, because the store buffer may overflow later,
    // and then we have to scan the entire heap and we don't want to find
    // spurious newspace pointers in the old space.
    *p = reinterpret_cast<HeapObject*>(Smi::FromInt(0));
  }
}


static String* UpdateReferenceInExternalStringTableEntry(Heap* heap,
                                                         Object** p) {
  MapWord map_word = HeapObject::cast(*p)->map_word();

  if (map_word.IsForwardingAddress()) {
    return String::cast(map_word.ToForwardingAddress());
  }

  return String::cast(*p);
}


bool MarkCompactCollector::TryPromoteObject(HeapObject* object,
                                            int object_size) {
  Object* result;

  if (object_size > heap()->MaxObjectSizeInPagedSpace()) {
    MaybeObject* maybe_result =
        heap()->lo_space()->AllocateRaw(object_size, NOT_EXECUTABLE);
    if (maybe_result->ToObject(&result)) {
      HeapObject* target = HeapObject::cast(result);
      MigrateObject(target->address(),
                    object->address(),
                    object_size,
                    LO_SPACE);
      heap()->mark_compact_collector()->tracer()->
          increment_promoted_objects_size(object_size);
      return true;
    }
  } else {
    OldSpace* target_space = heap()->TargetSpace(object);

    ASSERT(target_space == heap()->old_pointer_space() ||
           target_space == heap()->old_data_space());
    MaybeObject* maybe_result = target_space->AllocateRaw(object_size);
    if (maybe_result->ToObject(&result)) {
      HeapObject* target = HeapObject::cast(result);
      MigrateObject(target->address(),
                    object->address(),
                    object_size,
                    target_space->identity());
      heap()->mark_compact_collector()->tracer()->
          increment_promoted_objects_size(object_size);
      return true;
    }
  }

  return false;
}


void MarkCompactCollector::EvacuateNewSpace() {
  heap()->CheckNewSpaceExpansionCriteria();

  NewSpace* new_space = heap()->new_space();

  // Store allocation range before flipping semispaces.
  Address from_bottom = new_space->bottom();
  Address from_top = new_space->top();

  // Flip the semispaces.  After flipping, to space is empty, from space has
  // live objects.
  new_space->Flip();
  new_space->ResetAllocationInfo();

  int survivors_size = 0;

  // First pass: traverse all objects in inactive semispace, remove marks,
  // migrate live objects and write forwarding addresses.  This stage puts
  // new entries in the store buffer and may cause some pages to be marked
  // scan-on-scavenge.
  SemiSpaceIterator from_it(from_bottom, from_top);
  for (HeapObject* object = from_it.Next();
       object != NULL;
       object = from_it.Next()) {
    MarkBit mark_bit = Marking::MarkBitFrom(object);
    if (mark_bit.Get()) {
      mark_bit.Clear();
      // Don't bother decrementing live bytes count. We'll discard the
      // entire page at the end.
      int size = object->Size();
      survivors_size += size;

      // Aggressively promote young survivors to the old space.
      if (TryPromoteObject(object, size)) {
        continue;
      }

      // Promotion failed. Just migrate object to another semispace.
      MaybeObject* allocation = new_space->AllocateRaw(size);
      if (allocation->IsFailure()) {
        if (!new_space->AddFreshPage()) {
          // Shouldn't happen. We are sweeping linearly, and to-space
          // has the same number of pages as from-space, so there is
          // always room.
          UNREACHABLE();
        }
        allocation = new_space->AllocateRaw(size);
        ASSERT(!allocation->IsFailure());
      }
      Object* target = allocation->ToObjectUnchecked();

      MigrateObject(HeapObject::cast(target)->address(),
                    object->address(),
                    size,
                    NEW_SPACE);
    } else {
      // Process the dead object before we write a NULL into its header.
      LiveObjectList::ProcessNonLive(object);

      // Mark dead objects in the new space with null in their map field.
      Memory::Address_at(object->address()) = NULL;
    }
  }

  heap_->IncrementYoungSurvivorsCounter(survivors_size);
  new_space->set_age_mark(new_space->top());
}


void MarkCompactCollector::EvacuateLiveObjectsFromPage(Page* p) {
  AlwaysAllocateScope always_allocate;
  PagedSpace* space = static_cast<PagedSpace*>(p->owner());
  ASSERT(p->IsEvacuationCandidate() && !p->WasSwept());
  MarkBit::CellType* cells = p->markbits()->cells();
  p->MarkSweptPrecisely();

  int last_cell_index =
      Bitmap::IndexToCell(
          Bitmap::CellAlignIndex(
              p->AddressToMarkbitIndex(p->ObjectAreaEnd())));

  int cell_index = Page::kFirstUsedCell;
  Address cell_base = p->ObjectAreaStart();
  int offsets[16];

  for (cell_index = Page::kFirstUsedCell;
       cell_index < last_cell_index;
       cell_index++, cell_base += 32 * kPointerSize) {
    ASSERT((unsigned)cell_index ==
        Bitmap::IndexToCell(
            Bitmap::CellAlignIndex(
                p->AddressToMarkbitIndex(cell_base))));
    if (cells[cell_index] == 0) continue;

    int live_objects = MarkWordToObjectStarts(cells[cell_index], offsets);
    for (int i = 0; i < live_objects; i++) {
      Address object_addr = cell_base + offsets[i] * kPointerSize;
      HeapObject* object = HeapObject::FromAddress(object_addr);
      ASSERT(Marking::IsBlack(Marking::MarkBitFrom(object)));

      int size = object->Size();

      MaybeObject* target = space->AllocateRaw(size);
      if (target->IsFailure()) {
        // OS refused to give us memory.
        V8::FatalProcessOutOfMemory("Evacuation");
        return;
      }

      Object* target_object = target->ToObjectUnchecked();

      MigrateObject(HeapObject::cast(target_object)->address(),
                    object_addr,
                    size,
                    space->identity());
      ASSERT(object->map_word().IsForwardingAddress());
    }

    // Clear marking bits for current cell.
    cells[cell_index] = 0;
  }
  p->ResetLiveBytes();
}


void MarkCompactCollector::EvacuatePages() {
  int npages = evacuation_candidates_.length();
  for (int i = 0; i < npages; i++) {
    Page* p = evacuation_candidates_[i];
    ASSERT(p->IsEvacuationCandidate() ||
           p->IsFlagSet(Page::RESCAN_ON_EVACUATION));
    if (p->IsEvacuationCandidate()) {
      // During compaction we might have to request a new page.
      // Check that space still have room for that.
      if (static_cast<PagedSpace*>(p->owner())->CanExpand()) {
        EvacuateLiveObjectsFromPage(p);
      } else {
        // Without room for expansion evacuation is not guaranteed to succeed.
        // Pessimistically abandon unevacuated pages.
        for (int j = i; j < npages; j++) {
          Page* page = evacuation_candidates_[j];
          slots_buffer_allocator_.DeallocateChain(page->slots_buffer_address());
          page->ClearEvacuationCandidate();
          page->SetFlag(Page::RESCAN_ON_EVACUATION);
        }
        return;
      }
    }
  }
}


class EvacuationWeakObjectRetainer : public WeakObjectRetainer {
 public:
  virtual Object* RetainAs(Object* object) {
    if (object->IsHeapObject()) {
      HeapObject* heap_object = HeapObject::cast(object);
      MapWord map_word = heap_object->map_word();
      if (map_word.IsForwardingAddress()) {
        return map_word.ToForwardingAddress();
      }
    }
    return object;
  }
};


static inline void UpdateSlot(ObjectVisitor* v,
                              SlotsBuffer::SlotType slot_type,
                              Address addr) {
  switch (slot_type) {
    case SlotsBuffer::CODE_TARGET_SLOT: {
      RelocInfo rinfo(addr, RelocInfo::CODE_TARGET, 0, NULL);
      rinfo.Visit(v);
      break;
    }
    case SlotsBuffer::CODE_ENTRY_SLOT: {
      v->VisitCodeEntry(addr);
      break;
    }
    case SlotsBuffer::RELOCATED_CODE_OBJECT: {
      HeapObject* obj = HeapObject::FromAddress(addr);
      Code::cast(obj)->CodeIterateBody(v);
      break;
    }
    case SlotsBuffer::DEBUG_TARGET_SLOT: {
      RelocInfo rinfo(addr, RelocInfo::DEBUG_BREAK_SLOT, 0, NULL);
      if (rinfo.IsPatchedDebugBreakSlotSequence()) rinfo.Visit(v);
      break;
    }
    case SlotsBuffer::JS_RETURN_SLOT: {
      RelocInfo rinfo(addr, RelocInfo::JS_RETURN, 0, NULL);
      if (rinfo.IsPatchedReturnSequence()) rinfo.Visit(v);
      break;
    }
    case SlotsBuffer::EMBEDDED_OBJECT_SLOT: {
      RelocInfo rinfo(addr, RelocInfo::EMBEDDED_OBJECT, 0, NULL);
      rinfo.Visit(v);
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}


enum SweepingMode {
  SWEEP_ONLY,
  SWEEP_AND_VISIT_LIVE_OBJECTS
};


enum SkipListRebuildingMode {
  REBUILD_SKIP_LIST,
  IGNORE_SKIP_LIST
};


// Sweep a space precisely.  After this has been done the space can
// be iterated precisely, hitting only the live objects.  Code space
// is always swept precisely because we want to be able to iterate
// over it.  Map space is swept precisely, because it is not compacted.
// Slots in live objects pointing into evacuation candidates are updated
// if requested.
template<SweepingMode sweeping_mode, SkipListRebuildingMode skip_list_mode>
static void SweepPrecisely(PagedSpace* space,
                           Page* p,
                           ObjectVisitor* v) {
  ASSERT(!p->IsEvacuationCandidate() && !p->WasSwept());
  ASSERT_EQ(skip_list_mode == REBUILD_SKIP_LIST,
            space->identity() == CODE_SPACE);
  ASSERT((p->skip_list() == NULL) || (skip_list_mode == REBUILD_SKIP_LIST));

  MarkBit::CellType* cells = p->markbits()->cells();
  p->MarkSweptPrecisely();

  int last_cell_index =
      Bitmap::IndexToCell(
          Bitmap::CellAlignIndex(
              p->AddressToMarkbitIndex(p->ObjectAreaEnd())));

  int cell_index = Page::kFirstUsedCell;
  Address free_start = p->ObjectAreaStart();
  ASSERT(reinterpret_cast<intptr_t>(free_start) % (32 * kPointerSize) == 0);
  Address object_address = p->ObjectAreaStart();
  int offsets[16];

  SkipList* skip_list = p->skip_list();
  int curr_region = -1;
  if ((skip_list_mode == REBUILD_SKIP_LIST) && skip_list) {
    skip_list->Clear();
  }

  for (cell_index = Page::kFirstUsedCell;
       cell_index < last_cell_index;
       cell_index++, object_address += 32 * kPointerSize) {
    ASSERT((unsigned)cell_index ==
        Bitmap::IndexToCell(
            Bitmap::CellAlignIndex(
                p->AddressToMarkbitIndex(object_address))));
    int live_objects = MarkWordToObjectStarts(cells[cell_index], offsets);
    int live_index = 0;
    for ( ; live_objects != 0; live_objects--) {
      Address free_end = object_address + offsets[live_index++] * kPointerSize;
      if (free_end != free_start) {
        space->Free(free_start, static_cast<int>(free_end - free_start));
      }
      HeapObject* live_object = HeapObject::FromAddress(free_end);
      ASSERT(Marking::IsBlack(Marking::MarkBitFrom(live_object)));
      Map* map = live_object->map();
      int size = live_object->SizeFromMap(map);
      if (sweeping_mode == SWEEP_AND_VISIT_LIVE_OBJECTS) {
        live_object->IterateBody(map->instance_type(), size, v);
      }
      if ((skip_list_mode == REBUILD_SKIP_LIST) && skip_list != NULL) {
        int new_region_start =
            SkipList::RegionNumber(free_end);
        int new_region_end =
            SkipList::RegionNumber(free_end + size - kPointerSize);
        if (new_region_start != curr_region ||
            new_region_end != curr_region) {
          skip_list->AddObject(free_end, size);
          curr_region = new_region_end;
        }
      }
      free_start = free_end + size;
    }
    // Clear marking bits for current cell.
    cells[cell_index] = 0;
  }
  if (free_start != p->ObjectAreaEnd()) {
    space->Free(free_start, static_cast<int>(p->ObjectAreaEnd() - free_start));
  }
  p->ResetLiveBytes();
}


static bool SetMarkBitsUnderInvalidatedCode(Code* code, bool value) {
  Page* p = Page::FromAddress(code->address());

  if (p->IsEvacuationCandidate() ||
      p->IsFlagSet(Page::RESCAN_ON_EVACUATION)) {
    return false;
  }

  Address code_start = code->address();
  Address code_end = code_start + code->Size();

  uint32_t start_index = MemoryChunk::FastAddressToMarkbitIndex(code_start);
  uint32_t end_index =
      MemoryChunk::FastAddressToMarkbitIndex(code_end - kPointerSize);

  Bitmap* b = p->markbits();

  MarkBit start_mark_bit = b->MarkBitFromIndex(start_index);
  MarkBit end_mark_bit = b->MarkBitFromIndex(end_index);

  MarkBit::CellType* start_cell = start_mark_bit.cell();
  MarkBit::CellType* end_cell = end_mark_bit.cell();

  if (value) {
    MarkBit::CellType start_mask = ~(start_mark_bit.mask() - 1);
    MarkBit::CellType end_mask = (end_mark_bit.mask() << 1) - 1;

    if (start_cell == end_cell) {
      *start_cell |= start_mask & end_mask;
    } else {
      *start_cell |= start_mask;
      for (MarkBit::CellType* cell = start_cell + 1; cell < end_cell; cell++) {
        *cell = ~0;
      }
      *end_cell |= end_mask;
    }
  } else {
    for (MarkBit::CellType* cell = start_cell ; cell <= end_cell; cell++) {
      *cell = 0;
    }
  }

  return true;
}


static bool IsOnInvalidatedCodeObject(Address addr) {
  // We did not record any slots in large objects thus
  // we can safely go to the page from the slot address.
  Page* p = Page::FromAddress(addr);

  // First check owner's identity because old pointer and old data spaces
  // are swept lazily and might still have non-zero mark-bits on some
  // pages.
  if (p->owner()->identity() != CODE_SPACE) return false;

  // In code space only bits on evacuation candidates (but we don't record
  // any slots on them) and under invalidated code objects are non-zero.
  MarkBit mark_bit =
      p->markbits()->MarkBitFromIndex(Page::FastAddressToMarkbitIndex(addr));

  return mark_bit.Get();
}


void MarkCompactCollector::InvalidateCode(Code* code) {
  if (heap_->incremental_marking()->IsCompacting() &&
      !ShouldSkipEvacuationSlotRecording(code)) {
    ASSERT(compacting_);

    // If the object is white than no slots were recorded on it yet.
    MarkBit mark_bit = Marking::MarkBitFrom(code);
    if (Marking::IsWhite(mark_bit)) return;

    invalidated_code_.Add(code);
  }
}


bool MarkCompactCollector::MarkInvalidatedCode() {
  bool code_marked = false;

  int length = invalidated_code_.length();
  for (int i = 0; i < length; i++) {
    Code* code = invalidated_code_[i];

    if (SetMarkBitsUnderInvalidatedCode(code, true)) {
      code_marked = true;
    }
  }

  return code_marked;
}


void MarkCompactCollector::RemoveDeadInvalidatedCode() {
  int length = invalidated_code_.length();
  for (int i = 0; i < length; i++) {
    if (!IsMarked(invalidated_code_[i])) invalidated_code_[i] = NULL;
  }
}


void MarkCompactCollector::ProcessInvalidatedCode(ObjectVisitor* visitor) {
  int length = invalidated_code_.length();
  for (int i = 0; i < length; i++) {
    Code* code = invalidated_code_[i];
    if (code != NULL) {
      code->Iterate(visitor);
      SetMarkBitsUnderInvalidatedCode(code, false);
    }
  }
  invalidated_code_.Rewind(0);
}


void MarkCompactCollector::EvacuateNewSpaceAndCandidates() {
  bool code_slots_filtering_required = MarkInvalidatedCode();

  EvacuateNewSpace();
  EvacuatePages();

  // Second pass: find pointers to new space and update them.
  PointersUpdatingVisitor updating_visitor(heap());

  // Update pointers in to space.
  SemiSpaceIterator to_it(heap()->new_space()->bottom(),
                          heap()->new_space()->top());
  for (HeapObject* object = to_it.Next();
       object != NULL;
       object = to_it.Next()) {
    Map* map = object->map();
    object->IterateBody(map->instance_type(),
                        object->SizeFromMap(map),
                        &updating_visitor);
  }

  // Update roots.
  heap_->IterateRoots(&updating_visitor, VISIT_ALL_IN_SWEEP_NEWSPACE);
  LiveObjectList::IterateElements(&updating_visitor);

  {
    StoreBufferRebuildScope scope(heap_,
                                  heap_->store_buffer(),
                                  &Heap::ScavengeStoreBufferCallback);
    heap_->store_buffer()->IteratePointersToNewSpace(&UpdatePointer);
  }

  SlotsBuffer::UpdateSlotsRecordedIn(heap_,
                                     migration_slots_buffer_,
                                     code_slots_filtering_required);
  if (FLAG_trace_fragmentation) {
    PrintF("  migration slots buffer: %d\n",
           SlotsBuffer::SizeOfChain(migration_slots_buffer_));
  }

  if (compacting_ && was_marked_incrementally_) {
    // It's difficult to filter out slots recorded for large objects.
    LargeObjectIterator it(heap_->lo_space());
    for (HeapObject* obj = it.Next(); obj != NULL; obj = it.Next()) {
      // LargeObjectSpace is not swept yet thus we have to skip
      // dead objects explicitly.
      if (!IsMarked(obj)) continue;

      Page* p = Page::FromAddress(obj->address());
      if (p->IsFlagSet(Page::RESCAN_ON_EVACUATION)) {
        obj->Iterate(&updating_visitor);
        p->ClearFlag(Page::RESCAN_ON_EVACUATION);
      }
    }
  }

  int npages = evacuation_candidates_.length();
  for (int i = 0; i < npages; i++) {
    Page* p = evacuation_candidates_[i];
    ASSERT(p->IsEvacuationCandidate() ||
           p->IsFlagSet(Page::RESCAN_ON_EVACUATION));

    if (p->IsEvacuationCandidate()) {
      SlotsBuffer::UpdateSlotsRecordedIn(heap_,
                                         p->slots_buffer(),
                                         code_slots_filtering_required);
      if (FLAG_trace_fragmentation) {
        PrintF("  page %p slots buffer: %d\n",
               reinterpret_cast<void*>(p),
               SlotsBuffer::SizeOfChain(p->slots_buffer()));
      }

      // Important: skip list should be cleared only after roots were updated
      // because root iteration traverses the stack and might have to find code
      // objects from non-updated pc pointing into evacuation candidate.
      SkipList* list = p->skip_list();
      if (list != NULL) list->Clear();
    } else {
      if (FLAG_gc_verbose) {
        PrintF("Sweeping 0x%" V8PRIxPTR " during evacuation.\n",
               reinterpret_cast<intptr_t>(p));
      }
      PagedSpace* space = static_cast<PagedSpace*>(p->owner());
      p->ClearFlag(MemoryChunk::RESCAN_ON_EVACUATION);

      switch (space->identity()) {
        case OLD_DATA_SPACE:
          SweepConservatively(space, p);
          break;
        case OLD_POINTER_SPACE:
          SweepPrecisely<SWEEP_AND_VISIT_LIVE_OBJECTS, IGNORE_SKIP_LIST>(
              space, p, &updating_visitor);
          break;
        case CODE_SPACE:
          SweepPrecisely<SWEEP_AND_VISIT_LIVE_OBJECTS, REBUILD_SKIP_LIST>(
              space, p, &updating_visitor);
          break;
        default:
          UNREACHABLE();
          break;
      }
    }
  }

  // Update pointers from cells.
  HeapObjectIterator cell_iterator(heap_->cell_space());
  for (HeapObject* cell = cell_iterator.Next();
       cell != NULL;
       cell = cell_iterator.Next()) {
    if (cell->IsJSGlobalPropertyCell()) {
      Address value_address =
          reinterpret_cast<Address>(cell) +
          (JSGlobalPropertyCell::kValueOffset - kHeapObjectTag);
      updating_visitor.VisitPointer(reinterpret_cast<Object**>(value_address));
    }
  }

  // Update pointer from the global contexts list.
  updating_visitor.VisitPointer(heap_->global_contexts_list_address());

  heap_->symbol_table()->Iterate(&updating_visitor);

  // Update pointers from external string table.
  heap_->UpdateReferencesInExternalStringTable(
      &UpdateReferenceInExternalStringTableEntry);

  // Update JSFunction pointers from the runtime profiler.
  heap()->isolate()->runtime_profiler()->UpdateSamplesAfterCompact(
      &updating_visitor);

  EvacuationWeakObjectRetainer evacuation_object_retainer;
  heap()->ProcessWeakReferences(&evacuation_object_retainer);

  // Visit invalidated code (we ignored all slots on it) and clear mark-bits
  // under it.
  ProcessInvalidatedCode(&updating_visitor);

#ifdef DEBUG
  if (FLAG_verify_heap) {
    VerifyEvacuation(heap_);
  }
#endif

  slots_buffer_allocator_.DeallocateChain(&migration_slots_buffer_);
  ASSERT(migration_slots_buffer_ == NULL);
  for (int i = 0; i < npages; i++) {
    Page* p = evacuation_candidates_[i];
    if (!p->IsEvacuationCandidate()) continue;
    PagedSpace* space = static_cast<PagedSpace*>(p->owner());
    space->Free(p->ObjectAreaStart(), Page::kObjectAreaSize);
    p->set_scan_on_scavenge(false);
    slots_buffer_allocator_.DeallocateChain(p->slots_buffer_address());
    p->ClearEvacuationCandidate();
  }
  evacuation_candidates_.Rewind(0);
  compacting_ = false;
}


static const int kStartTableEntriesPerLine = 5;
static const int kStartTableLines = 171;
static const int kStartTableInvalidLine = 127;
static const int kStartTableUnusedEntry = 126;

#define _ kStartTableUnusedEntry
#define X kStartTableInvalidLine
// Mark-bit to object start offset table.
//
// The line is indexed by the mark bits in a byte.  The first number on
// the line describes the number of live object starts for the line and the
// other numbers on the line describe the offsets (in words) of the object
// starts.
//
// Since objects are at least 2 words large we don't have entries for two
// consecutive 1 bits.  All entries after 170 have at least 2 consecutive bits.
char kStartTable[kStartTableLines * kStartTableEntriesPerLine] = {
  0, _, _, _, _,  // 0
  1, 0, _, _, _,  // 1
  1, 1, _, _, _,  // 2
  X, _, _, _, _,  // 3
  1, 2, _, _, _,  // 4
  2, 0, 2, _, _,  // 5
  X, _, _, _, _,  // 6
  X, _, _, _, _,  // 7
  1, 3, _, _, _,  // 8
  2, 0, 3, _, _,  // 9
  2, 1, 3, _, _,  // 10
  X, _, _, _, _,  // 11
  X, _, _, _, _,  // 12
  X, _, _, _, _,  // 13
  X, _, _, _, _,  // 14
  X, _, _, _, _,  // 15
  1, 4, _, _, _,  // 16
  2, 0, 4, _, _,  // 17
  2, 1, 4, _, _,  // 18
  X, _, _, _, _,  // 19
  2, 2, 4, _, _,  // 20
  3, 0, 2, 4, _,  // 21
  X, _, _, _, _,  // 22
  X, _, _, _, _,  // 23
  X, _, _, _, _,  // 24
  X, _, _, _, _,  // 25
  X, _, _, _, _,  // 26
  X, _, _, _, _,  // 27
  X, _, _, _, _,  // 28
  X, _, _, _, _,  // 29
  X, _, _, _, _,  // 30
  X, _, _, _, _,  // 31
  1, 5, _, _, _,  // 32
  2, 0, 5, _, _,  // 33
  2, 1, 5, _, _,  // 34
  X, _, _, _, _,  // 35
  2, 2, 5, _, _,  // 36
  3, 0, 2, 5, _,  // 37
  X, _, _, _, _,  // 38
  X, _, _, _, _,  // 39
  2, 3, 5, _, _,  // 40
  3, 0, 3, 5, _,  // 41
  3, 1, 3, 5, _,  // 42
  X, _, _, _, _,  // 43
  X, _, _, _, _,  // 44
  X, _, _, _, _,  // 45
  X, _, _, _, _,  // 46
  X, _, _, _, _,  // 47
  X, _, _, _, _,  // 48
  X, _, _, _, _,  // 49
  X, _, _, _, _,  // 50
  X, _, _, _, _,  // 51
  X, _, _, _, _,  // 52
  X, _, _, _, _,  // 53
  X, _, _, _, _,  // 54
  X, _, _, _, _,  // 55
  X, _, _, _, _,  // 56
  X, _, _, _, _,  // 57
  X, _, _, _, _,  // 58
  X, _, _, _, _,  // 59
  X, _, _, _, _,  // 60
  X, _, _, _, _,  // 61
  X, _, _, _, _,  // 62
  X, _, _, _, _,  // 63
  1, 6, _, _, _,  // 64
  2, 0, 6, _, _,  // 65
  2, 1, 6, _, _,  // 66
  X, _, _, _, _,  // 67
  2, 2, 6, _, _,  // 68
  3, 0, 2, 6, _,  // 69
  X, _, _, _, _,  // 70
  X, _, _, _, _,  // 71
  2, 3, 6, _, _,  // 72
  3, 0, 3, 6, _,  // 73
  3, 1, 3, 6, _,  // 74
  X, _, _, _, _,  // 75
  X, _, _, _, _,  // 76
  X, _, _, _, _,  // 77
  X, _, _, _, _,  // 78
  X, _, _, _, _,  // 79
  2, 4, 6, _, _,  // 80
  3, 0, 4, 6, _,  // 81
  3, 1, 4, 6, _,  // 82
  X, _, _, _, _,  // 83
  3, 2, 4, 6, _,  // 84
  4, 0, 2, 4, 6,  // 85
  X, _, _, _, _,  // 86
  X, _, _, _, _,  // 87
  X, _, _, _, _,  // 88
  X, _, _, _, _,  // 89
  X, _, _, _, _,  // 90
  X, _, _, _, _,  // 91
  X, _, _, _, _,  // 92
  X, _, _, _, _,  // 93
  X, _, _, _, _,  // 94
  X, _, _, _, _,  // 95
  X, _, _, _, _,  // 96
  X, _, _, _, _,  // 97
  X, _, _, _, _,  // 98
  X, _, _, _, _,  // 99
  X, _, _, _, _,  // 100
  X, _, _, _, _,  // 101
  X, _, _, _, _,  // 102
  X, _, _, _, _,  // 103
  X, _, _, _, _,  // 104
  X, _, _, _, _,  // 105
  X, _, _, _, _,  // 106
  X, _, _, _, _,  // 107
  X, _, _, _, _,  // 108
  X, _, _, _, _,  // 109
  X, _, _, _, _,  // 110
  X, _, _, _, _,  // 111
  X, _, _, _, _,  // 112
  X, _, _, _, _,  // 113
  X, _, _, _, _,  // 114
  X, _, _, _, _,  // 115
  X, _, _, _, _,  // 116
  X, _, _, _, _,  // 117
  X, _, _, _, _,  // 118
  X, _, _, _, _,  // 119
  X, _, _, _, _,  // 120
  X, _, _, _, _,  // 121
  X, _, _, _, _,  // 122
  X, _, _, _, _,  // 123
  X, _, _, _, _,  // 124
  X, _, _, _, _,  // 125
  X, _, _, _, _,  // 126
  X, _, _, _, _,  // 127
  1, 7, _, _, _,  // 128
  2, 0, 7, _, _,  // 129
  2, 1, 7, _, _,  // 130
  X, _, _, _, _,  // 131
  2, 2, 7, _, _,  // 132
  3, 0, 2, 7, _,  // 133
  X, _, _, _, _,  // 134
  X, _, _, _, _,  // 135
  2, 3, 7, _, _,  // 136
  3, 0, 3, 7, _,  // 137
  3, 1, 3, 7, _,  // 138
  X, _, _, _, _,  // 139
  X, _, _, _, _,  // 140
  X, _, _, _, _,  // 141
  X, _, _, _, _,  // 142
  X, _, _, _, _,  // 143
  2, 4, 7, _, _,  // 144
  3, 0, 4, 7, _,  // 145
  3, 1, 4, 7, _,  // 146
  X, _, _, _, _,  // 147
  3, 2, 4, 7, _,  // 148
  4, 0, 2, 4, 7,  // 149
  X, _, _, _, _,  // 150
  X, _, _, _, _,  // 151
  X, _, _, _, _,  // 152
  X, _, _, _, _,  // 153
  X, _, _, _, _,  // 154
  X, _, _, _, _,  // 155
  X, _, _, _, _,  // 156
  X, _, _, _, _,  // 157
  X, _, _, _, _,  // 158
  X, _, _, _, _,  // 159
  2, 5, 7, _, _,  // 160
  3, 0, 5, 7, _,  // 161
  3, 1, 5, 7, _,  // 162
  X, _, _, _, _,  // 163
  3, 2, 5, 7, _,  // 164
  4, 0, 2, 5, 7,  // 165
  X, _, _, _, _,  // 166
  X, _, _, _, _,  // 167
  3, 3, 5, 7, _,  // 168
  4, 0, 3, 5, 7,  // 169
  4, 1, 3, 5, 7   // 170
};
#undef _
#undef X


// Takes a word of mark bits.  Returns the number of objects that start in the
// range.  Puts the offsets of the words in the supplied array.
static inline int MarkWordToObjectStarts(uint32_t mark_bits, int* starts) {
  int objects = 0;
  int offset = 0;

  // No consecutive 1 bits.
  ASSERT((mark_bits & 0x180) != 0x180);
  ASSERT((mark_bits & 0x18000) != 0x18000);
  ASSERT((mark_bits & 0x1800000) != 0x1800000);

  while (mark_bits != 0) {
    int byte = (mark_bits & 0xff);
    mark_bits >>= 8;
    if (byte != 0) {
      ASSERT(byte < kStartTableLines);  // No consecutive 1 bits.
      char* table = kStartTable + byte * kStartTableEntriesPerLine;
      int objects_in_these_8_words = table[0];
      ASSERT(objects_in_these_8_words != kStartTableInvalidLine);
      ASSERT(objects_in_these_8_words < kStartTableEntriesPerLine);
      for (int i = 0; i < objects_in_these_8_words; i++) {
        starts[objects++] = offset + table[1 + i];
      }
    }
    offset += 8;
  }
  return objects;
}


static inline Address DigestFreeStart(Address approximate_free_start,
                                      uint32_t free_start_cell) {
  ASSERT(free_start_cell != 0);

  // No consecutive 1 bits.
  ASSERT((free_start_cell & (free_start_cell << 1)) == 0);

  int offsets[16];
  uint32_t cell = free_start_cell;
  int offset_of_last_live;
  if ((cell & 0x80000000u) != 0) {
    // This case would overflow below.
    offset_of_last_live = 31;
  } else {
    // Remove all but one bit, the most significant.  This is an optimization
    // that may or may not be worthwhile.
    cell |= cell >> 16;
    cell |= cell >> 8;
    cell |= cell >> 4;
    cell |= cell >> 2;
    cell |= cell >> 1;
    cell = (cell + 1) >> 1;
    int live_objects = MarkWordToObjectStarts(cell, offsets);
    ASSERT(live_objects == 1);
    offset_of_last_live = offsets[live_objects - 1];
  }
  Address last_live_start =
      approximate_free_start + offset_of_last_live * kPointerSize;
  HeapObject* last_live = HeapObject::FromAddress(last_live_start);
  Address free_start = last_live_start + last_live->Size();
  return free_start;
}


static inline Address StartOfLiveObject(Address block_address, uint32_t cell) {
  ASSERT(cell != 0);

  // No consecutive 1 bits.
  ASSERT((cell & (cell << 1)) == 0);

  int offsets[16];
  if (cell == 0x80000000u) {  // Avoid overflow below.
    return block_address + 31 * kPointerSize;
  }
  uint32_t first_set_bit = ((cell ^ (cell - 1)) + 1) >> 1;
  ASSERT((first_set_bit & cell) == first_set_bit);
  int live_objects = MarkWordToObjectStarts(first_set_bit, offsets);
  ASSERT(live_objects == 1);
  USE(live_objects);
  return block_address + offsets[0] * kPointerSize;
}


// Sweeps a space conservatively.  After this has been done the larger free
// spaces have been put on the free list and the smaller ones have been
// ignored and left untouched.  A free space is always either ignored or put
// on the free list, never split up into two parts.  This is important
// because it means that any FreeSpace maps left actually describe a region of
// memory that can be ignored when scanning.  Dead objects other than free
// spaces will not contain the free space map.
intptr_t MarkCompactCollector::SweepConservatively(PagedSpace* space, Page* p) {
  ASSERT(!p->IsEvacuationCandidate() && !p->WasSwept());
  MarkBit::CellType* cells = p->markbits()->cells();
  p->MarkSweptConservatively();

  int last_cell_index =
      Bitmap::IndexToCell(
          Bitmap::CellAlignIndex(
              p->AddressToMarkbitIndex(p->ObjectAreaEnd())));

  int cell_index = Page::kFirstUsedCell;
  intptr_t freed_bytes = 0;

  // This is the start of the 32 word block that we are currently looking at.
  Address block_address = p->ObjectAreaStart();

  // Skip over all the dead objects at the start of the page and mark them free.
  for (cell_index = Page::kFirstUsedCell;
       cell_index < last_cell_index;
       cell_index++, block_address += 32 * kPointerSize) {
    if (cells[cell_index] != 0) break;
  }
  size_t size = block_address - p->ObjectAreaStart();
  if (cell_index == last_cell_index) {
    freed_bytes += static_cast<int>(space->Free(p->ObjectAreaStart(),
                                                static_cast<int>(size)));
    ASSERT_EQ(0, p->LiveBytes());
    return freed_bytes;
  }
  // Grow the size of the start-of-page free space a little to get up to the
  // first live object.
  Address free_end = StartOfLiveObject(block_address, cells[cell_index]);
  // Free the first free space.
  size = free_end - p->ObjectAreaStart();
  freed_bytes += space->Free(p->ObjectAreaStart(),
                             static_cast<int>(size));
  // The start of the current free area is represented in undigested form by
  // the address of the last 32-word section that contained a live object and
  // the marking bitmap for that cell, which describes where the live object
  // started.  Unless we find a large free space in the bitmap we will not
  // digest this pair into a real address.  We start the iteration here at the
  // first word in the marking bit map that indicates a live object.
  Address free_start = block_address;
  uint32_t free_start_cell = cells[cell_index];

  for ( ;
       cell_index < last_cell_index;
       cell_index++, block_address += 32 * kPointerSize) {
    ASSERT((unsigned)cell_index ==
        Bitmap::IndexToCell(
            Bitmap::CellAlignIndex(
                p->AddressToMarkbitIndex(block_address))));
    uint32_t cell = cells[cell_index];
    if (cell != 0) {
      // We have a live object.  Check approximately whether it is more than 32
      // words since the last live object.
      if (block_address - free_start > 32 * kPointerSize) {
        free_start = DigestFreeStart(free_start, free_start_cell);
        if (block_address - free_start > 32 * kPointerSize) {
          // Now that we know the exact start of the free space it still looks
          // like we have a large enough free space to be worth bothering with.
          // so now we need to find the start of the first live object at the
          // end of the free space.
          free_end = StartOfLiveObject(block_address, cell);
          freed_bytes += space->Free(free_start,
                                     static_cast<int>(free_end - free_start));
        }
      }
      // Update our undigested record of where the current free area started.
      free_start = block_address;
      free_start_cell = cell;
      // Clear marking bits for current cell.
      cells[cell_index] = 0;
    }
  }

  // Handle the free space at the end of the page.
  if (block_address - free_start > 32 * kPointerSize) {
    free_start = DigestFreeStart(free_start, free_start_cell);
    freed_bytes += space->Free(free_start,
                               static_cast<int>(block_address - free_start));
  }

  p->ResetLiveBytes();
  return freed_bytes;
}


void MarkCompactCollector::SweepSpace(PagedSpace* space,
                                      SweeperType sweeper) {
  space->set_was_swept_conservatively(sweeper == CONSERVATIVE ||
                                      sweeper == LAZY_CONSERVATIVE);

  space->ClearStats();

  PageIterator it(space);

  intptr_t freed_bytes = 0;
  intptr_t newspace_size = space->heap()->new_space()->Size();
  bool lazy_sweeping_active = false;
  bool unused_page_present = false;

  while (it.has_next()) {
    Page* p = it.next();

    // Clear sweeping flags indicating that marking bits are still intact.
    p->ClearSweptPrecisely();
    p->ClearSweptConservatively();

    if (p->IsEvacuationCandidate()) {
      ASSERT(evacuation_candidates_.length() > 0);
      continue;
    }

    if (p->IsFlagSet(Page::RESCAN_ON_EVACUATION)) {
      // Will be processed in EvacuateNewSpaceAndCandidates.
      continue;
    }

    if (lazy_sweeping_active) {
      if (FLAG_gc_verbose) {
        PrintF("Sweeping 0x%" V8PRIxPTR " lazily postponed.\n",
               reinterpret_cast<intptr_t>(p));
      }
      continue;
    }

    // One unused page is kept, all further are released before sweeping them.
    if (p->LiveBytes() == 0) {
      if (unused_page_present) {
        if (FLAG_gc_verbose) {
          PrintF("Sweeping 0x%" V8PRIxPTR " released page.\n",
                 reinterpret_cast<intptr_t>(p));
        }
        space->ReleasePage(p);
        continue;
      }
      unused_page_present = true;
    }

    if (FLAG_gc_verbose) {
      PrintF("Sweeping 0x%" V8PRIxPTR " with sweeper %d.\n",
             reinterpret_cast<intptr_t>(p),
             sweeper);
    }

    switch (sweeper) {
      case CONSERVATIVE: {
        SweepConservatively(space, p);
        break;
      }
      case LAZY_CONSERVATIVE: {
        freed_bytes += SweepConservatively(space, p);
        if (freed_bytes >= newspace_size && p != space->LastPage()) {
          space->SetPagesToSweep(p->next_page(), space->anchor());
          lazy_sweeping_active = true;
        }
        break;
      }
      case PRECISE: {
        if (space->identity() == CODE_SPACE) {
          SweepPrecisely<SWEEP_ONLY, REBUILD_SKIP_LIST>(space, p, NULL);
        } else {
          SweepPrecisely<SWEEP_ONLY, IGNORE_SKIP_LIST>(space, p, NULL);
        }
        break;
      }
      default: {
        UNREACHABLE();
      }
    }
  }

  // Give pages that are queued to be freed back to the OS.
  heap()->FreeQueuedChunks();
}


void MarkCompactCollector::SweepSpaces() {
  GCTracer::Scope gc_scope(tracer_, GCTracer::Scope::MC_SWEEP);
#ifdef DEBUG
  state_ = SWEEP_SPACES;
#endif
  SweeperType how_to_sweep =
      FLAG_lazy_sweeping ? LAZY_CONSERVATIVE : CONSERVATIVE;
  if (sweep_precisely_) how_to_sweep = PRECISE;
  // Noncompacting collections simply sweep the spaces to clear the mark
  // bits and free the nonlive blocks (for old and map spaces).  We sweep
  // the map space last because freeing non-live maps overwrites them and
  // the other spaces rely on possibly non-live maps to get the sizes for
  // non-live objects.
  SweepSpace(heap()->old_pointer_space(), how_to_sweep);
  SweepSpace(heap()->old_data_space(), how_to_sweep);

  RemoveDeadInvalidatedCode();
  SweepSpace(heap()->code_space(), PRECISE);

  SweepSpace(heap()->cell_space(), PRECISE);

  { GCTracer::Scope gc_scope(tracer_, GCTracer::Scope::MC_SWEEP_NEWSPACE);
    EvacuateNewSpaceAndCandidates();
  }

  // ClearNonLiveTransitions depends on precise sweeping of map space to
  // detect whether unmarked map became dead in this collection or in one
  // of the previous ones.
  SweepSpace(heap()->map_space(), PRECISE);

  ASSERT(live_map_objects_size_ <= heap()->map_space()->Size());

  // Deallocate unmarked objects and clear marked bits for marked objects.
  heap_->lo_space()->FreeUnmarkedObjects();
}


void MarkCompactCollector::EnableCodeFlushing(bool enable) {
  if (enable) {
    if (code_flusher_ != NULL) return;
    code_flusher_ = new CodeFlusher(heap()->isolate());
  } else {
    if (code_flusher_ == NULL) return;
    delete code_flusher_;
    code_flusher_ = NULL;
  }
}


// TODO(1466) ReportDeleteIfNeeded is not called currently.
// Our profiling tools do not expect intersections between
// code objects. We should either reenable it or change our tools.
void MarkCompactCollector::ReportDeleteIfNeeded(HeapObject* obj,
                                                Isolate* isolate) {
#ifdef ENABLE_GDB_JIT_INTERFACE
  if (obj->IsCode()) {
    GDBJITInterface::RemoveCode(reinterpret_cast<Code*>(obj));
  }
#endif
  if (obj->IsCode()) {
    PROFILE(isolate, CodeDeleteEvent(obj->address()));
  }
}


void MarkCompactCollector::Initialize() {
  StaticMarkingVisitor::Initialize();
}


bool SlotsBuffer::IsTypedSlot(ObjectSlot slot) {
  return reinterpret_cast<uintptr_t>(slot) < NUMBER_OF_SLOT_TYPES;
}


bool SlotsBuffer::AddTo(SlotsBufferAllocator* allocator,
                        SlotsBuffer** buffer_address,
                        SlotType type,
                        Address addr,
                        AdditionMode mode) {
  SlotsBuffer* buffer = *buffer_address;
  if (buffer == NULL || !buffer->HasSpaceForTypedSlot()) {
    if (mode == FAIL_ON_OVERFLOW && ChainLengthThresholdReached(buffer)) {
      allocator->DeallocateChain(buffer_address);
      return false;
    }
    buffer = allocator->AllocateBuffer(buffer);
    *buffer_address = buffer;
  }
  ASSERT(buffer->HasSpaceForTypedSlot());
  buffer->Add(reinterpret_cast<ObjectSlot>(type));
  buffer->Add(reinterpret_cast<ObjectSlot>(addr));
  return true;
}


static inline SlotsBuffer::SlotType SlotTypeForRMode(RelocInfo::Mode rmode) {
  if (RelocInfo::IsCodeTarget(rmode)) {
    return SlotsBuffer::CODE_TARGET_SLOT;
  } else if (RelocInfo::IsEmbeddedObject(rmode)) {
    return SlotsBuffer::EMBEDDED_OBJECT_SLOT;
  } else if (RelocInfo::IsDebugBreakSlot(rmode)) {
    return SlotsBuffer::DEBUG_TARGET_SLOT;
  } else if (RelocInfo::IsJSReturn(rmode)) {
    return SlotsBuffer::JS_RETURN_SLOT;
  }
  UNREACHABLE();
  return SlotsBuffer::NUMBER_OF_SLOT_TYPES;
}


void MarkCompactCollector::RecordRelocSlot(RelocInfo* rinfo, Object* target) {
  Page* target_page = Page::FromAddress(reinterpret_cast<Address>(target));
  if (target_page->IsEvacuationCandidate() &&
      (rinfo->host() == NULL ||
       !ShouldSkipEvacuationSlotRecording(rinfo->host()))) {
    if (!SlotsBuffer::AddTo(&slots_buffer_allocator_,
                            target_page->slots_buffer_address(),
                            SlotTypeForRMode(rinfo->rmode()),
                            rinfo->pc(),
                            SlotsBuffer::FAIL_ON_OVERFLOW)) {
      EvictEvacuationCandidate(target_page);
    }
  }
}


void MarkCompactCollector::RecordCodeEntrySlot(Address slot, Code* target) {
  Page* target_page = Page::FromAddress(reinterpret_cast<Address>(target));
  if (target_page->IsEvacuationCandidate() &&
      !ShouldSkipEvacuationSlotRecording(reinterpret_cast<Object**>(slot))) {
    if (!SlotsBuffer::AddTo(&slots_buffer_allocator_,
                            target_page->slots_buffer_address(),
                            SlotsBuffer::CODE_ENTRY_SLOT,
                            slot,
                            SlotsBuffer::FAIL_ON_OVERFLOW)) {
      EvictEvacuationCandidate(target_page);
    }
  }
}


static inline SlotsBuffer::SlotType DecodeSlotType(
    SlotsBuffer::ObjectSlot slot) {
  return static_cast<SlotsBuffer::SlotType>(reinterpret_cast<intptr_t>(slot));
}


void SlotsBuffer::UpdateSlots(Heap* heap) {
  PointersUpdatingVisitor v(heap);

  for (int slot_idx = 0; slot_idx < idx_; ++slot_idx) {
    ObjectSlot slot = slots_[slot_idx];
    if (!IsTypedSlot(slot)) {
      PointersUpdatingVisitor::UpdateSlot(heap, slot);
    } else {
      ++slot_idx;
      ASSERT(slot_idx < idx_);
      UpdateSlot(&v,
                 DecodeSlotType(slot),
                 reinterpret_cast<Address>(slots_[slot_idx]));
    }
  }
}


void SlotsBuffer::UpdateSlotsWithFilter(Heap* heap) {
  PointersUpdatingVisitor v(heap);

  for (int slot_idx = 0; slot_idx < idx_; ++slot_idx) {
    ObjectSlot slot = slots_[slot_idx];
    if (!IsTypedSlot(slot)) {
      if (!IsOnInvalidatedCodeObject(reinterpret_cast<Address>(slot))) {
        PointersUpdatingVisitor::UpdateSlot(heap, slot);
      }
    } else {
      ++slot_idx;
      ASSERT(slot_idx < idx_);
      Address pc = reinterpret_cast<Address>(slots_[slot_idx]);
      if (!IsOnInvalidatedCodeObject(pc)) {
        UpdateSlot(&v,
                   DecodeSlotType(slot),
                   reinterpret_cast<Address>(slots_[slot_idx]));
      }
    }
  }
}


SlotsBuffer* SlotsBufferAllocator::AllocateBuffer(SlotsBuffer* next_buffer) {
  return new SlotsBuffer(next_buffer);
}


void SlotsBufferAllocator::DeallocateBuffer(SlotsBuffer* buffer) {
  delete buffer;
}


void SlotsBufferAllocator::DeallocateChain(SlotsBuffer** buffer_address) {
  SlotsBuffer* buffer = *buffer_address;
  while (buffer != NULL) {
    SlotsBuffer* next_buffer = buffer->next();
    DeallocateBuffer(buffer);
    buffer = next_buffer;
  }
  *buffer_address = NULL;
}


} }  // namespace v8::internal
