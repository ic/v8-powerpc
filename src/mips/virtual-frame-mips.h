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


#ifndef V8_MIPS_VIRTUAL_FRAME_MIPS_H_
#define V8_MIPS_VIRTUAL_FRAME_MIPS_H_

#include "register-allocator.h"
#include "scopes.h"

namespace v8 {
namespace internal {


// -------------------------------------------------------------------------
// Virtual frames
//
// The virtual frame is an abstraction of the physical stack frame. It
// encapsulates the parameters, frame-allocated locals, and the expression
// stack. It supports push/pop operations on the expression stack, as well
// as random access to the expression stack elements, locals, and
// parameters.

class VirtualFrame : public ZoneObject {
 public:
  // A utility class to introduce a scope where the virtual frame is
  // expected to remain spilled. The constructor spills the code
  // generator's current frame, but no attempt is made to require it
  // to stay spilled. It is intended as documentation while the code
  // generator is being transformed.
  class SpilledScope BASE_EMBEDDED {
   public:
    SpilledScope() {}
  };

  // An illegal index into the virtual frame.
  static const int kIllegalIndex = -1;

  // Construct an initial virtual frame on entry to a JS function.
  inline VirtualFrame();

  // Construct a virtual frame as a clone of an existing one.
  explicit inline VirtualFrame(VirtualFrame* original);

  CodeGenerator* cgen() { return CodeGeneratorScope::Current(); }
  MacroAssembler* masm() { return cgen()->masm(); }

  // Create a duplicate of an existing valid frame element.
  FrameElement CopyElementAt(int index,
                             NumberInfo info = NumberInfo::Unknown());

  // The number of elements on the virtual frame.
  int element_count() { return elements_.length(); }

  // The height of the virtual expression stack.
  int height() {
    return element_count() - expression_base_index();
  }

  int register_location(int num) {
    ASSERT(num >= 0 && num < RegisterAllocator::kNumRegisters);
    return register_locations_[num];
  }

  int register_location(Register reg) {
    return register_locations_[RegisterAllocator::ToNumber(reg)];
  }

  void set_register_location(Register reg, int index) {
    register_locations_[RegisterAllocator::ToNumber(reg)] = index;
  }

  bool is_used(int num) {
    ASSERT(num >= 0 && num < RegisterAllocator::kNumRegisters);
    return register_locations_[num] != kIllegalIndex;
  }

  bool is_used(Register reg) {
    return register_locations_[RegisterAllocator::ToNumber(reg)]
        != kIllegalIndex;
  }

  // Add extra in-memory elements to the top of the frame to match an actual
  // frame (eg, the frame after an exception handler is pushed). No code is
  // emitted.
  void Adjust(int count);

  // Forget elements from the top of the frame to match an actual frame (eg,
  // the frame after a runtime call). No code is emitted.
  void Forget(int count) {
    ASSERT(count >= 0);
    ASSERT(stack_pointer_ == element_count() - 1);
    stack_pointer_ -= count;
    // On mips, all elements are in memory, so there is no extra bookkeeping
    // (registers, copies, etc.) beyond dropping the elements.
    elements_.Rewind(stack_pointer_ + 1);
  }

  // Forget count elements from the top of the frame and adjust the stack
  // pointer downward. This is used, for example, before merging frames at
  // break, continue, and return targets.
  void ForgetElements(int count);

  // Spill all values from the frame to memory.
  void SpillAll();

  // Spill all occurrences of a specific register from the frame.
  void Spill(Register reg) {
    if (is_used(reg)) SpillElementAt(register_location(reg));
  }

  // Spill all occurrences of an arbitrary register if possible. Return the
  // register spilled or no_reg if it was not possible to free any register
  // (ie, they all have frame-external references).
  Register SpillAnyRegister();

  // Prepare this virtual frame for merging to an expected frame by
  // performing some state changes that do not require generating
  // code. It is guaranteed that no code will be generated.
  void PrepareMergeTo(VirtualFrame* expected);

  // Make this virtual frame have a state identical to an expected virtual
  // frame. As a side effect, code may be emitted to make this frame match
  // the expected one.
  void MergeTo(VirtualFrame* expected);

  // Detach a frame from its code generator, perhaps temporarily. This
  // tells the register allocator that it is free to use frame-internal
  // registers. Used when the code generator's frame is switched from this
  // one to NULL by an unconditional jump.
  void DetachFromCodeGenerator() {
    RegisterAllocator* cgen_allocator = cgen()->allocator();
    for (int i = 0; i < RegisterAllocator::kNumRegisters; i++) {
      if (is_used(i)) cgen_allocator->Unuse(i);
    }
  }

  // (Re)attach a frame to its code generator. This informs the register
  // allocator that the frame-internal register references are active again.
  // Used when a code generator's frame is switched from NULL to this one by
  // binding a label.
  void AttachToCodeGenerator() {
    RegisterAllocator* cgen_allocator = cgen()->allocator();
    for (int i = 0; i < RegisterAllocator::kNumRegisters; i++) {
      if (is_used(i)) cgen_allocator->Unuse(i);
    }
  }

  // Emit code for the physical JS entry and exit frame sequences. After
  // calling Enter, the virtual frame is ready for use; and after calling
  // Exit it should not be used. Note that Enter does not allocate space in
  // the physical frame for storing frame-allocated locals.
  void Enter();
  void Exit();

  // Prepare for returning from the frame by spilling locals and
  // dropping all non-locals elements in the virtual frame. This
  // avoids generating unnecessary merge code when jumping to the
  // shared return site. Emits code for spills.
  void PrepareForReturn();

  // Allocate and initialize the frame-allocated locals.
  void AllocateStackSlots();

  // The current top of the expression stack as an assembly operand.
  MemOperand Top() { return MemOperand(sp, 0); }

  // An element of the expression stack as an assembly operand.
  MemOperand ElementAt(int index) {
    return MemOperand(sp, index * kPointerSize);
  }

  // Random-access store to a frame-top relative frame element. The result
  // becomes owned by the frame and is invalidated.
  void SetElementAt(int index, Result* value);

  // Set a frame element to a constant. The index is frame-top relative.
  void SetElementAt(int index, Handle<Object> value) {
    Result temp(value);
    SetElementAt(index, &temp);
  }

  void PushElementAt(int index) {
    PushFrameSlotAt(element_count() - index - 1);
  }

  // A frame-allocated local as an assembly operand.
  MemOperand LocalAt(int index) {
    ASSERT(0 <= index);
    ASSERT(index < local_count());
    return MemOperand(s8_fp, kLocal0Offset - index * kPointerSize);
  }

  // Push a copy of the value of a local frame slot on top of the frame.
  void PushLocalAt(int index) {
    PushFrameSlotAt(local0_index() + index);
  }

  // Push the value of a local frame slot on top of the frame and invalidate
  // the local slot. The slot should be written to before trying to read
  // from it again.
  void TakeLocalAt(int index) {
    TakeFrameSlotAt(local0_index() + index);
  }

  // Store the top value on the virtual frame into a local frame slot. The
  // value is left in place on top of the frame.
  void StoreToLocalAt(int index) {
    StoreToFrameSlotAt(local0_index() + index);
  }

  // Push the address of the receiver slot on the frame.
  void PushReceiverSlotAddress();

  // The function frame slot.
  MemOperand Function() { return MemOperand(s8_fp, kFunctionOffset); }

  // Push the function on top of the frame.
  void PushFunction() { PushFrameSlotAt(function_index()); }

  // The context frame slot.
  MemOperand Context() { return MemOperand(s8_fp, kContextOffset); }

  // Save the value of the cp register to the context frame slot.
  void SaveContextRegister();

  // Restore the cp register from the value of the context frame
  // slot.
  void RestoreContextRegister();

  // A parameter as an assembly operand.
  MemOperand ParameterAt(int index) {
    // Index -1 corresponds to the receiver.
    ASSERT(-1 <= index);  // -1 is the receiver.
    ASSERT(index <= parameter_count());
    uint16_t a = 0;   // Number of argument slots.
    return MemOperand(s8_fp, (1 + parameter_count() + a - index) *kPointerSize);
  }

  // Push a copy of the value of a parameter frame slot on top of the frame.
  void PushParameterAt(int index) {
    PushFrameSlotAt(param0_index() + index);
  }

  // Push the value of a paramter frame slot on top of the frame and
  // invalidate the parameter slot. The slot should be written to before
  // trying to read from it again.
  void TakeParameterAt(int index) {
    TakeFrameSlotAt(param0_index() + index);
  }

  // Store the top value on the virtual frame into a parameter frame slot.
  // The value is left in place on top of the frame.
  void StoreToParameterAt(int index) {
    StoreToFrameSlotAt(param0_index() + index);
  }

  // The receiver frame slot.
  MemOperand Receiver() { return ParameterAt(-1); }

  // Push a try-catch or try-finally handler on top of the virtual frame.
  void PushTryHandler(HandlerType type);

  // Call stub given the number of arguments it expects on (and
  // removes from) the stack.
  void CallStub(CodeStub* stub, int arg_count) {
    PrepareForCall(arg_count, arg_count);
    RawCallStub(stub);
  }

  void CallStub(CodeStub* stub, Result* arg);

  void CallStub(CodeStub* stub, Result* arg0, Result* arg1);

  // Call runtime given the number of arguments expected on (and
  // removed from) the stack.
  void CallRuntime(Runtime::Function* f, int arg_count);
  void CallRuntime(Runtime::FunctionId id, int arg_count);

  // Call runtime with sp aligned to 8 bytes.
  void CallAlignedRuntime(Runtime::Function* f, int arg_count);
  void CallAlignedRuntime(Runtime::FunctionId id, int arg_count);

  // Invoke builtin given the number of arguments it expects on (and
  // removes from) the stack.
  void InvokeBuiltin(Builtins::JavaScript id,
                     InvokeJSFlags flag,
                     Result* arg_count_register,
                     int arg_count);

  // Call into an IC stub given the number of arguments it removes
  // from the stack. Register arguments are passed as results and
  // consumed by the call.
  void CallCodeObject(Handle<Code> ic,
                      RelocInfo::Mode rmode,
                      int dropped_args);
  void CallCodeObject(Handle<Code> ic,
                      RelocInfo::Mode rmode,
                      Result* arg,
                      int dropped_args);
  void CallCodeObject(Handle<Code> ic,
                      RelocInfo::Mode rmode,
                      Result* arg0,
                      Result* arg1,
                      int dropped_args,
                      bool set_auto_args_slots = false);

  // Drop a number of elements from the top of the expression stack. May
  // emit code to affect the physical frame. Does not clobber any registers
  // excepting possibly the stack pointer.
  void Drop(int count);
  // Similar to VirtualFrame::Drop but we don't modify the actual stack.
  // This is because we need to manually restore sp to the correct position.
  void DropFromVFrameOnly(int count);

  // Drop one element.
  void Drop() { Drop(1); }
  void DropFromVFrameOnly() { DropFromVFrameOnly(1); }

  // Duplicate the top element of the frame.
  void Dup() { PushFrameSlotAt(element_count() - 1); }

  // Pop an element from the top of the expression stack. Returns a
  // Result, which may be a constant or a register.
  Result Pop();

  // Pop and save an element from the top of the expression stack and
  // emit a corresponding pop instruction.
  void EmitPop(Register reg);
  // Same but for multiple registers
  void EmitMultiPop(RegList regs);
  void EmitMultiPopReversed(RegList regs);

  // Push an element on top of the expression stack and emit a
  // corresponding push instruction.
  void EmitPush(Register reg);
  // Same but for multiple registers.
  void EmitMultiPush(RegList regs);
  void EmitMultiPushReversed(RegList regs);

  // Push an element on the virtual frame.
  inline void Push(Register reg, NumberInfo info = NumberInfo::Unknown());
  inline void Push(Handle<Object> value);
  inline void Push(Smi* value);

  // Pushing a result invalidates it (its contents become owned by the frame).
  void Push(Result* result) {
    if (result->is_register()) {
      Push(result->reg());
    } else {
      ASSERT(result->is_constant());
      Push(result->handle());
    }
    result->Unuse();
  }

  // Nip removes zero or more elements from immediately below the top
  // of the frame, leaving the previous top-of-frame value on top of
  // the frame. Nip(k) is equivalent to x = Pop(), Drop(k), Push(x).
  inline void Nip(int num_dropped);

  // This pushes 4 arguments slots on the stack and saves asked 'a' registers
  // 'a' registers are arguments register a0 to a3.
  void EmitArgumentSlots(RegList reglist);

  inline void SetTypeForLocalAt(int index, NumberInfo info);
  inline void SetTypeForParamAt(int index, NumberInfo info);

 private:
  static const int kLocal0Offset = JavaScriptFrameConstants::kLocal0Offset;
  static const int kFunctionOffset = JavaScriptFrameConstants::kFunctionOffset;
  static const int kContextOffset = StandardFrameConstants::kContextOffset;

  static const int kHandlerSize = StackHandlerConstants::kSize / kPointerSize;
  static const int kPreallocatedElements = 5 + 8;  // 8 expression stack slots.

  ZoneList<FrameElement> elements_;

  // The index of the element that is at the processor's stack pointer
  // (the sp register).
  int stack_pointer_;

  // The index of the register frame element using each register, or
  // kIllegalIndex if a register is not on the frame.
  int register_locations_[RegisterAllocator::kNumRegisters];

  // The number of frame-allocated locals and parameters respectively.
  int parameter_count() { return cgen()->scope()->num_parameters(); }
  int local_count() { return cgen()->scope()->num_stack_slots(); }

  // The index of the element that is at the processor's frame pointer
  // (the fp register). The parameters, receiver, function, and context
  // are below the frame pointer.
  int frame_pointer() { return parameter_count() + 3; }

  // The index of the first parameter. The receiver lies below the first
  // parameter.
  int param0_index() { return 1; }

  // The index of the context slot in the frame. It is immediately
  // below the frame pointer.
  int context_index() { return frame_pointer() - 1; }

  // The index of the function slot in the frame. It is below the frame
  // pointer and context slot.
  int function_index() { return frame_pointer() - 2; }

  // The index of the first local. Between the frame pointer and the
  // locals lies the return address.
  int local0_index() { return frame_pointer() + 2; }

  // The index of the base of the expression stack.
  int expression_base_index() { return local0_index() + local_count(); }

  // Convert a frame index into a frame pointer relative offset into the
  // actual stack.
  int fp_relative(int index) {
    ASSERT(index < element_count());
    ASSERT(frame_pointer() < element_count());  // FP is on the frame.
    return (frame_pointer() - index) * kPointerSize;
  }

  // Record an occurrence of a register in the virtual frame. This has the
  // effect of incrementing the register's external reference count and
  // of updating the index of the register's location in the frame.
  void Use(Register reg, int index) {
    ASSERT(!is_used(reg));
    set_register_location(reg, index);
    cgen()->allocator()->Use(reg);
  }

  // Record that a register reference has been dropped from the frame. This
  // decrements the register's external reference count and invalidates the
  // index of the register's location in the frame.
  void Unuse(Register reg) {
    ASSERT(is_used(reg));
    set_register_location(reg, kIllegalIndex);
    cgen()->allocator()->Unuse(reg);
  }

  // Spill the element at a particular index---write it to memory if
  // necessary, free any associated register, and forget its value if
  // constant.
  void SpillElementAt(int index);

  // Sync the element at a particular index. If it is a register or
  // constant that disagrees with the value on the stack, write it to memory.
  // Keep the element type as register or constant, and clear the dirty bit.
  void SyncElementAt(int index);

  // Sync the range of elements in [begin, end] with memory.
  void SyncRange(int begin, int end);

  // Sync a single unsynced element that lies beneath or at the stack pointer.
  void SyncElementBelowStackPointer(int index);

  // Sync a single unsynced element that lies just above the stack pointer.
  void SyncElementByPushing(int index);

  // Push a copy of a frame slot (typically a local or parameter) on top of
  // the frame.
  inline void PushFrameSlotAt(int index);

  // Push a the value of a frame slot (typically a local or parameter) on
  // top of the frame and invalidate the slot.
  void TakeFrameSlotAt(int index);

  // Store the value on top of the frame to a frame slot (typically a local
  // or parameter).
  void StoreToFrameSlotAt(int index);

  // Spill all elements in registers. Spill the top spilled_args elements
  // on the frame. Sync all other frame elements.
  // Then drop dropped_args elements from the virtual frame, to match
  // the effect of an upcoming call that will drop them from the stack.
  void PrepareForCall(int spilled_args, int dropped_args);

  // Move frame elements currently in registers or constants, that
  // should be in memory in the expected frame, to memory.
  void MergeMoveRegistersToMemory(VirtualFrame* expected);

  // Make the register-to-register moves necessary to
  // merge this frame with the expected frame.
  // Register to memory moves must already have been made,
  // and memory to register moves must follow this call.
  // This is because some new memory-to-register moves are
  // created in order to break cycles of register moves.
  // Used in the implementation of MergeTo().
  void MergeMoveRegistersToRegisters(VirtualFrame* expected);

  // Make the memory-to-register and constant-to-register moves
  // needed to make this frame equal the expected frame.
  // Called after all register-to-memory and register-to-register
  // moves have been made. After this function returns, the frames
  // should be equal.
  void MergeMoveMemoryToRegisters(VirtualFrame* expected);

  // Invalidates a frame slot (puts an invalid frame element in it).
  // Copies on the frame are correctly handled, and if this slot was
  // the backing store of copies, the index of the new backing store
  // is returned. Otherwise, returns kIllegalIndex.
  // Register counts are correctly updated.
  int InvalidateFrameSlotAt(int index);

  // Call a code stub that has already been prepared for calling (via
  // PrepareForCall).
  void RawCallStub(CodeStub* stub);

  // Calls a code object which has already been prepared for calling
  // (via PrepareForCall).
  void RawCallCodeObject(Handle<Code> code, RelocInfo::Mode rmode);

  inline bool Equals(VirtualFrame* other);

  // Classes that need raw access to the elements_ array.
  friend class DeferredCode;
  friend class JumpTarget;
};


} }  // namespace v8::internal

#endif  // V8_MIPS_VIRTUAL_FRAME_MIPS_H_

