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

#if defined(V8_TARGET_ARCH_ARM)

#include "codegen-inl.h"
#include "register-allocator-inl.h"
#include "scopes.h"
#include "virtual-frame-inl.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm())

void VirtualFrame::PopToR1R0() {
  // Shuffle things around so the top of stack is in r0 and r1.
  MergeTOSTo(R0_R1_TOS);
  // Pop the two registers off the stack so they are detached from the frame.
  element_count_ -= 2;
  top_of_stack_state_ = NO_TOS_REGISTERS;
}


void VirtualFrame::PopToR1() {
  // Shuffle things around so the top of stack is only in r1.
  MergeTOSTo(R1_TOS);
  // Pop the register off the stack so it is detached from the frame.
  element_count_ -= 1;
  top_of_stack_state_ = NO_TOS_REGISTERS;
}


void VirtualFrame::PopToR0() {
  // Shuffle things around so the top of stack only in r0.
  MergeTOSTo(R0_TOS);
  // Pop the register off the stack so it is detached from the frame.
  element_count_ -= 1;
  top_of_stack_state_ = NO_TOS_REGISTERS;
}


void VirtualFrame::MergeTo(const VirtualFrame* expected, Condition cond) {
  if (Equals(expected)) return;
  MergeTOSTo(expected->top_of_stack_state_, cond);
  ASSERT(register_allocation_map_ == expected->register_allocation_map_);
}


void VirtualFrame::MergeTOSTo(
    VirtualFrame::TopOfStack expected_top_of_stack_state, Condition cond) {
#define CASE_NUMBER(a, b) ((a) * TOS_STATES + (b))
  switch (CASE_NUMBER(top_of_stack_state_, expected_top_of_stack_state)) {
    case CASE_NUMBER(NO_TOS_REGISTERS, NO_TOS_REGISTERS):
      break;
    case CASE_NUMBER(NO_TOS_REGISTERS, R0_TOS):
      __ pop(r0, cond);
      break;
    case CASE_NUMBER(NO_TOS_REGISTERS, R1_TOS):
      __ pop(r1, cond);
      break;
    case CASE_NUMBER(NO_TOS_REGISTERS, R0_R1_TOS):
      __ pop(r0, cond);
      __ pop(r1, cond);
      break;
    case CASE_NUMBER(NO_TOS_REGISTERS, R1_R0_TOS):
      __ pop(r1, cond);
      __ pop(r0, cond);
      break;
    case CASE_NUMBER(R0_TOS, NO_TOS_REGISTERS):
      __ push(r0, cond);
      break;
    case CASE_NUMBER(R0_TOS, R0_TOS):
      break;
    case CASE_NUMBER(R0_TOS, R1_TOS):
      __ mov(r1, r0, LeaveCC, cond);
      break;
    case CASE_NUMBER(R0_TOS, R0_R1_TOS):
      __ pop(r1, cond);
      break;
    case CASE_NUMBER(R0_TOS, R1_R0_TOS):
      __ mov(r1, r0, LeaveCC, cond);
      __ pop(r0, cond);
      break;
    case CASE_NUMBER(R1_TOS, NO_TOS_REGISTERS):
      __ push(r1, cond);
      break;
    case CASE_NUMBER(R1_TOS, R0_TOS):
      __ mov(r0, r1, LeaveCC, cond);
      break;
    case CASE_NUMBER(R1_TOS, R1_TOS):
      break;
    case CASE_NUMBER(R1_TOS, R0_R1_TOS):
      __ mov(r0, r1, LeaveCC, cond);
      __ pop(r1, cond);
      break;
    case CASE_NUMBER(R1_TOS, R1_R0_TOS):
      __ pop(r0, cond);
      break;
    case CASE_NUMBER(R0_R1_TOS, NO_TOS_REGISTERS):
      __ Push(r1, r0, cond);
      break;
    case CASE_NUMBER(R0_R1_TOS, R0_TOS):
      __ push(r1, cond);
      break;
    case CASE_NUMBER(R0_R1_TOS, R1_TOS):
      __ push(r1, cond);
      __ mov(r1, r0, LeaveCC, cond);
      break;
    case CASE_NUMBER(R0_R1_TOS, R0_R1_TOS):
      break;
    case CASE_NUMBER(R0_R1_TOS, R1_R0_TOS):
      __ Swap(r0, r1, ip, cond);
      break;
    case CASE_NUMBER(R1_R0_TOS, NO_TOS_REGISTERS):
      __ Push(r0, r1, cond);
      break;
    case CASE_NUMBER(R1_R0_TOS, R0_TOS):
      __ push(r0, cond);
      __ mov(r0, r1, LeaveCC, cond);
      break;
    case CASE_NUMBER(R1_R0_TOS, R1_TOS):
      __ push(r0, cond);
      break;
    case CASE_NUMBER(R1_R0_TOS, R0_R1_TOS):
      __ Swap(r0, r1, ip, cond);
      break;
    case CASE_NUMBER(R1_R0_TOS, R1_R0_TOS):
      break;
    default:
      UNREACHABLE();
#undef CASE_NUMBER
  }
  // A conditional merge will be followed by a conditional branch and the
  // fall-through code will have an unchanged virtual frame state.  If the
  // merge is unconditional ('al'ways) then it might be followed by a fall
  // through.  We need to update the virtual frame state to match the code we
  // are falling into.  The final case is an unconditional merge followed by an
  // unconditional branch, in which case it doesn't matter what we do to the
  // virtual frame state, because the virtual frame will be invalidated.
  if (cond == al) {
    top_of_stack_state_ = expected_top_of_stack_state;
  }
}


void VirtualFrame::Enter() {
  Comment cmnt(masm(), "[ Enter JS frame");

#ifdef DEBUG
  // Verify that r1 contains a JS function.  The following code relies
  // on r2 being available for use.
  if (FLAG_debug_code) {
    Label map_check, done;
    __ tst(r1, Operand(kSmiTagMask));
    __ b(ne, &map_check);
    __ stop("VirtualFrame::Enter - r1 is not a function (smi check).");
    __ bind(&map_check);
    __ CompareObjectType(r1, r2, r2, JS_FUNCTION_TYPE);
    __ b(eq, &done);
    __ stop("VirtualFrame::Enter - r1 is not a function (map check).");
    __ bind(&done);
  }
#endif  // DEBUG

  // We are about to push four values to the frame.
  Adjust(4);
  __ stm(db_w, sp, r1.bit() | cp.bit() | fp.bit() | lr.bit());
  // Adjust FP to point to saved FP.
  __ add(fp, sp, Operand(2 * kPointerSize));
}


void VirtualFrame::Exit() {
  Comment cmnt(masm(), "[ Exit JS frame");
  // Record the location of the JS exit code for patching when setting
  // break point.
  __ RecordJSReturn();

  // Drop the execution stack down to the frame pointer and restore the caller
  // frame pointer and return address.
  __ mov(sp, fp);
  __ ldm(ia_w, sp, fp.bit() | lr.bit());
}


void VirtualFrame::AllocateStackSlots() {
  int count = local_count();
  if (count > 0) {
    Comment cmnt(masm(), "[ Allocate space for locals");
    Adjust(count);
    // Initialize stack slots with 'undefined' value.
    __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
    __ LoadRoot(r2, Heap::kStackLimitRootIndex);
    if (count < kLocalVarBound) {
      // For less locals the unrolled loop is more compact.
      for (int i = 0; i < count; i++) {
        __ push(ip);
      }
    } else {
      // For more locals a loop in generated code is more compact.
      Label alloc_locals_loop;
      __ mov(r1, Operand(count));
      __ bind(&alloc_locals_loop);
      __ push(ip);
      __ sub(r1, r1, Operand(1), SetCC);
      __ b(ne, &alloc_locals_loop);
    }
  } else {
    __ LoadRoot(r2, Heap::kStackLimitRootIndex);
  }
  // Check the stack for overflow or a break request.
  // Put the lr setup instruction in the delay slot.  The kInstrSize is added
  // to the implicit 8 byte offset that always applies to operations with pc
  // and gives a return address 12 bytes down.
  masm()->add(lr, pc, Operand(Assembler::kInstrSize));
  masm()->cmp(sp, Operand(r2));
  StackCheckStub stub;
  // Call the stub if lower.
  masm()->mov(pc,
              Operand(reinterpret_cast<intptr_t>(stub.GetCode().location()),
                      RelocInfo::CODE_TARGET),
              LeaveCC,
              lo);
}



void VirtualFrame::PushReceiverSlotAddress() {
  UNIMPLEMENTED();
}


void VirtualFrame::PushTryHandler(HandlerType type) {
  // Grow the expression stack by handler size less one (the return
  // address in lr is already counted by a call instruction).
  Adjust(kHandlerSize - 1);
  __ PushTryHandler(IN_JAVASCRIPT, type);
}


void VirtualFrame::CallJSFunction(int arg_count) {
  // InvokeFunction requires function in r1.
  PopToR1();
  SpillAll();

  // +1 for receiver.
  Forget(arg_count + 1);
  ASSERT(cgen()->HasValidEntryRegisters());
  ParameterCount count(arg_count);
  __ InvokeFunction(r1, count, CALL_FUNCTION);
  // Restore the context.
  __ ldr(cp, Context());
}


void VirtualFrame::CallRuntime(Runtime::Function* f, int arg_count) {
  SpillAll();
  Forget(arg_count);
  ASSERT(cgen()->HasValidEntryRegisters());
  __ CallRuntime(f, arg_count);
}


void VirtualFrame::CallRuntime(Runtime::FunctionId id, int arg_count) {
  SpillAll();
  Forget(arg_count);
  ASSERT(cgen()->HasValidEntryRegisters());
  __ CallRuntime(id, arg_count);
}


#ifdef ENABLE_DEBUGGER_SUPPORT
void VirtualFrame::DebugBreak() {
  ASSERT(cgen()->HasValidEntryRegisters());
  __ DebugBreak();
}
#endif


void VirtualFrame::InvokeBuiltin(Builtins::JavaScript id,
                                 InvokeJSFlags flags,
                                 int arg_count) {
  Forget(arg_count);
  __ InvokeBuiltin(id, flags);
}


void VirtualFrame::CallLoadIC(Handle<String> name, RelocInfo::Mode mode) {
  Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Initialize));
  PopToR0();
  SpillAll();
  __ mov(r2, Operand(name));
  CallCodeObject(ic, mode, 0);
}


void VirtualFrame::CallStoreIC(Handle<String> name, bool is_contextual) {
  Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
  PopToR0();
  if (is_contextual) {
    SpillAll();
    __ ldr(r1, MemOperand(cp, Context::SlotOffset(Context::GLOBAL_INDEX)));
  } else {
    EmitPop(r1);
    SpillAll();
  }
  __ mov(r2, Operand(name));
  CallCodeObject(ic, RelocInfo::CODE_TARGET, 0);
}


void VirtualFrame::CallKeyedLoadIC() {
  Handle<Code> ic(Builtins::builtin(Builtins::KeyedLoadIC_Initialize));
  PopToR1R0();
  SpillAll();
  CallCodeObject(ic, RelocInfo::CODE_TARGET, 0);
}


void VirtualFrame::CallKeyedStoreIC() {
  Handle<Code> ic(Builtins::builtin(Builtins::KeyedStoreIC_Initialize));
  PopToR1R0();
  SpillAll();
  EmitPop(r2);
  CallCodeObject(ic, RelocInfo::CODE_TARGET, 0);
}


void VirtualFrame::CallCodeObject(Handle<Code> code,
                                  RelocInfo::Mode rmode,
                                  int dropped_args) {
  switch (code->kind()) {
    case Code::CALL_IC:
    case Code::FUNCTION:
      break;
    case Code::KEYED_LOAD_IC:
    case Code::LOAD_IC:
    case Code::KEYED_STORE_IC:
    case Code::STORE_IC:
      ASSERT(dropped_args == 0);
      break;
    case Code::BUILTIN:
      ASSERT(*code == Builtins::builtin(Builtins::JSConstructCall));
      break;
    default:
      UNREACHABLE();
      break;
  }
  Forget(dropped_args);
  ASSERT(cgen()->HasValidEntryRegisters());
  __ Call(code, rmode);
}


//    NO_TOS_REGISTERS, R0_TOS, R1_TOS, R1_R0_TOS, R0_R1_TOS.
const bool VirtualFrame::kR0InUse[TOS_STATES] =
    { false,            true,   false,  true,      true };
const bool VirtualFrame::kR1InUse[TOS_STATES] =
    { false,            false,  true,   true,      true };
const int VirtualFrame::kVirtualElements[TOS_STATES] =
    { 0,                1,      1,      2,         2 };
const Register VirtualFrame::kTopRegister[TOS_STATES] =
    { r0,               r0,     r1,     r1,        r0 };
const Register VirtualFrame::kBottomRegister[TOS_STATES] =
    { r0,               r0,     r1,     r0,        r1 };
const Register VirtualFrame::kAllocatedRegisters[
    VirtualFrame::kNumberOfAllocatedRegisters] = { r2, r3, r4, r5, r6 };
// Popping is done by the transition implied by kStateAfterPop.  Of course if
// there were no stack slots allocated to registers then the physical SP must
// be adjusted.
const VirtualFrame::TopOfStack VirtualFrame::kStateAfterPop[TOS_STATES] =
    { NO_TOS_REGISTERS, NO_TOS_REGISTERS, NO_TOS_REGISTERS, R0_TOS, R1_TOS };
// Pushing is done by the transition implied by kStateAfterPush.  Of course if
// the maximum number of registers was already allocated to the top of stack
// slots then one register must be physically pushed onto the stack.
const VirtualFrame::TopOfStack VirtualFrame::kStateAfterPush[TOS_STATES] =
    { R0_TOS, R1_R0_TOS, R0_R1_TOS, R0_R1_TOS, R1_R0_TOS };


bool VirtualFrame::SpilledScope::is_spilled_ = false;


void VirtualFrame::Drop(int count) {
  ASSERT(count >= 0);
  ASSERT(height() >= count);
  // Discard elements from the virtual frame and free any registers.
  int num_virtual_elements = kVirtualElements[top_of_stack_state_];
  while (num_virtual_elements > 0) {
    Pop();
    num_virtual_elements--;
    count--;
    if (count == 0) return;
  }
  if (count == 0) return;
  __ add(sp, sp, Operand(count * kPointerSize));
  element_count_ -= count;
}


void VirtualFrame::Pop() {
  if (top_of_stack_state_ == NO_TOS_REGISTERS) {
    __ add(sp, sp, Operand(kPointerSize));
  } else {
    top_of_stack_state_ = kStateAfterPop[top_of_stack_state_];
  }
  element_count_--;
}


void VirtualFrame::EmitPop(Register reg) {
  ASSERT(!is_used(RegisterAllocator::ToNumber(reg)));
  if (top_of_stack_state_ == NO_TOS_REGISTERS) {
    __ pop(reg);
  } else {
    __ mov(reg, kTopRegister[top_of_stack_state_]);
    top_of_stack_state_ = kStateAfterPop[top_of_stack_state_];
  }
  element_count_--;
}


void VirtualFrame::SpillAllButCopyTOSToR0() {
  switch (top_of_stack_state_) {
    case NO_TOS_REGISTERS:
      __ ldr(r0, MemOperand(sp, 0));
      break;
    case R0_TOS:
      __ push(r0);
      break;
    case R1_TOS:
      __ push(r1);
      __ mov(r0, r1);
      break;
    case R0_R1_TOS:
      __ Push(r1, r0);
      break;
    case R1_R0_TOS:
      __ Push(r0, r1);
      __ mov(r0, r1);
      break;
    default:
      UNREACHABLE();
  }
  top_of_stack_state_ = NO_TOS_REGISTERS;
}


void VirtualFrame::SpillAllButCopyTOSToR1R0() {
  switch (top_of_stack_state_) {
    case NO_TOS_REGISTERS:
      __ ldr(r1, MemOperand(sp, 0));
      __ ldr(r0, MemOperand(sp, kPointerSize));
      break;
    case R0_TOS:
      __ push(r0);
      __ mov(r1, r0);
      __ ldr(r0, MemOperand(sp, kPointerSize));
      break;
    case R1_TOS:
      __ push(r1);
      __ ldr(r0, MemOperand(sp, kPointerSize));
      break;
    case R0_R1_TOS:
      __ Push(r1, r0);
      __ Swap(r0, r1, ip);
      break;
    case R1_R0_TOS:
      __ Push(r0, r1);
      break;
    default:
      UNREACHABLE();
  }
  top_of_stack_state_ = NO_TOS_REGISTERS;
}


Register VirtualFrame::Peek() {
  AssertIsNotSpilled();
  if (top_of_stack_state_ == NO_TOS_REGISTERS) {
    top_of_stack_state_ = kStateAfterPush[top_of_stack_state_];
    Register answer = kTopRegister[top_of_stack_state_];
    __ pop(answer);
    return answer;
  } else {
    return kTopRegister[top_of_stack_state_];
  }
}


void VirtualFrame::Dup() {
  if (SpilledScope::is_spilled()) {
    __ ldr(ip, MemOperand(sp, 0));
    __ push(ip);
  } else {
    switch (top_of_stack_state_) {
      case NO_TOS_REGISTERS:
        __ ldr(r0, MemOperand(sp, 0));
        top_of_stack_state_ = R0_TOS;
        break;
      case R0_TOS:
        __ mov(r1, r0);
        // r0 and r1 contains the same value. Prefer state with r0 holding TOS.
        top_of_stack_state_ = R0_R1_TOS;
        break;
      case R1_TOS:
        __ mov(r0, r1);
        // r0 and r1 contains the same value. Prefer state with r0 holding TOS.
        top_of_stack_state_ = R0_R1_TOS;
        break;
      case R0_R1_TOS:
        __ push(r1);
        __ mov(r1, r0);
        // r0 and r1 contains the same value. Prefer state with r0 holding TOS.
        top_of_stack_state_ = R0_R1_TOS;
        break;
      case R1_R0_TOS:
        __ push(r0);
        __ mov(r0, r1);
        // r0 and r1 contains the same value. Prefer state with r0 holding TOS.
        top_of_stack_state_ = R0_R1_TOS;
        break;
      default:
        UNREACHABLE();
    }
  }
  element_count_++;
}


void VirtualFrame::Dup2() {
  if (SpilledScope::is_spilled()) {
    __ ldr(ip, MemOperand(sp, kPointerSize));
    __ push(ip);
    __ ldr(ip, MemOperand(sp, kPointerSize));
    __ push(ip);
  } else {
    switch (top_of_stack_state_) {
      case NO_TOS_REGISTERS:
        __ ldr(r0, MemOperand(sp, 0));
        __ ldr(r1, MemOperand(sp, kPointerSize));
        top_of_stack_state_ = R0_R1_TOS;
        break;
      case R0_TOS:
        __ push(r0);
        __ ldr(r1, MemOperand(sp, kPointerSize));
        top_of_stack_state_ = R0_R1_TOS;
        break;
      case R1_TOS:
        __ push(r1);
        __ ldr(r0, MemOperand(sp, kPointerSize));
        top_of_stack_state_ = R1_R0_TOS;
        break;
      case R0_R1_TOS:
        __ Push(r1, r0);
        top_of_stack_state_ = R0_R1_TOS;
        break;
      case R1_R0_TOS:
        __ Push(r0, r1);
        top_of_stack_state_ = R1_R0_TOS;
        break;
      default:
        UNREACHABLE();
    }
  }
  element_count_ += 2;
}


Register VirtualFrame::PopToRegister(Register but_not_to_this_one) {
  ASSERT(but_not_to_this_one.is(r0) ||
         but_not_to_this_one.is(r1) ||
         but_not_to_this_one.is(no_reg));
  element_count_--;
  if (top_of_stack_state_ == NO_TOS_REGISTERS) {
    if (but_not_to_this_one.is(r0)) {
      __ pop(r1);
      return r1;
    } else {
      __ pop(r0);
      return r0;
    }
  } else {
    Register answer = kTopRegister[top_of_stack_state_];
    ASSERT(!answer.is(but_not_to_this_one));
    top_of_stack_state_ = kStateAfterPop[top_of_stack_state_];
    return answer;
  }
}


void VirtualFrame::EnsureOneFreeTOSRegister() {
  if (kVirtualElements[top_of_stack_state_] == kMaxTOSRegisters) {
    __ push(kBottomRegister[top_of_stack_state_]);
    top_of_stack_state_ = kStateAfterPush[top_of_stack_state_];
    top_of_stack_state_ = kStateAfterPop[top_of_stack_state_];
  }
  ASSERT(kVirtualElements[top_of_stack_state_] != kMaxTOSRegisters);
}


void VirtualFrame::EmitPush(Register reg) {
  element_count_++;
  if (reg.is(cp)) {
    // If we are pushing cp then we are about to make a call and things have to
    // be pushed to the physical stack.  There's nothing to be gained my moving
    // to a TOS register and then pushing that, we might as well push to the
    // physical stack immediately.
    MergeTOSTo(NO_TOS_REGISTERS);
    __ push(reg);
    return;
  }
  if (SpilledScope::is_spilled()) {
    ASSERT(top_of_stack_state_ == NO_TOS_REGISTERS);
    __ push(reg);
    return;
  }
  if (top_of_stack_state_ == NO_TOS_REGISTERS) {
    if (reg.is(r0)) {
      top_of_stack_state_ = R0_TOS;
      return;
    }
    if (reg.is(r1)) {
      top_of_stack_state_ = R1_TOS;
      return;
    }
  }
  EnsureOneFreeTOSRegister();
  top_of_stack_state_ = kStateAfterPush[top_of_stack_state_];
  Register dest = kTopRegister[top_of_stack_state_];
  __ Move(dest, reg);
}


void VirtualFrame::SetElementAt(Register reg, int this_far_down) {
  if (this_far_down == 0) {
    Pop();
    Register dest = GetTOSRegister();
    if (dest.is(reg)) {
      // We already popped one item off the top of the stack.  If the only
      // free register is the one we were asked to push then we have been
      // asked to push a register that was already in use, which cannot
      // happen.  It therefore folows that there are two free TOS registers:
      ASSERT(top_of_stack_state_ == NO_TOS_REGISTERS);
      dest = dest.is(r0) ? r1 : r0;
    }
    __ mov(dest, reg);
    EmitPush(dest);
  } else if (this_far_down == 1) {
    int virtual_elements = kVirtualElements[top_of_stack_state_];
    if (virtual_elements < 2) {
      __ str(reg, ElementAt(this_far_down));
    } else {
      ASSERT(virtual_elements == 2);
      ASSERT(!reg.is(r0));
      ASSERT(!reg.is(r1));
      Register dest = kBottomRegister[top_of_stack_state_];
      __ mov(dest, reg);
    }
  } else {
    ASSERT(this_far_down >= 2);
    ASSERT(kVirtualElements[top_of_stack_state_] <= 2);
    __ str(reg, ElementAt(this_far_down));
  }
}


Register VirtualFrame::GetTOSRegister() {
  if (SpilledScope::is_spilled()) return r0;

  EnsureOneFreeTOSRegister();
  return kTopRegister[kStateAfterPush[top_of_stack_state_]];
}


void VirtualFrame::EmitPush(Operand operand) {
  element_count_++;
  if (SpilledScope::is_spilled()) {
    __ mov(r0, operand);
    __ push(r0);
    return;
  }
  EnsureOneFreeTOSRegister();
  top_of_stack_state_ = kStateAfterPush[top_of_stack_state_];
  __ mov(kTopRegister[top_of_stack_state_], operand);
}


void VirtualFrame::EmitPush(MemOperand operand) {
  element_count_++;
  if (SpilledScope::is_spilled()) {
    __ ldr(r0, operand);
    __ push(r0);
    return;
  }
  EnsureOneFreeTOSRegister();
  top_of_stack_state_ = kStateAfterPush[top_of_stack_state_];
  __ ldr(kTopRegister[top_of_stack_state_], operand);
}


void VirtualFrame::EmitPushRoot(Heap::RootListIndex index) {
  element_count_++;
  if (SpilledScope::is_spilled()) {
    __ LoadRoot(r0, index);
    __ push(r0);
    return;
  }
  EnsureOneFreeTOSRegister();
  top_of_stack_state_ = kStateAfterPush[top_of_stack_state_];
  __ LoadRoot(kTopRegister[top_of_stack_state_], index);
}


void VirtualFrame::EmitPushMultiple(int count, int src_regs) {
  ASSERT(SpilledScope::is_spilled());
  Adjust(count);
  __ stm(db_w, sp, src_regs);
}


void VirtualFrame::SpillAll() {
  switch (top_of_stack_state_) {
    case R1_R0_TOS:
      masm()->push(r0);
      // Fall through.
    case R1_TOS:
      masm()->push(r1);
      top_of_stack_state_ = NO_TOS_REGISTERS;
      break;
    case R0_R1_TOS:
      masm()->push(r1);
      // Fall through.
    case R0_TOS:
      masm()->push(r0);
      top_of_stack_state_ = NO_TOS_REGISTERS;
      // Fall through.
    case NO_TOS_REGISTERS:
      break;
    default:
      UNREACHABLE();
      break;
  }
  ASSERT(register_allocation_map_ == 0);  // Not yet implemented.
}

#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_ARM
