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

#ifndef V8_DATAFLOW_H_
#define V8_DATAFLOW_H_

#include "ast.h"
#include "compiler.h"

namespace v8 {
namespace internal {

// This class is used to number all expressions in the AST according to
// their evaluation order (post-order left-to-right traversal).
class AstLabeler: public AstVisitor {
 public:
  AstLabeler() : next_number_(0) {}

  void Label(CompilationInfo* info);

 private:
  CompilationInfo* info() { return info_; }

  void VisitDeclarations(ZoneList<Declaration*>* decls);
  void VisitStatements(ZoneList<Statement*>* stmts);

  // AST node visit functions.
#define DECLARE_VISIT(type) virtual void Visit##type(type* node);
  AST_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT

  // Traversal number for labelling AST nodes.
  int next_number_;

  CompilationInfo* info_;

  DISALLOW_COPY_AND_ASSIGN(AstLabeler);
};


class VarUseMap : public HashMap {
 public:
  VarUseMap() : HashMap(VarMatch) {}

  ZoneList<Expression*>* Lookup(Variable* var);

 private:
  static bool VarMatch(void* key1, void* key2) { return key1 == key2; }
};


class DefinitionInfo : public ZoneObject {
 public:
  explicit DefinitionInfo() : last_use_(NULL) {}

  Expression* last_use() { return last_use_; }
  void set_last_use(Expression* expr) { last_use_ = expr; }

 private:
  Expression* last_use_;
  Register location_;
};


class LivenessAnalyzer : public AstVisitor {
 public:
  LivenessAnalyzer() {}

  void Analyze(FunctionLiteral* fun);

 private:
  void VisitStatements(ZoneList<Statement*>* stmts);

  void RecordUse(Variable* var, Expression* expr);
  void RecordDef(Variable* var, Expression* expr);


  // AST node visit functions.
#define DECLARE_VISIT(type) virtual void Visit##type(type* node);
  AST_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT

  // Map for tracking the live variables.
  VarUseMap live_vars_;

  DISALLOW_COPY_AND_ASSIGN(LivenessAnalyzer);
};


} }  // namespace v8::internal


#endif  // V8_DATAFLOW_H_
