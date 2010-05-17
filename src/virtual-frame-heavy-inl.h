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

#ifndef V8_VIRTUAL_FRAME_HEAVY_INL_H_
#define V8_VIRTUAL_FRAME_HEAVY_INL_H_

#include "type-info.h"
#include "register-allocator.h"
#include "scopes.h"
#include "register-allocator-inl.h"
#include "codegen-inl.h"

namespace v8 {
namespace internal {

// On entry to a function, the virtual frame already contains the receiver,
// the parameters, and a return address.  All frame elements are in memory.
VirtualFrame::VirtualFrame()
    : elements_(parameter_count() + local_count() + kPreallocatedElements),
      stack_pointer_(parameter_count() + 1) {  // 0-based index of TOS.
  for (int i = 0; i <= stack_pointer_; i++) {
    elements_.Add(FrameElement::MemoryElement(TypeInfo::Unknown()));
  }
  for (int i = 0; i < RegisterAllocator::kNumRegisters; i++) {
    register_locations_[i] = kIllegalIndex;
  }
}


// When cloned, a frame is a deep copy of the original.
VirtualFrame::VirtualFrame(VirtualFrame* original)
    : elements_(original->element_count()),
      stack_pointer_(original->stack_pointer_) {
  elements_.AddAll(original->elements_);
  // Copy register locations from original.
  memcpy(&register_locations_,
         original->register_locations_,
         sizeof(register_locations_));
}


void VirtualFrame::PushFrameSlotAt(int index) {
  elements_.Add(CopyElementAt(index));
}


void VirtualFrame::Push(Register reg, TypeInfo info) {
  if (is_used(reg)) {
    int index = register_location(reg);
    FrameElement element = CopyElementAt(index, info);
    elements_.Add(element);
  } else {
    Use(reg, element_count());
    FrameElement element =
        FrameElement::RegisterElement(reg, FrameElement::NOT_SYNCED, info);
    elements_.Add(element);
  }
}


void VirtualFrame::Push(Handle<Object> value) {
  FrameElement element =
      FrameElement::ConstantElement(value, FrameElement::NOT_SYNCED);
  elements_.Add(element);
}


bool VirtualFrame::Equals(VirtualFrame* other) {
#ifdef DEBUG
  for (int i = 0; i < RegisterAllocator::kNumRegisters; i++) {
    if (register_location(i) != other->register_location(i)) {
      return false;
    }
  }
  if (element_count() != other->element_count()) return false;
#endif
  if (stack_pointer_ != other->stack_pointer_) return false;
  for (int i = 0; i < element_count(); i++) {
    if (!elements_[i].Equals(other->elements_[i])) return false;
  }

  return true;
}


void VirtualFrame::SetTypeForLocalAt(int index, TypeInfo info) {
  elements_[local0_index() + index].set_type_info(info);
}


// Make the type of all elements be MEMORY.
void VirtualFrame::SpillAll() {
  for (int i = 0; i < element_count(); i++) {
    SpillElementAt(i);
  }
}


void VirtualFrame::PrepareForReturn() {
  // Spill all locals. This is necessary to make sure all locals have
  // the right value when breaking at the return site in the debugger.
  for (int i = 0; i < expression_base_index(); i++) {
    SpillElementAt(i);
  }
}


void VirtualFrame::SetTypeForParamAt(int index, TypeInfo info) {
  elements_[param0_index() + index].set_type_info(info);
}


void VirtualFrame::Nip(int num_dropped) {
  ASSERT(num_dropped >= 0);
  if (num_dropped == 0) return;
  Result tos = Pop();
  if (num_dropped > 1) {
    Drop(num_dropped - 1);
  }
  SetElementAt(0, &tos);
}


void VirtualFrame::Push(Smi* value) {
  Push(Handle<Object> (value));
}


int VirtualFrame::register_location(Register reg) {
  return register_locations_[RegisterAllocator::ToNumber(reg)];
}


void VirtualFrame::set_register_location(Register reg, int index) {
  register_locations_[RegisterAllocator::ToNumber(reg)] = index;
}


bool VirtualFrame::is_used(Register reg) {
  return register_locations_[RegisterAllocator::ToNumber(reg)]
      != kIllegalIndex;
}


void VirtualFrame::SetElementAt(int index, Handle<Object> value) {
  Result temp(value);
  SetElementAt(index, &temp);
}


Result VirtualFrame::CallStub(CodeStub* stub, int arg_count) {
  PrepareForCall(arg_count, arg_count);
  return RawCallStub(stub);
}


int VirtualFrame::parameter_count() {
  return cgen()->scope()->num_parameters();
}


int VirtualFrame::local_count() {
  return cgen()->scope()->num_stack_slots();
}

} }  // namespace v8::internal

#endif  // V8_VIRTUAL_FRAME_HEAVY_INL_H_
