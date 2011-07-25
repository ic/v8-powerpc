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

#include "factory.h"
#include "hydrogen.h"

#if V8_TARGET_ARCH_IA32
#include "ia32/lithium-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "x64/lithium-x64.h"
#elif V8_TARGET_ARCH_ARM
#include "arm/lithium-arm.h"
#elif V8_TARGET_ARCH_MIPS
#include "mips/lithium-mips.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {

#define DEFINE_COMPILE(type)                                         \
  LInstruction* H##type::CompileToLithium(LChunkBuilder* builder) {  \
    return builder->Do##type(this);                                  \
  }
HYDROGEN_CONCRETE_INSTRUCTION_LIST(DEFINE_COMPILE)
#undef DEFINE_COMPILE


const char* Representation::Mnemonic() const {
  switch (kind_) {
    case kNone: return "v";
    case kTagged: return "t";
    case kDouble: return "d";
    case kInteger32: return "i";
    case kExternal: return "x";
    default:
      UNREACHABLE();
      return NULL;
  }
}


void HValue::AssumeRepresentation(Representation r) {
  if (CheckFlag(kFlexibleRepresentation)) {
    ChangeRepresentation(r);
    // The representation of the value is dictated by type feedback and
    // will not be changed later.
    ClearFlag(kFlexibleRepresentation);
  }
}


static int32_t ConvertAndSetOverflow(int64_t result, bool* overflow) {
  if (result > kMaxInt) {
    *overflow = true;
    return kMaxInt;
  }
  if (result < kMinInt) {
    *overflow = true;
    return kMinInt;
  }
  return static_cast<int32_t>(result);
}


static int32_t AddWithoutOverflow(int32_t a, int32_t b, bool* overflow) {
  int64_t result = static_cast<int64_t>(a) + static_cast<int64_t>(b);
  return ConvertAndSetOverflow(result, overflow);
}


static int32_t SubWithoutOverflow(int32_t a, int32_t b, bool* overflow) {
  int64_t result = static_cast<int64_t>(a) - static_cast<int64_t>(b);
  return ConvertAndSetOverflow(result, overflow);
}


static int32_t MulWithoutOverflow(int32_t a, int32_t b, bool* overflow) {
  int64_t result = static_cast<int64_t>(a) * static_cast<int64_t>(b);
  return ConvertAndSetOverflow(result, overflow);
}


int32_t Range::Mask() const {
  if (lower_ == upper_) return lower_;
  if (lower_ >= 0) {
    int32_t res = 1;
    while (res < upper_) {
      res = (res << 1) | 1;
    }
    return res;
  }
  return 0xffffffff;
}


void Range::AddConstant(int32_t value) {
  if (value == 0) return;
  bool may_overflow = false;  // Overflow is ignored here.
  lower_ = AddWithoutOverflow(lower_, value, &may_overflow);
  upper_ = AddWithoutOverflow(upper_, value, &may_overflow);
  Verify();
}


void Range::Intersect(Range* other) {
  upper_ = Min(upper_, other->upper_);
  lower_ = Max(lower_, other->lower_);
  bool b = CanBeMinusZero() && other->CanBeMinusZero();
  set_can_be_minus_zero(b);
}


void Range::Union(Range* other) {
  upper_ = Max(upper_, other->upper_);
  lower_ = Min(lower_, other->lower_);
  bool b = CanBeMinusZero() || other->CanBeMinusZero();
  set_can_be_minus_zero(b);
}


void Range::Sar(int32_t value) {
  int32_t bits = value & 0x1F;
  lower_ = lower_ >> bits;
  upper_ = upper_ >> bits;
  set_can_be_minus_zero(false);
}


void Range::Shl(int32_t value) {
  int32_t bits = value & 0x1F;
  int old_lower = lower_;
  int old_upper = upper_;
  lower_ = lower_ << bits;
  upper_ = upper_ << bits;
  if (old_lower != lower_ >> bits || old_upper != upper_ >> bits) {
    upper_ = kMaxInt;
    lower_ = kMinInt;
  }
  set_can_be_minus_zero(false);
}


bool Range::AddAndCheckOverflow(Range* other) {
  bool may_overflow = false;
  lower_ = AddWithoutOverflow(lower_, other->lower(), &may_overflow);
  upper_ = AddWithoutOverflow(upper_, other->upper(), &may_overflow);
  KeepOrder();
  Verify();
  return may_overflow;
}


bool Range::SubAndCheckOverflow(Range* other) {
  bool may_overflow = false;
  lower_ = SubWithoutOverflow(lower_, other->upper(), &may_overflow);
  upper_ = SubWithoutOverflow(upper_, other->lower(), &may_overflow);
  KeepOrder();
  Verify();
  return may_overflow;
}


void Range::KeepOrder() {
  if (lower_ > upper_) {
    int32_t tmp = lower_;
    lower_ = upper_;
    upper_ = tmp;
  }
}


void Range::Verify() const {
  ASSERT(lower_ <= upper_);
}


bool Range::MulAndCheckOverflow(Range* other) {
  bool may_overflow = false;
  int v1 = MulWithoutOverflow(lower_, other->lower(), &may_overflow);
  int v2 = MulWithoutOverflow(lower_, other->upper(), &may_overflow);
  int v3 = MulWithoutOverflow(upper_, other->lower(), &may_overflow);
  int v4 = MulWithoutOverflow(upper_, other->upper(), &may_overflow);
  lower_ = Min(Min(v1, v2), Min(v3, v4));
  upper_ = Max(Max(v1, v2), Max(v3, v4));
  Verify();
  return may_overflow;
}


const char* HType::ToString() {
  switch (type_) {
    case kTagged: return "tagged";
    case kTaggedPrimitive: return "primitive";
    case kTaggedNumber: return "number";
    case kSmi: return "smi";
    case kHeapNumber: return "heap-number";
    case kString: return "string";
    case kBoolean: return "boolean";
    case kNonPrimitive: return "non-primitive";
    case kJSArray: return "array";
    case kJSObject: return "object";
    case kUninitialized: return "uninitialized";
  }
  UNREACHABLE();
  return "Unreachable code";
}


const char* HType::ToShortString() {
  switch (type_) {
    case kTagged: return "t";
    case kTaggedPrimitive: return "p";
    case kTaggedNumber: return "n";
    case kSmi: return "m";
    case kHeapNumber: return "h";
    case kString: return "s";
    case kBoolean: return "b";
    case kNonPrimitive: return "r";
    case kJSArray: return "a";
    case kJSObject: return "o";
    case kUninitialized: return "z";
  }
  UNREACHABLE();
  return "Unreachable code";
}


HType HType::TypeFromValue(Handle<Object> value) {
  HType result = HType::Tagged();
  if (value->IsSmi()) {
    result = HType::Smi();
  } else if (value->IsHeapNumber()) {
    result = HType::HeapNumber();
  } else if (value->IsString()) {
    result = HType::String();
  } else if (value->IsBoolean()) {
    result = HType::Boolean();
  } else if (value->IsJSObject()) {
    result = HType::JSObject();
  } else if (value->IsJSArray()) {
    result = HType::JSArray();
  }
  return result;
}


bool HValue::IsDefinedAfter(HBasicBlock* other) const {
  return block()->block_id() > other->block_id();
}


HUseIterator::HUseIterator(HUseListNode* head) : next_(head) {
  Advance();
}


void HUseIterator::Advance() {
  current_ = next_;
  if (current_ != NULL) {
    next_ = current_->tail();
    value_ = current_->value();
    index_ = current_->index();
  }
}


int HValue::UseCount() const {
  int count = 0;
  for (HUseIterator it(uses()); !it.Done(); it.Advance()) ++count;
  return count;
}


HUseListNode* HValue::RemoveUse(HValue* value, int index) {
  HUseListNode* previous = NULL;
  HUseListNode* current = use_list_;
  while (current != NULL) {
    if (current->value() == value && current->index() == index) {
      if (previous == NULL) {
        use_list_ = current->tail();
      } else {
        previous->set_tail(current->tail());
      }
      break;
    }

    previous = current;
    current = current->tail();
  }

#ifdef DEBUG
  // Do not reuse use list nodes in debug mode, zap them.
  if (current != NULL) {
    HUseListNode* temp =
        new HUseListNode(current->value(), current->index(), NULL);
    current->Zap();
    current = temp;
  }
#endif
  return current;
}


bool HValue::Equals(HValue* other) {
  if (other->opcode() != opcode()) return false;
  if (!other->representation().Equals(representation())) return false;
  if (!other->type_.Equals(type_)) return false;
  if (other->flags() != flags()) return false;
  if (OperandCount() != other->OperandCount()) return false;
  for (int i = 0; i < OperandCount(); ++i) {
    if (OperandAt(i)->id() != other->OperandAt(i)->id()) return false;
  }
  bool result = DataEquals(other);
  ASSERT(!result || Hashcode() == other->Hashcode());
  return result;
}


intptr_t HValue::Hashcode() {
  intptr_t result = opcode();
  int count = OperandCount();
  for (int i = 0; i < count; ++i) {
    result = result * 19 + OperandAt(i)->id() + (result >> 7);
  }
  return result;
}


const char* HValue::Mnemonic() const {
  switch (opcode()) {
#define MAKE_CASE(type) case k##type: return #type;
    HYDROGEN_CONCRETE_INSTRUCTION_LIST(MAKE_CASE)
#undef MAKE_CASE
    case kPhi: return "Phi";
    default: return "";
  }
}


void HValue::SetOperandAt(int index, HValue* value) {
  RegisterUse(index, value);
  InternalSetOperandAt(index, value);
}


void HValue::DeleteAndReplaceWith(HValue* other) {
  // We replace all uses first, so Delete can assert that there are none.
  if (other != NULL) ReplaceAllUsesWith(other);
  ASSERT(HasNoUses());
  ClearOperands();
  DeleteFromGraph();
}


void HValue::ReplaceAllUsesWith(HValue* other) {
  while (use_list_ != NULL) {
    HUseListNode* list_node = use_list_;
    HValue* value = list_node->value();
    ASSERT(!value->block()->IsStartBlock());
    value->InternalSetOperandAt(list_node->index(), other);
    use_list_ = list_node->tail();
    list_node->set_tail(other->use_list_);
    other->use_list_ = list_node;
  }
}


void HValue::ClearOperands() {
  for (int i = 0; i < OperandCount(); ++i) {
    SetOperandAt(i, NULL);
  }
}


void HValue::SetBlock(HBasicBlock* block) {
  ASSERT(block_ == NULL || block == NULL);
  block_ = block;
  if (id_ == kNoNumber && block != NULL) {
    id_ = block->graph()->GetNextValueID(this);
  }
}


void HValue::PrintTypeTo(StringStream* stream) {
  if (!representation().IsTagged() || type().Equals(HType::Tagged())) return;
  stream->Add(" type[%s]", type().ToString());
}


void HValue::PrintRangeTo(StringStream* stream) {
  if (range() == NULL || range()->IsMostGeneric()) return;
  stream->Add(" range[%d,%d,m0=%d]",
              range()->lower(),
              range()->upper(),
              static_cast<int>(range()->CanBeMinusZero()));
}


void HValue::PrintChangesTo(StringStream* stream) {
  int changes_flags = (flags() & HValue::ChangesFlagsMask());
  if (changes_flags == 0) return;
  stream->Add(" changes[");
  if (changes_flags == AllSideEffects()) {
    stream->Add("*");
  } else {
    bool add_comma = false;
#define PRINT_DO(type)                         \
    if (changes_flags & (1 << kChanges##type)) { \
      if (add_comma) stream->Add(",");           \
      add_comma = true;                          \
      stream->Add(#type);                        \
    }
    GVN_FLAG_LIST(PRINT_DO);
#undef PRINT_DO
  }
  stream->Add("]");
}


void HValue::PrintNameTo(StringStream* stream) {
  stream->Add("%s%d", representation_.Mnemonic(), id());
}


bool HValue::UpdateInferredType() {
  HType type = CalculateInferredType();
  bool result = (!type.Equals(type_));
  type_ = type;
  return result;
}


void HValue::RegisterUse(int index, HValue* new_value) {
  HValue* old_value = OperandAt(index);
  if (old_value == new_value) return;

  HUseListNode* removed = NULL;
  if (old_value != NULL) {
    removed = old_value->RemoveUse(this, index);
  }

  if (new_value != NULL) {
    if (removed == NULL) {
      new_value->use_list_ =
          new HUseListNode(this, index, new_value->use_list_);
    } else {
      removed->set_tail(new_value->use_list_);
      new_value->use_list_ = removed;
    }
  }
}


void HValue::AddNewRange(Range* r) {
  if (!HasRange()) ComputeInitialRange();
  if (!HasRange()) range_ = new Range();
  ASSERT(HasRange());
  r->StackUpon(range_);
  range_ = r;
}


void HValue::RemoveLastAddedRange() {
  ASSERT(HasRange());
  ASSERT(range_->next() != NULL);
  range_ = range_->next();
}


void HValue::ComputeInitialRange() {
  ASSERT(!HasRange());
  range_ = InferRange();
  ASSERT(HasRange());
}


void HInstruction::PrintTo(StringStream* stream) {
  PrintMnemonicTo(stream);
  PrintDataTo(stream);
  PrintRangeTo(stream);
  PrintChangesTo(stream);
  PrintTypeTo(stream);
}


void HInstruction::PrintMnemonicTo(StringStream* stream) {
  stream->Add("%s", Mnemonic());
  if (HasSideEffects()) stream->Add("*");
  stream->Add(" ");
}


void HInstruction::Unlink() {
  ASSERT(IsLinked());
  ASSERT(!IsControlInstruction());  // Must never move control instructions.
  ASSERT(!IsBlockEntry());  // Doesn't make sense to delete these.
  ASSERT(previous_ != NULL);
  previous_->next_ = next_;
  if (next_ == NULL) {
    ASSERT(block()->last() == this);
    block()->set_last(previous_);
  } else {
    next_->previous_ = previous_;
  }
  clear_block();
}


void HInstruction::InsertBefore(HInstruction* next) {
  ASSERT(!IsLinked());
  ASSERT(!next->IsBlockEntry());
  ASSERT(!IsControlInstruction());
  ASSERT(!next->block()->IsStartBlock());
  ASSERT(next->previous_ != NULL);
  HInstruction* prev = next->previous();
  prev->next_ = this;
  next->previous_ = this;
  next_ = next;
  previous_ = prev;
  SetBlock(next->block());
}


void HInstruction::InsertAfter(HInstruction* previous) {
  ASSERT(!IsLinked());
  ASSERT(!previous->IsControlInstruction());
  ASSERT(!IsControlInstruction() || previous->next_ == NULL);
  HBasicBlock* block = previous->block();
  // Never insert anything except constants into the start block after finishing
  // it.
  if (block->IsStartBlock() && block->IsFinished() && !IsConstant()) {
    ASSERT(block->end()->SecondSuccessor() == NULL);
    InsertAfter(block->end()->FirstSuccessor()->first());
    return;
  }

  // If we're inserting after an instruction with side-effects that is
  // followed by a simulate instruction, we need to insert after the
  // simulate instruction instead.
  HInstruction* next = previous->next_;
  if (previous->HasSideEffects() && next != NULL) {
    ASSERT(next->IsSimulate());
    previous = next;
    next = previous->next_;
  }

  previous_ = previous;
  next_ = next;
  SetBlock(block);
  previous->next_ = this;
  if (next != NULL) next->previous_ = this;
}


#ifdef DEBUG
void HInstruction::Verify() {
  // Verify that input operands are defined before use.
  HBasicBlock* cur_block = block();
  for (int i = 0; i < OperandCount(); ++i) {
    HValue* other_operand = OperandAt(i);
    HBasicBlock* other_block = other_operand->block();
    if (cur_block == other_block) {
      if (!other_operand->IsPhi()) {
        HInstruction* cur = cur_block->first();
        while (cur != NULL) {
          ASSERT(cur != this);  // We should reach other_operand before!
          if (cur == other_operand) break;
          cur = cur->next();
        }
        // Must reach other operand in the same block!
        ASSERT(cur == other_operand);
      }
    } else {
      // If the following assert fires, you may have forgotten an
      // AddInstruction.
      ASSERT(other_block->Dominates(cur_block));
    }
  }

  // Verify that instructions that may have side-effects are followed
  // by a simulate instruction.
  if (HasSideEffects() && !IsOsrEntry()) {
    ASSERT(next()->IsSimulate());
  }

  // Verify that instructions that can be eliminated by GVN have overridden
  // HValue::DataEquals.  The default implementation is UNREACHABLE.  We
  // don't actually care whether DataEquals returns true or false here.
  if (CheckFlag(kUseGVN)) DataEquals(this);
}
#endif


void HUnaryCall::PrintDataTo(StringStream* stream) {
  value()->PrintNameTo(stream);
  stream->Add(" ");
  stream->Add("#%d", argument_count());
}


void HBinaryCall::PrintDataTo(StringStream* stream) {
  first()->PrintNameTo(stream);
  stream->Add(" ");
  second()->PrintNameTo(stream);
  stream->Add(" ");
  stream->Add("#%d", argument_count());
}


void HCallConstantFunction::PrintDataTo(StringStream* stream) {
  if (IsApplyFunction()) {
    stream->Add("optimized apply ");
  } else {
    stream->Add("%o ", function()->shared()->DebugName());
  }
  stream->Add("#%d", argument_count());
}


void HCallNamed::PrintDataTo(StringStream* stream) {
  stream->Add("%o ", *name());
  HUnaryCall::PrintDataTo(stream);
}


void HCallGlobal::PrintDataTo(StringStream* stream) {
  stream->Add("%o ", *name());
  HUnaryCall::PrintDataTo(stream);
}


void HCallKnownGlobal::PrintDataTo(StringStream* stream) {
  stream->Add("o ", target()->shared()->DebugName());
  stream->Add("#%d", argument_count());
}


void HCallRuntime::PrintDataTo(StringStream* stream) {
  stream->Add("%o ", *name());
  stream->Add("#%d", argument_count());
}


void HClassOfTestAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("class_of_test(");
  value()->PrintNameTo(stream);
  stream->Add(", \"%o\")", *class_name());
}


void HAccessArgumentsAt::PrintDataTo(StringStream* stream) {
  arguments()->PrintNameTo(stream);
  stream->Add("[");
  index()->PrintNameTo(stream);
  stream->Add("], length ");
  length()->PrintNameTo(stream);
}


void HControlInstruction::PrintDataTo(StringStream* stream) {
  stream->Add(" goto (");
  bool first_block = true;
  for (HSuccessorIterator it(this); !it.Done(); it.Advance()) {
    stream->Add(first_block ? "B%d" : ", B%d", it.Current()->block_id());
    first_block = false;
  }
  stream->Add(")");
}


void HUnaryControlInstruction::PrintDataTo(StringStream* stream) {
  value()->PrintNameTo(stream);
  HControlInstruction::PrintDataTo(stream);
}


void HReturn::PrintDataTo(StringStream* stream) {
  value()->PrintNameTo(stream);
}


void HCompareMap::PrintDataTo(StringStream* stream) {
  value()->PrintNameTo(stream);
  stream->Add(" (%p)", *map());
  HControlInstruction::PrintDataTo(stream);
}


const char* HUnaryMathOperation::OpName() const {
  switch (op()) {
    case kMathFloor: return "floor";
    case kMathRound: return "round";
    case kMathCeil: return "ceil";
    case kMathAbs: return "abs";
    case kMathLog: return "log";
    case kMathSin: return "sin";
    case kMathCos: return "cos";
    case kMathTan: return "tan";
    case kMathASin: return "asin";
    case kMathACos: return "acos";
    case kMathATan: return "atan";
    case kMathExp: return "exp";
    case kMathSqrt: return "sqrt";
    default: break;
  }
  return "(unknown operation)";
}


void HUnaryMathOperation::PrintDataTo(StringStream* stream) {
  const char* name = OpName();
  stream->Add("%s ", name);
  value()->PrintNameTo(stream);
}


void HUnaryOperation::PrintDataTo(StringStream* stream) {
  value()->PrintNameTo(stream);
}


void HHasInstanceTypeAndBranch::PrintDataTo(StringStream* stream) {
  value()->PrintNameTo(stream);
  switch (from_) {
    case FIRST_JS_RECEIVER_TYPE:
      if (to_ == LAST_TYPE) stream->Add(" spec_object");
      break;
    case JS_REGEXP_TYPE:
      if (to_ == JS_REGEXP_TYPE) stream->Add(" reg_exp");
      break;
    case JS_ARRAY_TYPE:
      if (to_ == JS_ARRAY_TYPE) stream->Add(" array");
      break;
    case JS_FUNCTION_TYPE:
      if (to_ == JS_FUNCTION_TYPE) stream->Add(" function");
      break;
    default:
      break;
  }
}


void HTypeofIsAndBranch::PrintDataTo(StringStream* stream) {
  value()->PrintNameTo(stream);
  stream->Add(" == ");
  stream->Add(type_literal_->ToAsciiVector());
}


void HChange::PrintDataTo(StringStream* stream) {
  HUnaryOperation::PrintDataTo(stream);
  stream->Add(" %s to %s", from_.Mnemonic(), to().Mnemonic());

  if (CanTruncateToInt32()) stream->Add(" truncating-int32");
  if (CheckFlag(kBailoutOnMinusZero)) stream->Add(" -0?");
}


HValue* HCheckInstanceType::Canonicalize() {
  if (check_ == IS_STRING &&
      !value()->type().IsUninitialized() &&
      value()->type().IsString()) {
    return NULL;
  }
  if (check_ == IS_SYMBOL &&
      value()->IsConstant() &&
      HConstant::cast(value())->handle()->IsSymbol()) {
    return NULL;
  }
  return this;
}


void HCheckInstanceType::GetCheckInterval(InstanceType* first,
                                          InstanceType* last) {
  ASSERT(is_interval_check());
  switch (check_) {
    case IS_SPEC_OBJECT:
      *first = FIRST_SPEC_OBJECT_TYPE;
      *last = LAST_SPEC_OBJECT_TYPE;
      return;
    case IS_JS_ARRAY:
      *first = *last = JS_ARRAY_TYPE;
      return;
    default:
      UNREACHABLE();
  }
}


void HCheckInstanceType::GetCheckMaskAndTag(uint8_t* mask, uint8_t* tag) {
  ASSERT(!is_interval_check());
  switch (check_) {
    case IS_STRING:
      *mask = kIsNotStringMask;
      *tag = kStringTag;
      return;
    case IS_SYMBOL:
      *mask = kIsSymbolMask;
      *tag = kSymbolTag;
      return;
    default:
      UNREACHABLE();
  }
}


void HCheckMap::PrintDataTo(StringStream* stream) {
  value()->PrintNameTo(stream);
  stream->Add(" %p", *map());
}


void HCheckFunction::PrintDataTo(StringStream* stream) {
  value()->PrintNameTo(stream);
  stream->Add(" %p", *target());
}


void HCallStub::PrintDataTo(StringStream* stream) {
  stream->Add("%s ",
              CodeStub::MajorName(major_key_, false));
  HUnaryCall::PrintDataTo(stream);
}


void HInstanceOf::PrintDataTo(StringStream* stream) {
  left()->PrintNameTo(stream);
  stream->Add(" ");
  right()->PrintNameTo(stream);
  stream->Add(" ");
  context()->PrintNameTo(stream);
}


Range* HValue::InferRange() {
  if (representation().IsTagged()) {
    // Tagged values are always in int32 range when converted to integer,
    // but they can contain -0.
    Range* result = new Range();
    result->set_can_be_minus_zero(true);
    return result;
  } else if (representation().IsNone()) {
    return NULL;
  } else {
    // Untagged integer32 cannot be -0 and we don't compute ranges for
    // untagged doubles.
    return new Range();
  }
}


Range* HConstant::InferRange() {
  if (has_int32_value_) {
    Range* result = new Range(int32_value_, int32_value_);
    result->set_can_be_minus_zero(false);
    return result;
  }
  return HValue::InferRange();
}


Range* HPhi::InferRange() {
  if (representation().IsInteger32()) {
    if (block()->IsLoopHeader()) {
      Range* range = new Range(kMinInt, kMaxInt);
      return range;
    } else {
      Range* range = OperandAt(0)->range()->Copy();
      for (int i = 1; i < OperandCount(); ++i) {
        range->Union(OperandAt(i)->range());
      }
      return range;
    }
  } else {
    return HValue::InferRange();
  }
}


Range* HAdd::InferRange() {
  if (representation().IsInteger32()) {
    Range* a = left()->range();
    Range* b = right()->range();
    Range* res = a->Copy();
    if (!res->AddAndCheckOverflow(b)) {
      ClearFlag(kCanOverflow);
    }
    bool m0 = a->CanBeMinusZero() && b->CanBeMinusZero();
    res->set_can_be_minus_zero(m0);
    return res;
  } else {
    return HValue::InferRange();
  }
}


Range* HSub::InferRange() {
  if (representation().IsInteger32()) {
    Range* a = left()->range();
    Range* b = right()->range();
    Range* res = a->Copy();
    if (!res->SubAndCheckOverflow(b)) {
      ClearFlag(kCanOverflow);
    }
    res->set_can_be_minus_zero(a->CanBeMinusZero() && b->CanBeZero());
    return res;
  } else {
    return HValue::InferRange();
  }
}


Range* HMul::InferRange() {
  if (representation().IsInteger32()) {
    Range* a = left()->range();
    Range* b = right()->range();
    Range* res = a->Copy();
    if (!res->MulAndCheckOverflow(b)) {
      ClearFlag(kCanOverflow);
    }
    bool m0 = (a->CanBeZero() && b->CanBeNegative()) ||
        (a->CanBeNegative() && b->CanBeZero());
    res->set_can_be_minus_zero(m0);
    return res;
  } else {
    return HValue::InferRange();
  }
}


Range* HDiv::InferRange() {
  if (representation().IsInteger32()) {
    Range* result = new Range();
    if (left()->range()->CanBeMinusZero()) {
      result->set_can_be_minus_zero(true);
    }

    if (left()->range()->CanBeZero() && right()->range()->CanBeNegative()) {
      result->set_can_be_minus_zero(true);
    }

    if (right()->range()->Includes(-1) && left()->range()->Includes(kMinInt)) {
      SetFlag(HValue::kCanOverflow);
    }

    if (!right()->range()->CanBeZero()) {
      ClearFlag(HValue::kCanBeDivByZero);
    }
    return result;
  } else {
    return HValue::InferRange();
  }
}


Range* HMod::InferRange() {
  if (representation().IsInteger32()) {
    Range* a = left()->range();
    Range* result = new Range();
    if (a->CanBeMinusZero() || a->CanBeNegative()) {
      result->set_can_be_minus_zero(true);
    }
    if (!right()->range()->CanBeZero()) {
      ClearFlag(HValue::kCanBeDivByZero);
    }
    return result;
  } else {
    return HValue::InferRange();
  }
}


void HPhi::PrintTo(StringStream* stream) {
  stream->Add("[");
  for (int i = 0; i < OperandCount(); ++i) {
    HValue* value = OperandAt(i);
    stream->Add(" ");
    value->PrintNameTo(stream);
    stream->Add(" ");
  }
  stream->Add(" uses%d_%di_%dd_%dt]",
              UseCount(),
              int32_non_phi_uses() + int32_indirect_uses(),
              double_non_phi_uses() + double_indirect_uses(),
              tagged_non_phi_uses() + tagged_indirect_uses());
}


void HPhi::AddInput(HValue* value) {
  inputs_.Add(NULL);
  SetOperandAt(OperandCount() - 1, value);
  // Mark phis that may have 'arguments' directly or indirectly as an operand.
  if (!CheckFlag(kIsArguments) && value->CheckFlag(kIsArguments)) {
    SetFlag(kIsArguments);
  }
}


bool HPhi::HasRealUses() {
  for (HUseIterator it(uses()); !it.Done(); it.Advance()) {
    if (!it.value()->IsPhi()) return true;
  }
  return false;
}


HValue* HPhi::GetRedundantReplacement() {
  HValue* candidate = NULL;
  int count = OperandCount();
  int position = 0;
  while (position < count && candidate == NULL) {
    HValue* current = OperandAt(position++);
    if (current != this) candidate = current;
  }
  while (position < count) {
    HValue* current = OperandAt(position++);
    if (current != this && current != candidate) return NULL;
  }
  ASSERT(candidate != this);
  return candidate;
}


void HPhi::DeleteFromGraph() {
  ASSERT(block() != NULL);
  block()->RemovePhi(this);
  ASSERT(block() == NULL);
}


void HPhi::InitRealUses(int phi_id) {
  // Initialize real uses.
  phi_id_ = phi_id;
  for (HUseIterator it(uses()); !it.Done(); it.Advance()) {
    HValue* value = it.value();
    if (!value->IsPhi()) {
      Representation rep = value->RequiredInputRepresentation(it.index());
      ++non_phi_uses_[rep.kind()];
    }
  }
}


void HPhi::AddNonPhiUsesFrom(HPhi* other) {
  for (int i = 0; i < Representation::kNumRepresentations; i++) {
    indirect_uses_[i] += other->non_phi_uses_[i];
  }
}


void HPhi::AddIndirectUsesTo(int* dest) {
  for (int i = 0; i < Representation::kNumRepresentations; i++) {
    dest[i] += indirect_uses_[i];
  }
}


void HSimulate::PrintDataTo(StringStream* stream) {
  stream->Add("id=%d ", ast_id());
  if (pop_count_ > 0) stream->Add("pop %d", pop_count_);
  if (values_.length() > 0) {
    if (pop_count_ > 0) stream->Add(" /");
    for (int i = 0; i < values_.length(); ++i) {
      if (!HasAssignedIndexAt(i)) {
        stream->Add(" push ");
      } else {
        stream->Add(" var[%d] = ", GetAssignedIndexAt(i));
      }
      values_[i]->PrintNameTo(stream);
    }
  }
}


void HDeoptimize::PrintDataTo(StringStream* stream) {
  if (OperandCount() == 0) return;
  OperandAt(0)->PrintNameTo(stream);
  for (int i = 1; i < OperandCount(); ++i) {
    stream->Add(" ");
    OperandAt(i)->PrintNameTo(stream);
  }
}


void HEnterInlined::PrintDataTo(StringStream* stream) {
  SmartPointer<char> name = function()->debug_name()->ToCString();
  stream->Add("%s, id=%d", *name, function()->id());
}


HConstant::HConstant(Handle<Object> handle, Representation r)
    : handle_(handle),
      has_int32_value_(false),
      has_double_value_(false),
      int32_value_(0),
      double_value_(0)  {
  set_representation(r);
  SetFlag(kUseGVN);
  if (handle_->IsNumber()) {
    double n = handle_->Number();
    double roundtrip_value = static_cast<double>(static_cast<int32_t>(n));
    has_int32_value_ = BitCast<int64_t>(roundtrip_value) == BitCast<int64_t>(n);
    if (has_int32_value_) int32_value_ = static_cast<int32_t>(n);
    double_value_ = n;
    has_double_value_ = true;
  }
}


HConstant* HConstant::CopyToRepresentation(Representation r) const {
  if (r.IsInteger32() && !has_int32_value_) return NULL;
  if (r.IsDouble() && !has_double_value_) return NULL;
  return new HConstant(handle_, r);
}


HConstant* HConstant::CopyToTruncatedInt32() const {
  if (!has_double_value_) return NULL;
  int32_t truncated = NumberToInt32(*handle_);
  return new HConstant(FACTORY->NewNumberFromInt(truncated),
                       Representation::Integer32());
}


bool HConstant::ToBoolean() const {
  // Converts the constant's boolean value according to
  // ECMAScript section 9.2 ToBoolean conversion.
  if (HasInteger32Value()) return Integer32Value() != 0;
  if (HasDoubleValue()) {
    double v = DoubleValue();
    return v != 0 && !isnan(v);
  }
  if (handle()->IsTrue()) return true;
  if (handle()->IsFalse()) return false;
  if (handle()->IsUndefined()) return false;
  if (handle()->IsNull()) return false;
  if (handle()->IsString() &&
      String::cast(*handle())->length() == 0) return false;
  return true;
}

void HConstant::PrintDataTo(StringStream* stream) {
  handle()->ShortPrint(stream);
}


bool HArrayLiteral::IsCopyOnWrite() const {
  return constant_elements()->map() == HEAP->fixed_cow_array_map();
}


void HBinaryOperation::PrintDataTo(StringStream* stream) {
  left()->PrintNameTo(stream);
  stream->Add(" ");
  right()->PrintNameTo(stream);
  if (CheckFlag(kCanOverflow)) stream->Add(" !");
  if (CheckFlag(kBailoutOnMinusZero)) stream->Add(" -0?");
}


Range* HBitAnd::InferRange() {
  int32_t left_mask = (left()->range() != NULL)
      ? left()->range()->Mask()
      : 0xffffffff;
  int32_t right_mask = (right()->range() != NULL)
      ? right()->range()->Mask()
      : 0xffffffff;
  int32_t result_mask = left_mask & right_mask;
  return (result_mask >= 0)
      ? new Range(0, result_mask)
      : HValue::InferRange();
}


Range* HBitOr::InferRange() {
  int32_t left_mask = (left()->range() != NULL)
      ? left()->range()->Mask()
      : 0xffffffff;
  int32_t right_mask = (right()->range() != NULL)
      ? right()->range()->Mask()
      : 0xffffffff;
  int32_t result_mask = left_mask | right_mask;
  return (result_mask >= 0)
      ? new Range(0, result_mask)
      : HValue::InferRange();
}


Range* HSar::InferRange() {
  if (right()->IsConstant()) {
    HConstant* c = HConstant::cast(right());
    if (c->HasInteger32Value()) {
      Range* result = (left()->range() != NULL)
          ? left()->range()->Copy()
          : new Range();
      result->Sar(c->Integer32Value());
      return result;
    }
  }
  return HValue::InferRange();
}


Range* HShl::InferRange() {
  if (right()->IsConstant()) {
    HConstant* c = HConstant::cast(right());
    if (c->HasInteger32Value()) {
      Range* result = (left()->range() != NULL)
          ? left()->range()->Copy()
          : new Range();
      result->Shl(c->Integer32Value());
      return result;
    }
  }
  return HValue::InferRange();
}



void HCompareGeneric::PrintDataTo(StringStream* stream) {
  stream->Add(Token::Name(token()));
  stream->Add(" ");
  HBinaryOperation::PrintDataTo(stream);
}


void HCompareIDAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add(Token::Name(token()));
  stream->Add(" ");
  left()->PrintNameTo(stream);
  stream->Add(" ");
  right()->PrintNameTo(stream);
}


void HCompareIDAndBranch::SetInputRepresentation(Representation r) {
  input_representation_ = r;
  if (r.IsDouble()) {
    SetFlag(kDeoptimizeOnUndefined);
  } else {
    ASSERT(r.IsInteger32());
  }
}


void HParameter::PrintDataTo(StringStream* stream) {
  stream->Add("%u", index());
}


void HLoadNamedField::PrintDataTo(StringStream* stream) {
  object()->PrintNameTo(stream);
  stream->Add(" @%d%s", offset(), is_in_object() ? "[in-object]" : "");
}


HLoadNamedFieldPolymorphic::HLoadNamedFieldPolymorphic(HValue* context,
                                                       HValue* object,
                                                       ZoneMapList* types,
                                                       Handle<String> name)
    : types_(Min(types->length(), kMaxLoadPolymorphism)),
      name_(name),
      need_generic_(false) {
  SetOperandAt(0, context);
  SetOperandAt(1, object);
  set_representation(Representation::Tagged());
  SetFlag(kDependsOnMaps);
  for (int i = 0;
       i < types->length() && types_.length() < kMaxLoadPolymorphism;
       ++i) {
    Handle<Map> map = types->at(i);
    LookupResult lookup;
    map->LookupInDescriptors(NULL, *name, &lookup);
    if (lookup.IsProperty()) {
      switch (lookup.type()) {
        case FIELD: {
          int index = lookup.GetLocalFieldIndexFromMap(*map);
          if (index < 0) {
            SetFlag(kDependsOnInobjectFields);
          } else {
            SetFlag(kDependsOnBackingStoreFields);
          }
          types_.Add(types->at(i));
          break;
        }
        case CONSTANT_FUNCTION:
          types_.Add(types->at(i));
          break;
        default:
          break;
      }
    }
  }

  if (types_.length() == types->length() && FLAG_deoptimize_uncommon_cases) {
    SetFlag(kUseGVN);
  } else {
    SetAllSideEffects();
    need_generic_ = true;
  }
}


bool HLoadNamedFieldPolymorphic::DataEquals(HValue* value) {
  HLoadNamedFieldPolymorphic* other = HLoadNamedFieldPolymorphic::cast(value);
  if (types_.length() != other->types()->length()) return false;
  if (!name_.is_identical_to(other->name())) return false;
  if (need_generic_ != other->need_generic_) return false;
  for (int i = 0; i < types_.length(); i++) {
    bool found = false;
    for (int j = 0; j < types_.length(); j++) {
      if (types_.at(j).is_identical_to(other->types()->at(i))) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}


void HLoadKeyedFastElement::PrintDataTo(StringStream* stream) {
  object()->PrintNameTo(stream);
  stream->Add("[");
  key()->PrintNameTo(stream);
  stream->Add("]");
}


bool HLoadKeyedFastElement::RequiresHoleCheck() const {
  for (HUseIterator it(uses()); !it.Done(); it.Advance()) {
    HValue* use = it.value();
    if (!use->IsChange()) return true;
  }
  return false;
}


void HLoadKeyedFastDoubleElement::PrintDataTo(StringStream* stream) {
  elements()->PrintNameTo(stream);
  stream->Add("[");
  key()->PrintNameTo(stream);
  stream->Add("]");
}


bool HLoadKeyedFastDoubleElement::RequiresHoleCheck() const {
  return true;
}


void HLoadKeyedGeneric::PrintDataTo(StringStream* stream) {
  object()->PrintNameTo(stream);
  stream->Add("[");
  key()->PrintNameTo(stream);
  stream->Add("]");
}


void HLoadKeyedSpecializedArrayElement::PrintDataTo(
    StringStream* stream) {
  external_pointer()->PrintNameTo(stream);
  stream->Add(".");
  switch (elements_kind()) {
    case JSObject::EXTERNAL_BYTE_ELEMENTS:
      stream->Add("byte");
      break;
    case JSObject::EXTERNAL_UNSIGNED_BYTE_ELEMENTS:
      stream->Add("u_byte");
      break;
    case JSObject::EXTERNAL_SHORT_ELEMENTS:
      stream->Add("short");
      break;
    case JSObject::EXTERNAL_UNSIGNED_SHORT_ELEMENTS:
      stream->Add("u_short");
      break;
    case JSObject::EXTERNAL_INT_ELEMENTS:
      stream->Add("int");
      break;
    case JSObject::EXTERNAL_UNSIGNED_INT_ELEMENTS:
      stream->Add("u_int");
      break;
    case JSObject::EXTERNAL_FLOAT_ELEMENTS:
      stream->Add("float");
      break;
    case JSObject::EXTERNAL_DOUBLE_ELEMENTS:
      stream->Add("double");
      break;
    case JSObject::EXTERNAL_PIXEL_ELEMENTS:
      stream->Add("pixel");
      break;
    case JSObject::FAST_ELEMENTS:
    case JSObject::FAST_DOUBLE_ELEMENTS:
    case JSObject::DICTIONARY_ELEMENTS:
    case JSObject::NON_STRICT_ARGUMENTS_ELEMENTS:
      UNREACHABLE();
      break;
  }
  stream->Add("[");
  key()->PrintNameTo(stream);
  stream->Add("]");
}


void HStoreNamedGeneric::PrintDataTo(StringStream* stream) {
  object()->PrintNameTo(stream);
  stream->Add(".");
  ASSERT(name()->IsString());
  stream->Add(*String::cast(*name())->ToCString());
  stream->Add(" = ");
  value()->PrintNameTo(stream);
}


void HStoreNamedField::PrintDataTo(StringStream* stream) {
  object()->PrintNameTo(stream);
  stream->Add(".");
  ASSERT(name()->IsString());
  stream->Add(*String::cast(*name())->ToCString());
  stream->Add(" = ");
  value()->PrintNameTo(stream);
  if (!transition().is_null()) {
    stream->Add(" (transition map %p)", *transition());
  }
}


void HStoreKeyedFastElement::PrintDataTo(StringStream* stream) {
  object()->PrintNameTo(stream);
  stream->Add("[");
  key()->PrintNameTo(stream);
  stream->Add("] = ");
  value()->PrintNameTo(stream);
}


void HStoreKeyedFastDoubleElement::PrintDataTo(StringStream* stream) {
  elements()->PrintNameTo(stream);
  stream->Add("[");
  key()->PrintNameTo(stream);
  stream->Add("] = ");
  value()->PrintNameTo(stream);
}


void HStoreKeyedGeneric::PrintDataTo(StringStream* stream) {
  object()->PrintNameTo(stream);
  stream->Add("[");
  key()->PrintNameTo(stream);
  stream->Add("] = ");
  value()->PrintNameTo(stream);
}


void HStoreKeyedSpecializedArrayElement::PrintDataTo(
    StringStream* stream) {
  external_pointer()->PrintNameTo(stream);
  stream->Add(".");
  switch (elements_kind()) {
    case JSObject::EXTERNAL_BYTE_ELEMENTS:
      stream->Add("byte");
      break;
    case JSObject::EXTERNAL_UNSIGNED_BYTE_ELEMENTS:
      stream->Add("u_byte");
      break;
    case JSObject::EXTERNAL_SHORT_ELEMENTS:
      stream->Add("short");
      break;
    case JSObject::EXTERNAL_UNSIGNED_SHORT_ELEMENTS:
      stream->Add("u_short");
      break;
    case JSObject::EXTERNAL_INT_ELEMENTS:
      stream->Add("int");
      break;
    case JSObject::EXTERNAL_UNSIGNED_INT_ELEMENTS:
      stream->Add("u_int");
      break;
    case JSObject::EXTERNAL_FLOAT_ELEMENTS:
      stream->Add("float");
      break;
    case JSObject::EXTERNAL_DOUBLE_ELEMENTS:
      stream->Add("double");
      break;
    case JSObject::EXTERNAL_PIXEL_ELEMENTS:
      stream->Add("pixel");
      break;
    case JSObject::FAST_ELEMENTS:
    case JSObject::FAST_DOUBLE_ELEMENTS:
    case JSObject::DICTIONARY_ELEMENTS:
    case JSObject::NON_STRICT_ARGUMENTS_ELEMENTS:
      UNREACHABLE();
      break;
  }
  stream->Add("[");
  key()->PrintNameTo(stream);
  stream->Add("] = ");
  value()->PrintNameTo(stream);
}


void HLoadGlobalCell::PrintDataTo(StringStream* stream) {
  stream->Add("[%p]", *cell());
  if (check_hole_value()) stream->Add(" (deleteable/read-only)");
}


void HLoadGlobalGeneric::PrintDataTo(StringStream* stream) {
  stream->Add("%o ", *name());
}


void HStoreGlobalCell::PrintDataTo(StringStream* stream) {
  stream->Add("[%p] = ", *cell());
  value()->PrintNameTo(stream);
}


void HStoreGlobalGeneric::PrintDataTo(StringStream* stream) {
  stream->Add("%o = ", *name());
  value()->PrintNameTo(stream);
}


void HLoadContextSlot::PrintDataTo(StringStream* stream) {
  value()->PrintNameTo(stream);
  stream->Add("[%d]", slot_index());
}


void HStoreContextSlot::PrintDataTo(StringStream* stream) {
  context()->PrintNameTo(stream);
  stream->Add("[%d] = ", slot_index());
  value()->PrintNameTo(stream);
}


// Implementation of type inference and type conversions. Calculates
// the inferred type of this instruction based on the input operands.

HType HValue::CalculateInferredType() {
  return type_;
}


HType HCheckMap::CalculateInferredType() {
  return value()->type();
}


HType HCheckFunction::CalculateInferredType() {
  return value()->type();
}


HType HCheckNonSmi::CalculateInferredType() {
  // TODO(kasperl): Is there any way to signal that this isn't a smi?
  return HType::Tagged();
}


HType HCheckSmi::CalculateInferredType() {
  return HType::Smi();
}


HType HPhi::CalculateInferredType() {
  HType result = HType::Uninitialized();
  for (int i = 0; i < OperandCount(); ++i) {
    HType current = OperandAt(i)->type();
    result = result.Combine(current);
  }
  return result;
}


HType HConstant::CalculateInferredType() {
  return HType::TypeFromValue(handle_);
}


HType HCompareGeneric::CalculateInferredType() {
  return HType::Boolean();
}


HType HInstanceOf::CalculateInferredType() {
  return HType::Boolean();
}


HType HDeleteProperty::CalculateInferredType() {
  return HType::Boolean();
}


HType HInstanceOfKnownGlobal::CalculateInferredType() {
  return HType::Boolean();
}


HType HBitwiseBinaryOperation::CalculateInferredType() {
  return HType::TaggedNumber();
}


HType HArithmeticBinaryOperation::CalculateInferredType() {
  return HType::TaggedNumber();
}


HType HAdd::CalculateInferredType() {
  return HType::Tagged();
}


HType HBitAnd::CalculateInferredType() {
  return HType::TaggedNumber();
}


HType HBitXor::CalculateInferredType() {
  return HType::TaggedNumber();
}


HType HBitOr::CalculateInferredType() {
  return HType::TaggedNumber();
}


HType HBitNot::CalculateInferredType() {
  return HType::TaggedNumber();
}


HType HUnaryMathOperation::CalculateInferredType() {
  return HType::TaggedNumber();
}


HType HShl::CalculateInferredType() {
  return HType::TaggedNumber();
}


HType HShr::CalculateInferredType() {
  return HType::TaggedNumber();
}


HType HSar::CalculateInferredType() {
  return HType::TaggedNumber();
}


HValue* HUnaryMathOperation::EnsureAndPropagateNotMinusZero(
    BitVector* visited) {
  visited->Add(id());
  if (representation().IsInteger32() &&
      !value()->representation().IsInteger32()) {
    if (value()->range() == NULL || value()->range()->CanBeMinusZero()) {
      SetFlag(kBailoutOnMinusZero);
    }
  }
  if (RequiredInputRepresentation(0).IsInteger32() &&
      representation().IsInteger32()) {
    return value();
  }
  return NULL;
}



HValue* HChange::EnsureAndPropagateNotMinusZero(BitVector* visited) {
  visited->Add(id());
  if (from().IsInteger32()) return NULL;
  if (CanTruncateToInt32()) return NULL;
  if (value()->range() == NULL || value()->range()->CanBeMinusZero()) {
    SetFlag(kBailoutOnMinusZero);
  }
  ASSERT(!from().IsInteger32() || !to().IsInteger32());
  return NULL;
}


HValue* HForceRepresentation::EnsureAndPropagateNotMinusZero(
    BitVector* visited) {
  visited->Add(id());
  return value();
}


HValue* HMod::EnsureAndPropagateNotMinusZero(BitVector* visited) {
  visited->Add(id());
  if (range() == NULL || range()->CanBeMinusZero()) {
    SetFlag(kBailoutOnMinusZero);
    return left();
  }
  return NULL;
}


HValue* HDiv::EnsureAndPropagateNotMinusZero(BitVector* visited) {
  visited->Add(id());
  if (range() == NULL || range()->CanBeMinusZero()) {
    SetFlag(kBailoutOnMinusZero);
  }
  return NULL;
}


HValue* HMul::EnsureAndPropagateNotMinusZero(BitVector* visited) {
  visited->Add(id());
  if (range() == NULL || range()->CanBeMinusZero()) {
    SetFlag(kBailoutOnMinusZero);
  }
  return NULL;
}


HValue* HSub::EnsureAndPropagateNotMinusZero(BitVector* visited) {
  visited->Add(id());
  // Propagate to the left argument. If the left argument cannot be -0, then
  // the result of the add operation cannot be either.
  if (range() == NULL || range()->CanBeMinusZero()) {
    return left();
  }
  return NULL;
}


HValue* HAdd::EnsureAndPropagateNotMinusZero(BitVector* visited) {
  visited->Add(id());
  // Propagate to the left argument. If the left argument cannot be -0, then
  // the result of the sub operation cannot be either.
  if (range() == NULL || range()->CanBeMinusZero()) {
    return left();
  }
  return NULL;
}


void HIn::PrintDataTo(StringStream* stream) {
  key()->PrintNameTo(stream);
  stream->Add(" ");
  object()->PrintNameTo(stream);
}


// Node-specific verification code is only included in debug mode.
#ifdef DEBUG

void HPhi::Verify() {
  ASSERT(OperandCount() == block()->predecessors()->length());
  for (int i = 0; i < OperandCount(); ++i) {
    HValue* value = OperandAt(i);
    HBasicBlock* defining_block = value->block();
    HBasicBlock* predecessor_block = block()->predecessors()->at(i);
    ASSERT(defining_block == predecessor_block ||
           defining_block->Dominates(predecessor_block));
  }
}


void HSimulate::Verify() {
  HInstruction::Verify();
  ASSERT(HasAstId());
}


void HCheckSmi::Verify() {
  HInstruction::Verify();
  ASSERT(HasNoUses());
}


void HCheckNonSmi::Verify() {
  HInstruction::Verify();
  ASSERT(HasNoUses());
}


void HCheckFunction::Verify() {
  HInstruction::Verify();
  ASSERT(HasNoUses());
}


void HCheckPrototypeMaps::Verify() {
  HInstruction::Verify();
  ASSERT(HasNoUses());
}

#endif

} }  // namespace v8::internal
