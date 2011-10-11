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

#include "liveobjectlist-inl.h"
#include "macro-assembler.h"
#include "mark-compact.h"
#include "platform.h"

namespace v8 {
namespace internal {


// ----------------------------------------------------------------------------
// HeapObjectIterator

HeapObjectIterator::HeapObjectIterator(PagedSpace* space) {
  // You can't actually iterate over the anchor page.  It is not a real page,
  // just an anchor for the double linked page list.  Initialize as if we have
  // reached the end of the anchor page, then the first iteration will move on
  // to the first page.
  Initialize(space,
             NULL,
             NULL,
             kAllPagesInSpace,
             NULL);
}


HeapObjectIterator::HeapObjectIterator(PagedSpace* space,
                                       HeapObjectCallback size_func) {
  // You can't actually iterate over the anchor page.  It is not a real page,
  // just an anchor for the double linked page list.  Initialize the current
  // address and end as NULL, then the first iteration will move on
  // to the first page.
  Initialize(space,
             NULL,
             NULL,
             kAllPagesInSpace,
             size_func);
}


HeapObjectIterator::HeapObjectIterator(Page* page,
                                       HeapObjectCallback size_func) {
  Space* owner = page->owner();
  ASSERT(owner == HEAP->old_pointer_space() ||
         owner == HEAP->old_data_space() ||
         owner == HEAP->map_space() ||
         owner == HEAP->cell_space() ||
         owner == HEAP->code_space());
  Initialize(reinterpret_cast<PagedSpace*>(owner),
             page->ObjectAreaStart(),
             page->ObjectAreaEnd(),
             kOnePageOnly,
             size_func);
  ASSERT(page->WasSweptPrecisely());
}


void HeapObjectIterator::Initialize(PagedSpace* space,
                                    Address cur, Address end,
                                    HeapObjectIterator::PageMode mode,
                                    HeapObjectCallback size_f) {
  // Check that we actually can iterate this space.
  ASSERT(!space->was_swept_conservatively());

  space_ = space;
  cur_addr_ = cur;
  cur_end_ = end;
  page_mode_ = mode;
  size_func_ = size_f;

#ifdef DEBUG
  Verify();
#endif
}


// We have hit the end of the page and should advance to the next block of
// objects.  This happens at the end of the page.
bool HeapObjectIterator::AdvanceToNextPage() {
  ASSERT(cur_addr_ == cur_end_);
  if (page_mode_ == kOnePageOnly) return false;
  Page* cur_page;
  if (cur_addr_ == NULL) {
    cur_page = space_->anchor();
  } else {
    cur_page = Page::FromAddress(cur_addr_ - 1);
    ASSERT(cur_addr_ == cur_page->ObjectAreaEnd());
  }
  cur_page = cur_page->next_page();
  if (cur_page == space_->anchor()) return false;
  cur_addr_ = cur_page->ObjectAreaStart();
  cur_end_ = cur_page->ObjectAreaEnd();
  ASSERT(cur_page->WasSweptPrecisely());
  return true;
}


#ifdef DEBUG
void HeapObjectIterator::Verify() {
  // TODO(gc): We should do something here.
}
#endif


// -----------------------------------------------------------------------------
// CodeRange


CodeRange::CodeRange(Isolate* isolate)
    : isolate_(isolate),
      code_range_(NULL),
      free_list_(0),
      allocation_list_(0),
      current_allocation_block_index_(0) {
}


bool CodeRange::Setup(const size_t requested) {
  ASSERT(code_range_ == NULL);

  code_range_ = new VirtualMemory(requested);
  CHECK(code_range_ != NULL);
  if (!code_range_->IsReserved()) {
    delete code_range_;
    code_range_ = NULL;
    return false;
  }

  // We are sure that we have mapped a block of requested addresses.
  ASSERT(code_range_->size() == requested);
  LOG(isolate_, NewEvent("CodeRange", code_range_->address(), requested));
  Address base = reinterpret_cast<Address>(code_range_->address());
  Address aligned_base =
      RoundUp(reinterpret_cast<Address>(code_range_->address()),
              MemoryChunk::kAlignment);
  size_t size = code_range_->size() - (aligned_base - base);
  allocation_list_.Add(FreeBlock(aligned_base, size));
  current_allocation_block_index_ = 0;
  return true;
}


int CodeRange::CompareFreeBlockAddress(const FreeBlock* left,
                                       const FreeBlock* right) {
  // The entire point of CodeRange is that the difference between two
  // addresses in the range can be represented as a signed 32-bit int,
  // so the cast is semantically correct.
  return static_cast<int>(left->start - right->start);
}


void CodeRange::GetNextAllocationBlock(size_t requested) {
  for (current_allocation_block_index_++;
       current_allocation_block_index_ < allocation_list_.length();
       current_allocation_block_index_++) {
    if (requested <= allocation_list_[current_allocation_block_index_].size) {
      return;  // Found a large enough allocation block.
    }
  }

  // Sort and merge the free blocks on the free list and the allocation list.
  free_list_.AddAll(allocation_list_);
  allocation_list_.Clear();
  free_list_.Sort(&CompareFreeBlockAddress);
  for (int i = 0; i < free_list_.length();) {
    FreeBlock merged = free_list_[i];
    i++;
    // Add adjacent free blocks to the current merged block.
    while (i < free_list_.length() &&
           free_list_[i].start == merged.start + merged.size) {
      merged.size += free_list_[i].size;
      i++;
    }
    if (merged.size > 0) {
      allocation_list_.Add(merged);
    }
  }
  free_list_.Clear();

  for (current_allocation_block_index_ = 0;
       current_allocation_block_index_ < allocation_list_.length();
       current_allocation_block_index_++) {
    if (requested <= allocation_list_[current_allocation_block_index_].size) {
      return;  // Found a large enough allocation block.
    }
  }

  // Code range is full or too fragmented.
  V8::FatalProcessOutOfMemory("CodeRange::GetNextAllocationBlock");
}



Address CodeRange::AllocateRawMemory(const size_t requested,
                                     size_t* allocated) {
  ASSERT(current_allocation_block_index_ < allocation_list_.length());
  if (requested > allocation_list_[current_allocation_block_index_].size) {
    // Find an allocation block large enough.  This function call may
    // call V8::FatalProcessOutOfMemory if it cannot find a large enough block.
    GetNextAllocationBlock(requested);
  }
  // Commit the requested memory at the start of the current allocation block.
  size_t aligned_requested = RoundUp(requested, MemoryChunk::kAlignment);
  FreeBlock current = allocation_list_[current_allocation_block_index_];
  if (aligned_requested >= (current.size - Page::kPageSize)) {
    // Don't leave a small free block, useless for a large object or chunk.
    *allocated = current.size;
  } else {
    *allocated = aligned_requested;
  }
  ASSERT(*allocated <= current.size);
  ASSERT(IsAddressAligned(current.start, MemoryChunk::kAlignment));
  if (!code_range_->Commit(current.start, *allocated, true)) {
    *allocated = 0;
    return NULL;
  }
  allocation_list_[current_allocation_block_index_].start += *allocated;
  allocation_list_[current_allocation_block_index_].size -= *allocated;
  if (*allocated == current.size) {
    GetNextAllocationBlock(0);  // This block is used up, get the next one.
  }
  return current.start;
}


void CodeRange::FreeRawMemory(Address address, size_t length) {
  ASSERT(IsAddressAligned(address, MemoryChunk::kAlignment));
  free_list_.Add(FreeBlock(address, length));
  code_range_->Uncommit(address, length);
}


void CodeRange::TearDown() {
    delete code_range_;  // Frees all memory in the virtual memory range.
    code_range_ = NULL;
    free_list_.Free();
    allocation_list_.Free();
}


// -----------------------------------------------------------------------------
// MemoryAllocator
//

MemoryAllocator::MemoryAllocator(Isolate* isolate)
    : isolate_(isolate),
      capacity_(0),
      capacity_executable_(0),
      size_(0),
      size_executable_(0) {
}


bool MemoryAllocator::Setup(intptr_t capacity, intptr_t capacity_executable) {
  capacity_ = RoundUp(capacity, Page::kPageSize);
  capacity_executable_ = RoundUp(capacity_executable, Page::kPageSize);
  ASSERT_GE(capacity_, capacity_executable_);

  size_ = 0;
  size_executable_ = 0;

  return true;
}


void MemoryAllocator::TearDown() {
  // Check that spaces were torn down before MemoryAllocator.
  ASSERT(size_ == 0);
  // TODO(gc) this will be true again when we fix FreeMemory.
  // ASSERT(size_executable_ == 0);
  capacity_ = 0;
  capacity_executable_ = 0;
}


void MemoryAllocator::FreeMemory(VirtualMemory* reservation,
                                 Executability executable) {
  // TODO(gc) make code_range part of memory allocator?
  ASSERT(reservation->IsReserved());
  size_t size = reservation->size();
  ASSERT(size_ >= size);
  size_ -= size;

  isolate_->counters()->memory_allocated()->Decrement(static_cast<int>(size));

  if (executable == EXECUTABLE) {
    ASSERT(size_executable_ >= size);
    size_executable_ -= size;
  }
  // Code which is part of the code-range does not have its own VirtualMemory.
  ASSERT(!isolate_->code_range()->contains(
      static_cast<Address>(reservation->address())));
  ASSERT(executable == NOT_EXECUTABLE || !isolate_->code_range()->exists());
  reservation->Release();
}


void MemoryAllocator::FreeMemory(Address base,
                                 size_t size,
                                 Executability executable) {
  // TODO(gc) make code_range part of memory allocator?
  ASSERT(size_ >= size);
  size_ -= size;

  isolate_->counters()->memory_allocated()->Decrement(static_cast<int>(size));

  if (executable == EXECUTABLE) {
    ASSERT(size_executable_ >= size);
    size_executable_ -= size;
  }
  if (isolate_->code_range()->contains(static_cast<Address>(base))) {
    ASSERT(executable == EXECUTABLE);
    isolate_->code_range()->FreeRawMemory(base, size);
  } else {
    ASSERT(executable == NOT_EXECUTABLE || !isolate_->code_range()->exists());
    bool result = VirtualMemory::ReleaseRegion(base, size);
    USE(result);
    ASSERT(result);
  }
}


Address MemoryAllocator::ReserveAlignedMemory(size_t size,
                                              size_t alignment,
                                              VirtualMemory* controller) {
  VirtualMemory reservation(size, alignment);

  if (!reservation.IsReserved()) return NULL;
  size_ += reservation.size();
  Address base = RoundUp(static_cast<Address>(reservation.address()),
                         alignment);
  controller->TakeControl(&reservation);
  return base;
}


Address MemoryAllocator::AllocateAlignedMemory(size_t size,
                                               size_t alignment,
                                               Executability executable,
                                               VirtualMemory* controller) {
  VirtualMemory reservation;
  Address base = ReserveAlignedMemory(size, alignment, &reservation);
  if (base == NULL) return NULL;
  if (!reservation.Commit(base,
                          size,
                          executable == EXECUTABLE)) {
    return NULL;
  }
  controller->TakeControl(&reservation);
  return base;
}


void Page::InitializeAsAnchor(PagedSpace* owner) {
  set_owner(owner);
  set_prev_page(this);
  set_next_page(this);
}


NewSpacePage* NewSpacePage::Initialize(Heap* heap,
                                       Address start,
                                       SemiSpace* semi_space) {
  MemoryChunk* chunk = MemoryChunk::Initialize(heap,
                                               start,
                                               Page::kPageSize,
                                               NOT_EXECUTABLE,
                                               semi_space);
  chunk->set_next_chunk(NULL);
  chunk->set_prev_chunk(NULL);
  chunk->initialize_scan_on_scavenge(true);
  bool in_to_space = (semi_space->id() != kFromSpace);
  chunk->SetFlag(in_to_space ? MemoryChunk::IN_TO_SPACE
                             : MemoryChunk::IN_FROM_SPACE);
  ASSERT(!chunk->IsFlagSet(in_to_space ? MemoryChunk::IN_FROM_SPACE
                                       : MemoryChunk::IN_TO_SPACE));
  NewSpacePage* page = static_cast<NewSpacePage*>(chunk);
  heap->incremental_marking()->SetNewSpacePageFlags(page);
  return page;
}


void NewSpacePage::InitializeAsAnchor(SemiSpace* semi_space) {
  set_owner(semi_space);
  set_next_chunk(this);
  set_prev_chunk(this);
  // Flags marks this invalid page as not being in new-space.
  // All real new-space pages will be in new-space.
  SetFlags(0, ~0);
}


MemoryChunk* MemoryChunk::Initialize(Heap* heap,
                                     Address base,
                                     size_t size,
                                     Executability executable,
                                     Space* owner) {
  MemoryChunk* chunk = FromAddress(base);

  ASSERT(base == chunk->address());

  chunk->heap_ = heap;
  chunk->size_ = size;
  chunk->flags_ = 0;
  chunk->set_owner(owner);
  chunk->InitializeReservedMemory();
  chunk->slots_buffer_ = NULL;
  chunk->skip_list_ = NULL;
  chunk->ResetLiveBytes();
  Bitmap::Clear(chunk);
  chunk->initialize_scan_on_scavenge(false);
  chunk->SetFlag(WAS_SWEPT_PRECISELY);

  ASSERT(OFFSET_OF(MemoryChunk, flags_) == kFlagsOffset);
  ASSERT(OFFSET_OF(MemoryChunk, live_byte_count_) == kLiveBytesOffset);

  if (executable == EXECUTABLE) chunk->SetFlag(IS_EXECUTABLE);

  if (owner == heap->old_data_space()) chunk->SetFlag(CONTAINS_ONLY_DATA);

  return chunk;
}


void MemoryChunk::InsertAfter(MemoryChunk* other) {
  next_chunk_ = other->next_chunk_;
  prev_chunk_ = other;
  other->next_chunk_->prev_chunk_ = this;
  other->next_chunk_ = this;
}


void MemoryChunk::Unlink() {
  if (!InNewSpace() && IsFlagSet(SCAN_ON_SCAVENGE)) {
    heap_->decrement_scan_on_scavenge_pages();
    ClearFlag(SCAN_ON_SCAVENGE);
  }
  next_chunk_->prev_chunk_ = prev_chunk_;
  prev_chunk_->next_chunk_ = next_chunk_;
  prev_chunk_ = NULL;
  next_chunk_ = NULL;
}


MemoryChunk* MemoryAllocator::AllocateChunk(intptr_t body_size,
                                            Executability executable,
                                            Space* owner) {
  size_t chunk_size = MemoryChunk::kObjectStartOffset + body_size;
  Heap* heap = isolate_->heap();
  Address base = NULL;
  VirtualMemory reservation;
  if (executable == EXECUTABLE) {
    // Check executable memory limit.
    if (size_executable_ + chunk_size > capacity_executable_) {
      LOG(isolate_,
          StringEvent("MemoryAllocator::AllocateRawMemory",
                      "V8 Executable Allocation capacity exceeded"));
      return NULL;
    }

    // Allocate executable memory either from code range or from the
    // OS.
    if (isolate_->code_range()->exists()) {
      base = isolate_->code_range()->AllocateRawMemory(chunk_size, &chunk_size);
      ASSERT(IsAligned(reinterpret_cast<intptr_t>(base),
                       MemoryChunk::kAlignment));
      if (base == NULL) return NULL;
      size_ += chunk_size;
      // Update executable memory size.
      size_executable_ += chunk_size;
    } else {
      base = AllocateAlignedMemory(chunk_size,
                                   MemoryChunk::kAlignment,
                                   executable,
                                   &reservation);
      if (base == NULL) return NULL;
      // Update executable memory size.
      size_executable_ += reservation.size();
    }
  } else {
    base = AllocateAlignedMemory(chunk_size,
                                 MemoryChunk::kAlignment,
                                 executable,
                                 &reservation);

    if (base == NULL) return NULL;
  }

#ifdef DEBUG
  ZapBlock(base, chunk_size);
#endif
  isolate_->counters()->memory_allocated()->
      Increment(static_cast<int>(chunk_size));

  LOG(isolate_, NewEvent("MemoryChunk", base, chunk_size));
  if (owner != NULL) {
    ObjectSpace space = static_cast<ObjectSpace>(1 << owner->identity());
    PerformAllocationCallback(space, kAllocationActionAllocate, chunk_size);
  }

  MemoryChunk* result = MemoryChunk::Initialize(heap,
                                                base,
                                                chunk_size,
                                                executable,
                                                owner);
  result->set_reserved_memory(&reservation);
  return result;
}


Page* MemoryAllocator::AllocatePage(PagedSpace* owner,
                                    Executability executable) {
  MemoryChunk* chunk = AllocateChunk(Page::kObjectAreaSize, executable, owner);

  if (chunk == NULL) return NULL;

  return Page::Initialize(isolate_->heap(), chunk, executable, owner);
}


LargePage* MemoryAllocator::AllocateLargePage(intptr_t object_size,
                                              Executability executable,
                                              Space* owner) {
  MemoryChunk* chunk = AllocateChunk(object_size, executable, owner);
  if (chunk == NULL) return NULL;
  return LargePage::Initialize(isolate_->heap(), chunk);
}


void MemoryAllocator::Free(MemoryChunk* chunk) {
  LOG(isolate_, DeleteEvent("MemoryChunk", chunk));
  if (chunk->owner() != NULL) {
    ObjectSpace space =
        static_cast<ObjectSpace>(1 << chunk->owner()->identity());
    PerformAllocationCallback(space, kAllocationActionFree, chunk->size());
  }

  delete chunk->slots_buffer();
  delete chunk->skip_list();

  VirtualMemory* reservation = chunk->reserved_memory();
  if (reservation->IsReserved()) {
    FreeMemory(reservation, chunk->executable());
  } else {
    FreeMemory(chunk->address(),
               chunk->size(),
               chunk->executable());
  }
}


bool MemoryAllocator::CommitBlock(Address start,
                                  size_t size,
                                  Executability executable) {
  if (!VirtualMemory::CommitRegion(start, size, executable)) return false;
#ifdef DEBUG
  ZapBlock(start, size);
#endif
  isolate_->counters()->memory_allocated()->Increment(static_cast<int>(size));
  return true;
}


bool MemoryAllocator::UncommitBlock(Address start, size_t size) {
  if (!VirtualMemory::UncommitRegion(start, size)) return false;
  isolate_->counters()->memory_allocated()->Decrement(static_cast<int>(size));
  return true;
}


void MemoryAllocator::ZapBlock(Address start, size_t size) {
  for (size_t s = 0; s + kPointerSize <= size; s += kPointerSize) {
    Memory::Address_at(start + s) = kZapValue;
  }
}


void MemoryAllocator::PerformAllocationCallback(ObjectSpace space,
                                                AllocationAction action,
                                                size_t size) {
  for (int i = 0; i < memory_allocation_callbacks_.length(); ++i) {
    MemoryAllocationCallbackRegistration registration =
      memory_allocation_callbacks_[i];
    if ((registration.space & space) == space &&
        (registration.action & action) == action)
      registration.callback(space, action, static_cast<int>(size));
  }
}


bool MemoryAllocator::MemoryAllocationCallbackRegistered(
    MemoryAllocationCallback callback) {
  for (int i = 0; i < memory_allocation_callbacks_.length(); ++i) {
    if (memory_allocation_callbacks_[i].callback == callback) return true;
  }
  return false;
}


void MemoryAllocator::AddMemoryAllocationCallback(
    MemoryAllocationCallback callback,
    ObjectSpace space,
    AllocationAction action) {
  ASSERT(callback != NULL);
  MemoryAllocationCallbackRegistration registration(callback, space, action);
  ASSERT(!MemoryAllocator::MemoryAllocationCallbackRegistered(callback));
  return memory_allocation_callbacks_.Add(registration);
}


void MemoryAllocator::RemoveMemoryAllocationCallback(
     MemoryAllocationCallback callback) {
  ASSERT(callback != NULL);
  for (int i = 0; i < memory_allocation_callbacks_.length(); ++i) {
    if (memory_allocation_callbacks_[i].callback == callback) {
      memory_allocation_callbacks_.Remove(i);
      return;
    }
  }
  UNREACHABLE();
}


#ifdef DEBUG
void MemoryAllocator::ReportStatistics() {
  float pct = static_cast<float>(capacity_ - size_) / capacity_;
  PrintF("  capacity: %" V8_PTR_PREFIX "d"
             ", used: %" V8_PTR_PREFIX "d"
             ", available: %%%d\n\n",
         capacity_, size_, static_cast<int>(pct*100));
}
#endif

// -----------------------------------------------------------------------------
// PagedSpace implementation

PagedSpace::PagedSpace(Heap* heap,
                       intptr_t max_capacity,
                       AllocationSpace id,
                       Executability executable)
    : Space(heap, id, executable),
      free_list_(this),
      was_swept_conservatively_(false),
      first_unswept_page_(Page::FromAddress(NULL)),
      last_unswept_page_(Page::FromAddress(NULL)) {
  max_capacity_ = (RoundDown(max_capacity, Page::kPageSize) / Page::kPageSize)
                  * Page::kObjectAreaSize;
  accounting_stats_.Clear();

  allocation_info_.top = NULL;
  allocation_info_.limit = NULL;

  anchor_.InitializeAsAnchor(this);
}


bool PagedSpace::Setup() {
  return true;
}


bool PagedSpace::HasBeenSetup() {
  return true;
}


void PagedSpace::TearDown() {
  PageIterator iterator(this);
  while (iterator.has_next()) {
    heap()->isolate()->memory_allocator()->Free(iterator.next());
  }
  anchor_.set_next_page(&anchor_);
  anchor_.set_prev_page(&anchor_);
  accounting_stats_.Clear();
}


MaybeObject* PagedSpace::FindObject(Address addr) {
  // Note: this function can only be called on precisely swept spaces.
  ASSERT(!heap()->mark_compact_collector()->in_use());

  if (!Contains(addr)) return Failure::Exception();

  Page* p = Page::FromAddress(addr);
  HeapObjectIterator it(p, NULL);
  for (HeapObject* obj = it.Next(); obj != NULL; obj = it.Next()) {
    Address cur = obj->address();
    Address next = cur + obj->Size();
    if ((cur <= addr) && (addr < next)) return obj;
  }

  UNREACHABLE();
  return Failure::Exception();
}

bool PagedSpace::CanExpand() {
  ASSERT(max_capacity_ % Page::kObjectAreaSize == 0);
  ASSERT(Capacity() % Page::kObjectAreaSize == 0);

  if (Capacity() == max_capacity_) return false;

  ASSERT(Capacity() < max_capacity_);

  // Are we going to exceed capacity for this space?
  if ((Capacity() + Page::kPageSize) > max_capacity_) return false;

  return true;
}

bool PagedSpace::Expand() {
  if (!CanExpand()) return false;

  Page* p = heap()->isolate()->memory_allocator()->
      AllocatePage(this, executable());
  if (p == NULL) return false;

  ASSERT(Capacity() <= max_capacity_);

  p->InsertAfter(anchor_.prev_page());

  return true;
}


#ifdef DEBUG
int PagedSpace::CountTotalPages() {
  PageIterator it(this);
  int count = 0;
  while (it.has_next()) {
    it.next();
    count++;
  }
  return count;
}
#endif


void PagedSpace::ReleasePage(Page* page) {
  ASSERT(page->LiveBytes() == 0);
  page->Unlink();
  if (page->IsFlagSet(MemoryChunk::CONTAINS_ONLY_DATA)) {
    heap()->isolate()->memory_allocator()->Free(page);
  } else {
    heap()->QueueMemoryChunkForFree(page);
  }

  ASSERT(Capacity() > 0);
  ASSERT(Capacity() % Page::kObjectAreaSize == 0);
  accounting_stats_.ShrinkSpace(Page::kObjectAreaSize);
}


void PagedSpace::ReleaseAllUnusedPages() {
  PageIterator it(this);
  while (it.has_next()) {
    Page* page = it.next();
    if (page->LiveBytes() == 0) {
      ReleasePage(page);
    }
  }
  heap()->FreeQueuedChunks();
}


#ifdef DEBUG
void PagedSpace::Print() { }
#endif


#ifdef DEBUG
void PagedSpace::Verify(ObjectVisitor* visitor) {
  // We can only iterate over the pages if they were swept precisely.
  if (was_swept_conservatively_) return;

  bool allocation_pointer_found_in_space =
      (allocation_info_.top == allocation_info_.limit);
  PageIterator page_iterator(this);
  while (page_iterator.has_next()) {
    Page* page = page_iterator.next();
    ASSERT(page->owner() == this);
    if (page == Page::FromAllocationTop(allocation_info_.top)) {
      allocation_pointer_found_in_space = true;
    }
    ASSERT(page->WasSweptPrecisely());
    HeapObjectIterator it(page, NULL);
    Address end_of_previous_object = page->ObjectAreaStart();
    Address top = page->ObjectAreaEnd();
    int black_size = 0;
    for (HeapObject* object = it.Next(); object != NULL; object = it.Next()) {
      ASSERT(end_of_previous_object <= object->address());

      // The first word should be a map, and we expect all map pointers to
      // be in map space.
      Map* map = object->map();
      ASSERT(map->IsMap());
      ASSERT(heap()->map_space()->Contains(map));

      // Perform space-specific object verification.
      VerifyObject(object);

      // The object itself should look OK.
      object->Verify();

      // All the interior pointers should be contained in the heap.
      int size = object->Size();
      object->IterateBody(map->instance_type(), size, visitor);
      if (Marking::IsBlack(Marking::MarkBitFrom(object))) {
        black_size += size;
      }

      ASSERT(object->address() + size <= top);
      end_of_previous_object = object->address() + size;
    }
    ASSERT_LE(black_size, page->LiveBytes());
  }
  ASSERT(allocation_pointer_found_in_space);
}
#endif


// -----------------------------------------------------------------------------
// NewSpace implementation


bool NewSpace::Setup(int reserved_semispace_capacity,
                     int maximum_semispace_capacity) {
  // Setup new space based on the preallocated memory block defined by
  // start and size. The provided space is divided into two semi-spaces.
  // To support fast containment testing in the new space, the size of
  // this chunk must be a power of two and it must be aligned to its size.
  int initial_semispace_capacity = heap()->InitialSemiSpaceSize();

  size_t size = 2 * reserved_semispace_capacity;
  Address base =
      heap()->isolate()->memory_allocator()->ReserveAlignedMemory(
          size, size, &reservation_);
  if (base == NULL) return false;

  chunk_base_ = base;
  chunk_size_ = static_cast<uintptr_t>(size);
  LOG(heap()->isolate(), NewEvent("InitialChunk", chunk_base_, chunk_size_));

  ASSERT(initial_semispace_capacity <= maximum_semispace_capacity);
  ASSERT(IsPowerOf2(maximum_semispace_capacity));

  // Allocate and setup the histogram arrays if necessary.
  allocated_histogram_ = NewArray<HistogramInfo>(LAST_TYPE + 1);
  promoted_histogram_ = NewArray<HistogramInfo>(LAST_TYPE + 1);

#define SET_NAME(name) allocated_histogram_[name].set_name(#name); \
                       promoted_histogram_[name].set_name(#name);
  INSTANCE_TYPE_LIST(SET_NAME)
#undef SET_NAME

  ASSERT(reserved_semispace_capacity == heap()->ReservedSemiSpaceSize());
  ASSERT(static_cast<intptr_t>(chunk_size_) >=
         2 * heap()->ReservedSemiSpaceSize());
  ASSERT(IsAddressAligned(chunk_base_, 2 * reserved_semispace_capacity, 0));

  if (!to_space_.Setup(chunk_base_,
                       initial_semispace_capacity,
                       maximum_semispace_capacity)) {
    return false;
  }
  if (!from_space_.Setup(chunk_base_ + reserved_semispace_capacity,
                         initial_semispace_capacity,
                         maximum_semispace_capacity)) {
    return false;
  }

  start_ = chunk_base_;
  address_mask_ = ~(2 * reserved_semispace_capacity - 1);
  object_mask_ = address_mask_ | kHeapObjectTagMask;
  object_expected_ = reinterpret_cast<uintptr_t>(start_) | kHeapObjectTag;

  ResetAllocationInfo();

  return true;
}


void NewSpace::TearDown() {
  if (allocated_histogram_) {
    DeleteArray(allocated_histogram_);
    allocated_histogram_ = NULL;
  }
  if (promoted_histogram_) {
    DeleteArray(promoted_histogram_);
    promoted_histogram_ = NULL;
  }

  start_ = NULL;
  allocation_info_.top = NULL;
  allocation_info_.limit = NULL;

  to_space_.TearDown();
  from_space_.TearDown();

  LOG(heap()->isolate(), DeleteEvent("InitialChunk", chunk_base_));

  ASSERT(reservation_.IsReserved());
  heap()->isolate()->memory_allocator()->FreeMemory(&reservation_,
                                                    NOT_EXECUTABLE);
  chunk_base_ = NULL;
  chunk_size_ = 0;
}


void NewSpace::Flip() {
  SemiSpace::Swap(&from_space_, &to_space_);
}


void NewSpace::Grow() {
  // Double the semispace size but only up to maximum capacity.
  ASSERT(Capacity() < MaximumCapacity());
  int new_capacity = Min(MaximumCapacity(), 2 * static_cast<int>(Capacity()));
  if (to_space_.GrowTo(new_capacity)) {
    // Only grow from space if we managed to grow to-space.
    if (!from_space_.GrowTo(new_capacity)) {
      // If we managed to grow to-space but couldn't grow from-space,
      // attempt to shrink to-space.
      if (!to_space_.ShrinkTo(from_space_.Capacity())) {
        // We are in an inconsistent state because we could not
        // commit/uncommit memory from new space.
        V8::FatalProcessOutOfMemory("Failed to grow new space.");
      }
    }
  }
  ASSERT_SEMISPACE_ALLOCATION_INFO(allocation_info_, to_space_);
}


void NewSpace::Shrink() {
  int new_capacity = Max(InitialCapacity(), 2 * SizeAsInt());
  int rounded_new_capacity = RoundUp(new_capacity, Page::kPageSize);
  if (rounded_new_capacity < Capacity() &&
      to_space_.ShrinkTo(rounded_new_capacity))  {
    // Only shrink from-space if we managed to shrink to-space.
    from_space_.Reset();
    if (!from_space_.ShrinkTo(rounded_new_capacity)) {
      // If we managed to shrink to-space but couldn't shrink from
      // space, attempt to grow to-space again.
      if (!to_space_.GrowTo(from_space_.Capacity())) {
        // We are in an inconsistent state because we could not
        // commit/uncommit memory from new space.
        V8::FatalProcessOutOfMemory("Failed to shrink new space.");
      }
    }
  }
  allocation_info_.limit = to_space_.page_high();
  ASSERT_SEMISPACE_ALLOCATION_INFO(allocation_info_, to_space_);
}


void NewSpace::UpdateAllocationInfo() {
  allocation_info_.top = to_space_.page_low();
  allocation_info_.limit = to_space_.page_high();

  // Lower limit during incremental marking.
  if (heap()->incremental_marking()->IsMarking() &&
      inline_allocation_limit_step() != 0) {
    Address new_limit =
        allocation_info_.top + inline_allocation_limit_step();
    allocation_info_.limit = Min(new_limit, allocation_info_.limit);
  }
  ASSERT_SEMISPACE_ALLOCATION_INFO(allocation_info_, to_space_);
}


void NewSpace::ResetAllocationInfo() {
  to_space_.Reset();
  UpdateAllocationInfo();
  pages_used_ = 0;
  // Clear all mark-bits in the to-space.
  NewSpacePageIterator it(&to_space_);
  while (it.has_next()) {
    Bitmap::Clear(it.next());
  }
}


bool NewSpace::AddFreshPage() {
  Address top = allocation_info_.top;
  if (NewSpacePage::IsAtStart(top)) {
    // The current page is already empty. Don't try to make another.

    // We should only get here if someone asks to allocate more
    // than what can be stored in a single page.
    // TODO(gc): Change the limit on new-space allocation to prevent this
    // from happening (all such allocations should go directly to LOSpace).
    return false;
  }
  if (!to_space_.AdvancePage()) {
    // Failed to get a new page in to-space.
    return false;
  }
  // Clear remainder of current page.
  int remaining_in_page =
    static_cast<int>(NewSpacePage::FromLimit(top)->body_limit() - top);
  heap()->CreateFillerObjectAt(top, remaining_in_page);
  pages_used_++;
  UpdateAllocationInfo();
  return true;
}


#ifdef DEBUG
// We do not use the SemiSpaceIterator because verification doesn't assume
// that it works (it depends on the invariants we are checking).
void NewSpace::Verify() {
  // The allocation pointer should be in the space or at the very end.
  ASSERT_SEMISPACE_ALLOCATION_INFO(allocation_info_, to_space_);

  // There should be objects packed in from the low address up to the
  // allocation pointer.
  Address current = to_space_.first_page()->body();
  CHECK_EQ(current, to_space_.space_start());

  while (current != top()) {
    if (!NewSpacePage::IsAtEnd(current)) {
      // The allocation pointer should not be in the middle of an object.
      CHECK(!NewSpacePage::FromLimit(current)->ContainsLimit(top()) ||
            current < top());

      HeapObject* object = HeapObject::FromAddress(current);

      // The first word should be a map, and we expect all map pointers to
      // be in map space.
      Map* map = object->map();
      CHECK(map->IsMap());
      CHECK(heap()->map_space()->Contains(map));

      // The object should not be code or a map.
      CHECK(!object->IsMap());
      CHECK(!object->IsCode());

      // The object itself should look OK.
      object->Verify();

      // All the interior pointers should be contained in the heap.
      VerifyPointersVisitor visitor;
      int size = object->Size();
      object->IterateBody(map->instance_type(), size, &visitor);

      current += size;
    } else {
      // At end of page, switch to next page.
      NewSpacePage* page = NewSpacePage::FromLimit(current)->next_page();
      // Next page should be valid.
      CHECK(!page->is_anchor());
      current = page->body();
    }
  }

  // Check semi-spaces.
  ASSERT_EQ(from_space_.id(), kFromSpace);
  ASSERT_EQ(to_space_.id(), kToSpace);
  from_space_.Verify();
  to_space_.Verify();
}
#endif

// -----------------------------------------------------------------------------
// SemiSpace implementation

bool SemiSpace::Setup(Address start,
                      int initial_capacity,
                      int maximum_capacity) {
  // Creates a space in the young generation. The constructor does not
  // allocate memory from the OS.  A SemiSpace is given a contiguous chunk of
  // memory of size 'capacity' when set up, and does not grow or shrink
  // otherwise.  In the mark-compact collector, the memory region of the from
  // space is used as the marking stack. It requires contiguous memory
  // addresses.
  ASSERT(maximum_capacity >= Page::kPageSize);
  initial_capacity_ = RoundDown(initial_capacity, Page::kPageSize);
  capacity_ = initial_capacity;
  maximum_capacity_ = RoundDown(maximum_capacity, Page::kPageSize);
  committed_ = false;
  start_ = start;
  address_mask_ = ~(maximum_capacity - 1);
  object_mask_ = address_mask_ | kHeapObjectTagMask;
  object_expected_ = reinterpret_cast<uintptr_t>(start) | kHeapObjectTag;
  age_mark_ = start_;

  return Commit();
}


void SemiSpace::TearDown() {
  start_ = NULL;
  capacity_ = 0;
}


bool SemiSpace::Commit() {
  ASSERT(!is_committed());
  int pages = capacity_ / Page::kPageSize;
  Address end = start_ + maximum_capacity_;
  Address start = end - pages * Page::kPageSize;
  if (!heap()->isolate()->memory_allocator()->CommitBlock(start,
                                                          capacity_,
                                                          executable())) {
    return false;
  }

  NewSpacePage* page = anchor();
  for (int i = 1; i <= pages; i++) {
    NewSpacePage* new_page =
      NewSpacePage::Initialize(heap(), end - i * Page::kPageSize, this);
    new_page->InsertAfter(page);
    page = new_page;
  }

  committed_ = true;
  Reset();
  return true;
}


bool SemiSpace::Uncommit() {
  ASSERT(is_committed());
  Address start = start_ + maximum_capacity_ - capacity_;
  if (!heap()->isolate()->memory_allocator()->UncommitBlock(start, capacity_)) {
    return false;
  }
  anchor()->set_next_page(anchor());
  anchor()->set_prev_page(anchor());

  committed_ = false;
  return true;
}


bool SemiSpace::GrowTo(int new_capacity) {
  ASSERT((new_capacity & Page::kPageAlignmentMask) == 0);
  ASSERT(new_capacity <= maximum_capacity_);
  ASSERT(new_capacity > capacity_);
  int pages_before = capacity_ / Page::kPageSize;
  int pages_after = new_capacity / Page::kPageSize;

  Address end = start_ + maximum_capacity_;
  Address start = end - new_capacity;
  size_t delta = new_capacity - capacity_;

  ASSERT(IsAligned(delta, OS::AllocateAlignment()));
  if (!heap()->isolate()->memory_allocator()->CommitBlock(
      start, delta, executable())) {
    return false;
  }
  capacity_ = new_capacity;
  NewSpacePage* last_page = anchor()->prev_page();
  ASSERT(last_page != anchor());
  for (int i = pages_before + 1; i <= pages_after; i++) {
    Address page_address = end - i * Page::kPageSize;
    NewSpacePage* new_page = NewSpacePage::Initialize(heap(),
                                                      page_address,
                                                      this);
    new_page->InsertAfter(last_page);
    Bitmap::Clear(new_page);
    // Duplicate the flags that was set on the old page.
    new_page->SetFlags(last_page->GetFlags(),
                       NewSpacePage::kCopyOnFlipFlagsMask);
    last_page = new_page;
  }
  return true;
}


bool SemiSpace::ShrinkTo(int new_capacity) {
  ASSERT((new_capacity & Page::kPageAlignmentMask) == 0);
  ASSERT(new_capacity >= initial_capacity_);
  ASSERT(new_capacity < capacity_);
  // Semispaces grow backwards from the end of their allocated capacity,
  // so we find the before and after start addresses relative to the
  // end of the space.
  Address space_end = start_ + maximum_capacity_;
  Address old_start = space_end - capacity_;
  size_t delta = capacity_ - new_capacity;
  ASSERT(IsAligned(delta, OS::AllocateAlignment()));
  if (!heap()->isolate()->memory_allocator()->UncommitBlock(old_start, delta)) {
    return false;
  }
  capacity_ = new_capacity;

  int pages_after = capacity_ / Page::kPageSize;
  NewSpacePage* new_last_page =
      NewSpacePage::FromAddress(space_end - pages_after * Page::kPageSize);
  new_last_page->set_next_page(anchor());
  anchor()->set_prev_page(new_last_page);
  ASSERT((current_page_ <= first_page()) && (current_page_ >= new_last_page));

  return true;
}


void SemiSpace::FlipPages(intptr_t flags, intptr_t mask) {
  anchor_.set_owner(this);
  // Fixup back-pointers to anchor. Address of anchor changes
  // when we swap.
  anchor_.prev_page()->set_next_page(&anchor_);
  anchor_.next_page()->set_prev_page(&anchor_);

  bool becomes_to_space = (id_ == kFromSpace);
  id_ = becomes_to_space ? kToSpace : kFromSpace;
  NewSpacePage* page = anchor_.next_page();
  while (page != &anchor_) {
    page->set_owner(this);
    page->SetFlags(flags, mask);
    if (becomes_to_space) {
      page->ClearFlag(MemoryChunk::IN_FROM_SPACE);
      page->SetFlag(MemoryChunk::IN_TO_SPACE);
      page->ClearFlag(MemoryChunk::NEW_SPACE_BELOW_AGE_MARK);
      page->ResetLiveBytes();
    } else {
      page->SetFlag(MemoryChunk::IN_FROM_SPACE);
      page->ClearFlag(MemoryChunk::IN_TO_SPACE);
    }
    ASSERT(page->IsFlagSet(MemoryChunk::SCAN_ON_SCAVENGE));
    ASSERT(page->IsFlagSet(MemoryChunk::IN_TO_SPACE) ||
           page->IsFlagSet(MemoryChunk::IN_FROM_SPACE));
    page = page->next_page();
  }
}


void SemiSpace::Reset() {
  ASSERT(anchor_.next_page() != &anchor_);
  current_page_ = anchor_.next_page();
}


void SemiSpace::Swap(SemiSpace* from, SemiSpace* to) {
  // We won't be swapping semispaces without data in them.
  ASSERT(from->anchor_.next_page() != &from->anchor_);
  ASSERT(to->anchor_.next_page() != &to->anchor_);

  // Swap bits.
  SemiSpace tmp = *from;
  *from = *to;
  *to = tmp;

  // Fixup back-pointers to the page list anchor now that its address
  // has changed.
  // Swap to/from-space bits on pages.
  // Copy GC flags from old active space (from-space) to new (to-space).
  intptr_t flags = from->current_page()->GetFlags();
  to->FlipPages(flags, NewSpacePage::kCopyOnFlipFlagsMask);

  from->FlipPages(0, 0);
}


void SemiSpace::set_age_mark(Address mark) {
  ASSERT(NewSpacePage::FromLimit(mark)->semi_space() == this);
  age_mark_ = mark;
  // Mark all pages up to the one containing mark.
  NewSpacePageIterator it(space_start(), mark);
  while (it.has_next()) {
    it.next()->SetFlag(MemoryChunk::NEW_SPACE_BELOW_AGE_MARK);
  }
}


#ifdef DEBUG
void SemiSpace::Print() { }


void SemiSpace::Verify() {
  bool is_from_space = (id_ == kFromSpace);
  NewSpacePage* page = anchor_.next_page();
  CHECK(anchor_.semi_space() == this);
  while (page != &anchor_) {
    CHECK(page->semi_space() == this);
    CHECK(page->InNewSpace());
    CHECK(page->IsFlagSet(is_from_space ? MemoryChunk::IN_FROM_SPACE
                                        : MemoryChunk::IN_TO_SPACE));
    CHECK(!page->IsFlagSet(is_from_space ? MemoryChunk::IN_TO_SPACE
                                         : MemoryChunk::IN_FROM_SPACE));
    CHECK(page->IsFlagSet(MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING));
    if (!is_from_space) {
      // The pointers-from-here-are-interesting flag isn't updated dynamically
      // on from-space pages, so it might be out of sync with the marking state.
      if (page->heap()->incremental_marking()->IsMarking()) {
        CHECK(page->IsFlagSet(MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING));
      } else {
        CHECK(!page->IsFlagSet(
            MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING));
      }
      // TODO(gc): Check that the live_bytes_count_ field matches the
      // black marking on the page (if we make it match in new-space).
    }
    CHECK(page->IsFlagSet(MemoryChunk::SCAN_ON_SCAVENGE));
    CHECK(page->prev_page()->next_page() == page);
    page = page->next_page();
  }
}


void SemiSpace::AssertValidRange(Address start, Address end) {
  // Addresses belong to same semi-space
  NewSpacePage* page = NewSpacePage::FromLimit(start);
  NewSpacePage* end_page = NewSpacePage::FromLimit(end);
  SemiSpace* space = page->semi_space();
  CHECK_EQ(space, end_page->semi_space());
  // Start address is before end address, either on same page,
  // or end address is on a later page in the linked list of
  // semi-space pages.
  if (page == end_page) {
    CHECK(start <= end);
  } else {
    while (page != end_page) {
      page = page->next_page();
      CHECK_NE(page, space->anchor());
    }
  }
}
#endif


// -----------------------------------------------------------------------------
// SemiSpaceIterator implementation.
SemiSpaceIterator::SemiSpaceIterator(NewSpace* space) {
  Initialize(space->bottom(), space->top(), NULL);
}


SemiSpaceIterator::SemiSpaceIterator(NewSpace* space,
                                     HeapObjectCallback size_func) {
  Initialize(space->bottom(), space->top(), size_func);
}


SemiSpaceIterator::SemiSpaceIterator(NewSpace* space, Address start) {
  Initialize(start, space->top(), NULL);
}


SemiSpaceIterator::SemiSpaceIterator(Address from, Address to) {
  Initialize(from, to, NULL);
}


void SemiSpaceIterator::Initialize(Address start,
                                   Address end,
                                   HeapObjectCallback size_func) {
  SemiSpace::AssertValidRange(start, end);
  current_ = start;
  limit_ = end;
  size_func_ = size_func;
}


#ifdef DEBUG
// heap_histograms is shared, always clear it before using it.
static void ClearHistograms() {
  Isolate* isolate = Isolate::Current();
  // We reset the name each time, though it hasn't changed.
#define DEF_TYPE_NAME(name) isolate->heap_histograms()[name].set_name(#name);
  INSTANCE_TYPE_LIST(DEF_TYPE_NAME)
#undef DEF_TYPE_NAME

#define CLEAR_HISTOGRAM(name) isolate->heap_histograms()[name].clear();
  INSTANCE_TYPE_LIST(CLEAR_HISTOGRAM)
#undef CLEAR_HISTOGRAM

  isolate->js_spill_information()->Clear();
}


static void ClearCodeKindStatistics() {
  Isolate* isolate = Isolate::Current();
  for (int i = 0; i < Code::NUMBER_OF_KINDS; i++) {
    isolate->code_kind_statistics()[i] = 0;
  }
}


static void ReportCodeKindStatistics() {
  Isolate* isolate = Isolate::Current();
  const char* table[Code::NUMBER_OF_KINDS] = { NULL };

#define CASE(name)                            \
  case Code::name: table[Code::name] = #name; \
  break

  for (int i = 0; i < Code::NUMBER_OF_KINDS; i++) {
    switch (static_cast<Code::Kind>(i)) {
      CASE(FUNCTION);
      CASE(OPTIMIZED_FUNCTION);
      CASE(STUB);
      CASE(BUILTIN);
      CASE(LOAD_IC);
      CASE(KEYED_LOAD_IC);
      CASE(STORE_IC);
      CASE(KEYED_STORE_IC);
      CASE(CALL_IC);
      CASE(KEYED_CALL_IC);
      CASE(UNARY_OP_IC);
      CASE(BINARY_OP_IC);
      CASE(COMPARE_IC);
      CASE(TO_BOOLEAN_IC);
    }
  }

#undef CASE

  PrintF("\n   Code kind histograms: \n");
  for (int i = 0; i < Code::NUMBER_OF_KINDS; i++) {
    if (isolate->code_kind_statistics()[i] > 0) {
      PrintF("     %-20s: %10d bytes\n", table[i],
          isolate->code_kind_statistics()[i]);
    }
  }
  PrintF("\n");
}


static int CollectHistogramInfo(HeapObject* obj) {
  Isolate* isolate = Isolate::Current();
  InstanceType type = obj->map()->instance_type();
  ASSERT(0 <= type && type <= LAST_TYPE);
  ASSERT(isolate->heap_histograms()[type].name() != NULL);
  isolate->heap_histograms()[type].increment_number(1);
  isolate->heap_histograms()[type].increment_bytes(obj->Size());

  if (FLAG_collect_heap_spill_statistics && obj->IsJSObject()) {
    JSObject::cast(obj)->IncrementSpillStatistics(
        isolate->js_spill_information());
  }

  return obj->Size();
}


static void ReportHistogram(bool print_spill) {
  Isolate* isolate = Isolate::Current();
  PrintF("\n  Object Histogram:\n");
  for (int i = 0; i <= LAST_TYPE; i++) {
    if (isolate->heap_histograms()[i].number() > 0) {
      PrintF("    %-34s%10d (%10d bytes)\n",
             isolate->heap_histograms()[i].name(),
             isolate->heap_histograms()[i].number(),
             isolate->heap_histograms()[i].bytes());
    }
  }
  PrintF("\n");

  // Summarize string types.
  int string_number = 0;
  int string_bytes = 0;
#define INCREMENT(type, size, name, camel_name)      \
    string_number += isolate->heap_histograms()[type].number(); \
    string_bytes += isolate->heap_histograms()[type].bytes();
  STRING_TYPE_LIST(INCREMENT)
#undef INCREMENT
  if (string_number > 0) {
    PrintF("    %-34s%10d (%10d bytes)\n\n", "STRING_TYPE", string_number,
           string_bytes);
  }

  if (FLAG_collect_heap_spill_statistics && print_spill) {
    isolate->js_spill_information()->Print();
  }
}
#endif  // DEBUG


// Support for statistics gathering for --heap-stats and --log-gc.
void NewSpace::ClearHistograms() {
  for (int i = 0; i <= LAST_TYPE; i++) {
    allocated_histogram_[i].clear();
    promoted_histogram_[i].clear();
  }
}

// Because the copying collector does not touch garbage objects, we iterate
// the new space before a collection to get a histogram of allocated objects.
// This only happens when --log-gc flag is set.
void NewSpace::CollectStatistics() {
  ClearHistograms();
  SemiSpaceIterator it(this);
  for (HeapObject* obj = it.Next(); obj != NULL; obj = it.Next())
    RecordAllocation(obj);
}


static void DoReportStatistics(Isolate* isolate,
                               HistogramInfo* info, const char* description) {
  LOG(isolate, HeapSampleBeginEvent("NewSpace", description));
  // Lump all the string types together.
  int string_number = 0;
  int string_bytes = 0;
#define INCREMENT(type, size, name, camel_name)       \
    string_number += info[type].number();             \
    string_bytes += info[type].bytes();
  STRING_TYPE_LIST(INCREMENT)
#undef INCREMENT
  if (string_number > 0) {
    LOG(isolate,
        HeapSampleItemEvent("STRING_TYPE", string_number, string_bytes));
  }

  // Then do the other types.
  for (int i = FIRST_NONSTRING_TYPE; i <= LAST_TYPE; ++i) {
    if (info[i].number() > 0) {
      LOG(isolate,
          HeapSampleItemEvent(info[i].name(), info[i].number(),
                              info[i].bytes()));
    }
  }
  LOG(isolate, HeapSampleEndEvent("NewSpace", description));
}


void NewSpace::ReportStatistics() {
#ifdef DEBUG
  if (FLAG_heap_stats) {
    float pct = static_cast<float>(Available()) / Capacity();
    PrintF("  capacity: %" V8_PTR_PREFIX "d"
               ", available: %" V8_PTR_PREFIX "d, %%%d\n",
           Capacity(), Available(), static_cast<int>(pct*100));
    PrintF("\n  Object Histogram:\n");
    for (int i = 0; i <= LAST_TYPE; i++) {
      if (allocated_histogram_[i].number() > 0) {
        PrintF("    %-34s%10d (%10d bytes)\n",
               allocated_histogram_[i].name(),
               allocated_histogram_[i].number(),
               allocated_histogram_[i].bytes());
      }
    }
    PrintF("\n");
  }
#endif  // DEBUG

  if (FLAG_log_gc) {
    Isolate* isolate = ISOLATE;
    DoReportStatistics(isolate, allocated_histogram_, "allocated");
    DoReportStatistics(isolate, promoted_histogram_, "promoted");
  }
}


void NewSpace::RecordAllocation(HeapObject* obj) {
  InstanceType type = obj->map()->instance_type();
  ASSERT(0 <= type && type <= LAST_TYPE);
  allocated_histogram_[type].increment_number(1);
  allocated_histogram_[type].increment_bytes(obj->Size());
}


void NewSpace::RecordPromotion(HeapObject* obj) {
  InstanceType type = obj->map()->instance_type();
  ASSERT(0 <= type && type <= LAST_TYPE);
  promoted_histogram_[type].increment_number(1);
  promoted_histogram_[type].increment_bytes(obj->Size());
}

// -----------------------------------------------------------------------------
// Free lists for old object spaces implementation

void FreeListNode::set_size(Heap* heap, int size_in_bytes) {
  ASSERT(size_in_bytes > 0);
  ASSERT(IsAligned(size_in_bytes, kPointerSize));

  // We write a map and possibly size information to the block.  If the block
  // is big enough to be a FreeSpace with at least one extra word (the next
  // pointer), we set its map to be the free space map and its size to an
  // appropriate array length for the desired size from HeapObject::Size().
  // If the block is too small (eg, one or two words), to hold both a size
  // field and a next pointer, we give it a filler map that gives it the
  // correct size.
  if (size_in_bytes > FreeSpace::kHeaderSize) {
    set_map(heap->raw_unchecked_free_space_map());
    // Can't use FreeSpace::cast because it fails during deserialization.
    FreeSpace* this_as_free_space = reinterpret_cast<FreeSpace*>(this);
    this_as_free_space->set_size(size_in_bytes);
  } else if (size_in_bytes == kPointerSize) {
    set_map(heap->raw_unchecked_one_pointer_filler_map());
  } else if (size_in_bytes == 2 * kPointerSize) {
    set_map(heap->raw_unchecked_two_pointer_filler_map());
  } else {
    UNREACHABLE();
  }
  // We would like to ASSERT(Size() == size_in_bytes) but this would fail during
  // deserialization because the free space map is not done yet.
}


FreeListNode* FreeListNode::next() {
  ASSERT(IsFreeListNode(this));
  if (map() == HEAP->raw_unchecked_free_space_map()) {
    ASSERT(map() == NULL || Size() >= kNextOffset + kPointerSize);
    return reinterpret_cast<FreeListNode*>(
        Memory::Address_at(address() + kNextOffset));
  } else {
    return reinterpret_cast<FreeListNode*>(
        Memory::Address_at(address() + kPointerSize));
  }
}


FreeListNode** FreeListNode::next_address() {
  ASSERT(IsFreeListNode(this));
  if (map() == HEAP->raw_unchecked_free_space_map()) {
    ASSERT(Size() >= kNextOffset + kPointerSize);
    return reinterpret_cast<FreeListNode**>(address() + kNextOffset);
  } else {
    return reinterpret_cast<FreeListNode**>(address() + kPointerSize);
  }
}


void FreeListNode::set_next(FreeListNode* next) {
  ASSERT(IsFreeListNode(this));
  // While we are booting the VM the free space map will actually be null.  So
  // we have to make sure that we don't try to use it for anything at that
  // stage.
  if (map() == HEAP->raw_unchecked_free_space_map()) {
    ASSERT(map() == NULL || Size() >= kNextOffset + kPointerSize);
    Memory::Address_at(address() + kNextOffset) =
        reinterpret_cast<Address>(next);
  } else {
    Memory::Address_at(address() + kPointerSize) =
        reinterpret_cast<Address>(next);
  }
}


FreeList::FreeList(PagedSpace* owner)
    : owner_(owner), heap_(owner->heap()) {
  Reset();
}


void FreeList::Reset() {
  available_ = 0;
  small_list_ = NULL;
  medium_list_ = NULL;
  large_list_ = NULL;
  huge_list_ = NULL;
}


int FreeList::Free(Address start, int size_in_bytes) {
  if (size_in_bytes == 0) return 0;
  FreeListNode* node = FreeListNode::FromAddress(start);
  node->set_size(heap_, size_in_bytes);

  // Early return to drop too-small blocks on the floor.
  if (size_in_bytes < kSmallListMin) return size_in_bytes;

  // Insert other blocks at the head of a free list of the appropriate
  // magnitude.
  if (size_in_bytes <= kSmallListMax) {
    node->set_next(small_list_);
    small_list_ = node;
  } else if (size_in_bytes <= kMediumListMax) {
    node->set_next(medium_list_);
    medium_list_ = node;
  } else if (size_in_bytes <= kLargeListMax) {
    node->set_next(large_list_);
    large_list_ = node;
  } else {
    node->set_next(huge_list_);
    huge_list_ = node;
  }
  available_ += size_in_bytes;
  ASSERT(IsVeryLong() || available_ == SumFreeLists());
  return 0;
}


FreeListNode* FreeList::PickNodeFromList(FreeListNode** list, int* node_size) {
  FreeListNode* node = *list;

  if (node == NULL) return NULL;

  while (node != NULL &&
         Page::FromAddress(node->address())->IsEvacuationCandidate()) {
    available_ -= node->Size();
    node = node->next();
  }

  if (node != NULL) {
    *node_size = node->Size();
    *list = node->next();
  } else {
    *list = NULL;
  }

  return node;
}


FreeListNode* FreeList::FindNodeFor(int size_in_bytes, int* node_size) {
  FreeListNode* node = NULL;

  if (size_in_bytes <= kSmallAllocationMax) {
    node = PickNodeFromList(&small_list_, node_size);
    if (node != NULL) return node;
  }

  if (size_in_bytes <= kMediumAllocationMax) {
    node = PickNodeFromList(&medium_list_, node_size);
    if (node != NULL) return node;
  }

  if (size_in_bytes <= kLargeAllocationMax) {
    node = PickNodeFromList(&large_list_, node_size);
    if (node != NULL) return node;
  }

  for (FreeListNode** cur = &huge_list_;
       *cur != NULL;
       cur = (*cur)->next_address()) {
    FreeListNode* cur_node = *cur;
    while (cur_node != NULL &&
           Page::FromAddress(cur_node->address())->IsEvacuationCandidate()) {
      available_ -= reinterpret_cast<FreeSpace*>(cur_node)->Size();
      cur_node = cur_node->next();
    }

    *cur = cur_node;
    if (cur_node == NULL) break;

    ASSERT((*cur)->map() == HEAP->raw_unchecked_free_space_map());
    FreeSpace* cur_as_free_space = reinterpret_cast<FreeSpace*>(*cur);
    int size = cur_as_free_space->Size();
    if (size >= size_in_bytes) {
      // Large enough node found.  Unlink it from the list.
      node = *cur;
      *node_size = size;
      *cur = node->next();
      break;
    }
  }

  return node;
}


// Allocation on the old space free list.  If it succeeds then a new linear
// allocation space has been set up with the top and limit of the space.  If
// the allocation fails then NULL is returned, and the caller can perform a GC
// or allocate a new page before retrying.
HeapObject* FreeList::Allocate(int size_in_bytes) {
  ASSERT(0 < size_in_bytes);
  ASSERT(size_in_bytes <= kMaxBlockSize);
  ASSERT(IsAligned(size_in_bytes, kPointerSize));
  // Don't free list allocate if there is linear space available.
  ASSERT(owner_->limit() - owner_->top() < size_in_bytes);

  int new_node_size = 0;
  FreeListNode* new_node = FindNodeFor(size_in_bytes, &new_node_size);
  if (new_node == NULL) return NULL;

  available_ -= new_node_size;
  ASSERT(IsVeryLong() || available_ == SumFreeLists());

  int bytes_left = new_node_size - size_in_bytes;
  ASSERT(bytes_left >= 0);

  int old_linear_size = static_cast<int>(owner_->limit() - owner_->top());
  // Mark the old linear allocation area with a free space map so it can be
  // skipped when scanning the heap.  This also puts it back in the free list
  // if it is big enough.
  owner_->Free(owner_->top(), old_linear_size);
  owner_->heap()->incremental_marking()->OldSpaceStep(
      size_in_bytes - old_linear_size);

  // The old-space-step might have finished sweeping and restarted marking.
  // Verify that it did not turn the page of the new node into an evacuation
  // candidate.
  ASSERT(!MarkCompactCollector::IsOnEvacuationCandidate(new_node));

  const int kThreshold = IncrementalMarking::kAllocatedThreshold;

  // Memory in the linear allocation area is counted as allocated.  We may free
  // a little of this again immediately - see below.
  owner_->Allocate(new_node_size);

  if (bytes_left > kThreshold &&
      owner_->heap()->incremental_marking()->IsMarkingIncomplete() &&
      FLAG_incremental_marking_steps) {
    int linear_size = owner_->RoundSizeDownToObjectAlignment(kThreshold);
    // We don't want to give too large linear areas to the allocator while
    // incremental marking is going on, because we won't check again whether
    // we want to do another increment until the linear area is used up.
    owner_->Free(new_node->address() + size_in_bytes + linear_size,
                 new_node_size - size_in_bytes - linear_size);
    owner_->SetTop(new_node->address() + size_in_bytes,
                   new_node->address() + size_in_bytes + linear_size);
  } else if (bytes_left > 0) {
    // Normally we give the rest of the node to the allocator as its new
    // linear allocation area.
    owner_->SetTop(new_node->address() + size_in_bytes,
                   new_node->address() + new_node_size);
  } else {
    // TODO(gc) Try not freeing linear allocation region when bytes_left
    // are zero.
    owner_->SetTop(NULL, NULL);
  }

  return new_node;
}


static intptr_t CountFreeListItemsInList(FreeListNode* n, Page* p) {
  intptr_t sum = 0;
  while (n != NULL) {
    if (Page::FromAddress(n->address()) == p) {
      FreeSpace* free_space = reinterpret_cast<FreeSpace*>(n);
      sum += free_space->Size();
    }
    n = n->next();
  }
  return sum;
}


void FreeList::CountFreeListItems(Page* p, intptr_t* sizes) {
  sizes[0] = CountFreeListItemsInList(small_list_, p);
  sizes[1] = CountFreeListItemsInList(medium_list_, p);
  sizes[2] = CountFreeListItemsInList(large_list_, p);
  sizes[3] = CountFreeListItemsInList(huge_list_, p);
}

#ifdef DEBUG
intptr_t FreeList::SumFreeList(FreeListNode* cur) {
  intptr_t sum = 0;
  while (cur != NULL) {
    ASSERT(cur->map() == HEAP->raw_unchecked_free_space_map());
    FreeSpace* cur_as_free_space = reinterpret_cast<FreeSpace*>(cur);
    sum += cur_as_free_space->Size();
    cur = cur->next();
  }
  return sum;
}


static const int kVeryLongFreeList = 500;


int FreeList::FreeListLength(FreeListNode* cur) {
  int length = 0;
  while (cur != NULL) {
    length++;
    cur = cur->next();
    if (length == kVeryLongFreeList) return length;
  }
  return length;
}


bool FreeList::IsVeryLong() {
  if (FreeListLength(small_list_) == kVeryLongFreeList) return  true;
  if (FreeListLength(medium_list_) == kVeryLongFreeList) return  true;
  if (FreeListLength(large_list_) == kVeryLongFreeList) return  true;
  if (FreeListLength(huge_list_) == kVeryLongFreeList) return  true;
  return false;
}


// This can take a very long time because it is linear in the number of entries
// on the free list, so it should not be called if FreeListLength returns
// kVeryLongFreeList.
intptr_t FreeList::SumFreeLists() {
  intptr_t sum = SumFreeList(small_list_);
  sum += SumFreeList(medium_list_);
  sum += SumFreeList(large_list_);
  sum += SumFreeList(huge_list_);
  return sum;
}
#endif


// -----------------------------------------------------------------------------
// OldSpace implementation

bool NewSpace::ReserveSpace(int bytes) {
  // We can't reliably unpack a partial snapshot that needs more new space
  // space than the minimum NewSpace size.
  ASSERT(bytes <= InitialCapacity());
  Address limit = allocation_info_.limit;
  Address top = allocation_info_.top;
  return limit - top >= bytes;
}


void PagedSpace::PrepareForMarkCompact() {
  // We don't have a linear allocation area while sweeping.  It will be restored
  // on the first allocation after the sweep.
  // Mark the old linear allocation area with a free space map so it can be
  // skipped when scanning the heap.
  int old_linear_size = static_cast<int>(limit() - top());
  Free(top(), old_linear_size);
  SetTop(NULL, NULL);

  // Stop lazy sweeping and clear marking bits for unswept pages.
  if (first_unswept_page_ != NULL) {
    Page* last = last_unswept_page_;
    Page* p = first_unswept_page_;
    do {
      // Do not use ShouldBeSweptLazily predicate here.
      // New evacuation candidates were selected but they still have
      // to be swept before collection starts.
      if (!p->WasSwept()) {
        Bitmap::Clear(p);
        if (FLAG_gc_verbose) {
          PrintF("Sweeping 0x%" V8PRIxPTR " lazily abandoned.\n",
                 reinterpret_cast<intptr_t>(p));
        }
      }
      p = p->next_page();
    } while (p != last);
  }
  first_unswept_page_ = last_unswept_page_ = Page::FromAddress(NULL);

  // Clear the free list before a full GC---it will be rebuilt afterward.
  free_list_.Reset();
}


bool PagedSpace::ReserveSpace(int size_in_bytes) {
  ASSERT(size_in_bytes <= Page::kMaxHeapObjectSize);
  ASSERT(size_in_bytes == RoundSizeDownToObjectAlignment(size_in_bytes));
  Address current_top = allocation_info_.top;
  Address new_top = current_top + size_in_bytes;
  if (new_top <= allocation_info_.limit) return true;

  HeapObject* new_area = free_list_.Allocate(size_in_bytes);
  if (new_area == NULL) new_area = SlowAllocateRaw(size_in_bytes);
  if (new_area == NULL) return false;

  int old_linear_size = static_cast<int>(limit() - top());
  // Mark the old linear allocation area with a free space so it can be
  // skipped when scanning the heap.  This also puts it back in the free list
  // if it is big enough.
  Free(top(), old_linear_size);

  SetTop(new_area->address(), new_area->address() + size_in_bytes);
  Allocate(size_in_bytes);
  return true;
}


// You have to call this last, since the implementation from PagedSpace
// doesn't know that memory was 'promised' to large object space.
bool LargeObjectSpace::ReserveSpace(int bytes) {
  return heap()->OldGenerationSpaceAvailable() >= bytes;
}


bool PagedSpace::AdvanceSweeper(intptr_t bytes_to_sweep) {
  if (IsSweepingComplete()) return true;

  intptr_t freed_bytes = 0;
  Page* last = last_unswept_page_;
  Page* p = first_unswept_page_;
  do {
    Page* next_page = p->next_page();
    if (ShouldBeSweptLazily(p)) {
      if (FLAG_gc_verbose) {
        PrintF("Sweeping 0x%" V8PRIxPTR " lazily advanced.\n",
               reinterpret_cast<intptr_t>(p));
      }
      freed_bytes += MarkCompactCollector::SweepConservatively(this, p);
    }
    p = next_page;
  } while (p != last && freed_bytes < bytes_to_sweep);

  if (p == last) {
    last_unswept_page_ = first_unswept_page_ = Page::FromAddress(NULL);
  } else {
    first_unswept_page_ = p;
  }

  heap()->LowerOldGenLimits(freed_bytes);

  heap()->FreeQueuedChunks();

  return IsSweepingComplete();
}


void PagedSpace::EvictEvacuationCandidatesFromFreeLists() {
  if (allocation_info_.top >= allocation_info_.limit) return;

  if (Page::FromAddress(allocation_info_.top)->IsEvacuationCandidate()) {
    // Create filler object to keep page iterable if it was iterable.
    int remaining =
        static_cast<int>(allocation_info_.limit - allocation_info_.top);
    heap()->CreateFillerObjectAt(allocation_info_.top, remaining);

    allocation_info_.top = NULL;
    allocation_info_.limit = NULL;
  }
}


HeapObject* PagedSpace::SlowAllocateRaw(int size_in_bytes) {
  // Allocation in this space has failed.

  // Free list allocation failed and there is no next page.  Fail if we have
  // hit the old generation size limit that should cause a garbage
  // collection.
  if (!heap()->always_allocate() &&
      heap()->OldGenerationAllocationLimitReached()) {
    return NULL;
  }

  // If there are unswept pages advance lazy sweeper.
  if (first_unswept_page_->is_valid()) {
    AdvanceSweeper(size_in_bytes);

    // Retry the free list allocation.
    HeapObject* object = free_list_.Allocate(size_in_bytes);
    if (object != NULL) return object;

    if (!IsSweepingComplete()) {
      AdvanceSweeper(kMaxInt);

      // Retry the free list allocation.
      object = free_list_.Allocate(size_in_bytes);
      if (object != NULL) return object;
    }
  }

  // Try to expand the space and allocate in the new next page.
  if (Expand()) {
    return free_list_.Allocate(size_in_bytes);
  }

  // Finally, fail.
  return NULL;
}


#ifdef DEBUG
void PagedSpace::ReportCodeStatistics() {
  Isolate* isolate = Isolate::Current();
  CommentStatistic* comments_statistics =
      isolate->paged_space_comments_statistics();
  ReportCodeKindStatistics();
  PrintF("Code comment statistics (\"   [ comment-txt   :    size/   "
         "count  (average)\"):\n");
  for (int i = 0; i <= CommentStatistic::kMaxComments; i++) {
    const CommentStatistic& cs = comments_statistics[i];
    if (cs.size > 0) {
      PrintF("   %-30s: %10d/%6d     (%d)\n", cs.comment, cs.size, cs.count,
             cs.size/cs.count);
    }
  }
  PrintF("\n");
}


void PagedSpace::ResetCodeStatistics() {
  Isolate* isolate = Isolate::Current();
  CommentStatistic* comments_statistics =
      isolate->paged_space_comments_statistics();
  ClearCodeKindStatistics();
  for (int i = 0; i < CommentStatistic::kMaxComments; i++) {
    comments_statistics[i].Clear();
  }
  comments_statistics[CommentStatistic::kMaxComments].comment = "Unknown";
  comments_statistics[CommentStatistic::kMaxComments].size = 0;
  comments_statistics[CommentStatistic::kMaxComments].count = 0;
}


// Adds comment to 'comment_statistics' table. Performance OK as long as
// 'kMaxComments' is small
static void EnterComment(Isolate* isolate, const char* comment, int delta) {
  CommentStatistic* comments_statistics =
      isolate->paged_space_comments_statistics();
  // Do not count empty comments
  if (delta <= 0) return;
  CommentStatistic* cs = &comments_statistics[CommentStatistic::kMaxComments];
  // Search for a free or matching entry in 'comments_statistics': 'cs'
  // points to result.
  for (int i = 0; i < CommentStatistic::kMaxComments; i++) {
    if (comments_statistics[i].comment == NULL) {
      cs = &comments_statistics[i];
      cs->comment = comment;
      break;
    } else if (strcmp(comments_statistics[i].comment, comment) == 0) {
      cs = &comments_statistics[i];
      break;
    }
  }
  // Update entry for 'comment'
  cs->size += delta;
  cs->count += 1;
}


// Call for each nested comment start (start marked with '[ xxx', end marked
// with ']'.  RelocIterator 'it' must point to a comment reloc info.
static void CollectCommentStatistics(Isolate* isolate, RelocIterator* it) {
  ASSERT(!it->done());
  ASSERT(it->rinfo()->rmode() == RelocInfo::COMMENT);
  const char* tmp = reinterpret_cast<const char*>(it->rinfo()->data());
  if (tmp[0] != '[') {
    // Not a nested comment; skip
    return;
  }

  // Search for end of nested comment or a new nested comment
  const char* const comment_txt =
      reinterpret_cast<const char*>(it->rinfo()->data());
  const byte* prev_pc = it->rinfo()->pc();
  int flat_delta = 0;
  it->next();
  while (true) {
    // All nested comments must be terminated properly, and therefore exit
    // from loop.
    ASSERT(!it->done());
    if (it->rinfo()->rmode() == RelocInfo::COMMENT) {
      const char* const txt =
          reinterpret_cast<const char*>(it->rinfo()->data());
      flat_delta += static_cast<int>(it->rinfo()->pc() - prev_pc);
      if (txt[0] == ']') break;  // End of nested  comment
      // A new comment
      CollectCommentStatistics(isolate, it);
      // Skip code that was covered with previous comment
      prev_pc = it->rinfo()->pc();
    }
    it->next();
  }
  EnterComment(isolate, comment_txt, flat_delta);
}


// Collects code size statistics:
// - by code kind
// - by code comment
void PagedSpace::CollectCodeStatistics() {
  Isolate* isolate = heap()->isolate();
  HeapObjectIterator obj_it(this);
  for (HeapObject* obj = obj_it.Next(); obj != NULL; obj = obj_it.Next()) {
    if (obj->IsCode()) {
      Code* code = Code::cast(obj);
      isolate->code_kind_statistics()[code->kind()] += code->Size();
      RelocIterator it(code);
      int delta = 0;
      const byte* prev_pc = code->instruction_start();
      while (!it.done()) {
        if (it.rinfo()->rmode() == RelocInfo::COMMENT) {
          delta += static_cast<int>(it.rinfo()->pc() - prev_pc);
          CollectCommentStatistics(isolate, &it);
          prev_pc = it.rinfo()->pc();
        }
        it.next();
      }

      ASSERT(code->instruction_start() <= prev_pc &&
             prev_pc <= code->instruction_end());
      delta += static_cast<int>(code->instruction_end() - prev_pc);
      EnterComment(isolate, "NoComment", delta);
    }
  }
}


void PagedSpace::ReportStatistics() {
  int pct = static_cast<int>(Available() * 100 / Capacity());
  PrintF("  capacity: %" V8_PTR_PREFIX "d"
             ", waste: %" V8_PTR_PREFIX "d"
             ", available: %" V8_PTR_PREFIX "d, %%%d\n",
         Capacity(), Waste(), Available(), pct);

  if (was_swept_conservatively_) return;
  ClearHistograms();
  HeapObjectIterator obj_it(this);
  for (HeapObject* obj = obj_it.Next(); obj != NULL; obj = obj_it.Next())
    CollectHistogramInfo(obj);
  ReportHistogram(true);
}
#endif

// -----------------------------------------------------------------------------
// FixedSpace implementation

void FixedSpace::PrepareForMarkCompact() {
  // Call prepare of the super class.
  PagedSpace::PrepareForMarkCompact();

  // During a non-compacting collection, everything below the linear
  // allocation pointer except wasted top-of-page blocks is considered
  // allocated and we will rediscover available bytes during the
  // collection.
  accounting_stats_.AllocateBytes(free_list_.available());

  // Clear the free list before a full GC---it will be rebuilt afterward.
  free_list_.Reset();
}


// -----------------------------------------------------------------------------
// MapSpace implementation

#ifdef DEBUG
void MapSpace::VerifyObject(HeapObject* object) {
  // The object should be a map or a free-list node.
  ASSERT(object->IsMap() || object->IsFreeSpace());
}
#endif


// -----------------------------------------------------------------------------
// GlobalPropertyCellSpace implementation

#ifdef DEBUG
void CellSpace::VerifyObject(HeapObject* object) {
  // The object should be a global object property cell or a free-list node.
  ASSERT(object->IsJSGlobalPropertyCell() ||
         object->map() == heap()->two_pointer_filler_map());
}
#endif


// -----------------------------------------------------------------------------
// LargeObjectIterator

LargeObjectIterator::LargeObjectIterator(LargeObjectSpace* space) {
  current_ = space->first_page_;
  size_func_ = NULL;
}


LargeObjectIterator::LargeObjectIterator(LargeObjectSpace* space,
                                         HeapObjectCallback size_func) {
  current_ = space->first_page_;
  size_func_ = size_func;
}


HeapObject* LargeObjectIterator::Next() {
  if (current_ == NULL) return NULL;

  HeapObject* object = current_->GetObject();
  current_ = current_->next_page();
  return object;
}


// -----------------------------------------------------------------------------
// LargeObjectSpace

LargeObjectSpace::LargeObjectSpace(Heap* heap, AllocationSpace id)
    : Space(heap, id, NOT_EXECUTABLE),  // Managed on a per-allocation basis
      first_page_(NULL),
      size_(0),
      page_count_(0),
      objects_size_(0) {}


bool LargeObjectSpace::Setup() {
  first_page_ = NULL;
  size_ = 0;
  page_count_ = 0;
  objects_size_ = 0;
  return true;
}


void LargeObjectSpace::TearDown() {
  while (first_page_ != NULL) {
    LargePage* page = first_page_;
    first_page_ = first_page_->next_page();
    LOG(heap()->isolate(), DeleteEvent("LargeObjectChunk", page->address()));

    ObjectSpace space = static_cast<ObjectSpace>(1 << identity());
    heap()->isolate()->memory_allocator()->PerformAllocationCallback(
        space, kAllocationActionFree, page->size());
    heap()->isolate()->memory_allocator()->Free(page);
  }
  Setup();
}


MaybeObject* LargeObjectSpace::AllocateRaw(int object_size,
                                           Executability executable) {
  // Check if we want to force a GC before growing the old space further.
  // If so, fail the allocation.
  if (!heap()->always_allocate() &&
      heap()->OldGenerationAllocationLimitReached()) {
    return Failure::RetryAfterGC(identity());
  }

  LargePage* page = heap()->isolate()->memory_allocator()->
      AllocateLargePage(object_size, executable, this);
  if (page == NULL) return Failure::RetryAfterGC(identity());
  ASSERT(page->body_size() >= object_size);

  size_ += static_cast<int>(page->size());
  objects_size_ += object_size;
  page_count_++;
  page->set_next_page(first_page_);
  first_page_ = page;

  heap()->incremental_marking()->OldSpaceStep(object_size);
  return page->GetObject();
}


// GC support
MaybeObject* LargeObjectSpace::FindObject(Address a) {
  for (LargePage* page = first_page_;
       page != NULL;
       page = page->next_page()) {
    Address page_address = page->address();
    if (page_address <= a && a < page_address + page->size()) {
      return page->GetObject();
    }
  }
  return Failure::Exception();
}


LargePage* LargeObjectSpace::FindPageContainingPc(Address pc) {
  // TODO(853): Change this implementation to only find executable
  // chunks and use some kind of hash-based approach to speed it up.
  for (LargePage* chunk = first_page_;
       chunk != NULL;
       chunk = chunk->next_page()) {
    Address chunk_address = chunk->address();
    if (chunk_address <= pc && pc < chunk_address + chunk->size()) {
      return chunk;
    }
  }
  return NULL;
}


void LargeObjectSpace::FreeUnmarkedObjects() {
  LargePage* previous = NULL;
  LargePage* current = first_page_;
  while (current != NULL) {
    HeapObject* object = current->GetObject();
    // Can this large page contain pointers to non-trivial objects.  No other
    // pointer object is this big.
    bool is_pointer_object = object->IsFixedArray();
    MarkBit mark_bit = Marking::MarkBitFrom(object);
    if (mark_bit.Get()) {
      mark_bit.Clear();
      MemoryChunk::IncrementLiveBytes(object->address(), -object->Size());
      previous = current;
      current = current->next_page();
    } else {
      LargePage* page = current;
      // Cut the chunk out from the chunk list.
      current = current->next_page();
      if (previous == NULL) {
        first_page_ = current;
      } else {
        previous->set_next_page(current);
      }

      // Free the chunk.
      heap()->mark_compact_collector()->ReportDeleteIfNeeded(
          object, heap()->isolate());
      size_ -= static_cast<int>(page->size());
      objects_size_ -= object->Size();
      page_count_--;

      if (is_pointer_object) {
        heap()->QueueMemoryChunkForFree(page);
      } else {
        heap()->isolate()->memory_allocator()->Free(page);
      }
    }
  }
  heap()->FreeQueuedChunks();
}


bool LargeObjectSpace::Contains(HeapObject* object) {
  Address address = object->address();
  MemoryChunk* chunk = MemoryChunk::FromAddress(address);

  bool owned = (chunk->owner() == this);

  SLOW_ASSERT(!owned || !FindObject(address)->IsFailure());

  return owned;
}


#ifdef DEBUG
// We do not assume that the large object iterator works, because it depends
// on the invariants we are checking during verification.
void LargeObjectSpace::Verify() {
  for (LargePage* chunk = first_page_;
       chunk != NULL;
       chunk = chunk->next_page()) {
    // Each chunk contains an object that starts at the large object page's
    // object area start.
    HeapObject* object = chunk->GetObject();
    Page* page = Page::FromAddress(object->address());
    ASSERT(object->address() == page->ObjectAreaStart());

    // The first word should be a map, and we expect all map pointers to be
    // in map space.
    Map* map = object->map();
    ASSERT(map->IsMap());
    ASSERT(heap()->map_space()->Contains(map));

    // We have only code, sequential strings, external strings
    // (sequential strings that have been morphed into external
    // strings), fixed arrays, and byte arrays in large object space.
    ASSERT(object->IsCode() || object->IsSeqString() ||
           object->IsExternalString() || object->IsFixedArray() ||
           object->IsFixedDoubleArray() || object->IsByteArray());

    // The object itself should look OK.
    object->Verify();

    // Byte arrays and strings don't have interior pointers.
    if (object->IsCode()) {
      VerifyPointersVisitor code_visitor;
      object->IterateBody(map->instance_type(),
                          object->Size(),
                          &code_visitor);
    } else if (object->IsFixedArray()) {
      FixedArray* array = FixedArray::cast(object);
      for (int j = 0; j < array->length(); j++) {
        Object* element = array->get(j);
        if (element->IsHeapObject()) {
          HeapObject* element_object = HeapObject::cast(element);
          ASSERT(heap()->Contains(element_object));
          ASSERT(element_object->map()->IsMap());
        }
      }
    }
  }
}


void LargeObjectSpace::Print() {
  LargeObjectIterator it(this);
  for (HeapObject* obj = it.Next(); obj != NULL; obj = it.Next()) {
    obj->Print();
  }
}


void LargeObjectSpace::ReportStatistics() {
  PrintF("  size: %" V8_PTR_PREFIX "d\n", size_);
  int num_objects = 0;
  ClearHistograms();
  LargeObjectIterator it(this);
  for (HeapObject* obj = it.Next(); obj != NULL; obj = it.Next()) {
    num_objects++;
    CollectHistogramInfo(obj);
  }

  PrintF("  number of objects %d, "
         "size of objects %" V8_PTR_PREFIX "d\n", num_objects, objects_size_);
  if (num_objects > 0) ReportHistogram(false);
}


void LargeObjectSpace::CollectCodeStatistics() {
  Isolate* isolate = heap()->isolate();
  LargeObjectIterator obj_it(this);
  for (HeapObject* obj = obj_it.Next(); obj != NULL; obj = obj_it.Next()) {
    if (obj->IsCode()) {
      Code* code = Code::cast(obj);
      isolate->code_kind_statistics()[code->kind()] += code->Size();
    }
  }
}


void Page::Print() {
  // Make a best-effort to print the objects in the page.
  PrintF("Page@%p in %s\n",
         this->address(),
         AllocationSpaceName(this->owner()->identity()));
  printf(" --------------------------------------\n");
  HeapObjectIterator objects(this, heap()->GcSafeSizeOfOldObjectFunction());
  unsigned mark_size = 0;
  for (HeapObject* object = objects.Next();
       object != NULL;
       object = objects.Next()) {
    bool is_marked = Marking::MarkBitFrom(object).Get();
    PrintF(" %c ", (is_marked ? '!' : ' '));  // Indent a little.
    if (is_marked) {
      mark_size += heap()->GcSafeSizeOfOldObjectFunction()(object);
    }
    object->ShortPrint();
    PrintF("\n");
  }
  printf(" --------------------------------------\n");
  printf(" Marked: %x, LiveCount: %x\n", mark_size, LiveBytes());
}

#endif  // DEBUG

} }  // namespace v8::internal
