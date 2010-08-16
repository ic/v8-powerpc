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


#include "v8.h"

#if defined(V8_TARGET_ARCH_X64)

#include "codegen-inl.h"
#include "debug.h"


namespace v8 {
namespace internal {

#ifdef ENABLE_DEBUGGER_SUPPORT

bool Debug::IsDebugBreakAtReturn(v8::internal::RelocInfo* rinfo) {
  ASSERT(RelocInfo::IsJSReturn(rinfo->rmode()));
  return rinfo->IsPatchedReturnSequence();
}

#define __ ACCESS_MASM(masm)

static void Generate_DebugBreakCallHelper(MacroAssembler* masm,
                                          RegList pointer_regs,
                                          bool convert_call_to_jmp) {
  // Save the content of all general purpose registers in memory. This copy in
  // memory is later pushed onto the JS expression stack for the fake JS frame
  // generated and also to the C frame generated on top of that. In the JS
  // frame ONLY the registers containing pointers will be pushed on the
  // expression stack. This causes the GC to update these pointers so that
  // they will have the correct value when returning from the debugger.
  __ SaveRegistersToMemory(kJSCallerSaved);

  // Enter an internal frame.
  __ EnterInternalFrame();

  // Store the registers containing object pointers on the expression stack to
  // make sure that these are correctly updated during GC.
  __ PushRegistersFromMemory(pointer_regs);

#ifdef DEBUG
  __ RecordComment("// Calling from debug break to runtime - come in - over");
#endif
  __ xor_(rax, rax);  // No arguments (argc == 0).
  __ movq(rbx, ExternalReference::debug_break());

  CEntryStub ceb(1, ExitFrame::MODE_DEBUG);
  __ CallStub(&ceb);

  // Restore the register values containing object pointers from the expression
  // stack in the reverse order as they where pushed.
  __ PopRegistersToMemory(pointer_regs);

  // Get rid of the internal frame.
  __ LeaveInternalFrame();

  // If this call did not replace a call but patched other code then there will
  // be an unwanted return address left on the stack. Here we get rid of that.
  if (convert_call_to_jmp) {
    __ pop(rax);
  }

  // Finally restore all registers.
  __ RestoreRegistersFromMemory(kJSCallerSaved);

  // Now that the break point has been handled, resume normal execution by
  // jumping to the target address intended by the caller and that was
  // overwritten by the address of DebugBreakXXX.
  ExternalReference after_break_target =
      ExternalReference(Debug_Address::AfterBreakTarget());
  __ movq(kScratchRegister, after_break_target);
  __ jmp(Operand(kScratchRegister, 0));
}


void Debug::GenerateCallICDebugBreak(MacroAssembler* masm) {
  // Register state for keyed IC call call (from ic-x64.cc)
  // ----------- S t a t e -------------
  //  -- rax: number of arguments
  // -----------------------------------
  // The number of arguments in rax is not smi encoded.
  Generate_DebugBreakCallHelper(masm, 0, false);
}


void Debug::GenerateConstructCallDebugBreak(MacroAssembler* masm) {
  // Register state just before return from JS function (from codegen-x64.cc).
  // rax is the actual number of arguments not encoded as a smi, see comment
  // above IC call.
  // ----------- S t a t e -------------
  //  -- rax: number of arguments
  // -----------------------------------
  // The number of arguments in rax is not smi encoded.
  Generate_DebugBreakCallHelper(masm, 0, false);
}


void Debug::GenerateKeyedLoadICDebugBreak(MacroAssembler* masm) {
  // Register state for keyed IC load call (from ic-x64.cc).
  // ----------- S t a t e -------------
  //  -- rax     : key
  //  -- rdx     : receiver
  // -----------------------------------
  Generate_DebugBreakCallHelper(masm, rax.bit() | rdx.bit(), false);
}


void Debug::GenerateKeyedStoreICDebugBreak(MacroAssembler* masm) {
  // Register state for keyed IC load call (from ic-x64.cc).
  // ----------- S t a t e -------------
  //  -- rax    : value
  //  -- rcx    : key
  //  -- rdx    : receiver
  // -----------------------------------
  Generate_DebugBreakCallHelper(masm, rax.bit() | rcx.bit() | rdx.bit(), false);
}


void Debug::GenerateLoadICDebugBreak(MacroAssembler* masm) {
  // Register state for IC load call (from ic-x64.cc).
  // ----------- S t a t e -------------
  //  -- rax    : receiver
  //  -- rcx    : name
  // -----------------------------------
  Generate_DebugBreakCallHelper(masm, rax.bit() | rcx.bit(), false);
}


void Debug::GenerateReturnDebugBreak(MacroAssembler* masm) {
  // Register state just before return from JS function (from codegen-x64.cc).
  // ----------- S t a t e -------------
  //  -- rax: return value
  // -----------------------------------
  Generate_DebugBreakCallHelper(masm, rax.bit(), true);
}


void Debug::GenerateStoreICDebugBreak(MacroAssembler* masm) {
  // Register state for IC store call (from ic-x64.cc).
  // ----------- S t a t e -------------
  //  -- rax    : value
  //  -- rcx    : name
  //  -- rdx    : receiver
  // -----------------------------------
  Generate_DebugBreakCallHelper(masm, rax.bit() | rcx.bit() | rdx.bit(), false);
}


void Debug::GenerateStubNoRegistersDebugBreak(MacroAssembler* masm) {
  // Register state for stub CallFunction (from CallFunctionStub in ic-x64.cc).
  // ----------- S t a t e -------------
  //  No registers used on entry.
  // -----------------------------------
  Generate_DebugBreakCallHelper(masm, 0, false);
}


void Debug::GenerateSlot(MacroAssembler* masm) {
  // Generate enough nop's to make space for a call instruction.
  Label check_codesize;
  __ bind(&check_codesize);
  __ RecordDebugBreakSlot();
  for (int i = 0; i < Assembler::kDebugBreakSlotLength; i++) {
    __ nop();
  }
  ASSERT_EQ(Assembler::kDebugBreakSlotLength,
            masm->SizeOfCodeGeneratedSince(&check_codesize));
}


void Debug::GenerateSlotDebugBreak(MacroAssembler* masm) {
  // In the places where a debug break slot is inserted no registers can contain
  // object pointers.
  Generate_DebugBreakCallHelper(masm, 0, true);
}


void Debug::GeneratePlainReturnLiveEdit(MacroAssembler* masm) {
  masm->ret(0);
}


void Debug::GenerateFrameDropperLiveEdit(MacroAssembler* masm) {
  ExternalReference restarter_frame_function_slot =
      ExternalReference(Debug_Address::RestarterFrameFunctionPointer());
  __ movq(rax, restarter_frame_function_slot);
  __ movq(Operand(rax, 0), Immediate(0));

  // We do not know our frame height, but set rsp based on rbp.
  __ lea(rsp, Operand(rbp, -1 * kPointerSize));

  __ pop(rdi);  // Function.
  __ pop(rbp);

  // Load context from the function.
  __ movq(rsi, FieldOperand(rdi, JSFunction::kContextOffset));

  // Get function code.
  __ movq(rdx, FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
  __ movq(rdx, FieldOperand(rdx, SharedFunctionInfo::kCodeOffset));
  __ lea(rdx, FieldOperand(rdx, Code::kHeaderSize));

  // Re-run JSFunction, rdi is function, rsi is context.
  __ jmp(rdx);
}

const bool Debug::kFrameDropperSupported = true;

#undef __




void BreakLocationIterator::ClearDebugBreakAtReturn() {
  rinfo()->PatchCode(original_rinfo()->pc(),
                     Assembler::kJSReturnSequenceLength);
}


bool BreakLocationIterator::IsDebugBreakAtReturn()  {
  return Debug::IsDebugBreakAtReturn(rinfo());
}


void BreakLocationIterator::SetDebugBreakAtReturn()  {
  ASSERT(Assembler::kJSReturnSequenceLength >=
         Assembler::kCallInstructionLength);
  rinfo()->PatchCodeWithCall(Debug::debug_break_return()->entry(),
      Assembler::kJSReturnSequenceLength - Assembler::kCallInstructionLength);
}


bool BreakLocationIterator::IsDebugBreakAtSlot() {
  ASSERT(IsDebugBreakSlot());
  // Check whether the debug break slot instructions have been patched.
  return !Assembler::IsNop(rinfo()->pc());
}


void BreakLocationIterator::SetDebugBreakAtSlot() {
  ASSERT(IsDebugBreakSlot());
  rinfo()->PatchCodeWithCall(
      Debug::debug_break_slot()->entry(),
      Assembler::kDebugBreakSlotLength - Assembler::kCallInstructionLength);
}


void BreakLocationIterator::ClearDebugBreakAtSlot() {
  ASSERT(IsDebugBreakSlot());
  rinfo()->PatchCode(original_rinfo()->pc(), Assembler::kDebugBreakSlotLength);
}


#endif  // ENABLE_DEBUGGER_SUPPORT

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_X64
