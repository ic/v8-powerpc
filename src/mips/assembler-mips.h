// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2010 the V8 project authors. All rights reserved.


#ifndef V8_MIPS_ASSEMBLER_MIPS_H_
#define V8_MIPS_ASSEMBLER_MIPS_H_

#include <stdio.h>
#include "assembler.h"
#include "constants-mips.h"
#include "serialize.h"

namespace v8 {
namespace internal {

// CPU Registers.
//
// 1) We would prefer to use an enum, but enum values are assignment-
// compatible with int, which has caused code-generation bugs.
//
// 2) We would prefer to use a class instead of a struct but we don't like
// the register initialization to depend on the particular initialization
// order (which appears to be different on OS X, Linux, and Windows for the
// installed versions of C++ we tried). Using a struct permits C-style
// "initialization". Also, the Register objects cannot be const as this
// forces initialization stubs in MSVC, making us dependent on initialization
// order.
//
// 3) By not using an enum, we are possibly preventing the compiler from
// doing certain constant folds, which may significantly reduce the
// code generated for some assembly instructions (because they boil down
// to a few constants). If this is a problem, we could change the code
// such that we use an enum in optimized mode, and the struct in debug
// mode. This way we get the compile-time error checking in debug mode
// and best performance in optimized code.


// -----------------------------------------------------------------------------
// Implementation of Register and FPURegister

// Core register.
struct Register {
  static const int kNumRegisters = v8::internal::kNumRegisters;
  static const int kNumAllocatableRegisters = 14;  // v0 through t7

  static int ToAllocationIndex(Register reg) {
    return reg.code() - 2;  // zero_reg and 'at' are skipped.
  }

  static Register FromAllocationIndex(int index) {
    ASSERT(index >= 0 && index < kNumAllocatableRegisters);
    return from_code(index + 2);  // zero_reg and 'at' are skipped.
  }

  static const char* AllocationIndexToString(int index) {
    ASSERT(index >= 0 && index < kNumAllocatableRegisters);
    const char* const names[] = {
      "v0",
      "v1",
      "a0",
      "a1",
      "a2",
      "a3",
      "t0",
      "t1",
      "t2",
      "t3",
      "t4",
      "t5",
      "t6",
      "t7",
    };
    return names[index];
  }

  static Register from_code(int code) {
    Register r = { code };
    return r;
  }

  bool is_valid() const { return 0 <= code_ && code_ < kNumRegisters; }
  bool is(Register reg) const { return code_ == reg.code_; }
  int code() const {
    ASSERT(is_valid());
    return code_;
  }
  int bit() const {
    ASSERT(is_valid());
    return 1 << code_;
  }

  // Unfortunately we can't make this private in a struct.
  int code_;
};

const Register no_reg = { -1 };

const Register zero_reg = { 0 };
const Register at = { 1 };
const Register v0 = { 2 };
const Register v1 = { 3 };
const Register a0 = { 4 };
const Register a1 = { 5 };
const Register a2 = { 6 };
const Register a3 = { 7 };
const Register t0 = { 8 };
const Register t1 = { 9 };
const Register t2 = { 10 };
const Register t3 = { 11 };
const Register t4 = { 12 };
const Register t5 = { 13 };
const Register t6 = { 14 };
const Register t7 = { 15 };
const Register s0 = { 16 };
const Register s1 = { 17 };
const Register s2 = { 18 };
const Register s3 = { 19 };
const Register s4 = { 20 };
const Register s5 = { 21 };
const Register s6 = { 22 };
const Register s7 = { 23 };
const Register t8 = { 24 };
const Register t9 = { 25 };
const Register k0 = { 26 };
const Register k1 = { 27 };
const Register gp = { 28 };
const Register sp = { 29 };
const Register s8_fp = { 30 };
const Register ra = { 31 };


int ToNumber(Register reg);

Register ToRegister(int num);

// Coprocessor register.
struct FPURegister {
  static const int kNumRegisters = v8::internal::kNumFPURegisters;
  // f0 has been excluded from allocation. This is following ia32
  // where xmm0 is excluded.
  static const int kNumAllocatableRegisters = 15;

  static int ToAllocationIndex(FPURegister reg) {
    ASSERT(reg.code() != 0);
    ASSERT(reg.code() % 2 == 0);
    return (reg.code() / 2) - 1;
  }

  static FPURegister FromAllocationIndex(int index) {
    ASSERT(index >= 0 && index < kNumAllocatableRegisters);
    return from_code((index + 1) * 2);
  }

  static const char* AllocationIndexToString(int index) {
    ASSERT(index >= 0 && index < kNumAllocatableRegisters);
    const char* const names[] = {
      "f2",
      "f4",
      "f6",
      "f8",
      "f10",
      "f12",
      "f14",
      "f16",
      "f18",
      "f20",
      "f22",
      "f24",
      "f26",
      "f28",
      "f30"
    };
    return names[index];
  }

  static FPURegister from_code(int code) {
    FPURegister r = { code };
    return r;
  }

  bool is_valid() const { return 0 <= code_ && code_ < kNumFPURegisters ; }
  bool is(FPURegister creg) const { return code_ == creg.code_; }
  int code() const {
    ASSERT(is_valid());
    return code_;
  }
  int bit() const {
    ASSERT(is_valid());
    return 1 << code_;
  }
  void setcode(int f) {
    code_ = f;
    ASSERT(is_valid());
  }
  // Unfortunately we can't make this private in a struct.
  int code_;
};

typedef FPURegister DoubleRegister;

const FPURegister no_creg = { -1 };

const FPURegister f0 = { 0 };  // Return value in hard float mode.
const FPURegister f1 = { 1 };
const FPURegister f2 = { 2 };
const FPURegister f3 = { 3 };
const FPURegister f4 = { 4 };
const FPURegister f5 = { 5 };
const FPURegister f6 = { 6 };
const FPURegister f7 = { 7 };
const FPURegister f8 = { 8 };
const FPURegister f9 = { 9 };
const FPURegister f10 = { 10 };
const FPURegister f11 = { 11 };
const FPURegister f12 = { 12 };  // Arg 0 in hard float mode.
const FPURegister f13 = { 13 };
const FPURegister f14 = { 14 };  // Arg 1 in hard float mode.
const FPURegister f15 = { 15 };
const FPURegister f16 = { 16 };
const FPURegister f17 = { 17 };
const FPURegister f18 = { 18 };
const FPURegister f19 = { 19 };
const FPURegister f20 = { 20 };
const FPURegister f21 = { 21 };
const FPURegister f22 = { 22 };
const FPURegister f23 = { 23 };
const FPURegister f24 = { 24 };
const FPURegister f25 = { 25 };
const FPURegister f26 = { 26 };
const FPURegister f27 = { 27 };
const FPURegister f28 = { 28 };
const FPURegister f29 = { 29 };
const FPURegister f30 = { 30 };
const FPURegister f31 = { 31 };

// FPU (coprocessor 1) control registers.
// Currently only FCSR (#31) is implemented.
struct FPUControlRegister {
  static const int kFCSRRegister = 31;
  static const int kInvalidFPUControlRegister = -1;

  bool is_valid() const { return code_ == kFCSRRegister; }
  bool is(FPUControlRegister creg) const { return code_ == creg.code_; }
  int code() const {
    ASSERT(is_valid());
    return code_;
  }
  int bit() const {
    ASSERT(is_valid());
    return 1 << code_;
  }
  void setcode(int f) {
    code_ = f;
    ASSERT(is_valid());
  }
  // Unfortunately we can't make this private in a struct.
  int code_;
};

const FPUControlRegister no_fpucreg = { -1 };
const FPUControlRegister FCSR = { kFCSRRegister };


// -----------------------------------------------------------------------------
// Machine instruction Operands.

// Class Operand represents a shifter operand in data processing instructions.
class Operand BASE_EMBEDDED {
 public:
  // Immediate.
  INLINE(explicit Operand(int32_t immediate,
         RelocInfo::Mode rmode = RelocInfo::NONE));
  INLINE(explicit Operand(const ExternalReference& f));
  INLINE(explicit Operand(const char* s));
  INLINE(explicit Operand(Object** opp));
  INLINE(explicit Operand(Context** cpp));
  explicit Operand(Handle<Object> handle);
  INLINE(explicit Operand(Smi* value));

  // Register.
  INLINE(explicit Operand(Register rm));

  // Return true if this is a register operand.
  INLINE(bool is_reg() const);

  Register rm() const { return rm_; }

 private:
  Register rm_;
  int32_t imm32_;  // Valid if rm_ == no_reg
  RelocInfo::Mode rmode_;

  friend class Assembler;
  friend class MacroAssembler;
};


// On MIPS we have only one adressing mode with base_reg + offset.
// Class MemOperand represents a memory operand in load and store instructions.
class MemOperand : public Operand {
 public:

  explicit MemOperand(Register rn, int32_t offset = 0);

 private:
  int32_t offset_;

  friend class Assembler;
};


// CpuFeatures keeps track of which features are supported by the target CPU.
// Supported features must be enabled by a Scope before use.
class CpuFeatures {
 public:
  // Detect features of the target CPU. Set safe defaults if the serializer
  // is enabled (snapshots must be portable).
  void Probe(bool portable);

  // Check whether a feature is supported by the target CPU.
  bool IsSupported(CpuFeature f) const {
    if (f == FPU && !FLAG_enable_fpu) return false;
    return (supported_ & (1u << f)) != 0;
  }

  // Check whether a feature is currently enabled.
  bool IsEnabled(CpuFeature f) const {
    return (enabled_ & (1u << f)) != 0;
  }

  // Enable a specified feature within a scope.
  class Scope BASE_EMBEDDED {
#ifdef DEBUG
   public:
    explicit Scope(CpuFeature f)
        : cpu_features_(Isolate::Current()->cpu_features()),
          isolate_(Isolate::Current()) {
      ASSERT(cpu_features_->IsSupported(f));
      ASSERT(!Serializer::enabled() ||
             (cpu_features_->found_by_runtime_probing_ & (1u << f)) == 0);
      old_enabled_ = cpu_features_->enabled_;
      cpu_features_->enabled_ |= 1u << f;
    }
    ~Scope() {
      ASSERT_EQ(Isolate::Current(), isolate_);
      cpu_features_->enabled_ = old_enabled_;
     }
   private:
    unsigned old_enabled_;
    CpuFeatures* cpu_features_;
    Isolate* isolate_;
#else
   public:
    explicit Scope(CpuFeature f) {}
#endif
  };

 private:
  CpuFeatures();

  unsigned supported_;
  unsigned enabled_;
  unsigned found_by_runtime_probing_;

  friend class Isolate;

  DISALLOW_COPY_AND_ASSIGN(CpuFeatures);
};


class Assembler : public AssemblerBase {
 public:
  // Create an assembler. Instructions and relocation information are emitted
  // into a buffer, with the instructions starting from the beginning and the
  // relocation information starting from the end of the buffer. See CodeDesc
  // for a detailed comment on the layout (globals.h).
  //
  // If the provided buffer is NULL, the assembler allocates and grows its own
  // buffer, and buffer_size determines the initial buffer size. The buffer is
  // owned by the assembler and deallocated upon destruction of the assembler.
  //
  // If the provided buffer is not NULL, the assembler uses the provided buffer
  // for code generation and assumes its size to be buffer_size. If the buffer
  // is too small, a fatal error occurs. No deallocation of the buffer is done
  // upon destruction of the assembler.
  Assembler(void* buffer, int buffer_size);
  ~Assembler();

  // Overrides the default provided by FLAG_debug_code.
  void set_emit_debug_code(bool value) { emit_debug_code_ = value; }

  // GetCode emits any pending (non-emitted) code and fills the descriptor
  // desc. GetCode() is idempotent; it returns the same result if no other
  // Assembler functions are invoked in between GetCode() calls.
  void GetCode(CodeDesc* desc);

  // Label operations & relative jumps (PPUM Appendix D).
  //
  // Takes a branch opcode (cc) and a label (L) and generates
  // either a backward branch or a forward branch and links it
  // to the label fixup chain. Usage:
  //
  // Label L;    // unbound label
  // j(cc, &L);  // forward branch to unbound label
  // bind(&L);   // bind label to the current pc
  // j(cc, &L);  // backward branch to bound label
  // bind(&L);   // illegal: a label may be bound only once
  //
  // Note: The same Label can be used for forward and backward branches
  // but it may be bound only once.
  void bind(Label* L);  // binds an unbound label L to the current code position

  // Returns the branch offset to the given label from the current code position
  // Links the label to the current position if it is still unbound
  // Manages the jump elimination optimization if the second parameter is true.
  int32_t branch_offset(Label* L, bool jump_elimination_allowed);
  int32_t shifted_branch_offset(Label* L, bool jump_elimination_allowed) {
    int32_t o = branch_offset(L, jump_elimination_allowed);
    ASSERT((o & 3) == 0);   // Assert the offset is aligned.
    return o >> 2;
  }

  // Puts a labels target address at the given position.
  // The high 8 bits are set to zero.
  void label_at_put(Label* L, int at_offset);

  // Read/Modify the code target address in the branch/call instruction at pc.
  static Address target_address_at(Address pc);
  static void set_target_address_at(Address pc, Address target);

  // This sets the branch destination (which gets loaded at the call address).
  // This is for calls and branches within generated code.
  inline static void set_target_at(Address instruction_payload,
                                   Address target) {
    set_target_address_at(instruction_payload, target);
  }

  // This sets the branch destination.
  // This is for calls and branches to runtime code.
  inline static void set_external_target_at(Address instruction_payload,
                                            Address target) {
    set_target_address_at(instruction_payload, target);
  }

  // Size of an instruction.
  static const int kInstrSize = sizeof(Instr);

  // Difference between address of current opcode and target address offset.
  static const int kBranchPCOffset = 4;

  // Here we are patching the address in the LUI/ORI instruction pair.
  // These values are used in the serialization process and must be zero for
  // MIPS platform, as Code, Embedded Object or External-reference pointers
  // are split across two consecutive instructions and don't exist separately
  // in the code, so the serializer should not step forwards in memory after
  // a target is resolved and written.
  static const int kCallTargetSize = 0 * kInstrSize;
  static const int kExternalTargetSize = 0 * kInstrSize;

  // Number of consecutive instructions used to store 32bit constant.
  // Used in RelocInfo::target_address_address() function to tell serializer
  // address of the instruction that follows LUI/ORI instruction pair.
  static const int kInstructionsFor32BitConstant = 2;

  // Distance between the instruction referring to the address of the call
  // target and the return address.
  static const int kCallTargetAddressOffset = 4 * kInstrSize;

  // Distance between start of patched return sequence and the emitted address
  // to jump to.
  static const int kPatchReturnSequenceAddressOffset = 0;

  // Distance between start of patched debug break slot and the emitted address
  // to jump to.
  static const int kPatchDebugBreakSlotAddressOffset =  0 * kInstrSize;

  // Difference between address of current opcode and value read from pc
  // register.
  static const int kPcLoadDelta = 4;

  // Number of instructions used for the JS return sequence. The constant is
  // used by the debugger to patch the JS return sequence.
  static const int kJSReturnSequenceInstructions = 7;
  static const int kDebugBreakSlotInstructions = 4;
  static const int kDebugBreakSlotLength =
      kDebugBreakSlotInstructions * kInstrSize;


  // ---------------------------------------------------------------------------
  // Code generation.

  // Insert the smallest number of nop instructions
  // possible to align the pc offset to a multiple
  // of m. m must be a power of 2 (>= 4).
  void Align(int m);
  // Aligns code to something that's optimal for a jump target for the platform.
  void CodeTargetAlign();

  // Different nop operations are used by the code generator to detect certain
  // states of the generated code.
  enum NopMarkerTypes {
    NON_MARKING_NOP = 0,
    DEBUG_BREAK_NOP,
    // IC markers.
    PROPERTY_ACCESS_INLINED,
    PROPERTY_ACCESS_INLINED_CONTEXT,
    PROPERTY_ACCESS_INLINED_CONTEXT_DONT_DELETE,
    // Helper values.
    LAST_CODE_MARKER,
    FIRST_IC_MARKER = PROPERTY_ACCESS_INLINED
  };

  // type == 0 is the default non-marking type.
  void nop(unsigned int type = 0) {
    ASSERT(type < 32);
    sll(zero_reg, zero_reg, type, true);
  }


  //------- Branch and jump  instructions --------
  // We don't use likely variant of instructions.
  void b(int16_t offset);
  void b(Label* L) { b(branch_offset(L, false)>>2); }
  void bal(int16_t offset);
  void bal(Label* L) { bal(branch_offset(L, false)>>2); }

  void beq(Register rs, Register rt, int16_t offset);
  void beq(Register rs, Register rt, Label* L) {
    beq(rs, rt, branch_offset(L, false) >> 2);
  }
  void bgez(Register rs, int16_t offset);
  void bgezal(Register rs, int16_t offset);
  void bgtz(Register rs, int16_t offset);
  void blez(Register rs, int16_t offset);
  void bltz(Register rs, int16_t offset);
  void bltzal(Register rs, int16_t offset);
  void bne(Register rs, Register rt, int16_t offset);
  void bne(Register rs, Register rt, Label* L) {
    bne(rs, rt, branch_offset(L, false)>>2);
  }

  // Never use the int16_t b(l)cond version with a branch offset
  // instead of using the Label* version. See Twiki for infos.

  // Jump targets must be in the current 256 MB-aligned region. ie 28 bits.
  void j(int32_t target);
  void jal(int32_t target);
  void jalr(Register rs, Register rd = ra);
  void jr(Register target);


  //-------Data-processing-instructions---------

  // Arithmetic.
  void addu(Register rd, Register rs, Register rt);
  void subu(Register rd, Register rs, Register rt);
  void mult(Register rs, Register rt);
  void multu(Register rs, Register rt);
  void div(Register rs, Register rt);
  void divu(Register rs, Register rt);
  void mul(Register rd, Register rs, Register rt);

  void addiu(Register rd, Register rs, int32_t j);

  // Logical.
  void and_(Register rd, Register rs, Register rt);
  void or_(Register rd, Register rs, Register rt);
  void xor_(Register rd, Register rs, Register rt);
  void nor(Register rd, Register rs, Register rt);

  void andi(Register rd, Register rs, int32_t j);
  void ori(Register rd, Register rs, int32_t j);
  void xori(Register rd, Register rs, int32_t j);
  void lui(Register rd, int32_t j);

  // Shifts.
  // Please note: sll(zero_reg, zero_reg, x) instructions are reserved as nop
  // and may cause problems in normal code. coming_from_nop makes sure this
  // doesn't happen.
  void sll(Register rd, Register rt, uint16_t sa, bool coming_from_nop = false);
  void sllv(Register rd, Register rt, Register rs);
  void srl(Register rd, Register rt, uint16_t sa);
  void srlv(Register rd, Register rt, Register rs);
  void sra(Register rt, Register rd, uint16_t sa);
  void srav(Register rt, Register rd, Register rs);
  void rotr(Register rd, Register rt, uint16_t sa);
  void rotrv(Register rd, Register rt, Register rs);


  //------------Memory-instructions-------------

  void lb(Register rd, const MemOperand& rs);
  void lbu(Register rd, const MemOperand& rs);
  void lh(Register rd, const MemOperand& rs);
  void lhu(Register rd, const MemOperand& rs);
  void lw(Register rd, const MemOperand& rs);
  void lwl(Register rd, const MemOperand& rs);
  void lwr(Register rd, const MemOperand& rs);
  void sb(Register rd, const MemOperand& rs);
  void sh(Register rd, const MemOperand& rs);
  void sw(Register rd, const MemOperand& rs);
  void swl(Register rd, const MemOperand& rs);
  void swr(Register rd, const MemOperand& rs);


  //-------------Misc-instructions--------------

  // Break / Trap instructions.
  void break_(uint32_t code);
  void tge(Register rs, Register rt, uint16_t code);
  void tgeu(Register rs, Register rt, uint16_t code);
  void tlt(Register rs, Register rt, uint16_t code);
  void tltu(Register rs, Register rt, uint16_t code);
  void teq(Register rs, Register rt, uint16_t code);
  void tne(Register rs, Register rt, uint16_t code);

  // Move from HI/LO register.
  void mfhi(Register rd);
  void mflo(Register rd);

  // Set on less than.
  void slt(Register rd, Register rs, Register rt);
  void sltu(Register rd, Register rs, Register rt);
  void slti(Register rd, Register rs, int32_t j);
  void sltiu(Register rd, Register rs, int32_t j);

  // Conditional move.
  void movz(Register rd, Register rs, Register rt);
  void movn(Register rd, Register rs, Register rt);
  void movt(Register rd, Register rs, uint16_t cc = 0);
  void movf(Register rd, Register rs, uint16_t cc = 0);

  // Bit twiddling.
  void clz(Register rd, Register rs);
  void ins_(Register rt, Register rs, uint16_t pos, uint16_t size);
  void ext_(Register rt, Register rs, uint16_t pos, uint16_t size);

  //--------Coprocessor-instructions----------------

  // Load, store, and move.
  void lwc1(FPURegister fd, const MemOperand& src);
  void ldc1(FPURegister fd, const MemOperand& src);

  void swc1(FPURegister fs, const MemOperand& dst);
  void sdc1(FPURegister fs, const MemOperand& dst);

  void mtc1(Register rt, FPURegister fs);
  void mfc1(Register rt, FPURegister fs);

  void ctc1(Register rt, FPUControlRegister fs);
  void cfc1(Register rt, FPUControlRegister fs);

  // Arithmetic.
  void add_d(FPURegister fd, FPURegister fs, FPURegister ft);
  void sub_d(FPURegister fd, FPURegister fs, FPURegister ft);
  void mul_d(FPURegister fd, FPURegister fs, FPURegister ft);
  void div_d(FPURegister fd, FPURegister fs, FPURegister ft);
  void abs_d(FPURegister fd, FPURegister fs);
  void mov_d(FPURegister fd, FPURegister fs);
  void neg_d(FPURegister fd, FPURegister fs);
  void sqrt_d(FPURegister fd, FPURegister fs);

  // Conversion.
  void cvt_w_s(FPURegister fd, FPURegister fs);
  void cvt_w_d(FPURegister fd, FPURegister fs);
  void trunc_w_s(FPURegister fd, FPURegister fs);
  void trunc_w_d(FPURegister fd, FPURegister fs);
  void round_w_s(FPURegister fd, FPURegister fs);
  void round_w_d(FPURegister fd, FPURegister fs);
  void floor_w_s(FPURegister fd, FPURegister fs);
  void floor_w_d(FPURegister fd, FPURegister fs);
  void ceil_w_s(FPURegister fd, FPURegister fs);
  void ceil_w_d(FPURegister fd, FPURegister fs);

  void cvt_l_s(FPURegister fd, FPURegister fs);
  void cvt_l_d(FPURegister fd, FPURegister fs);
  void trunc_l_s(FPURegister fd, FPURegister fs);
  void trunc_l_d(FPURegister fd, FPURegister fs);
  void round_l_s(FPURegister fd, FPURegister fs);
  void round_l_d(FPURegister fd, FPURegister fs);
  void floor_l_s(FPURegister fd, FPURegister fs);
  void floor_l_d(FPURegister fd, FPURegister fs);
  void ceil_l_s(FPURegister fd, FPURegister fs);
  void ceil_l_d(FPURegister fd, FPURegister fs);

  void cvt_s_w(FPURegister fd, FPURegister fs);
  void cvt_s_l(FPURegister fd, FPURegister fs);
  void cvt_s_d(FPURegister fd, FPURegister fs);

  void cvt_d_w(FPURegister fd, FPURegister fs);
  void cvt_d_l(FPURegister fd, FPURegister fs);
  void cvt_d_s(FPURegister fd, FPURegister fs);

  // Conditions and branches.
  void c(FPUCondition cond, SecondaryField fmt,
         FPURegister ft, FPURegister fs, uint16_t cc = 0);

  void bc1f(int16_t offset, uint16_t cc = 0);
  void bc1f(Label* L, uint16_t cc = 0) { bc1f(branch_offset(L, false)>>2, cc); }
  void bc1t(int16_t offset, uint16_t cc = 0);
  void bc1t(Label* L, uint16_t cc = 0) { bc1t(branch_offset(L, false)>>2, cc); }
  void fcmp(FPURegister src1, const double src2, FPUCondition cond);

  // Check the code size generated from label to here.
  int InstructionsGeneratedSince(Label* l) {
    return (pc_offset() - l->pos()) / kInstrSize;
  }

  // Class for scoping postponing the trampoline pool generation.
  class BlockTrampolinePoolScope {
   public:
    explicit BlockTrampolinePoolScope(Assembler* assem) : assem_(assem) {
      assem_->StartBlockTrampolinePool();
    }
    ~BlockTrampolinePoolScope() {
      assem_->EndBlockTrampolinePool();
    }

   private:
    Assembler* assem_;

    DISALLOW_IMPLICIT_CONSTRUCTORS(BlockTrampolinePoolScope);
  };

  // Debugging.

  // Mark address of the ExitJSFrame code.
  void RecordJSReturn();

  // Mark address of a debug break slot.
  void RecordDebugBreakSlot();

  // Record a comment relocation entry that can be used by a disassembler.
  // Use --code-comments to enable.
  void RecordComment(const char* msg);

  // Writes a single byte or word of data in the code stream.  Used for
  // inline tables, e.g., jump-tables.
  void db(uint8_t data);
  void dd(uint32_t data);

  int32_t pc_offset() const { return pc_ - buffer_; }

  PositionsRecorder* positions_recorder() { return &positions_recorder_; }

  bool can_peephole_optimize(int instructions) {
    if (!allow_peephole_optimization_) return false;
    if (last_bound_pos_ > pc_offset() - instructions * kInstrSize) return false;
    return reloc_info_writer.last_pc() <= pc_ - instructions * kInstrSize;
  }

  // Postpone the generation of the trampoline pool for the specified number of
  // instructions.
  void BlockTrampolinePoolFor(int instructions);

  // Check if there is less than kGap bytes available in the buffer.
  // If this is the case, we need to grow the buffer before emitting
  // an instruction or relocation information.
  inline bool overflow() const { return pc_ >= reloc_info_writer.pos() - kGap; }

  // Get the number of bytes available in the buffer.
  inline int available_space() const { return reloc_info_writer.pos() - pc_; }

  // Read/patch instructions.
  static Instr instr_at(byte* pc) { return *reinterpret_cast<Instr*>(pc); }
  static void instr_at_put(byte* pc, Instr instr) {
    *reinterpret_cast<Instr*>(pc) = instr;
  }
  Instr instr_at(int pos) { return *reinterpret_cast<Instr*>(buffer_ + pos); }
  void instr_at_put(int pos, Instr instr) {
    *reinterpret_cast<Instr*>(buffer_ + pos) = instr;
  }

  // Check if an instruction is a branch of some kind.
  static bool IsBranch(Instr instr);

  static bool IsNop(Instr instr, unsigned int type);
  static bool IsPop(Instr instr);
  static bool IsPush(Instr instr);
  static bool IsLwRegFpOffset(Instr instr);
  static bool IsSwRegFpOffset(Instr instr);
  static bool IsLwRegFpNegOffset(Instr instr);
  static bool IsSwRegFpNegOffset(Instr instr);

  static Register GetRt(Instr instr);

  static int32_t GetBranchOffset(Instr instr);
  static bool IsLw(Instr instr);
  static int16_t GetLwOffset(Instr instr);
  static Instr SetLwOffset(Instr instr, int16_t offset);

  static bool IsSw(Instr instr);
  static Instr SetSwOffset(Instr instr, int16_t offset);
  static bool IsAddImmediate(Instr instr);
  static Instr SetAddImmediateOffset(Instr instr, int16_t offset);

  void CheckTrampolinePool(bool force_emit = false);

 protected:
  bool emit_debug_code() const { return emit_debug_code_; }

  int32_t buffer_space() const { return reloc_info_writer.pos() - pc_; }

  // Decode branch instruction at pos and return branch target pos.
  int target_at(int32_t pos);

  // Patch branch instruction at pos to branch to given branch target pos.
  void target_at_put(int32_t pos, int32_t target_pos);

  // Say if we need to relocate with this mode.
  bool MustUseReg(RelocInfo::Mode rmode);

  // Record reloc info for current pc_.
  void RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data = 0);

  // Block the emission of the trampoline pool before pc_offset.
  void BlockTrampolinePoolBefore(int pc_offset) {
    if (no_trampoline_pool_before_ < pc_offset)
      no_trampoline_pool_before_ = pc_offset;
  }

  void StartBlockTrampolinePool() {
    trampoline_pool_blocked_nesting_++;
  }
  void EndBlockTrampolinePool() {
    trampoline_pool_blocked_nesting_--;
  }

  bool is_trampoline_pool_blocked() const {
    return trampoline_pool_blocked_nesting_ > 0;
  }

 private:
  // Code buffer:
  // The buffer into which code and relocation info are generated.
  byte* buffer_;
  int buffer_size_;
  // True if the assembler owns the buffer, false if buffer is external.
  bool own_buffer_;

  // Buffer size and constant pool distance are checked together at regular
  // intervals of kBufferCheckInterval emitted bytes.
  static const int kBufferCheckInterval = 1*KB/2;

  // Code generation.
  // The relocation writer's position is at least kGap bytes below the end of
  // the generated instructions. This is so that multi-instruction sequences do
  // not have to check for overflow. The same is true for writes of large
  // relocation info entries.
  static const int kGap = 32;
  byte* pc_;  // The program counter - moves forward.


  // Repeated checking whether the trampoline pool should be emitted is rather
  // expensive. By default we only check again once a number of instructions
  // has been generated.
  static const int kCheckConstIntervalInst = 32;
  static const int kCheckConstInterval = kCheckConstIntervalInst * kInstrSize;

  int next_buffer_check_;  // pc offset of next buffer check.

  // Emission of the trampoline pool may be blocked in some code sequences.
  int trampoline_pool_blocked_nesting_;  // Block emission if this is not zero.
  int no_trampoline_pool_before_;  // Block emission before this pc offset.

  // Keep track of the last emitted pool to guarantee a maximal distance.
  int last_trampoline_pool_end_;  // pc offset of the end of the last pool.

  // Relocation information generation.
  // Each relocation is encoded as a variable size value.
  static const int kMaxRelocSize = RelocInfoWriter::kMaxSize;
  RelocInfoWriter reloc_info_writer;

  // The bound position, before this we cannot do instruction elimination.
  int last_bound_pos_;

  // Code emission.
  inline void CheckBuffer();
  void GrowBuffer();
  inline void emit(Instr x);
  inline void CheckTrampolinePoolQuick();

  // Instruction generation.
  // We have 3 different kind of encoding layout on MIPS.
  // However due to many different types of objects encoded in the same fields
  // we have quite a few aliases for each mode.
  // Using the same structure to refer to Register and FPURegister would spare a
  // few aliases, but mixing both does not look clean to me.
  // Anyway we could surely implement this differently.

  void GenInstrRegister(Opcode opcode,
                        Register rs,
                        Register rt,
                        Register rd,
                        uint16_t sa = 0,
                        SecondaryField func = NULLSF);

  void GenInstrRegister(Opcode opcode,
                        Register rs,
                        Register rt,
                        uint16_t msb,
                        uint16_t lsb,
                        SecondaryField func);

  void GenInstrRegister(Opcode opcode,
                        SecondaryField fmt,
                        FPURegister ft,
                        FPURegister fs,
                        FPURegister fd,
                        SecondaryField func = NULLSF);

  void GenInstrRegister(Opcode opcode,
                        SecondaryField fmt,
                        Register rt,
                        FPURegister fs,
                        FPURegister fd,
                        SecondaryField func = NULLSF);

  void GenInstrRegister(Opcode opcode,
                        SecondaryField fmt,
                        Register rt,
                        FPUControlRegister fs,
                        SecondaryField func = NULLSF);


  void GenInstrImmediate(Opcode opcode,
                         Register rs,
                         Register rt,
                         int32_t  j);
  void GenInstrImmediate(Opcode opcode,
                         Register rs,
                         SecondaryField SF,
                         int32_t  j);
  void GenInstrImmediate(Opcode opcode,
                         Register r1,
                         FPURegister r2,
                         int32_t  j);


  void GenInstrJump(Opcode opcode,
                     uint32_t address);

  // Helpers.
  void LoadRegPlusOffsetToAt(const MemOperand& src);

  // Labels.
  void print(Label* L);
  void bind_to(Label* L, int pos);
  void link_to(Label* L, Label* appendix);
  void next(Label* L);

  // One trampoline consists of:
  // - space for trampoline slots,
  // - space for labels.
  //
  // Space for trampoline slots is equal to slot_count * 2 * kInstrSize.
  // Space for trampoline slots preceeds space for labels. Each label is of one
  // instruction size, so total amount for labels is equal to
  // label_count *  kInstrSize.
  class Trampoline {
   public:
    Trampoline(int start, int slot_count, int label_count) {
      start_ = start;
      next_slot_ = start;
      free_slot_count_ = slot_count;
      next_label_ = start + slot_count * 2 * kInstrSize;
      free_label_count_ = label_count;
      end_ = next_label_ + (label_count - 1) * kInstrSize;
    }
    int start() {
      return start_;
    }
    int end() {
      return end_;
    }
    int take_slot() {
      int trampoline_slot = next_slot_;
      ASSERT(free_slot_count_ > 0);
      free_slot_count_--;
      next_slot_ += 2 * kInstrSize;
      return trampoline_slot;
    }
    int take_label() {
      int label_pos = next_label_;
      ASSERT(free_label_count_ > 0);
      free_label_count_--;
      next_label_ += kInstrSize;
      return label_pos;
    }
   private:
    int start_;
    int end_;
    int next_slot_;
    int free_slot_count_;
    int next_label_;
    int free_label_count_;
  };

  int32_t get_label_entry(int32_t pos, bool next_pool = true);
  int32_t get_trampoline_entry(int32_t pos, bool next_pool = true);

  static const int kSlotsPerTrampoline = 2304;
  static const int kLabelsPerTrampoline = 8;
  static const int kTrampolineInst =
      2 * kSlotsPerTrampoline + kLabelsPerTrampoline;
  static const int kTrampolineSize = kTrampolineInst * kInstrSize;
  static const int kMaxBranchOffset = (1 << (18 - 1)) - 1;
  static const int kMaxDistBetweenPools =
      kMaxBranchOffset - 2 * kTrampolineSize;

  List<Trampoline> trampolines_;

  friend class RegExpMacroAssemblerMIPS;
  friend class RelocInfo;
  friend class CodePatcher;
  friend class BlockTrampolinePoolScope;

  PositionsRecorder positions_recorder_;
  bool allow_peephole_optimization_;
  bool emit_debug_code_;
  friend class PositionsRecorder;
  friend class EnsureSpace;
};


class EnsureSpace BASE_EMBEDDED {
 public:
  explicit EnsureSpace(Assembler* assembler) {
    assembler->CheckBuffer();
  }
};

} }  // namespace v8::internal

#endif  // V8_ARM_ASSEMBLER_MIPS_H_
