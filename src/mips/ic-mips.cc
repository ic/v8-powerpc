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

#include "codegen-inl.h"
#include "ic-inl.h"
#include "runtime.h"
#include "stub-cache.h"

namespace v8 {
namespace internal {


// ----------------------------------------------------------------------------
// Static IC stub generators.
//

#define __ ACCESS_MASM(masm)


void LoadIC::GenerateArrayLength(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void LoadIC::GenerateStringLength(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void LoadIC::GenerateFunctionPrototype(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


// Defined in ic.cc.
Object* CallIC_Miss(Arguments args);

void CallIC::GenerateMegamorphic(MacroAssembler* masm, int argc) {
  UNIMPLEMENTED_MIPS();
}


void CallIC::GenerateNormal(MacroAssembler* masm, int argc) {
  UNIMPLEMENTED_MIPS();
}

void CallIC::GenerateMiss(MacroAssembler* masm, int argc) {
  UNIMPLEMENTED_MIPS();
}

// Defined in ic.cc.
Object* LoadIC_Miss(Arguments args);

void LoadIC::GenerateMegamorphic(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void LoadIC::GenerateNormal(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void LoadIC::GenerateMiss(MacroAssembler* masm) {
  Generate(masm, ExternalReference(IC_Utility(kLoadIC_Miss)));
}


void LoadIC::Generate(MacroAssembler* masm, const ExternalReference& f) {
  UNIMPLEMENTED_MIPS();
}


void LoadIC::ClearInlinedVersion(Address address) {}
bool LoadIC::PatchInlinedLoad(Address address, Object* map, int offset) {
  return false;
}

void KeyedLoadIC::ClearInlinedVersion(Address address) {}
bool KeyedLoadIC::PatchInlinedLoad(Address address, Object* map) {
  return false;
}

void KeyedStoreIC::ClearInlinedVersion(Address address) {}
void KeyedStoreIC::RestoreInlinedVersion(Address address) {}
bool KeyedStoreIC::PatchInlinedStore(Address address, Object* map) {
  return false;
}


Object* KeyedLoadIC_Miss(Arguments args);


void KeyedLoadIC::GenerateMiss(MacroAssembler* masm) {
  Generate(masm, ExternalReference(IC_Utility(kKeyedLoadIC_Miss)));
}


void KeyedLoadIC::Generate(MacroAssembler* masm, const ExternalReference& f) {
  UNIMPLEMENTED_MIPS();
}


void KeyedLoadIC::GenerateGeneric(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void KeyedLoadIC::GenerateString(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void KeyedLoadIC::GenerateExternalArray(MacroAssembler* masm,
                                        ExternalArrayType array_type) {
  UNIMPLEMENTED_MIPS();
}


void KeyedStoreIC::Generate(MacroAssembler* masm,
                            const ExternalReference& f) {
  UNIMPLEMENTED_MIPS();
}


void KeyedStoreIC::GenerateGeneric(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void KeyedStoreIC::GenerateExternalArray(MacroAssembler* masm,
                                         ExternalArrayType array_type) {
  UNIMPLEMENTED_MIPS();
}


void KeyedStoreIC::GenerateExtendStorage(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void StoreIC::GenerateMegamorphic(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void StoreIC::GenerateExtendStorage(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}


void StoreIC::GenerateMiss(MacroAssembler* masm) {
  UNIMPLEMENTED_MIPS();
}

#undef __

} }  // namespace v8::internal

