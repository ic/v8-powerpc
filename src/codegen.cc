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

#include "bootstrapper.h"
#include "codegen-inl.h"
#include "compiler.h"
#include "debug.h"
#include "prettyprinter.h"
#include "register-allocator-inl.h"
#include "rewriter.h"
#include "runtime.h"
#include "scopeinfo.h"
#include "stub-cache.h"
#include "virtual-frame-inl.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm_)

#ifdef DEBUG

Comment::Comment(MacroAssembler* masm, const char* msg)
    : masm_(masm), msg_(msg) {
  __ RecordComment(msg);
}


Comment::~Comment() {
  if (msg_[0] == '[') __ RecordComment("]");
}

#endif  // DEBUG

#undef __


void CodeGenerator::ProcessDeferred() {
  while (!deferred_.is_empty()) {
    DeferredCode* code = deferred_.RemoveLast();
    ASSERT(masm_ == code->masm());
    // Record position of deferred code stub.
    masm_->positions_recorder()->RecordStatementPosition(
        code->statement_position());
    if (code->position() != RelocInfo::kNoPosition) {
      masm_->positions_recorder()->RecordPosition(code->position());
    }
    // Generate the code.
    Comment cmnt(masm_, code->comment());
    masm_->bind(code->entry_label());
    if (code->AutoSaveAndRestore()) {
      code->SaveRegisters();
    }
    code->Generate();
    if (code->AutoSaveAndRestore()) {
      code->RestoreRegisters();
      code->Exit();
    }
  }
}


void DeferredCode::Exit() {
  masm_->jmp(exit_label());
}


void CodeGenerator::SetFrame(VirtualFrame* new_frame,
                             RegisterFile* non_frame_registers) {
  RegisterFile saved_counts;
  if (has_valid_frame()) {
    frame_->DetachFromCodeGenerator();
    // The remaining register reference counts are the non-frame ones.
    allocator_->SaveTo(&saved_counts);
  }

  if (new_frame != NULL) {
    // Restore the non-frame register references that go with the new frame.
    allocator_->RestoreFrom(non_frame_registers);
    new_frame->AttachToCodeGenerator();
  }

  frame_ = new_frame;
  saved_counts.CopyTo(non_frame_registers);
}


void CodeGenerator::DeleteFrame() {
  if (has_valid_frame()) {
    frame_->DetachFromCodeGenerator();
    frame_ = NULL;
  }
}


void CodeGenerator::MakeCodePrologue(CompilationInfo* info) {
#ifdef DEBUG
  bool print_source = false;
  bool print_ast = false;
  bool print_json_ast = false;
  const char* ftype;

  if (Isolate::Current()->bootstrapper()->IsActive()) {
    print_source = FLAG_print_builtin_source;
    print_ast = FLAG_print_builtin_ast;
    print_json_ast = FLAG_print_builtin_json_ast;
    ftype = "builtin";
  } else {
    print_source = FLAG_print_source;
    print_ast = FLAG_print_ast;
    print_json_ast = FLAG_print_json_ast;
    Vector<const char> filter = CStrVector(FLAG_hydrogen_filter);
    if (print_source && !filter.is_empty()) {
      print_source = info->function()->name()->IsEqualTo(filter);
    }
    if (print_ast && !filter.is_empty()) {
      print_ast = info->function()->name()->IsEqualTo(filter);
    }
    if (print_json_ast && !filter.is_empty()) {
      print_json_ast = info->function()->name()->IsEqualTo(filter);
    }
    ftype = "user-defined";
  }

  if (FLAG_trace_codegen || print_source || print_ast) {
    PrintF("*** Generate code for %s function: ", ftype);
    info->function()->name()->ShortPrint();
    PrintF(" ***\n");
  }

  if (print_source) {
    PrintF("--- Source from AST ---\n%s\n",
           PrettyPrinter().PrintProgram(info->function()));
  }

  if (print_ast) {
    PrintF("--- AST ---\n%s\n",
           AstPrinter().PrintProgram(info->function()));
  }

  if (print_json_ast) {
    JsonAstBuilder builder;
    PrintF("%s", builder.BuildProgram(info->function()));
  }
#endif  // DEBUG
}


Handle<Code> CodeGenerator::MakeCodeEpilogue(MacroAssembler* masm,
                                             Code::Flags flags,
                                             CompilationInfo* info) {
  Isolate* isolate = info->isolate();

  // Allocate and install the code.
  CodeDesc desc;
  masm->GetCode(&desc);
  Handle<Code> code =
      isolate->factory()->NewCode(desc, flags, masm->CodeObject());

  if (!code.is_null()) {
    isolate->counters()->total_compiled_code_size()->Increment(
        code->instruction_size());
  }
  return code;
}


void CodeGenerator::PrintCode(Handle<Code> code, CompilationInfo* info) {
#ifdef ENABLE_DISASSEMBLER
  bool print_code = Isolate::Current()->bootstrapper()->IsActive()
      ? FLAG_print_builtin_code
      : (FLAG_print_code || (info->IsOptimizing() && FLAG_print_opt_code));
  Vector<const char> filter = CStrVector(FLAG_hydrogen_filter);
  FunctionLiteral* function = info->function();
  bool match = filter.is_empty() || function->debug_name()->IsEqualTo(filter);
  if (print_code && match) {
    // Print the source code if available.
    Handle<Script> script = info->script();
    if (!script->IsUndefined() && !script->source()->IsUndefined()) {
      PrintF("--- Raw source ---\n");
      StringInputBuffer stream(String::cast(script->source()));
      stream.Seek(function->start_position());
      // fun->end_position() points to the last character in the stream. We
      // need to compensate by adding one to calculate the length.
      int source_len =
          function->end_position() - function->start_position() + 1;
      for (int i = 0; i < source_len; i++) {
        if (stream.has_more()) PrintF("%c", stream.GetNext());
      }
      PrintF("\n\n");
    }
    if (info->IsOptimizing()) {
      if (FLAG_print_unopt_code) {
        PrintF("--- Unoptimized code ---\n");
        info->closure()->shared()->code()->Disassemble(
            *function->debug_name()->ToCString());
      }
      PrintF("--- Optimized code ---\n");
    } else {
      PrintF("--- Code ---\n");
    }
    code->Disassemble(*function->debug_name()->ToCString());
  }
#endif  // ENABLE_DISASSEMBLER
}


// Generate the code.  Compile the AST and assemble all the pieces into a
// Code object.
bool CodeGenerator::MakeCode(CompilationInfo* info) {
  // When using Crankshaft the classic backend should never be used.
  ASSERT(!V8::UseCrankshaft());
  Handle<Script> script = info->script();
  if (!script->IsUndefined() && !script->source()->IsUndefined()) {
    int len = String::cast(script->source())->length();
    Counters* counters = info->isolate()->counters();
    counters->total_old_codegen_source_size()->Increment(len);
  }
  if (FLAG_trace_codegen) {
    PrintF("Classic Compiler - ");
  }
  MakeCodePrologue(info);
  // Generate code.
  const int kInitialBufferSize = 4 * KB;
  MacroAssembler masm(info->isolate(), NULL, kInitialBufferSize);
#ifdef ENABLE_GDB_JIT_INTERFACE
  masm.positions_recorder()->StartGDBJITLineInfoRecording();
#endif
  CodeGenerator cgen(&masm);
  CodeGeneratorScope scope(Isolate::Current(), &cgen);
  cgen.Generate(info);
  if (cgen.HasStackOverflow()) {
    ASSERT(!Isolate::Current()->has_pending_exception());
    return false;
  }

  InLoopFlag in_loop = info->is_in_loop() ? IN_LOOP : NOT_IN_LOOP;
  Code::Flags flags = Code::ComputeFlags(Code::FUNCTION, in_loop);
  Handle<Code> code = MakeCodeEpilogue(cgen.masm(), flags, info);
  // There is no stack check table in code generated by the classic backend.
  code->SetNoStackCheckTable();
  CodeGenerator::PrintCode(code, info);
  info->SetCode(code);  // May be an empty handle.
#ifdef ENABLE_GDB_JIT_INTERFACE
  if (FLAG_gdbjit && !code.is_null()) {
    GDBJITLineInfo* lineinfo =
        masm.positions_recorder()->DetachGDBJITLineInfo();

    GDBJIT(RegisterDetailedLineInfo(*code, lineinfo));
  }
#endif
  return !code.is_null();
}


#ifdef ENABLE_LOGGING_AND_PROFILING


static Vector<const char> kRegexp = CStrVector("regexp");


bool CodeGenerator::ShouldGenerateLog(Expression* type) {
  ASSERT(type != NULL);
  if (!LOGGER->is_logging() && !CpuProfiler::is_profiling()) return false;
  Handle<String> name = Handle<String>::cast(type->AsLiteral()->handle());
  if (FLAG_log_regexp) {
    if (name->IsEqualTo(kRegexp))
      return true;
  }
  return false;
}

#endif


void CodeGenerator::ProcessDeclarations(ZoneList<Declaration*>* declarations) {
  int length = declarations->length();
  int globals = 0;
  for (int i = 0; i < length; i++) {
    Declaration* node = declarations->at(i);
    Variable* var = node->proxy()->var();
    Slot* slot = var->AsSlot();

    // If it was not possible to allocate the variable at compile
    // time, we need to "declare" it at runtime to make sure it
    // actually exists in the local context.
    if ((slot != NULL && slot->type() == Slot::LOOKUP) || !var->is_global()) {
      VisitDeclaration(node);
    } else {
      // Count global variables and functions for later processing
      globals++;
    }
  }

  // Return in case of no declared global functions or variables.
  if (globals == 0) return;

  // Compute array of global variable and function declarations.
  Handle<FixedArray> array = FACTORY->NewFixedArray(2 * globals, TENURED);
  for (int j = 0, i = 0; i < length; i++) {
    Declaration* node = declarations->at(i);
    Variable* var = node->proxy()->var();
    Slot* slot = var->AsSlot();

    if ((slot != NULL && slot->type() == Slot::LOOKUP) || !var->is_global()) {
      // Skip - already processed.
    } else {
      array->set(j++, *(var->name()));
      if (node->fun() == NULL) {
        if (var->mode() == Variable::CONST) {
          // In case this is const property use the hole.
          array->set_the_hole(j++);
        } else {
          array->set_undefined(j++);
        }
      } else {
        Handle<SharedFunctionInfo> function =
            Compiler::BuildFunctionInfo(node->fun(), script());
        // Check for stack-overflow exception.
        if (function.is_null()) {
          SetStackOverflow();
          return;
        }
        array->set(j++, *function);
      }
    }
  }

  // Invoke the platform-dependent code generator to do the actual
  // declaration the global variables and functions.
  DeclareGlobals(array);
}


void CodeGenerator::VisitIncrementOperation(IncrementOperation* expr) {
  UNREACHABLE();
}


// Lookup table for code generators for special runtime calls which are
// generated inline.
#define INLINE_FUNCTION_GENERATOR_ADDRESS(Name, argc, ressize)          \
    &CodeGenerator::Generate##Name,

const CodeGenerator::InlineFunctionGenerator
    CodeGenerator::kInlineFunctionGenerators[] = {
        INLINE_FUNCTION_LIST(INLINE_FUNCTION_GENERATOR_ADDRESS)
        INLINE_RUNTIME_FUNCTION_LIST(INLINE_FUNCTION_GENERATOR_ADDRESS)
};
#undef INLINE_FUNCTION_GENERATOR_ADDRESS


bool CodeGenerator::CheckForInlineRuntimeCall(CallRuntime* node) {
  ZoneList<Expression*>* args = node->arguments();
  Handle<String> name = node->name();
  const Runtime::Function* function = node->function();
  if (function != NULL && function->intrinsic_type == Runtime::INLINE) {
    int lookup_index = static_cast<int>(function->function_id) -
        static_cast<int>(Runtime::kFirstInlineFunction);
    ASSERT(lookup_index >= 0);
    ASSERT(static_cast<size_t>(lookup_index) <
           ARRAY_SIZE(kInlineFunctionGenerators));
    InlineFunctionGenerator generator = kInlineFunctionGenerators[lookup_index];
    (this->*generator)(args);
    return true;
  }
  return false;
}


// Simple condition analysis.  ALWAYS_TRUE and ALWAYS_FALSE represent a
// known result for the test expression, with no side effects.
CodeGenerator::ConditionAnalysis CodeGenerator::AnalyzeCondition(
    Expression* cond) {
  if (cond == NULL) return ALWAYS_TRUE;

  Literal* lit = cond->AsLiteral();
  if (lit == NULL) return DONT_KNOW;

  if (lit->IsTrue()) {
    return ALWAYS_TRUE;
  } else if (lit->IsFalse()) {
    return ALWAYS_FALSE;
  }

  return DONT_KNOW;
}


bool CodeGenerator::RecordPositions(MacroAssembler* masm,
                                    int pos,
                                    bool right_here) {
  if (pos != RelocInfo::kNoPosition) {
    masm->positions_recorder()->RecordStatementPosition(pos);
    masm->positions_recorder()->RecordPosition(pos);
    if (right_here) {
      return masm->positions_recorder()->WriteRecordedPositions();
    }
  }
  return false;
}


void CodeGenerator::CodeForFunctionPosition(FunctionLiteral* fun) {
  if (FLAG_debug_info) RecordPositions(masm(), fun->start_position(), false);
}


void CodeGenerator::CodeForReturnPosition(FunctionLiteral* fun) {
  if (FLAG_debug_info) RecordPositions(masm(), fun->end_position() - 1, false);
}


void CodeGenerator::CodeForStatementPosition(Statement* stmt) {
  if (FLAG_debug_info) RecordPositions(masm(), stmt->statement_pos(), false);
}


void CodeGenerator::CodeForDoWhileConditionPosition(DoWhileStatement* stmt) {
  if (FLAG_debug_info)
    RecordPositions(masm(), stmt->condition_position(), false);
}


void CodeGenerator::CodeForSourcePosition(int pos) {
  if (FLAG_debug_info && pos != RelocInfo::kNoPosition) {
    masm()->positions_recorder()->RecordPosition(pos);
  }
}


const char* GenericUnaryOpStub::GetName() {
  switch (op_) {
    case Token::SUB:
      if (negative_zero_ == kStrictNegativeZero) {
        return overwrite_ == UNARY_OVERWRITE
            ? "GenericUnaryOpStub_SUB_Overwrite_Strict0"
            : "GenericUnaryOpStub_SUB_Alloc_Strict0";
      } else {
        return overwrite_ == UNARY_OVERWRITE
            ? "GenericUnaryOpStub_SUB_Overwrite_Ignore0"
            : "GenericUnaryOpStub_SUB_Alloc_Ignore0";
      }
    case Token::BIT_NOT:
      return overwrite_ == UNARY_OVERWRITE
          ? "GenericUnaryOpStub_BIT_NOT_Overwrite"
          : "GenericUnaryOpStub_BIT_NOT_Alloc";
    default:
      UNREACHABLE();
      return "<unknown>";
  }
}


void ArgumentsAccessStub::Generate(MacroAssembler* masm) {
  switch (type_) {
    case READ_ELEMENT:
      GenerateReadElement(masm);
      break;
    case NEW_NON_STRICT:
    case NEW_STRICT:
      GenerateNewObject(masm);
      break;
  }
}


int CEntryStub::MinorKey() {
  ASSERT(result_size_ == 1 || result_size_ == 2);
  int result = save_doubles_ ? 1 : 0;
#ifdef _WIN64
  return result | ((result_size_ == 1) ? 0 : 2);
#else
  return result;
#endif
}


} }  // namespace v8::internal
