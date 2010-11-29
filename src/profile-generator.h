// Copyright 2010 the V8 project authors. All rights reserved.
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

#ifndef V8_PROFILE_GENERATOR_H_
#define V8_PROFILE_GENERATOR_H_

#ifdef ENABLE_LOGGING_AND_PROFILING

#include "hashmap.h"
#include "../include/v8-profiler.h"

namespace v8 {
namespace internal {

class TokenEnumerator {
 public:
  TokenEnumerator();
  ~TokenEnumerator();
  int GetTokenId(Object* token);

  static const int kNoSecurityToken = -1;
  static const int kInheritsSecurityToken = -2;

 private:
  static void TokenRemovedCallback(v8::Persistent<v8::Value> handle,
                                   void* parameter);
  void TokenRemoved(Object** token_location);

  List<Object**> token_locations_;
  List<bool> token_removed_;

  friend class TokenEnumeratorTester;

  DISALLOW_COPY_AND_ASSIGN(TokenEnumerator);
};


// Provides a storage of strings allocated in C++ heap, to hold them
// forever, even if they disappear from JS heap or external storage.
class StringsStorage {
 public:
  StringsStorage();
  ~StringsStorage();

  const char* GetName(String* name);
  const char* GetName(int index);
  inline const char* GetFunctionName(String* name);
  inline const char* GetFunctionName(const char* name);

 private:
  INLINE(static bool StringsMatch(void* key1, void* key2)) {
    return strcmp(reinterpret_cast<char*>(key1),
                  reinterpret_cast<char*>(key2)) == 0;
  }

  // Mapping of strings by String::Hash to const char* strings.
  HashMap names_;
  // Mapping from ints to char* strings.
  List<char*> index_names_;

  DISALLOW_COPY_AND_ASSIGN(StringsStorage);
};


class CodeEntry {
 public:
  explicit INLINE(CodeEntry(int security_token_id));
  // CodeEntry doesn't own name strings, just references them.
  INLINE(CodeEntry(Logger::LogEventsAndTags tag,
                   const char* name_prefix,
                   const char* name,
                   const char* resource_name,
                   int line_number,
                   int security_token_id));

  INLINE(bool is_js_function() const) { return is_js_function_tag(tag_); }
  INLINE(const char* name_prefix() const) { return name_prefix_; }
  INLINE(bool has_name_prefix() const) { return name_prefix_[0] != '\0'; }
  INLINE(const char* name() const) { return name_; }
  INLINE(const char* resource_name() const) { return resource_name_; }
  INLINE(int line_number() const) { return line_number_; }
  INLINE(int security_token_id() const) { return security_token_id_; }

  INLINE(static bool is_js_function_tag(Logger::LogEventsAndTags tag));

  void CopyData(const CodeEntry& source);
  uint32_t GetCallUid() const;
  bool IsSameAs(CodeEntry* entry) const;

  static const char* kEmptyNamePrefix;

 private:
  Logger::LogEventsAndTags tag_;
  const char* name_prefix_;
  const char* name_;
  const char* resource_name_;
  int line_number_;
  int security_token_id_;

  DISALLOW_COPY_AND_ASSIGN(CodeEntry);
};


class ProfileTree;

class ProfileNode {
 public:
  INLINE(ProfileNode(ProfileTree* tree, CodeEntry* entry));

  ProfileNode* FindChild(CodeEntry* entry);
  ProfileNode* FindOrAddChild(CodeEntry* entry);
  INLINE(void IncrementSelfTicks()) { ++self_ticks_; }
  INLINE(void IncreaseSelfTicks(unsigned amount)) { self_ticks_ += amount; }
  INLINE(void IncreaseTotalTicks(unsigned amount)) { total_ticks_ += amount; }

  INLINE(CodeEntry* entry() const) { return entry_; }
  INLINE(unsigned self_ticks() const) { return self_ticks_; }
  INLINE(unsigned total_ticks() const) { return total_ticks_; }
  INLINE(const List<ProfileNode*>* children() const) { return &children_list_; }
  double GetSelfMillis() const;
  double GetTotalMillis() const;

  void Print(int indent);

 private:
  INLINE(static bool CodeEntriesMatch(void* entry1, void* entry2)) {
    return reinterpret_cast<CodeEntry*>(entry1)->IsSameAs(
        reinterpret_cast<CodeEntry*>(entry2));
  }

  INLINE(static uint32_t CodeEntryHash(CodeEntry* entry)) {
    return entry->GetCallUid();
  }

  ProfileTree* tree_;
  CodeEntry* entry_;
  unsigned total_ticks_;
  unsigned self_ticks_;
  // Mapping from CodeEntry* to ProfileNode*
  HashMap children_;
  List<ProfileNode*> children_list_;

  DISALLOW_COPY_AND_ASSIGN(ProfileNode);
};


class ProfileTree {
 public:
  ProfileTree();
  ~ProfileTree();

  void AddPathFromEnd(const Vector<CodeEntry*>& path);
  void AddPathFromStart(const Vector<CodeEntry*>& path);
  void CalculateTotalTicks();
  void FilteredClone(ProfileTree* src, int security_token_id);

  double TicksToMillis(unsigned ticks) const {
    return ticks * ms_to_ticks_scale_;
  }
  ProfileNode* root() const { return root_; }
  void SetTickRatePerMs(double ticks_per_ms);

  void ShortPrint();
  void Print() {
    root_->Print(0);
  }

 private:
  template <typename Callback>
  void TraverseDepthFirst(Callback* callback);

  CodeEntry root_entry_;
  ProfileNode* root_;
  double ms_to_ticks_scale_;

  DISALLOW_COPY_AND_ASSIGN(ProfileTree);
};


class CpuProfile {
 public:
  CpuProfile(const char* title, unsigned uid)
      : title_(title), uid_(uid) { }

  // Add pc -> ... -> main() call path to the profile.
  void AddPath(const Vector<CodeEntry*>& path);
  void CalculateTotalTicks();
  void SetActualSamplingRate(double actual_sampling_rate);
  CpuProfile* FilteredClone(int security_token_id);

  INLINE(const char* title() const) { return title_; }
  INLINE(unsigned uid() const) { return uid_; }
  INLINE(const ProfileTree* top_down() const) { return &top_down_; }
  INLINE(const ProfileTree* bottom_up() const) { return &bottom_up_; }

  void UpdateTicksScale();

  void ShortPrint();
  void Print();

 private:
  const char* title_;
  unsigned uid_;
  ProfileTree top_down_;
  ProfileTree bottom_up_;

  DISALLOW_COPY_AND_ASSIGN(CpuProfile);
};


class CodeMap {
 public:
  CodeMap() { }
  INLINE(void AddCode(Address addr, CodeEntry* entry, unsigned size));
  INLINE(void MoveCode(Address from, Address to));
  INLINE(void DeleteCode(Address addr));
  void AddAlias(Address start, CodeEntry* entry, Address code_start);
  CodeEntry* FindEntry(Address addr);

  void Print();

 private:
  struct CodeEntryInfo {
    CodeEntryInfo(CodeEntry* an_entry, unsigned a_size)
        : entry(an_entry), size(a_size) { }
    CodeEntry* entry;
    unsigned size;
  };

  struct CodeTreeConfig {
    typedef Address Key;
    typedef CodeEntryInfo Value;
    static const Key kNoKey;
    static const Value kNoValue;
    static int Compare(const Key& a, const Key& b) {
      return a < b ? -1 : (a > b ? 1 : 0);
    }
  };
  typedef SplayTree<CodeTreeConfig> CodeTree;

  class CodeTreePrinter {
   public:
    void Call(const Address& key, const CodeEntryInfo& value);
  };

  CodeTree tree_;

  DISALLOW_COPY_AND_ASSIGN(CodeMap);
};


class CpuProfilesCollection {
 public:
  CpuProfilesCollection();
  ~CpuProfilesCollection();

  bool StartProfiling(const char* title, unsigned uid);
  bool StartProfiling(String* title, unsigned uid);
  CpuProfile* StopProfiling(int security_token_id,
                            const char* title,
                            double actual_sampling_rate);
  List<CpuProfile*>* Profiles(int security_token_id);
  const char* GetName(String* name) {
    return function_and_resource_names_.GetName(name);
  }
  const char* GetName(int args_count) {
    return function_and_resource_names_.GetName(args_count);
  }
  CpuProfile* GetProfile(int security_token_id, unsigned uid);
  bool IsLastProfile(const char* title);

  CodeEntry* NewCodeEntry(Logger::LogEventsAndTags tag,
                          String* name, String* resource_name, int line_number);
  CodeEntry* NewCodeEntry(Logger::LogEventsAndTags tag, const char* name);
  CodeEntry* NewCodeEntry(Logger::LogEventsAndTags tag,
                          const char* name_prefix, String* name);
  CodeEntry* NewCodeEntry(Logger::LogEventsAndTags tag, int args_count);
  CodeEntry* NewCodeEntry(int security_token_id);

  // Called from profile generator thread.
  void AddPathToCurrentProfiles(const Vector<CodeEntry*>& path);

  // Limits the number of profiles that can be simultaneously collected.
  static const int kMaxSimultaneousProfiles = 100;

 private:
  const char* GetFunctionName(String* name) {
    return function_and_resource_names_.GetFunctionName(name);
  }
  const char* GetFunctionName(const char* name) {
    return function_and_resource_names_.GetFunctionName(name);
  }
  List<CpuProfile*>* GetProfilesList(int security_token_id);
  int TokenToIndex(int security_token_id);

  INLINE(static bool UidsMatch(void* key1, void* key2)) {
    return key1 == key2;
  }

  StringsStorage function_and_resource_names_;
  List<CodeEntry*> code_entries_;
  List<List<CpuProfile*>* > profiles_by_token_;
  // Mapping from profiles' uids to indexes in the second nested list
  // of profiles_by_token_.
  HashMap profiles_uids_;

  // Accessed by VM thread and profile generator thread.
  List<CpuProfile*> current_profiles_;
  Semaphore* current_profiles_semaphore_;

  DISALLOW_COPY_AND_ASSIGN(CpuProfilesCollection);
};


class SampleRateCalculator {
 public:
  SampleRateCalculator()
      : result_(Logger::kSamplingIntervalMs * kResultScale),
        ticks_per_ms_(Logger::kSamplingIntervalMs),
        measurements_count_(0),
        wall_time_query_countdown_(1) {
  }

  double ticks_per_ms() {
    return result_ / static_cast<double>(kResultScale);
  }
  void Tick();
  void UpdateMeasurements(double current_time);

  // Instead of querying current wall time each tick,
  // we use this constant to control query intervals.
  static const unsigned kWallTimeQueryIntervalMs = 100;

 private:
  // As the result needs to be accessed from a different thread, we
  // use type that guarantees atomic writes to memory.  There should
  // be <= 1000 ticks per second, thus storing a value of a 10 ** 5
  // order should provide enough precision while keeping away from a
  // potential overflow.
  static const int kResultScale = 100000;

  AtomicWord result_;
  // All other fields are accessed only from the sampler thread.
  double ticks_per_ms_;
  unsigned measurements_count_;
  unsigned wall_time_query_countdown_;
  double last_wall_time_;

  DISALLOW_COPY_AND_ASSIGN(SampleRateCalculator);
};


class ProfileGenerator {
 public:
  explicit ProfileGenerator(CpuProfilesCollection* profiles);

  INLINE(CodeEntry* NewCodeEntry(Logger::LogEventsAndTags tag,
                                 String* name,
                                 String* resource_name,
                                 int line_number)) {
    return profiles_->NewCodeEntry(tag, name, resource_name, line_number);
  }

  INLINE(CodeEntry* NewCodeEntry(Logger::LogEventsAndTags tag,
                                 const char* name)) {
    return profiles_->NewCodeEntry(tag, name);
  }

  INLINE(CodeEntry* NewCodeEntry(Logger::LogEventsAndTags tag,
                                 const char* name_prefix,
                                 String* name)) {
    return profiles_->NewCodeEntry(tag, name_prefix, name);
  }

  INLINE(CodeEntry* NewCodeEntry(Logger::LogEventsAndTags tag,
                                 int args_count)) {
    return profiles_->NewCodeEntry(tag, args_count);
  }

  INLINE(CodeEntry* NewCodeEntry(int security_token_id)) {
    return profiles_->NewCodeEntry(security_token_id);
  }

  void RecordTickSample(const TickSample& sample);

  INLINE(CodeMap* code_map()) { return &code_map_; }

  INLINE(void Tick()) { sample_rate_calc_.Tick(); }
  INLINE(double actual_sampling_rate()) {
    return sample_rate_calc_.ticks_per_ms();
  }

  static const char* kAnonymousFunctionName;
  static const char* kProgramEntryName;
  static const char* kGarbageCollectorEntryName;

 private:
  INLINE(CodeEntry* EntryForVMState(StateTag tag));

  CpuProfilesCollection* profiles_;
  CodeMap code_map_;
  CodeEntry* program_entry_;
  CodeEntry* gc_entry_;
  SampleRateCalculator sample_rate_calc_;

  DISALLOW_COPY_AND_ASSIGN(ProfileGenerator);
};


class HeapEntry;

class HeapGraphEdge BASE_EMBEDDED {
 public:
  enum Type {
    kContextVariable = v8::HeapGraphEdge::kContextVariable,
    kElement = v8::HeapGraphEdge::kElement,
    kProperty = v8::HeapGraphEdge::kProperty,
    kInternal = v8::HeapGraphEdge::kInternal,
    kHidden = v8::HeapGraphEdge::kHidden,
    kShortcut = v8::HeapGraphEdge::kShortcut
  };

  HeapGraphEdge() { }
  void Init(int child_index, Type type, const char* name, HeapEntry* to);
  void Init(int child_index, Type type, int index, HeapEntry* to);
  void Init(int child_index, int index, HeapEntry* to);

  Type type() { return static_cast<Type>(type_); }
  int index() {
    ASSERT(type_ == kElement || type_ == kHidden);
    return index_;
  }
  const char* name() {
    ASSERT(type_ == kContextVariable
           || type_ == kProperty
           || type_ == kInternal
           || type_ == kShortcut);
    return name_;
  }
  HeapEntry* to() { return to_; }

  HeapEntry* From();

 private:
  int child_index_ : 29;
  unsigned type_ : 3;
  union {
    int index_;
    const char* name_;
  };
  HeapEntry* to_;

  DISALLOW_COPY_AND_ASSIGN(HeapGraphEdge);
};


class CachedHeapGraphPath;
class HeapGraphPath;
class HeapSnapshot;

// HeapEntry instances represent an entity from the heap (or a special
// virtual node, e.g. root). To make heap snapshots more compact,
// HeapEntries has a special memory layout (no Vectors or Lists used):
//
//   +-----------------+
//        HeapEntry
//   +-----------------+
//      HeapGraphEdge    |
//           ...         } children_count
//      HeapGraphEdge    |
//   +-----------------+
//      HeapGraphEdge*   |
//           ...         } retainers_count
//      HeapGraphEdge*   |
//   +-----------------+
//
// In a HeapSnapshot, all entries are hand-allocated in a continuous array
// of raw bytes.
//
class HeapEntry BASE_EMBEDDED {
 public:
  enum Type {
    kHidden = v8::HeapGraphNode::kHidden,
    kArray = v8::HeapGraphNode::kArray,
    kString = v8::HeapGraphNode::kString,
    kObject = v8::HeapGraphNode::kObject,
    kCode = v8::HeapGraphNode::kCode,
    kClosure = v8::HeapGraphNode::kClosure,
    kRegExp = v8::HeapGraphNode::kRegExp,
    kHeapNumber = v8::HeapGraphNode::kHeapNumber
  };

  HeapEntry() { }
  void Init(HeapSnapshot* snapshot,
            Type type,
            const char* name,
            uint64_t id,
            int self_size,
            int children_count,
            int retainers_count);

  HeapSnapshot* snapshot() { return snapshot_; }
  Type type() { return static_cast<Type>(type_); }
  const char* name() { return name_; }
  uint64_t id();
  int self_size() { return self_size_; }
  int retained_size() { return retained_size_; }
  void add_retained_size(int size) { retained_size_ += size; }
  void set_retained_size(int value) { retained_size_ = value; }
  int ordered_index() { return ordered_index_; }
  void set_ordered_index(int value) { ordered_index_ = value; }

  Vector<HeapGraphEdge> children() {
    return Vector<HeapGraphEdge>(children_arr(), children_count_); }
  Vector<HeapGraphEdge*> retainers() {
    return Vector<HeapGraphEdge*>(retainers_arr(), retainers_count_); }
  List<HeapGraphPath*>* GetRetainingPaths();
  HeapEntry* dominator() { return dominator_; }
  void set_dominator(HeapEntry* entry) { dominator_ = entry; }

  void clear_paint() { painted_ = kUnpainted; }
  bool painted_reachable() { return painted_ == kPainted; }
  void paint_reachable() {
    ASSERT(painted_ == kUnpainted);
    painted_ = kPainted;
  }
  bool not_painted_reachable_from_others() {
    return painted_ != kPaintedReachableFromOthers;
  }
  void paint_reachable_from_others() {
    painted_ = kPaintedReachableFromOthers;
  }
  template<class Visitor>
  void ApplyAndPaintAllReachable(Visitor* visitor);
  void PaintAllReachable();

  bool is_leaf() { return painted_ == kLeaf; }
  void set_leaf() { painted_ = kLeaf; }
  bool is_non_leaf() { return painted_ == kNonLeaf; }
  void set_non_leaf() { painted_ = kNonLeaf; }
  bool is_processed() { return painted_ == kProcessed; }
  void set_processed() { painted_ = kProcessed; }

  void SetIndexedReference(HeapGraphEdge::Type type,
                           int child_index,
                           int index,
                           HeapEntry* entry,
                           int retainer_index);
  void SetNamedReference(HeapGraphEdge::Type type,
                         int child_index,
                         const char* name,
                         HeapEntry* entry,
                         int retainer_index);
  void SetUnidirElementReference(int child_index, int index, HeapEntry* entry);

  int EntrySize() { return EntriesSize(1, children_count_, retainers_count_); }
  int RetainedSize(bool exact);
  List<HeapGraphPath*>* CalculateRetainingPaths();

  void Print(int max_depth, int indent);

  static int EntriesSize(int entries_count,
                         int children_count,
                         int retainers_count);
  static uint32_t Hash(HeapEntry* entry) {
    return ComputeIntegerHash(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entry)));
  }
  static bool Match(void* entry1, void* entry2) { return entry1 == entry2; }

 private:
  HeapGraphEdge* children_arr() {
    return reinterpret_cast<HeapGraphEdge*>(this + 1);
  }
  HeapGraphEdge** retainers_arr() {
    return reinterpret_cast<HeapGraphEdge**>(children_arr() + children_count_);
  }
  void CalculateExactRetainedSize();
  void FindRetainingPaths(CachedHeapGraphPath* prev_path,
                          List<HeapGraphPath*>* retaining_paths);
  const char* TypeAsString();

  unsigned painted_: 2;
  unsigned type_: 3;
  int children_count_: 27;
  int retainers_count_;
  int self_size_;
  union {
    int ordered_index_;  // Used during dominator tree building.
    int retained_size_;  // At that moment, there is no retained size yet.
  };
  HeapEntry* dominator_;
  HeapSnapshot* snapshot_;
  struct Id {
    uint32_t id1_;
    uint32_t id2_;
  } id_;  // This is to avoid extra padding of 64-bit value.
  const char* name_;

  // Paints used for exact retained sizes calculation.
  static const unsigned kUnpainted = 0;
  static const unsigned kPainted = 1;
  static const unsigned kPaintedReachableFromOthers = 2;
  // Paints used for approximate retained sizes calculation.
  static const unsigned kLeaf = 0;
  static const unsigned kNonLeaf = 1;
  static const unsigned kProcessed = 2;

  static const int kExactRetainedSizeTag = 1;

  DISALLOW_COPY_AND_ASSIGN(HeapEntry);
};


class HeapGraphPath {
 public:
  HeapGraphPath()
      : path_(8) { }
  explicit HeapGraphPath(const List<HeapGraphEdge*>& path);

  void Add(HeapGraphEdge* edge) { path_.Add(edge); }
  void Set(int index, HeapGraphEdge* edge) { path_[index] = edge; }
  const List<HeapGraphEdge*>* path() { return &path_; }

  void Print();

 private:
  List<HeapGraphEdge*> path_;

  DISALLOW_COPY_AND_ASSIGN(HeapGraphPath);
};


class HeapSnapshotsCollection;
class HeapSnapshotsDiff;

// HeapSnapshot represents a single heap snapshot. It is stored in
// HeapSnapshotsCollection, which is also a factory for
// HeapSnapshots. All HeapSnapshots share strings copied from JS heap
// to be able to return them even if they were collected.
// HeapSnapshotGenerator fills in a HeapSnapshot.
class HeapSnapshot {
 public:
  enum Type {
    kFull = v8::HeapSnapshot::kFull,
    kAggregated = v8::HeapSnapshot::kAggregated
  };

  HeapSnapshot(HeapSnapshotsCollection* collection,
               Type type,
               const char* title,
               unsigned uid);
  ~HeapSnapshot();

  HeapSnapshotsCollection* collection() { return collection_; }
  Type type() { return type_; }
  const char* title() { return title_; }
  unsigned uid() { return uid_; }
  HeapEntry* root() { return root_entry_; }
  HeapEntry* gc_roots() { return gc_roots_entry_; }

  void AllocateEntries(
      int entries_count, int children_count, int retainers_count);
  HeapEntry* AddEntry(
      HeapObject* object, int children_count, int retainers_count);
  HeapEntry* AddEntry(HeapEntry::Type type,
                      const char* name,
                      uint64_t id,
                      int size,
                      int children_count,
                      int retainers_count);
  void ApproximateRetainedSizes();
  void ClearPaint();
  HeapSnapshotsDiff* CompareWith(HeapSnapshot* snapshot);
  List<HeapGraphPath*>* GetRetainingPaths(HeapEntry* entry);
  List<HeapEntry*>* GetSortedEntriesList();
  template<class Visitor>
  void IterateEntries(Visitor* visitor) { entries_.Iterate(visitor); }
  void SetDominatorsToSelf();

  void Print(int max_depth);
  void PrintEntriesSize();

  static HeapObject* const kInternalRootObject;
  static HeapObject* const kGcRootsObject;

 private:
  HeapEntry* AddEntry(HeapObject* object,
                      HeapEntry::Type type,
                      const char* name,
                      int children_count,
                      int retainers_count);
  HeapEntry* GetNextEntryToInit();
  void BuildDominatorTree(const Vector<HeapEntry*>& entries,
                          Vector<HeapEntry*>* dominators);
  void FillReversePostorderIndexes(Vector<HeapEntry*>* entries);
  void SetEntriesDominators();

  HeapSnapshotsCollection* collection_;
  Type type_;
  const char* title_;
  unsigned uid_;
  HeapEntry* root_entry_;
  HeapEntry* gc_roots_entry_;
  char* raw_entries_;
  List<HeapEntry*> entries_;
  bool entries_sorted_;
  HashMap retaining_paths_;
#ifdef DEBUG
  int raw_entries_size_;
#endif

  friend class HeapSnapshotTester;

  DISALLOW_COPY_AND_ASSIGN(HeapSnapshot);
};


class HeapObjectsMap {
 public:
  HeapObjectsMap();
  ~HeapObjectsMap();

  void SnapshotGenerationFinished();
  uint64_t FindObject(Address addr);
  void MoveObject(Address from, Address to);

  static const uint64_t kInternalRootObjectId;
  static const uint64_t kGcRootsObjectId;
  static const uint64_t kFirstAvailableObjectId;

 private:
  struct EntryInfo {
    explicit EntryInfo(uint64_t id) : id(id), accessed(true) { }
    EntryInfo(uint64_t id, bool accessed) : id(id), accessed(accessed) { }
    uint64_t id;
    bool accessed;
  };

  void AddEntry(Address addr, uint64_t id);
  uint64_t FindEntry(Address addr);
  void RemoveDeadEntries();

  static bool AddressesMatch(void* key1, void* key2) {
    return key1 == key2;
  }

  static uint32_t AddressHash(Address addr) {
    return ComputeIntegerHash(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
  }

  bool initial_fill_mode_;
  uint64_t next_id_;
  HashMap entries_map_;
  List<EntryInfo>* entries_;

  DISALLOW_COPY_AND_ASSIGN(HeapObjectsMap);
};


class HeapSnapshotsDiff {
 public:
  HeapSnapshotsDiff(HeapSnapshot* snapshot1, HeapSnapshot* snapshot2)
      : snapshot1_(snapshot1),
        snapshot2_(snapshot2),
        raw_additions_root_(NULL),
        raw_deletions_root_(NULL) { }

  ~HeapSnapshotsDiff() {
    DeleteArray(raw_deletions_root_);
    DeleteArray(raw_additions_root_);
  }

  void AddAddedEntry(int child_index, int index, HeapEntry* entry) {
    additions_root()->SetUnidirElementReference(child_index, index, entry);
  }

  void AddDeletedEntry(int child_index, int index, HeapEntry* entry) {
    deletions_root()->SetUnidirElementReference(child_index, index, entry);
  }

  void CreateRoots(int additions_count, int deletions_count);

  HeapEntry* additions_root() {
    return reinterpret_cast<HeapEntry*>(raw_additions_root_);
  }
  HeapEntry* deletions_root() {
    return reinterpret_cast<HeapEntry*>(raw_deletions_root_);
  }

 private:
  HeapSnapshot* snapshot1_;
  HeapSnapshot* snapshot2_;
  char* raw_additions_root_;
  char* raw_deletions_root_;

  DISALLOW_COPY_AND_ASSIGN(HeapSnapshotsDiff);
};


class HeapSnapshotsComparator {
 public:
  HeapSnapshotsComparator() { }
  ~HeapSnapshotsComparator();
  HeapSnapshotsDiff* Compare(HeapSnapshot* snapshot1, HeapSnapshot* snapshot2);
 private:
  List<HeapSnapshotsDiff*> diffs_;

  DISALLOW_COPY_AND_ASSIGN(HeapSnapshotsComparator);
};


class HeapSnapshotsCollection {
 public:
  HeapSnapshotsCollection();
  ~HeapSnapshotsCollection();

  bool is_tracking_objects() { return is_tracking_objects_; }

  HeapSnapshot* NewSnapshot(
      HeapSnapshot::Type type, const char* name, unsigned uid);
  void SnapshotGenerationFinished() { ids_.SnapshotGenerationFinished(); }
  List<HeapSnapshot*>* snapshots() { return &snapshots_; }
  HeapSnapshot* GetSnapshot(unsigned uid);

  const char* GetName(String* name) { return names_.GetName(name); }
  const char* GetName(int index) { return names_.GetName(index); }
  const char* GetFunctionName(String* name) {
    return names_.GetFunctionName(name);
  }

  TokenEnumerator* token_enumerator() { return token_enumerator_; }

  uint64_t GetObjectId(Address addr) { return ids_.FindObject(addr); }
  void ObjectMoveEvent(Address from, Address to) { ids_.MoveObject(from, to); }

  HeapSnapshotsDiff* CompareSnapshots(HeapSnapshot* snapshot1,
                                      HeapSnapshot* snapshot2);

 private:
  INLINE(static bool HeapSnapshotsMatch(void* key1, void* key2)) {
    return key1 == key2;
  }

  bool is_tracking_objects_;  // Whether tracking object moves is needed.
  List<HeapSnapshot*> snapshots_;
  // Mapping from snapshots' uids to HeapSnapshot* pointers.
  HashMap snapshots_uids_;
  StringsStorage names_;
  TokenEnumerator* token_enumerator_;
  // Mapping from HeapObject addresses to objects' uids.
  HeapObjectsMap ids_;
  HeapSnapshotsComparator comparator_;

  DISALLOW_COPY_AND_ASSIGN(HeapSnapshotsCollection);
};


// The HeapEntriesMap instance is used to track a mapping between
// real heap objects and their representations in heap snapshots.
class HeapEntriesMap {
 public:
  HeapEntriesMap();
  ~HeapEntriesMap();

  HeapEntry* Map(HeapObject* object);
  void Pair(HeapObject* object, HeapEntry* entry);
  void CountReference(HeapObject* from, HeapObject* to,
                      int* prev_children_count = NULL,
                      int* prev_retainers_count = NULL);
  template<class Visitor>
  void UpdateEntries(Visitor* visitor);

  int entries_count() { return entries_count_; }
  int total_children_count() { return total_children_count_; }
  int total_retainers_count() { return total_retainers_count_; }

  static HeapEntry *const kHeapEntryPlaceholder;

 private:
  struct EntryInfo {
    explicit EntryInfo(HeapEntry* entry)
        : entry(entry), children_count(0), retainers_count(0) { }
    HeapEntry* entry;
    int children_count;
    int retainers_count;
  };

  static uint32_t Hash(HeapObject* object) {
    return ComputeIntegerHash(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object)));
  }
  static bool HeapObjectsMatch(void* key1, void* key2) { return key1 == key2; }

  HashMap entries_;
  int entries_count_;
  int total_children_count_;
  int total_retainers_count_;

  friend class HeapObjectsSet;

  DISALLOW_COPY_AND_ASSIGN(HeapEntriesMap);
};


class HeapObjectsSet {
 public:
  HeapObjectsSet();
  void Clear();
  bool Contains(Object* object);
  void Insert(Object* obj);

 private:
  HashMap entries_;

  DISALLOW_COPY_AND_ASSIGN(HeapObjectsSet);
};


class HeapSnapshotGenerator {
 public:
  class SnapshotFillerInterface {
   public:
    virtual ~SnapshotFillerInterface() { }
    virtual HeapEntry* AddEntry(HeapObject* obj) = 0;
    virtual void SetIndexedReference(HeapGraphEdge::Type type,
                                     HeapObject* parent_obj,
                                     HeapEntry* parent_entry,
                                     int index,
                                     Object* child_obj,
                                     HeapEntry* child_entry) = 0;
    virtual void SetNamedReference(HeapGraphEdge::Type type,
                                   HeapObject* parent_obj,
                                   HeapEntry* parent_entry,
                                   const char* reference_name,
                                   Object* child_obj,
                                   HeapEntry* child_entry) = 0;
    virtual void SetRootGcRootsReference() = 0;
    virtual void SetRootShortcutReference(Object* child_obj,
                                          HeapEntry* child_entry) = 0;
    virtual void SetStrongRootReference(Object* child_obj,
                                        HeapEntry* child_entry) = 0;
  };

  explicit HeapSnapshotGenerator(HeapSnapshot* snapshot);
  void GenerateSnapshot();

 private:
  HeapEntry* GetEntry(Object* obj);
  void ExtractReferences(HeapObject* obj);
  void ExtractClosureReferences(JSObject* js_obj, HeapEntry* entry);
  void ExtractPropertyReferences(JSObject* js_obj, HeapEntry* entry);
  void ExtractElementReferences(JSObject* js_obj, HeapEntry* entry);
  void ExtractInternalReferences(JSObject* js_obj, HeapEntry* entry);
  void SetClosureReference(HeapObject* parent_obj,
                           HeapEntry* parent,
                           String* reference_name,
                           Object* child);
  void SetElementReference(HeapObject* parent_obj,
                           HeapEntry* parent,
                           int index,
                           Object* child);
  void SetInternalReference(HeapObject* parent_obj,
                            HeapEntry* parent,
                            const char* reference_name,
                            Object* child);
  void SetInternalReference(HeapObject* parent_obj,
                            HeapEntry* parent,
                            int index,
                            Object* child);
  void SetHiddenReference(HeapObject* parent_obj,
                          HeapEntry* parent,
                          int index,
                          Object* child);
  void SetPropertyReference(HeapObject* parent_obj,
                            HeapEntry* parent,
                            String* reference_name,
                            Object* child);
  void SetPropertyShortcutReference(HeapObject* parent_obj,
                                    HeapEntry* parent,
                                    String* reference_name,
                                    Object* child);
  void SetRootShortcutReference(Object* child);
  void SetRootGcRootsReference();
  void SetGcRootsReference(Object* child);

  HeapSnapshot* snapshot_;
  HeapSnapshotsCollection* collection_;
  // Mapping from HeapObject* pointers to HeapEntry* pointers.
  HeapEntriesMap entries_;
  SnapshotFillerInterface* filler_;
  // Used during references extraction to mark heap objects that
  // are references via non-hidden properties.
  HeapObjectsSet known_references_;

  friend class IndexedReferencesExtractor;
  friend class RootsReferencesExtractor;

  DISALLOW_COPY_AND_ASSIGN(HeapSnapshotGenerator);
};

class OutputStreamWriter;

class HeapSnapshotJSONSerializer {
 public:
  explicit HeapSnapshotJSONSerializer(HeapSnapshot* snapshot)
      : snapshot_(snapshot),
        nodes_(ObjectsMatch),
        strings_(ObjectsMatch),
        next_node_id_(1),
        next_string_id_(1),
        writer_(NULL) {
  }
  void Serialize(v8::OutputStream* stream);

 private:
  INLINE(static bool ObjectsMatch(void* key1, void* key2)) {
    return key1 == key2;
  }

  INLINE(static uint32_t ObjectHash(const void* key)) {
    return ComputeIntegerHash(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(key)));
  }

  void EnumerateNodes();
  int GetNodeId(HeapEntry* entry);
  int GetStringId(const char* s);
  void SerializeEdge(HeapGraphEdge* edge);
  void SerializeImpl();
  void SerializeNode(HeapEntry* entry);
  void SerializeNodes();
  void SerializeSnapshot();
  void SerializeString(const unsigned char* s);
  void SerializeStrings();
  void SortHashMap(HashMap* map, List<HashMap::Entry*>* sorted_entries);

  HeapSnapshot* snapshot_;
  HashMap nodes_;
  HashMap strings_;
  int next_node_id_;
  int next_string_id_;
  OutputStreamWriter* writer_;

  friend class HeapSnapshotJSONSerializerEnumerator;
  friend class HeapSnapshotJSONSerializerIterator;

  DISALLOW_COPY_AND_ASSIGN(HeapSnapshotJSONSerializer);
};


String* GetConstructorNameForHeapProfile(JSObject* object);

} }  // namespace v8::internal

#endif  // ENABLE_LOGGING_AND_PROFILING

#endif  // V8_PROFILE_GENERATOR_H_
