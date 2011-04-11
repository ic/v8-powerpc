// Copyright 2009 the V8 project authors. All rights reserved.
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

#include "api.h"
#include "global-handles.h"

#include "vm-state-inl.h"

namespace v8 {
namespace internal {


ObjectGroup::~ObjectGroup() {
  if (info_ != NULL) info_->Dispose();
}


class GlobalHandles::Node : public Malloced {
 public:

  void Initialize(Object* object) {
    // Set the initial value of the handle.
    object_ = object;
    class_id_ = v8::HeapProfiler::kPersistentHandleNoClassId;
    state_  = NORMAL;
    parameter_or_next_free_.parameter = NULL;
    callback_ = NULL;
  }

  Node() {
    state_ = DESTROYED;
  }

  explicit Node(Object* object) {
    Initialize(object);
    // Initialize link structure.
    next_ = NULL;
  }

  ~Node() {
    if (state_ != DESTROYED) Destroy(Isolate::Current()->global_handles());
#ifdef DEBUG
    // Zap the values for eager trapping.
    object_ = NULL;
    next_ = NULL;
    parameter_or_next_free_.next_free = NULL;
#endif
  }

  void Destroy(GlobalHandles* global_handles) {
    if (state_ == WEAK || IsNearDeath()) {
      global_handles->number_of_weak_handles_--;
      if (object_->IsJSGlobalObject()) {
        global_handles->number_of_global_object_weak_handles_--;
      }
    }
    state_ = DESTROYED;
  }

  // Accessors for next_.
  Node* next() { return next_; }
  void set_next(Node* value) { next_ = value; }
  Node** next_addr() { return &next_; }

  // Accessors for next free node in the free list.
  Node* next_free() {
    ASSERT(state_ == DESTROYED);
    return parameter_or_next_free_.next_free;
  }
  void set_next_free(Node* value) {
    ASSERT(state_ == DESTROYED);
    parameter_or_next_free_.next_free = value;
  }

  // Returns a link from the handle.
  static Node* FromLocation(Object** location) {
    ASSERT(OFFSET_OF(Node, object_) == 0);
    return reinterpret_cast<Node*>(location);
  }

  // Returns the handle.
  Handle<Object> handle() { return Handle<Object>(&object_); }

  // Make this handle weak.
  void MakeWeak(GlobalHandles* global_handles, void* parameter,
                WeakReferenceCallback callback) {
    LOG(global_handles->isolate(),
        HandleEvent("GlobalHandle::MakeWeak", handle().location()));
    ASSERT(state_ != DESTROYED);
    if (state_ != WEAK && !IsNearDeath()) {
      global_handles->number_of_weak_handles_++;
      if (object_->IsJSGlobalObject()) {
        global_handles->number_of_global_object_weak_handles_++;
      }
    }
    state_ = WEAK;
    set_parameter(parameter);
    callback_ = callback;
  }

  void ClearWeakness(GlobalHandles* global_handles) {
    LOG(global_handles->isolate(),
        HandleEvent("GlobalHandle::ClearWeakness", handle().location()));
    ASSERT(state_ != DESTROYED);
    if (state_ == WEAK || IsNearDeath()) {
      global_handles->number_of_weak_handles_--;
      if (object_->IsJSGlobalObject()) {
        global_handles->number_of_global_object_weak_handles_--;
      }
    }
    state_ = NORMAL;
    set_parameter(NULL);
  }

  bool IsNearDeath() {
    // Check for PENDING to ensure correct answer when processing callbacks.
    return state_ == PENDING || state_ == NEAR_DEATH;
  }

  bool IsWeak() {
    return state_ == WEAK;
  }

  bool CanBeRetainer() {
    return state_ != DESTROYED && state_ != NEAR_DEATH;
  }

  void SetWrapperClassId(uint16_t class_id) {
    class_id_ = class_id;
  }

  // Returns the id for this weak handle.
  void set_parameter(void* parameter) {
    ASSERT(state_ != DESTROYED);
    parameter_or_next_free_.parameter = parameter;
  }
  void* parameter() {
    ASSERT(state_ != DESTROYED);
    return parameter_or_next_free_.parameter;
  }

  // Returns the callback for this weak handle.
  WeakReferenceCallback callback() { return callback_; }

  bool PostGarbageCollectionProcessing(Isolate* isolate,
                                       GlobalHandles* global_handles) {
    if (state_ != Node::PENDING) return false;
    LOG(isolate, HandleEvent("GlobalHandle::Processing", handle().location()));
    WeakReferenceCallback func = callback();
    if (func == NULL) {
      Destroy(global_handles);
      return false;
    }
    void* par = parameter();
    state_ = NEAR_DEATH;
    set_parameter(NULL);

    v8::Persistent<v8::Object> object = ToApi<v8::Object>(handle());
    {
      // Forbid reuse of destroyed nodes as they might be already deallocated.
      // It's fine though to reuse nodes that were destroyed in weak callback
      // as those cannot be deallocated until we are back from the callback.
      global_handles->set_first_free(NULL);
      if (global_handles->first_deallocated()) {
        global_handles->first_deallocated()->set_next(global_handles->head());
      }
      // Check that we are not passing a finalized external string to
      // the callback.
      ASSERT(!object_->IsExternalAsciiString() ||
             ExternalAsciiString::cast(object_)->resource() != NULL);
      ASSERT(!object_->IsExternalTwoByteString() ||
             ExternalTwoByteString::cast(object_)->resource() != NULL);
      // Leaving V8.
      VMState state(isolate, EXTERNAL);
      func(object, par);
    }
    // Absense of explicit cleanup or revival of weak handle
    // in most of the cases would lead to memory leak.
    ASSERT(state_ != NEAR_DEATH);
    return true;
  }

  // Place the handle address first to avoid offset computation.
  Object* object_;  // Storage for object pointer.

  uint16_t class_id_;

  // Transition diagram:
  // NORMAL <-> WEAK -> PENDING -> NEAR_DEATH -> { NORMAL, WEAK, DESTROYED }
  enum State {
    NORMAL,      // Normal global handle.
    WEAK,        // Flagged as weak but not yet finalized.
    PENDING,     // Has been recognized as only reachable by weak handles.
    NEAR_DEATH,  // Callback has informed the handle is near death.
    DESTROYED
  };
  State state_ : 4;  // Need one more bit for MSVC as it treats enums as signed.

 private:
  // Handle specific callback.
  WeakReferenceCallback callback_;
  // Provided data for callback.  In DESTROYED state, this is used for
  // the free list link.
  union {
    void* parameter;
    Node* next_free;
  } parameter_or_next_free_;

  // Linkage for the list.
  Node* next_;

 public:
  TRACK_MEMORY("GlobalHandles::Node")
};


class GlobalHandles::Pool {
  public:
    Pool() {
      current_ = new Chunk();
      current_->previous = NULL;
      next_ = current_->nodes;
      limit_ = current_->nodes + kNodesPerChunk;
    }

    ~Pool() {
      if (current_ != NULL) {
        Release();
      }
    }

    Node* Allocate() {
      if (next_ < limit_) {
        return next_++;
      }
      return SlowAllocate();
    }

    void Release() {
      Chunk* current = current_;
      ASSERT(current != NULL);  // At least a single block must by allocated
      do {
        Chunk* previous = current->previous;
        delete current;
        current = previous;
      } while (current != NULL);
      current_ = NULL;
      next_ = limit_ = NULL;
    }

  private:
    static const int kNodesPerChunk = (1 << 12) - 1;
    struct Chunk : public Malloced {
      Chunk* previous;
      Node nodes[kNodesPerChunk];
    };

    Node* SlowAllocate() {
      Chunk* chunk = new Chunk();
      chunk->previous = current_;
      current_ = chunk;

      Node* new_nodes = current_->nodes;
      next_ = new_nodes + 1;
      limit_ = new_nodes + kNodesPerChunk;
      return new_nodes;
    }

    Chunk* current_;
    Node* next_;
    Node* limit_;
};


GlobalHandles::GlobalHandles(Isolate* isolate)
    : isolate_(isolate),
      number_of_weak_handles_(0),
      number_of_global_object_weak_handles_(0),
      head_(NULL),
      first_free_(NULL),
      first_deallocated_(NULL),
      pool_(new Pool()),
      post_gc_processing_count_(0),
      object_groups_(4) {
}


GlobalHandles::~GlobalHandles() {
  delete pool_;
  pool_ = 0;
}


Handle<Object> GlobalHandles::Create(Object* value) {
  isolate_->counters()->global_handles()->Increment();
  Node* result;
  if (first_free()) {
    // Take the first node in the free list.
    result = first_free();
    set_first_free(result->next_free());
  } else if (first_deallocated()) {
    // Next try deallocated list
    result = first_deallocated();
    set_first_deallocated(result->next_free());
    ASSERT(result->next() == head());
    set_head(result);
  } else {
    // Allocate a new node.
    result = pool_->Allocate();
    result->set_next(head());
    set_head(result);
  }
  result->Initialize(value);
  return result->handle();
}


void GlobalHandles::Destroy(Object** location) {
  isolate_->counters()->global_handles()->Decrement();
  if (location == NULL) return;
  Node* node = Node::FromLocation(location);
  node->Destroy(this);
  // Link the destroyed.
  node->set_next_free(first_free());
  set_first_free(node);
}


void GlobalHandles::MakeWeak(Object** location, void* parameter,
                             WeakReferenceCallback callback) {
  ASSERT(callback != NULL);
  Node::FromLocation(location)->MakeWeak(this, parameter, callback);
}


void GlobalHandles::ClearWeakness(Object** location) {
  Node::FromLocation(location)->ClearWeakness(this);
}


bool GlobalHandles::IsNearDeath(Object** location) {
  return Node::FromLocation(location)->IsNearDeath();
}


bool GlobalHandles::IsWeak(Object** location) {
  return Node::FromLocation(location)->IsWeak();
}


void GlobalHandles::SetWrapperClassId(Object** location, uint16_t class_id) {
  Node::FromLocation(location)->SetWrapperClassId(class_id);
}


void GlobalHandles::IterateWeakRoots(ObjectVisitor* v) {
  // Traversal of GC roots in the global handle list that are marked as
  // WEAK or PENDING.
  for (Node* current = head_; current != NULL; current = current->next()) {
    if (current->state_ == Node::WEAK
      || current->state_ == Node::PENDING
      || current->state_ == Node::NEAR_DEATH) {
      v->VisitPointer(&current->object_);
    }
  }
}


void GlobalHandles::IterateWeakRoots(WeakReferenceGuest f,
                                     WeakReferenceCallback callback) {
  for (Node* current = head_; current != NULL; current = current->next()) {
    if (current->IsWeak() && current->callback() == callback) {
      f(current->object_, current->parameter());
    }
  }
}


void GlobalHandles::IdentifyWeakHandles(WeakSlotCallback f) {
  for (Node* current = head_; current != NULL; current = current->next()) {
    if (current->state_ == Node::WEAK) {
      if (f(&current->object_)) {
        current->state_ = Node::PENDING;
        LOG(isolate_,
            HandleEvent("GlobalHandle::Pending", current->handle().location()));
      }
    }
  }
}


bool GlobalHandles::PostGarbageCollectionProcessing() {
  // Process weak global handle callbacks. This must be done after the
  // GC is completely done, because the callbacks may invoke arbitrary
  // API functions.
  // At the same time deallocate all DESTROYED nodes.
  ASSERT(isolate_->heap()->gc_state() == Heap::NOT_IN_GC);
  const int initial_post_gc_processing_count = ++post_gc_processing_count_;
  bool next_gc_likely_to_collect_more = false;
  Node** p = &head_;
  while (*p != NULL) {
    if ((*p)->PostGarbageCollectionProcessing(isolate_, this)) {
      if (initial_post_gc_processing_count != post_gc_processing_count_) {
        // Weak callback triggered another GC and another round of
        // PostGarbageCollection processing.  The current node might
        // have been deleted in that round, so we need to bail out (or
        // restart the processing).
        break;
      }
    }
    if ((*p)->state_ == Node::DESTROYED) {
      // Delete the link.
      Node* node = *p;
      *p = node->next();  // Update the link.
      if (first_deallocated()) {
        first_deallocated()->set_next(node);
      }
      node->set_next_free(first_deallocated());
      set_first_deallocated(node);
      next_gc_likely_to_collect_more = true;
    } else {
      p = (*p)->next_addr();
    }
  }
  set_first_free(NULL);
  if (first_deallocated()) {
    first_deallocated()->set_next(head());
  }

  return next_gc_likely_to_collect_more;
}


void GlobalHandles::IterateStrongRoots(ObjectVisitor* v) {
  // Traversal of global handles marked as NORMAL.
  for (Node* current = head_; current != NULL; current = current->next()) {
    if (current->state_ == Node::NORMAL) {
      v->VisitPointer(&current->object_);
    }
  }
}


void GlobalHandles::IterateAllRoots(ObjectVisitor* v) {
  for (Node* current = head_; current != NULL; current = current->next()) {
    if (current->state_ != Node::DESTROYED) {
      v->VisitPointer(&current->object_);
    }
  }
}


void GlobalHandles::IterateAllRootsWithClassIds(ObjectVisitor* v) {
  for (Node* current = head_; current != NULL; current = current->next()) {
    if (current->class_id_ != v8::HeapProfiler::kPersistentHandleNoClassId &&
        current->CanBeRetainer()) {
      v->VisitEmbedderReference(&current->object_, current->class_id_);
    }
  }
}


void GlobalHandles::TearDown() {
  // Reset all the lists.
  set_head(NULL);
  set_first_free(NULL);
  set_first_deallocated(NULL);
  pool_->Release();
}


void GlobalHandles::RecordStats(HeapStats* stats) {
  *stats->global_handle_count = 0;
  *stats->weak_global_handle_count = 0;
  *stats->pending_global_handle_count = 0;
  *stats->near_death_global_handle_count = 0;
  *stats->destroyed_global_handle_count = 0;
  for (Node* current = head_; current != NULL; current = current->next()) {
    *stats->global_handle_count += 1;
    if (current->state_ == Node::WEAK) {
      *stats->weak_global_handle_count += 1;
    } else if (current->state_ == Node::PENDING) {
      *stats->pending_global_handle_count += 1;
    } else if (current->state_ == Node::NEAR_DEATH) {
      *stats->near_death_global_handle_count += 1;
    } else if (current->state_ == Node::DESTROYED) {
      *stats->destroyed_global_handle_count += 1;
    }
  }
}

#ifdef DEBUG

void GlobalHandles::PrintStats() {
  int total = 0;
  int weak = 0;
  int pending = 0;
  int near_death = 0;
  int destroyed = 0;

  for (Node* current = head_; current != NULL; current = current->next()) {
    total++;
    if (current->state_ == Node::WEAK) weak++;
    if (current->state_ == Node::PENDING) pending++;
    if (current->state_ == Node::NEAR_DEATH) near_death++;
    if (current->state_ == Node::DESTROYED) destroyed++;
  }

  PrintF("Global Handle Statistics:\n");
  PrintF("  allocated memory = %" V8_PTR_PREFIX "dB\n", sizeof(Node) * total);
  PrintF("  # weak       = %d\n", weak);
  PrintF("  # pending    = %d\n", pending);
  PrintF("  # near_death = %d\n", near_death);
  PrintF("  # destroyed  = %d\n", destroyed);
  PrintF("  # total      = %d\n", total);
}

void GlobalHandles::Print() {
  PrintF("Global handles:\n");
  for (Node* current = head_; current != NULL; current = current->next()) {
    PrintF("  handle %p to %p (weak=%d)\n",
           reinterpret_cast<void*>(current->handle().location()),
           reinterpret_cast<void*>(*current->handle()),
           current->state_ == Node::WEAK);
  }
}

#endif



void GlobalHandles::AddObjectGroup(Object*** handles,
                                   size_t length,
                                   v8::RetainedObjectInfo* info) {
  if (length == 0) {
    if (info != NULL) info->Dispose();
    return;
  }
  object_groups_.Add(ObjectGroup::New(handles, length, info));
}


void GlobalHandles::AddImplicitReferences(HeapObject** parent,
                                          Object*** children,
                                          size_t length) {
  if (length == 0) return;
  implicit_ref_groups_.Add(ImplicitRefGroup::New(parent, children, length));
}


void GlobalHandles::RemoveObjectGroups() {
  for (int i = 0; i < object_groups_.length(); i++) {
    object_groups_.at(i)->Dispose();
  }
  object_groups_.Clear();
}


void GlobalHandles::RemoveImplicitRefGroups() {
  for (int i = 0; i < implicit_ref_groups_.length(); i++) {
    implicit_ref_groups_.at(i)->Dispose();
  }
  implicit_ref_groups_.Clear();
}


} }  // namespace v8::internal
