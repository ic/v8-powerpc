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

#ifndef V8_SCOPEINFO_H_
#define V8_SCOPEINFO_H_

#include "allocation.h"
#include "variables.h"
#include "zone-inl.h"

namespace v8 {
namespace internal {

// Scope information represents information about a functions's
// scopes (currently only one, because we don't do any inlining)
// and the allocation of the scope's variables. Scope information
// is stored in a compressed form in FixedArray objects and is used
// at runtime (stack dumps, deoptimization, etc.).
//
// Historical note: In other VMs built by this team, ScopeInfo was
// usually called DebugInfo since the information was used (among
// other things) for on-demand debugging (Self, Smalltalk). However,
// DebugInfo seems misleading, since this information is primarily used
// in debugging-unrelated contexts.

// Forward defined as
// template <class Allocator = FreeStoreAllocationPolicy> class ScopeInfo;
template<class Allocator>
class ScopeInfo BASE_EMBEDDED {
 public:
  // Create a ScopeInfo instance from a scope.
  explicit ScopeInfo(Scope* scope);

  // Create a ScopeInfo instance from SerializedScopeInfo.
  explicit ScopeInfo(SerializedScopeInfo* data);

  // Creates a SerializedScopeInfo holding the serialized scope info.
  Handle<SerializedScopeInfo> Serialize();

  // --------------------------------------------------------------------------
  // Lookup

  Handle<String> function_name() const { return function_name_; }

  Handle<String> parameter_name(int i) const { return parameters_[i]; }
  int number_of_parameters() const { return parameters_.length(); }

  Handle<String> stack_slot_name(int i) const { return stack_slots_[i]; }
  int number_of_stack_slots() const { return stack_slots_.length(); }

  Handle<String> context_slot_name(int i) const {
    return context_slots_[i - Context::MIN_CONTEXT_SLOTS];
  }
  int number_of_context_slots() const {
    int l = context_slots_.length();
    return l == 0 ? 0 : l + Context::MIN_CONTEXT_SLOTS;
  }

  Handle<String> LocalName(int i) const;
  int NumberOfLocals() const;

  // --------------------------------------------------------------------------
  // Debugging support

#ifdef DEBUG
  void Print();
#endif

 private:
  Handle<String> function_name_;
  bool calls_eval_;
  bool is_strict_mode_;
  List<Handle<String>, Allocator > parameters_;
  List<Handle<String>, Allocator > stack_slots_;
  List<Handle<String>, Allocator > context_slots_;
  List<Variable::Mode, Allocator > context_modes_;
};


// This object provides quick access to scope info details for runtime
// routines w/o the need to explicitly create a ScopeInfo object.
class SerializedScopeInfo : public FixedArray {
 public :

  static SerializedScopeInfo* cast(Object* object) {
    ASSERT(object->IsFixedArray());
    return reinterpret_cast<SerializedScopeInfo*>(object);
  }

  // Does this scope call eval?
  bool CallsEval();

  // Is this scope a strict mode scope?
  bool IsStrictMode();

  // Return the number of stack slots for code.
  int NumberOfStackSlots();

  // Return the number of context slots for code.
  int NumberOfContextSlots();

  // Return if this has context slots besides MIN_CONTEXT_SLOTS;
  bool HasHeapAllocatedLocals();

  // Lookup support for serialized scope info. Returns the
  // the stack slot index for a given slot name if the slot is
  // present; otherwise returns a value < 0. The name must be a symbol
  // (canonicalized).
  int StackSlotIndex(String* name);

  // Lookup support for serialized scope info. Returns the
  // context slot index for a given slot name if the slot is present; otherwise
  // returns a value < 0. The name must be a symbol (canonicalized).
  // If the slot is present and mode != NULL, sets *mode to the corresponding
  // mode for that variable.
  int ContextSlotIndex(String* name, Variable::Mode* mode);

  // Lookup support for serialized scope info. Returns the
  // parameter index for a given parameter name if the parameter is present;
  // otherwise returns a value < 0. The name must be a symbol (canonicalized).
  int ParameterIndex(String* name);

  // Lookup support for serialized scope info. Returns the
  // function context slot index if the function name is present (named
  // function expressions, only), otherwise returns a value < 0. The name
  // must be a symbol (canonicalized).
  int FunctionContextSlotIndex(String* name);

  static Handle<SerializedScopeInfo> Create(Scope* scope);

  // Serializes empty scope info.
  static SerializedScopeInfo* Empty();

 private:

  inline Object** ContextEntriesAddr();

  inline Object** ParameterEntriesAddr();

  inline Object** StackSlotEntriesAddr();
};


// Cache for mapping (data, property name) into context slot index.
// The cache contains both positive and negative results.
// Slot index equals -1 means the property is absent.
// Cleared at startup and prior to mark sweep collection.
class ContextSlotCache {
 public:
  // Lookup context slot index for (data, name).
  // If absent, kNotFound is returned.
  int Lookup(Object* data,
             String* name,
             Variable::Mode* mode);

  // Update an element in the cache.
  void Update(Object* data,
              String* name,
              Variable::Mode mode,
              int slot_index);

  // Clear the cache.
  void Clear();

  static const int kNotFound = -2;
 private:
  ContextSlotCache() {
    for (int i = 0; i < kLength; ++i) {
      keys_[i].data = NULL;
      keys_[i].name = NULL;
      values_[i] = kNotFound;
    }
  }

  inline static int Hash(Object* data, String* name);

#ifdef DEBUG
  void ValidateEntry(Object* data,
                     String* name,
                     Variable::Mode mode,
                     int slot_index);
#endif

  static const int kLength = 256;
  struct Key {
    Object* data;
    String* name;
  };

  struct Value {
    Value(Variable::Mode mode, int index) {
      ASSERT(ModeField::is_valid(mode));
      ASSERT(IndexField::is_valid(index));
      value_ = ModeField::encode(mode) | IndexField::encode(index);
      ASSERT(mode == this->mode());
      ASSERT(index == this->index());
    }

    explicit inline Value(uint32_t value) : value_(value) {}

    uint32_t raw() { return value_; }

    Variable::Mode mode() { return ModeField::decode(value_); }

    int index() { return IndexField::decode(value_); }

    // Bit fields in value_ (type, shift, size). Must be public so the
    // constants can be embedded in generated code.
    class ModeField:  public BitField<Variable::Mode, 0, 3> {};
    class IndexField: public BitField<int,            3, 32-3> {};
   private:
    uint32_t value_;
  };

  Key keys_[kLength];
  uint32_t values_[kLength];

  friend class Isolate;
  DISALLOW_COPY_AND_ASSIGN(ContextSlotCache);
};


} }  // namespace v8::internal

#endif  // V8_SCOPEINFO_H_
