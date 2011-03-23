// Copyright 2008 the V8 project authors. All rights reserved.
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

#ifndef V8_REGISTER_ALLOCATOR_H_
#define V8_REGISTER_ALLOCATOR_H_

#include "macro-assembler.h"
#include "type-info.h"

#if V8_TARGET_ARCH_IA32
#include "ia32/register-allocator-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "x64/register-allocator-x64.h"
#elif V8_TARGET_ARCH_ARM
#include "arm/register-allocator-arm.h"
#elif V8_TARGET_ARCH_MIPS
#include "mips/register-allocator-mips.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {


// -------------------------------------------------------------------------
// Results
//
// Results encapsulate the compile-time values manipulated by the code
// generator.  They can represent registers or constants.

class Result BASE_EMBEDDED {
 public:
  enum Type {
    INVALID,
    REGISTER,
    CONSTANT
  };

  // Construct an invalid result.
  Result() { invalidate(); }

  // Construct a register Result.
  explicit Result(Register reg, TypeInfo info = TypeInfo::Unknown());

  // Construct a Result whose value is a compile-time constant.
  explicit Result(Handle<Object> value) {
    ZoneObjectList* constant_list = Isolate::Current()->result_constant_list();
    TypeInfo info = TypeInfo::TypeFromValue(value);
    value_ = TypeField::encode(CONSTANT)
        | TypeInfoField::encode(info.ToInt())
        | IsUntaggedInt32Field::encode(false)
        | DataField::encode(constant_list->length());
    constant_list->Add(value);
  }

  // The copy constructor and assignment operators could each create a new
  // register reference.
  inline Result(const Result& other);

  inline Result& operator=(const Result& other);

  inline ~Result();

  inline void Unuse();

  Type type() const { return TypeField::decode(value_); }

  void invalidate() { value_ = TypeField::encode(INVALID); }

  inline TypeInfo type_info() const;
  inline void set_type_info(TypeInfo info);
  inline bool is_number() const;
  inline bool is_smi() const;
  inline bool is_integer32() const;
  inline bool is_double() const;

  bool is_valid() const { return type() != INVALID; }
  bool is_register() const { return type() == REGISTER; }
  bool is_constant() const { return type() == CONSTANT; }

  // An untagged int32 Result contains a signed int32 in a register
  // or as a constant.  These are only allowed in a side-effect-free
  // int32 calculation, and if a non-int32 input shows up or an overflow
  // occurs, we bail out and drop all the int32 values.  Constants are
  // not converted to int32 until they are loaded into a register.
  bool is_untagged_int32() const {
    return IsUntaggedInt32Field::decode(value_);
  }
  void set_untagged_int32(bool value) {
    value_ &= ~IsUntaggedInt32Field::mask();
    value_ |= IsUntaggedInt32Field::encode(value);
  }

  Register reg() const {
    ASSERT(is_register());
    uint32_t reg = DataField::decode(value_);
    Register result;
    result.code_ = reg;
    return result;
  }

  Handle<Object> handle() const {
    ASSERT(type() == CONSTANT);
    return Isolate::Current()->result_constant_list()->
        at(DataField::decode(value_));
  }

  // Move this result to an arbitrary register.  The register is not
  // necessarily spilled from the frame or even singly-referenced outside
  // it.
  void ToRegister();

  // Move this result to a specified register.  The register is spilled from
  // the frame, and the register is singly-referenced (by this result)
  // outside the frame.
  void ToRegister(Register reg);

 private:
  uint32_t value_;

  // Declare BitFields with template parameters <type, start, size>.
  class TypeField: public BitField<Type, 0, 2> {};
  class TypeInfoField : public BitField<int, 2, 6> {};
  class IsUntaggedInt32Field : public BitField<bool, 8, 1> {};
  class DataField: public BitField<uint32_t, 9, 32 - 9> {};

  inline void CopyTo(Result* destination) const;

  friend class CodeGeneratorScope;
};


// -------------------------------------------------------------------------
// Register file
//
// The register file tracks reference counts for the processor registers.
// It is used by both the register allocator and the virtual frame.

class RegisterFile BASE_EMBEDDED {
 public:
  RegisterFile() { Reset(); }

  void Reset() {
    for (int i = 0; i < kNumRegisters; i++) {
      ref_counts_[i] = 0;
    }
  }

  // Predicates and accessors for the reference counts.
  bool is_used(int num) {
    ASSERT(0 <= num && num < kNumRegisters);
    return ref_counts_[num] > 0;
  }

  int count(int num) {
    ASSERT(0 <= num && num < kNumRegisters);
    return ref_counts_[num];
  }

  // Record a use of a register by incrementing its reference count.
  void Use(int num) {
    ASSERT(0 <= num && num < kNumRegisters);
    ref_counts_[num]++;
  }

  // Record that a register will no longer be used by decrementing its
  // reference count.
  void Unuse(int num) {
    ASSERT(is_used(num));
    ref_counts_[num]--;
  }

  // Copy the reference counts from this register file to the other.
  void CopyTo(RegisterFile* other) {
    for (int i = 0; i < kNumRegisters; i++) {
      other->ref_counts_[i] = ref_counts_[i];
    }
  }

 private:
  // C++ doesn't like zero length arrays, so we make the array length 1 even if
  // we don't need it.
  static const int kNumRegisters =
      (RegisterAllocatorConstants::kNumRegisters == 0) ?
      1 : RegisterAllocatorConstants::kNumRegisters;

  int ref_counts_[kNumRegisters];

  // Very fast inlined loop to find a free register.  Used in
  // RegisterAllocator::AllocateWithoutSpilling.  Returns
  // kInvalidRegister if no free register found.
  int ScanForFreeRegister() {
    for (int i = 0; i < RegisterAllocatorConstants::kNumRegisters; i++) {
      if (!is_used(i)) return i;
    }
    return RegisterAllocatorConstants::kInvalidRegister;
  }

  friend class RegisterAllocator;
};


// -------------------------------------------------------------------------
// Register allocator
//

class RegisterAllocator BASE_EMBEDDED {
 public:
  static const int kNumRegisters =
      RegisterAllocatorConstants::kNumRegisters;
  static const int kInvalidRegister =
      RegisterAllocatorConstants::kInvalidRegister;

  explicit RegisterAllocator(CodeGenerator* cgen) : cgen_(cgen) {}

  // True if the register is reserved by the code generator, false if it
  // can be freely used by the allocator Defined in the
  // platform-specific XXX-inl.h files..
  static inline bool IsReserved(Register reg);

  // Convert between (unreserved) assembler registers and allocator
  // numbers.  Defined in the platform-specific XXX-inl.h files.
  static inline int ToNumber(Register reg);
  static inline Register ToRegister(int num);

  // Predicates and accessors for the registers' reference counts.
  bool is_used(int num) { return registers_.is_used(num); }
  inline bool is_used(Register reg);

  int count(int num) { return registers_.count(num); }
  inline int count(Register reg);

  // Explicitly record a reference to a register.
  void Use(int num) { registers_.Use(num); }
  inline void Use(Register reg);

  // Explicitly record that a register will no longer be used.
  void Unuse(int num) { registers_.Unuse(num); }
  inline void Unuse(Register reg);

  // Reset the register reference counts to free all non-reserved registers.
  void Reset() { registers_.Reset(); }

  // Initialize the register allocator for entry to a JS function.  On
  // entry, the (non-reserved) registers used by the JS calling
  // convention are referenced and the other (non-reserved) registers
  // are free.
  inline void Initialize();

  // Allocate a free register and return a register result if possible or
  // fail and return an invalid result.
  Result Allocate();

  // Allocate a specific register if possible, spilling it from the
  // current frame if necessary, or else fail and return an invalid
  // result.
  Result Allocate(Register target);

  // Allocate a free register without spilling any from the current
  // frame or fail and return an invalid result.
  Result AllocateWithoutSpilling();

  // Allocate a free byte register without spilling any from the current
  // frame or fail and return an invalid result.
  Result AllocateByteRegisterWithoutSpilling();

  // Copy the internal state to a register file, to be restored later by
  // RestoreFrom.
  void SaveTo(RegisterFile* register_file) {
    registers_.CopyTo(register_file);
  }

  // Restore the internal state.
  void RestoreFrom(RegisterFile* register_file) {
    register_file->CopyTo(&registers_);
  }

 private:
  CodeGenerator* cgen_;
  RegisterFile registers_;
};

} }  // namespace v8::internal

#endif  // V8_REGISTER_ALLOCATOR_H_
