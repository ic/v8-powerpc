// Copyright 2011 the V8 project authors. All rights reserved.
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

#ifndef V8_ARM_LITHIUM_CODEGEN_ARM_H_
#define V8_ARM_LITHIUM_CODEGEN_ARM_H_

#include "arm/lithium-arm.h"

#include "deoptimizer.h"
#include "safepoint-table.h"
#include "scopes.h"

namespace v8 {
namespace internal {

// Forward declarations.
class LDeferredCode;
class LGapNode;
class SafepointGenerator;

class LGapResolver BASE_EMBEDDED {
 public:
  LGapResolver();
  const ZoneList<LMoveOperands>* Resolve(const ZoneList<LMoveOperands>* moves,
                                         LOperand* marker_operand);

 private:
  LGapNode* LookupNode(LOperand* operand);
  bool CanReach(LGapNode* a, LGapNode* b, int visited_id);
  bool CanReach(LGapNode* a, LGapNode* b);
  void RegisterMove(LMoveOperands move);
  void AddResultMove(LOperand* from, LOperand* to);
  void AddResultMove(LGapNode* from, LGapNode* to);
  void ResolveCycle(LGapNode* start, LOperand* marker_operand);

  ZoneList<LGapNode*> nodes_;
  ZoneList<LGapNode*> identified_cycles_;
  ZoneList<LMoveOperands> result_;
  int next_visited_id_;
};


class LCodeGen BASE_EMBEDDED {
 public:
  LCodeGen(LChunk* chunk, MacroAssembler* assembler, CompilationInfo* info)
      : chunk_(chunk),
        masm_(assembler),
        info_(info),
        current_block_(-1),
        current_instruction_(-1),
        instructions_(chunk->instructions()),
        deoptimizations_(4),
        deoptimization_literals_(8),
        inlined_function_count_(0),
        scope_(chunk->graph()->info()->scope()),
        status_(UNUSED),
        deferred_(8),
        osr_pc_offset_(-1) {
    PopulateDeoptimizationLiteralsWithInlinedFunctions();
  }

  // Try to generate code for the entire chunk, but it may fail if the
  // chunk contains constructs we cannot handle. Returns true if the
  // code generation attempt succeeded.
  bool GenerateCode();

  // Finish the code by setting stack height, safepoint, and bailout
  // information on it.
  void FinishCode(Handle<Code> code);

  // Deferred code support.
  template<int T>
  void DoDeferredGenericBinaryStub(LTemplateInstruction<1, 2, T>* instr,
                                   Token::Value op);
  void DoDeferredNumberTagD(LNumberTagD* instr);
  void DoDeferredNumberTagI(LNumberTagI* instr);
  void DoDeferredTaggedToI(LTaggedToI* instr);
  void DoDeferredMathAbsTaggedHeapNumber(LUnaryMathOperation* instr);
  void DoDeferredStackCheck(LGoto* instr);
  void DoDeferredStringCharCodeAt(LStringCharCodeAt* instr);
  void DoDeferredLInstanceOfKnownGlobal(LInstanceOfKnownGlobal* instr,
                                        Label* map_check);

  // Parallel move support.
  void DoParallelMove(LParallelMove* move);

  // Emit frame translation commands for an environment.
  void WriteTranslation(LEnvironment* environment, Translation* translation);

  // Declare methods that deal with the individual node types.
#define DECLARE_DO(type) void Do##type(L##type* node);
  LITHIUM_CONCRETE_INSTRUCTION_LIST(DECLARE_DO)
#undef DECLARE_DO

 private:
  enum Status {
    UNUSED,
    GENERATING,
    DONE,
    ABORTED
  };

  bool is_unused() const { return status_ == UNUSED; }
  bool is_generating() const { return status_ == GENERATING; }
  bool is_done() const { return status_ == DONE; }
  bool is_aborted() const { return status_ == ABORTED; }

  LChunk* chunk() const { return chunk_; }
  Scope* scope() const { return scope_; }
  HGraph* graph() const { return chunk_->graph(); }
  MacroAssembler* masm() const { return masm_; }

  Register scratch0() { return r9; }
  DwVfpRegister double_scratch0() { return d0; }

  int GetNextEmittedBlock(int block);
  LInstruction* GetNextInstruction();

  void EmitClassOfTest(Label* if_true,
                       Label* if_false,
                       Handle<String> class_name,
                       Register input,
                       Register temporary,
                       Register temporary2);

  int StackSlotCount() const { return chunk()->spill_slot_count(); }
  int ParameterCount() const { return scope()->num_parameters(); }

  void Abort(const char* format, ...);
  void Comment(const char* format, ...);

  void AddDeferredCode(LDeferredCode* code) { deferred_.Add(code); }

  // Code generation passes.  Returns true if code generation should
  // continue.
  bool GeneratePrologue();
  bool GenerateBody();
  bool GenerateDeferredCode();
  bool GenerateSafepointTable();

  void CallCode(Handle<Code> code,
                RelocInfo::Mode mode,
                LInstruction* instr);
  void CallRuntime(Runtime::Function* function,
                   int num_arguments,
                   LInstruction* instr);
  void CallRuntime(Runtime::FunctionId id,
                   int num_arguments,
                   LInstruction* instr) {
    Runtime::Function* function = Runtime::FunctionForId(id);
    CallRuntime(function, num_arguments, instr);
  }

  // Generate a direct call to a known function.  Expects the function
  // to be in edi.
  void CallKnownFunction(Handle<JSFunction> function,
                         int arity,
                         LInstruction* instr);

  void LoadHeapObject(Register result, Handle<HeapObject> object);

  void RegisterLazyDeoptimization(LInstruction* instr);
  void RegisterEnvironmentForDeoptimization(LEnvironment* environment);
  void DeoptimizeIf(Condition cc, LEnvironment* environment);

  void AddToTranslation(Translation* translation,
                        LOperand* op,
                        bool is_tagged);
  void PopulateDeoptimizationData(Handle<Code> code);
  int DefineDeoptimizationLiteral(Handle<Object> literal);

  void PopulateDeoptimizationLiteralsWithInlinedFunctions();

  Register ToRegister(int index) const;
  DoubleRegister ToDoubleRegister(int index) const;

  // LOperand must be a register.
  Register ToRegister(LOperand* op) const;

  // LOperand is loaded into scratch, unless already a register.
  Register EmitLoadRegister(LOperand* op, Register scratch);

  // LOperand must be a double register.
  DoubleRegister ToDoubleRegister(LOperand* op) const;

  // LOperand is loaded into dbl_scratch, unless already a double register.
  DoubleRegister EmitLoadDoubleRegister(LOperand* op,
                                        SwVfpRegister flt_scratch,
                                        DoubleRegister dbl_scratch);

  int ToInteger32(LConstantOperand* op) const;
  Operand ToOperand(LOperand* op);
  MemOperand ToMemOperand(LOperand* op) const;

  // Specific math operations - used from DoUnaryMathOperation.
  void EmitIntegerMathAbs(LUnaryMathOperation* instr);
  void DoMathAbs(LUnaryMathOperation* instr);
  void EmitVFPTruncate(VFPRoundingMode rounding_mode,
                       SwVfpRegister result,
                       DwVfpRegister double_input,
                       Register scratch1,
                       Register scratch2);
  void DoMathFloor(LUnaryMathOperation* instr);
  void DoMathSqrt(LUnaryMathOperation* instr);

  // Support for recording safepoint and position information.
  void RecordSafepoint(LPointerMap* pointers,
                       Safepoint::Kind kind,
                       int arguments,
                       int deoptimization_index);
  void RecordSafepoint(LPointerMap* pointers, int deoptimization_index);
  void RecordSafepointWithRegisters(LPointerMap* pointers,
                                    int arguments,
                                    int deoptimization_index);
  void RecordSafepointWithRegistersAndDoubles(LPointerMap* pointers,
                                              int arguments,
                                              int deoptimization_index);
  void RecordPosition(int position);

  static Condition TokenToCondition(Token::Value op, bool is_unsigned);
  void EmitGoto(int block, LDeferredCode* deferred_stack_check = NULL);
  void EmitBranch(int left_block, int right_block, Condition cc);
  void EmitCmpI(LOperand* left, LOperand* right);
  void EmitNumberUntagD(Register input,
                        DoubleRegister result,
                        LEnvironment* env);

  // Emits optimized code for typeof x == "y".  Modifies input register.
  // Returns the condition on which a final split to
  // true and false label should be made, to optimize fallthrough.
  Condition EmitTypeofIs(Label* true_label, Label* false_label,
                         Register input, Handle<String> type_name);

  // Emits optimized code for %_IsObject(x).  Preserves input register.
  // Returns the condition on which a final split to
  // true and false label should be made, to optimize fallthrough.
  Condition EmitIsObject(Register input,
                         Register temp1,
                         Register temp2,
                         Label* is_not_object,
                         Label* is_object);

  LChunk* const chunk_;
  MacroAssembler* const masm_;
  CompilationInfo* const info_;

  int current_block_;
  int current_instruction_;
  const ZoneList<LInstruction*>* instructions_;
  ZoneList<LEnvironment*> deoptimizations_;
  ZoneList<Handle<Object> > deoptimization_literals_;
  int inlined_function_count_;
  Scope* const scope_;
  Status status_;
  TranslationBuffer translations_;
  ZoneList<LDeferredCode*> deferred_;
  int osr_pc_offset_;

  // Builder that keeps track of safepoints in the code. The table
  // itself is emitted at the end of the generated code.
  SafepointTableBuilder safepoints_;

  // Compiler from a set of parallel moves to a sequential list of moves.
  LGapResolver resolver_;

  friend class LDeferredCode;
  friend class LEnvironment;
  friend class SafepointGenerator;
  DISALLOW_COPY_AND_ASSIGN(LCodeGen);
};


class LDeferredCode: public ZoneObject {
 public:
  explicit LDeferredCode(LCodeGen* codegen)
      : codegen_(codegen), external_exit_(NULL) {
    codegen->AddDeferredCode(this);
  }

  virtual ~LDeferredCode() { }
  virtual void Generate() = 0;

  void SetExit(Label *exit) { external_exit_ = exit; }
  Label* entry() { return &entry_; }
  Label* exit() { return external_exit_ != NULL ? external_exit_ : &exit_; }

 protected:
  LCodeGen* codegen() const { return codegen_; }
  MacroAssembler* masm() const { return codegen_->masm(); }

 private:
  LCodeGen* codegen_;
  Label entry_;
  Label exit_;
  Label* external_exit_;
};

} }  // namespace v8::internal

#endif  // V8_ARM_LITHIUM_CODEGEN_ARM_H_
