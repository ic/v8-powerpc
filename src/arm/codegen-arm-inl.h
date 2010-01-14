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


#ifndef V8_ARM_CODEGEN_ARM_INL_H_
#define V8_ARM_CODEGEN_ARM_INL_H_

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm_)

void CodeGenerator::LoadConditionAndSpill(Expression* expression,
                                          JumpTarget* true_target,
                                          JumpTarget* false_target,
                                          bool force_control) {
  LoadCondition(expression, true_target, false_target, force_control);
}


void CodeGenerator::LoadAndSpill(Expression* expression) {
  Load(expression);
}


void CodeGenerator::VisitAndSpill(Statement* statement) {
  Visit(statement);
}


void CodeGenerator::VisitStatementsAndSpill(ZoneList<Statement*>* statements) {
  VisitStatements(statements);
}


void Reference::GetValueAndSpill() {
  GetValue();
}


// Platform-specific inline functions.

void DeferredCode::Jump() { __ jmp(&entry_label_); }
void DeferredCode::Branch(Condition cc) { __ b(cc, &entry_label_); }

#undef __

} }  // namespace v8::internal

#endif  // V8_ARM_CODEGEN_ARM_INL_H_
