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

#if defined(V8_TARGET_ARCH_IA32)

#include "codegen-inl.h"
#include "debug.h"


namespace v8 {
namespace internal {

#ifdef ENABLE_DEBUGGER_SUPPORT

bool BreakLocationIterator::IsDebugBreakAtReturn() {
  return Debug::IsDebugBreakAtReturn(rinfo());
}


// Patch the JS frame exit code with a debug break call. See
// CodeGenerator::VisitReturnStatement and VirtualFrame::Exit in codegen-ia32.cc
// for the precise return instructions sequence.
void BreakLocationIterator::SetDebugBreakAtReturn() {
  ASSERT(Assembler::kJSReturnSequenceLength >=
         Assembler::kCallInstructionLength);
  rinfo()->PatchCodeWithCall(Debug::debug_break_return()->entry(),
      Assembler::kJSReturnSequenceLength - Assembler::kCallInstructionLength);
}


// Restore the JS frame exit code.
void BreakLocationIterator::ClearDebugBreakAtReturn() {
  rinfo()->PatchCode(original_rinfo()->pc(),
                     Assembler::kJSReturnSequenceLength);
}


// A debug break in the frame exit code is identified by the JS frame exit code
// having been patched with a call instruction.
bool Debug::IsDebugBreakAtReturn(RelocInfo* rinfo) {
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
  __ Set(eax, Immediate(0));  // no arguments
  __ mov(ebx, Immediate(ExternalReference::debug_break()));

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
    __ pop(eax);
  }

  // Finally restore all registers.
  __ RestoreRegistersFromMemory(kJSCallerSaved);

  // Now that the break point has been handled, resume normal execution by
  // jumping to the target address intended by the caller and that was
  // overwritten by the address of DebugBreakXXX.
  ExternalReference after_break_target =
      ExternalReference(Debug_Address::AfterBreakTarget());
  __ jmp(Operand::StaticVariable(after_break_target));
}


void Debug::GenerateLoadICDebugBreak(MacroAssembler* masm) {
  // Register state for IC load call (from ic-ia32.cc).
  // ----------- S t a t e -------------
  //  -- eax    : receiver
  //  -- ecx    : name
  // -----------------------------------
  Generate_DebugBreakCallHelper(masm, eax.bit() | ecx.bit(), false);
}


void Debug::GenerateStoreICDebugBreak(MacroAssembler* masm) {
  // Register state for IC store call (from ic-ia32.cc).
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : name
  //  -- edx    : receiver
  // -----------------------------------
  Generate_DebugBreakCallHelper(masm, eax.bit() | ecx.bit() | edx.bit(), false);
}


void Debug::GenerateKeyedLoadICDebugBreak(MacroAssembler* masm) {
  // Register state for keyed IC load call (from ic-ia32.cc).
  // ----------- S t a t e -------------
  //  -- edx    : receiver
  //  -- eax    : key
  // -----------------------------------
  Generate_DebugBreakCallHelper(masm, eax.bit() | edx.bit(), false);
}


void Debug::GenerateKeyedStoreICDebugBreak(MacroAssembler* masm) {
  // Register state for keyed IC load call (from ic-ia32.cc).
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : key
  //  -- edx    : receiver
  // -----------------------------------
  // Register eax contains an object that needs to be pushed on the
  // expression stack of the fake JS frame.
  Generate_DebugBreakCallHelper(masm, eax.bit() | ecx.bit() | edx.bit(), false);
}


void Debug::GenerateCallICDebugBreak(MacroAssembler* masm) {
  // Register state for keyed IC call call (from ic-ia32.cc)
  // ----------- S t a t e -------------
  //  -- eax: number of arguments
  // -----------------------------------
  // The number of arguments in eax is not smi encoded.
  Generate_DebugBreakCallHelper(masm, 0, false);
}


void Debug::GenerateConstructCallDebugBreak(MacroAssembler* masm) {
  // Register state just before return from JS function (from codegen-ia32.cc).
  // eax is the actual number of arguments not encoded as a smi see comment
  // above IC call.
  // ----------- S t a t e -------------
  //  -- eax: number of arguments
  // -----------------------------------
  // The number of arguments in eax is not smi encoded.
  Generate_DebugBreakCallHelper(masm, 0, false);
}


void Debug::GenerateReturnDebugBreak(MacroAssembler* masm) {
  // Register state just before return from JS function (from codegen-ia32.cc).
  // ----------- S t a t e -------------
  //  -- eax: return value
  // -----------------------------------
  Generate_DebugBreakCallHelper(masm, eax.bit(), true);
}


void Debug::GenerateStubNoRegistersDebugBreak(MacroAssembler* masm) {
  // Register state for stub CallFunction (from CallFunctionStub in ic-ia32.cc).
  // ----------- S t a t e -------------
  //  No registers used on entry.
  // -----------------------------------
  Generate_DebugBreakCallHelper(masm, 0, false);
}


void Debug::GeneratePlainReturnLiveEdit(MacroAssembler* masm) {
  masm->ret(0);
}

// FrameDropper is a code replacement for a JavaScript frame with possibly
// several frames above.
// There is no calling conventions here, because it never actually gets called,
// it only gets returned to.
// Frame structure (conforms InternalFrame structure):
//   -- JSFunction
//   -- code
//   -- SMI maker
//   -- context
//   -- frame base
void Debug::GenerateFrameDropperLiveEdit(MacroAssembler* masm) {
  // We do not know our frame height, but set esp based on ebp.
  __ lea(esp, Operand(ebp, -4 * kPointerSize));

  __ pop(edi);  // function

  // Skip code self-reference and marker.
  __ add(Operand(esp), Immediate(2 * kPointerSize));

  __ pop(esi);  // Context.
  __ pop(ebp);

  // Get function code.
  __ mov(edx, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
  __ mov(edx, FieldOperand(edx, SharedFunctionInfo::kCodeOffset));
  __ lea(edx, FieldOperand(edx, Code::kHeaderSize));

  // Re-run JSFunction, edi is function, esi is context.
  __ jmp(Operand(edx));
}

#undef __


void Debug::SetUpFrameDropperFrame(StackFrame* bottom_js_frame,
                                   Handle<Code> code) {
  ASSERT(bottom_js_frame->is_java_script());

  Address fp = bottom_js_frame->fp();
  Memory::Object_at(fp - 4 * kPointerSize) =
      Memory::Object_at(fp - 2 * kPointerSize);  // Move edi (function).

  Memory::Object_at(fp - 3 * kPointerSize) = *code;
  Memory::Object_at(fp - 2 * kPointerSize) = Smi::FromInt(StackFrame::INTERNAL);
}
const int Debug::kFrameDropperFrameSize = 5;


#endif  // ENABLE_DEBUGGER_SUPPORT

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_IA32
