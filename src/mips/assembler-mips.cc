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


#include "v8.h"

#if defined(V8_TARGET_ARCH_MIPS)

#include "mips/assembler-mips-inl.h"
#include "serialize.h"

namespace v8 {
namespace internal {

CpuFeatures::CpuFeatures()
    : supported_(0),
      enabled_(0),
      found_by_runtime_probing_(0) {
}

void CpuFeatures::Probe(bool portable) {
  // If the compiler is allowed to use fpu then we can use fpu too in our
  // code generation.
#if !defined(__mips__)
  // For the simulator=mips build, use FPU when FLAG_enable_fpu is enabled.
  if (FLAG_enable_fpu) {
      supported_ |= 1u << FPU;
  }
#else
  if (portable && Serializer::enabled()) {
    supported_ |= OS::CpuFeaturesImpliedByPlatform();
    return;  // No features if we might serialize.
  }

  if (OS::MipsCpuHasFeature(FPU)) {
    // This implementation also sets the FPU flags if
    // runtime detection of FPU returns true.
    supported_ |= 1u << FPU;
    found_by_runtime_probing_ |= 1u << FPU;
  }

  if (!portable) found_by_runtime_probing_ = 0;
#endif
}


int ToNumber(Register reg) {
  ASSERT(reg.is_valid());
  const int kNumbers[] = {
    0,    // zero_reg
    1,    // at
    2,    // v0
    3,    // v1
    4,    // a0
    5,    // a1
    6,    // a2
    7,    // a3
    8,    // t0
    9,    // t1
    10,   // t2
    11,   // t3
    12,   // t4
    13,   // t5
    14,   // t6
    15,   // t7
    16,   // s0
    17,   // s1
    18,   // s2
    19,   // s3
    20,   // s4
    21,   // s5
    22,   // s6
    23,   // s7
    24,   // t8
    25,   // t9
    26,   // k0
    27,   // k1
    28,   // gp
    29,   // sp
    30,   // s8_fp
    31,   // ra
  };
  return kNumbers[reg.code()];
}


Register ToRegister(int num) {
  ASSERT(num >= 0 && num < kNumRegisters);
  const Register kRegisters[] = {
    zero_reg,
    at,
    v0, v1,
    a0, a1, a2, a3,
    t0, t1, t2, t3, t4, t5, t6, t7,
    s0, s1, s2, s3, s4, s5, s6, s7,
    t8, t9,
    k0, k1,
    gp,
    sp,
    s8_fp,
    ra
  };
  return kRegisters[num];
}


// -----------------------------------------------------------------------------
// Implementation of RelocInfo.

const int RelocInfo::kApplyMask = 0;


bool RelocInfo::IsCodedSpecially() {
  // The deserializer needs to know whether a pointer is specially coded.  Being
  // specially coded on MIPS means that it is a lui/ori instruction, and that is
  // always the case inside code objects.
  return true;
}


// Patch the code at the current address with the supplied instructions.
void RelocInfo::PatchCode(byte* instructions, int instruction_count) {
  Instr* pc = reinterpret_cast<Instr*>(pc_);
  Instr* instr = reinterpret_cast<Instr*>(instructions);
  for (int i = 0; i < instruction_count; i++) {
    *(pc + i) = *(instr + i);
  }

  // Indicate that code has changed.
  CPU::FlushICache(pc_, instruction_count * Assembler::kInstrSize);
}


// Patch the code at the current PC with a call to the target address.
// Additional guard instructions can be added if required.
void RelocInfo::PatchCodeWithCall(Address target, int guard_bytes) {
  // Patch the code at the current address with a call to the target.
  UNIMPLEMENTED_MIPS();
}


// -----------------------------------------------------------------------------
// Implementation of Operand and MemOperand.
// See assembler-mips-inl.h for inlined constructors.

Operand::Operand(Handle<Object> handle) {
  rm_ = no_reg;
  // Verify all Objects referred by code are NOT in new space.
  Object* obj = *handle;
  ASSERT(!HEAP->InNewSpace(obj));
  if (obj->IsHeapObject()) {
    imm32_ = reinterpret_cast<intptr_t>(handle.location());
    rmode_ = RelocInfo::EMBEDDED_OBJECT;
  } else {
    // No relocation needed.
    imm32_ = reinterpret_cast<intptr_t>(obj);
    rmode_ = RelocInfo::NONE;
  }
}


MemOperand::MemOperand(Register rm, int32_t offset) : Operand(rm) {
  offset_ = offset;
}


// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.

static const int kNegOffset = 0x00008000;
// addiu(sp, sp, 4) aka Pop() operation or part of Pop(r)
// operations as post-increment of sp.
const Instr kPopInstruction = ADDIU | (sp.code() << kRsShift)
      | (sp.code() << kRtShift) | (kPointerSize & kImm16Mask);
// addiu(sp, sp, -4) part of Push(r) operation as pre-decrement of sp.
const Instr kPushInstruction = ADDIU | (sp.code() << kRsShift)
      | (sp.code() << kRtShift) | (-kPointerSize & kImm16Mask);
// sw(r, MemOperand(sp, 0))
const Instr kPushRegPattern = SW | (sp.code() << kRsShift)
      |  (0 & kImm16Mask);
//  lw(r, MemOperand(sp, 0))
const Instr kPopRegPattern = LW | (sp.code() << kRsShift)
      |  (0 & kImm16Mask);

const Instr kLwRegFpOffsetPattern = LW | (s8_fp.code() << kRsShift)
      |  (0 & kImm16Mask);

const Instr kSwRegFpOffsetPattern = SW | (s8_fp.code() << kRsShift)
      |  (0 & kImm16Mask);

const Instr kLwRegFpNegOffsetPattern = LW | (s8_fp.code() << kRsShift)
      |  (kNegOffset & kImm16Mask);

const Instr kSwRegFpNegOffsetPattern = SW | (s8_fp.code() << kRsShift)
      |  (kNegOffset & kImm16Mask);
// A mask for the Rt register for push, pop, lw, sw instructions.
const Instr kRtMask = kRtFieldMask;
const Instr kLwSwInstrTypeMask = 0xffe00000;
const Instr kLwSwInstrArgumentMask  = ~kLwSwInstrTypeMask;
const Instr kLwSwOffsetMask = kImm16Mask;


// Spare buffer.
static const int kMinimalBufferSize = 4 * KB;


Assembler::Assembler(void* buffer, int buffer_size)
    : AssemblerBase(Isolate::Current()),
      positions_recorder_(this),
      allow_peephole_optimization_(false) {
  // BUG(3245989): disable peephole optimization if crankshaft is enabled.
  allow_peephole_optimization_ = FLAG_peephole_optimization;
  if (buffer == NULL) {
    // Do our own buffer management.
    if (buffer_size <= kMinimalBufferSize) {
      buffer_size = kMinimalBufferSize;

      if (isolate()->assembler_spare_buffer() != NULL) {
        buffer = isolate()->assembler_spare_buffer();
        isolate()->set_assembler_spare_buffer(NULL);
      }
    }
    if (buffer == NULL) {
      buffer_ = NewArray<byte>(buffer_size);
    } else {
      buffer_ = static_cast<byte*>(buffer);
    }
    buffer_size_ = buffer_size;
    own_buffer_ = true;

  } else {
    // Use externally provided buffer instead.
    ASSERT(buffer_size > 0);
    buffer_ = static_cast<byte*>(buffer);
    buffer_size_ = buffer_size;
    own_buffer_ = false;
  }

  // Setup buffer pointers.
  ASSERT(buffer_ != NULL);
  pc_ = buffer_;
  reloc_info_writer.Reposition(buffer_ + buffer_size, pc_);

  last_trampoline_pool_end_ = 0;
  no_trampoline_pool_before_ = 0;
  trampoline_pool_blocked_nesting_ = 0;
  next_buffer_check_ = kMaxBranchOffset - kTrampolineSize;
}


Assembler::~Assembler() {
  if (own_buffer_) {
    if (isolate()->assembler_spare_buffer() == NULL &&
      buffer_size_ == kMinimalBufferSize) {
      isolate()->set_assembler_spare_buffer(buffer_);
    } else {
      DeleteArray(buffer_);
    }
  }
}


void Assembler::GetCode(CodeDesc* desc) {
  ASSERT(pc_ <= reloc_info_writer.pos());  // No overlap.
  // Setup code descriptor.
  desc->buffer = buffer_;
  desc->buffer_size = buffer_size_;
  desc->instr_size = pc_offset();
  desc->reloc_size = (buffer_ + buffer_size_) - reloc_info_writer.pos();
}


void Assembler::Align(int m) {
  ASSERT(m >= 4 && IsPowerOf2(m));
  while ((pc_offset() & (m - 1)) != 0) {
    nop();
  }
}


void Assembler::CodeTargetAlign() {
  // No advantage to aligning branch/call targets to more than
  // single instruction, that I am aware of.
  Align(4);
}


Register Assembler::GetRt(Instr instr) {
  Register rt;
  rt.code_ = (instr & kRtMask) >> kRtShift;
  return rt;
}


bool Assembler::IsPop(Instr instr) {
  return (instr & ~kRtMask) == kPopRegPattern;
}


bool Assembler::IsPush(Instr instr) {
  return (instr & ~kRtMask) == kPushRegPattern;
}


bool Assembler::IsSwRegFpOffset(Instr instr) {
  return ((instr & kLwSwInstrTypeMask) == kSwRegFpOffsetPattern);
}


bool Assembler::IsLwRegFpOffset(Instr instr) {
  return ((instr & kLwSwInstrTypeMask) == kLwRegFpOffsetPattern);
}


bool Assembler::IsSwRegFpNegOffset(Instr instr) {
  return ((instr & (kLwSwInstrTypeMask | kNegOffset)) ==
          kSwRegFpNegOffsetPattern);
}


bool Assembler::IsLwRegFpNegOffset(Instr instr) {
  return ((instr & (kLwSwInstrTypeMask | kNegOffset)) ==
          kLwRegFpNegOffsetPattern);
}


// Labels refer to positions in the (to be) generated code.
// There are bound, linked, and unused labels.
//
// Bound labels refer to known positions in the already
// generated code. pos() is the position the label refers to.
//
// Linked labels refer to unknown positions in the code
// to be generated; pos() is the position of the last
// instruction using the label.

// The link chain is terminated by a value in the instruction of -1,
// which is an otherwise illegal value (branch -1 is inf loop).
// The instruction 16-bit offset field addresses 32-bit words, but in
// code is conv to an 18-bit value addressing bytes, hence the -4 value.

const int kEndOfChain = -4;


bool Assembler::IsBranch(Instr instr) {
  uint32_t opcode   = ((instr & kOpcodeMask));
  uint32_t rt_field = ((instr & kRtFieldMask));
  uint32_t rs_field = ((instr & kRsFieldMask));
  uint32_t label_constant = (instr & ~kImm16Mask);
  // Checks if the instruction is a branch.
  return opcode == BEQ ||
      opcode == BNE ||
      opcode == BLEZ ||
      opcode == BGTZ ||
      opcode == BEQL ||
      opcode == BNEL ||
      opcode == BLEZL ||
      opcode == BGTZL||
      (opcode == REGIMM && (rt_field == BLTZ || rt_field == BGEZ ||
                            rt_field == BLTZAL || rt_field == BGEZAL)) ||
      (opcode == COP1 && rs_field == BC1) ||  // Coprocessor branch.
      label_constant == 0;  // Emitted label const in reg-exp engine.
}


bool Assembler::IsNop(Instr instr, unsigned int type) {
  // See Assembler::nop(type).
  ASSERT(type < 32);
  uint32_t opcode = ((instr & kOpcodeMask));
  uint32_t rt = ((instr & kRtFieldMask) >> kRtShift);
  uint32_t rs = ((instr & kRsFieldMask) >> kRsShift);
  uint32_t sa = ((instr & kSaFieldMask) >> kSaShift);

  // nop(type) == sll(zero_reg, zero_reg, type);
  // Technically all these values will be 0 but
  // this makes more sense to the reader.

  bool ret = (opcode == SLL &&
              rt == static_cast<uint32_t>(ToNumber(zero_reg)) &&
              rs == static_cast<uint32_t>(ToNumber(zero_reg)) &&
              sa == type);

  return ret;
}


int32_t Assembler::GetBranchOffset(Instr instr) {
  ASSERT(IsBranch(instr));
  return ((int16_t)(instr & kImm16Mask)) << 2;
}


bool Assembler::IsLw(Instr instr) {
  return ((instr & kOpcodeMask) == LW);
}


int16_t Assembler::GetLwOffset(Instr instr) {
  ASSERT(IsLw(instr));
  return ((instr & kImm16Mask));
}


Instr Assembler::SetLwOffset(Instr instr, int16_t offset) {
  ASSERT(IsLw(instr));

  // We actually create a new lw instruction based on the original one.
  Instr temp_instr = LW | (instr & kRsFieldMask) | (instr & kRtFieldMask)
      | (offset & kImm16Mask);

  return temp_instr;
}


bool Assembler::IsSw(Instr instr) {
  return ((instr & kOpcodeMask) == SW);
}


Instr Assembler::SetSwOffset(Instr instr, int16_t offset) {
  ASSERT(IsSw(instr));
  return ((instr & ~kImm16Mask) | (offset & kImm16Mask));
}


bool Assembler::IsAddImmediate(Instr instr) {
  return ((instr & kOpcodeMask) == ADDIU);
}


Instr Assembler::SetAddImmediateOffset(Instr instr, int16_t offset) {
  ASSERT(IsAddImmediate(instr));
  return ((instr & ~kImm16Mask) | (offset & kImm16Mask));
}


int Assembler::target_at(int32_t pos) {
  Instr instr = instr_at(pos);
  if ((instr & ~kImm16Mask) == 0) {
    // Emitted label constant, not part of a branch.
    if (instr == 0) {
       return kEndOfChain;
     } else {
       int32_t imm18 =((instr & static_cast<int32_t>(kImm16Mask)) << 16) >> 14;
       return (imm18 + pos);
     }
  }
  // Check we have a branch instruction.
  ASSERT(IsBranch(instr));
  // Do NOT change this to <<2. We rely on arithmetic shifts here, assuming
  // the compiler uses arithmectic shifts for signed integers.
  int32_t imm18 = ((instr & static_cast<int32_t>(kImm16Mask)) << 16) >> 14;

  if (imm18 == kEndOfChain) {
    // EndOfChain sentinel is returned directly, not relative to pc or pos.
    return kEndOfChain;
  } else {
    return pos + kBranchPCOffset + imm18;
  }
}


void Assembler::target_at_put(int32_t pos, int32_t target_pos) {
  Instr instr = instr_at(pos);
  if ((instr & ~kImm16Mask) == 0) {
    ASSERT(target_pos == kEndOfChain || target_pos >= 0);
    // Emitted label constant, not part of a branch.
    // Make label relative to Code* of generated Code object.
    instr_at_put(pos, target_pos + (Code::kHeaderSize - kHeapObjectTag));
    return;
  }

  ASSERT(IsBranch(instr));
  int32_t imm18 = target_pos - (pos + kBranchPCOffset);
  ASSERT((imm18 & 3) == 0);

  instr &= ~kImm16Mask;
  int32_t imm16 = imm18 >> 2;
  ASSERT(is_int16(imm16));

  instr_at_put(pos, instr | (imm16 & kImm16Mask));
}


void Assembler::print(Label* L) {
  if (L->is_unused()) {
    PrintF("unused label\n");
  } else if (L->is_bound()) {
    PrintF("bound label to %d\n", L->pos());
  } else if (L->is_linked()) {
    Label l = *L;
    PrintF("unbound label");
    while (l.is_linked()) {
      PrintF("@ %d ", l.pos());
      Instr instr = instr_at(l.pos());
      if ((instr & ~kImm16Mask) == 0) {
        PrintF("value\n");
      } else {
        PrintF("%d\n", instr);
      }
      next(&l);
    }
  } else {
    PrintF("label in inconsistent state (pos = %d)\n", L->pos_);
  }
}


void Assembler::bind_to(Label* L, int pos) {
  ASSERT(0 <= pos && pos <= pc_offset());  // Must have valid binding position.
  while (L->is_linked()) {
    int32_t fixup_pos = L->pos();
    int32_t dist = pos - fixup_pos;
    next(L);  // Call next before overwriting link with target at fixup_pos.
    if (dist > kMaxBranchOffset) {
      do {
        int32_t trampoline_pos = get_trampoline_entry(fixup_pos);
        ASSERT((trampoline_pos - fixup_pos) <= kMaxBranchOffset);
        target_at_put(fixup_pos, trampoline_pos);
        fixup_pos = trampoline_pos;
        dist = pos - fixup_pos;
      } while (dist > kMaxBranchOffset);
    } else if (dist < -kMaxBranchOffset) {
      do {
        int32_t trampoline_pos = get_trampoline_entry(fixup_pos, false);
        ASSERT((trampoline_pos - fixup_pos) >= -kMaxBranchOffset);
        target_at_put(fixup_pos, trampoline_pos);
        fixup_pos = trampoline_pos;
        dist = pos - fixup_pos;
      } while (dist < -kMaxBranchOffset);
    };
    target_at_put(fixup_pos, pos);
  }
  L->bind_to(pos);

  // Keep track of the last bound label so we don't eliminate any instructions
  // before a bound label.
  if (pos > last_bound_pos_)
    last_bound_pos_ = pos;
}


void Assembler::link_to(Label* L, Label* appendix) {
  if (appendix->is_linked()) {
    if (L->is_linked()) {
      // Append appendix to L's list.
      int fixup_pos;
      int link = L->pos();
      do {
        fixup_pos = link;
        link = target_at(fixup_pos);
      } while (link > 0);
      ASSERT(link == kEndOfChain);
      target_at_put(fixup_pos, appendix->pos());
    } else {
      // L is empty, simply use appendix.
      *L = *appendix;
    }
  }
  appendix->Unuse();  // Appendix should not be used anymore.
}


void Assembler::bind(Label* L) {
  ASSERT(!L->is_bound());  // Label can only be bound once.
  bind_to(L, pc_offset());
}


void Assembler::next(Label* L) {
  ASSERT(L->is_linked());
  int link = target_at(L->pos());
  ASSERT(link > 0 || link == kEndOfChain);
  if (link == kEndOfChain) {
    L->Unuse();
  } else if (link > 0) {
    L->link_to(link);
  }
}


// We have to use a temporary register for things that can be relocated even
// if they can be encoded in the MIPS's 16 bits of immediate-offset instruction
// space.  There is no guarantee that the relocated location can be similarly
// encoded.
bool Assembler::MustUseReg(RelocInfo::Mode rmode) {
  return rmode != RelocInfo::NONE;
}


void Assembler::GenInstrRegister(Opcode opcode,
                                 Register rs,
                                 Register rt,
                                 Register rd,
                                 uint16_t sa,
                                 SecondaryField func) {
  ASSERT(rd.is_valid() && rs.is_valid() && rt.is_valid() && is_uint5(sa));
  Instr instr = opcode | (rs.code() << kRsShift) | (rt.code() << kRtShift)
      | (rd.code() << kRdShift) | (sa << kSaShift) | func;
  emit(instr);
}


void Assembler::GenInstrRegister(Opcode opcode,
                                 Register rs,
                                 Register rt,
                                 uint16_t msb,
                                 uint16_t lsb,
                                 SecondaryField func) {
  ASSERT(rs.is_valid() && rt.is_valid() && is_uint5(msb) && is_uint5(lsb));
  Instr instr = opcode | (rs.code() << kRsShift) | (rt.code() << kRtShift)
      | (msb << kRdShift) | (lsb << kSaShift) | func;
  emit(instr);
}


void Assembler::GenInstrRegister(Opcode opcode,
                                 SecondaryField fmt,
                                 FPURegister ft,
                                 FPURegister fs,
                                 FPURegister fd,
                                 SecondaryField func) {
  ASSERT(fd.is_valid() && fs.is_valid() && ft.is_valid());
  ASSERT(isolate()->cpu_features()->IsEnabled(FPU));
  Instr instr = opcode | fmt | (ft.code() << kFtShift) | (fs.code() << kFsShift)
      | (fd.code() << kFdShift) | func;
  emit(instr);
}


void Assembler::GenInstrRegister(Opcode opcode,
                                 SecondaryField fmt,
                                 Register rt,
                                 FPURegister fs,
                                 FPURegister fd,
                                 SecondaryField func) {
  ASSERT(fd.is_valid() && fs.is_valid() && rt.is_valid());
  ASSERT(isolate()->cpu_features()->IsEnabled(FPU));
  Instr instr = opcode | fmt | (rt.code() << kRtShift)
      | (fs.code() << kFsShift) | (fd.code() << kFdShift) | func;
  emit(instr);
}


void Assembler::GenInstrRegister(Opcode opcode,
                                 SecondaryField fmt,
                                 Register rt,
                                 FPUControlRegister fs,
                                 SecondaryField func) {
  ASSERT(fs.is_valid() && rt.is_valid());
  ASSERT(isolate()->cpu_features()->IsEnabled(FPU));
  Instr instr =
      opcode | fmt | (rt.code() << kRtShift) | (fs.code() << kFsShift) | func;
  emit(instr);
}


// Instructions with immediate value.
// Registers are in the order of the instruction encoding, from left to right.
void Assembler::GenInstrImmediate(Opcode opcode,
                                  Register rs,
                                  Register rt,
                                  int32_t j) {
  ASSERT(rs.is_valid() && rt.is_valid() && (is_int16(j) || is_uint16(j)));
  Instr instr = opcode | (rs.code() << kRsShift) | (rt.code() << kRtShift)
      | (j & kImm16Mask);
  emit(instr);
}


void Assembler::GenInstrImmediate(Opcode opcode,
                                  Register rs,
                                  SecondaryField SF,
                                  int32_t j) {
  ASSERT(rs.is_valid() && (is_int16(j) || is_uint16(j)));
  Instr instr = opcode | (rs.code() << kRsShift) | SF | (j & kImm16Mask);
  emit(instr);
}


void Assembler::GenInstrImmediate(Opcode opcode,
                                  Register rs,
                                  FPURegister ft,
                                  int32_t j) {
  ASSERT(rs.is_valid() && ft.is_valid() && (is_int16(j) || is_uint16(j)));
  ASSERT(isolate()->cpu_features()->IsEnabled(FPU));
  Instr instr = opcode | (rs.code() << kRsShift) | (ft.code() << kFtShift)
      | (j & kImm16Mask);
  emit(instr);
}


// Registers are in the order of the instruction encoding, from left to right.
void Assembler::GenInstrJump(Opcode opcode,
                              uint32_t address) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  ASSERT(is_uint26(address));
  Instr instr = opcode | address;
  emit(instr);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}


// Returns the next free label entry from the next trampoline pool.
int32_t Assembler::get_label_entry(int32_t pos, bool next_pool) {
  int trampoline_count = trampolines_.length();
  int32_t label_entry = 0;
  ASSERT(trampoline_count > 0);

  if (next_pool) {
    for (int i = 0; i < trampoline_count; i++) {
      if (trampolines_[i].start() > pos) {
       label_entry = trampolines_[i].take_label();
       break;
      }
    }
  } else {  //  Caller needs a label entry from the previous pool.
    for (int i = trampoline_count-1; i >= 0; i--) {
      if (trampolines_[i].end() < pos) {
       label_entry = trampolines_[i].take_label();
       break;
      }
    }
  }
  return label_entry;
}


// Returns the next free trampoline entry from the next trampoline pool.
int32_t Assembler::get_trampoline_entry(int32_t pos, bool next_pool) {
  int trampoline_count = trampolines_.length();
  int32_t trampoline_entry = 0;
  ASSERT(trampoline_count > 0);

  if (next_pool) {
    for (int i = 0; i < trampoline_count; i++) {
      if (trampolines_[i].start() > pos) {
       trampoline_entry = trampolines_[i].take_slot();
       break;
      }
    }
  } else {  // Caller needs a trampoline entry from the previous pool.
    for (int i = trampoline_count-1; i >= 0; i--) {
      if (trampolines_[i].end() < pos) {
       trampoline_entry = trampolines_[i].take_slot();
       break;
      }
    }
  }
  return trampoline_entry;
}


int32_t Assembler::branch_offset(Label* L, bool jump_elimination_allowed) {
  int32_t target_pos;
  int32_t pc_offset_v = pc_offset();

  if (L->is_bound()) {
    target_pos = L->pos();
    int32_t dist = pc_offset_v - target_pos;
    if (dist > kMaxBranchOffset) {
      do {
        int32_t trampoline_pos = get_trampoline_entry(target_pos);
        ASSERT((trampoline_pos - target_pos) > 0);
        ASSERT((trampoline_pos - target_pos) <= kMaxBranchOffset);
        target_at_put(trampoline_pos, target_pos);
        target_pos = trampoline_pos;
        dist = pc_offset_v - target_pos;
      } while (dist > kMaxBranchOffset);
    } else if (dist < -kMaxBranchOffset) {
      do {
        int32_t trampoline_pos = get_trampoline_entry(target_pos, false);
        ASSERT((target_pos - trampoline_pos) > 0);
        ASSERT((target_pos - trampoline_pos) <= kMaxBranchOffset);
        target_at_put(trampoline_pos, target_pos);
        target_pos = trampoline_pos;
        dist = pc_offset_v - target_pos;
      } while (dist < -kMaxBranchOffset);
    }
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link.
      int32_t dist = pc_offset_v - target_pos;
      if (dist > kMaxBranchOffset) {
        do {
          int32_t label_pos = get_label_entry(target_pos);
          ASSERT((label_pos - target_pos) < kMaxBranchOffset);
          label_at_put(L, label_pos);
          target_pos = label_pos;
          dist = pc_offset_v - target_pos;
        } while (dist > kMaxBranchOffset);
      } else if (dist < -kMaxBranchOffset) {
        do {
          int32_t label_pos = get_label_entry(target_pos, false);
          ASSERT((label_pos - target_pos) > -kMaxBranchOffset);
          label_at_put(L, label_pos);
          target_pos = label_pos;
          dist = pc_offset_v - target_pos;
        } while (dist < -kMaxBranchOffset);
      }
      L->link_to(pc_offset());
    } else {
      L->link_to(pc_offset());
      return kEndOfChain;
    }
  }

  int32_t offset = target_pos - (pc_offset() + kBranchPCOffset);
  ASSERT((offset & 3) == 0);
  ASSERT(is_int16(offset >> 2));

  return offset;
}


void Assembler::label_at_put(Label* L, int at_offset) {
  int target_pos;
  if (L->is_bound()) {
    target_pos = L->pos();
    instr_at_put(at_offset, target_pos + (Code::kHeaderSize - kHeapObjectTag));
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link.
      int32_t imm18 = target_pos - at_offset;
      ASSERT((imm18 & 3) == 0);
      int32_t imm16 = imm18 >> 2;
      ASSERT(is_int16(imm16));
      instr_at_put(at_offset, (imm16 & kImm16Mask));
    } else {
      target_pos = kEndOfChain;
      instr_at_put(at_offset, 0);
    }
    L->link_to(at_offset);
  }
}


//------- Branch and jump instructions --------

void Assembler::b(int16_t offset) {
  beq(zero_reg, zero_reg, offset);
}


void Assembler::bal(int16_t offset) {
  positions_recorder()->WriteRecordedPositions();
  bgezal(zero_reg, offset);
}


void Assembler::beq(Register rs, Register rt, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(BEQ, rs, rt, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}


void Assembler::bgez(Register rs, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(REGIMM, rs, BGEZ, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}


void Assembler::bgezal(Register rs, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  positions_recorder()->WriteRecordedPositions();
  GenInstrImmediate(REGIMM, rs, BGEZAL, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}


void Assembler::bgtz(Register rs, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(BGTZ, rs, zero_reg, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}


void Assembler::blez(Register rs, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(BLEZ, rs, zero_reg, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}


void Assembler::bltz(Register rs, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(REGIMM, rs, BLTZ, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}


void Assembler::bltzal(Register rs, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  positions_recorder()->WriteRecordedPositions();
  GenInstrImmediate(REGIMM, rs, BLTZAL, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}


void Assembler::bne(Register rs, Register rt, int16_t offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  GenInstrImmediate(BNE, rs, rt, offset);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}


void Assembler::j(int32_t target) {
  ASSERT(is_uint28(target) && ((target & 3) == 0));
  GenInstrJump(J, target >> 2);
}


void Assembler::jr(Register rs) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (rs.is(ra)) {
    positions_recorder()->WriteRecordedPositions();
  }
  GenInstrRegister(SPECIAL, rs, zero_reg, zero_reg, 0, JR);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}


void Assembler::jal(int32_t target) {
  positions_recorder()->WriteRecordedPositions();
  ASSERT(is_uint28(target) && ((target & 3) == 0));
  GenInstrJump(JAL, target >> 2);
}


void Assembler::jalr(Register rs, Register rd) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  positions_recorder()->WriteRecordedPositions();
  GenInstrRegister(SPECIAL, rs, zero_reg, rd, 0, JALR);
  BlockTrampolinePoolFor(1);  // For associated delay slot.
}


//-------Data-processing-instructions---------

// Arithmetic.

void Assembler::addu(Register rd, Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, ADDU);
}


void Assembler::addiu(Register rd, Register rs, int32_t j) {
  GenInstrImmediate(ADDIU, rs, rd, j);

  // Eliminate pattern: push(r), pop().
  //   addiu(sp, sp, Operand(-kPointerSize));
  //   sw(src, MemOperand(sp, 0);
  //   addiu(sp, sp, Operand(kPointerSize));
  // Both instructions can be eliminated.
  if (can_peephole_optimize(3) &&
      // Pattern.
      instr_at(pc_ - 1 * kInstrSize) == kPopInstruction &&
      (instr_at(pc_ - 2 * kInstrSize) & ~kRtMask) == kPushRegPattern &&
      (instr_at(pc_ - 3 * kInstrSize)) == kPushInstruction) {
    pc_ -= 3 * kInstrSize;
    if (FLAG_print_peephole_optimization) {
      PrintF("%x push(reg)/pop() eliminated\n", pc_offset());
    }
  }

  // Eliminate pattern: push(ry), pop(rx).
  //   addiu(sp, sp, -kPointerSize)
  //   sw(ry, MemOperand(sp, 0)
  //   lw(rx, MemOperand(sp, 0)
  //   addiu(sp, sp, kPointerSize);
  // Both instructions can be eliminated if ry = rx.
  // If ry != rx, a register copy from ry to rx is inserted
  // after eliminating the push and the pop instructions.
  if (can_peephole_optimize(4)) {
    Instr pre_push_sp_set = instr_at(pc_ - 4 * kInstrSize);
    Instr push_instr = instr_at(pc_ - 3 * kInstrSize);
    Instr pop_instr = instr_at(pc_ - 2 * kInstrSize);
    Instr post_pop_sp_set = instr_at(pc_ - 1 * kInstrSize);

    if (IsPush(push_instr) &&
        IsPop(pop_instr) && pre_push_sp_set == kPushInstruction &&
        post_pop_sp_set == kPopInstruction) {
      if ((pop_instr & kRtMask) != (push_instr & kRtMask)) {
        // For consecutive push and pop on different registers,
        // we delete both the push & pop and insert a register move.
        // push ry, pop rx --> mov rx, ry.
        Register reg_pushed, reg_popped;
        reg_pushed = GetRt(push_instr);
        reg_popped = GetRt(pop_instr);
        pc_ -= 4 * kInstrSize;
        // Insert a mov instruction, which is better than a pair of push & pop.
        or_(reg_popped, reg_pushed, zero_reg);
        if (FLAG_print_peephole_optimization) {
          PrintF("%x push/pop (diff reg) replaced by a reg move\n",
                 pc_offset());
        }
      } else {
        // For consecutive push and pop on the same register,
        // both the push and the pop can be deleted.
        pc_ -= 4 * kInstrSize;
        if (FLAG_print_peephole_optimization) {
          PrintF("%x push/pop (same reg) eliminated\n", pc_offset());
        }
      }
    }
  }

  if (can_peephole_optimize(5)) {
    Instr pre_push_sp_set = instr_at(pc_ - 5 * kInstrSize);
    Instr mem_write_instr = instr_at(pc_ - 4 * kInstrSize);
    Instr lw_instr = instr_at(pc_ - 3 * kInstrSize);
    Instr mem_read_instr = instr_at(pc_ - 2 * kInstrSize);
    Instr post_pop_sp_set = instr_at(pc_ - 1 * kInstrSize);

    if (IsPush(mem_write_instr) &&
        pre_push_sp_set == kPushInstruction &&
        IsPop(mem_read_instr) &&
        post_pop_sp_set == kPopInstruction) {
      if ((IsLwRegFpOffset(lw_instr) ||
        IsLwRegFpNegOffset(lw_instr))) {
        if ((mem_write_instr & kRtMask) ==
              (mem_read_instr & kRtMask)) {
          // Pattern: push & pop from/to same register,
          // with a fp+offset lw in between.
          //
          // The following:
          // addiu sp, sp, -4
          // sw rx, [sp, #0]!
          // lw rz, [fp, #-24]
          // lw rx, [sp, 0],
          // addiu sp, sp, 4
          //
          // Becomes:
          // if(rx == rz)
          //   delete all
          // else
          //   lw rz, [fp, #-24]

          if ((mem_write_instr & kRtMask) == (lw_instr & kRtMask)) {
            pc_ -= 5 * kInstrSize;
          } else {
            pc_ -= 5 * kInstrSize;
            // Reinsert back the lw rz.
            emit(lw_instr);
          }
          if (FLAG_print_peephole_optimization) {
            PrintF("%x push/pop -dead ldr fp+offset in middle\n", pc_offset());
          }
        } else {
          // Pattern: push & pop from/to different registers
          // with a fp + offset lw in between.
          //
          // The following:
          // addiu sp, sp ,-4
          // sw rx, [sp, 0]
          // lw rz, [fp, #-24]
          // lw ry, [sp, 0]
          // addiu sp, sp, 4
          //
          // Becomes:
          // if(ry == rz)
          //   mov ry, rx;
          // else if(rx != rz)
          //   lw rz, [fp, #-24]
          //   mov ry, rx
          // else if((ry != rz) || (rx == rz)) becomes:
          //   mov ry, rx
          //   lw rz, [fp, #-24]

          Register reg_pushed, reg_popped;
          if ((mem_read_instr & kRtMask) == (lw_instr & kRtMask)) {
            reg_pushed = GetRt(mem_write_instr);
            reg_popped = GetRt(mem_read_instr);
            pc_ -= 5 * kInstrSize;
            or_(reg_popped, reg_pushed, zero_reg);  // Move instruction.
          } else if ((mem_write_instr & kRtMask)
                                != (lw_instr & kRtMask)) {
            reg_pushed = GetRt(mem_write_instr);
            reg_popped = GetRt(mem_read_instr);
            pc_ -= 5 * kInstrSize;
            emit(lw_instr);
            or_(reg_popped, reg_pushed, zero_reg);  // Move instruction.
          } else if (((mem_read_instr & kRtMask)
                                     != (lw_instr & kRtMask)) ||
                    ((mem_write_instr & kRtMask)
                                     == (lw_instr & kRtMask)) ) {
            reg_pushed = GetRt(mem_write_instr);
            reg_popped = GetRt(mem_read_instr);
            pc_ -= 5 * kInstrSize;
            or_(reg_popped, reg_pushed, zero_reg);  // Move instruction.
            emit(lw_instr);
          }
          if (FLAG_print_peephole_optimization) {
            PrintF("%x push/pop (ldr fp+off in middle)\n", pc_offset());
          }
        }
      }
    }
  }
}


void Assembler::subu(Register rd, Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, SUBU);
}


void Assembler::mul(Register rd, Register rs, Register rt) {
  GenInstrRegister(SPECIAL2, rs, rt, rd, 0, MUL);
}


void Assembler::mult(Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, zero_reg, 0, MULT);
}


void Assembler::multu(Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, zero_reg, 0, MULTU);
}


void Assembler::div(Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, zero_reg, 0, DIV);
}


void Assembler::divu(Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, zero_reg, 0, DIVU);
}


// Logical.

void Assembler::and_(Register rd, Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, AND);
}


void Assembler::andi(Register rt, Register rs, int32_t j) {
  GenInstrImmediate(ANDI, rs, rt, j);
}


void Assembler::or_(Register rd, Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, OR);
}


void Assembler::ori(Register rt, Register rs, int32_t j) {
  GenInstrImmediate(ORI, rs, rt, j);
}


void Assembler::xor_(Register rd, Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, XOR);
}


void Assembler::xori(Register rt, Register rs, int32_t j) {
  GenInstrImmediate(XORI, rs, rt, j);
}


void Assembler::nor(Register rd, Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, NOR);
}


// Shifts.
void Assembler::sll(Register rd,
                    Register rt,
                    uint16_t sa,
                    bool coming_from_nop) {
  // Don't allow nop instructions in the form sll zero_reg, zero_reg to be
  // generated using the sll instruction. They must be generated using
  // nop(int/NopMarkerTypes) or MarkCode(int/NopMarkerTypes) pseudo
  // instructions.
  ASSERT(coming_from_nop || !(rd.is(zero_reg) && rt.is(zero_reg)));
  GenInstrRegister(SPECIAL, zero_reg, rt, rd, sa, SLL);
}


void Assembler::sllv(Register rd, Register rt, Register rs) {
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, SLLV);
}


void Assembler::srl(Register rd, Register rt, uint16_t sa) {
  GenInstrRegister(SPECIAL, zero_reg, rt, rd, sa, SRL);
}


void Assembler::srlv(Register rd, Register rt, Register rs) {
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, SRLV);
}


void Assembler::sra(Register rd, Register rt, uint16_t sa) {
  GenInstrRegister(SPECIAL, zero_reg, rt, rd, sa, SRA);
}


void Assembler::srav(Register rd, Register rt, Register rs) {
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, SRAV);
}


void Assembler::rotr(Register rd, Register rt, uint16_t sa) {
  // Should be called via MacroAssembler::Ror.
  ASSERT(rd.is_valid() && rt.is_valid() && is_uint5(sa));
  ASSERT(mips32r2);
  Instr instr = SPECIAL | (1 << kRsShift) | (rt.code() << kRtShift)
      | (rd.code() << kRdShift) | (sa << kSaShift) | SRL;
  emit(instr);
}


void Assembler::rotrv(Register rd, Register rt, Register rs) {
  // Should be called via MacroAssembler::Ror.
  ASSERT(rd.is_valid() && rt.is_valid() && rs.is_valid() );
  ASSERT(mips32r2);
  Instr instr = SPECIAL | (rs.code() << kRsShift) | (rt.code() << kRtShift)
     | (rd.code() << kRdShift) | (1 << kSaShift) | SRLV;
  emit(instr);
}


//------------Memory-instructions-------------

// Helper for base-reg + offset, when offset is larger than int16.
void Assembler::LoadRegPlusOffsetToAt(const MemOperand& src) {
  ASSERT(!src.rm().is(at));
  lui(at, src.offset_ >> kLuiShift);
  ori(at, at, src.offset_ & kImm16Mask);  // Load 32-bit offset.
  addu(at, at, src.rm());  // Add base register.
}


void Assembler::lb(Register rd, const MemOperand& rs) {
  if (is_int16(rs.offset_)) {
    GenInstrImmediate(LB, rs.rm(), rd, rs.offset_);
  } else {  // Offset > 16 bits, use multiple instructions to load.
    LoadRegPlusOffsetToAt(rs);
    GenInstrImmediate(LB, at, rd, 0);  // Equiv to lb(rd, MemOperand(at, 0));
  }
}


void Assembler::lbu(Register rd, const MemOperand& rs) {
  if (is_int16(rs.offset_)) {
    GenInstrImmediate(LBU, rs.rm(), rd, rs.offset_);
  } else {  // Offset > 16 bits, use multiple instructions to load.
    LoadRegPlusOffsetToAt(rs);
    GenInstrImmediate(LBU, at, rd, 0);  // Equiv to lbu(rd, MemOperand(at, 0));
  }
}


void Assembler::lh(Register rd, const MemOperand& rs) {
  if (is_int16(rs.offset_)) {
    GenInstrImmediate(LH, rs.rm(), rd, rs.offset_);
  } else {  // Offset > 16 bits, use multiple instructions to load.
    LoadRegPlusOffsetToAt(rs);
    GenInstrImmediate(LH, at, rd, 0);  // Equiv to lh(rd, MemOperand(at, 0));
  }
}


void Assembler::lhu(Register rd, const MemOperand& rs) {
  if (is_int16(rs.offset_)) {
    GenInstrImmediate(LHU, rs.rm(), rd, rs.offset_);
  } else {  // Offset > 16 bits, use multiple instructions to load.
    LoadRegPlusOffsetToAt(rs);
    GenInstrImmediate(LHU, at, rd, 0);  // Equiv to lhu(rd, MemOperand(at, 0));
  }
}


void Assembler::lw(Register rd, const MemOperand& rs) {
  if (is_int16(rs.offset_)) {
    GenInstrImmediate(LW, rs.rm(), rd, rs.offset_);
  } else {  // Offset > 16 bits, use multiple instructions to load.
    LoadRegPlusOffsetToAt(rs);
    GenInstrImmediate(LW, at, rd, 0);  // Equiv to lw(rd, MemOperand(at, 0));
  }

  if (can_peephole_optimize(2)) {
    Instr sw_instr = instr_at(pc_ - 2 * kInstrSize);
    Instr lw_instr = instr_at(pc_ - 1 * kInstrSize);

    if ((IsSwRegFpOffset(sw_instr) &&
         IsLwRegFpOffset(lw_instr)) ||
       (IsSwRegFpNegOffset(sw_instr) &&
         IsLwRegFpNegOffset(lw_instr))) {
      if ((lw_instr & kLwSwInstrArgumentMask) ==
            (sw_instr & kLwSwInstrArgumentMask)) {
        // Pattern: Lw/sw same fp+offset, same register.
        //
        // The following:
        // sw rx, [fp, #-12]
        // lw rx, [fp, #-12]
        //
        // Becomes:
        // sw rx, [fp, #-12]

        pc_ -= 1 * kInstrSize;
        if (FLAG_print_peephole_optimization) {
          PrintF("%x sw/lw (fp + same offset), same reg\n", pc_offset());
        }
      } else if ((lw_instr & kLwSwOffsetMask) ==
                 (sw_instr & kLwSwOffsetMask)) {
        // Pattern: Lw/sw same fp+offset, different register.
        //
        // The following:
        // sw rx, [fp, #-12]
        // lw ry, [fp, #-12]
        //
        // Becomes:
        // sw rx, [fp, #-12]
        // mov ry, rx

        Register reg_stored, reg_loaded;
        reg_stored = GetRt(sw_instr);
        reg_loaded = GetRt(lw_instr);
        pc_ -= 1 * kInstrSize;
        // Insert a mov instruction, which is better than lw.
        or_(reg_loaded, reg_stored, zero_reg);  // Move instruction.
        if (FLAG_print_peephole_optimization) {
          PrintF("%x sw/lw (fp + same offset), diff reg \n", pc_offset());
        }
      }
    }
  }
}


void Assembler::lwl(Register rd, const MemOperand& rs) {
  GenInstrImmediate(LWL, rs.rm(), rd, rs.offset_);
}


void Assembler::lwr(Register rd, const MemOperand& rs) {
  GenInstrImmediate(LWR, rs.rm(), rd, rs.offset_);
}


void Assembler::sb(Register rd, const MemOperand& rs) {
  if (is_int16(rs.offset_)) {
    GenInstrImmediate(SB, rs.rm(), rd, rs.offset_);
  } else {  // Offset > 16 bits, use multiple instructions to store.
    LoadRegPlusOffsetToAt(rs);
    GenInstrImmediate(SB, at, rd, 0);  // Equiv to sb(rd, MemOperand(at, 0));
  }
}


void Assembler::sh(Register rd, const MemOperand& rs) {
  if (is_int16(rs.offset_)) {
    GenInstrImmediate(SH, rs.rm(), rd, rs.offset_);
  } else {  // Offset > 16 bits, use multiple instructions to store.
    LoadRegPlusOffsetToAt(rs);
    GenInstrImmediate(SH, at, rd, 0);  // Equiv to sh(rd, MemOperand(at, 0));
  }
}


void Assembler::sw(Register rd, const MemOperand& rs) {
  if (is_int16(rs.offset_)) {
    GenInstrImmediate(SW, rs.rm(), rd, rs.offset_);
  } else {  // Offset > 16 bits, use multiple instructions to store.
    LoadRegPlusOffsetToAt(rs);
    GenInstrImmediate(SW, at, rd, 0);  // Equiv to sw(rd, MemOperand(at, 0));
  }

  // Eliminate pattern: pop(), push(r).
  //     addiu sp, sp, Operand(kPointerSize);
  //     addiu sp, sp, Operand(-kPointerSize);
  // ->  sw r, MemOpernad(sp, 0);
  if (can_peephole_optimize(3) &&
     // Pattern.
     instr_at(pc_ - 1 * kInstrSize) ==
       (kPushRegPattern | (rd.code() << kRtShift)) &&
     instr_at(pc_ - 2 * kInstrSize) == kPushInstruction &&
     instr_at(pc_ - 3 * kInstrSize) == kPopInstruction) {
    pc_ -= 3 * kInstrSize;
    GenInstrImmediate(SW, rs.rm(), rd, rs.offset_);
    if (FLAG_print_peephole_optimization) {
      PrintF("%x pop()/push(reg) eliminated\n", pc_offset());
    }
  }
}


void Assembler::swl(Register rd, const MemOperand& rs) {
  GenInstrImmediate(SWL, rs.rm(), rd, rs.offset_);
}


void Assembler::swr(Register rd, const MemOperand& rs) {
  GenInstrImmediate(SWR, rs.rm(), rd, rs.offset_);
}


void Assembler::lui(Register rd, int32_t j) {
  GenInstrImmediate(LUI, zero_reg, rd, j);
}


//-------------Misc-instructions--------------

// Break / Trap instructions.
void Assembler::break_(uint32_t code) {
  ASSERT((code & ~0xfffff) == 0);
  Instr break_instr = SPECIAL | BREAK | (code << 6);
  emit(break_instr);
}


void Assembler::tge(Register rs, Register rt, uint16_t code) {
  ASSERT(is_uint10(code));
  Instr instr = SPECIAL | TGE | rs.code() << kRsShift
      | rt.code() << kRtShift | code << 6;
  emit(instr);
}


void Assembler::tgeu(Register rs, Register rt, uint16_t code) {
  ASSERT(is_uint10(code));
  Instr instr = SPECIAL | TGEU | rs.code() << kRsShift
      | rt.code() << kRtShift | code << 6;
  emit(instr);
}


void Assembler::tlt(Register rs, Register rt, uint16_t code) {
  ASSERT(is_uint10(code));
  Instr instr =
      SPECIAL | TLT | rs.code() << kRsShift | rt.code() << kRtShift | code << 6;
  emit(instr);
}


void Assembler::tltu(Register rs, Register rt, uint16_t code) {
  ASSERT(is_uint10(code));
  Instr instr =
      SPECIAL | TLTU | rs.code() << kRsShift
      | rt.code() << kRtShift | code << 6;
  emit(instr);
}


void Assembler::teq(Register rs, Register rt, uint16_t code) {
  ASSERT(is_uint10(code));
  Instr instr =
      SPECIAL | TEQ | rs.code() << kRsShift | rt.code() << kRtShift | code << 6;
  emit(instr);
}


void Assembler::tne(Register rs, Register rt, uint16_t code) {
  ASSERT(is_uint10(code));
  Instr instr =
      SPECIAL | TNE | rs.code() << kRsShift | rt.code() << kRtShift | code << 6;
  emit(instr);
}


// Move from HI/LO register.

void Assembler::mfhi(Register rd) {
  GenInstrRegister(SPECIAL, zero_reg, zero_reg, rd, 0, MFHI);
}


void Assembler::mflo(Register rd) {
  GenInstrRegister(SPECIAL, zero_reg, zero_reg, rd, 0, MFLO);
}


// Set on less than instructions.
void Assembler::slt(Register rd, Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, SLT);
}


void Assembler::sltu(Register rd, Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, SLTU);
}


void Assembler::slti(Register rt, Register rs, int32_t j) {
  GenInstrImmediate(SLTI, rs, rt, j);
}


void Assembler::sltiu(Register rt, Register rs, int32_t j) {
  GenInstrImmediate(SLTIU, rs, rt, j);
}


// Conditional move.
void Assembler::movz(Register rd, Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, MOVZ);
}


void Assembler::movn(Register rd, Register rs, Register rt) {
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, MOVN);
}


void Assembler::movt(Register rd, Register rs, uint16_t cc) {
  Register rt;
  rt.code_ = (cc & 0x0003) << 2 | 1;
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, MOVCI);
}


void Assembler::movf(Register rd, Register rs, uint16_t cc) {
  Register rt;
  rt.code_ = (cc & 0x0003) << 2 | 0;
  GenInstrRegister(SPECIAL, rs, rt, rd, 0, MOVCI);
}


// Bit twiddling.
void Assembler::clz(Register rd, Register rs) {
  // Clz instr requires same GPR number in 'rd' and 'rt' fields.
  GenInstrRegister(SPECIAL2, rs, rd, rd, 0, CLZ);
}


void Assembler::ins_(Register rt, Register rs, uint16_t pos, uint16_t size) {
  // Should be called via MacroAssembler::Ins.
  // Ins instr has 'rt' field as dest, and two uint5: msb, lsb.
  ASSERT(mips32r2);
  GenInstrRegister(SPECIAL3, rs, rt, pos + size - 1, pos, INS);
}


void Assembler::ext_(Register rt, Register rs, uint16_t pos, uint16_t size) {
  // Should be called via MacroAssembler::Ext.
  // Ext instr has 'rt' field as dest, and two uint5: msb, lsb.
  ASSERT(mips32r2);
  GenInstrRegister(SPECIAL3, rs, rt, size - 1, pos, EXT);
}


//--------Coprocessor-instructions----------------

// Load, store, move.
void Assembler::lwc1(FPURegister fd, const MemOperand& src) {
  GenInstrImmediate(LWC1, src.rm(), fd, src.offset_);
}


void Assembler::ldc1(FPURegister fd, const MemOperand& src) {
  // Workaround for non-8-byte alignment of HeapNumber, convert 64-bit
  // load to two 32-bit loads.
  GenInstrImmediate(LWC1, src.rm(), fd, src.offset_);
  FPURegister nextfpreg;
  nextfpreg.setcode(fd.code() + 1);
  GenInstrImmediate(LWC1, src.rm(), nextfpreg, src.offset_ + 4);
}


void Assembler::swc1(FPURegister fd, const MemOperand& src) {
  GenInstrImmediate(SWC1, src.rm(), fd, src.offset_);
}


void Assembler::sdc1(FPURegister fd, const MemOperand& src) {
  // Workaround for non-8-byte alignment of HeapNumber, convert 64-bit
  // store to two 32-bit stores.
  GenInstrImmediate(SWC1, src.rm(), fd, src.offset_);
  FPURegister nextfpreg;
  nextfpreg.setcode(fd.code() + 1);
  GenInstrImmediate(SWC1, src.rm(), nextfpreg, src.offset_ + 4);
}


void Assembler::mtc1(Register rt, FPURegister fs) {
  GenInstrRegister(COP1, MTC1, rt, fs, f0);
}


void Assembler::mfc1(Register rt, FPURegister fs) {
  GenInstrRegister(COP1, MFC1, rt, fs, f0);
}


void Assembler::ctc1(Register rt, FPUControlRegister fs) {
  GenInstrRegister(COP1, CTC1, rt, fs);
}


void Assembler::cfc1(Register rt, FPUControlRegister fs) {
  GenInstrRegister(COP1, CFC1, rt, fs);
}


// Arithmetic.

void Assembler::add_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  GenInstrRegister(COP1, D, ft, fs, fd, ADD_D);
}


void Assembler::sub_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  GenInstrRegister(COP1, D, ft, fs, fd, SUB_D);
}


void Assembler::mul_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  GenInstrRegister(COP1, D, ft, fs, fd, MUL_D);
}


void Assembler::div_d(FPURegister fd, FPURegister fs, FPURegister ft) {
  GenInstrRegister(COP1, D, ft, fs, fd, DIV_D);
}


void Assembler::abs_d(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, D, f0, fs, fd, ABS_D);
}


void Assembler::mov_d(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, D, f0, fs, fd, MOV_D);
}


void Assembler::neg_d(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, D, f0, fs, fd, NEG_D);
}


void Assembler::sqrt_d(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, D, f0, fs, fd, SQRT_D);
}


// Conversions.

void Assembler::cvt_w_s(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, S, f0, fs, fd, CVT_W_S);
}


void Assembler::cvt_w_d(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, D, f0, fs, fd, CVT_W_D);
}


void Assembler::trunc_w_s(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, S, f0, fs, fd, TRUNC_W_S);
}


void Assembler::trunc_w_d(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, D, f0, fs, fd, TRUNC_W_D);
}


void Assembler::round_w_s(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, S, f0, fs, fd, ROUND_W_S);
}


void Assembler::round_w_d(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, D, f0, fs, fd, ROUND_W_D);
}


void Assembler::floor_w_s(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, S, f0, fs, fd, FLOOR_W_S);
}


void Assembler::floor_w_d(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, D, f0, fs, fd, FLOOR_W_D);
}


void Assembler::ceil_w_s(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, S, f0, fs, fd, CEIL_W_S);
}


void Assembler::ceil_w_d(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, D, f0, fs, fd, CEIL_W_D);
}


void Assembler::cvt_l_s(FPURegister fd, FPURegister fs) {
  ASSERT(mips32r2);
  GenInstrRegister(COP1, S, f0, fs, fd, CVT_L_S);
}


void Assembler::cvt_l_d(FPURegister fd, FPURegister fs) {
  ASSERT(mips32r2);
  GenInstrRegister(COP1, D, f0, fs, fd, CVT_L_D);
}


void Assembler::trunc_l_s(FPURegister fd, FPURegister fs) {
  ASSERT(mips32r2);
  GenInstrRegister(COP1, S, f0, fs, fd, TRUNC_L_S);
}


void Assembler::trunc_l_d(FPURegister fd, FPURegister fs) {
  ASSERT(mips32r2);
  GenInstrRegister(COP1, D, f0, fs, fd, TRUNC_L_D);
}


void Assembler::round_l_s(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, S, f0, fs, fd, ROUND_L_S);
}


void Assembler::round_l_d(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, D, f0, fs, fd, ROUND_L_D);
}


void Assembler::floor_l_s(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, S, f0, fs, fd, FLOOR_L_S);
}


void Assembler::floor_l_d(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, D, f0, fs, fd, FLOOR_L_D);
}


void Assembler::ceil_l_s(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, S, f0, fs, fd, CEIL_L_S);
}


void Assembler::ceil_l_d(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, D, f0, fs, fd, CEIL_L_D);
}


void Assembler::cvt_s_w(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, W, f0, fs, fd, CVT_S_W);
}


void Assembler::cvt_s_l(FPURegister fd, FPURegister fs) {
  ASSERT(mips32r2);
  GenInstrRegister(COP1, L, f0, fs, fd, CVT_S_L);
}


void Assembler::cvt_s_d(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, D, f0, fs, fd, CVT_S_D);
}


void Assembler::cvt_d_w(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, W, f0, fs, fd, CVT_D_W);
}


void Assembler::cvt_d_l(FPURegister fd, FPURegister fs) {
  ASSERT(mips32r2);
  GenInstrRegister(COP1, L, f0, fs, fd, CVT_D_L);
}


void Assembler::cvt_d_s(FPURegister fd, FPURegister fs) {
  GenInstrRegister(COP1, S, f0, fs, fd, CVT_D_S);
}


// Conditions.
void Assembler::c(FPUCondition cond, SecondaryField fmt,
    FPURegister fs, FPURegister ft, uint16_t cc) {
  ASSERT(isolate()->cpu_features()->IsEnabled(FPU));
  ASSERT(is_uint3(cc));
  ASSERT((fmt & ~(31 << kRsShift)) == 0);
  Instr instr = COP1 | fmt | ft.code() << 16 | fs.code() << kFsShift
      | cc << 8 | 3 << 4 | cond;
  emit(instr);
}


void Assembler::fcmp(FPURegister src1, const double src2,
      FPUCondition cond) {
  ASSERT(isolate()->cpu_features()->IsSupported(FPU));
  ASSERT(src2 == 0.0);
  mtc1(zero_reg, f14);
  cvt_d_w(f14, f14);
  c(cond, D, src1, f14, 0);
}


void Assembler::bc1f(int16_t offset, uint16_t cc) {
  ASSERT(isolate()->cpu_features()->IsEnabled(FPU));
  ASSERT(is_uint3(cc));
  Instr instr = COP1 | BC1 | cc << 18 | 0 << 16 | (offset & kImm16Mask);
  emit(instr);
}


void Assembler::bc1t(int16_t offset, uint16_t cc) {
  ASSERT(isolate()->cpu_features()->IsEnabled(FPU));
  ASSERT(is_uint3(cc));
  Instr instr = COP1 | BC1 | cc << 18 | 1 << 16 | (offset & kImm16Mask);
  emit(instr);
}


// Debugging.
void Assembler::RecordJSReturn() {
  positions_recorder()->WriteRecordedPositions();
  CheckBuffer();
  RecordRelocInfo(RelocInfo::JS_RETURN);
}


void Assembler::RecordDebugBreakSlot() {
  positions_recorder()->WriteRecordedPositions();
  CheckBuffer();
  RecordRelocInfo(RelocInfo::DEBUG_BREAK_SLOT);
}


void Assembler::RecordComment(const char* msg) {
  if (FLAG_code_comments) {
    CheckBuffer();
    RecordRelocInfo(RelocInfo::COMMENT, reinterpret_cast<intptr_t>(msg));
  }
}


void Assembler::GrowBuffer() {
  if (!own_buffer_) FATAL("external code buffer is too small");

  // Compute new buffer size.
  CodeDesc desc;  // The new buffer.
  if (buffer_size_ < 4*KB) {
    desc.buffer_size = 4*KB;
  } else if (buffer_size_ < 1*MB) {
    desc.buffer_size = 2*buffer_size_;
  } else {
    desc.buffer_size = buffer_size_ + 1*MB;
  }
  CHECK_GT(desc.buffer_size, 0);  // No overflow.

  // Setup new buffer.
  desc.buffer = NewArray<byte>(desc.buffer_size);

  desc.instr_size = pc_offset();
  desc.reloc_size = (buffer_ + buffer_size_) - reloc_info_writer.pos();

  // Copy the data.
  int pc_delta = desc.buffer - buffer_;
  int rc_delta = (desc.buffer + desc.buffer_size) - (buffer_ + buffer_size_);
  memmove(desc.buffer, buffer_, desc.instr_size);
  memmove(reloc_info_writer.pos() + rc_delta,
          reloc_info_writer.pos(), desc.reloc_size);

  // Switch buffers.
  DeleteArray(buffer_);
  buffer_ = desc.buffer;
  buffer_size_ = desc.buffer_size;
  pc_ += pc_delta;
  reloc_info_writer.Reposition(reloc_info_writer.pos() + rc_delta,
                               reloc_info_writer.last_pc() + pc_delta);

  // On ia32 and ARM pc relative addressing is used, and we thus need to apply a
  // shift by pc_delta. But on MIPS the target address it directly loaded, so
  // we do not need to relocate here.

  ASSERT(!overflow());
}


void Assembler::db(uint8_t data) {
  CheckBuffer();
  *reinterpret_cast<uint8_t*>(pc_) = data;
  pc_ += sizeof(uint8_t);
}


void Assembler::dd(uint32_t data) {
  CheckBuffer();
  *reinterpret_cast<uint32_t*>(pc_) = data;
  pc_ += sizeof(uint32_t);
}


void Assembler::RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data) {
  RelocInfo rinfo(pc_, rmode, data);  // We do not try to reuse pool constants.
  if (rmode >= RelocInfo::JS_RETURN && rmode <= RelocInfo::DEBUG_BREAK_SLOT) {
    // Adjust code for new modes.
    ASSERT(RelocInfo::IsDebugBreakSlot(rmode)
           || RelocInfo::IsJSReturn(rmode)
           || RelocInfo::IsComment(rmode)
           || RelocInfo::IsPosition(rmode));
    // These modes do not need an entry in the constant pool.
  }
  if (rinfo.rmode() != RelocInfo::NONE) {
    // Don't record external references unless the heap will be serialized.
    if (rmode == RelocInfo::EXTERNAL_REFERENCE &&
        !Serializer::enabled() &&
        !FLAG_debug_code) {
      return;
    }
    ASSERT(buffer_space() >= kMaxRelocSize);  // Too late to grow buffer here.
    reloc_info_writer.Write(&rinfo);
  }
}


void Assembler::BlockTrampolinePoolFor(int instructions) {
  BlockTrampolinePoolBefore(pc_offset() + instructions * kInstrSize);
}


void Assembler::CheckTrampolinePool(bool force_emit) {
  // Calculate the offset of the next check.
  next_buffer_check_ = pc_offset() + kCheckConstInterval;

  int dist = pc_offset() - last_trampoline_pool_end_;

  if (dist <= kMaxDistBetweenPools && !force_emit) {
    return;
  }

  // Some small sequences of instructions must not be broken up by the
  // insertion of a trampoline pool; such sequences are protected by setting
  // either trampoline_pool_blocked_nesting_ or no_trampoline_pool_before_,
  // which are both checked here. Also, recursive calls to CheckTrampolinePool
  // are blocked by trampoline_pool_blocked_nesting_.
  if ((trampoline_pool_blocked_nesting_ > 0) ||
      (pc_offset() < no_trampoline_pool_before_)) {
    // Emission is currently blocked; make sure we try again as soon as
    // possible.
    if (trampoline_pool_blocked_nesting_ > 0) {
      next_buffer_check_ = pc_offset() + kInstrSize;
    } else {
      next_buffer_check_ = no_trampoline_pool_before_;
    }
    return;
  }

  // First we emit jump (2 instructions), then we emit trampoline pool.
  { BlockTrampolinePoolScope block_trampoline_pool(this);
    Label after_pool;
    b(&after_pool);
    nop();

    int pool_start = pc_offset();
    for (int i = 0; i < kSlotsPerTrampoline; i++) {
      b(&after_pool);
      nop();
    }
    for (int i = 0; i < kLabelsPerTrampoline; i++) {
      emit(0);
    }
    last_trampoline_pool_end_ = pc_offset() - kInstrSize;
    bind(&after_pool);
    trampolines_.Add(Trampoline(pool_start,
                                kSlotsPerTrampoline,
                                kLabelsPerTrampoline));

    // Since a trampoline pool was just emitted,
    // move the check offset forward by the standard interval.
    next_buffer_check_ = last_trampoline_pool_end_ + kMaxDistBetweenPools;
  }
  return;
}


Address Assembler::target_address_at(Address pc) {
  Instr instr1 = instr_at(pc);
  Instr instr2 = instr_at(pc + kInstrSize);
  // Check we have 2 instructions generated by li.
  ASSERT(((instr1 & kOpcodeMask) == LUI && (instr2 & kOpcodeMask) == ORI) ||
         ((instr1 == nopInstr) && ((instr2 & kOpcodeMask) == ADDI ||
                            (instr2 & kOpcodeMask) == ORI ||
                            (instr2 & kOpcodeMask) == LUI)));
  // Interpret these 2 instructions.
  if (instr1 == nopInstr) {
    if ((instr2 & kOpcodeMask) == ADDI) {
      return reinterpret_cast<Address>(((instr2 & kImm16Mask) << 16) >> 16);
    } else if ((instr2 & kOpcodeMask) == ORI) {
      return reinterpret_cast<Address>(instr2 & kImm16Mask);
    } else if ((instr2 & kOpcodeMask) == LUI) {
      return reinterpret_cast<Address>((instr2 & kImm16Mask) << 16);
    }
  } else if ((instr1 & kOpcodeMask) == LUI && (instr2 & kOpcodeMask) == ORI) {
    // 32 bit value.
    return reinterpret_cast<Address>(
        (instr1 & kImm16Mask) << 16 | (instr2 & kImm16Mask));
  }

  // We should never get here.
  UNREACHABLE();
  return (Address)0x0;
}


void Assembler::set_target_address_at(Address pc, Address target) {
  // On MIPS we need to patch the code to generate.

  // First check we have a li.
  Instr instr2 = instr_at(pc + kInstrSize);
#ifdef DEBUG
  Instr instr1 = instr_at(pc);

  // Check we have indeed the result from a li with MustUseReg true.
  CHECK(((instr1 & kOpcodeMask) == LUI && (instr2 & kOpcodeMask) == ORI) ||
        ((instr1 == 0) && ((instr2 & kOpcodeMask)== ADDIU ||
                           (instr2 & kOpcodeMask)== ORI ||
                           (instr2 & kOpcodeMask)== LUI)));
#endif

  uint32_t rt_code = (instr2 & kRtFieldMask);
  uint32_t* p = reinterpret_cast<uint32_t*>(pc);
  uint32_t itarget = reinterpret_cast<uint32_t>(target);

  if (is_int16(itarget)) {
    // nop.
    // addiu rt zero_reg j.
    *p = nopInstr;
    *(p+1) = ADDIU | rt_code | (itarget & kImm16Mask);
  } else if (!(itarget & kHiMask)) {
    // nop.
    // ori rt zero_reg j.
    *p = nopInstr;
    *(p+1) = ORI | rt_code | (itarget & kImm16Mask);
  } else if (!(itarget & kImm16Mask)) {
    // nop.
    // lui rt (kHiMask & itarget) >> kLuiShift.
    *p = nopInstr;
    *(p+1) = LUI | rt_code | ((itarget & kHiMask) >> kLuiShift);
  } else {
    // lui rt (kHiMask & itarget) >> kLuiShift.
    // ori rt rt, (kImm16Mask & itarget).
    *p = LUI | rt_code | ((itarget & kHiMask) >> kLuiShift);
    *(p+1) = ORI | rt_code | (rt_code << 5) | (itarget & kImm16Mask);
  }

  CPU::FlushICache(pc, 2 * sizeof(int32_t));
}


} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_MIPS
