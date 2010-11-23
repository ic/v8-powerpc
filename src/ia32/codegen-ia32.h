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

#ifndef V8_IA32_CODEGEN_IA32_H_
#define V8_IA32_CODEGEN_IA32_H_

#include "ast.h"
#include "ic-inl.h"
#include "jump-target-heavy.h"

namespace v8 {
namespace internal {

// Forward declarations
class CompilationInfo;
class DeferredCode;
class FrameRegisterState;
class RegisterAllocator;
class RegisterFile;
class RuntimeCallHelper;

enum InitState { CONST_INIT, NOT_CONST_INIT };
enum TypeofState { INSIDE_TYPEOF, NOT_INSIDE_TYPEOF };


// -------------------------------------------------------------------------
// Reference support

// A reference is a C++ stack-allocated object that puts a
// reference on the virtual frame.  The reference may be consumed
// by GetValue, TakeValue and SetValue.
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

  // Like GetValue except that the slot is expected to be written to before
  // being read from again.  The value of the reference may be invalidated,
  // causing subsequent attempts to read it to fail.
  void TakeValue();

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
// Control destinations.

// A control destination encapsulates a pair of jump targets and a
// flag indicating which one is the preferred fall-through.  The
// preferred fall-through must be unbound, the other may be already
// bound (ie, a backward target).
//
// The true and false targets may be jumped to unconditionally or
// control may split conditionally.  Unconditional jumping and
// splitting should be emitted in tail position (as the last thing
// when compiling an expression) because they can cause either label
// to be bound or the non-fall through to be jumped to leaving an
// invalid virtual frame.
//
// The labels in the control destination can be extracted and
// manipulated normally without affecting the state of the
// destination.

class ControlDestination BASE_EMBEDDED {
 public:
  ControlDestination(JumpTarget* true_target,
                     JumpTarget* false_target,
                     bool true_is_fall_through)
      : true_target_(true_target),
        false_target_(false_target),
        true_is_fall_through_(true_is_fall_through),
        is_used_(false) {
    ASSERT(true_is_fall_through ? !true_target->is_bound()
                                : !false_target->is_bound());
  }

  // Accessors for the jump targets.  Directly jumping or branching to
  // or binding the targets will not update the destination's state.
  JumpTarget* true_target() const { return true_target_; }
  JumpTarget* false_target() const { return false_target_; }

  // True if the the destination has been jumped to unconditionally or
  // control has been split to both targets.  This predicate does not
  // test whether the targets have been extracted and manipulated as
  // raw jump targets.
  bool is_used() const { return is_used_; }

  // True if the destination is used and the true target (respectively
  // false target) was the fall through.  If the target is backward,
  // "fall through" included jumping unconditionally to it.
  bool true_was_fall_through() const {
    return is_used_ && true_is_fall_through_;
  }

  bool false_was_fall_through() const {
    return is_used_ && !true_is_fall_through_;
  }

  // Emit a branch to one of the true or false targets, and bind the
  // other target.  Because this binds the fall-through target, it
  // should be emitted in tail position (as the last thing when
  // compiling an expression).
  void Split(Condition cc) {
    ASSERT(!is_used_);
    if (true_is_fall_through_) {
      false_target_->Branch(NegateCondition(cc));
      true_target_->Bind();
    } else {
      true_target_->Branch(cc);
      false_target_->Bind();
    }
    is_used_ = true;
  }

  // Emit an unconditional jump in tail position, to the true target
  // (if the argument is true) or the false target.  The "jump" will
  // actually bind the jump target if it is forward, jump to it if it
  // is backward.
  void Goto(bool where) {
    ASSERT(!is_used_);
    JumpTarget* target = where ? true_target_ : false_target_;
    if (target->is_bound()) {
      target->Jump();
    } else {
      target->Bind();
    }
    is_used_ = true;
    true_is_fall_through_ = where;
  }

  // Mark this jump target as used as if Goto had been called, but
  // without generating a jump or binding a label (the control effect
  // should have already happened).  This is used when the left
  // subexpression of the short-circuit boolean operators are
  // compiled.
  void Use(bool where) {
    ASSERT(!is_used_);
    ASSERT((where ? true_target_ : false_target_)->is_bound());
    is_used_ = true;
    true_is_fall_through_ = where;
  }

  // Swap the true and false targets but keep the same actual label as
  // the fall through.  This is used when compiling negated
  // expressions, where we want to swap the targets but preserve the
  // state.
  void Invert() {
    JumpTarget* temp_target = true_target_;
    true_target_ = false_target_;
    false_target_ = temp_target;

    true_is_fall_through_ = !true_is_fall_through_;
  }

 private:
  // True and false jump targets.
  JumpTarget* true_target_;
  JumpTarget* false_target_;

  // Before using the destination: true if the true target is the
  // preferred fall through, false if the false target is.  After
  // using the destination: true if the true target was actually used
  // as the fall through, false if the false target was.
  bool true_is_fall_through_;

  // True if the Split or Goto functions have been called.
  bool is_used_;
};


// -------------------------------------------------------------------------
// Code generation state

// The state is passed down the AST by the code generator (and back up, in
// the form of the state of the jump target pair).  It is threaded through
// the call stack.  Constructing a state implicitly pushes it on the owning
// code generator's stack of states, and destroying one implicitly pops it.
//
// The code generator state is only used for expressions, so statements have
// the initial state.

class CodeGenState BASE_EMBEDDED {
 public:
  // Create an initial code generator state.  Destroying the initial state
  // leaves the code generator with a NULL state.
  explicit CodeGenState(CodeGenerator* owner);

  // Create a code generator state based on a code generator's current
  // state.  The new state has its own control destination.
  CodeGenState(CodeGenerator* owner, ControlDestination* destination);

  // Destroy a code generator state and restore the owning code generator's
  // previous state.
  ~CodeGenState();

  // Accessors for the state.
  ControlDestination* destination() const { return destination_; }

 private:
  // The owning code generator.
  CodeGenerator* owner_;

  // A control destination in case the expression has a control-flow
  // effect.
  ControlDestination* destination_;

  // The previous state of the owning code generator, restored when
  // this state is destroyed.
  CodeGenState* previous_;
};


// -------------------------------------------------------------------------
// Arguments allocation mode.

enum ArgumentsAllocationMode {
  NO_ARGUMENTS_ALLOCATION,
  EAGER_ARGUMENTS_ALLOCATION,
  LAZY_ARGUMENTS_ALLOCATION
};


// -------------------------------------------------------------------------
// CodeGenerator

class CodeGenerator: public AstVisitor {
 public:
  static bool MakeCode(CompilationInfo* info);

  // Printing of AST, etc. as requested by flags.
  static void MakeCodePrologue(CompilationInfo* info);

  // Allocate and install the code.
  static Handle<Code> MakeCodeEpilogue(MacroAssembler* masm,
                                       Code::Flags flags,
                                       CompilationInfo* info);

#ifdef ENABLE_LOGGING_AND_PROFILING
  static bool ShouldGenerateLog(Expression* type);
#endif

  static bool RecordPositions(MacroAssembler* masm,
                              int pos,
                              bool right_here = false);

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

  bool in_spilled_code() const { return in_spilled_code_; }
  void set_in_spilled_code(bool flag) { in_spilled_code_ = flag; }

  // Return a position of the element at |index_as_smi| + |additional_offset|
  // in FixedArray pointer to which is held in |array|.  |index_as_smi| is Smi.
  static Operand FixedArrayElementOperand(Register array,
                                          Register index_as_smi,
                                          int additional_offset = 0) {
    int offset = FixedArray::kHeaderSize + additional_offset * kPointerSize;
    return FieldOperand(array, index_as_smi, times_half_pointer_size, offset);
  }

 private:
  // Type of a member function that generates inline code for a native function.
  typedef void (CodeGenerator::*InlineFunctionGenerator)
      (ZoneList<Expression*>*);

  static const InlineFunctionGenerator kInlineFunctionGenerators[];

  // Construction/Destruction
  explicit CodeGenerator(MacroAssembler* masm);

  // Accessors
  inline bool is_eval();
  inline Scope* scope();

  // Generating deferred code.
  void ProcessDeferred();

  // State
  ControlDestination* destination() const { return state_->destination(); }

  // Control of side-effect-free int32 expression compilation.
  bool in_safe_int32_mode() { return in_safe_int32_mode_; }
  void set_in_safe_int32_mode(bool value) { in_safe_int32_mode_ = value; }
  bool safe_int32_mode_enabled() {
    return FLAG_safe_int32_compiler && safe_int32_mode_enabled_;
  }
  void set_safe_int32_mode_enabled(bool value) {
    safe_int32_mode_enabled_ = value;
  }
  void set_unsafe_bailout(BreakTarget* unsafe_bailout) {
    unsafe_bailout_ = unsafe_bailout;
  }

  // Take the Result that is an untagged int32, and convert it to a tagged
  // Smi or HeapNumber.  Remove the untagged_int32 flag from the result.
  void ConvertInt32ResultToNumber(Result* value);
  void ConvertInt32ResultToSmi(Result* value);

  // Track loop nesting level.
  int loop_nesting() const { return loop_nesting_; }
  void IncrementLoopNesting() { loop_nesting_++; }
  void DecrementLoopNesting() { loop_nesting_--; }

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
  void VisitAndSpill(Statement* statement);

  // Visit a list of statements and then spill the virtual frame if control
  // flow can reach the end of the list.
  void VisitStatementsAndSpill(ZoneList<Statement*>* statements);

  // Main code generation function
  void Generate(CompilationInfo* info);

  // Generate the return sequence code.  Should be called no more than
  // once per compiled function, immediately after binding the return
  // target (which can not be done more than once).
  void GenerateReturnSequence(Result* return_value);

  // Returns the arguments allocation mode.
  ArgumentsAllocationMode ArgumentsMode();

  // Store the arguments object and allocate it if necessary.
  Result StoreArgumentsObject(bool initial);

  // The following are used by class Reference.
  void LoadReference(Reference* ref);

  Operand SlotOperand(Slot* slot, Register tmp);

  Operand ContextSlotOperandCheckExtensions(Slot* slot,
                                            Result tmp,
                                            JumpTarget* slow);

  // Expressions
  void LoadCondition(Expression* expr,
                     ControlDestination* destination,
                     bool force_control);
  void Load(Expression* expr);
  void LoadGlobal();
  void LoadGlobalReceiver();

  // Generate code to push the value of an expression on top of the frame
  // and then spill the frame fully to memory.  This function is used
  // temporarily while the code generator is being transformed.
  void LoadAndSpill(Expression* expression);

  // Evaluate an expression and place its value on top of the frame,
  // using, or not using, the side-effect-free expression compiler.
  void LoadInSafeInt32Mode(Expression* expr, BreakTarget* unsafe_bailout);
  void LoadWithSafeInt32ModeDisabled(Expression* expr);

  // Read a value from a slot and leave it on top of the expression stack.
  void LoadFromSlot(Slot* slot, TypeofState typeof_state);
  void LoadFromSlotCheckForArguments(Slot* slot, TypeofState typeof_state);
  Result LoadFromGlobalSlotCheckExtensions(Slot* slot,
                                           TypeofState typeof_state,
                                           JumpTarget* slow);

  // Support for loading from local/global variables and arguments
  // whose location is known unless they are shadowed by
  // eval-introduced bindings. Generates no code for unsupported slot
  // types and therefore expects to fall through to the slow jump target.
  void EmitDynamicLoadFromSlotFastCase(Slot* slot,
                                       TypeofState typeof_state,
                                       Result* result,
                                       JumpTarget* slow,
                                       JumpTarget* done);

  // Store the value on top of the expression stack into a slot, leaving the
  // value in place.
  void StoreToSlot(Slot* slot, InitState init_state);

  // Support for compiling assignment expressions.
  void EmitSlotAssignment(Assignment* node);
  void EmitNamedPropertyAssignment(Assignment* node);
  void EmitKeyedPropertyAssignment(Assignment* node);

  // Receiver is passed on the frame and consumed.
  Result EmitNamedLoad(Handle<String> name, bool is_contextual);

  // If the store is contextual, value is passed on the frame and consumed.
  // Otherwise, receiver and value are passed on the frame and consumed.
  Result EmitNamedStore(Handle<String> name, bool is_contextual);

  // Receiver and key are passed on the frame and consumed.
  Result EmitKeyedLoad();

  // Receiver, key, and value are passed on the frame and consumed.
  Result EmitKeyedStore(StaticType* key_type);

  // Special code for typeof expressions: Unfortunately, we must
  // be careful when loading the expression in 'typeof'
  // expressions. We are not allowed to throw reference errors for
  // non-existing properties of the global object, so we must make it
  // look like an explicit property access, instead of an access
  // through the context chain.
  void LoadTypeofExpression(Expression* x);

  // Translate the value on top of the frame into control flow to the
  // control destination.
  void ToBoolean(ControlDestination* destination);

  // Generate code that computes a shortcutting logical operation.
  void GenerateLogicalBooleanOperation(BinaryOperation* node);

  void GenericBinaryOperation(BinaryOperation* expr,
                              OverwriteMode overwrite_mode);

  // Emits code sequence that jumps to a JumpTarget if the inputs
  // are both smis.  Cannot be in MacroAssembler because it takes
  // advantage of TypeInfo to skip unneeded checks.
  // Allocates a temporary register, possibly spilling from the frame,
  // if it needs to check both left and right.
  void JumpIfBothSmiUsingTypeInfo(Result* left,
                                  Result* right,
                                  JumpTarget* both_smi);

  // Emits code sequence that jumps to deferred code if the inputs
  // are not both smis.  Cannot be in MacroAssembler because it takes
  // a deferred code object.
  void JumpIfNotBothSmiUsingTypeInfo(Register left,
                                     Register right,
                                     Register scratch,
                                     TypeInfo left_info,
                                     TypeInfo right_info,
                                     DeferredCode* deferred);

  // Emits code sequence that jumps to the label if the inputs
  // are not both smis.
  void JumpIfNotBothSmiUsingTypeInfo(Register left,
                                     Register right,
                                     Register scratch,
                                     TypeInfo left_info,
                                     TypeInfo right_info,
                                     Label* on_non_smi);

  // If possible, combine two constant smi values using op to produce
  // a smi result, and push it on the virtual frame, all at compile time.
  // Returns true if it succeeds.  Otherwise it has no effect.
  bool FoldConstantSmis(Token::Value op, int left, int right);

  // Emit code to perform a binary operation on a constant
  // smi and a likely smi.  Consumes the Result operand.
  Result ConstantSmiBinaryOperation(BinaryOperation* expr,
                                    Result* operand,
                                    Handle<Object> constant_operand,
                                    bool reversed,
                                    OverwriteMode overwrite_mode);

  // Emit code to perform a binary operation on two likely smis.
  // The code to handle smi arguments is produced inline.
  // Consumes the Results left and right.
  Result LikelySmiBinaryOperation(BinaryOperation* expr,
                                  Result* left,
                                  Result* right,
                                  OverwriteMode overwrite_mode);


  // Emit code to perform a binary operation on two untagged int32 values.
  // The values are on top of the frame, and the result is pushed on the frame.
  void Int32BinaryOperation(BinaryOperation* node);


  // Generate a stub call from the virtual frame.
  Result GenerateGenericBinaryOpStubCall(GenericBinaryOpStub* stub,
                                         Result* left,
                                         Result* right);

  void Comparison(AstNode* node,
                  Condition cc,
                  bool strict,
                  ControlDestination* destination);

  // If at least one of the sides is a constant smi, generate optimized code.
  void ConstantSmiComparison(Condition cc,
                             bool strict,
                             ControlDestination* destination,
                             Result* left_side,
                             Result* right_side,
                             bool left_side_constant_smi,
                             bool right_side_constant_smi,
                             bool is_loop_condition);

  void GenerateInlineNumberComparison(Result* left_side,
                                      Result* right_side,
                                      Condition cc,
                                      ControlDestination* dest);

  // To prevent long attacker-controlled byte sequences, integer constants
  // from the JavaScript source are loaded in two parts if they are larger
  // than 17 bits.
  static const int kMaxSmiInlinedBits = 17;
  bool IsUnsafeSmi(Handle<Object> value);
  // Load an integer constant x into a register target or into the stack using
  // at most 16 bits of user-controlled data per assembly operation.
  void MoveUnsafeSmi(Register target, Handle<Object> value);
  void StoreUnsafeSmiToLocal(int offset, Handle<Object> value);
  void PushUnsafeSmi(Handle<Object> value);

  void CallWithArguments(ZoneList<Expression*>* arguments,
                         CallFunctionFlags flags,
                         int position);

  // An optimized implementation of expressions of the form
  // x.apply(y, arguments).  We call x the applicand and y the receiver.
  // The optimization avoids allocating an arguments object if possible.
  void CallApplyLazy(Expression* applicand,
                     Expression* receiver,
                     VariableProxy* arguments,
                     int position);

  void CheckStack();

  bool CheckForInlineRuntimeCall(CallRuntime* node);

  void ProcessDeclarations(ZoneList<Declaration*>* declarations);

  // Declare global variables and functions in the given array of
  // name/value pairs.
  void DeclareGlobals(Handle<FixedArray> pairs);

  // Instantiate the function based on the shared function info.
  Result InstantiateFunction(Handle<SharedFunctionInfo> function_info,
                             bool pretenure);

  // Support for types.
  void GenerateIsSmi(ZoneList<Expression*>* args);
  void GenerateIsNonNegativeSmi(ZoneList<Expression*>* args);
  void GenerateIsArray(ZoneList<Expression*>* args);
  void GenerateIsRegExp(ZoneList<Expression*>* args);
  void GenerateIsObject(ZoneList<Expression*>* args);
  void GenerateIsSpecObject(ZoneList<Expression*>* args);
  void GenerateIsFunction(ZoneList<Expression*>* args);
  void GenerateIsUndetectableObject(ZoneList<Expression*>* args);
  void GenerateIsStringWrapperSafeForDefaultValueOf(
      ZoneList<Expression*>* args);

  // Support for construct call checks.
  void GenerateIsConstructCall(ZoneList<Expression*>* args);

  // Support for arguments.length and arguments[?].
  void GenerateArgumentsLength(ZoneList<Expression*>* args);
  void GenerateArguments(ZoneList<Expression*>* args);

  // Support for accessing the class and value fields of an object.
  void GenerateClassOf(ZoneList<Expression*>* args);
  void GenerateValueOf(ZoneList<Expression*>* args);
  void GenerateSetValueOf(ZoneList<Expression*>* args);

  // Fast support for charCodeAt(n).
  void GenerateStringCharCodeAt(ZoneList<Expression*>* args);

  // Fast support for string.charAt(n) and string[n].
  void GenerateStringCharFromCode(ZoneList<Expression*>* args);

  // Fast support for string.charAt(n) and string[n].
  void GenerateStringCharAt(ZoneList<Expression*>* args);

  // Fast support for object equality testing.
  void GenerateObjectEquals(ZoneList<Expression*>* args);

  void GenerateLog(ZoneList<Expression*>* args);

  void GenerateGetFramePointer(ZoneList<Expression*>* args);

  // Fast support for Math.random().
  void GenerateRandomHeapNumber(ZoneList<Expression*>* args);

  // Fast support for StringAdd.
  void GenerateStringAdd(ZoneList<Expression*>* args);

  // Fast support for SubString.
  void GenerateSubString(ZoneList<Expression*>* args);

  // Fast support for StringCompare.
  void GenerateStringCompare(ZoneList<Expression*>* args);

  // Support for direct calls from JavaScript to native RegExp code.
  void GenerateRegExpExec(ZoneList<Expression*>* args);

  // Construct a RegExp exec result with two in-object properties.
  void GenerateRegExpConstructResult(ZoneList<Expression*>* args);

  // Support for fast native caches.
  void GenerateGetFromCache(ZoneList<Expression*>* args);

  // Fast support for number to string.
  void GenerateNumberToString(ZoneList<Expression*>* args);

  // Fast swapping of elements. Takes three expressions, the object and two
  // indices. This should only be used if the indices are known to be
  // non-negative and within bounds of the elements array at the call site.
  void GenerateSwapElements(ZoneList<Expression*>* args);

  // Fast call for custom callbacks.
  void GenerateCallFunction(ZoneList<Expression*>* args);

  // Fast call to math functions.
  void GenerateMathPow(ZoneList<Expression*>* args);
  void GenerateMathSin(ZoneList<Expression*>* args);
  void GenerateMathCos(ZoneList<Expression*>* args);
  void GenerateMathSqrt(ZoneList<Expression*>* args);

  // Check whether two RegExps are equivalent
  void GenerateIsRegExpEquivalent(ZoneList<Expression*>* args);

  void GenerateHasCachedArrayIndex(ZoneList<Expression*>* args);
  void GenerateGetCachedArrayIndex(ZoneList<Expression*>* args);
  void GenerateFastAsciiArrayJoin(ZoneList<Expression*>* args);

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
  void CodeForStatementPosition(Statement* stmt);
  void CodeForDoWhileConditionPosition(DoWhileStatement* stmt);
  void CodeForSourcePosition(int pos);

  void SetTypeForStackSlot(Slot* slot, TypeInfo info);

#ifdef DEBUG
  // True if the registers are valid for entry to a block.  There should
  // be no frame-external references to (non-reserved) registers.
  bool HasValidEntryRegisters();
#endif

  ZoneList<DeferredCode*> deferred_;

  // Assembler
  MacroAssembler* masm_;  // to generate code

  CompilationInfo* info_;

  // Code generation state
  VirtualFrame* frame_;
  RegisterAllocator* allocator_;
  CodeGenState* state_;
  int loop_nesting_;
  bool in_safe_int32_mode_;
  bool safe_int32_mode_enabled_;

  // Jump targets.
  // The target of the return from the function.
  BreakTarget function_return_;
  // The target of the bailout from a side-effect-free int32 subexpression.
  BreakTarget* unsafe_bailout_;

  // True if the function return is shadowed (ie, jumping to the target
  // function_return_ does not jump to the true function return, but rather
  // to some unlinking code).
  bool function_return_is_shadowed_;

  // True when we are in code that expects the virtual frame to be fully
  // spilled.  Some virtual frame function are disabled in DEBUG builds when
  // called from spilled code, because they do not leave the virtual frame
  // in a spilled state.
  bool in_spilled_code_;

  // A cookie that is used for JIT IMM32 Encoding.  Initialized to a
  // random number when the command-line
  // FLAG_mask_constants_with_cookie is true, zero otherwise.
  int jit_cookie_;

  friend class VirtualFrame;
  friend class JumpTarget;
  friend class Reference;
  friend class Result;
  friend class FastCodeGenerator;
  friend class FullCodeGenerator;
  friend class FullCodeGenSyntaxChecker;

  friend class CodeGeneratorPatcher;  // Used in test-log-stack-tracer.cc

  DISALLOW_COPY_AND_ASSIGN(CodeGenerator);
};


} }  // namespace v8::internal

#endif  // V8_IA32_CODEGEN_IA32_H_
