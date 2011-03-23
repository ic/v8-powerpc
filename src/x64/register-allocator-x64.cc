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

#if defined(V8_TARGET_ARCH_X64)

#include "codegen-inl.h"
#include "register-allocator-inl.h"
#include "virtual-frame-inl.h"

namespace v8 {
namespace internal {

// -------------------------------------------------------------------------
// Result implementation.

void Result::ToRegister() {
  ASSERT(is_valid());
  if (is_constant()) {
    CodeGenerator* code_generator =
        CodeGeneratorScope::Current(Isolate::Current());
    Result fresh = code_generator->allocator()->Allocate();
    ASSERT(fresh.is_valid());
    code_generator->masm()->Move(fresh.reg(), handle());
    // This result becomes a copy of the fresh one.
    fresh.set_type_info(type_info());
    *this = fresh;
  }
  ASSERT(is_register());
}


void Result::ToRegister(Register target) {
  ASSERT(is_valid());
  CodeGenerator* code_generator =
      CodeGeneratorScope::Current(Isolate::Current());
  if (!is_register() || !reg().is(target)) {
    Result fresh = code_generator->allocator()->Allocate(target);
    ASSERT(fresh.is_valid());
    if (is_register()) {
      code_generator->masm()->movq(fresh.reg(), reg());
    } else {
      ASSERT(is_constant());
      code_generator->masm()->Move(fresh.reg(), handle());
    }
    fresh.set_type_info(type_info());
    *this = fresh;
  } else if (is_register() && reg().is(target)) {
    ASSERT(code_generator->has_valid_frame());
    code_generator->frame()->Spill(target);
    ASSERT(code_generator->allocator()->count(target) == 1);
  }
  ASSERT(is_register());
  ASSERT(reg().is(target));
}


// -------------------------------------------------------------------------
// RegisterAllocator implementation.

Result RegisterAllocator::AllocateByteRegisterWithoutSpilling() {
  // This function is not used in 64-bit code.
  UNREACHABLE();
  return Result();
}


} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_X64
