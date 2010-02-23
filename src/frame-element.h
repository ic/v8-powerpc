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

#ifndef V8_FRAME_ELEMENT_H_
#define V8_FRAME_ELEMENT_H_

#include "number-info.h"
#include "macro-assembler.h"

namespace v8 {
namespace internal {

// -------------------------------------------------------------------------
// Virtual frame elements
//
// The internal elements of the virtual frames.  There are several kinds of
// elements:
//   * Invalid: elements that are uninitialized or not actually part
//     of the virtual frame.  They should not be read.
//   * Memory: an element that resides in the actual frame.  Its address is
//     given by its position in the virtual frame.
//   * Register: an element that resides in a register.
//   * Constant: an element whose value is known at compile time.

class FrameElement BASE_EMBEDDED {
 public:
  enum SyncFlag {
    NOT_SYNCED,
    SYNCED
  };

  inline NumberInfo::Type number_info() {
    // Copied elements do not have number info. Instead
    // we have to inspect their backing element in the frame.
    ASSERT(!is_copy());
    if (!is_constant()) return NumberInfoField::decode(value_);
    Handle<Object> value = handle();
    if (value->IsSmi()) return NumberInfo::kSmi;
    if (value->IsHeapNumber()) return NumberInfo::kHeapNumber;
    return NumberInfo::kUnknown;
  }

  inline void set_number_info(NumberInfo::Type info) {
    // Copied elements do not have number info. Instead
    // we have to inspect their backing element in the frame.
    ASSERT(!is_copy());
    value_ = value_ & ~NumberInfoField::mask();
    value_ = value_ | NumberInfoField::encode(info);
  }

  // The default constructor creates an invalid frame element.
  FrameElement() {
    value_ = TypeField::encode(INVALID)
        | CopiedField::encode(false)
        | SyncedField::encode(false)
        | NumberInfoField::encode(NumberInfo::kUninitialized)
        | DataField::encode(0);
  }

  // Factory function to construct an invalid frame element.
  static FrameElement InvalidElement() {
    FrameElement result;
    return result;
  }

  // Factory function to construct an in-memory frame element.
  static FrameElement MemoryElement(NumberInfo::Type info) {
    FrameElement result(MEMORY, no_reg, SYNCED, info);
    return result;
  }

  // Factory function to construct an in-register frame element.
  static FrameElement RegisterElement(Register reg,
                                      SyncFlag is_synced,
                                      NumberInfo::Type info) {
    return FrameElement(REGISTER, reg, is_synced, info);
  }

  // Factory function to construct a frame element whose value is known at
  // compile time.
  static FrameElement ConstantElement(Handle<Object> value,
                                      SyncFlag is_synced) {
    FrameElement result(value, is_synced);
    return result;
  }

  // Static indirection table for handles to constants.  If a frame
  // element represents a constant, the data contains an index into
  // this table of handles to the actual constants.
  typedef ZoneList<Handle<Object> > ZoneObjectList;

  static ZoneObjectList* ConstantList();

  // Clear the constants indirection table.
  static void ClearConstantList() {
    ConstantList()->Clear();
  }

  bool is_synced() const { return SyncedField::decode(value_); }

  void set_sync() {
    ASSERT(type() != MEMORY);
    value_ = value_ | SyncedField::encode(true);
  }

  void clear_sync() {
    ASSERT(type() != MEMORY);
    value_ = value_ & ~SyncedField::mask();
  }

  bool is_valid() const { return type() != INVALID; }
  bool is_memory() const { return type() == MEMORY; }
  bool is_register() const { return type() == REGISTER; }
  bool is_constant() const { return type() == CONSTANT; }
  bool is_copy() const { return type() == COPY; }

  bool is_copied() const { return CopiedField::decode(value_); }
  void set_copied() { value_ = value_ | CopiedField::encode(true); }
  void clear_copied() { value_ = value_ & ~CopiedField::mask(); }

  Register reg() const {
    ASSERT(is_register());
    uint32_t reg = DataField::decode(value_);
    Register result;
    result.code_ = reg;
    return result;
  }

  Handle<Object> handle() const {
    ASSERT(is_constant());
    return ConstantList()->at(DataField::decode(value_));
  }

  int index() const {
    ASSERT(is_copy());
    return DataField::decode(value_);
  }

  bool Equals(FrameElement other) {
    uint32_t masked_difference = (value_ ^ other.value_) & ~CopiedField::mask();
    if (!masked_difference) {
      // The elements are equal if they agree exactly except on copied field.
      return true;
    } else {
      // If two constants have the same value, and agree otherwise, return true.
       return !(masked_difference & ~DataField::mask()) &&
              is_constant() &&
              handle().is_identical_to(other.handle());
    }
  }

  // Test if two FrameElements refer to the same memory or register location.
  bool SameLocation(FrameElement* other) {
    if (type() == other->type()) {
      if (value_ == other->value_) return true;
      if (is_constant() && handle().is_identical_to(other->handle())) {
        return true;
      }
    }
    return false;
  }

  // Given a pair of non-null frame element pointers, return one of them
  // as an entry frame candidate or null if they are incompatible.
  FrameElement* Combine(FrameElement* other) {
    // If either is invalid, the result is.
    if (!is_valid()) return this;
    if (!other->is_valid()) return other;

    if (!SameLocation(other)) return NULL;
    // If either is unsynced, the result is.
    FrameElement* result = is_synced() ? other : this;
    return result;
  }

 private:
  enum Type {
    INVALID,
    MEMORY,
    REGISTER,
    CONSTANT,
    COPY
  };

  // Used to construct memory and register elements.
  FrameElement(Type type,
               Register reg,
               SyncFlag is_synced,
               NumberInfo::Type info) {
    value_ = TypeField::encode(type)
        | CopiedField::encode(false)
        | SyncedField::encode(is_synced != NOT_SYNCED)
        | NumberInfoField::encode(info)
        | DataField::encode(reg.code_ > 0 ? reg.code_ : 0);
  }

  // Used to construct constant elements.
  FrameElement(Handle<Object> value, SyncFlag is_synced) {
    value_ = TypeField::encode(CONSTANT)
        | CopiedField::encode(false)
        | SyncedField::encode(is_synced != NOT_SYNCED)
        | NumberInfoField::encode(NumberInfo::kUninitialized)
        | DataField::encode(ConstantList()->length());
    ConstantList()->Add(value);
  }

  Type type() const { return TypeField::decode(value_); }
  void set_type(Type type) {
    value_ = value_ & ~TypeField::mask();
    value_ = value_ | TypeField::encode(type);
  }

  void set_index(int new_index) {
    ASSERT(is_copy());
    value_ = value_ & ~DataField::mask();
    value_ = value_ | DataField::encode(new_index);
  }

  void set_reg(Register new_reg) {
    ASSERT(is_register());
    value_ = value_ & ~DataField::mask();
    value_ = value_ | DataField::encode(new_reg.code_);
  }

  // Encode type, copied, synced and data in one 32 bit integer.
  uint32_t value_;

  class TypeField: public BitField<Type, 0, 3> {};
  class CopiedField: public BitField<bool, 3, 1> {};
  class SyncedField: public BitField<bool, 4, 1> {};
  class NumberInfoField: public BitField<NumberInfo::Type, 5, 3> {};
  class DataField: public BitField<uint32_t, 8, 32 - 8> {};

  friend class VirtualFrame;
};

} }  // namespace v8::internal

#endif  // V8_FRAME_ELEMENT_H_
