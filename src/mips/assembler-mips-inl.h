// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2011 the V8 project authors. All rights reserved.


#ifndef V8_MIPS_ASSEMBLER_MIPS_INL_H_
#define V8_MIPS_ASSEMBLER_MIPS_INL_H_

#include "mips/assembler-mips.h"
#include "cpu.h"
#include "debug.h"


namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// Operand and MemOperand.

Operand::Operand(int32_t immediate, RelocInfo::Mode rmode)  {
  rm_ = no_reg;
  imm32_ = immediate;
  rmode_ = rmode;
}


Operand::Operand(const ExternalReference& f)  {
  rm_ = no_reg;
  imm32_ = reinterpret_cast<int32_t>(f.address());
  rmode_ = RelocInfo::EXTERNAL_REFERENCE;
}


Operand::Operand(Smi* value) {
  rm_ = no_reg;
  imm32_ =  reinterpret_cast<intptr_t>(value);
  rmode_ = RelocInfo::NONE;
}


Operand::Operand(Register rm) {
  rm_ = rm;
}


bool Operand::is_reg() const {
  return rm_.is_valid();
}



// -----------------------------------------------------------------------------
// RelocInfo.

void RelocInfo::apply(intptr_t delta) {
  if (IsInternalReference(rmode_)) {
    // Absolute code pointer inside code object moves with the code object.
    byte* p = reinterpret_cast<byte*>(pc_);
    int count = Assembler::RelocateInternalReference(p, delta);
    CPU::FlushICache(p, count * sizeof(uint32_t));
  }
}


Address RelocInfo::target_address() {
  ASSERT(IsCodeTarget(rmode_) || rmode_ == RUNTIME_ENTRY);
  return Assembler::target_address_at(pc_);
}


Address RelocInfo::target_address_address() {
  ASSERT(IsCodeTarget(rmode_) || rmode_ == RUNTIME_ENTRY);
  return reinterpret_cast<Address>(pc_);
}


int RelocInfo::target_address_size() {
  return Assembler::kExternalTargetSize;
}


void RelocInfo::set_target_address(Address target) {
  ASSERT(IsCodeTarget(rmode_) || rmode_ == RUNTIME_ENTRY);
  Assembler::set_target_address_at(pc_, target);
}


Object* RelocInfo::target_object() {
  ASSERT(IsCodeTarget(rmode_) || rmode_ == EMBEDDED_OBJECT);
  return reinterpret_cast<Object*>(Assembler::target_address_at(pc_));
}


Handle<Object> RelocInfo::target_object_handle(Assembler *origin) {
  ASSERT(IsCodeTarget(rmode_) || rmode_ == EMBEDDED_OBJECT);
  return Handle<Object>(reinterpret_cast<Object**>(
      Assembler::target_address_at(pc_)));
}


Object** RelocInfo::target_object_address() {
  // Provide a "natural pointer" to the embedded object,
  // which can be de-referenced during heap iteration.
  ASSERT(IsCodeTarget(rmode_) || rmode_ == EMBEDDED_OBJECT);
  reconstructed_obj_ptr_ =
      reinterpret_cast<Object*>(Assembler::target_address_at(pc_));
  return &reconstructed_obj_ptr_;
}


void RelocInfo::set_target_object(Object* target) {
  ASSERT(IsCodeTarget(rmode_) || rmode_ == EMBEDDED_OBJECT);
  Assembler::set_target_address_at(pc_, reinterpret_cast<Address>(target));
}


Address* RelocInfo::target_reference_address() {
  ASSERT(rmode_ == EXTERNAL_REFERENCE);
  reconstructed_adr_ptr_ = Assembler::target_address_at(pc_);
  return &reconstructed_adr_ptr_;
}


Handle<JSGlobalPropertyCell> RelocInfo::target_cell_handle() {
  ASSERT(rmode_ == RelocInfo::GLOBAL_PROPERTY_CELL);
  Address address = Memory::Address_at(pc_);
  return Handle<JSGlobalPropertyCell>(
      reinterpret_cast<JSGlobalPropertyCell**>(address));
}


JSGlobalPropertyCell* RelocInfo::target_cell() {
  ASSERT(rmode_ == RelocInfo::GLOBAL_PROPERTY_CELL);
  Address address = Memory::Address_at(pc_);
  Object* object = HeapObject::FromAddress(
      address - JSGlobalPropertyCell::kValueOffset);
  return reinterpret_cast<JSGlobalPropertyCell*>(object);
}


void RelocInfo::set_target_cell(JSGlobalPropertyCell* cell) {
  ASSERT(rmode_ == RelocInfo::GLOBAL_PROPERTY_CELL);
  Address address = cell->address() + JSGlobalPropertyCell::kValueOffset;
  Memory::Address_at(pc_) = address;
}


Address RelocInfo::call_address() {
  ASSERT((IsJSReturn(rmode()) && IsPatchedReturnSequence()) ||
         (IsDebugBreakSlot(rmode()) && IsPatchedDebugBreakSlotSequence()));
  // The pc_ offset of 0 assumes mips patched return sequence per
  // debug-mips.cc BreakLocationIterator::SetDebugBreakAtReturn(), or
  // debug break slot per BreakLocationIterator::SetDebugBreakAtSlot().
  return Assembler::target_address_at(pc_);
}


void RelocInfo::set_call_address(Address target) {
  ASSERT((IsJSReturn(rmode()) && IsPatchedReturnSequence()) ||
         (IsDebugBreakSlot(rmode()) && IsPatchedDebugBreakSlotSequence()));
  // The pc_ offset of 0 assumes mips patched return sequence per
  // debug-mips.cc BreakLocationIterator::SetDebugBreakAtReturn(), or
  // debug break slot per BreakLocationIterator::SetDebugBreakAtSlot().
  Assembler::set_target_address_at(pc_, target);
}


Object* RelocInfo::call_object() {
  return *call_object_address();
}


Object** RelocInfo::call_object_address() {
  ASSERT((IsJSReturn(rmode()) && IsPatchedReturnSequence()) ||
         (IsDebugBreakSlot(rmode()) && IsPatchedDebugBreakSlotSequence()));
  return reinterpret_cast<Object**>(pc_ + 2 * Assembler::kInstrSize);
}


void RelocInfo::set_call_object(Object* target) {
  *call_object_address() = target;
}


bool RelocInfo::IsPatchedReturnSequence() {
  Instr instr0 = Assembler::instr_at(pc_);
  Instr instr1 = Assembler::instr_at(pc_ + 1 * Assembler::kInstrSize);
  Instr instr2 = Assembler::instr_at(pc_ + 2 * Assembler::kInstrSize);
  bool patched_return = ((instr0 & kOpcodeMask) == LUI &&
                         (instr1 & kOpcodeMask) == ORI &&
                         (instr2 & kOpcodeMask) == SPECIAL &&
                         (instr2 & kFunctionFieldMask) == JALR);
  return patched_return;
}


bool RelocInfo::IsPatchedDebugBreakSlotSequence() {
  Instr current_instr = Assembler::instr_at(pc_);
  return !Assembler::IsNop(current_instr, Assembler::DEBUG_BREAK_NOP);
}


void RelocInfo::Visit(ObjectVisitor* visitor) {
  RelocInfo::Mode mode = rmode();
  if (mode == RelocInfo::EMBEDDED_OBJECT) {
    Object** p = target_object_address();
    Object* orig = *p;
    visitor->VisitPointer(p);
    if (*p != orig) {
      set_target_object(*p);
    }
  } else if (RelocInfo::IsCodeTarget(mode)) {
    visitor->VisitCodeTarget(this);
  } else if (mode == RelocInfo::GLOBAL_PROPERTY_CELL) {
    visitor->VisitGlobalPropertyCell(this);
  } else if (mode == RelocInfo::EXTERNAL_REFERENCE) {
    visitor->VisitExternalReference(target_reference_address());
#ifdef ENABLE_DEBUGGER_SUPPORT
  // TODO(isolates): Get a cached isolate below.
  } else if (((RelocInfo::IsJSReturn(mode) &&
               IsPatchedReturnSequence()) ||
              (RelocInfo::IsDebugBreakSlot(mode) &&
               IsPatchedDebugBreakSlotSequence())) &&
             Isolate::Current()->debug()->has_break_points()) {
    visitor->VisitDebugTarget(this);
#endif
  } else if (mode == RelocInfo::RUNTIME_ENTRY) {
    visitor->VisitRuntimeEntry(this);
  }
}


template<typename StaticVisitor>
void RelocInfo::Visit(Heap* heap) {
  RelocInfo::Mode mode = rmode();
  if (mode == RelocInfo::EMBEDDED_OBJECT) {
    StaticVisitor::VisitPointer(heap, target_object_address());
  } else if (RelocInfo::IsCodeTarget(mode)) {
    StaticVisitor::VisitCodeTarget(heap, this);
  } else if (mode == RelocInfo::GLOBAL_PROPERTY_CELL) {
    StaticVisitor::VisitGlobalPropertyCell(heap, this);
  } else if (mode == RelocInfo::EXTERNAL_REFERENCE) {
    StaticVisitor::VisitExternalReference(target_reference_address());
#ifdef ENABLE_DEBUGGER_SUPPORT
  } else if (heap->isolate()->debug()->has_break_points() &&
             ((RelocInfo::IsJSReturn(mode) &&
              IsPatchedReturnSequence()) ||
             (RelocInfo::IsDebugBreakSlot(mode) &&
              IsPatchedDebugBreakSlotSequence()))) {
    StaticVisitor::VisitDebugTarget(heap, this);
#endif
  } else if (mode == RelocInfo::RUNTIME_ENTRY) {
    StaticVisitor::VisitRuntimeEntry(this);
  }
}


// -----------------------------------------------------------------------------
// Assembler.


void Assembler::CheckBuffer() {
  if (buffer_space() <= kGap) {
    GrowBuffer();
  }
}


void Assembler::CheckTrampolinePoolQuick() {
  if (pc_offset() >= next_buffer_check_) {
    CheckTrampolinePool();
  }
}


void Assembler::emit(Instr x) {
  if (!is_buffer_growth_blocked()) {
    CheckBuffer();
  }
  *reinterpret_cast<Instr*>(pc_) = x;
  pc_ += kInstrSize;
  CheckTrampolinePoolQuick();
}


} }  // namespace v8::internal

#endif  // V8_MIPS_ASSEMBLER_MIPS_INL_H_
