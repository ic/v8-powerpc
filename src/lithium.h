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

#ifndef V8_LITHIUM_H_
#define V8_LITHIUM_H_

#include "hydrogen.h"
#include "safepoint-table.h"

namespace v8 {
namespace internal {

class LOperand: public ZoneObject {
 public:
  enum Kind {
    INVALID,
    UNALLOCATED,
    CONSTANT_OPERAND,
    STACK_SLOT,
    DOUBLE_STACK_SLOT,
    REGISTER,
    DOUBLE_REGISTER,
    ARGUMENT
  };

  LOperand() : value_(KindField::encode(INVALID)) { }

  Kind kind() const { return KindField::decode(value_); }
  int index() const { return static_cast<int>(value_) >> kKindFieldWidth; }
  bool IsConstantOperand() const { return kind() == CONSTANT_OPERAND; }
  bool IsStackSlot() const { return kind() == STACK_SLOT; }
  bool IsDoubleStackSlot() const { return kind() == DOUBLE_STACK_SLOT; }
  bool IsRegister() const { return kind() == REGISTER; }
  bool IsDoubleRegister() const { return kind() == DOUBLE_REGISTER; }
  bool IsArgument() const { return kind() == ARGUMENT; }
  bool IsUnallocated() const { return kind() == UNALLOCATED; }
  bool Equals(LOperand* other) const { return value_ == other->value_; }
  int VirtualRegister();

  void PrintTo(StringStream* stream);
  void ConvertTo(Kind kind, int index) {
    value_ = KindField::encode(kind);
    value_ |= index << kKindFieldWidth;
    ASSERT(this->index() == index);
  }

 protected:
  static const int kKindFieldWidth = 3;
  class KindField : public BitField<Kind, 0, kKindFieldWidth> { };

  LOperand(Kind kind, int index) { ConvertTo(kind, index); }

  unsigned value_;
};


class LUnallocated: public LOperand {
 public:
  enum Policy {
    NONE,
    ANY,
    FIXED_REGISTER,
    FIXED_DOUBLE_REGISTER,
    FIXED_SLOT,
    MUST_HAVE_REGISTER,
    WRITABLE_REGISTER,
    SAME_AS_FIRST_INPUT,
    IGNORE
  };

  // Lifetime of operand inside the instruction.
  enum Lifetime {
    // USED_AT_START operand is guaranteed to be live only at
    // instruction start. Register allocator is free to assign the same register
    // to some other operand used inside instruction (i.e. temporary or
    // output).
    USED_AT_START,

    // USED_AT_END operand is treated as live until the end of
    // instruction. This means that register allocator will not reuse it's
    // register for any other operand inside instruction.
    USED_AT_END
  };

  explicit LUnallocated(Policy policy) : LOperand(UNALLOCATED, 0) {
    Initialize(policy, 0, USED_AT_END);
  }

  LUnallocated(Policy policy, int fixed_index) : LOperand(UNALLOCATED, 0) {
    Initialize(policy, fixed_index, USED_AT_END);
  }

  LUnallocated(Policy policy, Lifetime lifetime) : LOperand(UNALLOCATED, 0) {
    Initialize(policy, 0, lifetime);
  }

  // The superclass has a KindField.  Some policies have a signed fixed
  // index in the upper bits.
  static const int kPolicyWidth = 4;
  static const int kLifetimeWidth = 1;
  static const int kVirtualRegisterWidth = 17;

  static const int kPolicyShift = kKindFieldWidth;
  static const int kLifetimeShift = kPolicyShift + kPolicyWidth;
  static const int kVirtualRegisterShift = kLifetimeShift + kLifetimeWidth;
  static const int kFixedIndexShift =
      kVirtualRegisterShift + kVirtualRegisterWidth;

  class PolicyField : public BitField<Policy, kPolicyShift, kPolicyWidth> { };

  class LifetimeField
      : public BitField<Lifetime, kLifetimeShift, kLifetimeWidth> {
  };

  class VirtualRegisterField
      : public BitField<unsigned,
                        kVirtualRegisterShift,
                        kVirtualRegisterWidth> {
  };

  static const int kMaxVirtualRegisters = 1 << (kVirtualRegisterWidth + 1);
  static const int kMaxFixedIndices = 128;

  bool HasIgnorePolicy() const { return policy() == IGNORE; }
  bool HasNoPolicy() const { return policy() == NONE; }
  bool HasAnyPolicy() const {
    return policy() == ANY;
  }
  bool HasFixedPolicy() const {
    return policy() == FIXED_REGISTER ||
        policy() == FIXED_DOUBLE_REGISTER ||
        policy() == FIXED_SLOT;
  }
  bool HasRegisterPolicy() const {
    return policy() == WRITABLE_REGISTER || policy() == MUST_HAVE_REGISTER;
  }
  bool HasSameAsInputPolicy() const {
    return policy() == SAME_AS_FIRST_INPUT;
  }
  Policy policy() const { return PolicyField::decode(value_); }
  void set_policy(Policy policy) {
    value_ &= ~PolicyField::mask();
    value_ |= PolicyField::encode(policy);
  }
  int fixed_index() const {
    return static_cast<int>(value_) >> kFixedIndexShift;
  }

  unsigned virtual_register() const {
    return VirtualRegisterField::decode(value_);
  }

  void set_virtual_register(unsigned id) {
    value_ &= ~VirtualRegisterField::mask();
    value_ |= VirtualRegisterField::encode(id);
  }

  LUnallocated* CopyUnconstrained() {
    LUnallocated* result = new LUnallocated(ANY);
    result->set_virtual_register(virtual_register());
    return result;
  }

  static LUnallocated* cast(LOperand* op) {
    ASSERT(op->IsUnallocated());
    return reinterpret_cast<LUnallocated*>(op);
  }

  bool IsUsedAtStart() {
    return LifetimeField::decode(value_) == USED_AT_START;
  }

 private:
  void Initialize(Policy policy, int fixed_index, Lifetime lifetime) {
    value_ |= PolicyField::encode(policy);
    value_ |= LifetimeField::encode(lifetime);
    value_ |= fixed_index << kFixedIndexShift;
    ASSERT(this->fixed_index() == fixed_index);
  }
};


class LMoveOperands BASE_EMBEDDED {
 public:
  LMoveOperands(LOperand* source, LOperand* destination)
      : source_(source), destination_(destination) {
  }

  LOperand* source() const { return source_; }
  void set_source(LOperand* operand) { source_ = operand; }

  LOperand* destination() const { return destination_; }
  void set_destination(LOperand* operand) { destination_ = operand; }

  // The gap resolver marks moves as "in-progress" by clearing the
  // destination (but not the source).
  bool IsPending() const {
    return destination_ == NULL && source_ != NULL;
  }

  // True if this move a move into the given destination operand.
  bool Blocks(LOperand* operand) const {
    return !IsEliminated() && source()->Equals(operand);
  }

  // A move is redundant if it's been eliminated, if its source and
  // destination are the same, or if its destination is unneeded.
  bool IsRedundant() const {
    return IsEliminated() || source_->Equals(destination_) || IsIgnored();
  }

  bool IsIgnored() const {
    return destination_ != NULL &&
        destination_->IsUnallocated() &&
        LUnallocated::cast(destination_)->HasIgnorePolicy();
  }

  // We clear both operands to indicate move that's been eliminated.
  void Eliminate() { source_ = destination_ = NULL; }
  bool IsEliminated() const {
    ASSERT(source_ != NULL || destination_ == NULL);
    return source_ == NULL;
  }

 private:
  LOperand* source_;
  LOperand* destination_;
};


class LConstantOperand: public LOperand {
 public:
  static LConstantOperand* Create(int index) {
    ASSERT(index >= 0);
    if (index < kNumCachedOperands) return &cache[index];
    return new LConstantOperand(index);
  }

  static LConstantOperand* cast(LOperand* op) {
    ASSERT(op->IsConstantOperand());
    return reinterpret_cast<LConstantOperand*>(op);
  }

  static void SetupCache();

 private:
  static const int kNumCachedOperands = 128;
  static LConstantOperand cache[];

  LConstantOperand() : LOperand() { }
  explicit LConstantOperand(int index) : LOperand(CONSTANT_OPERAND, index) { }
};


class LArgument: public LOperand {
 public:
  explicit LArgument(int index) : LOperand(ARGUMENT, index) { }

  static LArgument* cast(LOperand* op) {
    ASSERT(op->IsArgument());
    return reinterpret_cast<LArgument*>(op);
  }
};


class LStackSlot: public LOperand {
 public:
  static LStackSlot* Create(int index) {
    ASSERT(index >= 0);
    if (index < kNumCachedOperands) return &cache[index];
    return new LStackSlot(index);
  }

  static LStackSlot* cast(LOperand* op) {
    ASSERT(op->IsStackSlot());
    return reinterpret_cast<LStackSlot*>(op);
  }

  static void SetupCache();

 private:
  static const int kNumCachedOperands = 128;
  static LStackSlot cache[];

  LStackSlot() : LOperand() { }
  explicit LStackSlot(int index) : LOperand(STACK_SLOT, index) { }
};


class LDoubleStackSlot: public LOperand {
 public:
  static LDoubleStackSlot* Create(int index) {
    ASSERT(index >= 0);
    if (index < kNumCachedOperands) return &cache[index];
    return new LDoubleStackSlot(index);
  }

  static LDoubleStackSlot* cast(LOperand* op) {
    ASSERT(op->IsStackSlot());
    return reinterpret_cast<LDoubleStackSlot*>(op);
  }

  static void SetupCache();

 private:
  static const int kNumCachedOperands = 128;
  static LDoubleStackSlot cache[];

  LDoubleStackSlot() : LOperand() { }
  explicit LDoubleStackSlot(int index) : LOperand(DOUBLE_STACK_SLOT, index) { }
};


class LRegister: public LOperand {
 public:
  static LRegister* Create(int index) {
    ASSERT(index >= 0);
    if (index < kNumCachedOperands) return &cache[index];
    return new LRegister(index);
  }

  static LRegister* cast(LOperand* op) {
    ASSERT(op->IsRegister());
    return reinterpret_cast<LRegister*>(op);
  }

  static void SetupCache();

 private:
  static const int kNumCachedOperands = 16;
  static LRegister cache[];

  LRegister() : LOperand() { }
  explicit LRegister(int index) : LOperand(REGISTER, index) { }
};


class LDoubleRegister: public LOperand {
 public:
  static LDoubleRegister* Create(int index) {
    ASSERT(index >= 0);
    if (index < kNumCachedOperands) return &cache[index];
    return new LDoubleRegister(index);
  }

  static LDoubleRegister* cast(LOperand* op) {
    ASSERT(op->IsDoubleRegister());
    return reinterpret_cast<LDoubleRegister*>(op);
  }

  static void SetupCache();

 private:
  static const int kNumCachedOperands = 16;
  static LDoubleRegister cache[];

  LDoubleRegister() : LOperand() { }
  explicit LDoubleRegister(int index) : LOperand(DOUBLE_REGISTER, index) { }
};


class LParallelMove : public ZoneObject {
 public:
  LParallelMove() : move_operands_(4) { }

  void AddMove(LOperand* from, LOperand* to) {
    move_operands_.Add(LMoveOperands(from, to));
  }

  bool IsRedundant() const;

  const ZoneList<LMoveOperands>* move_operands() const {
    return &move_operands_;
  }

  void PrintDataTo(StringStream* stream) const;

 private:
  ZoneList<LMoveOperands> move_operands_;
};


class LPointerMap: public ZoneObject {
 public:
  explicit LPointerMap(int position)
      : pointer_operands_(8), position_(position), lithium_position_(-1) { }

  const ZoneList<LOperand*>* operands() const { return &pointer_operands_; }
  int position() const { return position_; }
  int lithium_position() const { return lithium_position_; }

  void set_lithium_position(int pos) {
    ASSERT(lithium_position_ == -1);
    lithium_position_ = pos;
  }

  void RecordPointer(LOperand* op);
  void PrintTo(StringStream* stream);

 private:
  ZoneList<LOperand*> pointer_operands_;
  int position_;
  int lithium_position_;
};


class LEnvironment: public ZoneObject {
 public:
  LEnvironment(Handle<JSFunction> closure,
               int ast_id,
               int parameter_count,
               int argument_count,
               int value_count,
               LEnvironment* outer)
      : closure_(closure),
        arguments_stack_height_(argument_count),
        deoptimization_index_(Safepoint::kNoDeoptimizationIndex),
        translation_index_(-1),
        ast_id_(ast_id),
        parameter_count_(parameter_count),
        values_(value_count),
        representations_(value_count),
        spilled_registers_(NULL),
        spilled_double_registers_(NULL),
        outer_(outer) {
  }

  Handle<JSFunction> closure() const { return closure_; }
  int arguments_stack_height() const { return arguments_stack_height_; }
  int deoptimization_index() const { return deoptimization_index_; }
  int translation_index() const { return translation_index_; }
  int ast_id() const { return ast_id_; }
  int parameter_count() const { return parameter_count_; }
  LOperand** spilled_registers() const { return spilled_registers_; }
  LOperand** spilled_double_registers() const {
    return spilled_double_registers_;
  }
  const ZoneList<LOperand*>* values() const { return &values_; }
  LEnvironment* outer() const { return outer_; }

  void AddValue(LOperand* operand, Representation representation) {
    values_.Add(operand);
    representations_.Add(representation);
  }

  bool HasTaggedValueAt(int index) const {
    return representations_[index].IsTagged();
  }

  void Register(int deoptimization_index, int translation_index) {
    ASSERT(!HasBeenRegistered());
    deoptimization_index_ = deoptimization_index;
    translation_index_ = translation_index;
  }
  bool HasBeenRegistered() const {
    return deoptimization_index_ != Safepoint::kNoDeoptimizationIndex;
  }

  void SetSpilledRegisters(LOperand** registers,
                           LOperand** double_registers) {
    spilled_registers_ = registers;
    spilled_double_registers_ = double_registers;
  }

  void PrintTo(StringStream* stream);

 private:
  Handle<JSFunction> closure_;
  int arguments_stack_height_;
  int deoptimization_index_;
  int translation_index_;
  int ast_id_;
  int parameter_count_;
  ZoneList<LOperand*> values_;
  ZoneList<Representation> representations_;

  // Allocation index indexed arrays of spill slot operands for registers
  // that are also in spill slots at an OSR entry.  NULL for environments
  // that do not correspond to an OSR entry.
  LOperand** spilled_registers_;
  LOperand** spilled_double_registers_;

  LEnvironment* outer_;

  friend class LCodegen;
};


// Iterates over the non-null, non-constant operands in an environment.
class ShallowIterator BASE_EMBEDDED {
 public:
  explicit ShallowIterator(LEnvironment* env)
      : env_(env),
        limit_(env != NULL ? env->values()->length() : 0),
        current_(0) {
    current_ = AdvanceToNext(0);
  }

  inline bool HasNext() {
    return env_ != NULL && current_ < limit_;
  }

  inline LOperand* Next() {
    ASSERT(HasNext());
    return env_->values()->at(current_);
  }

  inline void Advance() {
    current_ = AdvanceToNext(current_ + 1);
  }

  inline LEnvironment* env() { return env_; }

 private:
  inline bool ShouldSkip(LOperand* op) {
    return op == NULL || op->IsConstantOperand() || op->IsArgument();
  }

  inline int AdvanceToNext(int start) {
    while (start < limit_ && ShouldSkip(env_->values()->at(start))) {
      start++;
    }
    return start;
  }

  LEnvironment* env_;
  int limit_;
  int current_;
};


// Iterator for non-null, non-constant operands incl. outer environments.
class DeepIterator BASE_EMBEDDED {
 public:
  explicit DeepIterator(LEnvironment* env)
      : current_iterator_(env) { }

  inline bool HasNext() {
    if (current_iterator_.HasNext()) return true;
    if (current_iterator_.env() == NULL) return false;
    AdvanceToOuter();
    return current_iterator_.HasNext();
  }

  inline LOperand* Next() {
    ASSERT(current_iterator_.HasNext());
    return current_iterator_.Next();
  }

  inline void Advance() {
    if (current_iterator_.HasNext()) {
      current_iterator_.Advance();
    } else {
      AdvanceToOuter();
    }
  }

 private:
  inline void AdvanceToOuter() {
    current_iterator_ = ShallowIterator(current_iterator_.env()->outer());
  }

  ShallowIterator current_iterator_;
};

} }  // namespace v8::internal

#endif  // V8_LITHIUM_H_
