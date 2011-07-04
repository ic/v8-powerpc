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

#ifndef V8_MIPS_MACRO_ASSEMBLER_MIPS_H_
#define V8_MIPS_MACRO_ASSEMBLER_MIPS_H_

#include "assembler.h"
#include "mips/assembler-mips.h"
#include "v8globals.h"

namespace v8 {
namespace internal {

// Forward declaration.
class JumpTarget;

// Reserved Register Usage Summary.
//
// Registers t8, t9, and at are reserved for use by the MacroAssembler.
//
// The programmer should know that the MacroAssembler may clobber these three,
// but won't touch other registers except in special cases.
//
// Per the MIPS ABI, register t9 must be used for indirect function call
// via 'jalr t9' or 'jr t9' instructions. This is relied upon by gcc when
// trying to update gp register for position-independent-code. Whenever
// MIPS generated code calls C code, it must be via t9 register.

// Registers aliases
// cp is assumed to be a callee saved register.
const Register roots = s6;  // Roots array pointer.
const Register cp = s7;     // JavaScript context pointer.
const Register fp = s8_fp;  // Alias for fp.
// Registers used for condition evaluation.
const Register condReg1 = s4;
const Register condReg2 = s5;


// Flags used for the AllocateInNewSpace functions.
enum AllocationFlags {
  // No special flags.
  NO_ALLOCATION_FLAGS = 0,
  // Return the pointer to the allocated already tagged as a heap object.
  TAG_OBJECT = 1 << 0,
  // The content of the result register already contains the allocation top in
  // new space.
  RESULT_CONTAINS_TOP = 1 << 1,
  // Specify that the requested size of the space to allocate is specified in
  // words instead of bytes.
  SIZE_IN_WORDS = 1 << 2
};

// Flags used for the ObjectToDoubleFPURegister function.
enum ObjectToDoubleFlags {
  // No special flags.
  NO_OBJECT_TO_DOUBLE_FLAGS = 0,
  // Object is known to be a non smi.
  OBJECT_NOT_SMI = 1 << 0,
  // Don't load NaNs or infinities, branch to the non number case instead.
  AVOID_NANS_AND_INFINITIES = 1 << 1
};

// Allow programmer to use Branch Delay Slot of Branches, Jumps, Calls.
enum BranchDelaySlot {
  USE_DELAY_SLOT,
  PROTECT
};

// MacroAssembler implements a collection of frequently used macros.
class MacroAssembler: public Assembler {
 public:
  // The isolate parameter can be NULL if the macro assembler should
  // not use isolate-dependent functionality. In this case, it's the
  // responsibility of the caller to never invoke such function on the
  // macro assembler.
  MacroAssembler(Isolate* isolate, void* buffer, int size);

// Arguments macros.
#define COND_TYPED_ARGS Condition cond, Register r1, const Operand& r2
#define COND_ARGS cond, r1, r2

// Prototypes.

// Prototypes for functions with no target (eg Ret()).
#define DECLARE_NOTARGET_PROTOTYPE(Name) \
  void Name(BranchDelaySlot bd = PROTECT); \
  void Name(COND_TYPED_ARGS, BranchDelaySlot bd = PROTECT); \
  inline void Name(BranchDelaySlot bd, COND_TYPED_ARGS) { \
    Name(COND_ARGS, bd); \
  }

// Prototypes for functions with a target.

// Cases when relocation may be needed.
#define DECLARE_RELOC_PROTOTYPE(Name, target_type) \
  void Name(target_type target, \
            RelocInfo::Mode rmode, \
            BranchDelaySlot bd = PROTECT); \
  inline void Name(BranchDelaySlot bd, \
                   target_type target, \
                   RelocInfo::Mode rmode) { \
    Name(target, rmode, bd); \
  } \
  void Name(target_type target, \
            RelocInfo::Mode rmode, \
            COND_TYPED_ARGS, \
            BranchDelaySlot bd = PROTECT); \
  inline void Name(BranchDelaySlot bd, \
                   target_type target, \
                   RelocInfo::Mode rmode, \
                   COND_TYPED_ARGS) { \
    Name(target, rmode, COND_ARGS, bd); \
  }

// Cases when relocation is not needed.
#define DECLARE_NORELOC_PROTOTYPE(Name, target_type) \
  void Name(target_type target, BranchDelaySlot bd = PROTECT); \
  inline void Name(BranchDelaySlot bd, target_type target) { \
    Name(target, bd); \
  } \
  void Name(target_type target, \
            COND_TYPED_ARGS, \
            BranchDelaySlot bd = PROTECT); \
  inline void Name(BranchDelaySlot bd, \
                   target_type target, \
                   COND_TYPED_ARGS) { \
    Name(target, COND_ARGS, bd); \
  }

// Target prototypes.

#define DECLARE_JUMP_CALL_PROTOTYPES(Name) \
  DECLARE_NORELOC_PROTOTYPE(Name, Register) \
  DECLARE_NORELOC_PROTOTYPE(Name, const Operand&) \
  DECLARE_RELOC_PROTOTYPE(Name, byte*) \
  DECLARE_RELOC_PROTOTYPE(Name, Handle<Code>)

#define DECLARE_BRANCH_PROTOTYPES(Name) \
  DECLARE_NORELOC_PROTOTYPE(Name, Label*) \
  DECLARE_NORELOC_PROTOTYPE(Name, int16_t)


DECLARE_JUMP_CALL_PROTOTYPES(Jump)
DECLARE_JUMP_CALL_PROTOTYPES(Call)

DECLARE_BRANCH_PROTOTYPES(Branch)
DECLARE_BRANCH_PROTOTYPES(BranchAndLink)

DECLARE_NOTARGET_PROTOTYPE(Ret)

#undef COND_TYPED_ARGS
#undef COND_ARGS
#undef DECLARE_NOTARGET_PROTOTYPE
#undef DECLARE_NORELOC_PROTOTYPE
#undef DECLARE_RELOC_PROTOTYPE
#undef DECLARE_JUMP_CALL_PROTOTYPES
#undef DECLARE_BRANCH_PROTOTYPES

  void CallWithAstId(Handle<Code> code,
                     RelocInfo::Mode rmode = RelocInfo::CODE_TARGET,
                     unsigned ast_id = kNoASTId,
                     Condition cond = al,
                     Register r1 = zero_reg,
                     const Operand& r2 = Operand(zero_reg));

  int CallSize(Register reg);
  int CallSize(Handle<Code> code, RelocInfo::Mode rmode);

  // Emit code to discard a non-negative number of pointer-sized elements
  // from the stack, clobbering only the sp register.
  void Drop(int count,
            Condition cond = cc_always,
            Register reg = no_reg,
            const Operand& op = Operand(no_reg));

  void DropAndRet(int drop = 0,
                  Condition cond = cc_always,
                  Register reg = no_reg,
                  const Operand& op = Operand(no_reg));

  // Swap two registers.  If the scratch register is omitted then a slightly
  // less efficient form using xor instead of mov is emitted.
  void Swap(Register reg1, Register reg2, Register scratch = no_reg);

  void Call(Label* target);

  inline void Move(Register dst, Register src) {
    if (!dst.is(src)) {
      mov(dst, src);
    }
  }

  inline void Move(FPURegister dst, FPURegister src) {
    if (!dst.is(src)) {
      mov_d(dst, src);
    }
  }

  inline void Move(Register dst_low, Register dst_high, FPURegister src) {
    mfc1(dst_low, src);
    mfc1(dst_high, FPURegister::from_code(src.code() + 1));
  }

  inline void Move(FPURegister dst, Register src_low, Register src_high) {
    mtc1(src_low, dst);
    mtc1(src_high, FPURegister::from_code(dst.code() + 1));
  }

  // Jump unconditionally to given label.
  // We NEED a nop in the branch delay slot, as it used by v8, for example in
  // CodeGenerator::ProcessDeferred().
  // Currently the branch delay slot is filled by the MacroAssembler.
  // Use rather b(Label) for code generation.
  void jmp(Label* L) {
    Branch(L);
  }

  // Load an object from the root table.
  void LoadRoot(Register destination,
                Heap::RootListIndex index);
  void LoadRoot(Register destination,
                Heap::RootListIndex index,
                Condition cond, Register src1, const Operand& src2);

  // Store an object to the root table.
  void StoreRoot(Register source,
                 Heap::RootListIndex index);
  void StoreRoot(Register source,
                 Heap::RootListIndex index,
                 Condition cond, Register src1, const Operand& src2);


  // Check if object is in new space.
  // scratch can be object itself, but it will be clobbered.
  void InNewSpace(Register object,
                  Register scratch,
                  Condition cc,  // eq for new space, ne otherwise.
                  Label* branch);


  // For the page containing |object| mark the region covering [address]
  // dirty. The object address must be in the first 8K of an allocated page.
  void RecordWriteHelper(Register object,
                         Register address,
                         Register scratch);

  // For the page containing |object| mark the region covering
  // [object+offset] dirty. The object address must be in the first 8K
  // of an allocated page.  The 'scratch' registers are used in the
  // implementation and all 3 registers are clobbered by the
  // operation, as well as the 'at' register. RecordWrite updates the
  // write barrier even when storing smis.
  void RecordWrite(Register object,
                   Operand offset,
                   Register scratch0,
                   Register scratch1);

  // For the page containing |object| mark the region covering
  // [address] dirty. The object address must be in the first 8K of an
  // allocated page.  All 3 registers are clobbered by the operation,
  // as well as the ip register. RecordWrite updates the write barrier
  // even when storing smis.
  void RecordWrite(Register object,
                   Register address,
                   Register scratch);


  // ---------------------------------------------------------------------------
  // Inline caching support.

  // Generate code for checking access rights - used for security checks
  // on access to global objects across environments. The holder register
  // is left untouched, whereas both scratch registers are clobbered.
  void CheckAccessGlobalProxy(Register holder_reg,
                              Register scratch,
                              Label* miss);

  inline void MarkCode(NopMarkerTypes type) {
    nop(type);
  }

  // Check if the given instruction is a 'type' marker.
  // ie. check if it is a sll zero_reg, zero_reg, <type> (referenced as
  // nop(type)). These instructions are generated to mark special location in
  // the code, like some special IC code.
  static inline bool IsMarkedCode(Instr instr, int type) {
    ASSERT((FIRST_IC_MARKER <= type) && (type < LAST_CODE_MARKER));
    return IsNop(instr, type);
  }


  static inline int GetCodeMarker(Instr instr) {
    uint32_t opcode = ((instr & kOpcodeMask));
    uint32_t rt = ((instr & kRtFieldMask) >> kRtShift);
    uint32_t rs = ((instr & kRsFieldMask) >> kRsShift);
    uint32_t sa = ((instr & kSaFieldMask) >> kSaShift);

    // Return <n> if we have a sll zero_reg, zero_reg, n
    // else return -1.
    bool sllzz = (opcode == SLL &&
                  rt == static_cast<uint32_t>(ToNumber(zero_reg)) &&
                  rs == static_cast<uint32_t>(ToNumber(zero_reg)));
    int type =
        (sllzz && FIRST_IC_MARKER <= sa && sa < LAST_CODE_MARKER) ? sa : -1;
    ASSERT((type == -1) ||
           ((FIRST_IC_MARKER <= type) && (type < LAST_CODE_MARKER)));
    return type;
  }



  // ---------------------------------------------------------------------------
  // Allocation support.

  // Allocate an object in new space. The object_size is specified
  // either in bytes or in words if the allocation flag SIZE_IN_WORDS
  // is passed. If the new space is exhausted control continues at the
  // gc_required label. The allocated object is returned in result. If
  // the flag tag_allocated_object is true the result is tagged as as
  // a heap object. All registers are clobbered also when control
  // continues at the gc_required label.
  void AllocateInNewSpace(int object_size,
                          Register result,
                          Register scratch1,
                          Register scratch2,
                          Label* gc_required,
                          AllocationFlags flags);
  void AllocateInNewSpace(Register object_size,
                          Register result,
                          Register scratch1,
                          Register scratch2,
                          Label* gc_required,
                          AllocationFlags flags);

  // Undo allocation in new space. The object passed and objects allocated after
  // it will no longer be allocated. The caller must make sure that no pointers
  // are left to the object(s) no longer allocated as they would be invalid when
  // allocation is undone.
  void UndoAllocationInNewSpace(Register object, Register scratch);


  void AllocateTwoByteString(Register result,
                             Register length,
                             Register scratch1,
                             Register scratch2,
                             Register scratch3,
                             Label* gc_required);
  void AllocateAsciiString(Register result,
                           Register length,
                           Register scratch1,
                           Register scratch2,
                           Register scratch3,
                           Label* gc_required);
  void AllocateTwoByteConsString(Register result,
                                 Register length,
                                 Register scratch1,
                                 Register scratch2,
                                 Label* gc_required);
  void AllocateAsciiConsString(Register result,
                               Register length,
                               Register scratch1,
                               Register scratch2,
                               Label* gc_required);

  // Allocates a heap number or jumps to the gc_required label if the young
  // space is full and a scavenge is needed. All registers are clobbered also
  // when control continues at the gc_required label.
  void AllocateHeapNumber(Register result,
                          Register scratch1,
                          Register scratch2,
                          Register heap_number_map,
                          Label* gc_required);
  void AllocateHeapNumberWithValue(Register result,
                                   FPURegister value,
                                   Register scratch1,
                                   Register scratch2,
                                   Label* gc_required);

  // ---------------------------------------------------------------------------
  // Instruction macros.

#define DEFINE_INSTRUCTION(instr)                                              \
  void instr(Register rd, Register rs, const Operand& rt);                     \
  void instr(Register rd, Register rs, Register rt) {                          \
    instr(rd, rs, Operand(rt));                                                \
  }                                                                            \
  void instr(Register rs, Register rt, int32_t j) {                            \
    instr(rs, rt, Operand(j));                                                 \
  }

#define DEFINE_INSTRUCTION2(instr)                                             \
  void instr(Register rs, const Operand& rt);                                  \
  void instr(Register rs, Register rt) {                                       \
    instr(rs, Operand(rt));                                                    \
  }                                                                            \
  void instr(Register rs, int32_t j) {                                         \
    instr(rs, Operand(j));                                                     \
  }

  DEFINE_INSTRUCTION(Addu);
  DEFINE_INSTRUCTION(Subu);
  DEFINE_INSTRUCTION(Mul);
  DEFINE_INSTRUCTION2(Mult);
  DEFINE_INSTRUCTION2(Multu);
  DEFINE_INSTRUCTION2(Div);
  DEFINE_INSTRUCTION2(Divu);

  DEFINE_INSTRUCTION(And);
  DEFINE_INSTRUCTION(Or);
  DEFINE_INSTRUCTION(Xor);
  DEFINE_INSTRUCTION(Nor);
  DEFINE_INSTRUCTION2(Neg);

  DEFINE_INSTRUCTION(Slt);
  DEFINE_INSTRUCTION(Sltu);

  // MIPS32 R2 instruction macro.
  DEFINE_INSTRUCTION(Ror);

#undef DEFINE_INSTRUCTION
#undef DEFINE_INSTRUCTION2


  // ---------------------------------------------------------------------------
  // Pseudo-instructions.

  void mov(Register rd, Register rt) { or_(rd, rt, zero_reg); }

  // Load int32 in the rd register.
  void li(Register rd, Operand j, bool gen2instr = false);
  inline void li(Register rd, int32_t j, bool gen2instr = false) {
    li(rd, Operand(j), gen2instr);
  }
  inline void li(Register dst, Handle<Object> value, bool gen2instr = false) {
    li(dst, Operand(value), gen2instr);
  }

  // Push multiple registers on the stack.
  // Registers are saved in numerical order, with higher numbered registers
  // saved in higher memory addresses.
  void MultiPush(RegList regs);
  void MultiPushReversed(RegList regs);

  // Lower case push() for compatibility with arch-independent code.
  void push(Register src) {
    Addu(sp, sp, Operand(-kPointerSize));
    sw(src, MemOperand(sp, 0));
  }

  // Push a handle.
  void Push(Handle<Object> handle);

  // Push two registers. Pushes leftmost register first (to highest address).
  void Push(Register src1, Register src2) {
    Subu(sp, sp, Operand(2 * kPointerSize));
    sw(src1, MemOperand(sp, 1 * kPointerSize));
    sw(src2, MemOperand(sp, 0 * kPointerSize));
  }

  // Push three registers. Pushes leftmost register first (to highest address).
  void Push(Register src1, Register src2, Register src3) {
    Subu(sp, sp, Operand(3 * kPointerSize));
    sw(src1, MemOperand(sp, 2 * kPointerSize));
    sw(src2, MemOperand(sp, 1 * kPointerSize));
    sw(src3, MemOperand(sp, 0 * kPointerSize));
  }

  // Push four registers. Pushes leftmost register first (to highest address).
  void Push(Register src1, Register src2, Register src3, Register src4) {
    Subu(sp, sp, Operand(4 * kPointerSize));
    sw(src1, MemOperand(sp, 3 * kPointerSize));
    sw(src2, MemOperand(sp, 2 * kPointerSize));
    sw(src3, MemOperand(sp, 1 * kPointerSize));
    sw(src4, MemOperand(sp, 0 * kPointerSize));
  }

  void Push(Register src, Condition cond, Register tst1, Register tst2) {
    // Since we don't have conditional execution we use a Branch.
    Branch(3, cond, tst1, Operand(tst2));
    Subu(sp, sp, Operand(kPointerSize));
    sw(src, MemOperand(sp, 0));
  }

  // Pops multiple values from the stack and load them in the
  // registers specified in regs. Pop order is the opposite as in MultiPush.
  void MultiPop(RegList regs);
  void MultiPopReversed(RegList regs);

  // Lower case pop() for compatibility with arch-independent code.
  void pop(Register dst) {
    lw(dst, MemOperand(sp, 0));
    Addu(sp, sp, Operand(kPointerSize));
  }

  // Pop two registers. Pops rightmost register first (from lower address).
  void Pop(Register src1, Register src2) {
    ASSERT(!src1.is(src2));
    lw(src2, MemOperand(sp, 0 * kPointerSize));
    lw(src1, MemOperand(sp, 1 * kPointerSize));
    Addu(sp, sp, 2 * kPointerSize);
  }

  void Pop(uint32_t count = 1) {
    Addu(sp, sp, Operand(count * kPointerSize));
  }

  // Push and pop the registers that can hold pointers, as defined by the
  // RegList constant kSafepointSavedRegisters.
  void PushSafepointRegisters();
  void PopSafepointRegisters();
  void PushSafepointRegistersAndDoubles();
  void PopSafepointRegistersAndDoubles();
  // Store value in register src in the safepoint stack slot for
  // register dst.
  void StoreToSafepointRegisterSlot(Register src, Register dst);
  void StoreToSafepointRegistersAndDoublesSlot(Register src, Register dst);
  // Load the value of the src register from its safepoint stack slot
  // into register dst.
  void LoadFromSafepointRegisterSlot(Register dst, Register src);

  // MIPS32 R2 instruction macro.
  void Ins(Register rt, Register rs, uint16_t pos, uint16_t size);
  void Ext(Register rt, Register rs, uint16_t pos, uint16_t size);

  // Convert unsigned word to double.
  void Cvt_d_uw(FPURegister fd, FPURegister fs);
  void Cvt_d_uw(FPURegister fd, Register rs);

  // Convert double to unsigned word.
  void Trunc_uw_d(FPURegister fd, FPURegister fs);
  void Trunc_uw_d(FPURegister fd, Register rs);

  // Convert the HeapNumber pointed to by source to a 32bits signed integer
  // dest. If the HeapNumber does not fit into a 32bits signed integer branch
  // to not_int32 label. If FPU is available double_scratch is used but not
  // scratch2.
  void ConvertToInt32(Register source,
                      Register dest,
                      Register scratch,
                      Register scratch2,
                      FPURegister double_scratch,
                      Label *not_int32);

  // Helper for EmitECMATruncate.
  // This will truncate a floating-point value outside of the singed 32bit
  // integer range to a 32bit signed integer.
  // Expects the double value loaded in input_high and input_low.
  // Exits with the answer in 'result'.
  // Note that this code does not work for values in the 32bit range!
  void EmitOutOfInt32RangeTruncate(Register result,
                                   Register input_high,
                                   Register input_low,
                                   Register scratch);

  // Performs a truncating conversion of a floating point number as used by
  // the JS bitwise operations. See ECMA-262 9.5: ToInt32.
  // Exits with 'result' holding the answer and all other registers clobbered.
  void EmitECMATruncate(Register result,
                        FPURegister double_input,
                        FPURegister single_scratch,
                        Register scratch,
                        Register scratch2,
                        Register scratch3);

  // -------------------------------------------------------------------------
  // Activation frames.

  void EnterInternalFrame() { EnterFrame(StackFrame::INTERNAL); }
  void LeaveInternalFrame() { LeaveFrame(StackFrame::INTERNAL); }

  void EnterConstructFrame() { EnterFrame(StackFrame::CONSTRUCT); }
  void LeaveConstructFrame() { LeaveFrame(StackFrame::CONSTRUCT); }

  // Enter exit frame.
  // argc - argument count to be dropped by LeaveExitFrame.
  // save_doubles - saves FPU registers on stack, currently disabled.
  // stack_space - extra stack space.
  void EnterExitFrame(bool save_doubles,
                      int stack_space = 0);

  // Leave the current exit frame.
  void LeaveExitFrame(bool save_doubles, Register arg_count);

  // Get the actual activation frame alignment for target environment.
  static int ActivationFrameAlignment();

  // Make sure the stack is aligned. Only emits code in debug mode.
  void AssertStackIsAligned();

  void LoadContext(Register dst, int context_chain_length);

  void LoadGlobalFunction(int index, Register function);

  // Load the initial map from the global function. The registers
  // function and map can be the same, function is then overwritten.
  void LoadGlobalFunctionInitialMap(Register function,
                                    Register map,
                                    Register scratch);

  // -------------------------------------------------------------------------
  // JavaScript invokes.

  // Setup call kind marking in t1. The method takes t1 as an
  // explicit first parameter to make the code more readable at the
  // call sites.
  void SetCallKind(Register dst, CallKind kind);

  // Invoke the JavaScript function code by either calling or jumping.
  void InvokeCode(Register code,
                  const ParameterCount& expected,
                  const ParameterCount& actual,
                  InvokeFlag flag,
                  const CallWrapper& call_wrapper,
                  CallKind call_kind);

  void InvokeCode(Handle<Code> code,
                  const ParameterCount& expected,
                  const ParameterCount& actual,
                  RelocInfo::Mode rmode,
                  InvokeFlag flag,
                  CallKind call_kind);

  // Invoke the JavaScript function in the given register. Changes the
  // current context to the context in the function before invoking.
  void InvokeFunction(Register function,
                      const ParameterCount& actual,
                      InvokeFlag flag,
                      const CallWrapper& call_wrapper,
                      CallKind call_kind);

  void InvokeFunction(JSFunction* function,
                      const ParameterCount& actual,
                      InvokeFlag flag,
                      CallKind call_kind);


  void IsObjectJSObjectType(Register heap_object,
                            Register map,
                            Register scratch,
                            Label* fail);

  void IsInstanceJSObjectType(Register map,
                              Register scratch,
                              Label* fail);

  void IsObjectJSStringType(Register object,
                            Register scratch,
                            Label* fail);

#ifdef ENABLE_DEBUGGER_SUPPORT
  // -------------------------------------------------------------------------
  // Debugger Support.

  void DebugBreak();
#endif


  // -------------------------------------------------------------------------
  // Exception handling.

  // Push a new try handler and link into try handler chain.
  // The return address must be passed in register ra.
  // Clobber t0, t1, t2.
  void PushTryHandler(CodeLocation try_location, HandlerType type);

  // Unlink the stack handler on top of the stack from the try handler chain.
  // Must preserve the result register.
  void PopTryHandler();

  // Passes thrown value (in v0) to the handler of top of the try handler chain.
  void Throw(Register value);

  // Propagates an uncatchable exception to the top of the current JS stack's
  // handler chain.
  void ThrowUncatchable(UncatchableExceptionType type, Register value);

  // Copies a fixed number of fields of heap objects from src to dst.
  void CopyFields(Register dst, Register src, RegList temps, int field_count);

  // Copies a number of bytes from src to dst. All registers are clobbered. On
  // exit src and dst will point to the place just after where the last byte was
  // read or written and length will be zero.
  void CopyBytes(Register src,
                 Register dst,
                 Register length,
                 Register scratch);

  // -------------------------------------------------------------------------
  // Support functions.

  // Try to get function prototype of a function and puts the value in
  // the result register. Checks that the function really is a
  // function and jumps to the miss label if the fast checks fail. The
  // function register will be untouched; the other registers may be
  // clobbered.
  void TryGetFunctionPrototype(Register function,
                               Register result,
                               Register scratch,
                               Label* miss);

  void GetObjectType(Register function,
                     Register map,
                     Register type_reg);

  // Check if a map for a JSObject indicates that the object has fast elements.
  // Jump to the specified label if it does not.
  void CheckFastElements(Register map,
                         Register scratch,
                         Label* fail);

  // Check if the map of an object is equal to a specified map (either
  // given directly or as an index into the root list) and branch to
  // label if not. Skip the smi check if not required (object is known
  // to be a heap object).
  void CheckMap(Register obj,
                Register scratch,
                Handle<Map> map,
                Label* fail,
                SmiCheckType smi_check_type);

  void CheckMap(Register obj,
                Register scratch,
                Heap::RootListIndex index,
                Label* fail,
                SmiCheckType smi_check_type);

  // Check if the map of an object is equal to a specified map and branch to a
  // specified target if equal. Skip the smi check if not required (object is
  // known to be a heap object)
  void DispatchMap(Register obj,
                   Register scratch,
                   Handle<Map> map,
                   Handle<Code> success,
                   SmiCheckType smi_check_type);

  // Generates code for reporting that an illegal operation has
  // occurred.
  void IllegalOperation(int num_arguments);

  // Picks out an array index from the hash field.
  // Register use:
  //   hash - holds the index's hash. Clobbered.
  //   index - holds the overwritten index on exit.
  void IndexFromHash(Register hash, Register index);

  // Get the number of least significant bits from a register.
  void GetLeastBitsFromSmi(Register dst, Register src, int num_least_bits);
  void GetLeastBitsFromInt32(Register dst, Register src, int mun_least_bits);

  // Load the value of a number object into a FPU double register. If the
  // object is not a number a jump to the label not_number is performed
  // and the FPU double register is unchanged.
  void ObjectToDoubleFPURegister(
      Register object,
      FPURegister value,
      Register scratch1,
      Register scratch2,
      Register heap_number_map,
      Label* not_number,
      ObjectToDoubleFlags flags = NO_OBJECT_TO_DOUBLE_FLAGS);

  // Load the value of a smi object into a FPU double register. The register
  // scratch1 can be the same register as smi in which case smi will hold the
  // untagged value afterwards.
  void SmiToDoubleFPURegister(Register smi,
                              FPURegister value,
                              Register scratch1);

  // -------------------------------------------------------------------------
  // Overflow handling functions.
  // Usage: first call the appropriate arithmetic function, then call one of the
  // jump functions with the overflow_dst register as the second parameter.

  void AdduAndCheckForOverflow(Register dst,
                               Register left,
                               Register right,
                               Register overflow_dst,
                               Register scratch = at);

  void SubuAndCheckForOverflow(Register dst,
                               Register left,
                               Register right,
                               Register overflow_dst,
                               Register scratch = at);

  void BranchOnOverflow(Label* label,
                        Register overflow_check,
                        BranchDelaySlot bd = PROTECT) {
    Branch(label, lt, overflow_check, Operand(zero_reg), bd);
  }

  void BranchOnNoOverflow(Label* label,
                          Register overflow_check,
                          BranchDelaySlot bd = PROTECT) {
    Branch(label, ge, overflow_check, Operand(zero_reg), bd);
  }

  void RetOnOverflow(Register overflow_check, BranchDelaySlot bd = PROTECT) {
    Ret(lt, overflow_check, Operand(zero_reg), bd);
  }

  void RetOnNoOverflow(Register overflow_check, BranchDelaySlot bd = PROTECT) {
    Ret(ge, overflow_check, Operand(zero_reg), bd);
  }

  // -------------------------------------------------------------------------
  // Runtime calls.

  // Call a code stub.
  void CallStub(CodeStub* stub, Condition cond = cc_always,
                Register r1 = zero_reg, const Operand& r2 = Operand(zero_reg));

  // Call a code stub and return the code object called.  Try to generate
  // the code if necessary.  Do not perform a GC but instead return a retry
  // after GC failure.
  MUST_USE_RESULT MaybeObject* TryCallStub(CodeStub* stub,
                                           Condition cond = cc_always,
                                           Register r1 = zero_reg,
                                           const Operand& r2 =
                                               Operand(zero_reg));

  // Tail call a code stub (jump).
  void TailCallStub(CodeStub* stub);

  // Tail call a code stub (jump) and return the code object called.  Try to
  // generate the code if necessary.  Do not perform a GC but instead return
  // a retry after GC failure.
  MUST_USE_RESULT MaybeObject* TryTailCallStub(CodeStub* stub,
                                               Condition cond = cc_always,
                                               Register r1 = zero_reg,
                                               const Operand& r2 =
                                                   Operand(zero_reg));

  void CallJSExitStub(CodeStub* stub);

  // Call a runtime routine.
  void CallRuntime(const Runtime::Function* f, int num_arguments);
  void CallRuntimeSaveDoubles(Runtime::FunctionId id);

  // Convenience function: Same as above, but takes the fid instead.
  void CallRuntime(Runtime::FunctionId fid, int num_arguments);

  // Convenience function: call an external reference.
  void CallExternalReference(const ExternalReference& ext,
                             int num_arguments);

  // Tail call of a runtime routine (jump).
  // Like JumpToExternalReference, but also takes care of passing the number
  // of parameters.
  void TailCallExternalReference(const ExternalReference& ext,
                                 int num_arguments,
                                 int result_size);

  // Tail call of a runtime routine (jump). Try to generate the code if
  // necessary. Do not perform a GC but instead return a retry after GC
  // failure.
  MUST_USE_RESULT MaybeObject* TryTailCallExternalReference(
      const ExternalReference& ext, int num_arguments, int result_size);

  // Convenience function: tail call a runtime routine (jump).
  void TailCallRuntime(Runtime::FunctionId fid,
                       int num_arguments,
                       int result_size);

  // Before calling a C-function from generated code, align arguments on stack
  // and add space for the four mips argument slots.
  // After aligning the frame, non-register arguments must be stored on the
  // stack, after the argument-slots using helper: CFunctionArgumentOperand().
  // The argument count assumes all arguments are word sized.
  // Some compilers/platforms require the stack to be aligned when calling
  // C++ code.
  // Needs a scratch register to do some arithmetic. This register will be
  // trashed.
  void PrepareCallCFunction(int num_arguments, Register scratch);

  // Arguments 1-4 are placed in registers a0 thru a3 respectively.
  // Arguments 5..n are stored to stack using following:
  //  sw(t0, CFunctionArgumentOperand(5));

  // Calls a C function and cleans up the space for arguments allocated
  // by PrepareCallCFunction. The called function is not allowed to trigger a
  // garbage collection, since that might move the code and invalidate the
  // return address (unless this is somehow accounted for by the called
  // function).
  void CallCFunction(ExternalReference function, int num_arguments);
  void CallCFunction(Register function, Register scratch, int num_arguments);
  void GetCFunctionDoubleResult(const DoubleRegister dst);

  // There are two ways of passing double arguments on MIPS, depending on
  // whether soft or hard floating point ABI is used. These functions
  // abstract parameter passing for the three different ways we call
  // C functions from generated code.
  void SetCallCDoubleArguments(DoubleRegister dreg);
  void SetCallCDoubleArguments(DoubleRegister dreg1, DoubleRegister dreg2);
  void SetCallCDoubleArguments(DoubleRegister dreg, Register reg);

  // Calls an API function. Allocates HandleScope, extracts returned value
  // from handle and propagates exceptions. Restores context.
  MaybeObject* TryCallApiFunctionAndReturn(ExternalReference function,
                                           int stack_space);

  // Jump to the builtin routine.
  void JumpToExternalReference(const ExternalReference& builtin);

  MaybeObject* TryJumpToExternalReference(const ExternalReference& ext);

  // Invoke specified builtin JavaScript function. Adds an entry to
  // the unresolved list if the name does not resolve.
  void InvokeBuiltin(Builtins::JavaScript id,
                     InvokeFlag flag,
                     const CallWrapper& call_wrapper = NullCallWrapper());

  // Store the code object for the given builtin in the target register and
  // setup the function in a1.
  void GetBuiltinEntry(Register target, Builtins::JavaScript id);

  // Store the function for the given builtin in the target register.
  void GetBuiltinFunction(Register target, Builtins::JavaScript id);

  struct Unresolved {
    int pc;
    uint32_t flags;  // See Bootstrapper::FixupFlags decoders/encoders.
    const char* name;
  };

  Handle<Object> CodeObject() {
    ASSERT(!code_object_.is_null());
    return code_object_;
  }

  // -------------------------------------------------------------------------
  // StatsCounter support.

  void SetCounter(StatsCounter* counter, int value,
                  Register scratch1, Register scratch2);
  void IncrementCounter(StatsCounter* counter, int value,
                        Register scratch1, Register scratch2);
  void DecrementCounter(StatsCounter* counter, int value,
                        Register scratch1, Register scratch2);


  // -------------------------------------------------------------------------
  // Debugging.

  // Calls Abort(msg) if the condition cc is not satisfied.
  // Use --debug_code to enable.
  void Assert(Condition cc, const char* msg, Register rs, Operand rt);
  void AssertRegisterIsRoot(Register reg, Heap::RootListIndex index);
  void AssertFastElements(Register elements);

  // Like Assert(), but always enabled.
  void Check(Condition cc, const char* msg, Register rs, Operand rt);

  // Print a message to stdout and abort execution.
  void Abort(const char* msg);

  // Verify restrictions about code generated in stubs.
  void set_generating_stub(bool value) { generating_stub_ = value; }
  bool generating_stub() { return generating_stub_; }
  void set_allow_stub_calls(bool value) { allow_stub_calls_ = value; }
  bool allow_stub_calls() { return allow_stub_calls_; }

  // ---------------------------------------------------------------------------
  // Number utilities.

  // Check whether the value of reg is a power of two and not zero. If not
  // control continues at the label not_power_of_two. If reg is a power of two
  // the register scratch contains the value of (reg - 1) when control falls
  // through.
  void JumpIfNotPowerOfTwoOrZero(Register reg,
                                 Register scratch,
                                 Label* not_power_of_two_or_zero);

  // -------------------------------------------------------------------------
  // Smi utilities.

  // Try to convert int32 to smi. If the value is to large, preserve
  // the original value and jump to not_a_smi. Destroys scratch and
  // sets flags.
  // This is only used by crankshaft atm so it is unimplemented on MIPS.
  void TrySmiTag(Register reg, Label* not_a_smi, Register scratch) {
    UNIMPLEMENTED_MIPS();
  }

  void SmiTag(Register reg) {
    Addu(reg, reg, reg);
  }

  void SmiTag(Register dst, Register src) {
    Addu(dst, src, src);
  }

  void SmiUntag(Register reg) {
    sra(reg, reg, kSmiTagSize);
  }

  void SmiUntag(Register dst, Register src) {
    sra(dst, src, kSmiTagSize);
  }

  // Jump the register contains a smi.
  inline void JumpIfSmi(Register value, Label* smi_label,
                        Register scratch = at) {
    ASSERT_EQ(0, kSmiTag);
    andi(scratch, value, kSmiTagMask);
    Branch(smi_label, eq, scratch, Operand(zero_reg));
  }

  // Jump if the register contains a non-smi.
  inline void JumpIfNotSmi(Register value, Label* not_smi_label,
                           Register scratch = at) {
    ASSERT_EQ(0, kSmiTag);
    andi(scratch, value, kSmiTagMask);
    Branch(not_smi_label, ne, scratch, Operand(zero_reg));
  }

  // Jump if either of the registers contain a non-smi.
  void JumpIfNotBothSmi(Register reg1, Register reg2, Label* on_not_both_smi);
  // Jump if either of the registers contain a smi.
  void JumpIfEitherSmi(Register reg1, Register reg2, Label* on_either_smi);

  // Abort execution if argument is a smi. Used in debug code.
  void AbortIfSmi(Register object);
  void AbortIfNotSmi(Register object);

  // Abort execution if argument is a string. Used in debug code.
  void AbortIfNotString(Register object);

  // Abort execution if argument is not the root value with the given index.
  void AbortIfNotRootValue(Register src,
                           Heap::RootListIndex root_value_index,
                           const char* message);

  // ---------------------------------------------------------------------------
  // HeapNumber utilities.

  void JumpIfNotHeapNumber(Register object,
                           Register heap_number_map,
                           Register scratch,
                           Label* on_not_heap_number);

  // -------------------------------------------------------------------------
  // String utilities.

  // Checks if both instance types are sequential ASCII strings and jumps to
  // label if either is not.
  void JumpIfBothInstanceTypesAreNotSequentialAscii(
      Register first_object_instance_type,
      Register second_object_instance_type,
      Register scratch1,
      Register scratch2,
      Label* failure);

  // Check if instance type is sequential ASCII string and jump to label if
  // it is not.
  void JumpIfInstanceTypeIsNotSequentialAscii(Register type,
                                              Register scratch,
                                              Label* failure);

  // Test that both first and second are sequential ASCII strings.
  // Assume that they are non-smis.
  void JumpIfNonSmisNotBothSequentialAsciiStrings(Register first,
                                                  Register second,
                                                  Register scratch1,
                                                  Register scratch2,
                                                  Label* failure);

  // Test that both first and second are sequential ASCII strings.
  // Check that they are non-smis.
  void JumpIfNotBothSequentialAsciiStrings(Register first,
                                           Register second,
                                           Register scratch1,
                                           Register scratch2,
                                           Label* failure);

  void LoadInstanceDescriptors(Register map, Register descriptors);

 private:
  void CallCFunctionHelper(Register function,
                           ExternalReference function_reference,
                           Register scratch,
                           int num_arguments);

  void BranchShort(int16_t offset, BranchDelaySlot bdslot = PROTECT);
  void BranchShort(int16_t offset, Condition cond, Register rs,
                   const Operand& rt,
                   BranchDelaySlot bdslot = PROTECT);
  void BranchShort(Label* L, BranchDelaySlot bdslot = PROTECT);
  void BranchShort(Label* L, Condition cond, Register rs,
                   const Operand& rt,
                   BranchDelaySlot bdslot = PROTECT);
  void BranchAndLinkShort(int16_t offset, BranchDelaySlot bdslot = PROTECT);
  void BranchAndLinkShort(int16_t offset, Condition cond, Register rs,
                          const Operand& rt,
                          BranchDelaySlot bdslot = PROTECT);
  void BranchAndLinkShort(Label* L, BranchDelaySlot bdslot = PROTECT);
  void BranchAndLinkShort(Label* L, Condition cond, Register rs,
                          const Operand& rt,
                          BranchDelaySlot bdslot = PROTECT);
  void J(Label* L, BranchDelaySlot bdslot);
  void Jr(Label* L, BranchDelaySlot bdslot);
  void Jalr(Label* L, BranchDelaySlot bdslot);

  void Jump(intptr_t target, RelocInfo::Mode rmode,
            BranchDelaySlot bd = PROTECT);
  void Jump(intptr_t target, RelocInfo::Mode rmode, Condition cond = cc_always,
            Register r1 = zero_reg, const Operand& r2 = Operand(zero_reg),
            BranchDelaySlot bd = PROTECT);
  void Call(intptr_t target, RelocInfo::Mode rmode,
            BranchDelaySlot bd = PROTECT);
  void Call(intptr_t target, RelocInfo::Mode rmode, Condition cond = cc_always,
            Register r1 = zero_reg, const Operand& r2 = Operand(zero_reg),
            BranchDelaySlot bd = PROTECT);

  // Helper functions for generating invokes.
  void InvokePrologue(const ParameterCount& expected,
                      const ParameterCount& actual,
                      Handle<Code> code_constant,
                      Register code_reg,
                      Label* done,
                      InvokeFlag flag,
                      const CallWrapper& call_wrapper,
                      CallKind call_kind);

  // Get the code for the given builtin. Returns if able to resolve
  // the function in the 'resolved' flag.
  Handle<Code> ResolveBuiltin(Builtins::JavaScript id, bool* resolved);

  // Activation support.
  void EnterFrame(StackFrame::Type type);
  void LeaveFrame(StackFrame::Type type);

  void InitializeNewString(Register string,
                           Register length,
                           Heap::RootListIndex map_index,
                           Register scratch1,
                           Register scratch2);

  // Compute memory operands for safepoint stack slots.
  static int SafepointRegisterStackIndex(int reg_code);
  MemOperand SafepointRegisterSlot(Register reg);
  MemOperand SafepointRegistersAndDoublesSlot(Register reg);

  bool UseAbsoluteCodePointers();

  bool generating_stub_;
  bool allow_stub_calls_;
  // This handle will be patched with the code object on installation.
  Handle<Object> code_object_;

  // Needs access to SafepointRegisterStackIndex for optimized frame
  // traversal.
  friend class OptimizedFrame;
};


// The code patcher is used to patch (typically) small parts of code e.g. for
// debugging and other types of instrumentation. When using the code patcher
// the exact number of bytes specified must be emitted. It is not legal to emit
// relocation information. If any of these constraints are violated it causes
// an assertion to fail.
class CodePatcher {
 public:
  CodePatcher(byte* address, int instructions);
  virtual ~CodePatcher();

  // Macro assembler to emit code.
  MacroAssembler* masm() { return &masm_; }

  // Emit an instruction directly.
  void Emit(Instr instr);

  // Emit an address directly.
  void Emit(Address addr);

  // Change the condition part of an instruction leaving the rest of the current
  // instruction unchanged.
  void ChangeBranchCondition(Condition cond);

 private:
  byte* address_;  // The address of the code being patched.
  int instructions_;  // Number of instructions of the expected patch size.
  int size_;  // Number of bytes of the expected patch size.
  MacroAssembler masm_;  // Macro assembler used to generate the code.
};


// -----------------------------------------------------------------------------
// Static helper functions.

static MemOperand ContextOperand(Register context, int index) {
  return MemOperand(context, Context::SlotOffset(index));
}


static inline MemOperand GlobalObjectOperand()  {
  return ContextOperand(cp, Context::GLOBAL_INDEX);
}


// Generate a MemOperand for loading a field from an object.
static inline MemOperand FieldMemOperand(Register object, int offset) {
  return MemOperand(object, offset - kHeapObjectTag);
}


// Generate a MemOperand for storing arguments 5..N on the stack
// when calling CallCFunction().
static inline MemOperand CFunctionArgumentOperand(int index) {
  ASSERT(index > StandardFrameConstants::kCArgSlotCount);
  // Argument 5 takes the slot just past the four Arg-slots.
  int offset =
      (index - 5) * kPointerSize + StandardFrameConstants::kCArgsSlotsSize;
  return MemOperand(sp, offset);
}


#ifdef GENERATED_CODE_COVERAGE
#define CODE_COVERAGE_STRINGIFY(x) #x
#define CODE_COVERAGE_TOSTRING(x) CODE_COVERAGE_STRINGIFY(x)
#define __FILE_LINE__ __FILE__ ":" CODE_COVERAGE_TOSTRING(__LINE__)
#define ACCESS_MASM(masm) masm->stop(__FILE_LINE__); masm->
#else
#define ACCESS_MASM(masm) masm->
#endif

} }  // namespace v8::internal

#endif  // V8_MIPS_MACRO_ASSEMBLER_MIPS_H_
