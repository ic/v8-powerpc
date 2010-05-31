// Copyright 2006-2008 the V8 project authors. All rights reserved.
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

#ifndef V8_SPACES_H_
#define V8_SPACES_H_

#include "list-inl.h"
#include "log.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// Heap structures:
//
// A JS heap consists of a young generation, an old generation, and a large
// object space. The young generation is divided into two semispaces. A
// scavenger implements Cheney's copying algorithm. The old generation is
// separated into a map space and an old object space. The map space contains
// all (and only) map objects, the rest of old objects go into the old space.
// The old generation is collected by a mark-sweep-compact collector.
//
// The semispaces of the young generation are contiguous.  The old and map
// spaces consists of a list of pages. A page has a page header and an object
// area. A page size is deliberately chosen as 8K bytes.
// The first word of a page is an opaque page header that has the
// address of the next page and its ownership information. The second word may
// have the allocation top address of this page. Heap objects are aligned to the
// pointer size.
//
// There is a separate large object space for objects larger than
// Page::kMaxHeapObjectSize, so that they do not have to move during
// collection. The large object space is paged. Pages in large object space
// may be larger than 8K.
//
// A card marking write barrier is used to keep track of intergenerational
// references. Old space pages are divided into regions of Page::kRegionSize
// size. Each region has a corresponding dirty bit in the page header which is
// set if the region might contain pointers to new space. For details about
// dirty bits encoding see comments in the Page::GetRegionNumberForAddress()
// method body.
//
// During scavenges and mark-sweep collections we iterate intergenerational
// pointers without decoding heap object maps so if the page belongs to old
// pointer space or large object space it is essential to guarantee that
// the page does not contain any garbage pointers to new space: every pointer
// aligned word which satisfies the Heap::InNewSpace() predicate must be a
// pointer to a live heap object in new space. Thus objects in old pointer
// and large object spaces should have a special layout (e.g. no bare integer
// fields). This requirement does not apply to map space which is iterated in
// a special fashion. However we still require pointer fields of dead maps to
// be cleaned.
//
// To enable lazy cleaning of old space pages we use a notion of allocation
// watermark. Every pointer under watermark is considered to be well formed.
// Page allocation watermark is not necessarily equal to page allocation top but
// all alive objects on page should reside under allocation watermark.
// During scavenge allocation watermark might be bumped and invalid pointers
// might appear below it. To avoid following them we store a valid watermark
// into special field in the page header and set a page WATERMARK_INVALIDATED
// flag. For details see comments in the Page::SetAllocationWatermark() method
// body.
//

// Some assertion macros used in the debugging mode.

#define ASSERT_PAGE_ALIGNED(address)                                           \
  ASSERT((OffsetFrom(address) & Page::kPageAlignmentMask) == 0)

#define ASSERT_OBJECT_ALIGNED(address)                                         \
  ASSERT((OffsetFrom(address) & kObjectAlignmentMask) == 0)

#define ASSERT_MAP_ALIGNED(address)                                            \
  ASSERT((OffsetFrom(address) & kMapAlignmentMask) == 0)

#define ASSERT_OBJECT_SIZE(size)                                               \
  ASSERT((0 < size) && (size <= Page::kMaxHeapObjectSize))

#define ASSERT_PAGE_OFFSET(offset)                                             \
  ASSERT((Page::kObjectStartOffset <= offset)                                  \
      && (offset <= Page::kPageSize))

#define ASSERT_MAP_PAGE_INDEX(index)                                           \
  ASSERT((0 <= index) && (index <= MapSpace::kMaxMapPageIndex))


class PagedSpace;
class MemoryAllocator;
class AllocationInfo;

// -----------------------------------------------------------------------------
// A page normally has 8K bytes. Large object pages may be larger.  A page
// address is always aligned to the 8K page size.
//
// Each page starts with a header of Page::kPageHeaderSize size which contains
// bookkeeping data.
//
// The mark-compact collector transforms a map pointer into a page index and a
// page offset. The exact encoding is described in the comments for
// class MapWord in objects.h.
//
// The only way to get a page pointer is by calling factory methods:
//   Page* p = Page::FromAddress(addr); or
//   Page* p = Page::FromAllocationTop(top);
class Page {
 public:
  // Returns the page containing a given address. The address ranges
  // from [page_addr .. page_addr + kPageSize[
  //
  // Note that this function only works for addresses in normal paged
  // spaces and addresses in the first 8K of large object pages (i.e.,
  // the start of large objects but not necessarily derived pointers
  // within them).
  INLINE(static Page* FromAddress(Address a)) {
    return reinterpret_cast<Page*>(OffsetFrom(a) & ~kPageAlignmentMask);
  }

  // Returns the page containing an allocation top. Because an allocation
  // top address can be the upper bound of the page, we need to subtract
  // it with kPointerSize first. The address ranges from
  // [page_addr + kObjectStartOffset .. page_addr + kPageSize].
  INLINE(static Page* FromAllocationTop(Address top)) {
    Page* p = FromAddress(top - kPointerSize);
    ASSERT_PAGE_OFFSET(p->Offset(top));
    return p;
  }

  // Returns the start address of this page.
  Address address() { return reinterpret_cast<Address>(this); }

  // Checks whether this is a valid page address.
  bool is_valid() { return address() != NULL; }

  // Returns the next page of this page.
  inline Page* next_page();

  // Return the end of allocation in this page. Undefined for unused pages.
  inline Address AllocationTop();

  // Return the allocation watermark for the page.
  // For old space pages it is guaranteed that the area under the watermark
  // does not contain any garbage pointers to new space.
  inline Address AllocationWatermark();

  // Return the allocation watermark offset from the beginning of the page.
  inline uint32_t AllocationWatermarkOffset();

  inline void SetAllocationWatermark(Address allocation_watermark);

  inline void SetCachedAllocationWatermark(Address allocation_watermark);
  inline Address CachedAllocationWatermark();

  // Returns the start address of the object area in this page.
  Address ObjectAreaStart() { return address() + kObjectStartOffset; }

  // Returns the end address (exclusive) of the object area in this page.
  Address ObjectAreaEnd() { return address() + Page::kPageSize; }

  // Checks whether an address is page aligned.
  static bool IsAlignedToPageSize(Address a) {
    return 0 == (OffsetFrom(a) & kPageAlignmentMask);
  }

  // True if this page was in use before current compaction started.
  // Result is valid only for pages owned by paged spaces and
  // only after PagedSpace::PrepareForMarkCompact was called.
  inline bool WasInUseBeforeMC();

  inline void SetWasInUseBeforeMC(bool was_in_use);

  // True if this page is a large object page.
  inline bool IsLargeObjectPage();

  inline void SetIsLargeObjectPage(bool is_large_object_page);

  // Returns the offset of a given address to this page.
  INLINE(int Offset(Address a)) {
    int offset = static_cast<int>(a - address());
    ASSERT_PAGE_OFFSET(offset);
    return offset;
  }

  // Returns the address for a given offset to the this page.
  Address OffsetToAddress(int offset) {
    ASSERT_PAGE_OFFSET(offset);
    return address() + offset;
  }

  // ---------------------------------------------------------------------
  // Card marking support

  static const uint32_t kAllRegionsCleanMarks = 0x0;
  static const uint32_t kAllRegionsDirtyMarks = 0xFFFFFFFF;

  inline uint32_t GetRegionMarks();
  inline void SetRegionMarks(uint32_t dirty);

  inline uint32_t GetRegionMaskForAddress(Address addr);
  inline int GetRegionNumberForAddress(Address addr);

  inline void MarkRegionDirty(Address addr);
  inline bool IsRegionDirty(Address addr);

  inline void ClearRegionMarks(Address start,
                               Address end,
                               bool reaches_limit);

  // Page size in bytes.  This must be a multiple of the OS page size.
  static const int kPageSize = 1 << kPageSizeBits;

  // Page size mask.
  static const intptr_t kPageAlignmentMask = (1 << kPageSizeBits) - 1;

  static const int kPageHeaderSize = kPointerSize + kPointerSize + kIntSize +
    kIntSize + kPointerSize;

  // The start offset of the object area in a page.
  static const int kObjectStartOffset = MAP_POINTER_ALIGN(kPageHeaderSize);

  // Object area size in bytes.
  static const int kObjectAreaSize = kPageSize - kObjectStartOffset;

  // Maximum object size that fits in a page.
  static const int kMaxHeapObjectSize = kObjectAreaSize;

  static const int kDirtyFlagOffset = 2 * kPointerSize;
  static const int kRegionSizeLog2 = 8;
  static const int kRegionSize = 1 << kRegionSizeLog2;
  static const intptr_t kRegionAlignmentMask = (kRegionSize - 1);

  STATIC_CHECK(kRegionSize == kPageSize / kBitsPerInt);

  enum PageFlag {
    IS_NORMAL_PAGE = 1 << 0,
    WAS_IN_USE_BEFORE_MC = 1 << 1,

    // Page allocation watermark was bumped by preallocation during scavenge.
    // Correct watermark can be retrieved by CachedAllocationWatermark() method
    WATERMARK_INVALIDATED = 1 << 2
  };

  // To avoid an additional WATERMARK_INVALIDATED flag clearing pass during
  // scavenge we just invalidate the watermark on each old space page after
  // processing it. And then we flip the meaning of the WATERMARK_INVALIDATED
  // flag at the beginning of the next scavenge and each page becomes marked as
  // having a valid watermark.
  //
  // The following invariant must hold for pages in old pointer and map spaces:
  //     If page is in use then page is marked as having invalid watermark at
  //     the beginning and at the end of any GC.
  //
  // This invariant guarantees that after flipping flag meaning at the
  // beginning of scavenge all pages in use will be marked as having valid
  // watermark.
  static inline void FlipMeaningOfInvalidatedWatermarkFlag();

  // Returns true if the page allocation watermark was not altered during
  // scavenge.
  inline bool IsWatermarkValid();

  inline void InvalidateWatermark(bool value);

  inline bool GetPageFlag(PageFlag flag);
  inline void SetPageFlag(PageFlag flag, bool value);
  inline void ClearPageFlags();

  inline void ClearGCFields();

  static const int kAllocationWatermarkOffsetShift = 3;
  static const int kAllocationWatermarkOffsetBits  = kPageSizeBits + 1;
  static const uint32_t kAllocationWatermarkOffsetMask =
      ((1 << kAllocationWatermarkOffsetBits) - 1) <<
      kAllocationWatermarkOffsetShift;

  static const uint32_t kFlagsMask =
    ((1 << kAllocationWatermarkOffsetShift) - 1);

  STATIC_CHECK(kBitsPerInt - kAllocationWatermarkOffsetShift >=
               kAllocationWatermarkOffsetBits);

  // This field contains the meaning of the WATERMARK_INVALIDATED flag.
  // Instead of clearing this flag from all pages we just flip
  // its meaning at the beginning of a scavenge.
  static intptr_t watermark_invalidated_mark_;

  //---------------------------------------------------------------------------
  // Page header description.
  //
  // If a page is not in the large object space, the first word,
  // opaque_header, encodes the next page address (aligned to kPageSize 8K)
  // and the chunk number (0 ~ 8K-1).  Only MemoryAllocator should use
  // opaque_header. The value range of the opaque_header is [0..kPageSize[,
  // or [next_page_start, next_page_end[. It cannot point to a valid address
  // in the current page.  If a page is in the large object space, the first
  // word *may* (if the page start and large object chunk start are the
  // same) contain the address of the next large object chunk.
  intptr_t opaque_header;

  // If the page is not in the large object space, the low-order bit of the
  // second word is set. If the page is in the large object space, the
  // second word *may* (if the page start and large object chunk start are
  // the same) contain the large object chunk size.  In either case, the
  // low-order bit for large object pages will be cleared.
  // For normal pages this word is used to store page flags and
  // offset of allocation top.
  intptr_t flags_;

  // This field contains dirty marks for regions covering the page. Only dirty
  // regions might contain intergenerational references.
  // Only 32 dirty marks are supported so for large object pages several regions
  // might be mapped to a single dirty mark.
  uint32_t dirty_regions_;

  // The index of the page in its owner space.
  int mc_page_index;

  // During mark-compact collections this field contains the forwarding address
  // of the first live object in this page.
  // During scavenge collection this field is used to store allocation watermark
  // if it is altered during scavenge.
  Address mc_first_forwarded;
};


// ----------------------------------------------------------------------------
// Space is the abstract superclass for all allocation spaces.
class Space : public Malloced {
 public:
  Space(AllocationSpace id, Executability executable)
      : id_(id), executable_(executable) {}

  virtual ~Space() {}

  // Does the space need executable memory?
  Executability executable() { return executable_; }

  // Identity used in error reporting.
  AllocationSpace identity() { return id_; }

  virtual int Size() = 0;

#ifdef ENABLE_HEAP_PROTECTION
  // Protect/unprotect the space by marking it read-only/writable.
  virtual void Protect() = 0;
  virtual void Unprotect() = 0;
#endif

#ifdef DEBUG
  virtual void Print() = 0;
#endif

  // After calling this we can allocate a certain number of bytes using only
  // linear allocation (with a LinearAllocationScope and an AlwaysAllocateScope)
  // without using freelists or causing a GC.  This is used by partial
  // snapshots.  It returns true of space was reserved or false if a GC is
  // needed.  For paged spaces the space requested must include the space wasted
  // at the end of each when allocating linearly.
  virtual bool ReserveSpace(int bytes) = 0;

 private:
  AllocationSpace id_;
  Executability executable_;
};


// ----------------------------------------------------------------------------
// All heap objects containing executable code (code objects) must be allocated
// from a 2 GB range of memory, so that they can call each other using 32-bit
// displacements.  This happens automatically on 32-bit platforms, where 32-bit
// displacements cover the entire 4GB virtual address space.  On 64-bit
// platforms, we support this using the CodeRange object, which reserves and
// manages a range of virtual memory.
class CodeRange : public AllStatic {
 public:
  // Reserves a range of virtual memory, but does not commit any of it.
  // Can only be called once, at heap initialization time.
  // Returns false on failure.
  static bool Setup(const size_t requested_size);

  // Frees the range of virtual memory, and frees the data structures used to
  // manage it.
  static void TearDown();

  static bool exists() { return code_range_ != NULL; }
  static bool contains(Address address) {
    if (code_range_ == NULL) return false;
    Address start = static_cast<Address>(code_range_->address());
    return start <= address && address < start + code_range_->size();
  }

  // Allocates a chunk of memory from the large-object portion of
  // the code range.  On platforms with no separate code range, should
  // not be called.
  static void* AllocateRawMemory(const size_t requested, size_t* allocated);
  static void FreeRawMemory(void* buf, size_t length);

 private:
  // The reserved range of virtual memory that all code objects are put in.
  static VirtualMemory* code_range_;
  // Plain old data class, just a struct plus a constructor.
  class FreeBlock {
   public:
    FreeBlock(Address start_arg, size_t size_arg)
        : start(start_arg), size(size_arg) {}
    FreeBlock(void* start_arg, size_t size_arg)
        : start(static_cast<Address>(start_arg)), size(size_arg) {}

    Address start;
    size_t size;
  };

  // Freed blocks of memory are added to the free list.  When the allocation
  // list is exhausted, the free list is sorted and merged to make the new
  // allocation list.
  static List<FreeBlock> free_list_;
  // Memory is allocated from the free blocks on the allocation list.
  // The block at current_allocation_block_index_ is the current block.
  static List<FreeBlock> allocation_list_;
  static int current_allocation_block_index_;

  // Finds a block on the allocation list that contains at least the
  // requested amount of memory.  If none is found, sorts and merges
  // the existing free memory blocks, and searches again.
  // If none can be found, terminates V8 with FatalProcessOutOfMemory.
  static void GetNextAllocationBlock(size_t requested);
  // Compares the start addresses of two free blocks.
  static int CompareFreeBlockAddress(const FreeBlock* left,
                                     const FreeBlock* right);
};


// ----------------------------------------------------------------------------
// A space acquires chunks of memory from the operating system. The memory
// allocator manages chunks for the paged heap spaces (old space and map
// space).  A paged chunk consists of pages. Pages in a chunk have contiguous
// addresses and are linked as a list.
//
// The allocator keeps an initial chunk which is used for the new space.  The
// leftover regions of the initial chunk are used for the initial chunks of
// old space and map space if they are big enough to hold at least one page.
// The allocator assumes that there is one old space and one map space, each
// expands the space by allocating kPagesPerChunk pages except the last
// expansion (before running out of space).  The first chunk may contain fewer
// than kPagesPerChunk pages as well.
//
// The memory allocator also allocates chunks for the large object space, but
// they are managed by the space itself.  The new space does not expand.
//
// The fact that pages for paged spaces are allocated and deallocated in chunks
// induces a constraint on the order of pages in a linked lists. We say that
// pages are linked in the chunk-order if and only if every two consecutive
// pages from the same chunk are consecutive in the linked list.
//


class MemoryAllocator : public AllStatic {
 public:
  // Initializes its internal bookkeeping structures.
  // Max capacity of the total space.
  static bool Setup(int max_capacity);

  // Deletes valid chunks.
  static void TearDown();

  // Reserves an initial address range of virtual memory to be split between
  // the two new space semispaces, the old space, and the map space.  The
  // memory is not yet committed or assigned to spaces and split into pages.
  // The initial chunk is unmapped when the memory allocator is torn down.
  // This function should only be called when there is not already a reserved
  // initial chunk (initial_chunk_ should be NULL).  It returns the start
  // address of the initial chunk if successful, with the side effect of
  // setting the initial chunk, or else NULL if unsuccessful and leaves the
  // initial chunk NULL.
  static void* ReserveInitialChunk(const size_t requested);

  // Commits pages from an as-yet-unmanaged block of virtual memory into a
  // paged space.  The block should be part of the initial chunk reserved via
  // a call to ReserveInitialChunk.  The number of pages is always returned in
  // the output parameter num_pages.  This function assumes that the start
  // address is non-null and that it is big enough to hold at least one
  // page-aligned page.  The call always succeeds, and num_pages is always
  // greater than zero.
  static Page* CommitPages(Address start, size_t size, PagedSpace* owner,
                           int* num_pages);

  // Commit a contiguous block of memory from the initial chunk.  Assumes that
  // the address is not NULL, the size is greater than zero, and that the
  // block is contained in the initial chunk.  Returns true if it succeeded
  // and false otherwise.
  static bool CommitBlock(Address start, size_t size, Executability executable);

  // Uncommit a contiguous block of memory [start..(start+size)[.
  // start is not NULL, the size is greater than zero, and the
  // block is contained in the initial chunk.  Returns true if it succeeded
  // and false otherwise.
  static bool UncommitBlock(Address start, size_t size);

  // Zaps a contiguous block of memory [start..(start+size)[ thus
  // filling it up with a recognizable non-NULL bit pattern.
  static void ZapBlock(Address start, size_t size);

  // Attempts to allocate the requested (non-zero) number of pages from the
  // OS.  Fewer pages might be allocated than requested. If it fails to
  // allocate memory for the OS or cannot allocate a single page, this
  // function returns an invalid page pointer (NULL). The caller must check
  // whether the returned page is valid (by calling Page::is_valid()).  It is
  // guaranteed that allocated pages have contiguous addresses.  The actual
  // number of allocated pages is returned in the output parameter
  // allocated_pages.  If the PagedSpace owner is executable and there is
  // a code range, the pages are allocated from the code range.
  static Page* AllocatePages(int requested_pages, int* allocated_pages,
                             PagedSpace* owner);

  // Frees pages from a given page and after. Requires pages to be
  // linked in chunk-order (see comment for class).
  // If 'p' is the first page of a chunk, pages from 'p' are freed
  // and this function returns an invalid page pointer.
  // Otherwise, the function searches a page after 'p' that is
  // the first page of a chunk. Pages after the found page
  // are freed and the function returns 'p'.
  static Page* FreePages(Page* p);

  // Frees all pages owned by given space.
  static void FreeAllPages(PagedSpace* space);

  // Allocates and frees raw memory of certain size.
  // These are just thin wrappers around OS::Allocate and OS::Free,
  // but keep track of allocated bytes as part of heap.
  // If the flag is EXECUTABLE and a code range exists, the requested
  // memory is allocated from the code range.  If a code range exists
  // and the freed memory is in it, the code range manages the freed memory.
  static void* AllocateRawMemory(const size_t requested,
                                 size_t* allocated,
                                 Executability executable);
  static void FreeRawMemory(void* buf, size_t length);

  // Returns the maximum available bytes of heaps.
  static int Available() { return capacity_ < size_ ? 0 : capacity_ - size_; }

  // Returns allocated spaces in bytes.
  static int Size() { return size_; }

  // Returns maximum available bytes that the old space can have.
  static int MaxAvailable() {
    return (Available() / Page::kPageSize) * Page::kObjectAreaSize;
  }

  // Links two pages.
  static inline void SetNextPage(Page* prev, Page* next);

  // Returns the next page of a given page.
  static inline Page* GetNextPage(Page* p);

  // Checks whether a page belongs to a space.
  static inline bool IsPageInSpace(Page* p, PagedSpace* space);

  // Returns the space that owns the given page.
  static inline PagedSpace* PageOwner(Page* page);

  // Finds the first/last page in the same chunk as a given page.
  static Page* FindFirstPageInSameChunk(Page* p);
  static Page* FindLastPageInSameChunk(Page* p);

  // Relinks list of pages owned by space to make it chunk-ordered.
  // Returns new first and last pages of space.
  // Also returns last page in relinked list which has WasInUsedBeforeMC
  // flag set.
  static void RelinkPageListInChunkOrder(PagedSpace* space,
                                         Page** first_page,
                                         Page** last_page,
                                         Page** last_page_in_use);

#ifdef ENABLE_HEAP_PROTECTION
  // Protect/unprotect a block of memory by marking it read-only/writable.
  static inline void Protect(Address start, size_t size);
  static inline void Unprotect(Address start, size_t size,
                               Executability executable);

  // Protect/unprotect a chunk given a page in the chunk.
  static inline void ProtectChunkFromPage(Page* page);
  static inline void UnprotectChunkFromPage(Page* page);
#endif

#ifdef DEBUG
  // Reports statistic info of the space.
  static void ReportStatistics();
#endif

  // Due to encoding limitation, we can only have 8K chunks.
  static const int kMaxNofChunks = 1 << kPageSizeBits;
  // If a chunk has at least 16 pages, the maximum heap size is about
  // 8K * 8K * 16 = 1G bytes.
#ifdef V8_TARGET_ARCH_X64
  static const int kPagesPerChunk = 32;
#else
  static const int kPagesPerChunk = 16;
#endif
  static const int kChunkSize = kPagesPerChunk * Page::kPageSize;

 private:
  // Maximum space size in bytes.
  static int capacity_;

  // Allocated space size in bytes.
  static int size_;

  // The initial chunk of virtual memory.
  static VirtualMemory* initial_chunk_;

  // Allocated chunk info: chunk start address, chunk size, and owning space.
  class ChunkInfo BASE_EMBEDDED {
   public:
    ChunkInfo() : address_(NULL), size_(0), owner_(NULL) {}
    void init(Address a, size_t s, PagedSpace* o) {
      address_ = a;
      size_ = s;
      owner_ = o;
    }
    Address address() { return address_; }
    size_t size() { return size_; }
    PagedSpace* owner() { return owner_; }

   private:
    Address address_;
    size_t size_;
    PagedSpace* owner_;
  };

  // Chunks_, free_chunk_ids_ and top_ act as a stack of free chunk ids.
  static List<ChunkInfo> chunks_;
  static List<int> free_chunk_ids_;
  static int max_nof_chunks_;
  static int top_;

  // Push/pop a free chunk id onto/from the stack.
  static void Push(int free_chunk_id);
  static int Pop();
  static bool OutOfChunkIds() { return top_ == 0; }

  // Frees a chunk.
  static void DeleteChunk(int chunk_id);

  // Basic check whether a chunk id is in the valid range.
  static inline bool IsValidChunkId(int chunk_id);

  // Checks whether a chunk id identifies an allocated chunk.
  static inline bool IsValidChunk(int chunk_id);

  // Returns the chunk id that a page belongs to.
  static inline int GetChunkId(Page* p);

  // True if the address lies in the initial chunk.
  static inline bool InInitialChunk(Address address);

  // Initializes pages in a chunk. Returns the first page address.
  // This function and GetChunkId() are provided for the mark-compact
  // collector to rebuild page headers in the from space, which is
  // used as a marking stack and its page headers are destroyed.
  static Page* InitializePagesInChunk(int chunk_id, int pages_in_chunk,
                                      PagedSpace* owner);

  static Page* RelinkPagesInChunk(int chunk_id,
                                  Address chunk_start,
                                  size_t chunk_size,
                                  Page* prev,
                                  Page** last_page_in_use);
};


// -----------------------------------------------------------------------------
// Interface for heap object iterator to be implemented by all object space
// object iterators.
//
// NOTE: The space specific object iterators also implements the own next()
//       method which is used to avoid using virtual functions
//       iterating a specific space.

class ObjectIterator : public Malloced {
 public:
  virtual ~ObjectIterator() { }

  virtual HeapObject* next_object() = 0;
};


// -----------------------------------------------------------------------------
// Heap object iterator in new/old/map spaces.
//
// A HeapObjectIterator iterates objects from a given address to the
// top of a space. The given address must be below the current
// allocation pointer (space top). There are some caveats.
//
// (1) If the space top changes upward during iteration (because of
//     allocating new objects), the iterator does not iterate objects
//     above the original space top. The caller must create a new
//     iterator starting from the old top in order to visit these new
//     objects.
//
// (2) If new objects are allocated below the original allocation top
//     (e.g., free-list allocation in paged spaces), the new objects
//     may or may not be iterated depending on their position with
//     respect to the current point of iteration.
//
// (3) The space top should not change downward during iteration,
//     otherwise the iterator will return not-necessarily-valid
//     objects.

class HeapObjectIterator: public ObjectIterator {
 public:
  // Creates a new object iterator in a given space. If a start
  // address is not given, the iterator starts from the space bottom.
  // If the size function is not given, the iterator calls the default
  // Object::Size().
  explicit HeapObjectIterator(PagedSpace* space);
  HeapObjectIterator(PagedSpace* space, HeapObjectCallback size_func);
  HeapObjectIterator(PagedSpace* space, Address start);
  HeapObjectIterator(PagedSpace* space,
                     Address start,
                     HeapObjectCallback size_func);

  inline HeapObject* next() {
    return (cur_addr_ < cur_limit_) ? FromCurrentPage() : FromNextPage();
  }

  // implementation of ObjectIterator.
  virtual HeapObject* next_object() { return next(); }

 private:
  Address cur_addr_;  // current iteration point
  Address end_addr_;  // end iteration point
  Address cur_limit_;  // current page limit
  HeapObjectCallback size_func_;  // size function
  Page* end_page_;  // caches the page of the end address

  HeapObject* FromCurrentPage() {
    ASSERT(cur_addr_ < cur_limit_);

    HeapObject* obj = HeapObject::FromAddress(cur_addr_);
    int obj_size = (size_func_ == NULL) ? obj->Size() : size_func_(obj);
    ASSERT_OBJECT_SIZE(obj_size);

    cur_addr_ += obj_size;
    ASSERT(cur_addr_ <= cur_limit_);

    return obj;
  }

  // Slow path of next, goes into the next page.
  HeapObject* FromNextPage();

  // Initializes fields.
  void Initialize(Address start, Address end, HeapObjectCallback size_func);

#ifdef DEBUG
  // Verifies whether fields have valid values.
  void Verify();
#endif
};


// -----------------------------------------------------------------------------
// A PageIterator iterates the pages in a paged space.
//
// The PageIterator class provides three modes for iterating pages in a space:
//   PAGES_IN_USE iterates pages containing allocated objects.
//   PAGES_USED_BY_MC iterates pages that hold relocated objects during a
//                    mark-compact collection.
//   ALL_PAGES iterates all pages in the space.
//
// There are some caveats.
//
// (1) If the space expands during iteration, new pages will not be
//     returned by the iterator in any mode.
//
// (2) If new objects are allocated during iteration, they will appear
//     in pages returned by the iterator.  Allocation may cause the
//     allocation pointer or MC allocation pointer in the last page to
//     change between constructing the iterator and iterating the last
//     page.
//
// (3) The space should not shrink during iteration, otherwise the
//     iterator will return deallocated pages.

class PageIterator BASE_EMBEDDED {
 public:
  enum Mode {
    PAGES_IN_USE,
    PAGES_USED_BY_MC,
    ALL_PAGES
  };

  PageIterator(PagedSpace* space, Mode mode);

  inline bool has_next();
  inline Page* next();

 private:
  PagedSpace* space_;
  Page* prev_page_;  // Previous page returned.
  Page* stop_page_;  // Page to stop at (last page returned by the iterator).
};


// -----------------------------------------------------------------------------
// A space has a list of pages. The next page can be accessed via
// Page::next_page() call. The next page of the last page is an
// invalid page pointer. A space can expand and shrink dynamically.

// An abstraction of allocation and relocation pointers in a page-structured
// space.
class AllocationInfo {
 public:
  Address top;  // current allocation top
  Address limit;  // current allocation limit

#ifdef DEBUG
  bool VerifyPagedAllocation() {
    return (Page::FromAllocationTop(top) == Page::FromAllocationTop(limit))
        && (top <= limit);
  }
#endif
};


// An abstraction of the accounting statistics of a page-structured space.
// The 'capacity' of a space is the number of object-area bytes (ie, not
// including page bookkeeping structures) currently in the space. The 'size'
// of a space is the number of allocated bytes, the 'waste' in the space is
// the number of bytes that are not allocated and not available to
// allocation without reorganizing the space via a GC (eg, small blocks due
// to internal fragmentation, top of page areas in map space), and the bytes
// 'available' is the number of unallocated bytes that are not waste.  The
// capacity is the sum of size, waste, and available.
//
// The stats are only set by functions that ensure they stay balanced. These
// functions increase or decrease one of the non-capacity stats in
// conjunction with capacity, or else they always balance increases and
// decreases to the non-capacity stats.
class AllocationStats BASE_EMBEDDED {
 public:
  AllocationStats() { Clear(); }

  // Zero out all the allocation statistics (ie, no capacity).
  void Clear() {
    capacity_ = 0;
    available_ = 0;
    size_ = 0;
    waste_ = 0;
  }

  // Reset the allocation statistics (ie, available = capacity with no
  // wasted or allocated bytes).
  void Reset() {
    available_ = capacity_;
    size_ = 0;
    waste_ = 0;
  }

  // Accessors for the allocation statistics.
  int Capacity() { return capacity_; }
  int Available() { return available_; }
  int Size() { return size_; }
  int Waste() { return waste_; }

  // Grow the space by adding available bytes.
  void ExpandSpace(int size_in_bytes) {
    capacity_ += size_in_bytes;
    available_ += size_in_bytes;
  }

  // Shrink the space by removing available bytes.
  void ShrinkSpace(int size_in_bytes) {
    capacity_ -= size_in_bytes;
    available_ -= size_in_bytes;
  }

  // Allocate from available bytes (available -> size).
  void AllocateBytes(int size_in_bytes) {
    available_ -= size_in_bytes;
    size_ += size_in_bytes;
  }

  // Free allocated bytes, making them available (size -> available).
  void DeallocateBytes(int size_in_bytes) {
    size_ -= size_in_bytes;
    available_ += size_in_bytes;
  }

  // Waste free bytes (available -> waste).
  void WasteBytes(int size_in_bytes) {
    available_ -= size_in_bytes;
    waste_ += size_in_bytes;
  }

  // Consider the wasted bytes to be allocated, as they contain filler
  // objects (waste -> size).
  void FillWastedBytes(int size_in_bytes) {
    waste_ -= size_in_bytes;
    size_ += size_in_bytes;
  }

 private:
  int capacity_;
  int available_;
  int size_;
  int waste_;
};


class PagedSpace : public Space {
 public:
  // Creates a space with a maximum capacity, and an id.
  PagedSpace(int max_capacity, AllocationSpace id, Executability executable);

  virtual ~PagedSpace() {}

  // Set up the space using the given address range of virtual memory (from
  // the memory allocator's initial chunk) if possible.  If the block of
  // addresses is not big enough to contain a single page-aligned page, a
  // fresh chunk will be allocated.
  bool Setup(Address start, size_t size);

  // Returns true if the space has been successfully set up and not
  // subsequently torn down.
  bool HasBeenSetup();

  // Cleans up the space, frees all pages in this space except those belonging
  // to the initial chunk, uncommits addresses in the initial chunk.
  void TearDown();

  // Checks whether an object/address is in this space.
  inline bool Contains(Address a);
  bool Contains(HeapObject* o) { return Contains(o->address()); }

  // Given an address occupied by a live object, return that object if it is
  // in this space, or Failure::Exception() if it is not. The implementation
  // iterates over objects in the page containing the address, the cost is
  // linear in the number of objects in the page. It may be slow.
  Object* FindObject(Address addr);

  // Checks whether page is currently in use by this space.
  bool IsUsed(Page* page);

  void MarkAllPagesClean();

  // Prepares for a mark-compact GC.
  virtual void PrepareForMarkCompact(bool will_compact);

  // The top of allocation in a page in this space. Undefined if page is unused.
  Address PageAllocationTop(Page* page) {
    return page == TopPageOf(allocation_info_) ? top()
        : PageAllocationLimit(page);
  }

  // The limit of allocation for a page in this space.
  virtual Address PageAllocationLimit(Page* page) = 0;

  void FlushTopPageWatermark() {
    AllocationTopPage()->SetCachedAllocationWatermark(top());
    AllocationTopPage()->InvalidateWatermark(true);
  }

  // Current capacity without growing (Size() + Available() + Waste()).
  int Capacity() { return accounting_stats_.Capacity(); }

  // Total amount of memory committed for this space.  For paged
  // spaces this equals the capacity.
  int CommittedMemory() { return Capacity(); }

  // Available bytes without growing.
  int Available() { return accounting_stats_.Available(); }

  // Allocated bytes in this space.
  virtual int Size() { return accounting_stats_.Size(); }

  // Wasted bytes due to fragmentation and not recoverable until the
  // next GC of this space.
  int Waste() { return accounting_stats_.Waste(); }

  // Returns the address of the first object in this space.
  Address bottom() { return first_page_->ObjectAreaStart(); }

  // Returns the allocation pointer in this space.
  Address top() { return allocation_info_.top; }

  // Allocate the requested number of bytes in the space if possible, return a
  // failure object if not.
  inline Object* AllocateRaw(int size_in_bytes);

  // Allocate the requested number of bytes for relocation during mark-compact
  // collection.
  inline Object* MCAllocateRaw(int size_in_bytes);

  virtual bool ReserveSpace(int bytes);

  // Used by ReserveSpace.
  virtual void PutRestOfCurrentPageOnFreeList(Page* current_page) = 0;

  // Free all pages in range from prev (exclusive) to last (inclusive).
  // Freed pages are moved to the end of page list.
  void FreePages(Page* prev, Page* last);

  // Set space allocation info.
  void SetTop(Address top) {
    allocation_info_.top = top;
    allocation_info_.limit = PageAllocationLimit(Page::FromAllocationTop(top));
  }

  // ---------------------------------------------------------------------------
  // Mark-compact collection support functions

  // Set the relocation point to the beginning of the space.
  void MCResetRelocationInfo();

  // Writes relocation info to the top page.
  void MCWriteRelocationInfoToPage() {
    TopPageOf(mc_forwarding_info_)->
        SetAllocationWatermark(mc_forwarding_info_.top);
  }

  // Computes the offset of a given address in this space to the beginning
  // of the space.
  int MCSpaceOffsetForAddress(Address addr);

  // Updates the allocation pointer to the relocation top after a mark-compact
  // collection.
  virtual void MCCommitRelocationInfo() = 0;

  // Releases half of unused pages.
  void Shrink();

  // Ensures that the capacity is at least 'capacity'. Returns false on failure.
  bool EnsureCapacity(int capacity);

#ifdef ENABLE_HEAP_PROTECTION
  // Protect/unprotect the space by marking it read-only/writable.
  void Protect();
  void Unprotect();
#endif

#ifdef DEBUG
  // Print meta info and objects in this space.
  virtual void Print();

  // Verify integrity of this space.
  virtual void Verify(ObjectVisitor* visitor);

  // Overridden by subclasses to verify space-specific object
  // properties (e.g., only maps or free-list nodes are in map space).
  virtual void VerifyObject(HeapObject* obj) {}

  // Report code object related statistics
  void CollectCodeStatistics();
  static void ReportCodeStatistics();
  static void ResetCodeStatistics();
#endif

  // Returns the page of the allocation pointer.
  Page* AllocationTopPage() { return TopPageOf(allocation_info_); }

 protected:
  // Maximum capacity of this space.
  int max_capacity_;

  // Accounting information for this space.
  AllocationStats accounting_stats_;

  // The first page in this space.
  Page* first_page_;

  // The last page in this space.  Initially set in Setup, updated in
  // Expand and Shrink.
  Page* last_page_;

  // True if pages owned by this space are linked in chunk-order.
  // See comment for class MemoryAllocator for definition of chunk-order.
  bool page_list_is_chunk_ordered_;

  // Normal allocation information.
  AllocationInfo allocation_info_;

  // Relocation information during mark-compact collections.
  AllocationInfo mc_forwarding_info_;

  // Bytes of each page that cannot be allocated.  Possibly non-zero
  // for pages in spaces with only fixed-size objects.  Always zero
  // for pages in spaces with variable sized objects (those pages are
  // padded with free-list nodes).
  int page_extra_;

  // Sets allocation pointer to a page bottom.
  static void SetAllocationInfo(AllocationInfo* alloc_info, Page* p);

  // Returns the top page specified by an allocation info structure.
  static Page* TopPageOf(AllocationInfo alloc_info) {
    return Page::FromAllocationTop(alloc_info.limit);
  }

  int CountPagesToTop() {
    Page* p = Page::FromAllocationTop(allocation_info_.top);
    PageIterator it(this, PageIterator::ALL_PAGES);
    int counter = 1;
    while (it.has_next()) {
      if (it.next() == p) return counter;
      counter++;
    }
    UNREACHABLE();
    return -1;
  }

  // Expands the space by allocating a fixed number of pages. Returns false if
  // it cannot allocate requested number of pages from OS. Newly allocated
  // pages are append to the last_page;
  bool Expand(Page* last_page);

  // Generic fast case allocation function that tries linear allocation in
  // the top page of 'alloc_info'.  Returns NULL on failure.
  inline HeapObject* AllocateLinearly(AllocationInfo* alloc_info,
                                      int size_in_bytes);

  // During normal allocation or deserialization, roll to the next page in
  // the space (there is assumed to be one) and allocate there.  This
  // function is space-dependent.
  virtual HeapObject* AllocateInNextPage(Page* current_page,
                                         int size_in_bytes) = 0;

  // Slow path of AllocateRaw.  This function is space-dependent.
  virtual HeapObject* SlowAllocateRaw(int size_in_bytes) = 0;

  // Slow path of MCAllocateRaw.
  HeapObject* SlowMCAllocateRaw(int size_in_bytes);

#ifdef DEBUG
  // Returns the number of total pages in this space.
  int CountTotalPages();
#endif
 private:

  // Returns a pointer to the page of the relocation pointer.
  Page* MCRelocationTopPage() { return TopPageOf(mc_forwarding_info_); }

  friend class PageIterator;
};


#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
class NumberAndSizeInfo BASE_EMBEDDED {
 public:
  NumberAndSizeInfo() : number_(0), bytes_(0) {}

  int number() const { return number_; }
  void increment_number(int num) { number_ += num; }

  int bytes() const { return bytes_; }
  void increment_bytes(int size) { bytes_ += size; }

  void clear() {
    number_ = 0;
    bytes_ = 0;
  }

 private:
  int number_;
  int bytes_;
};


// HistogramInfo class for recording a single "bar" of a histogram.  This
// class is used for collecting statistics to print to stdout (when compiled
// with DEBUG) or to the log file (when compiled with
// ENABLE_LOGGING_AND_PROFILING).
class HistogramInfo: public NumberAndSizeInfo {
 public:
  HistogramInfo() : NumberAndSizeInfo() {}

  const char* name() { return name_; }
  void set_name(const char* name) { name_ = name; }

 private:
  const char* name_;
};
#endif


// -----------------------------------------------------------------------------
// SemiSpace in young generation
//
// A semispace is a contiguous chunk of memory. The mark-compact collector
// uses the memory in the from space as a marking stack when tracing live
// objects.

class SemiSpace : public Space {
 public:
  // Constructor.
  SemiSpace() :Space(NEW_SPACE, NOT_EXECUTABLE) {
    start_ = NULL;
    age_mark_ = NULL;
  }

  // Sets up the semispace using the given chunk.
  bool Setup(Address start, int initial_capacity, int maximum_capacity);

  // Tear down the space.  Heap memory was not allocated by the space, so it
  // is not deallocated here.
  void TearDown();

  // True if the space has been set up but not torn down.
  bool HasBeenSetup() { return start_ != NULL; }

  // Grow the size of the semispace by committing extra virtual memory.
  // Assumes that the caller has checked that the semispace has not reached
  // its maximum capacity (and thus there is space available in the reserved
  // address range to grow).
  bool Grow();

  // Grow the semispace to the new capacity.  The new capacity
  // requested must be larger than the current capacity.
  bool GrowTo(int new_capacity);

  // Shrinks the semispace to the new capacity.  The new capacity
  // requested must be more than the amount of used memory in the
  // semispace and less than the current capacity.
  bool ShrinkTo(int new_capacity);

  // Returns the start address of the space.
  Address low() { return start_; }
  // Returns one past the end address of the space.
  Address high() { return low() + capacity_; }

  // Age mark accessors.
  Address age_mark() { return age_mark_; }
  void set_age_mark(Address mark) { age_mark_ = mark; }

  // True if the address is in the address range of this semispace (not
  // necessarily below the allocation pointer).
  bool Contains(Address a) {
    return (reinterpret_cast<uintptr_t>(a) & address_mask_)
           == reinterpret_cast<uintptr_t>(start_);
  }

  // True if the object is a heap object in the address range of this
  // semispace (not necessarily below the allocation pointer).
  bool Contains(Object* o) {
    return (reinterpret_cast<uintptr_t>(o) & object_mask_) == object_expected_;
  }

  // The offset of an address from the beginning of the space.
  int SpaceOffsetForAddress(Address addr) {
    return static_cast<int>(addr - low());
  }

  // If we don't have these here then SemiSpace will be abstract.  However
  // they should never be called.
  virtual int Size() {
    UNREACHABLE();
    return 0;
  }

  virtual bool ReserveSpace(int bytes) {
    UNREACHABLE();
    return false;
  }

  bool is_committed() { return committed_; }
  bool Commit();
  bool Uncommit();

#ifdef ENABLE_HEAP_PROTECTION
  // Protect/unprotect the space by marking it read-only/writable.
  virtual void Protect() {}
  virtual void Unprotect() {}
#endif

#ifdef DEBUG
  virtual void Print();
  virtual void Verify();
#endif

  // Returns the current capacity of the semi space.
  int Capacity() { return capacity_; }

  // Returns the maximum capacity of the semi space.
  int MaximumCapacity() { return maximum_capacity_; }

  // Returns the initial capacity of the semi space.
  int InitialCapacity() { return initial_capacity_; }

 private:
  // The current and maximum capacity of the space.
  int capacity_;
  int maximum_capacity_;
  int initial_capacity_;

  // The start address of the space.
  Address start_;
  // Used to govern object promotion during mark-compact collection.
  Address age_mark_;

  // Masks and comparison values to test for containment in this semispace.
  uintptr_t address_mask_;
  uintptr_t object_mask_;
  uintptr_t object_expected_;

  bool committed_;

 public:
  TRACK_MEMORY("SemiSpace")
};


// A SemiSpaceIterator is an ObjectIterator that iterates over the active
// semispace of the heap's new space.  It iterates over the objects in the
// semispace from a given start address (defaulting to the bottom of the
// semispace) to the top of the semispace.  New objects allocated after the
// iterator is created are not iterated.
class SemiSpaceIterator : public ObjectIterator {
 public:
  // Create an iterator over the objects in the given space.  If no start
  // address is given, the iterator starts from the bottom of the space.  If
  // no size function is given, the iterator calls Object::Size().
  explicit SemiSpaceIterator(NewSpace* space);
  SemiSpaceIterator(NewSpace* space, HeapObjectCallback size_func);
  SemiSpaceIterator(NewSpace* space, Address start);

  HeapObject* next() {
    if (current_ == limit_) return NULL;

    HeapObject* object = HeapObject::FromAddress(current_);
    int size = (size_func_ == NULL) ? object->Size() : size_func_(object);

    current_ += size;
    return object;
  }

  // Implementation of the ObjectIterator functions.
  virtual HeapObject* next_object() { return next(); }

 private:
  void Initialize(NewSpace* space, Address start, Address end,
                  HeapObjectCallback size_func);

  // The semispace.
  SemiSpace* space_;
  // The current iteration point.
  Address current_;
  // The end of iteration.
  Address limit_;
  // The callback function.
  HeapObjectCallback size_func_;
};


// -----------------------------------------------------------------------------
// The young generation space.
//
// The new space consists of a contiguous pair of semispaces.  It simply
// forwards most functions to the appropriate semispace.

class NewSpace : public Space {
 public:
  // Constructor.
  NewSpace() : Space(NEW_SPACE, NOT_EXECUTABLE) {}

  // Sets up the new space using the given chunk.
  bool Setup(Address start, int size);

  // Tears down the space.  Heap memory was not allocated by the space, so it
  // is not deallocated here.
  void TearDown();

  // True if the space has been set up but not torn down.
  bool HasBeenSetup() {
    return to_space_.HasBeenSetup() && from_space_.HasBeenSetup();
  }

  // Flip the pair of spaces.
  void Flip();

  // Grow the capacity of the semispaces.  Assumes that they are not at
  // their maximum capacity.
  void Grow();

  // Shrink the capacity of the semispaces.
  void Shrink();

  // True if the address or object lies in the address range of either
  // semispace (not necessarily below the allocation pointer).
  bool Contains(Address a) {
    return (reinterpret_cast<uintptr_t>(a) & address_mask_)
        == reinterpret_cast<uintptr_t>(start_);
  }
  bool Contains(Object* o) {
    return (reinterpret_cast<uintptr_t>(o) & object_mask_) == object_expected_;
  }

  // Return the allocated bytes in the active semispace.
  virtual int Size() { return static_cast<int>(top() - bottom()); }

  // Return the current capacity of a semispace.
  int Capacity() {
    ASSERT(to_space_.Capacity() == from_space_.Capacity());
    return to_space_.Capacity();
  }

  // Return the total amount of memory committed for new space.
  int CommittedMemory() {
    if (from_space_.is_committed()) return 2 * Capacity();
    return Capacity();
  }

  // Return the available bytes without growing in the active semispace.
  int Available() { return Capacity() - Size(); }

  // Return the maximum capacity of a semispace.
  int MaximumCapacity() {
    ASSERT(to_space_.MaximumCapacity() == from_space_.MaximumCapacity());
    return to_space_.MaximumCapacity();
  }

  // Returns the initial capacity of a semispace.
  int InitialCapacity() {
    ASSERT(to_space_.InitialCapacity() == from_space_.InitialCapacity());
    return to_space_.InitialCapacity();
  }

  // Return the address of the allocation pointer in the active semispace.
  Address top() { return allocation_info_.top; }
  // Return the address of the first object in the active semispace.
  Address bottom() { return to_space_.low(); }

  // Get the age mark of the inactive semispace.
  Address age_mark() { return from_space_.age_mark(); }
  // Set the age mark in the active semispace.
  void set_age_mark(Address mark) { to_space_.set_age_mark(mark); }

  // The start address of the space and a bit mask. Anding an address in the
  // new space with the mask will result in the start address.
  Address start() { return start_; }
  uintptr_t mask() { return address_mask_; }

  // The allocation top and limit addresses.
  Address* allocation_top_address() { return &allocation_info_.top; }
  Address* allocation_limit_address() { return &allocation_info_.limit; }

  Object* AllocateRaw(int size_in_bytes) {
    return AllocateRawInternal(size_in_bytes, &allocation_info_);
  }

  // Allocate the requested number of bytes for relocation during mark-compact
  // collection.
  Object* MCAllocateRaw(int size_in_bytes) {
    return AllocateRawInternal(size_in_bytes, &mc_forwarding_info_);
  }

  // Reset the allocation pointer to the beginning of the active semispace.
  void ResetAllocationInfo();
  // Reset the reloction pointer to the bottom of the inactive semispace in
  // preparation for mark-compact collection.
  void MCResetRelocationInfo();
  // Update the allocation pointer in the active semispace after a
  // mark-compact collection.
  void MCCommitRelocationInfo();

  // Get the extent of the inactive semispace (for use as a marking stack).
  Address FromSpaceLow() { return from_space_.low(); }
  Address FromSpaceHigh() { return from_space_.high(); }

  // Get the extent of the active semispace (to sweep newly copied objects
  // during a scavenge collection).
  Address ToSpaceLow() { return to_space_.low(); }
  Address ToSpaceHigh() { return to_space_.high(); }

  // Offsets from the beginning of the semispaces.
  int ToSpaceOffsetForAddress(Address a) {
    return to_space_.SpaceOffsetForAddress(a);
  }
  int FromSpaceOffsetForAddress(Address a) {
    return from_space_.SpaceOffsetForAddress(a);
  }

  // True if the object is a heap object in the address range of the
  // respective semispace (not necessarily below the allocation pointer of the
  // semispace).
  bool ToSpaceContains(Object* o) { return to_space_.Contains(o); }
  bool FromSpaceContains(Object* o) { return from_space_.Contains(o); }

  bool ToSpaceContains(Address a) { return to_space_.Contains(a); }
  bool FromSpaceContains(Address a) { return from_space_.Contains(a); }

  virtual bool ReserveSpace(int bytes);

#ifdef ENABLE_HEAP_PROTECTION
  // Protect/unprotect the space by marking it read-only/writable.
  virtual void Protect();
  virtual void Unprotect();
#endif

#ifdef DEBUG
  // Verify the active semispace.
  virtual void Verify();
  // Print the active semispace.
  virtual void Print() { to_space_.Print(); }
#endif

#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
  // Iterates the active semispace to collect statistics.
  void CollectStatistics();
  // Reports previously collected statistics of the active semispace.
  void ReportStatistics();
  // Clears previously collected statistics.
  void ClearHistograms();

  // Record the allocation or promotion of a heap object.  Note that we don't
  // record every single allocation, but only those that happen in the
  // to space during a scavenge GC.
  void RecordAllocation(HeapObject* obj);
  void RecordPromotion(HeapObject* obj);
#endif

  // Return whether the operation succeded.
  bool CommitFromSpaceIfNeeded() {
    if (from_space_.is_committed()) return true;
    return from_space_.Commit();
  }

  bool UncommitFromSpace() {
    if (!from_space_.is_committed()) return true;
    return from_space_.Uncommit();
  }

 private:
  // The semispaces.
  SemiSpace to_space_;
  SemiSpace from_space_;

  // Start address and bit mask for containment testing.
  Address start_;
  uintptr_t address_mask_;
  uintptr_t object_mask_;
  uintptr_t object_expected_;

  // Allocation pointer and limit for normal allocation and allocation during
  // mark-compact collection.
  AllocationInfo allocation_info_;
  AllocationInfo mc_forwarding_info_;

#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
  HistogramInfo* allocated_histogram_;
  HistogramInfo* promoted_histogram_;
#endif

  // Implementation of AllocateRaw and MCAllocateRaw.
  inline Object* AllocateRawInternal(int size_in_bytes,
                                     AllocationInfo* alloc_info);

  friend class SemiSpaceIterator;

 public:
  TRACK_MEMORY("NewSpace")
};


// -----------------------------------------------------------------------------
// Free lists for old object spaces
//
// Free-list nodes are free blocks in the heap.  They look like heap objects
// (free-list node pointers have the heap object tag, and they have a map like
// a heap object).  They have a size and a next pointer.  The next pointer is
// the raw address of the next free list node (or NULL).
class FreeListNode: public HeapObject {
 public:
  // Obtain a free-list node from a raw address.  This is not a cast because
  // it does not check nor require that the first word at the address is a map
  // pointer.
  static FreeListNode* FromAddress(Address address) {
    return reinterpret_cast<FreeListNode*>(HeapObject::FromAddress(address));
  }

  static inline bool IsFreeListNode(HeapObject* object);

  // Set the size in bytes, which can be read with HeapObject::Size().  This
  // function also writes a map to the first word of the block so that it
  // looks like a heap object to the garbage collector and heap iteration
  // functions.
  void set_size(int size_in_bytes);

  // Accessors for the next field.
  inline Address next();
  inline void set_next(Address next);

 private:
  static const int kNextOffset = POINTER_SIZE_ALIGN(ByteArray::kHeaderSize);

  DISALLOW_IMPLICIT_CONSTRUCTORS(FreeListNode);
};


// The free list for the old space.
class OldSpaceFreeList BASE_EMBEDDED {
 public:
  explicit OldSpaceFreeList(AllocationSpace owner);

  // Clear the free list.
  void Reset();

  // Return the number of bytes available on the free list.
  int available() { return available_; }

  // Place a node on the free list.  The block of size 'size_in_bytes'
  // starting at 'start' is placed on the free list.  The return value is the
  // number of bytes that have been lost due to internal fragmentation by
  // freeing the block.  Bookkeeping information will be written to the block,
  // ie, its contents will be destroyed.  The start address should be word
  // aligned, and the size should be a non-zero multiple of the word size.
  int Free(Address start, int size_in_bytes);

  // Allocate a block of size 'size_in_bytes' from the free list.  The block
  // is unitialized.  A failure is returned if no block is available.  The
  // number of bytes lost to fragmentation is returned in the output parameter
  // 'wasted_bytes'.  The size should be a non-zero multiple of the word size.
  Object* Allocate(int size_in_bytes, int* wasted_bytes);

 private:
  // The size range of blocks, in bytes. (Smaller allocations are allowed, but
  // will always result in waste.)
  static const int kMinBlockSize = 2 * kPointerSize;
  static const int kMaxBlockSize = Page::kMaxHeapObjectSize;

  // The identity of the owning space, for building allocation Failure
  // objects.
  AllocationSpace owner_;

  // Total available bytes in all blocks on this free list.
  int available_;

  // Blocks are put on exact free lists in an array, indexed by size in words.
  // The available sizes are kept in an increasingly ordered list. Entries
  // corresponding to sizes < kMinBlockSize always have an empty free list
  // (but index kHead is used for the head of the size list).
  struct SizeNode {
    // Address of the head FreeListNode of the implied block size or NULL.
    Address head_node_;
    // Size (words) of the next larger available size if head_node_ != NULL.
    int next_size_;
  };
  static const int kFreeListsLength = kMaxBlockSize / kPointerSize + 1;
  SizeNode free_[kFreeListsLength];

  // Sentinel elements for the size list. Real elements are in ]kHead..kEnd[.
  static const int kHead = kMinBlockSize / kPointerSize - 1;
  static const int kEnd = kMaxInt;

  // We keep a "finger" in the size list to speed up a common pattern:
  // repeated requests for the same or increasing sizes.
  int finger_;

  // Starting from *prev, find and return the smallest size >= index (words),
  // or kEnd. Update *prev to be the largest size < index, or kHead.
  int FindSize(int index, int* prev) {
    int cur = free_[*prev].next_size_;
    while (cur < index) {
      *prev = cur;
      cur = free_[cur].next_size_;
    }
    return cur;
  }

  // Remove an existing element from the size list.
  void RemoveSize(int index) {
    int prev = kHead;
    int cur = FindSize(index, &prev);
    ASSERT(cur == index);
    free_[prev].next_size_ = free_[cur].next_size_;
    finger_ = prev;
  }

  // Insert a new element into the size list.
  void InsertSize(int index) {
    int prev = kHead;
    int cur = FindSize(index, &prev);
    ASSERT(cur != index);
    free_[prev].next_size_ = index;
    free_[index].next_size_ = cur;
  }

  // The size list is not updated during a sequence of calls to Free, but is
  // rebuilt before the next allocation.
  void RebuildSizeList();
  bool needs_rebuild_;

#ifdef DEBUG
  // Does this free list contain a free block located at the address of 'node'?
  bool Contains(FreeListNode* node);
#endif

  DISALLOW_COPY_AND_ASSIGN(OldSpaceFreeList);
};


// The free list for the map space.
class FixedSizeFreeList BASE_EMBEDDED {
 public:
  FixedSizeFreeList(AllocationSpace owner, int object_size);

  // Clear the free list.
  void Reset();

  // Return the number of bytes available on the free list.
  int available() { return available_; }

  // Place a node on the free list.  The block starting at 'start' (assumed to
  // have size object_size_) is placed on the free list.  Bookkeeping
  // information will be written to the block, ie, its contents will be
  // destroyed.  The start address should be word aligned.
  void Free(Address start);

  // Allocate a fixed sized block from the free list.  The block is unitialized.
  // A failure is returned if no block is available.
  Object* Allocate();

 private:
  // Available bytes on the free list.
  int available_;

  // The head of the free list.
  Address head_;

  // The tail of the free list.
  Address tail_;

  // The identity of the owning space, for building allocation Failure
  // objects.
  AllocationSpace owner_;

  // The size of the objects in this space.
  int object_size_;

  DISALLOW_COPY_AND_ASSIGN(FixedSizeFreeList);
};


// -----------------------------------------------------------------------------
// Old object space (excluding map objects)

class OldSpace : public PagedSpace {
 public:
  // Creates an old space object with a given maximum capacity.
  // The constructor does not allocate pages from OS.
  explicit OldSpace(int max_capacity,
                    AllocationSpace id,
                    Executability executable)
      : PagedSpace(max_capacity, id, executable), free_list_(id) {
    page_extra_ = 0;
  }

  // The bytes available on the free list (ie, not above the linear allocation
  // pointer).
  int AvailableFree() { return free_list_.available(); }

  // The limit of allocation for a page in this space.
  virtual Address PageAllocationLimit(Page* page) {
    return page->ObjectAreaEnd();
  }

  // Give a block of memory to the space's free list.  It might be added to
  // the free list or accounted as waste.
  // If add_to_freelist is false then just accounting stats are updated and
  // no attempt to add area to free list is made.
  void Free(Address start, int size_in_bytes, bool add_to_freelist) {
    accounting_stats_.DeallocateBytes(size_in_bytes);

    if (add_to_freelist) {
      int wasted_bytes = free_list_.Free(start, size_in_bytes);
      accounting_stats_.WasteBytes(wasted_bytes);
    }
  }

  // Prepare for full garbage collection.  Resets the relocation pointer and
  // clears the free list.
  virtual void PrepareForMarkCompact(bool will_compact);

  // Updates the allocation pointer to the relocation top after a mark-compact
  // collection.
  virtual void MCCommitRelocationInfo();

  virtual void PutRestOfCurrentPageOnFreeList(Page* current_page);

#ifdef DEBUG
  // Reports statistics for the space
  void ReportStatistics();
#endif

 protected:
  // Virtual function in the superclass.  Slow path of AllocateRaw.
  HeapObject* SlowAllocateRaw(int size_in_bytes);

  // Virtual function in the superclass.  Allocate linearly at the start of
  // the page after current_page (there is assumed to be one).
  HeapObject* AllocateInNextPage(Page* current_page, int size_in_bytes);

 private:
  // The space's free list.
  OldSpaceFreeList free_list_;

 public:
  TRACK_MEMORY("OldSpace")
};


// -----------------------------------------------------------------------------
// Old space for objects of a fixed size

class FixedSpace : public PagedSpace {
 public:
  FixedSpace(int max_capacity,
             AllocationSpace id,
             int object_size_in_bytes,
             const char* name)
      : PagedSpace(max_capacity, id, NOT_EXECUTABLE),
        object_size_in_bytes_(object_size_in_bytes),
        name_(name),
        free_list_(id, object_size_in_bytes) {
    page_extra_ = Page::kObjectAreaSize % object_size_in_bytes;
  }

  // The limit of allocation for a page in this space.
  virtual Address PageAllocationLimit(Page* page) {
    return page->ObjectAreaEnd() - page_extra_;
  }

  int object_size_in_bytes() { return object_size_in_bytes_; }

  // Give a fixed sized block of memory to the space's free list.
  // If add_to_freelist is false then just accounting stats are updated and
  // no attempt to add area to free list is made.
  void Free(Address start, bool add_to_freelist) {
    if (add_to_freelist) {
      free_list_.Free(start);
    }
    accounting_stats_.DeallocateBytes(object_size_in_bytes_);
  }

  // Prepares for a mark-compact GC.
  virtual void PrepareForMarkCompact(bool will_compact);

  // Updates the allocation pointer to the relocation top after a mark-compact
  // collection.
  virtual void MCCommitRelocationInfo();

  virtual void PutRestOfCurrentPageOnFreeList(Page* current_page);

#ifdef DEBUG
  // Reports statistic info of the space
  void ReportStatistics();
#endif

 protected:
  // Virtual function in the superclass.  Slow path of AllocateRaw.
  HeapObject* SlowAllocateRaw(int size_in_bytes);

  // Virtual function in the superclass.  Allocate linearly at the start of
  // the page after current_page (there is assumed to be one).
  HeapObject* AllocateInNextPage(Page* current_page, int size_in_bytes);

  void ResetFreeList() {
    free_list_.Reset();
  }

 private:
  // The size of objects in this space.
  int object_size_in_bytes_;

  // The name of this space.
  const char* name_;

  // The space's free list.
  FixedSizeFreeList free_list_;
};


// -----------------------------------------------------------------------------
// Old space for all map objects

class MapSpace : public FixedSpace {
 public:
  // Creates a map space object with a maximum capacity.
  MapSpace(int max_capacity, int max_map_space_pages, AllocationSpace id)
      : FixedSpace(max_capacity, id, Map::kSize, "map"),
        max_map_space_pages_(max_map_space_pages) {
    ASSERT(max_map_space_pages < kMaxMapPageIndex);
  }

  // Prepares for a mark-compact GC.
  virtual void PrepareForMarkCompact(bool will_compact);

  // Given an index, returns the page address.
  Address PageAddress(int page_index) { return page_addresses_[page_index]; }

  static const int kMaxMapPageIndex = 1 << MapWord::kMapPageIndexBits;

  // Are map pointers encodable into map word?
  bool MapPointersEncodable() {
    if (!FLAG_use_big_map_space) {
      ASSERT(CountPagesToTop() <= kMaxMapPageIndex);
      return true;
    }
    return CountPagesToTop() <= max_map_space_pages_;
  }

  // Should be called after forced sweep to find out if map space needs
  // compaction.
  bool NeedsCompaction(int live_maps) {
    return !MapPointersEncodable() && live_maps <= CompactionThreshold();
  }

  Address TopAfterCompaction(int live_maps) {
    ASSERT(NeedsCompaction(live_maps));

    int pages_left = live_maps / kMapsPerPage;
    PageIterator it(this, PageIterator::ALL_PAGES);
    while (pages_left-- > 0) {
      ASSERT(it.has_next());
      it.next()->SetRegionMarks(Page::kAllRegionsCleanMarks);
    }
    ASSERT(it.has_next());
    Page* top_page = it.next();
    top_page->SetRegionMarks(Page::kAllRegionsCleanMarks);
    ASSERT(top_page->is_valid());

    int offset = live_maps % kMapsPerPage * Map::kSize;
    Address top = top_page->ObjectAreaStart() + offset;
    ASSERT(top < top_page->ObjectAreaEnd());
    ASSERT(Contains(top));

    return top;
  }

  void FinishCompaction(Address new_top, int live_maps) {
    Page* top_page = Page::FromAddress(new_top);
    ASSERT(top_page->is_valid());

    SetAllocationInfo(&allocation_info_, top_page);
    allocation_info_.top = new_top;

    int new_size = live_maps * Map::kSize;
    accounting_stats_.DeallocateBytes(accounting_stats_.Size());
    accounting_stats_.AllocateBytes(new_size);

#ifdef DEBUG
    if (FLAG_enable_slow_asserts) {
      intptr_t actual_size = 0;
      for (Page* p = first_page_; p != top_page; p = p->next_page())
        actual_size += kMapsPerPage * Map::kSize;
      actual_size += (new_top - top_page->ObjectAreaStart());
      ASSERT(accounting_stats_.Size() == actual_size);
    }
#endif

    Shrink();
    ResetFreeList();
  }

 protected:
#ifdef DEBUG
  virtual void VerifyObject(HeapObject* obj);
#endif

 private:
  static const int kMapsPerPage = Page::kObjectAreaSize / Map::kSize;

  // Do map space compaction if there is a page gap.
  int CompactionThreshold() {
    return kMapsPerPage * (max_map_space_pages_ - 1);
  }

  const int max_map_space_pages_;

  // An array of page start address in a map space.
  Address page_addresses_[kMaxMapPageIndex];

 public:
  TRACK_MEMORY("MapSpace")
};


// -----------------------------------------------------------------------------
// Old space for all global object property cell objects

class CellSpace : public FixedSpace {
 public:
  // Creates a property cell space object with a maximum capacity.
  CellSpace(int max_capacity, AllocationSpace id)
      : FixedSpace(max_capacity, id, JSGlobalPropertyCell::kSize, "cell") {}

 protected:
#ifdef DEBUG
  virtual void VerifyObject(HeapObject* obj);
#endif

 public:
  TRACK_MEMORY("CellSpace")
};


// -----------------------------------------------------------------------------
// Large objects ( > Page::kMaxHeapObjectSize ) are allocated and managed by
// the large object space. A large object is allocated from OS heap with
// extra padding bytes (Page::kPageSize + Page::kObjectStartOffset).
// A large object always starts at Page::kObjectStartOffset to a page.
// Large objects do not move during garbage collections.

// A LargeObjectChunk holds exactly one large object page with exactly one
// large object.
class LargeObjectChunk {
 public:
  // Allocates a new LargeObjectChunk that contains a large object page
  // (Page::kPageSize aligned) that has at least size_in_bytes (for a large
  // object) bytes after the object area start of that page.
  // The allocated chunk size is set in the output parameter chunk_size.
  static LargeObjectChunk* New(int size_in_bytes,
                               size_t* chunk_size,
                               Executability executable);

  // Interpret a raw address as a large object chunk.
  static LargeObjectChunk* FromAddress(Address address) {
    return reinterpret_cast<LargeObjectChunk*>(address);
  }

  // Returns the address of this chunk.
  Address address() { return reinterpret_cast<Address>(this); }

  // Accessors for the fields of the chunk.
  LargeObjectChunk* next() { return next_; }
  void set_next(LargeObjectChunk* chunk) { next_ = chunk; }

  size_t size() { return size_; }
  void set_size(size_t size_in_bytes) { size_ = size_in_bytes; }

  // Returns the object in this chunk.
  inline HeapObject* GetObject();

  // Given a requested size returns the physical size of a chunk to be
  // allocated.
  static int ChunkSizeFor(int size_in_bytes);

  // Given a chunk size, returns the object size it can accommodate.  Used by
  // LargeObjectSpace::Available.
  static int ObjectSizeFor(int chunk_size) {
    if (chunk_size <= (Page::kPageSize + Page::kObjectStartOffset)) return 0;
    return chunk_size - Page::kPageSize - Page::kObjectStartOffset;
  }

 private:
  // A pointer to the next large object chunk in the space or NULL.
  LargeObjectChunk* next_;

  // The size of this chunk.
  size_t size_;

 public:
  TRACK_MEMORY("LargeObjectChunk")
};


class LargeObjectSpace : public Space {
 public:
  explicit LargeObjectSpace(AllocationSpace id);
  virtual ~LargeObjectSpace() {}

  // Initializes internal data structures.
  bool Setup();

  // Releases internal resources, frees objects in this space.
  void TearDown();

  // Allocates a (non-FixedArray, non-Code) large object.
  Object* AllocateRaw(int size_in_bytes);
  // Allocates a large Code object.
  Object* AllocateRawCode(int size_in_bytes);
  // Allocates a large FixedArray.
  Object* AllocateRawFixedArray(int size_in_bytes);

  // Available bytes for objects in this space.
  int Available() {
    return LargeObjectChunk::ObjectSizeFor(MemoryAllocator::Available());
  }

  virtual int Size() {
    return size_;
  }

  int PageCount() {
    return page_count_;
  }

  // Finds an object for a given address, returns Failure::Exception()
  // if it is not found. The function iterates through all objects in this
  // space, may be slow.
  Object* FindObject(Address a);

  // Iterates objects covered by dirty regions.
  void IterateDirtyRegions(ObjectSlotCallback func);

  // Frees unmarked objects.
  void FreeUnmarkedObjects();

  // Checks whether a heap object is in this space; O(1).
  bool Contains(HeapObject* obj);

  // Checks whether the space is empty.
  bool IsEmpty() { return first_chunk_ == NULL; }

  // See the comments for ReserveSpace in the Space class.  This has to be
  // called after ReserveSpace has been called on the paged spaces, since they
  // may use some memory, leaving less for large objects.
  virtual bool ReserveSpace(int bytes);

#ifdef ENABLE_HEAP_PROTECTION
  // Protect/unprotect the space by marking it read-only/writable.
  void Protect();
  void Unprotect();
#endif

#ifdef DEBUG
  virtual void Verify();
  virtual void Print();
  void ReportStatistics();
  void CollectCodeStatistics();
#endif
  // Checks whether an address is in the object area in this space.  It
  // iterates all objects in the space. May be slow.
  bool SlowContains(Address addr) { return !FindObject(addr)->IsFailure(); }

 private:
  // The head of the linked list of large object chunks.
  LargeObjectChunk* first_chunk_;
  int size_;  // allocated bytes
  int page_count_;  // number of chunks


  // Shared implementation of AllocateRaw, AllocateRawCode and
  // AllocateRawFixedArray.
  Object* AllocateRawInternal(int requested_size,
                              int object_size,
                              Executability executable);

  friend class LargeObjectIterator;

 public:
  TRACK_MEMORY("LargeObjectSpace")
};


class LargeObjectIterator: public ObjectIterator {
 public:
  explicit LargeObjectIterator(LargeObjectSpace* space);
  LargeObjectIterator(LargeObjectSpace* space, HeapObjectCallback size_func);

  HeapObject* next();

  // implementation of ObjectIterator.
  virtual HeapObject* next_object() { return next(); }

 private:
  LargeObjectChunk* current_;
  HeapObjectCallback size_func_;
};


} }  // namespace v8::internal

#endif  // V8_SPACES_H_
