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

#if defined(V8_TARGET_ARCH_IA32)

#include "lithium-allocator-inl.h"
#include "ia32/lithium-ia32.h"
#include "ia32/lithium-codegen-ia32.h"

namespace v8 {
namespace internal {

#define DEFINE_COMPILE(type)                            \
  void L##type::CompileToNative(LCodeGen* generator) {  \
    generator->Do##type(this);                          \
  }
LITHIUM_CONCRETE_INSTRUCTION_LIST(DEFINE_COMPILE)
#undef DEFINE_COMPILE

LOsrEntry::LOsrEntry() {
  for (int i = 0; i < Register::kNumAllocatableRegisters; ++i) {
    register_spills_[i] = NULL;
  }
  for (int i = 0; i < DoubleRegister::kNumAllocatableRegisters; ++i) {
    double_register_spills_[i] = NULL;
  }
}


void LOsrEntry::MarkSpilledRegister(int allocation_index,
                                    LOperand* spill_operand) {
  ASSERT(spill_operand->IsStackSlot());
  ASSERT(register_spills_[allocation_index] == NULL);
  register_spills_[allocation_index] = spill_operand;
}


void LOsrEntry::MarkSpilledDoubleRegister(int allocation_index,
                                          LOperand* spill_operand) {
  ASSERT(spill_operand->IsDoubleStackSlot());
  ASSERT(double_register_spills_[allocation_index] == NULL);
  double_register_spills_[allocation_index] = spill_operand;
}


#ifdef DEBUG
void LInstruction::VerifyCall() {
  // Call instructions can use only fixed registers as temporaries and
  // outputs because all registers are blocked by the calling convention.
  // Inputs operands must use a fixed register or use-at-start policy or
  // a non-register policy.
  ASSERT(Output() == NULL ||
         LUnallocated::cast(Output())->HasFixedPolicy() ||
         !LUnallocated::cast(Output())->HasRegisterPolicy());
  for (UseIterator it(this); !it.Done(); it.Advance()) {
    LUnallocated* operand = LUnallocated::cast(it.Current());
    ASSERT(operand->HasFixedPolicy() ||
           operand->IsUsedAtStart());
  }
  for (TempIterator it(this); !it.Done(); it.Advance()) {
    LUnallocated* operand = LUnallocated::cast(it.Current());
    ASSERT(operand->HasFixedPolicy() ||!operand->HasRegisterPolicy());
  }
}
#endif


void LInstruction::PrintTo(StringStream* stream) {
  stream->Add("%s ", this->Mnemonic());

  PrintOutputOperandTo(stream);

  PrintDataTo(stream);

  if (HasEnvironment()) {
    stream->Add(" ");
    environment()->PrintTo(stream);
  }

  if (HasPointerMap()) {
    stream->Add(" ");
    pointer_map()->PrintTo(stream);
  }
}


template<int R, int I, int T>
void LTemplateInstruction<R, I, T>::PrintDataTo(StringStream* stream) {
  stream->Add("= ");
  for (int i = 0; i < inputs_.length(); i++) {
    if (i > 0) stream->Add(" ");
    inputs_[i]->PrintTo(stream);
  }
}


template<int R, int I, int T>
void LTemplateInstruction<R, I, T>::PrintOutputOperandTo(StringStream* stream) {
  for (int i = 0; i < results_.length(); i++) {
    if (i > 0) stream->Add(" ");
    results_[i]->PrintTo(stream);
  }
}


void LLabel::PrintDataTo(StringStream* stream) {
  LGap::PrintDataTo(stream);
  LLabel* rep = replacement();
  if (rep != NULL) {
    stream->Add(" Dead block replaced with B%d", rep->block_id());
  }
}


bool LGap::IsRedundant() const {
  for (int i = 0; i < 4; i++) {
    if (parallel_moves_[i] != NULL && !parallel_moves_[i]->IsRedundant()) {
      return false;
    }
  }

  return true;
}


void LGap::PrintDataTo(StringStream* stream) {
  for (int i = 0; i < 4; i++) {
    stream->Add("(");
    if (parallel_moves_[i] != NULL) {
      parallel_moves_[i]->PrintDataTo(stream);
    }
    stream->Add(") ");
  }
}


const char* LArithmeticD::Mnemonic() const {
  switch (op()) {
    case Token::ADD: return "add-d";
    case Token::SUB: return "sub-d";
    case Token::MUL: return "mul-d";
    case Token::DIV: return "div-d";
    case Token::MOD: return "mod-d";
    default:
      UNREACHABLE();
      return NULL;
  }
}


const char* LArithmeticT::Mnemonic() const {
  switch (op()) {
    case Token::ADD: return "add-t";
    case Token::SUB: return "sub-t";
    case Token::MUL: return "mul-t";
    case Token::MOD: return "mod-t";
    case Token::DIV: return "div-t";
    case Token::BIT_AND: return "bit-and-t";
    case Token::BIT_OR: return "bit-or-t";
    case Token::BIT_XOR: return "bit-xor-t";
    case Token::SHL: return "sal-t";
    case Token::SAR: return "sar-t";
    case Token::SHR: return "shr-t";
    default:
      UNREACHABLE();
      return NULL;
  }
}


void LGoto::PrintDataTo(StringStream* stream) {
  stream->Add("B%d", block_id());
}


void LBranch::PrintDataTo(StringStream* stream) {
  stream->Add("B%d | B%d on ", true_block_id(), false_block_id());
  InputAt(0)->PrintTo(stream);
}


void LCmpIDAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if ");
  InputAt(0)->PrintTo(stream);
  stream->Add(" %s ", Token::String(op()));
  InputAt(1)->PrintTo(stream);
  stream->Add(" then B%d else B%d", true_block_id(), false_block_id());
}


void LIsNilAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if ");
  InputAt(0)->PrintTo(stream);
  stream->Add(kind() == kStrictEquality ? " === " : " == ");
  stream->Add(nil() == kNullValue ? "null" : "undefined");
  stream->Add(" then B%d else B%d", true_block_id(), false_block_id());
}


void LIsObjectAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if is_object(");
  InputAt(0)->PrintTo(stream);
  stream->Add(") then B%d else B%d", true_block_id(), false_block_id());
}


void LIsSmiAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if is_smi(");
  InputAt(0)->PrintTo(stream);
  stream->Add(") then B%d else B%d", true_block_id(), false_block_id());
}


void LIsUndetectableAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if is_undetectable(");
  InputAt(0)->PrintTo(stream);
  stream->Add(") then B%d else B%d", true_block_id(), false_block_id());
}


void LHasInstanceTypeAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if has_instance_type(");
  InputAt(0)->PrintTo(stream);
  stream->Add(") then B%d else B%d", true_block_id(), false_block_id());
}


void LHasCachedArrayIndexAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if has_cached_array_index(");
  InputAt(0)->PrintTo(stream);
  stream->Add(") then B%d else B%d", true_block_id(), false_block_id());
}


void LClassOfTestAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if class_of_test(");
  InputAt(0)->PrintTo(stream);
  stream->Add(", \"%o\") then B%d else B%d",
              *hydrogen()->class_name(),
              true_block_id(),
              false_block_id());
}


void LTypeofIsAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if typeof ");
  InputAt(0)->PrintTo(stream);
  stream->Add(" == \"%s\" then B%d else B%d",
              *hydrogen()->type_literal()->ToCString(),
              true_block_id(), false_block_id());
}


void LCallConstantFunction::PrintDataTo(StringStream* stream) {
  stream->Add("#%d / ", arity());
}


void LUnaryMathOperation::PrintDataTo(StringStream* stream) {
  stream->Add("/%s ", hydrogen()->OpName());
  InputAt(0)->PrintTo(stream);
}


void LLoadContextSlot::PrintDataTo(StringStream* stream) {
  InputAt(0)->PrintTo(stream);
  stream->Add("[%d]", slot_index());
}


void LStoreContextSlot::PrintDataTo(StringStream* stream) {
  InputAt(0)->PrintTo(stream);
  stream->Add("[%d] <- ", slot_index());
  InputAt(1)->PrintTo(stream);
}


void LInvokeFunction::PrintDataTo(StringStream* stream) {
  stream->Add("= ");
  InputAt(0)->PrintTo(stream);
  stream->Add(" ");
  InputAt(1)->PrintTo(stream);
  stream->Add(" #%d / ", arity());
}


void LCallKeyed::PrintDataTo(StringStream* stream) {
  stream->Add("[ecx] #%d / ", arity());
}


void LCallNamed::PrintDataTo(StringStream* stream) {
  SmartArrayPointer<char> name_string = name()->ToCString();
  stream->Add("%s #%d / ", *name_string, arity());
}


void LCallGlobal::PrintDataTo(StringStream* stream) {
  SmartArrayPointer<char> name_string = name()->ToCString();
  stream->Add("%s #%d / ", *name_string, arity());
}


void LCallKnownGlobal::PrintDataTo(StringStream* stream) {
  stream->Add("#%d / ", arity());
}


void LCallNew::PrintDataTo(StringStream* stream) {
  stream->Add("= ");
  InputAt(0)->PrintTo(stream);
  stream->Add(" #%d / ", arity());
}


void LAccessArgumentsAt::PrintDataTo(StringStream* stream) {
  arguments()->PrintTo(stream);

  stream->Add(" length ");
  length()->PrintTo(stream);

  stream->Add(" index ");
  index()->PrintTo(stream);
}


int LChunk::GetNextSpillIndex(bool is_double) {
  // Skip a slot if for a double-width slot.
  if (is_double) {
    spill_slot_count_ |= 1;  // Make it odd, so incrementing makes it even.
    spill_slot_count_++;
    num_double_slots_++;
  }
  return spill_slot_count_++;
}


LOperand* LChunk::GetNextSpillSlot(bool is_double) {
  int index = GetNextSpillIndex(is_double);
  if (is_double) {
    return LDoubleStackSlot::Create(index);
  } else {
    return LStackSlot::Create(index);
  }
}


void LChunk::MarkEmptyBlocks() {
  HPhase phase("Mark empty blocks", this);
  for (int i = 0; i < graph()->blocks()->length(); ++i) {
    HBasicBlock* block = graph()->blocks()->at(i);
    int first = block->first_instruction_index();
    int last = block->last_instruction_index();
    LInstruction* first_instr = instructions()->at(first);
    LInstruction* last_instr = instructions()->at(last);

    LLabel* label = LLabel::cast(first_instr);
    if (last_instr->IsGoto()) {
      LGoto* goto_instr = LGoto::cast(last_instr);
      if (label->IsRedundant() &&
          !label->is_loop_header()) {
        bool can_eliminate = true;
        for (int i = first + 1; i < last && can_eliminate; ++i) {
          LInstruction* cur = instructions()->at(i);
          if (cur->IsGap()) {
            LGap* gap = LGap::cast(cur);
            if (!gap->IsRedundant()) {
              can_eliminate = false;
            }
          } else {
            can_eliminate = false;
          }
        }

        if (can_eliminate) {
          label->set_replacement(GetLabel(goto_instr->block_id()));
        }
      }
    }
  }
}


void LStoreNamedField::PrintDataTo(StringStream* stream) {
  object()->PrintTo(stream);
  stream->Add(".");
  stream->Add(*String::cast(*name())->ToCString());
  stream->Add(" <- ");
  value()->PrintTo(stream);
}


void LStoreNamedGeneric::PrintDataTo(StringStream* stream) {
  object()->PrintTo(stream);
  stream->Add(".");
  stream->Add(*String::cast(*name())->ToCString());
  stream->Add(" <- ");
  value()->PrintTo(stream);
}


void LStoreKeyedFastElement::PrintDataTo(StringStream* stream) {
  object()->PrintTo(stream);
  stream->Add("[");
  key()->PrintTo(stream);
  stream->Add("] <- ");
  value()->PrintTo(stream);
}


void LStoreKeyedFastDoubleElement::PrintDataTo(StringStream* stream) {
  elements()->PrintTo(stream);
  stream->Add("[");
  key()->PrintTo(stream);
  stream->Add("] <- ");
  value()->PrintTo(stream);
}


void LStoreKeyedGeneric::PrintDataTo(StringStream* stream) {
  object()->PrintTo(stream);
  stream->Add("[");
  key()->PrintTo(stream);
  stream->Add("] <- ");
  value()->PrintTo(stream);
}


void LChunk::AddInstruction(LInstruction* instr, HBasicBlock* block) {
  LInstructionGap* gap = new LInstructionGap(block);
  int index = -1;
  if (instr->IsControl()) {
    instructions_.Add(gap);
    index = instructions_.length();
    instructions_.Add(instr);
  } else {
    index = instructions_.length();
    instructions_.Add(instr);
    instructions_.Add(gap);
  }
  if (instr->HasPointerMap()) {
    pointer_maps_.Add(instr->pointer_map());
    instr->pointer_map()->set_lithium_position(index);
  }
}


LConstantOperand* LChunk::DefineConstantOperand(HConstant* constant) {
  return LConstantOperand::Create(constant->id());
}


int LChunk::GetParameterStackSlot(int index) const {
  // The receiver is at index 0, the first parameter at index 1, so we
  // shift all parameter indexes down by the number of parameters, and
  // make sure they end up negative so they are distinguishable from
  // spill slots.
  int result = index - info()->scope()->num_parameters() - 1;
  ASSERT(result < 0);
  return result;
}

// A parameter relative to ebp in the arguments stub.
int LChunk::ParameterAt(int index) {
  ASSERT(-1 <= index);  // -1 is the receiver.
  return (1 + info()->scope()->num_parameters() - index) *
      kPointerSize;
}


LGap* LChunk::GetGapAt(int index) const {
  return LGap::cast(instructions_[index]);
}


bool LChunk::IsGapAt(int index) const {
  return instructions_[index]->IsGap();
}


int LChunk::NearestGapPos(int index) const {
  while (!IsGapAt(index)) index--;
  return index;
}


void LChunk::AddGapMove(int index, LOperand* from, LOperand* to) {
  GetGapAt(index)->GetOrCreateParallelMove(LGap::START)->AddMove(from, to);
}


Handle<Object> LChunk::LookupLiteral(LConstantOperand* operand) const {
  return HConstant::cast(graph_->LookupValue(operand->index()))->handle();
}


Representation LChunk::LookupLiteralRepresentation(
    LConstantOperand* operand) const {
  return graph_->LookupValue(operand->index())->representation();
}


LChunk* LChunkBuilder::Build() {
  ASSERT(is_unused());
  chunk_ = new LChunk(info(), graph());
  HPhase phase("Building chunk", chunk_);
  status_ = BUILDING;
  const ZoneList<HBasicBlock*>* blocks = graph()->blocks();
  for (int i = 0; i < blocks->length(); i++) {
    HBasicBlock* next = NULL;
    if (i < blocks->length() - 1) next = blocks->at(i + 1);
    DoBasicBlock(blocks->at(i), next);
    if (is_aborted()) return NULL;
  }
  status_ = DONE;
  return chunk_;
}


void LChunkBuilder::Abort(const char* format, ...) {
  if (FLAG_trace_bailout) {
    SmartArrayPointer<char> name(
        info()->shared_info()->DebugName()->ToCString());
    PrintF("Aborting LChunk building in @\"%s\": ", *name);
    va_list arguments;
    va_start(arguments, format);
    OS::VPrint(format, arguments);
    va_end(arguments);
    PrintF("\n");
  }
  status_ = ABORTED;
}


LRegister* LChunkBuilder::ToOperand(Register reg) {
  return LRegister::Create(Register::ToAllocationIndex(reg));
}


LUnallocated* LChunkBuilder::ToUnallocated(Register reg) {
  return new LUnallocated(LUnallocated::FIXED_REGISTER,
                          Register::ToAllocationIndex(reg));
}


LUnallocated* LChunkBuilder::ToUnallocated(XMMRegister reg) {
  return new LUnallocated(LUnallocated::FIXED_DOUBLE_REGISTER,
                          XMMRegister::ToAllocationIndex(reg));
}


LOperand* LChunkBuilder::UseFixed(HValue* value, Register fixed_register) {
  return Use(value, ToUnallocated(fixed_register));
}


LOperand* LChunkBuilder::UseFixedDouble(HValue* value, XMMRegister reg) {
  return Use(value, ToUnallocated(reg));
}


LOperand* LChunkBuilder::UseRegister(HValue* value) {
  return Use(value, new LUnallocated(LUnallocated::MUST_HAVE_REGISTER));
}


LOperand* LChunkBuilder::UseRegisterAtStart(HValue* value) {
  return Use(value,
             new LUnallocated(LUnallocated::MUST_HAVE_REGISTER,
                              LUnallocated::USED_AT_START));
}


LOperand* LChunkBuilder::UseTempRegister(HValue* value) {
  return Use(value, new LUnallocated(LUnallocated::WRITABLE_REGISTER));
}


LOperand* LChunkBuilder::Use(HValue* value) {
  return Use(value, new LUnallocated(LUnallocated::NONE));
}


LOperand* LChunkBuilder::UseAtStart(HValue* value) {
  return Use(value, new LUnallocated(LUnallocated::NONE,
                                     LUnallocated::USED_AT_START));
}


LOperand* LChunkBuilder::UseOrConstant(HValue* value) {
  return value->IsConstant()
      ? chunk_->DefineConstantOperand(HConstant::cast(value))
      : Use(value);
}


LOperand* LChunkBuilder::UseOrConstantAtStart(HValue* value) {
  return value->IsConstant()
      ? chunk_->DefineConstantOperand(HConstant::cast(value))
      : UseAtStart(value);
}


LOperand* LChunkBuilder::UseRegisterOrConstant(HValue* value) {
  return value->IsConstant()
      ? chunk_->DefineConstantOperand(HConstant::cast(value))
      : UseRegister(value);
}


LOperand* LChunkBuilder::UseRegisterOrConstantAtStart(HValue* value) {
  return value->IsConstant()
      ? chunk_->DefineConstantOperand(HConstant::cast(value))
      : UseRegisterAtStart(value);
}


LOperand* LChunkBuilder::UseAny(HValue* value) {
  return value->IsConstant()
      ? chunk_->DefineConstantOperand(HConstant::cast(value))
      :  Use(value, new LUnallocated(LUnallocated::ANY));
}


LOperand* LChunkBuilder::Use(HValue* value, LUnallocated* operand) {
  if (value->EmitAtUses()) {
    HInstruction* instr = HInstruction::cast(value);
    VisitInstruction(instr);
  }
  allocator_->RecordUse(value, operand);
  return operand;
}


template<int I, int T>
LInstruction* LChunkBuilder::Define(LTemplateInstruction<1, I, T>* instr,
                                    LUnallocated* result) {
  allocator_->RecordDefinition(current_instruction_, result);
  instr->set_result(result);
  return instr;
}


template<int I, int T>
LInstruction* LChunkBuilder::Define(LTemplateInstruction<1, I, T>* instr) {
  return Define(instr, new LUnallocated(LUnallocated::NONE));
}


template<int I, int T>
LInstruction* LChunkBuilder::DefineAsRegister(
    LTemplateInstruction<1, I, T>* instr) {
  return Define(instr, new LUnallocated(LUnallocated::MUST_HAVE_REGISTER));
}


template<int I, int T>
LInstruction* LChunkBuilder::DefineAsSpilled(
    LTemplateInstruction<1, I, T>* instr,
    int index) {
  return Define(instr, new LUnallocated(LUnallocated::FIXED_SLOT, index));
}


template<int I, int T>
LInstruction* LChunkBuilder::DefineSameAsFirst(
    LTemplateInstruction<1, I, T>* instr) {
  return Define(instr, new LUnallocated(LUnallocated::SAME_AS_FIRST_INPUT));
}


template<int I, int T>
LInstruction* LChunkBuilder::DefineFixed(LTemplateInstruction<1, I, T>* instr,
                                         Register reg) {
  return Define(instr, ToUnallocated(reg));
}


template<int I, int T>
LInstruction* LChunkBuilder::DefineFixedDouble(
    LTemplateInstruction<1, I, T>* instr,
    XMMRegister reg) {
  return Define(instr, ToUnallocated(reg));
}


LInstruction* LChunkBuilder::AssignEnvironment(LInstruction* instr) {
  HEnvironment* hydrogen_env = current_block_->last_environment();
  int argument_index_accumulator = 0;
  instr->set_environment(CreateEnvironment(hydrogen_env,
                                           &argument_index_accumulator));
  return instr;
}


LInstruction* LChunkBuilder::SetInstructionPendingDeoptimizationEnvironment(
    LInstruction* instr, int ast_id) {
  ASSERT(instruction_pending_deoptimization_environment_ == NULL);
  ASSERT(pending_deoptimization_ast_id_ == AstNode::kNoNumber);
  instruction_pending_deoptimization_environment_ = instr;
  pending_deoptimization_ast_id_ = ast_id;
  return instr;
}


void LChunkBuilder::ClearInstructionPendingDeoptimizationEnvironment() {
  instruction_pending_deoptimization_environment_ = NULL;
  pending_deoptimization_ast_id_ = AstNode::kNoNumber;
}


LInstruction* LChunkBuilder::MarkAsCall(LInstruction* instr,
                                        HInstruction* hinstr,
                                        CanDeoptimize can_deoptimize) {
#ifdef DEBUG
  instr->VerifyCall();
#endif
  instr->MarkAsCall();
  instr = AssignPointerMap(instr);

  if (hinstr->HasSideEffects()) {
    ASSERT(hinstr->next()->IsSimulate());
    HSimulate* sim = HSimulate::cast(hinstr->next());
    instr = SetInstructionPendingDeoptimizationEnvironment(
        instr, sim->ast_id());
  }

  // If instruction does not have side-effects lazy deoptimization
  // after the call will try to deoptimize to the point before the call.
  // Thus we still need to attach environment to this call even if
  // call sequence can not deoptimize eagerly.
  bool needs_environment =
      (can_deoptimize == CAN_DEOPTIMIZE_EAGERLY) || !hinstr->HasSideEffects();
  if (needs_environment && !instr->HasEnvironment()) {
    instr = AssignEnvironment(instr);
  }

  return instr;
}


LInstruction* LChunkBuilder::MarkAsSaveDoubles(LInstruction* instr) {
  instr->MarkAsSaveDoubles();
  return instr;
}


LInstruction* LChunkBuilder::AssignPointerMap(LInstruction* instr) {
  ASSERT(!instr->HasPointerMap());
  instr->set_pointer_map(new LPointerMap(position_));
  return instr;
}


LUnallocated* LChunkBuilder::TempRegister() {
  LUnallocated* operand = new LUnallocated(LUnallocated::MUST_HAVE_REGISTER);
  allocator_->RecordTemporary(operand);
  return operand;
}


LOperand* LChunkBuilder::FixedTemp(Register reg) {
  LUnallocated* operand = ToUnallocated(reg);
  allocator_->RecordTemporary(operand);
  return operand;
}


LOperand* LChunkBuilder::FixedTemp(XMMRegister reg) {
  LUnallocated* operand = ToUnallocated(reg);
  allocator_->RecordTemporary(operand);
  return operand;
}


LInstruction* LChunkBuilder::DoBlockEntry(HBlockEntry* instr) {
  return new LLabel(instr->block());
}


LInstruction* LChunkBuilder::DoSoftDeoptimize(HSoftDeoptimize* instr) {
  return AssignEnvironment(new LDeoptimize);
}


LInstruction* LChunkBuilder::DoDeoptimize(HDeoptimize* instr) {
  return AssignEnvironment(new LDeoptimize);
}


LInstruction* LChunkBuilder::DoBit(Token::Value op,
                                   HBitwiseBinaryOperation* instr) {
  if (instr->representation().IsInteger32()) {
    ASSERT(instr->left()->representation().IsInteger32());
    ASSERT(instr->right()->representation().IsInteger32());

    LOperand* left = UseRegisterAtStart(instr->LeastConstantOperand());
    LOperand* right = UseOrConstantAtStart(instr->MostConstantOperand());
    return DefineSameAsFirst(new LBitI(op, left, right));
  } else {
    ASSERT(instr->representation().IsTagged());
    ASSERT(instr->left()->representation().IsTagged());
    ASSERT(instr->right()->representation().IsTagged());

    LOperand* context = UseFixed(instr->context(), esi);
    LOperand* left = UseFixed(instr->left(), edx);
    LOperand* right = UseFixed(instr->right(), eax);
    LArithmeticT* result = new LArithmeticT(op, context, left, right);
    return MarkAsCall(DefineFixed(result, eax), instr);
  }
}


LInstruction* LChunkBuilder::DoShift(Token::Value op,
                                     HBitwiseBinaryOperation* instr) {
  if (instr->representation().IsTagged()) {
    ASSERT(instr->left()->representation().IsTagged());
    ASSERT(instr->right()->representation().IsTagged());

    LOperand* context = UseFixed(instr->context(), esi);
    LOperand* left = UseFixed(instr->left(), edx);
    LOperand* right = UseFixed(instr->right(), eax);
    LArithmeticT* result = new LArithmeticT(op, context, left, right);
    return MarkAsCall(DefineFixed(result, eax), instr);
  }

  ASSERT(instr->representation().IsInteger32());
  ASSERT(instr->left()->representation().IsInteger32());
  ASSERT(instr->right()->representation().IsInteger32());
  LOperand* left = UseRegisterAtStart(instr->left());

  HValue* right_value = instr->right();
  LOperand* right = NULL;
  int constant_value = 0;
  if (right_value->IsConstant()) {
    HConstant* constant = HConstant::cast(right_value);
    right = chunk_->DefineConstantOperand(constant);
    constant_value = constant->Integer32Value() & 0x1f;
  } else {
    right = UseFixed(right_value, ecx);
  }

  // Shift operations can only deoptimize if we do a logical shift by 0 and
  // the result cannot be truncated to int32.
  bool may_deopt = (op == Token::SHR && constant_value == 0);
  bool does_deopt = false;
  if (may_deopt) {
    for (HUseIterator it(instr->uses()); !it.Done(); it.Advance()) {
      if (!it.value()->CheckFlag(HValue::kTruncatingToInt32)) {
        does_deopt = true;
        break;
      }
    }
  }

  LInstruction* result =
      DefineSameAsFirst(new LShiftI(op, left, right, does_deopt));
  return does_deopt ? AssignEnvironment(result) : result;
}


LInstruction* LChunkBuilder::DoArithmeticD(Token::Value op,
                                           HArithmeticBinaryOperation* instr) {
  ASSERT(instr->representation().IsDouble());
  ASSERT(instr->left()->representation().IsDouble());
  ASSERT(instr->right()->representation().IsDouble());
  ASSERT(op != Token::MOD);
  LOperand* left = UseRegisterAtStart(instr->left());
  LOperand* right = UseRegisterAtStart(instr->right());
  LArithmeticD* result = new LArithmeticD(op, left, right);
  return DefineSameAsFirst(result);
}


LInstruction* LChunkBuilder::DoArithmeticT(Token::Value op,
                                           HArithmeticBinaryOperation* instr) {
  ASSERT(op == Token::ADD ||
         op == Token::DIV ||
         op == Token::MOD ||
         op == Token::MUL ||
         op == Token::SUB);
  HValue* left = instr->left();
  HValue* right = instr->right();
  ASSERT(left->representation().IsTagged());
  ASSERT(right->representation().IsTagged());
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* left_operand = UseFixed(left, edx);
  LOperand* right_operand = UseFixed(right, eax);
  LArithmeticT* result =
      new LArithmeticT(op, context, left_operand, right_operand);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


void LChunkBuilder::DoBasicBlock(HBasicBlock* block, HBasicBlock* next_block) {
  ASSERT(is_building());
  current_block_ = block;
  next_block_ = next_block;
  if (block->IsStartBlock()) {
    block->UpdateEnvironment(graph_->start_environment());
    argument_count_ = 0;
  } else if (block->predecessors()->length() == 1) {
    // We have a single predecessor => copy environment and outgoing
    // argument count from the predecessor.
    ASSERT(block->phis()->length() == 0);
    HBasicBlock* pred = block->predecessors()->at(0);
    HEnvironment* last_environment = pred->last_environment();
    ASSERT(last_environment != NULL);
    // Only copy the environment, if it is later used again.
    if (pred->end()->SecondSuccessor() == NULL) {
      ASSERT(pred->end()->FirstSuccessor() == block);
    } else {
      if (pred->end()->FirstSuccessor()->block_id() > block->block_id() ||
          pred->end()->SecondSuccessor()->block_id() > block->block_id()) {
        last_environment = last_environment->Copy();
      }
    }
    block->UpdateEnvironment(last_environment);
    ASSERT(pred->argument_count() >= 0);
    argument_count_ = pred->argument_count();
  } else {
    // We are at a state join => process phis.
    HBasicBlock* pred = block->predecessors()->at(0);
    // No need to copy the environment, it cannot be used later.
    HEnvironment* last_environment = pred->last_environment();
    for (int i = 0; i < block->phis()->length(); ++i) {
      HPhi* phi = block->phis()->at(i);
      last_environment->SetValueAt(phi->merged_index(), phi);
    }
    for (int i = 0; i < block->deleted_phis()->length(); ++i) {
      last_environment->SetValueAt(block->deleted_phis()->at(i),
                                   graph_->GetConstantUndefined());
    }
    block->UpdateEnvironment(last_environment);
    // Pick up the outgoing argument count of one of the predecessors.
    argument_count_ = pred->argument_count();
  }
  HInstruction* current = block->first();
  int start = chunk_->instructions()->length();
  while (current != NULL && !is_aborted()) {
    // Code for constants in registers is generated lazily.
    if (!current->EmitAtUses()) {
      VisitInstruction(current);
    }
    current = current->next();
  }
  int end = chunk_->instructions()->length() - 1;
  if (end >= start) {
    block->set_first_instruction_index(start);
    block->set_last_instruction_index(end);
  }
  block->set_argument_count(argument_count_);
  next_block_ = NULL;
  current_block_ = NULL;
}


void LChunkBuilder::VisitInstruction(HInstruction* current) {
  HInstruction* old_current = current_instruction_;
  current_instruction_ = current;
  if (current->has_position()) position_ = current->position();
  LInstruction* instr = current->CompileToLithium(this);

  if (instr != NULL) {
    if (FLAG_stress_pointer_maps && !instr->HasPointerMap()) {
      instr = AssignPointerMap(instr);
    }
    if (FLAG_stress_environments && !instr->HasEnvironment()) {
      instr = AssignEnvironment(instr);
    }
    instr->set_hydrogen_value(current);
    chunk_->AddInstruction(instr, current_block_);
  }
  current_instruction_ = old_current;
}


LEnvironment* LChunkBuilder::CreateEnvironment(
    HEnvironment* hydrogen_env,
    int* argument_index_accumulator) {
  if (hydrogen_env == NULL) return NULL;

  LEnvironment* outer =
      CreateEnvironment(hydrogen_env->outer(), argument_index_accumulator);
  int ast_id = hydrogen_env->ast_id();
  ASSERT(ast_id != AstNode::kNoNumber);
  int value_count = hydrogen_env->length();
  LEnvironment* result = new LEnvironment(hydrogen_env->closure(),
                                          ast_id,
                                          hydrogen_env->parameter_count(),
                                          argument_count_,
                                          value_count,
                                          outer);
  for (int i = 0; i < value_count; ++i) {
    if (hydrogen_env->is_special_index(i)) continue;

    HValue* value = hydrogen_env->values()->at(i);
    LOperand* op = NULL;
    if (value->IsArgumentsObject()) {
      op = NULL;
    } else if (value->IsPushArgument()) {
      op = new LArgument((*argument_index_accumulator)++);
    } else {
      op = UseAny(value);
    }
    result->AddValue(op, value->representation());
  }

  return result;
}


LInstruction* LChunkBuilder::DoGoto(HGoto* instr) {
  return new LGoto(instr->FirstSuccessor()->block_id());
}


LInstruction* LChunkBuilder::DoBranch(HBranch* instr) {
  HValue* v = instr->value();
  if (v->EmitAtUses()) {
    ASSERT(v->IsConstant());
    ASSERT(!v->representation().IsDouble());
    HBasicBlock* successor = HConstant::cast(v)->ToBoolean()
        ? instr->FirstSuccessor()
        : instr->SecondSuccessor();
    return new LGoto(successor->block_id());
  }
  ToBooleanStub::Types expected = instr->expected_input_types();
  // We need a temporary register when we have to access the map *or* we have
  // no type info yet, in which case we handle all cases (including the ones
  // involving maps).
  bool needs_temp = expected.NeedsMap() || expected.IsEmpty();
  LOperand* temp = needs_temp ? TempRegister() : NULL;
  return AssignEnvironment(new LBranch(UseRegister(v), temp));
}


LInstruction* LChunkBuilder::DoCompareMap(HCompareMap* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  LOperand* value = UseRegisterAtStart(instr->value());
  return new LCmpMapAndBranch(value);
}


LInstruction* LChunkBuilder::DoArgumentsLength(HArgumentsLength* length) {
  return DefineAsRegister(new LArgumentsLength(Use(length->value())));
}


LInstruction* LChunkBuilder::DoArgumentsElements(HArgumentsElements* elems) {
  return DefineAsRegister(new LArgumentsElements);
}


LInstruction* LChunkBuilder::DoInstanceOf(HInstanceOf* instr) {
  LOperand* left = UseFixed(instr->left(), InstanceofStub::left());
  LOperand* right = UseFixed(instr->right(), InstanceofStub::right());
  LOperand* context = UseFixed(instr->context(), esi);
  LInstanceOf* result = new LInstanceOf(context, left, right);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoInstanceOfKnownGlobal(
    HInstanceOfKnownGlobal* instr) {
  LInstanceOfKnownGlobal* result =
      new LInstanceOfKnownGlobal(
          UseFixed(instr->context(), esi),
          UseFixed(instr->left(), InstanceofStub::left()),
          FixedTemp(edi));
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoApplyArguments(HApplyArguments* instr) {
  LOperand* function = UseFixed(instr->function(), edi);
  LOperand* receiver = UseFixed(instr->receiver(), eax);
  LOperand* length = UseFixed(instr->length(), ebx);
  LOperand* elements = UseFixed(instr->elements(), ecx);
  LOperand* temp = FixedTemp(edx);
  LApplyArguments* result = new LApplyArguments(function,
                                                receiver,
                                                length,
                                                elements,
                                                temp);
  return MarkAsCall(DefineFixed(result, eax), instr, CAN_DEOPTIMIZE_EAGERLY);
}


LInstruction* LChunkBuilder::DoPushArgument(HPushArgument* instr) {
  ++argument_count_;
  LOperand* argument = UseAny(instr->argument());
  return new LPushArgument(argument);
}


LInstruction* LChunkBuilder::DoThisFunction(HThisFunction* instr) {
  return instr->HasNoUses() ? NULL : DefineAsRegister(new LThisFunction);
}


LInstruction* LChunkBuilder::DoContext(HContext* instr) {
  return instr->HasNoUses() ? NULL : DefineAsRegister(new LContext);
}


LInstruction* LChunkBuilder::DoOuterContext(HOuterContext* instr) {
  LOperand* context = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new LOuterContext(context));
}


LInstruction* LChunkBuilder::DoGlobalObject(HGlobalObject* instr) {
  LOperand* context = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new LGlobalObject(context));
}


LInstruction* LChunkBuilder::DoGlobalReceiver(HGlobalReceiver* instr) {
  LOperand* global_object = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new LGlobalReceiver(global_object));
}


LInstruction* LChunkBuilder::DoCallConstantFunction(
    HCallConstantFunction* instr) {
  argument_count_ -= instr->argument_count();
  return MarkAsCall(DefineFixed(new LCallConstantFunction, eax), instr);
}


LInstruction* LChunkBuilder::DoInvokeFunction(HInvokeFunction* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* function = UseFixed(instr->function(), edi);
  argument_count_ -= instr->argument_count();
  LInvokeFunction* result = new LInvokeFunction(context, function);
  return MarkAsCall(DefineFixed(result, eax), instr, CANNOT_DEOPTIMIZE_EAGERLY);
}


LInstruction* LChunkBuilder::DoUnaryMathOperation(HUnaryMathOperation* instr) {
  BuiltinFunctionId op = instr->op();
  if (op == kMathLog) {
    ASSERT(instr->representation().IsDouble());
    ASSERT(instr->value()->representation().IsDouble());
    LOperand* context = UseAny(instr->context());  // Not actually used.
    LOperand* input = UseRegisterAtStart(instr->value());
    LUnaryMathOperation* result = new LUnaryMathOperation(context, input);
    return DefineSameAsFirst(result);
  } else if (op == kMathSin || op == kMathCos) {
    LOperand* context = UseFixed(instr->context(), esi);
    LOperand* input = UseFixedDouble(instr->value(), xmm1);
    LUnaryMathOperation* result = new LUnaryMathOperation(context, input);
    return MarkAsCall(DefineFixedDouble(result, xmm1), instr);
  } else {
    LOperand* input = UseRegisterAtStart(instr->value());
    LOperand* context = UseAny(instr->context());  // Deferred use by MathAbs.
    LUnaryMathOperation* result = new LUnaryMathOperation(context, input);
    switch (op) {
      case kMathAbs:
        return AssignEnvironment(AssignPointerMap(DefineSameAsFirst(result)));
      case kMathFloor:
        return AssignEnvironment(DefineAsRegister(result));
      case kMathRound:
        return AssignEnvironment(DefineAsRegister(result));
      case kMathSqrt:
        return DefineSameAsFirst(result);
      case kMathPowHalf:
        return DefineSameAsFirst(result);
      default:
        UNREACHABLE();
        return NULL;
    }
  }
}


LInstruction* LChunkBuilder::DoCallKeyed(HCallKeyed* instr) {
  ASSERT(instr->key()->representation().IsTagged());
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* key = UseFixed(instr->key(), ecx);
  argument_count_ -= instr->argument_count();
  LCallKeyed* result = new LCallKeyed(context, key);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoCallNamed(HCallNamed* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  argument_count_ -= instr->argument_count();
  LCallNamed* result = new LCallNamed(context);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoCallGlobal(HCallGlobal* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  argument_count_ -= instr->argument_count();
  LCallGlobal* result = new LCallGlobal(context);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoCallKnownGlobal(HCallKnownGlobal* instr) {
  argument_count_ -= instr->argument_count();
  return MarkAsCall(DefineFixed(new LCallKnownGlobal, eax), instr);
}


LInstruction* LChunkBuilder::DoCallNew(HCallNew* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* constructor = UseFixed(instr->constructor(), edi);
  argument_count_ -= instr->argument_count();
  LCallNew* result = new LCallNew(context, constructor);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoCallFunction(HCallFunction* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  argument_count_ -= instr->argument_count();
  LCallFunction* result = new LCallFunction(context);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoCallRuntime(HCallRuntime* instr) {
  argument_count_ -= instr->argument_count();
  LOperand* context = UseFixed(instr->context(), esi);
  return MarkAsCall(DefineFixed(new LCallRuntime(context), eax), instr);
}


LInstruction* LChunkBuilder::DoShr(HShr* instr) {
  return DoShift(Token::SHR, instr);
}


LInstruction* LChunkBuilder::DoSar(HSar* instr) {
  return DoShift(Token::SAR, instr);
}


LInstruction* LChunkBuilder::DoShl(HShl* instr) {
  return DoShift(Token::SHL, instr);
}


LInstruction* LChunkBuilder::DoBitAnd(HBitAnd* instr) {
  return DoBit(Token::BIT_AND, instr);
}


LInstruction* LChunkBuilder::DoBitNot(HBitNot* instr) {
  ASSERT(instr->value()->representation().IsInteger32());
  ASSERT(instr->representation().IsInteger32());
  LOperand* input = UseRegisterAtStart(instr->value());
  LBitNotI* result = new LBitNotI(input);
  return DefineSameAsFirst(result);
}


LInstruction* LChunkBuilder::DoBitOr(HBitOr* instr) {
  return DoBit(Token::BIT_OR, instr);
}


LInstruction* LChunkBuilder::DoBitXor(HBitXor* instr) {
  return DoBit(Token::BIT_XOR, instr);
}


LInstruction* LChunkBuilder::DoDiv(HDiv* instr) {
  if (instr->representation().IsDouble()) {
    return DoArithmeticD(Token::DIV, instr);
  } else if (instr->representation().IsInteger32()) {
    // The temporary operand is necessary to ensure that right is not allocated
    // into edx.
    LOperand* temp = FixedTemp(edx);
    LOperand* dividend = UseFixed(instr->left(), eax);
    LOperand* divisor = UseRegister(instr->right());
    LDivI* result = new LDivI(dividend, divisor, temp);
    return AssignEnvironment(DefineFixed(result, eax));
  } else {
    ASSERT(instr->representation().IsTagged());
    return DoArithmeticT(Token::DIV, instr);
  }
}


LInstruction* LChunkBuilder::DoMod(HMod* instr) {
  if (instr->representation().IsInteger32()) {
    ASSERT(instr->left()->representation().IsInteger32());
    ASSERT(instr->right()->representation().IsInteger32());

    LInstruction* result;
    if (instr->HasPowerOf2Divisor()) {
      ASSERT(!instr->CheckFlag(HValue::kCanBeDivByZero));
      LOperand* value = UseRegisterAtStart(instr->left());
      LModI* mod = new LModI(value, UseOrConstant(instr->right()), NULL);
      result = DefineSameAsFirst(mod);
    } else {
      // The temporary operand is necessary to ensure that right is
      // not allocated into edx.
      LOperand* temp = FixedTemp(edx);
      LOperand* value = UseFixed(instr->left(), eax);
      LOperand* divisor = UseRegister(instr->right());
      LModI* mod = new LModI(value, divisor, temp);
      result = DefineFixed(mod, edx);
    }

    return (instr->CheckFlag(HValue::kBailoutOnMinusZero) ||
            instr->CheckFlag(HValue::kCanBeDivByZero))
        ? AssignEnvironment(result)
        : result;
  } else if (instr->representation().IsTagged()) {
    return DoArithmeticT(Token::MOD, instr);
  } else {
    ASSERT(instr->representation().IsDouble());
    // We call a C function for double modulo. It can't trigger a GC.
    // We need to use fixed result register for the call.
    // TODO(fschneider): Allow any register as input registers.
    LOperand* left = UseFixedDouble(instr->left(), xmm2);
    LOperand* right = UseFixedDouble(instr->right(), xmm1);
    LArithmeticD* result = new LArithmeticD(Token::MOD, left, right);
    return MarkAsCall(DefineFixedDouble(result, xmm1), instr);
  }
}


LInstruction* LChunkBuilder::DoMul(HMul* instr) {
  if (instr->representation().IsInteger32()) {
    ASSERT(instr->left()->representation().IsInteger32());
    ASSERT(instr->right()->representation().IsInteger32());
    LOperand* left = UseRegisterAtStart(instr->LeastConstantOperand());
    LOperand* right = UseOrConstant(instr->MostConstantOperand());
    LOperand* temp = NULL;
    if (instr->CheckFlag(HValue::kBailoutOnMinusZero)) {
      temp = TempRegister();
    }
    LMulI* mul = new LMulI(left, right, temp);
    return AssignEnvironment(DefineSameAsFirst(mul));
  } else if (instr->representation().IsDouble()) {
    return DoArithmeticD(Token::MUL, instr);
  } else {
    ASSERT(instr->representation().IsTagged());
    return DoArithmeticT(Token::MUL, instr);
  }
}


LInstruction* LChunkBuilder::DoSub(HSub* instr) {
  if (instr->representation().IsInteger32()) {
    ASSERT(instr->left()->representation().IsInteger32());
    ASSERT(instr->right()->representation().IsInteger32());
    LOperand* left = UseRegisterAtStart(instr->left());
    LOperand* right = UseOrConstantAtStart(instr->right());
    LSubI* sub = new LSubI(left, right);
    LInstruction* result = DefineSameAsFirst(sub);
    if (instr->CheckFlag(HValue::kCanOverflow)) {
      result = AssignEnvironment(result);
    }
    return result;
  } else if (instr->representation().IsDouble()) {
    return DoArithmeticD(Token::SUB, instr);
  } else {
    ASSERT(instr->representation().IsTagged());
    return DoArithmeticT(Token::SUB, instr);
  }
}


LInstruction* LChunkBuilder::DoAdd(HAdd* instr) {
  if (instr->representation().IsInteger32()) {
    ASSERT(instr->left()->representation().IsInteger32());
    ASSERT(instr->right()->representation().IsInteger32());
    LOperand* left = UseRegisterAtStart(instr->LeastConstantOperand());
    LOperand* right = UseOrConstantAtStart(instr->MostConstantOperand());
    LAddI* add = new LAddI(left, right);
    LInstruction* result = DefineSameAsFirst(add);
    if (instr->CheckFlag(HValue::kCanOverflow)) {
      result = AssignEnvironment(result);
    }
    return result;
  } else if (instr->representation().IsDouble()) {
    return DoArithmeticD(Token::ADD, instr);
  } else {
    ASSERT(instr->representation().IsTagged());
    return DoArithmeticT(Token::ADD, instr);
  }
}


LInstruction* LChunkBuilder::DoPower(HPower* instr) {
  ASSERT(instr->representation().IsDouble());
  // We call a C function for double power. It can't trigger a GC.
  // We need to use fixed result register for the call.
  Representation exponent_type = instr->right()->representation();
  ASSERT(instr->left()->representation().IsDouble());
  LOperand* left = UseFixedDouble(instr->left(), xmm1);
  LOperand* right = exponent_type.IsDouble() ?
      UseFixedDouble(instr->right(), xmm2) :
      UseFixed(instr->right(), eax);
  LPower* result = new LPower(left, right);
  return MarkAsCall(DefineFixedDouble(result, xmm3), instr,
                    CAN_DEOPTIMIZE_EAGERLY);
}


LInstruction* LChunkBuilder::DoCompareGeneric(HCompareGeneric* instr) {
  Token::Value op = instr->token();
  ASSERT(instr->left()->representation().IsTagged());
  ASSERT(instr->right()->representation().IsTagged());
  bool reversed = (op == Token::GT || op == Token::LTE);
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* left = UseFixed(instr->left(), reversed ? eax : edx);
  LOperand* right = UseFixed(instr->right(), reversed ? edx : eax);
  LCmpT* result = new LCmpT(context, left, right);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoCompareIDAndBranch(
    HCompareIDAndBranch* instr) {
  Representation r = instr->GetInputRepresentation();
  if (r.IsInteger32()) {
    ASSERT(instr->left()->representation().IsInteger32());
    ASSERT(instr->right()->representation().IsInteger32());
    LOperand* left = UseRegisterAtStart(instr->left());
    LOperand* right = UseOrConstantAtStart(instr->right());
    return new LCmpIDAndBranch(left, right);
  } else {
    ASSERT(r.IsDouble());
    ASSERT(instr->left()->representation().IsDouble());
    ASSERT(instr->right()->representation().IsDouble());
    LOperand* left = UseRegisterAtStart(instr->left());
    LOperand* right = UseRegisterAtStart(instr->right());
    return new LCmpIDAndBranch(left, right);
  }
}


LInstruction* LChunkBuilder::DoCompareObjectEqAndBranch(
    HCompareObjectEqAndBranch* instr) {
  LOperand* left = UseRegisterAtStart(instr->left());
  LOperand* right = UseAtStart(instr->right());
  return new LCmpObjectEqAndBranch(left, right);
}


LInstruction* LChunkBuilder::DoCompareConstantEqAndBranch(
  HCompareConstantEqAndBranch* instr) {
  return new LCmpConstantEqAndBranch(UseRegisterAtStart(instr->value()));
}


LInstruction* LChunkBuilder::DoIsNilAndBranch(HIsNilAndBranch* instr) {
  // We only need a temp register for non-strict compare.
  LOperand* temp = instr->kind() == kStrictEquality ? NULL : TempRegister();
  return new LIsNilAndBranch(UseRegisterAtStart(instr->value()), temp);
}


LInstruction* LChunkBuilder::DoIsObjectAndBranch(HIsObjectAndBranch* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  LOperand* temp = TempRegister();
  return new LIsObjectAndBranch(UseRegister(instr->value()), temp);
}


LInstruction* LChunkBuilder::DoIsSmiAndBranch(HIsSmiAndBranch* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  return new LIsSmiAndBranch(Use(instr->value()));
}


LInstruction* LChunkBuilder::DoIsUndetectableAndBranch(
    HIsUndetectableAndBranch* instr) {
  ASSERT(instr  ->value()->representation().IsTagged());
  return new LIsUndetectableAndBranch(UseRegisterAtStart(instr->value()),
                                      TempRegister());
}


LInstruction* LChunkBuilder::DoHasInstanceTypeAndBranch(
    HHasInstanceTypeAndBranch* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  return new LHasInstanceTypeAndBranch(UseRegisterAtStart(instr->value()),
                                       TempRegister());
}


LInstruction* LChunkBuilder::DoGetCachedArrayIndex(
    HGetCachedArrayIndex* instr)  {
  ASSERT(instr->value()->representation().IsTagged());
  LOperand* value = UseRegisterAtStart(instr->value());

  return DefineAsRegister(new LGetCachedArrayIndex(value));
}


LInstruction* LChunkBuilder::DoHasCachedArrayIndexAndBranch(
    HHasCachedArrayIndexAndBranch* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  return new LHasCachedArrayIndexAndBranch(
      UseRegisterAtStart(instr->value()));
}


LInstruction* LChunkBuilder::DoClassOfTestAndBranch(
    HClassOfTestAndBranch* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  return new LClassOfTestAndBranch(UseTempRegister(instr->value()),
                                   TempRegister(),
                                   TempRegister());
}


LInstruction* LChunkBuilder::DoJSArrayLength(HJSArrayLength* instr) {
  LOperand* array = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new LJSArrayLength(array));
}


LInstruction* LChunkBuilder::DoFixedArrayBaseLength(
    HFixedArrayBaseLength* instr) {
  LOperand* array = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new LFixedArrayBaseLength(array));
}


LInstruction* LChunkBuilder::DoElementsKind(HElementsKind* instr) {
  LOperand* object = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new LElementsKind(object));
}


LInstruction* LChunkBuilder::DoValueOf(HValueOf* instr) {
  LOperand* object = UseRegister(instr->value());
  LValueOf* result = new LValueOf(object, TempRegister());
  return AssignEnvironment(DefineSameAsFirst(result));
}


LInstruction* LChunkBuilder::DoBoundsCheck(HBoundsCheck* instr) {
  return AssignEnvironment(new LBoundsCheck(
      UseRegisterOrConstantAtStart(instr->index()),
      UseAtStart(instr->length())));
}


LInstruction* LChunkBuilder::DoAbnormalExit(HAbnormalExit* instr) {
  // The control instruction marking the end of a block that completed
  // abruptly (e.g., threw an exception).  There is nothing specific to do.
  return NULL;
}


LInstruction* LChunkBuilder::DoThrow(HThrow* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* value = UseFixed(instr->value(), eax);
  return MarkAsCall(new LThrow(context, value), instr);
}


LInstruction* LChunkBuilder::DoUseConst(HUseConst* instr) {
  return NULL;
}


LInstruction* LChunkBuilder::DoForceRepresentation(HForceRepresentation* bad) {
  // All HForceRepresentation instructions should be eliminated in the
  // representation change phase of Hydrogen.
  UNREACHABLE();
  return NULL;
}


LInstruction* LChunkBuilder::DoChange(HChange* instr) {
  Representation from = instr->from();
  Representation to = instr->to();
  if (from.IsTagged()) {
    if (to.IsDouble()) {
      LOperand* value = UseRegister(instr->value());
      LNumberUntagD* res = new LNumberUntagD(value);
      return AssignEnvironment(DefineAsRegister(res));
    } else {
      ASSERT(to.IsInteger32());
      LOperand* value = UseRegister(instr->value());
      bool needs_check = !instr->value()->type().IsSmi();
      if (needs_check) {
        bool truncating = instr->CanTruncateToInt32();
        LOperand* xmm_temp =
            (truncating && CpuFeatures::IsSupported(SSE3))
            ? NULL
            : FixedTemp(xmm1);
        LTaggedToI* res = new LTaggedToI(value, xmm_temp);
        return AssignEnvironment(DefineSameAsFirst(res));
      } else {
        return DefineSameAsFirst(new LSmiUntag(value, needs_check));
      }
    }
  } else if (from.IsDouble()) {
    if (to.IsTagged()) {
      LOperand* value = UseRegister(instr->value());
      LOperand* temp = TempRegister();

      // Make sure that temp and result_temp are different registers.
      LUnallocated* result_temp = TempRegister();
      LNumberTagD* result = new LNumberTagD(value, temp);
      return AssignPointerMap(Define(result, result_temp));
    } else {
      ASSERT(to.IsInteger32());
      bool truncating = instr->CanTruncateToInt32();
      bool needs_temp = truncating && !CpuFeatures::IsSupported(SSE3);
      LOperand* value = needs_temp ?
          UseTempRegister(instr->value()) : UseRegister(instr->value());
      LOperand* temp = needs_temp ? TempRegister() : NULL;
      return AssignEnvironment(DefineAsRegister(new LDoubleToI(value, temp)));
    }
  } else if (from.IsInteger32()) {
    if (to.IsTagged()) {
      HValue* val = instr->value();
      LOperand* value = UseRegister(val);
      if (val->HasRange() && val->range()->IsInSmiRange()) {
        return DefineSameAsFirst(new LSmiTag(value));
      } else {
        LNumberTagI* result = new LNumberTagI(value);
        return AssignEnvironment(AssignPointerMap(DefineSameAsFirst(result)));
      }
    } else {
      ASSERT(to.IsDouble());
      return DefineAsRegister(new LInteger32ToDouble(Use(instr->value())));
    }
  }
  UNREACHABLE();
  return NULL;
}


LInstruction* LChunkBuilder::DoCheckNonSmi(HCheckNonSmi* instr) {
  LOperand* value = UseAtStart(instr->value());
  return AssignEnvironment(new LCheckNonSmi(value));
}


LInstruction* LChunkBuilder::DoCheckInstanceType(HCheckInstanceType* instr) {
  LOperand* value = UseRegisterAtStart(instr->value());
  LOperand* temp = TempRegister();
  LCheckInstanceType* result = new LCheckInstanceType(value, temp);
  return AssignEnvironment(result);
}


LInstruction* LChunkBuilder::DoCheckPrototypeMaps(HCheckPrototypeMaps* instr) {
  LOperand* temp = TempRegister();
  LCheckPrototypeMaps* result = new LCheckPrototypeMaps(temp);
  return AssignEnvironment(result);
}


LInstruction* LChunkBuilder::DoCheckSmi(HCheckSmi* instr) {
  LOperand* value = UseAtStart(instr->value());
  return AssignEnvironment(new LCheckSmi(value));
}


LInstruction* LChunkBuilder::DoCheckFunction(HCheckFunction* instr) {
  // If the target is in new space, we'll emit a global cell compare and so
  // want the value in a register.  If the target gets promoted before we
  // emit code, we will still get the register but will do an immediate
  // compare instead of the cell compare.  This is safe.
  LOperand* value = Isolate::Current()->heap()->InNewSpace(*instr->target())
      ? UseRegisterAtStart(instr->value())
      : UseAtStart(instr->value());
  return AssignEnvironment(new LCheckFunction(value));
}


LInstruction* LChunkBuilder::DoCheckMap(HCheckMap* instr) {
  LOperand* value = UseRegisterAtStart(instr->value());
  LCheckMap* result = new LCheckMap(value);
  return AssignEnvironment(result);
}


LInstruction* LChunkBuilder::DoClampToUint8(HClampToUint8* instr) {
  HValue* value = instr->value();
  Representation input_rep = value->representation();
  if (input_rep.IsDouble()) {
    LOperand* reg = UseRegister(value);
    return DefineAsRegister(new LClampDToUint8(reg));
  } else if (input_rep.IsInteger32()) {
    LOperand* reg = UseFixed(value, eax);
    return DefineFixed(new LClampIToUint8(reg), eax);
  } else {
    ASSERT(input_rep.IsTagged());
    LOperand* reg = UseFixed(value, eax);
    // Register allocator doesn't (yet) support allocation of double
    // temps. Reserve xmm1 explicitly.
    LOperand* temp = FixedTemp(xmm1);
    LClampTToUint8* result = new LClampTToUint8(reg, temp);
    return AssignEnvironment(DefineFixed(result, eax));
  }
}


LInstruction* LChunkBuilder::DoToInt32(HToInt32* instr) {
  HValue* value = instr->value();
  Representation input_rep = value->representation();

  LInstruction* result;
  if (input_rep.IsDouble()) {
    LOperand* reg = UseRegister(value);
    LOperand* temp_reg =
        CpuFeatures::IsSupported(SSE3) ? NULL : TempRegister();
    result = DefineAsRegister(new LDoubleToI(reg, temp_reg));
  } else if (input_rep.IsInteger32()) {
    // Canonicalization should already have removed the hydrogen instruction in
    // this case, since it is a noop.
    UNREACHABLE();
    return NULL;
  } else {
    ASSERT(input_rep.IsTagged());
    LOperand* reg = UseRegister(value);
    // Register allocator doesn't (yet) support allocation of double
    // temps. Reserve xmm1 explicitly.
    LOperand* xmm_temp =
        CpuFeatures::IsSupported(SSE3) ? NULL : FixedTemp(xmm1);
    result = DefineSameAsFirst(new LTaggedToI(reg, xmm_temp));
  }
  return AssignEnvironment(result);
}


LInstruction* LChunkBuilder::DoReturn(HReturn* instr) {
  return new LReturn(UseFixed(instr->value(), eax));
}


LInstruction* LChunkBuilder::DoConstant(HConstant* instr) {
  Representation r = instr->representation();
  if (r.IsInteger32()) {
    return DefineAsRegister(new LConstantI);
  } else if (r.IsDouble()) {
    double value = instr->DoubleValue();
    LOperand* temp = (BitCast<uint64_t, double>(value) != 0)
        ? TempRegister()
        : NULL;
    return DefineAsRegister(new LConstantD(temp));
  } else if (r.IsTagged()) {
    return DefineAsRegister(new LConstantT);
  } else {
    UNREACHABLE();
    return NULL;
  }
}


LInstruction* LChunkBuilder::DoLoadGlobalCell(HLoadGlobalCell* instr) {
  LLoadGlobalCell* result = new LLoadGlobalCell;
  return instr->RequiresHoleCheck()
      ? AssignEnvironment(DefineAsRegister(result))
      : DefineAsRegister(result);
}


LInstruction* LChunkBuilder::DoLoadGlobalGeneric(HLoadGlobalGeneric* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* global_object = UseFixed(instr->global_object(), eax);
  LLoadGlobalGeneric* result = new LLoadGlobalGeneric(context, global_object);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoStoreGlobalCell(HStoreGlobalCell* instr) {
  LStoreGlobalCell* result =
      new LStoreGlobalCell(UseTempRegister(instr->value()),
                           TempRegister(),
                           TempRegister());
  return instr->RequiresHoleCheck() ? AssignEnvironment(result) : result;
}


LInstruction* LChunkBuilder::DoStoreGlobalGeneric(HStoreGlobalGeneric* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* global_object = UseFixed(instr->global_object(), edx);
  LOperand* value = UseFixed(instr->value(), eax);
  LStoreGlobalGeneric* result =
      new LStoreGlobalGeneric(context, global_object, value);
  return MarkAsCall(result, instr);
}


LInstruction* LChunkBuilder::DoLoadContextSlot(HLoadContextSlot* instr) {
  LOperand* context = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new LLoadContextSlot(context));
}


LInstruction* LChunkBuilder::DoStoreContextSlot(HStoreContextSlot* instr) {
  LOperand* value;
  LOperand* temp;
  LOperand* context = UseRegister(instr->context());
  if (instr->NeedsWriteBarrier()) {
    value = UseTempRegister(instr->value());
    temp = TempRegister();
  } else {
    value = UseRegister(instr->value());
    temp = NULL;
  }
  return new LStoreContextSlot(context, value, temp);
}


LInstruction* LChunkBuilder::DoLoadNamedField(HLoadNamedField* instr) {
  ASSERT(instr->representation().IsTagged());
  LOperand* obj = UseRegisterAtStart(instr->object());
  return DefineAsRegister(new LLoadNamedField(obj));
}


LInstruction* LChunkBuilder::DoLoadNamedFieldPolymorphic(
    HLoadNamedFieldPolymorphic* instr) {
  ASSERT(instr->representation().IsTagged());
  LOperand* context = UseFixed(instr->context(), esi);
  if (instr->need_generic()) {
    LOperand* obj = UseFixed(instr->object(), eax);
    LLoadNamedFieldPolymorphic* result =
        new LLoadNamedFieldPolymorphic(context, obj);
    return MarkAsCall(DefineFixed(result, eax), instr);
  } else {
    LOperand* obj = UseRegisterAtStart(instr->object());
    LLoadNamedFieldPolymorphic* result =
        new LLoadNamedFieldPolymorphic(context, obj);
    return AssignEnvironment(DefineAsRegister(result));
  }
}


LInstruction* LChunkBuilder::DoLoadNamedGeneric(HLoadNamedGeneric* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* object = UseFixed(instr->object(), eax);
  LLoadNamedGeneric* result = new LLoadNamedGeneric(context, object);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoLoadFunctionPrototype(
    HLoadFunctionPrototype* instr) {
  return AssignEnvironment(DefineAsRegister(
      new LLoadFunctionPrototype(UseRegister(instr->function()),
                                 TempRegister())));
}


LInstruction* LChunkBuilder::DoLoadElements(HLoadElements* instr) {
  LOperand* input = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new LLoadElements(input));
}


LInstruction* LChunkBuilder::DoLoadExternalArrayPointer(
    HLoadExternalArrayPointer* instr) {
  LOperand* input = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new LLoadExternalArrayPointer(input));
}


LInstruction* LChunkBuilder::DoLoadKeyedFastElement(
    HLoadKeyedFastElement* instr) {
  ASSERT(instr->representation().IsTagged());
  ASSERT(instr->key()->representation().IsInteger32());
  LOperand* obj = UseRegisterAtStart(instr->object());
  LOperand* key = UseRegisterOrConstantAtStart(instr->key());
  LLoadKeyedFastElement* result = new LLoadKeyedFastElement(obj, key);
  return AssignEnvironment(DefineAsRegister(result));
}


LInstruction* LChunkBuilder::DoLoadKeyedFastDoubleElement(
    HLoadKeyedFastDoubleElement* instr) {
  ASSERT(instr->representation().IsDouble());
  ASSERT(instr->key()->representation().IsInteger32());
  LOperand* elements = UseRegisterAtStart(instr->elements());
  LOperand* key = UseRegisterOrConstantAtStart(instr->key());
  LLoadKeyedFastDoubleElement* result =
      new LLoadKeyedFastDoubleElement(elements, key);
  return AssignEnvironment(DefineAsRegister(result));
}


LInstruction* LChunkBuilder::DoLoadKeyedSpecializedArrayElement(
    HLoadKeyedSpecializedArrayElement* instr) {
  ElementsKind elements_kind = instr->elements_kind();
  Representation representation(instr->representation());
  ASSERT(
      (representation.IsInteger32() &&
       (elements_kind != EXTERNAL_FLOAT_ELEMENTS) &&
       (elements_kind != EXTERNAL_DOUBLE_ELEMENTS)) ||
      (representation.IsDouble() &&
       ((elements_kind == EXTERNAL_FLOAT_ELEMENTS) ||
       (elements_kind == EXTERNAL_DOUBLE_ELEMENTS))));
  ASSERT(instr->key()->representation().IsInteger32());
  LOperand* external_pointer = UseRegister(instr->external_pointer());
  LOperand* key = UseRegisterOrConstant(instr->key());
  LLoadKeyedSpecializedArrayElement* result =
      new LLoadKeyedSpecializedArrayElement(external_pointer,
                                            key);
  LInstruction* load_instr = DefineAsRegister(result);
  // An unsigned int array load might overflow and cause a deopt, make sure it
  // has an environment.
  return (elements_kind == EXTERNAL_UNSIGNED_INT_ELEMENTS)
      ? AssignEnvironment(load_instr)
      : load_instr;
}


LInstruction* LChunkBuilder::DoLoadKeyedGeneric(HLoadKeyedGeneric* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* object = UseFixed(instr->object(), edx);
  LOperand* key = UseFixed(instr->key(), eax);

  LLoadKeyedGeneric* result = new LLoadKeyedGeneric(context, object, key);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoStoreKeyedFastElement(
    HStoreKeyedFastElement* instr) {
  bool needs_write_barrier = instr->NeedsWriteBarrier();
  ASSERT(instr->value()->representation().IsTagged());
  ASSERT(instr->object()->representation().IsTagged());
  ASSERT(instr->key()->representation().IsInteger32());

  LOperand* obj = UseRegister(instr->object());
  LOperand* val = needs_write_barrier
      ? UseTempRegister(instr->value())
      : UseRegisterAtStart(instr->value());
  LOperand* key = needs_write_barrier
      ? UseTempRegister(instr->key())
      : UseRegisterOrConstantAtStart(instr->key());

  return AssignEnvironment(new LStoreKeyedFastElement(obj, key, val));
}


LInstruction* LChunkBuilder::DoStoreKeyedFastDoubleElement(
    HStoreKeyedFastDoubleElement* instr) {
  ASSERT(instr->value()->representation().IsDouble());
  ASSERT(instr->elements()->representation().IsTagged());
  ASSERT(instr->key()->representation().IsInteger32());

  LOperand* elements = UseRegisterAtStart(instr->elements());
  LOperand* val = UseTempRegister(instr->value());
  LOperand* key = UseRegisterOrConstantAtStart(instr->key());

  return new LStoreKeyedFastDoubleElement(elements, key, val);
}


LInstruction* LChunkBuilder::DoStoreKeyedSpecializedArrayElement(
    HStoreKeyedSpecializedArrayElement* instr) {
  Representation representation(instr->value()->representation());
  ElementsKind elements_kind = instr->elements_kind();
    ASSERT(
      (representation.IsInteger32() &&
       (elements_kind != EXTERNAL_FLOAT_ELEMENTS) &&
       (elements_kind != EXTERNAL_DOUBLE_ELEMENTS)) ||
      (representation.IsDouble() &&
       ((elements_kind == EXTERNAL_FLOAT_ELEMENTS) ||
       (elements_kind == EXTERNAL_DOUBLE_ELEMENTS))));
  ASSERT(instr->external_pointer()->representation().IsExternal());
  ASSERT(instr->key()->representation().IsInteger32());

  LOperand* external_pointer = UseRegister(instr->external_pointer());
  LOperand* key = UseRegisterOrConstant(instr->key());
  LOperand* val = NULL;
  if (elements_kind == EXTERNAL_BYTE_ELEMENTS ||
      elements_kind == EXTERNAL_UNSIGNED_BYTE_ELEMENTS ||
      elements_kind == EXTERNAL_PIXEL_ELEMENTS) {
    // We need a byte register in this case for the value.
    val = UseFixed(instr->value(), eax);
  } else {
    val = UseRegister(instr->value());
  }

  return new LStoreKeyedSpecializedArrayElement(external_pointer,
                                                key,
                                                val);
}


LInstruction* LChunkBuilder::DoStoreKeyedGeneric(HStoreKeyedGeneric* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* object = UseFixed(instr->object(), edx);
  LOperand* key = UseFixed(instr->key(), ecx);
  LOperand* value = UseFixed(instr->value(), eax);

  ASSERT(instr->object()->representation().IsTagged());
  ASSERT(instr->key()->representation().IsTagged());
  ASSERT(instr->value()->representation().IsTagged());

  LStoreKeyedGeneric* result =
      new LStoreKeyedGeneric(context, object, key, value);
  return MarkAsCall(result, instr);
}


LInstruction* LChunkBuilder::DoStoreNamedField(HStoreNamedField* instr) {
  bool needs_write_barrier = instr->NeedsWriteBarrier();

  LOperand* obj;
  if (needs_write_barrier) {
    obj = instr->is_in_object()
        ? UseRegister(instr->object())
        : UseTempRegister(instr->object());
  } else {
    obj = UseRegisterAtStart(instr->object());
  }

  LOperand* val = needs_write_barrier
      ? UseTempRegister(instr->value())
      : UseRegister(instr->value());

  // We only need a scratch register if we have a write barrier or we
  // have a store into the properties array (not in-object-property).
  LOperand* temp = (!instr->is_in_object() || needs_write_barrier)
      ? TempRegister()
      : NULL;

  return new LStoreNamedField(obj, val, temp);
}


LInstruction* LChunkBuilder::DoStoreNamedGeneric(HStoreNamedGeneric* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* object = UseFixed(instr->object(), edx);
  LOperand* value = UseFixed(instr->value(), eax);

  LStoreNamedGeneric* result = new LStoreNamedGeneric(context, object, value);
  return MarkAsCall(result, instr);
}


LInstruction* LChunkBuilder::DoStringAdd(HStringAdd* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* left = UseOrConstantAtStart(instr->left());
  LOperand* right = UseOrConstantAtStart(instr->right());
  LStringAdd* string_add = new LStringAdd(context, left, right);
  return MarkAsCall(DefineFixed(string_add, eax), instr);
}


LInstruction* LChunkBuilder::DoStringCharCodeAt(HStringCharCodeAt* instr) {
  LOperand* string = UseTempRegister(instr->string());
  LOperand* index = UseTempRegister(instr->index());
  LOperand* context = UseAny(instr->context());
  LStringCharCodeAt* result = new LStringCharCodeAt(context, string, index);
  return AssignEnvironment(AssignPointerMap(DefineAsRegister(result)));
}


LInstruction* LChunkBuilder::DoStringCharFromCode(HStringCharFromCode* instr) {
  LOperand* char_code = UseRegister(instr->value());
  LOperand* context = UseAny(instr->context());
  LStringCharFromCode* result = new LStringCharFromCode(context, char_code);
  return AssignPointerMap(DefineAsRegister(result));
}


LInstruction* LChunkBuilder::DoStringLength(HStringLength* instr) {
  LOperand* string = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new LStringLength(string));
}


LInstruction* LChunkBuilder::DoArrayLiteral(HArrayLiteral* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  return MarkAsCall(DefineFixed(new LArrayLiteral(context), eax), instr);
}


LInstruction* LChunkBuilder::DoObjectLiteral(HObjectLiteral* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  return MarkAsCall(DefineFixed(new LObjectLiteral(context), eax), instr);
}


LInstruction* LChunkBuilder::DoRegExpLiteral(HRegExpLiteral* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  return MarkAsCall(DefineFixed(new LRegExpLiteral(context), eax), instr);
}


LInstruction* LChunkBuilder::DoFunctionLiteral(HFunctionLiteral* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  return MarkAsCall(DefineFixed(new LFunctionLiteral(context), eax), instr);
}


LInstruction* LChunkBuilder::DoDeleteProperty(HDeleteProperty* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* object = UseAtStart(instr->object());
  LOperand* key = UseOrConstantAtStart(instr->key());
  LDeleteProperty* result = new LDeleteProperty(context, object, key);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoOsrEntry(HOsrEntry* instr) {
  allocator_->MarkAsOsrEntry();
  current_block_->last_environment()->set_ast_id(instr->ast_id());
  return AssignEnvironment(new LOsrEntry);
}


LInstruction* LChunkBuilder::DoParameter(HParameter* instr) {
  int spill_index = chunk()->GetParameterStackSlot(instr->index());
  return DefineAsSpilled(new LParameter, spill_index);
}


LInstruction* LChunkBuilder::DoUnknownOSRValue(HUnknownOSRValue* instr) {
  int spill_index = chunk()->GetNextSpillIndex(false);  // Not double-width.
  if (spill_index > LUnallocated::kMaxFixedIndex) {
    Abort("Too many spill slots needed for OSR");
    spill_index = 0;
  }
  return DefineAsSpilled(new LUnknownOSRValue, spill_index);
}


LInstruction* LChunkBuilder::DoCallStub(HCallStub* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  argument_count_ -= instr->argument_count();
  LCallStub* result = new LCallStub(context);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoArgumentsObject(HArgumentsObject* instr) {
  // There are no real uses of the arguments object.
  // arguments.length and element access are supported directly on
  // stack arguments, and any real arguments object use causes a bailout.
  // So this value is never used.
  return NULL;
}


LInstruction* LChunkBuilder::DoAccessArgumentsAt(HAccessArgumentsAt* instr) {
  LOperand* arguments = UseRegister(instr->arguments());
  LOperand* length = UseTempRegister(instr->length());
  LOperand* index = Use(instr->index());
  LAccessArgumentsAt* result = new LAccessArgumentsAt(arguments, length, index);
  return AssignEnvironment(DefineAsRegister(result));
}


LInstruction* LChunkBuilder::DoToFastProperties(HToFastProperties* instr) {
  LOperand* object = UseFixed(instr->value(), eax);
  LToFastProperties* result = new LToFastProperties(object);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoTypeof(HTypeof* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* value = UseAtStart(instr->value());
  LTypeof* result = new LTypeof(context, value);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


LInstruction* LChunkBuilder::DoTypeofIsAndBranch(HTypeofIsAndBranch* instr) {
  return new LTypeofIsAndBranch(UseTempRegister(instr->value()));
}


LInstruction* LChunkBuilder::DoIsConstructCallAndBranch(
    HIsConstructCallAndBranch* instr) {
  return new LIsConstructCallAndBranch(TempRegister());
}


LInstruction* LChunkBuilder::DoSimulate(HSimulate* instr) {
  HEnvironment* env = current_block_->last_environment();
  ASSERT(env != NULL);

  env->set_ast_id(instr->ast_id());

  env->Drop(instr->pop_count());
  for (int i = 0; i < instr->values()->length(); ++i) {
    HValue* value = instr->values()->at(i);
    if (instr->HasAssignedIndexAt(i)) {
      env->Bind(instr->GetAssignedIndexAt(i), value);
    } else {
      env->Push(value);
    }
  }

  // If there is an instruction pending deoptimization environment create a
  // lazy bailout instruction to capture the environment.
  if (pending_deoptimization_ast_id_ != AstNode::kNoNumber) {
    ASSERT(pending_deoptimization_ast_id_ == instr->ast_id());
    LLazyBailout* lazy_bailout = new LLazyBailout;
    LInstruction* result = AssignEnvironment(lazy_bailout);
    instruction_pending_deoptimization_environment_->
        set_deoptimization_environment(result->environment());
    ClearInstructionPendingDeoptimizationEnvironment();
    return result;
  }

  return NULL;
}


LInstruction* LChunkBuilder::DoStackCheck(HStackCheck* instr) {
  if (instr->is_function_entry()) {
    LOperand* context = UseFixed(instr->context(), esi);
    return MarkAsCall(new LStackCheck(context), instr);
  } else {
    ASSERT(instr->is_backwards_branch());
    LOperand* context = UseAny(instr->context());
    return AssignEnvironment(AssignPointerMap(new LStackCheck(context)));
  }
}


LInstruction* LChunkBuilder::DoEnterInlined(HEnterInlined* instr) {
  HEnvironment* outer = current_block_->last_environment();
  HConstant* undefined = graph()->GetConstantUndefined();
  HEnvironment* inner = outer->CopyForInlining(instr->closure(),
                                               instr->function(),
                                               undefined,
                                               instr->call_kind());
  current_block_->UpdateEnvironment(inner);
  chunk_->AddInlinedClosure(instr->closure());
  return NULL;
}


LInstruction* LChunkBuilder::DoLeaveInlined(HLeaveInlined* instr) {
  HEnvironment* outer = current_block_->last_environment()->outer();
  current_block_->UpdateEnvironment(outer);
  return NULL;
}


LInstruction* LChunkBuilder::DoIn(HIn* instr) {
  LOperand* context = UseFixed(instr->context(), esi);
  LOperand* key = UseOrConstantAtStart(instr->key());
  LOperand* object = UseOrConstantAtStart(instr->object());
  LIn* result = new LIn(context, key, object);
  return MarkAsCall(DefineFixed(result, eax), instr);
}


} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_IA32
