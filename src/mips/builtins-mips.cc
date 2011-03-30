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

#if defined(V8_TARGET_ARCH_MIPS)

#include "codegen-inl.h"
#include "debug.h"
#include "deoptimizer.h"
#include "full-codegen.h"
#include "runtime.h"

namespace v8 {
namespace internal {


#define __ ACCESS_MASM(masm)


void Builtins::Generate_Adaptor(MacroAssembler* masm,
                                CFunctionId id,
                                BuiltinExtraArguments extra_args) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_ArrayCode(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_ArrayConstructCode(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_StringConstructCode(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_JSConstructCall(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_JSConstructStubCountdown(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_JSConstructStubGeneric(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_JSConstructStubApi(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_JSEntryTrampoline(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_JSConstructEntryTrampoline(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_LazyCompile(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_LazyRecompile(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_NotifyDeoptimized(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_NotifyLazyDeoptimized(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_NotifyOSR(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_OnStackReplacement(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_FunctionCall(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_FunctionApply(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void Builtins::Generate_ArgumentsAdaptorTrampoline(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_MIPS
