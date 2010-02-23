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

#ifndef V8_ARM_CODEGEN_ARM_H_
#define V8_ARM_CODEGEN_ARM_H_

namespace v8 {
namespace internal {

// Forward declarations
class CompilationInfo;
class DeferredCode;
class RegisterAllocator;
class RegisterFile;

enum InitState { CONST_INIT, NOT_CONST_INIT };
enum TypeofState { INSIDE_TYPEOF, NOT_INSIDE_TYPEOF };


// -------------------------------------------------------------------------
// Reference support

// A reference is a C++ stack-allocated object that puts a
// reference on the virtual frame.  The reference may be consumed
// by GetValue, TakeValue, SetValue, and Codegen::UnloadReference.
// When the lifetime (scope) of a valid reference ends, it must have
// been consumed, and be in state UNLOADED.
class Reference BASE_EMBEDDED {
 public:
  // The values of the types is important, see size().
  enum Type { UNLOADED = -2, ILLEGAL = -1, SLOT = 0, NAMED = 1, KEYED = 2 };
  Reference(CodeGenerator* cgen,
            Expression* expression,
            bool persist_after_get = false);
  ~Reference();

  Expression* expression() const { return expression_; }
  Type type() const { return type_; }
  void set_type(Type value) {
    ASSERT_EQ(ILLEGAL, type_);
    type_ = value;
  }

  void set_unloaded() {
    ASSERT_NE(ILLEGAL, type_);
    ASSERT_NE(UNLOADED, type_);
    type_ = UNLOADED;
  }
  // The size the reference takes up on the stack.
  int size() const {
    return (type_ < SLOT) ? 0 : type_;
  }

  bool is_illegal() const { return type_ == ILLEGAL; }
  bool is_slot() const { return type_ == SLOT; }
  bool is_property() const { return type_ == NAMED || type_ == KEYED; }
  bool is_unloaded() const { return type_ == UNLOADED; }

  // Return the name.  Only valid for named property references.
  Handle<String> GetName();

  // Generate code to push the value of the reference on top of the
  // expression stack.  The reference is expected to be already on top of
  // the expression stack, and it is consumed by the call unless the
  // reference is for a compound assignment.
  // If the reference is not consumed, it is left in place under its value.
  void GetValue();

  // Generate code to pop a reference, push the value of the reference,
  // and then spill the stack frame.
  inline void GetValueAndSpill();

  // Generate code to store the value on top of the expression stack in the
  // reference.  The reference is expected to be immediately below the value
  // on the expression stack.  The  value is stored in the location specified
  // by the reference, and is left on top of the stack, after the reference
  // is popped from beneath it (unloaded).
  void SetValue(InitState init_state);

 private:
  CodeGenerator* cgen_;
  Expression* expression_;
  Type type_;
  // Keep the reference on the stack after get, so it can be used by set later.
  bool persist_after_get_;
};


// -------------------------------------------------------------------------
// Code generation state

// The state is passed down the AST by the code generator (and back up, in
// the form of the state of the label pair).  It is threaded through the
// call stack.  Constructing a state implicitly pushes it on the owning code
// generator's stack of states, and destroying one implicitly pops it.

class CodeGenState BASE_EMBEDDED {
 public:
  // Create an initial code generator state.  Destroying the initial state
  // leaves the code generator with a NULL state.
  explicit CodeGenState(CodeGenerator* owner);

  // Create a code generator state based on a code generator's current
  // state.  The new state has its own pair of branch labels.
  CodeGenState(CodeGenerator* owner,
               JumpTarget* true_target,
               JumpTarget* false_target);

  // Destroy a code generator state and restore the owning code generator's
  // previous state.
  ~CodeGenState();

  JumpTarget* true_target() const { return true_target_; }
  JumpTarget* false_target() const { return false_target_; }

 private:
  CodeGenerator* owner_;
  JumpTarget* true_target_;
  JumpTarget* false_target_;
  CodeGenState* previous_;
};


// -------------------------------------------------------------------------
// CodeGenerator

class CodeGenerator: public AstVisitor {
 public:
  // Takes a function literal, generates code for it. This function should only
  // be called by compiler.cc.
  static Handle<Code> MakeCode(CompilationInfo* info);

  // Printing of AST, etc. as requested by flags.
  static void MakeCodePrologue(CompilationInfo* info);

  // Allocate and install the code.
  static Handle<Code> MakeCodeEpilogue(MacroAssembler* masm,
                                       Code::Flags flags,
                                       CompilationInfo* info);

#ifdef ENABLE_LOGGING_AND_PROFILING
  static bool ShouldGenerateLog(Expression* type);
#endif

  static void SetFunctionInfo(Handle<JSFunction> fun,
                              FunctionLiteral* lit,
                              bool is_toplevel,
                              Handle<Script> script);

  static void RecordPositions(MacroAssembler* masm, int pos);

  // Accessors
  MacroAssembler* masm() { return masm_; }
  VirtualFrame* frame() const { return frame_; }
  inline Handle<Script> script();

  bool has_valid_frame() const { return frame_ != NULL; }

  // Set the virtual frame to be new_frame, with non-frame register
  // reference counts given by non_frame_registers.  The non-frame
  // register reference counts of the old frame are returned in
  // non_frame_registers.
  void SetFrame(VirtualFrame* new_frame, RegisterFile* non_frame_registers);

  void DeleteFrame();

  RegisterAllocator* allocator() const { return allocator_; }

  CodeGenState* state() { return state_; }
  void set_state(CodeGenState* state) { state_ = state; }

  void AddDeferred(DeferredCode* code) { deferred_.Add(code); }

  static const int kUnknownIntValue = -1;

 private:
  // Construction/Destruction
  explicit CodeGenerator(MacroAssembler* masm);

  // Accessors
  inline bool is_eval();
  Scope* scope();

  // Generating deferred code.
  void ProcessDeferred();

  // State
  bool has_cc() const  { return cc_reg_ != al; }
  JumpTarget* true_target() const  { return state_->true_target(); }
  JumpTarget* false_target() const  { return state_->false_target(); }

  // We don't track loop nesting level on ARM yet.
  int loop_nesting() const { return 0; }

  // Node visitors.
  void VisitStatements(ZoneList<Statement*>* statements);

#define DEF_VISIT(type) \
  void Visit##type(type* node);
  AST_NODE_LIST(DEF_VISIT)
#undef DEF_VISIT

  // Visit a statement and then spill the virtual frame if control flow can
  // reach the end of the statement (ie, it does not exit via break,
  // continue, return, or throw).  This function is used temporarily while
  // the code generator is being transformed.
  inline void VisitAndSpill(Statement* statement);

  // Visit a list of statements and then spill the virtual frame if control
  // flow can reach the end of the list.
  inline void VisitStatementsAndSpill(ZoneList<Statement*>* statements);

  // Main code generation function
  void Generate(CompilationInfo* info);

  // The following are used by class Reference.
  void LoadReference(Reference* ref);
  void UnloadReference(Reference* ref);

  static MemOperand ContextOperand(Register context, int index) {
    return MemOperand(context, Context::SlotOffset(index));
  }

  MemOperand SlotOperand(Slot* slot, Register tmp);

  MemOperand ContextSlotOperandCheckExtensions(Slot* slot,
                                               Register tmp,
                                               Register tmp2,
                                               JumpTarget* slow);

  // Expressions
  static MemOperand GlobalObject()  {
    return ContextOperand(cp, Context::GLOBAL_INDEX);
  }

  void LoadCondition(Expression* x,
                     JumpTarget* true_target,
                     JumpTarget* false_target,
                     bool force_cc);
  void Load(Expression* expr);
  void LoadGlobal();
  void LoadGlobalReceiver(Register scratch);

  // Generate code to push the value of an expression on top of the frame
  // and then spill the frame fully to memory.  This function is used
  // temporarily while the code generator is being transformed.
  inline void LoadAndSpill(Expression* expression);

  // Call LoadCondition and then spill the virtual frame unless control flow
  // cannot reach the end of the expression (ie, by emitting only
  // unconditional jumps to the control targets).
  inline void LoadConditionAndSpill(Expression* expression,
                                    JumpTarget* true_target,
                                    JumpTarget* false_target,
                                    bool force_control);

  // Read a value from a slot and leave it on top of the expression stack.
  void LoadFromSlot(Slot* slot, TypeofState typeof_state);
  // Store the value on top of the stack to a slot.
  void StoreToSlot(Slot* slot, InitState init_state);
  // Load a keyed property, leaving it in r0.  The receiver and key are
  // passed on the stack, and remain there.
  void EmitKeyedLoad(bool is_global);

  void LoadFromGlobalSlotCheckExtensions(Slot* slot,
                                         TypeofState typeof_state,
                                         Register tmp,
                                         Register tmp2,
                                         JumpTarget* slow);

  // Special code for typeof expressions: Unfortunately, we must
  // be careful when loading the expression in 'typeof'
  // expressions. We are not allowed to throw reference errors for
  // non-existing properties of the global object, so we must make it
  // look like an explicit property access, instead of an access
  // through the context chain.
  void LoadTypeofExpression(Expression* x);

  void ToBoolean(JumpTarget* true_target, JumpTarget* false_target);

  void GenericBinaryOperation(Token::Value op,
                              OverwriteMode overwrite_mode,
                              int known_rhs = kUnknownIntValue);
  void Comparison(Condition cc,
                  Expression* left,
                  Expression* right,
                  bool strict = false);

  void SmiOperation(Token::Value op,
                    Handle<Object> value,
                    bool reversed,
                    OverwriteMode mode);

  void CallWithArguments(ZoneList<Expression*>* arguments,
                         CallFunctionFlags flags,
                         int position);

  // Control flow
  void Branch(bool if_true, JumpTarget* target);
  void CheckStack();

  struct InlineRuntimeLUT {
    void (CodeGenerator::*method)(ZoneList<Expression*>*);
    const char* name;
  };

  static InlineRuntimeLUT* FindInlineRuntimeLUT(Handle<String> name);
  bool CheckForInlineRuntimeCall(CallRuntime* node);
  static bool PatchInlineRuntimeEntry(Handle<String> name,
                                      const InlineRuntimeLUT& new_entry,
                                      InlineRuntimeLUT* old_entry);

  static Handle<Code> ComputeLazyCompile(int argc);
  void ProcessDeclarations(ZoneList<Declaration*>* declarations);

  static Handle<Code> ComputeCallInitialize(int argc, InLoopFlag in_loop);

  // Declare global variables and functions in the given array of
  // name/value pairs.
  void DeclareGlobals(Handle<FixedArray> pairs);

  // Instantiate the function boilerplate.
  void InstantiateBoilerplate(Handle<JSFunction> boilerplate);

  // Support for type checks.
  void GenerateIsSmi(ZoneList<Expression*>* args);
  void GenerateIsNonNegativeSmi(ZoneList<Expression*>* args);
  void GenerateIsArray(ZoneList<Expression*>* args);
  void GenerateIsRegExp(ZoneList<Expression*>* args);
  void GenerateIsObject(ZoneList<Expression*>* args);
  void GenerateIsFunction(ZoneList<Expression*>* args);
  void GenerateIsUndetectableObject(ZoneList<Expression*>* args);

  // Support for construct call checks.
  void GenerateIsConstructCall(ZoneList<Expression*>* args);

  // Support for arguments.length and arguments[?].
  void GenerateArgumentsLength(ZoneList<Expression*>* args);
  void GenerateArgumentsAccess(ZoneList<Expression*>* args);

  // Support for accessing the class and value fields of an object.
  void GenerateClassOf(ZoneList<Expression*>* args);
  void GenerateValueOf(ZoneList<Expression*>* args);
  void GenerateSetValueOf(ZoneList<Expression*>* args);

  // Fast support for charCodeAt(n).
  void GenerateFastCharCodeAt(ZoneList<Expression*>* args);

  // Fast support for object equality testing.
  void GenerateObjectEquals(ZoneList<Expression*>* args);

  void GenerateLog(ZoneList<Expression*>* args);

  // Fast support for Math.random().
  void GenerateRandomPositiveSmi(ZoneList<Expression*>* args);

  // Fast support for StringAdd.
  void GenerateStringAdd(ZoneList<Expression*>* args);

  // Fast support for SubString.
  void GenerateSubString(ZoneList<Expression*>* args);

  // Fast support for StringCompare.
  void GenerateStringCompare(ZoneList<Expression*>* args);

  // Support for direct calls from JavaScript to native RegExp code.
  void GenerateRegExpExec(ZoneList<Expression*>* args);

  // Fast support for number to string.
  void GenerateNumberToString(ZoneList<Expression*>* args);

  // Simple condition analysis.
  enum ConditionAnalysis {
    ALWAYS_TRUE,
    ALWAYS_FALSE,
    DONT_KNOW
  };
  ConditionAnalysis AnalyzeCondition(Expression* cond);

  // Methods used to indicate which source code is generated for. Source
  // positions are collected by the assembler and emitted with the relocation
  // information.
  void CodeForFunctionPosition(FunctionLiteral* fun);
  void CodeForReturnPosition(FunctionLiteral* fun);
  void CodeForStatementPosition(Statement* node);
  void CodeForDoWhileConditionPosition(DoWhileStatement* stmt);
  void CodeForSourcePosition(int pos);

#ifdef DEBUG
  // True if the registers are valid for entry to a block.
  bool HasValidEntryRegisters();
#endif

  List<DeferredCode*> deferred_;

  // Assembler
  MacroAssembler* masm_;  // to generate code

  CompilationInfo* info_;

  // Code generation state
  VirtualFrame* frame_;
  RegisterAllocator* allocator_;
  Condition cc_reg_;
  CodeGenState* state_;

  // Jump targets
  BreakTarget function_return_;

  // True if the function return is shadowed (ie, jumping to the target
  // function_return_ does not jump to the true function return, but rather
  // to some unlinking code).
  bool function_return_is_shadowed_;

  static InlineRuntimeLUT kInlineRuntimeLUT[];

  friend class VirtualFrame;
  friend class JumpTarget;
  friend class Reference;
  friend class FastCodeGenerator;
  friend class FullCodeGenerator;
  friend class FullCodeGenSyntaxChecker;

  DISALLOW_COPY_AND_ASSIGN(CodeGenerator);
};


class GenericBinaryOpStub : public CodeStub {
 public:
  GenericBinaryOpStub(Token::Value op,
                      OverwriteMode mode,
                      int constant_rhs = CodeGenerator::kUnknownIntValue)
      : op_(op),
        mode_(mode),
        constant_rhs_(constant_rhs),
        specialized_on_rhs_(RhsIsOneWeWantToOptimizeFor(op, constant_rhs)),
        name_(NULL) { }

 private:
  Token::Value op_;
  OverwriteMode mode_;
  int constant_rhs_;
  bool specialized_on_rhs_;
  char* name_;

  static const int kMaxKnownRhs = 0x40000000;

  // Minor key encoding in 16 bits.
  class ModeBits: public BitField<OverwriteMode, 0, 2> {};
  class OpBits: public BitField<Token::Value, 2, 6> {};
  class KnownIntBits: public BitField<int, 8, 8> {};

  Major MajorKey() { return GenericBinaryOp; }
  int MinorKey() {
    // Encode the parameters in a unique 16 bit value.
    return OpBits::encode(op_)
           | ModeBits::encode(mode_)
           | KnownIntBits::encode(MinorKeyForKnownInt());
  }

  void Generate(MacroAssembler* masm);
  void HandleNonSmiBitwiseOp(MacroAssembler* masm);

  static bool RhsIsOneWeWantToOptimizeFor(Token::Value op, int constant_rhs) {
    if (constant_rhs == CodeGenerator::kUnknownIntValue) return false;
    if (op == Token::DIV) return constant_rhs >= 2 && constant_rhs <= 3;
    if (op == Token::MOD) {
      if (constant_rhs <= 1) return false;
      if (constant_rhs <= 10) return true;
      if (constant_rhs <= kMaxKnownRhs && IsPowerOf2(constant_rhs)) return true;
      return false;
    }
    return false;
  }

  int MinorKeyForKnownInt() {
    if (!specialized_on_rhs_) return 0;
    if (constant_rhs_ <= 10) return constant_rhs_ + 1;
    ASSERT(IsPowerOf2(constant_rhs_));
    int key = 12;
    int d = constant_rhs_;
    while ((d & 1) == 0) {
      key++;
      d >>= 1;
    }
    return key;
  }

  const char* GetName();

#ifdef DEBUG
  void Print() {
    if (!specialized_on_rhs_) {
      PrintF("GenericBinaryOpStub (%s)\n", Token::String(op_));
    } else {
      PrintF("GenericBinaryOpStub (%s by %d)\n",
             Token::String(op_),
             constant_rhs_);
    }
  }
#endif
};


class StringStubBase: public CodeStub {
 public:
  // Generate code for copying characters using a simple loop. This should only
  // be used in places where the number of characters is small and the
  // additional setup and checking in GenerateCopyCharactersLong adds too much
  // overhead. Copying of overlapping regions is not supported.
  // Dest register ends at the position after the last character written.
  void GenerateCopyCharacters(MacroAssembler* masm,
                              Register dest,
                              Register src,
                              Register count,
                              Register scratch,
                              bool ascii);

  // Generate code for copying a large number of characters. This function
  // is allowed to spend extra time setting up conditions to make copying
  // faster. Copying of overlapping regions is not supported.
  // Dest register ends at the position after the last character written.
  void GenerateCopyCharactersLong(MacroAssembler* masm,
                                  Register dest,
                                  Register src,
                                  Register count,
                                  Register scratch1,
                                  Register scratch2,
                                  Register scratch3,
                                  Register scratch4,
                                  Register scratch5,
                                  int flags);
};


// Flag that indicates how to generate code for the stub StringAddStub.
enum StringAddFlags {
  NO_STRING_ADD_FLAGS = 0,
  NO_STRING_CHECK_IN_STUB = 1 << 0  // Omit string check in stub.
};


class StringAddStub: public StringStubBase {
 public:
  explicit StringAddStub(StringAddFlags flags) {
    string_check_ = ((flags & NO_STRING_CHECK_IN_STUB) == 0);
  }

 private:
  Major MajorKey() { return StringAdd; }
  int MinorKey() { return string_check_ ? 0 : 1; }

  void Generate(MacroAssembler* masm);

  // Should the stub check whether arguments are strings?
  bool string_check_;
};


class SubStringStub: public StringStubBase {
 public:
  SubStringStub() {}

 private:
  Major MajorKey() { return SubString; }
  int MinorKey() { return 0; }

  void Generate(MacroAssembler* masm);
};



class StringCompareStub: public CodeStub {
 public:
  StringCompareStub() { }

  // Compare two flat ASCII strings and returns result in r0.
  // Does not use the stack.
  static void GenerateCompareFlatAsciiStrings(MacroAssembler* masm,
                                              Register left,
                                              Register right,
                                              Register scratch1,
                                              Register scratch2,
                                              Register scratch3,
                                              Register scratch4);

 private:
  Major MajorKey() { return StringCompare; }
  int MinorKey() { return 0; }

  void Generate(MacroAssembler* masm);
};


} }  // namespace v8::internal

#endif  // V8_ARM_CODEGEN_ARM_H_
