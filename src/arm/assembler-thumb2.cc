// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the
// distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2010 the V8 project authors. All rights reserved.

#include "v8.h"

#include "arm/assembler-thumb2-inl.h"
#include "serialize.h"

namespace v8 {
namespace internal {

// Safe default is no features.
unsigned CpuFeatures::supported_ = 0;
unsigned CpuFeatures::enabled_ = 0;
unsigned CpuFeatures::found_by_runtime_probing_ = 0;

void CpuFeatures::Probe() {
  // If the compiler is allowed to use vfp then we can use vfp too in our
  // code generation.
#if !defined(__arm__)
  // For the simulator=arm build, use VFP when FLAG_enable_vfp3 is enabled.
  if (FLAG_enable_vfp3) {
      supported_ |= 1u << VFP3;
  }
  // For the simulator=arm build, use ARMv7 when FLAG_enable_armv7 is enabled
  if (FLAG_enable_armv7) {
      supported_ |= 1u << ARMv7;
  }
#else
  if (Serializer::enabled()) {
    supported_ |= OS::CpuFeaturesImpliedByPlatform();
    return;  // No features if we might serialize.
  }

  if (OS::ArmCpuHasFeature(VFP3)) {
    // This implementation also sets the VFP flags if
    // runtime detection of VFP returns true.
    supported_ |= 1u << VFP3;
    found_by_runtime_probing_ |= 1u << VFP3;
  }

  if (OS::ArmCpuHasFeature(ARMv7)) {
    supported_ |= 1u << ARMv7;
    found_by_runtime_probing_ |= 1u << ARMv7;
  }
#endif
}


// -----------------------------------------------------------------------------
// Implementation of Register and CRegister

Register no_reg = { -1 };

Register r0  = {  0 };
Register r1  = {  1 };
Register r2  = {  2 };
Register r3  = {  3 };
Register r4  = {  4 };
Register r5  = {  5 };
Register r6  = {  6 };
Register r7  = {  7 };
Register r8  = {  8 };  // Used as context register.
Register r9  = {  9 };
Register r10 = { 10 };  // Used as roots register.
Register fp  = { 11 };
Register ip  = { 12 };
Register sp  = { 13 };
Register lr  = { 14 };
Register pc  = { 15 };


CRegister no_creg = { -1 };

CRegister cr0  = {  0 };
CRegister cr1  = {  1 };
CRegister cr2  = {  2 };
CRegister cr3  = {  3 };
CRegister cr4  = {  4 };
CRegister cr5  = {  5 };
CRegister cr6  = {  6 };
CRegister cr7  = {  7 };
CRegister cr8  = {  8 };
CRegister cr9  = {  9 };
CRegister cr10 = { 10 };
CRegister cr11 = { 11 };
CRegister cr12 = { 12 };
CRegister cr13 = { 13 };
CRegister cr14 = { 14 };
CRegister cr15 = { 15 };

// Support for the VFP registers s0 to s31 (d0 to d15).
// Note that "sN:sM" is the same as "dN/2".
SwVfpRegister s0  = {  0 };
SwVfpRegister s1  = {  1 };
SwVfpRegister s2  = {  2 };
SwVfpRegister s3  = {  3 };
SwVfpRegister s4  = {  4 };
SwVfpRegister s5  = {  5 };
SwVfpRegister s6  = {  6 };
SwVfpRegister s7  = {  7 };
SwVfpRegister s8  = {  8 };
SwVfpRegister s9  = {  9 };
SwVfpRegister s10 = { 10 };
SwVfpRegister s11 = { 11 };
SwVfpRegister s12 = { 12 };
SwVfpRegister s13 = { 13 };
SwVfpRegister s14 = { 14 };
SwVfpRegister s15 = { 15 };
SwVfpRegister s16 = { 16 };
SwVfpRegister s17 = { 17 };
SwVfpRegister s18 = { 18 };
SwVfpRegister s19 = { 19 };
SwVfpRegister s20 = { 20 };
SwVfpRegister s21 = { 21 };
SwVfpRegister s22 = { 22 };
SwVfpRegister s23 = { 23 };
SwVfpRegister s24 = { 24 };
SwVfpRegister s25 = { 25 };
SwVfpRegister s26 = { 26 };
SwVfpRegister s27 = { 27 };
SwVfpRegister s28 = { 28 };
SwVfpRegister s29 = { 29 };
SwVfpRegister s30 = { 30 };
SwVfpRegister s31 = { 31 };

DwVfpRegister d0  = {  0 };
DwVfpRegister d1  = {  1 };
DwVfpRegister d2  = {  2 };
DwVfpRegister d3  = {  3 };
DwVfpRegister d4  = {  4 };
DwVfpRegister d5  = {  5 };
DwVfpRegister d6  = {  6 };
DwVfpRegister d7  = {  7 };
DwVfpRegister d8  = {  8 };
DwVfpRegister d9  = {  9 };
DwVfpRegister d10 = { 10 };
DwVfpRegister d11 = { 11 };
DwVfpRegister d12 = { 12 };
DwVfpRegister d13 = { 13 };
DwVfpRegister d14 = { 14 };
DwVfpRegister d15 = { 15 };

// -----------------------------------------------------------------------------
// Implementation of RelocInfo

const int RelocInfo::kApplyMask = 0;


void RelocInfo::PatchCode(byte* instructions, int instruction_count) {
  // Patch the code at the current address with the supplied instructions.
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
  UNIMPLEMENTED();
}


// -----------------------------------------------------------------------------
// Implementation of Operand and MemOperand
// See assembler-thumb2-inl.h for inlined constructors

Operand::Operand(Handle<Object> handle) {
  rm_ = no_reg;
  // Verify all Objects referred by code are NOT in new space.
  Object* obj = *handle;
  ASSERT(!Heap::InNewSpace(obj));
  if (obj->IsHeapObject()) {
    imm32_ = reinterpret_cast<intptr_t>(handle.location());
    rmode_ = RelocInfo::EMBEDDED_OBJECT;
  } else {
    // no relocation needed
    imm32_ =  reinterpret_cast<intptr_t>(obj);
    rmode_ = RelocInfo::NONE;
  }
}


Operand::Operand(Register rm, ShiftOp shift_op, int shift_imm) {
  ASSERT(is_uint5(shift_imm));
  ASSERT(shift_op != ROR || shift_imm != 0);  // use RRX if you mean it
  rm_ = rm;
  rs_ = no_reg;
  shift_op_ = shift_op;
  shift_imm_ = shift_imm & 31;
  if (shift_op == RRX) {
    // encoded as ROR with shift_imm == 0
    ASSERT(shift_imm == 0);
    shift_op_ = ROR;
    shift_imm_ = 0;
  }
}


Operand::Operand(Register rm, ShiftOp shift_op, Register rs) {
  ASSERT(shift_op != RRX);
  rm_ = rm;
  rs_ = no_reg;
  shift_op_ = shift_op;
  rs_ = rs;
}


MemOperand::MemOperand(Register rn, int32_t offset, AddrMode am) {
  rn_ = rn;
  rm_ = no_reg;
  offset_ = offset;
  am_ = am;
}

MemOperand::MemOperand(Register rn, Register rm, AddrMode am) {
  rn_ = rn;
  rm_ = rm;
  shift_op_ = LSL;
  shift_imm_ = 0;
  am_ = am;
}


MemOperand::MemOperand(Register rn, Register rm,
                       ShiftOp shift_op, int shift_imm, AddrMode am) {
  ASSERT(is_uint5(shift_imm));
  rn_ = rn;
  rm_ = rm;
  shift_op_ = shift_op;
  shift_imm_ = shift_imm & 31;
  am_ = am;
}


// -----------------------------------------------------------------------------
// Implementation of Assembler.

// Instruction encoding bits.
enum {
  H   = 1 << 5,   // halfword (or byte)
  S6  = 1 << 6,   // signed (or unsigned)
  L   = 1 << 20,  // load (or store)
  S   = 1 << 20,  // set condition code (or leave unchanged)
  W   = 1 << 21,  // writeback base register (or leave unchanged)
  A   = 1 << 21,  // accumulate in multiply instruction (or not)
  B   = 1 << 22,  // unsigned byte (or word)
  N   = 1 << 22,  // long (or short)
  U   = 1 << 23,  // positive (or negative) offset/index
  P   = 1 << 24,  // offset/pre-indexed addressing (or post-indexed addressing)
  I   = 1 << 25,  // immediate shifter operand (or not)

  B4  = 1 << 4,
  B5  = 1 << 5,
  B6  = 1 << 6,
  B7  = 1 << 7,
  B8  = 1 << 8,
  B9  = 1 << 9,
  B12 = 1 << 12,
  B16 = 1 << 16,
  B18 = 1 << 18,
  B19 = 1 << 19,
  B20 = 1 << 20,
  B21 = 1 << 21,
  B22 = 1 << 22,
  B23 = 1 << 23,
  B24 = 1 << 24,
  B25 = 1 << 25,
  B26 = 1 << 26,
  B27 = 1 << 27,

  // Instruction bit masks.
  RdMask     = 15 << 12,  // in str instruction
  CondMask   = 15 << 28,
  CoprocessorMask = 15 << 8,
  OpCodeMask = 15 << 21,  // in data-processing instructions
  Imm24Mask  = (1 << 24) - 1,
  Off12Mask  = (1 << 12) - 1,
  // Reserved condition.
  nv = 15 << 28
};


// add(sp, sp, 4) instruction (aka Pop())
static const Instr kPopInstruction =
    al | 4 * B21 | 4 | LeaveCC | I | sp.code() * B16 | sp.code() * B12;
// str(r, MemOperand(sp, 4, NegPreIndex), al) instruction (aka push(r))
// register r is not encoded.
static const Instr kPushRegPattern =
    al | B26 | 4 | NegPreIndex | sp.code() * B16;
// ldr(r, MemOperand(sp, 4, PostIndex), al) instruction (aka pop(r))
// register r is not encoded.
static const Instr kPopRegPattern =
    al | B26 | L | 4 | PostIndex | sp.code() * B16;
// mov lr, pc
const Instr kMovLrPc = al | 13*B21 | pc.code() | lr.code() * B12;
// ldr pc, [pc, #XXX]
const Instr kLdrPCPattern = al | B26 | L | pc.code() * B16;

// Spare buffer.
static const int kMinimalBufferSize = 4*KB;
static byte* spare_buffer_ = NULL;

Assembler::Assembler(void* buffer, int buffer_size) {
  if (buffer == NULL) {
    // Do our own buffer management.
    if (buffer_size <= kMinimalBufferSize) {
      buffer_size = kMinimalBufferSize;

      if (spare_buffer_ != NULL) {
        buffer = spare_buffer_;
        spare_buffer_ = NULL;
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
  num_prinfo_ = 0;
  next_buffer_check_ = 0;
  no_const_pool_before_ = 0;
  last_const_pool_end_ = 0;
  last_bound_pos_ = 0;
  current_statement_position_ = RelocInfo::kNoPosition;
  current_position_ = RelocInfo::kNoPosition;
  written_statement_position_ = current_statement_position_;
  written_position_ = current_position_;
}


Assembler::~Assembler() {
  if (own_buffer_) {
    if (spare_buffer_ == NULL && buffer_size_ == kMinimalBufferSize) {
      spare_buffer_ = buffer_;
    } else {
      DeleteArray(buffer_);
    }
  }
}


void Assembler::GetCode(CodeDesc* desc) {
  // Emit constant pool if necessary.
  CheckConstPool(true, false);
  ASSERT(num_prinfo_ == 0);

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


// Labels refer to positions in the (to be) generated code.
// There are bound, linked, and unused labels.
//
// Bound labels refer to known positions in the already
// generated code. pos() is the position the label refers to.
//
// Linked labels refer to unknown positions in the code
// to be generated; pos() is the position of the last
// instruction using the label.


// The link chain is terminated by a negative code position (must be aligned)
const int kEndOfChain = -4;


int Assembler::target_at(int pos)  {
  Instr instr = instr_at(pos);
  if ((instr & ~Imm24Mask) == 0) {
    // Emitted label constant, not part of a branch.
    return instr - (Code::kHeaderSize - kHeapObjectTag);
  }
  ASSERT((instr & 7*B25) == 5*B25);  // b, bl, or blx imm24
  int imm26 = ((instr & Imm24Mask) << 8) >> 6;
  if ((instr & CondMask) == nv && (instr & B24) != 0)
    // blx uses bit 24 to encode bit 2 of imm26
    imm26 += 2;

  return pos + kPcLoadDelta + imm26;
}


void Assembler::target_at_put(int pos, int target_pos) {
  Instr instr = instr_at(pos);
  if ((instr & ~Imm24Mask) == 0) {
    ASSERT(target_pos == kEndOfChain || target_pos >= 0);
    // Emitted label constant, not part of a branch.
    // Make label relative to Code* of generated Code object.
    instr_at_put(pos, target_pos + (Code::kHeaderSize - kHeapObjectTag));
    return;
  }
  int imm26 = target_pos - (pos + kPcLoadDelta);
  ASSERT((instr & 7*B25) == 5*B25);  // b, bl, or blx imm24
  if ((instr & CondMask) == nv) {
    // blx uses bit 24 to encode bit 2 of imm26
    ASSERT((imm26 & 1) == 0);
    instr = (instr & ~(B24 | Imm24Mask)) | ((imm26 & 2) >> 1)*B24;
  } else {
    ASSERT((imm26 & 3) == 0);
    instr &= ~Imm24Mask;
  }
  int imm24 = imm26 >> 2;
  ASSERT(is_int24(imm24));
  instr_at_put(pos, instr | (imm24 & Imm24Mask));
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
      if ((instr & ~Imm24Mask) == 0) {
        PrintF("value\n");
      } else {
        ASSERT((instr & 7*B25) == 5*B25);  // b, bl, or blx
        int cond = instr & CondMask;
        const char* b;
        const char* c;
        if (cond == nv) {
          b = "blx";
          c = "";
        } else {
          if ((instr & B24) != 0)
            b = "bl";
          else
            b = "b";

          switch (cond) {
            case eq: c = "eq"; break;
            case ne: c = "ne"; break;
            case hs: c = "hs"; break;
            case lo: c = "lo"; break;
            case mi: c = "mi"; break;
            case pl: c = "pl"; break;
            case vs: c = "vs"; break;
            case vc: c = "vc"; break;
            case hi: c = "hi"; break;
            case ls: c = "ls"; break;
            case ge: c = "ge"; break;
            case lt: c = "lt"; break;
            case gt: c = "gt"; break;
            case le: c = "le"; break;
            case al: c = ""; break;
            default:
              c = "";
              UNREACHABLE();
          }
        }
        PrintF("%s%s\n", b, c);
      }
      next(&l);
    }
  } else {
    PrintF("label in inconsistent state (pos = %d)\n", L->pos_);
  }
}


void Assembler::bind_to(Label* L, int pos) {
  ASSERT(0 <= pos && pos <= pc_offset());  // must have a valid binding position
  while (L->is_linked()) {
    int fixup_pos = L->pos();
    next(L);  // call next before overwriting link with target at fixup_pos
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
  appendix->Unuse();  // appendix should not be used anymore
}


void Assembler::bind(Label* L) {
  ASSERT(!L->is_bound());  // label can only be bound once
  bind_to(L, pc_offset());
}


void Assembler::next(Label* L) {
  ASSERT(L->is_linked());
  int link = target_at(L->pos());
  if (link > 0) {
    L->link_to(link);
  } else {
    ASSERT(link == kEndOfChain);
    L->Unuse();
  }
}


// Low-level code emission routines depending on the addressing mode.
static bool fits_shifter(uint32_t imm32,
                         uint32_t* rotate_imm,
                         uint32_t* immed_8,
                         Instr* instr) {
  // imm32 must be unsigned.
  for (int rot = 0; rot < 16; rot++) {
    uint32_t imm8 = (imm32 << 2*rot) | (imm32 >> (32 - 2*rot));
    if ((imm8 <= 0xff)) {
      *rotate_imm = rot;
      *immed_8 = imm8;
      return true;
    }
  }
  // If the opcode is mov or mvn and if ~imm32 fits, change the opcode.
  if (instr != NULL && (*instr & 0xd*B21) == 0xd*B21) {
    if (fits_shifter(~imm32, rotate_imm, immed_8, NULL)) {
      *instr ^= 0x2*B21;
      return true;
    }
  }
  return false;
}


// We have to use the temporary register for things that can be relocated even
// if they can be encoded in the ARM's 12 bits of immediate-offset instruction
// space.  There is no guarantee that the relocated location can be similarly
// encoded.
static bool MustUseIp(RelocInfo::Mode rmode) {
  if (rmode == RelocInfo::EXTERNAL_REFERENCE) {
#ifdef DEBUG
    if (!Serializer::enabled()) {
      Serializer::TooLateToEnableNow();
    }
#endif
    return Serializer::enabled();
  } else if (rmode == RelocInfo::NONE) {
    return false;
  }
  return true;
}


void Assembler::addrmod1(Instr instr,
                         Register rn,
                         Register rd,
                         const Operand& x) {
  CheckBuffer();
  ASSERT((instr & ~(CondMask | OpCodeMask | S)) == 0);
  if (!x.rm_.is_valid()) {
    // Immediate.
    uint32_t rotate_imm;
    uint32_t immed_8;
    if (MustUseIp(x.rmode_) ||
        !fits_shifter(x.imm32_, &rotate_imm, &immed_8, &instr)) {
      // The immediate operand cannot be encoded as a shifter operand, so load
      // it first to register ip and change the original instruction to use ip.
      // However, if the original instruction is a 'mov rd, x' (not setting the
      // condition code), then replace it with a 'ldr rd, [pc]'.
      RecordRelocInfo(x.rmode_, x.imm32_);
      CHECK(!rn.is(ip));  // rn should never be ip, or will be trashed
      Condition cond = static_cast<Condition>(instr & CondMask);
      if ((instr & ~CondMask) == 13*B21) {  // mov, S not set
        ldr(rd, MemOperand(pc, 0), cond);
      } else {
        ldr(ip, MemOperand(pc, 0), cond);
        addrmod1(instr, rn, rd, Operand(ip));
      }
      return;
    }
    instr |= I | rotate_imm*B8 | immed_8;
  } else if (!x.rs_.is_valid()) {
    // Immediate shift.
    instr |= x.shift_imm_*B7 | x.shift_op_ | x.rm_.code();
  } else {
    // Register shift.
    ASSERT(!rn.is(pc) && !rd.is(pc) && !x.rm_.is(pc) && !x.rs_.is(pc));
    instr |= x.rs_.code()*B8 | x.shift_op_ | B4 | x.rm_.code();
  }
  emit(instr | rn.code()*B16 | rd.code()*B12);
  if (rn.is(pc) || x.rm_.is(pc))
    // Block constant pool emission for one instruction after reading pc.
    BlockConstPoolBefore(pc_offset() + kInstrSize);
}


void Assembler::addrmod2(Instr instr, Register rd, const MemOperand& x) {
  ASSERT((instr & ~(CondMask | B | L)) == B26);
  int am = x.am_;
  if (!x.rm_.is_valid()) {
    // Immediate offset.
    int offset_12 = x.offset_;
    if (offset_12 < 0) {
      offset_12 = -offset_12;
      am ^= U;
    }
    if (!is_uint12(offset_12)) {
      // Immediate offset cannot be encoded, load it first to register ip
      // rn (and rd in a load) should never be ip, or will be trashed.
      ASSERT(!x.rn_.is(ip) && ((instr & L) == L || !rd.is(ip)));
      mov(ip, Operand(x.offset_), LeaveCC,
          static_cast<Condition>(instr & CondMask));
      addrmod2(instr, rd, MemOperand(x.rn_, ip, x.am_));
      return;
    }
    ASSERT(offset_12 >= 0);  // no masking needed
    instr |= offset_12;
  } else {
    // Register offset (shift_imm_ and shift_op_ are 0) or scaled
    // register offset the constructors make sure than both shift_imm_
    // and shift_op_ are initialized.
    ASSERT(!x.rm_.is(pc));
    instr |= B25 | x.shift_imm_*B7 | x.shift_op_ | x.rm_.code();
  }
  ASSERT((am & (P|W)) == P || !x.rn_.is(pc));  // no pc base with writeback
  emit(instr | am | x.rn_.code()*B16 | rd.code()*B12);
}


void Assembler::addrmod3(Instr instr, Register rd, const MemOperand& x) {
  ASSERT((instr & ~(CondMask | L | S6 | H)) == (B4 | B7));
  ASSERT(x.rn_.is_valid());
  int am = x.am_;
  if (!x.rm_.is_valid()) {
    // Immediate offset.
    int offset_8 = x.offset_;
    if (offset_8 < 0) {
      offset_8 = -offset_8;
      am ^= U;
    }
    if (!is_uint8(offset_8)) {
      // Immediate offset cannot be encoded, load it first to register ip
      // rn (and rd in a load) should never be ip, or will be trashed.
      ASSERT(!x.rn_.is(ip) && ((instr & L) == L || !rd.is(ip)));
      mov(ip, Operand(x.offset_), LeaveCC,
          static_cast<Condition>(instr & CondMask));
      addrmod3(instr, rd, MemOperand(x.rn_, ip, x.am_));
      return;
    }
    ASSERT(offset_8 >= 0);  // no masking needed
    instr |= B | (offset_8 >> 4)*B8 | (offset_8 & 0xf);
  } else if (x.shift_imm_ != 0) {
    // Scaled register offset not supported, load index first
    // rn (and rd in a load) should never be ip, or will be trashed.
    ASSERT(!x.rn_.is(ip) && ((instr & L) == L || !rd.is(ip)));
    mov(ip, Operand(x.rm_, x.shift_op_, x.shift_imm_), LeaveCC,
        static_cast<Condition>(instr & CondMask));
    addrmod3(instr, rd, MemOperand(x.rn_, ip, x.am_));
    return;
  } else {
    // Register offset.
    ASSERT((am & (P|W)) == P || !x.rm_.is(pc));  // no pc index with writeback
    instr |= x.rm_.code();
  }
  ASSERT((am & (P|W)) == P || !x.rn_.is(pc));  // no pc base with writeback
  emit(instr | am | x.rn_.code()*B16 | rd.code()*B12);
}


void Assembler::addrmod4(Instr instr, Register rn, RegList rl) {
  ASSERT((instr & ~(CondMask | P | U | W | L)) == B27);
  ASSERT(rl != 0);
  ASSERT(!rn.is(pc));
  emit(instr | rn.code()*B16 | rl);
}


void Assembler::addrmod5(Instr instr, CRegister crd, const MemOperand& x) {
  // Unindexed addressing is not encoded by this function.
  ASSERT_EQ((B27 | B26),
            (instr & ~(CondMask | CoprocessorMask | P | U | N | W | L)));
  ASSERT(x.rn_.is_valid() && !x.rm_.is_valid());
  int am = x.am_;
  int offset_8 = x.offset_;
  ASSERT((offset_8 & 3) == 0);  // offset must be an aligned word offset
  offset_8 >>= 2;
  if (offset_8 < 0) {
    offset_8 = -offset_8;
    am ^= U;
  }
  ASSERT(is_uint8(offset_8));  // unsigned word offset must fit in a byte
  ASSERT((am & (P|W)) == P || !x.rn_.is(pc));  // no pc base with writeback

  // Post-indexed addressing requires W == 1; different than in addrmod2/3.
  if ((am & P) == 0)
    am |= W;

  ASSERT(offset_8 >= 0);  // no masking needed
  emit(instr | am | x.rn_.code()*B16 | crd.code()*B12 | offset_8);
}


int Assembler::branch_offset(Label* L, bool jump_elimination_allowed) {
  int target_pos;
  if (L->is_bound()) {
    target_pos = L->pos();
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link
    } else {
      target_pos = kEndOfChain;
    }
    L->link_to(pc_offset());
  }

  // Block the emission of the constant pool, since the branch instruction must
  // be emitted at the pc offset recorded by the label.
  BlockConstPoolBefore(pc_offset() + kInstrSize);
  return target_pos - (pc_offset() + kPcLoadDelta);
}


void Assembler::label_at_put(Label* L, int at_offset) {
  int target_pos;
  if (L->is_bound()) {
    target_pos = L->pos();
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link
    } else {
      target_pos = kEndOfChain;
    }
    L->link_to(at_offset);
    instr_at_put(at_offset, target_pos + (Code::kHeaderSize - kHeapObjectTag));
  }
}


// Branch instructions.
void Assembler::b(int branch_offset, Condition cond) {
  ASSERT((branch_offset & 3) == 0);
  int imm24 = branch_offset >> 2;
  ASSERT(is_int24(imm24));
  emit(cond | B27 | B25 | (imm24 & Imm24Mask));

  if (cond == al)
    // Dead code is a good location to emit the constant pool.
    CheckConstPool(false, false);
}


void Assembler::bl(int branch_offset, Condition cond) {
  ASSERT((branch_offset & 3) == 0);
  int imm24 = branch_offset >> 2;
  ASSERT(is_int24(imm24));
  emit(cond | B27 | B25 | B24 | (imm24 & Imm24Mask));
}


void Assembler::blx(int branch_offset) {  // v5 and above
  WriteRecordedPositions();
  ASSERT((branch_offset & 1) == 0);
  int h = ((branch_offset & 2) >> 1)*B24;
  int imm24 = branch_offset >> 2;
  ASSERT(is_int24(imm24));
  emit(15 << 28 | B27 | B25 | h | (imm24 & Imm24Mask));
}


void Assembler::blx(Register target, Condition cond) {  // v5 and above
  WriteRecordedPositions();
  ASSERT(!target.is(pc));
  emit(cond | B24 | B21 | 15*B16 | 15*B12 | 15*B8 | 3*B4 | target.code());
}


void Assembler::bx(Register target, Condition cond) {  // v5 and above, plus v4t
  WriteRecordedPositions();
  ASSERT(!target.is(pc));  // use of pc is actually allowed, but discouraged
  emit(cond | B24 | B21 | 15*B16 | 15*B12 | 15*B8 | B4 | target.code());
}


// Data-processing instructions.

// UBFX <Rd>,<Rn>,#<lsb>,#<width - 1>
// Instruction details available in ARM DDI 0406A, A8-464.
// cond(31-28) | 01111(27-23)| 1(22) | 1(21) | widthm1(20-16) |
//  Rd(15-12) | lsb(11-7) | 101(6-4) | Rn(3-0)
void Assembler::ubfx(Register dst, Register src1, const Operand& src2,
                     const Operand& src3, Condition cond) {
  ASSERT(!src2.rm_.is_valid() && !src3.rm_.is_valid());
  ASSERT(static_cast<uint32_t>(src2.imm32_) <= 0x1f);
  ASSERT(static_cast<uint32_t>(src3.imm32_) <= 0x1f);
  emit(cond | 0x3F*B21 | src3.imm32_*B16 |
       dst.code()*B12 | src2.imm32_*B7 | 0x5*B4 | src1.code());
}


void Assembler::and_(Register dst, Register src1, const Operand& src2,
                     SBit s, Condition cond) {
  addrmod1(cond | 0*B21 | s, src1, dst, src2);
}


void Assembler::eor(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  addrmod1(cond | 1*B21 | s, src1, dst, src2);
}


void Assembler::sub(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  addrmod1(cond | 2*B21 | s, src1, dst, src2);
}


void Assembler::rsb(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  addrmod1(cond | 3*B21 | s, src1, dst, src2);
}


void Assembler::add(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  addrmod1(cond | 4*B21 | s, src1, dst, src2);

  // Eliminate pattern: push(r), pop()
  //   str(src, MemOperand(sp, 4, NegPreIndex), al);
  //   add(sp, sp, Operand(kPointerSize));
  // Both instructions can be eliminated.
  int pattern_size = 2 * kInstrSize;
  if (FLAG_push_pop_elimination &&
      last_bound_pos_ <= (pc_offset() - pattern_size) &&
      reloc_info_writer.last_pc() <= (pc_ - pattern_size) &&
      // Pattern.
      instr_at(pc_ - 1 * kInstrSize) == kPopInstruction &&
      (instr_at(pc_ - 2 * kInstrSize) & ~RdMask) == kPushRegPattern) {
    pc_ -= 2 * kInstrSize;
    if (FLAG_print_push_pop_elimination) {
      PrintF("%x push(reg)/pop() eliminated\n", pc_offset());
    }
  }
}


void Assembler::adc(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  addrmod1(cond | 5*B21 | s, src1, dst, src2);
}


void Assembler::sbc(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  addrmod1(cond | 6*B21 | s, src1, dst, src2);
}


void Assembler::rsc(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  addrmod1(cond | 7*B21 | s, src1, dst, src2);
}


void Assembler::tst(Register src1, const Operand& src2, Condition cond) {
  addrmod1(cond | 8*B21 | S, src1, r0, src2);
}


void Assembler::teq(Register src1, const Operand& src2, Condition cond) {
  addrmod1(cond | 9*B21 | S, src1, r0, src2);
}


void Assembler::cmp(Register src1, const Operand& src2, Condition cond) {
  addrmod1(cond | 10*B21 | S, src1, r0, src2);
}


void Assembler::cmn(Register src1, const Operand& src2, Condition cond) {
  addrmod1(cond | 11*B21 | S, src1, r0, src2);
}


void Assembler::orr(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  addrmod1(cond | 12*B21 | s, src1, dst, src2);
}


void Assembler::mov(Register dst, const Operand& src, SBit s, Condition cond) {
  if (dst.is(pc)) {
    WriteRecordedPositions();
  }
  addrmod1(cond | 13*B21 | s, r0, dst, src);
}


void Assembler::bic(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  addrmod1(cond | 14*B21 | s, src1, dst, src2);
}


void Assembler::mvn(Register dst, const Operand& src, SBit s, Condition cond) {
  addrmod1(cond | 15*B21 | s, r0, dst, src);
}


// Multiply instructions.
void Assembler::mla(Register dst, Register src1, Register src2, Register srcA,
                    SBit s, Condition cond) {
  ASSERT(!dst.is(pc) && !src1.is(pc) && !src2.is(pc) && !srcA.is(pc));
  emit(cond | A | s | dst.code()*B16 | srcA.code()*B12 |
       src2.code()*B8 | B7 | B4 | src1.code());
}


void Assembler::mul(Register dst, Register src1, Register src2,
                    SBit s, Condition cond) {
  ASSERT(!dst.is(pc) && !src1.is(pc) && !src2.is(pc));
  // dst goes in bits 16-19 for this instruction!
  emit(cond | s | dst.code()*B16 | src2.code()*B8 | B7 | B4 | src1.code());
}


void Assembler::smlal(Register dstL,
                      Register dstH,
                      Register src1,
                      Register src2,
                      SBit s,
                      Condition cond) {
  ASSERT(!dstL.is(pc) && !dstH.is(pc) && !src1.is(pc) && !src2.is(pc));
  ASSERT(!dstL.is(dstH));
  emit(cond | B23 | B22 | A | s | dstH.code()*B16 | dstL.code()*B12 |
       src2.code()*B8 | B7 | B4 | src1.code());
}


void Assembler::smull(Register dstL,
                      Register dstH,
                      Register src1,
                      Register src2,
                      SBit s,
                      Condition cond) {
  ASSERT(!dstL.is(pc) && !dstH.is(pc) && !src1.is(pc) && !src2.is(pc));
  ASSERT(!dstL.is(dstH));
  emit(cond | B23 | B22 | s | dstH.code()*B16 | dstL.code()*B12 |
       src2.code()*B8 | B7 | B4 | src1.code());
}


void Assembler::umlal(Register dstL,
                      Register dstH,
                      Register src1,
                      Register src2,
                      SBit s,
                      Condition cond) {
  ASSERT(!dstL.is(pc) && !dstH.is(pc) && !src1.is(pc) && !src2.is(pc));
  ASSERT(!dstL.is(dstH));
  emit(cond | B23 | A | s | dstH.code()*B16 | dstL.code()*B12 |
       src2.code()*B8 | B7 | B4 | src1.code());
}


void Assembler::umull(Register dstL,
                      Register dstH,
                      Register src1,
                      Register src2,
                      SBit s,
                      Condition cond) {
  ASSERT(!dstL.is(pc) && !dstH.is(pc) && !src1.is(pc) && !src2.is(pc));
  ASSERT(!dstL.is(dstH));
  emit(cond | B23 | s | dstH.code()*B16 | dstL.code()*B12 |
       src2.code()*B8 | B7 | B4 | src1.code());
}


// Miscellaneous arithmetic instructions.
void Assembler::clz(Register dst, Register src, Condition cond) {
  // v5 and above.
  ASSERT(!dst.is(pc) && !src.is(pc));
  emit(cond | B24 | B22 | B21 | 15*B16 | dst.code()*B12 |
       15*B8 | B4 | src.code());
}


// Status register access instructions.
void Assembler::mrs(Register dst, SRegister s, Condition cond) {
  ASSERT(!dst.is(pc));
  emit(cond | B24 | s | 15*B16 | dst.code()*B12);
}


void Assembler::msr(SRegisterFieldMask fields, const Operand& src,
                    Condition cond) {
  ASSERT(fields >= B16 && fields < B20);  // at least one field set
  Instr instr;
  if (!src.rm_.is_valid()) {
    // Immediate.
    uint32_t rotate_imm;
    uint32_t immed_8;
    if (MustUseIp(src.rmode_) ||
        !fits_shifter(src.imm32_, &rotate_imm, &immed_8, NULL)) {
      // Immediate operand cannot be encoded, load it first to register ip.
      RecordRelocInfo(src.rmode_, src.imm32_);
      ldr(ip, MemOperand(pc, 0), cond);
      msr(fields, Operand(ip), cond);
      return;
    }
    instr = I | rotate_imm*B8 | immed_8;
  } else {
    ASSERT(!src.rs_.is_valid() && src.shift_imm_ == 0);  // only rm allowed
    instr = src.rm_.code();
  }
  emit(cond | instr | B24 | B21 | fields | 15*B12);
}


// Load/Store instructions.
void Assembler::ldr(Register dst, const MemOperand& src, Condition cond) {
  if (dst.is(pc)) {
    WriteRecordedPositions();
  }
  addrmod2(cond | B26 | L, dst, src);

  // Eliminate pattern: push(r), pop(r)
  //   str(r, MemOperand(sp, 4, NegPreIndex), al)
  //   ldr(r, MemOperand(sp, 4, PostIndex), al)
  // Both instructions can be eliminated.
  int pattern_size = 2 * kInstrSize;
  if (FLAG_push_pop_elimination &&
      last_bound_pos_ <= (pc_offset() - pattern_size) &&
      reloc_info_writer.last_pc() <= (pc_ - pattern_size) &&
      // Pattern.
      instr_at(pc_ - 1 * kInstrSize) == (kPopRegPattern | dst.code() * B12) &&
      instr_at(pc_ - 2 * kInstrSize) == (kPushRegPattern | dst.code() * B12)) {
    pc_ -= 2 * kInstrSize;
    if (FLAG_print_push_pop_elimination) {
      PrintF("%x push/pop (same reg) eliminated\n", pc_offset());
    }
  }
}


void Assembler::str(Register src, const MemOperand& dst, Condition cond) {
  addrmod2(cond | B26, src, dst);

  // Eliminate pattern: pop(), push(r)
  //     add sp, sp, #4 LeaveCC, al; str r, [sp, #-4], al
  // ->  str r, [sp, 0], al
  int pattern_size = 2 * kInstrSize;
  if (FLAG_push_pop_elimination &&
     last_bound_pos_ <= (pc_offset() - pattern_size) &&
     reloc_info_writer.last_pc() <= (pc_ - pattern_size) &&
     // Pattern.
     instr_at(pc_ - 1 * kInstrSize) == (kPushRegPattern | src.code() * B12) &&
     instr_at(pc_ - 2 * kInstrSize) == kPopInstruction) {
    pc_ -= 2 * kInstrSize;
    emit(al | B26 | 0 | Offset | sp.code() * B16 | src.code() * B12);
    if (FLAG_print_push_pop_elimination) {
      PrintF("%x pop()/push(reg) eliminated\n", pc_offset());
    }
  }
}


void Assembler::ldrb(Register dst, const MemOperand& src, Condition cond) {
  addrmod2(cond | B26 | B | L, dst, src);
}


void Assembler::strb(Register src, const MemOperand& dst, Condition cond) {
  addrmod2(cond | B26 | B, src, dst);
}


void Assembler::ldrh(Register dst, const MemOperand& src, Condition cond) {
  addrmod3(cond | L | B7 | H | B4, dst, src);
}


void Assembler::strh(Register src, const MemOperand& dst, Condition cond) {
  addrmod3(cond | B7 | H | B4, src, dst);
}


void Assembler::ldrsb(Register dst, const MemOperand& src, Condition cond) {
  addrmod3(cond | L | B7 | S6 | B4, dst, src);
}


void Assembler::ldrsh(Register dst, const MemOperand& src, Condition cond) {
  addrmod3(cond | L | B7 | S6 | H | B4, dst, src);
}


// Load/Store multiple instructions.
void Assembler::ldm(BlockAddrMode am,
                    Register base,
                    RegList dst,
                    Condition cond) {
  // ABI stack constraint: ldmxx base, {..sp..}  base != sp  is not restartable.
  ASSERT(base.is(sp) || (dst & sp.bit()) == 0);

  addrmod4(cond | B27 | am | L, base, dst);

  // Emit the constant pool after a function return implemented by ldm ..{..pc}.
  if (cond == al && (dst & pc.bit()) != 0) {
    // There is a slight chance that the ldm instruction was actually a call,
    // in which case it would be wrong to return into the constant pool; we
    // recognize this case by checking if the emission of the pool was blocked
    // at the pc of the ldm instruction by a mov lr, pc instruction; if this is
    // the case, we emit a jump over the pool.
    CheckConstPool(true, no_const_pool_before_ == pc_offset() - kInstrSize);
  }
}


void Assembler::stm(BlockAddrMode am,
                    Register base,
                    RegList src,
                    Condition cond) {
  addrmod4(cond | B27 | am, base, src);
}


// Semaphore instructions.
void Assembler::swp(Register dst, Register src, Register base, Condition cond) {
  ASSERT(!dst.is(pc) && !src.is(pc) && !base.is(pc));
  ASSERT(!dst.is(base) && !src.is(base));
  emit(cond | P | base.code()*B16 | dst.code()*B12 |
       B7 | B4 | src.code());
}


void Assembler::swpb(Register dst,
                     Register src,
                     Register base,
                     Condition cond) {
  ASSERT(!dst.is(pc) && !src.is(pc) && !base.is(pc));
  ASSERT(!dst.is(base) && !src.is(base));
  emit(cond | P | B | base.code()*B16 | dst.code()*B12 |
       B7 | B4 | src.code());
}


// Exception-generating instructions and debugging support.
void Assembler::stop(const char* msg) {
#if !defined(__arm__)
  // The simulator handles these special instructions and stops execution.
  emit(15 << 28 | ((intptr_t) msg));
#else
  // Just issue a simple break instruction for now. Alternatively we could use
  // the swi(0x9f0001) instruction on Linux.
  bkpt(0);
#endif
}


void Assembler::bkpt(uint32_t imm16) {  // v5 and above
  ASSERT(is_uint16(imm16));
  emit(al | B24 | B21 | (imm16 >> 4)*B8 | 7*B4 | (imm16 & 0xf));
}


void Assembler::swi(uint32_t imm24, Condition cond) {
  ASSERT(is_uint24(imm24));
  emit(cond | 15*B24 | imm24);
}


// Coprocessor instructions.
void Assembler::cdp(Coprocessor coproc,
                    int opcode_1,
                    CRegister crd,
                    CRegister crn,
                    CRegister crm,
                    int opcode_2,
                    Condition cond) {
  ASSERT(is_uint4(opcode_1) && is_uint3(opcode_2));
  emit(cond | B27 | B26 | B25 | (opcode_1 & 15)*B20 | crn.code()*B16 |
       crd.code()*B12 | coproc*B8 | (opcode_2 & 7)*B5 | crm.code());
}


void Assembler::cdp2(Coprocessor coproc,
                     int opcode_1,
                     CRegister crd,
                     CRegister crn,
                     CRegister crm,
                     int opcode_2) {  // v5 and above
  cdp(coproc, opcode_1, crd, crn, crm, opcode_2, static_cast<Condition>(nv));
}


void Assembler::mcr(Coprocessor coproc,
                    int opcode_1,
                    Register rd,
                    CRegister crn,
                    CRegister crm,
                    int opcode_2,
                    Condition cond) {
  ASSERT(is_uint3(opcode_1) && is_uint3(opcode_2));
  emit(cond | B27 | B26 | B25 | (opcode_1 & 7)*B21 | crn.code()*B16 |
       rd.code()*B12 | coproc*B8 | (opcode_2 & 7)*B5 | B4 | crm.code());
}


void Assembler::mcr2(Coprocessor coproc,
                     int opcode_1,
                     Register rd,
                     CRegister crn,
                     CRegister crm,
                     int opcode_2) {  // v5 and above
  mcr(coproc, opcode_1, rd, crn, crm, opcode_2, static_cast<Condition>(nv));
}


void Assembler::mrc(Coprocessor coproc,
                    int opcode_1,
                    Register rd,
                    CRegister crn,
                    CRegister crm,
                    int opcode_2,
                    Condition cond) {
  ASSERT(is_uint3(opcode_1) && is_uint3(opcode_2));
  emit(cond | B27 | B26 | B25 | (opcode_1 & 7)*B21 | L | crn.code()*B16 |
       rd.code()*B12 | coproc*B8 | (opcode_2 & 7)*B5 | B4 | crm.code());
}


void Assembler::mrc2(Coprocessor coproc,
                     int opcode_1,
                     Register rd,
                     CRegister crn,
                     CRegister crm,
                     int opcode_2) {  // v5 and above
  mrc(coproc, opcode_1, rd, crn, crm, opcode_2, static_cast<Condition>(nv));
}


void Assembler::ldc(Coprocessor coproc,
                    CRegister crd,
                    const MemOperand& src,
                    LFlag l,
                    Condition cond) {
  addrmod5(cond | B27 | B26 | l | L | coproc*B8, crd, src);
}


void Assembler::ldc(Coprocessor coproc,
                    CRegister crd,
                    Register rn,
                    int option,
                    LFlag l,
                    Condition cond) {
  // Unindexed addressing.
  ASSERT(is_uint8(option));
  emit(cond | B27 | B26 | U | l | L | rn.code()*B16 | crd.code()*B12 |
       coproc*B8 | (option & 255));
}


void Assembler::ldc2(Coprocessor coproc,
                     CRegister crd,
                     const MemOperand& src,
                     LFlag l) {  // v5 and above
  ldc(coproc, crd, src, l, static_cast<Condition>(nv));
}


void Assembler::ldc2(Coprocessor coproc,
                     CRegister crd,
                     Register rn,
                     int option,
                     LFlag l) {  // v5 and above
  ldc(coproc, crd, rn, option, l, static_cast<Condition>(nv));
}


void Assembler::stc(Coprocessor coproc,
                    CRegister crd,
                    const MemOperand& dst,
                    LFlag l,
                    Condition cond) {
  addrmod5(cond | B27 | B26 | l | coproc*B8, crd, dst);
}


void Assembler::stc(Coprocessor coproc,
                    CRegister crd,
                    Register rn,
                    int option,
                    LFlag l,
                    Condition cond) {
  // Unindexed addressing.
  ASSERT(is_uint8(option));
  emit(cond | B27 | B26 | U | l | rn.code()*B16 | crd.code()*B12 |
       coproc*B8 | (option & 255));
}


void Assembler::stc2(Coprocessor
                     coproc, CRegister crd,
                     const MemOperand& dst,
                     LFlag l) {  // v5 and above
  stc(coproc, crd, dst, l, static_cast<Condition>(nv));
}


void Assembler::stc2(Coprocessor coproc,
                     CRegister crd,
                     Register rn,
                     int option,
                     LFlag l) {  // v5 and above
  stc(coproc, crd, rn, option, l, static_cast<Condition>(nv));
}


// Support for VFP.
void Assembler::vldr(const DwVfpRegister dst,
                     const Register base,
                     int offset,
                     const Condition cond) {
  // Ddst = MEM(Rbase + offset).
  // Instruction details available in ARM DDI 0406A, A8-628.
  // cond(31-28) | 1101(27-24)| 1001(23-20) | Rbase(19-16) |
  // Vdst(15-12) | 1011(11-8) | offset
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  ASSERT(offset % 4 == 0);
  emit(cond | 0xD9*B20 | base.code()*B16 | dst.code()*B12 |
       0xB*B8 | ((offset / 4) & 255));
}


void Assembler::vstr(const DwVfpRegister src,
                     const Register base,
                     int offset,
                     const Condition cond) {
  // MEM(Rbase + offset) = Dsrc.
  // Instruction details available in ARM DDI 0406A, A8-786.
  // cond(31-28) | 1101(27-24)| 1000(23-20) | | Rbase(19-16) |
  // Vsrc(15-12) | 1011(11-8) | (offset/4)
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  ASSERT(offset % 4 == 0);
  emit(cond | 0xD8*B20 | base.code()*B16 | src.code()*B12 |
       0xB*B8 | ((offset / 4) & 255));
}


void Assembler::vmov(const DwVfpRegister dst,
                     const Register src1,
                     const Register src2,
                     const Condition cond) {
  // Dm = <Rt,Rt2>.
  // Instruction details available in ARM DDI 0406A, A8-646.
  // cond(31-28) | 1100(27-24)| 010(23-21) | op=0(20) | Rt2(19-16) |
  // Rt(15-12) | 1011(11-8) | 00(7-6) | M(5) | 1(4) | Vm
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  ASSERT(!src1.is(pc) && !src2.is(pc));
  emit(cond | 0xC*B24 | B22 | src2.code()*B16 |
       src1.code()*B12 | 0xB*B8 | B4 | dst.code());
}


void Assembler::vmov(const Register dst1,
                     const Register dst2,
                     const DwVfpRegister src,
                     const Condition cond) {
  // <Rt,Rt2> = Dm.
  // Instruction details available in ARM DDI 0406A, A8-646.
  // cond(31-28) | 1100(27-24)| 010(23-21) | op=1(20) | Rt2(19-16) |
  // Rt(15-12) | 1011(11-8) | 00(7-6) | M(5) | 1(4) | Vm
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  ASSERT(!dst1.is(pc) && !dst2.is(pc));
  emit(cond | 0xC*B24 | B22 | B20 | dst2.code()*B16 |
       dst1.code()*B12 | 0xB*B8 | B4 | src.code());
}


void Assembler::vmov(const SwVfpRegister dst,
                     const Register src,
                     const Condition cond) {
  // Sn = Rt.
  // Instruction details available in ARM DDI 0406A, A8-642.
  // cond(31-28) | 1110(27-24)| 000(23-21) | op=0(20) | Vn(19-16) |
  // Rt(15-12) | 1010(11-8) | N(7)=0 | 00(6-5) | 1(4) | 0000(3-0)
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  ASSERT(!src.is(pc));
  emit(cond | 0xE*B24 | (dst.code() >> 1)*B16 |
       src.code()*B12 | 0xA*B8 | (0x1 & dst.code())*B7 | B4);
}


void Assembler::vmov(const Register dst,
                     const SwVfpRegister src,
                     const Condition cond) {
  // Rt = Sn.
  // Instruction details available in ARM DDI 0406A, A8-642.
  // cond(31-28) | 1110(27-24)| 000(23-21) | op=1(20) | Vn(19-16) |
  // Rt(15-12) | 1010(11-8) | N(7)=0 | 00(6-5) | 1(4) | 0000(3-0)
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  ASSERT(!dst.is(pc));
  emit(cond | 0xE*B24 | B20 | (src.code() >> 1)*B16 |
       dst.code()*B12 | 0xA*B8 | (0x1 & src.code())*B7 | B4);
}


void Assembler::vcvt(const DwVfpRegister dst,
                     const SwVfpRegister src,
                     const Condition cond) {
  // Dd = Sm (integer in Sm converted to IEEE 64-bit doubles in Dd).
  // Instruction details available in ARM DDI 0406A, A8-576.
  // cond(31-28) | 11101(27-23)| D=?(22) | 11(21-20) | 1(19) | opc2=000(18-16) |
  // Vd(15-12) | 101(11-9) | sz(8)=1 | op(7)=1 | 1(6) | M=?(5) | 0(4) | Vm(3-0)
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  emit(cond | 0xE*B24 | B23 | 0x3*B20 | B19 |
       dst.code()*B12 | 0x5*B9 | B8 | B7 | B6 |
       (0x1 & src.code())*B5 | (src.code() >> 1));
}


void Assembler::vcvt(const SwVfpRegister dst,
                     const DwVfpRegister src,
                     const Condition cond) {
  // Sd = Dm (IEEE 64-bit doubles in Dm converted to 32 bit integer in Sd).
  // Instruction details available in ARM DDI 0406A, A8-576.
  // cond(31-28) | 11101(27-23)| D=?(22) | 11(21-20) | 1(19) | opc2=101(18-16)|
  // Vd(15-12) | 101(11-9) | sz(8)=1 | op(7)=? | 1(6) | M=?(5) | 0(4) | Vm(3-0)
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  emit(cond | 0xE*B24 | B23 |(0x1 & dst.code())*B22 |
       0x3*B20 | B19 | 0x5*B16 | (dst.code() >> 1)*B12 |
       0x5*B9 | B8 | B7 | B6 | src.code());
}


void Assembler::vadd(const DwVfpRegister dst,
                     const DwVfpRegister src1,
                     const DwVfpRegister src2,
                     const Condition cond) {
  // Dd = vadd(Dn, Dm) double precision floating point addition.
  // Dd = D:Vd; Dm=M:Vm; Dn=N:Vm.
  // Instruction details available in ARM DDI 0406A, A8-536.
  // cond(31-28) | 11100(27-23)| D=?(22) | 11(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz(8)=1 | N(7)=0 | 0(6) | M=?(5) | 0(4) | Vm(3-0)
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  emit(cond | 0xE*B24 | 0x3*B20 | src1.code()*B16 |
       dst.code()*B12 | 0x5*B9 | B8 | src2.code());
}


void Assembler::vsub(const DwVfpRegister dst,
                     const DwVfpRegister src1,
                     const DwVfpRegister src2,
                     const Condition cond) {
  // Dd = vsub(Dn, Dm) double precision floating point subtraction.
  // Dd = D:Vd; Dm=M:Vm; Dn=N:Vm.
  // Instruction details available in ARM DDI 0406A, A8-784.
  // cond(31-28) | 11100(27-23)| D=?(22) | 11(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz(8)=1 | N(7)=0 | 1(6) | M=?(5) | 0(4) | Vm(3-0)
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  emit(cond | 0xE*B24 | 0x3*B20 | src1.code()*B16 |
       dst.code()*B12 | 0x5*B9 | B8 | B6 | src2.code());
}


void Assembler::vmul(const DwVfpRegister dst,
                     const DwVfpRegister src1,
                     const DwVfpRegister src2,
                     const Condition cond) {
  // Dd = vmul(Dn, Dm) double precision floating point multiplication.
  // Dd = D:Vd; Dm=M:Vm; Dn=N:Vm.
  // Instruction details available in ARM DDI 0406A, A8-784.
  // cond(31-28) | 11100(27-23)| D=?(22) | 10(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz(8)=1 | N(7)=0 | 0(6) | M=?(5) | 0(4) | Vm(3-0)
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  emit(cond | 0xE*B24 | 0x2*B20 | src1.code()*B16 |
       dst.code()*B12 | 0x5*B9 | B8 | src2.code());
}


void Assembler::vdiv(const DwVfpRegister dst,
                     const DwVfpRegister src1,
                     const DwVfpRegister src2,
                     const Condition cond) {
  // Dd = vdiv(Dn, Dm) double precision floating point division.
  // Dd = D:Vd; Dm=M:Vm; Dn=N:Vm.
  // Instruction details available in ARM DDI 0406A, A8-584.
  // cond(31-28) | 11101(27-23)| D=?(22) | 00(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz(8)=1 | N(7)=? | 0(6) | M=?(5) | 0(4) | Vm(3-0)
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  emit(cond | 0xE*B24 | B23 | src1.code()*B16 |
       dst.code()*B12 | 0x5*B9 | B8 | src2.code());
}


void Assembler::vcmp(const DwVfpRegister src1,
                     const DwVfpRegister src2,
                     const SBit s,
                     const Condition cond) {
  // vcmp(Dd, Dm) double precision floating point comparison.
  // Instruction details available in ARM DDI 0406A, A8-570.
  // cond(31-28) | 11101 (27-23)| D=?(22) | 11 (21-20) | 0100 (19-16) |
  // Vd(15-12) | 101(11-9) | sz(8)=1 | E(7)=? | 1(6) | M(5)=? | 0(4) | Vm(3-0)
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  emit(cond | 0xE*B24 |B23 | 0x3*B20 | B18 |
       src1.code()*B12 | 0x5*B9 | B8 | B6 | src2.code());
}


void Assembler::vmrs(Register dst, Condition cond) {
  // Instruction details available in ARM DDI 0406A, A8-652.
  // cond(31-28) | 1110 (27-24) | 1111(23-20)| 0001 (19-16) |
  // Rt(15-12) | 1010 (11-8) | 0(7) | 00 (6-5) | 1(4) | 0000(3-0)
  ASSERT(CpuFeatures::IsEnabled(VFP3));
  emit(cond | 0xE*B24 | 0xF*B20 |  B16 |
       dst.code()*B12 | 0xA*B8 | B4);
}


// Pseudo instructions.
void Assembler::lea(Register dst,
                    const MemOperand& x,
                    SBit s,
                    Condition cond) {
  int am = x.am_;
  if (!x.rm_.is_valid()) {
    // Immediate offset.
    if ((am & P) == 0)  // post indexing
      mov(dst, Operand(x.rn_), s, cond);
    else if ((am & U) == 0)  // negative indexing
      sub(dst, x.rn_, Operand(x.offset_), s, cond);
    else
      add(dst, x.rn_, Operand(x.offset_), s, cond);
  } else {
    // Register offset (shift_imm_ and shift_op_ are 0) or scaled
    // register offset the constructors make sure than both shift_imm_
    // and shift_op_ are initialized.
    ASSERT(!x.rm_.is(pc));
    if ((am & P) == 0)  // post indexing
      mov(dst, Operand(x.rn_), s, cond);
    else if ((am & U) == 0)  // negative indexing
      sub(dst, x.rn_, Operand(x.rm_, x.shift_op_, x.shift_imm_), s, cond);
    else
      add(dst, x.rn_, Operand(x.rm_, x.shift_op_, x.shift_imm_), s, cond);
  }
}


bool Assembler::ImmediateFitsAddrMode1Instruction(int32_t imm32) {
  uint32_t dummy1;
  uint32_t dummy2;
  return fits_shifter(imm32, &dummy1, &dummy2, NULL);
}


void Assembler::BlockConstPoolFor(int instructions) {
  BlockConstPoolBefore(pc_offset() + instructions * kInstrSize);
}


// Debugging.
void Assembler::RecordJSReturn() {
  WriteRecordedPositions();
  CheckBuffer();
  RecordRelocInfo(RelocInfo::JS_RETURN);
}


void Assembler::RecordComment(const char* msg) {
  if (FLAG_debug_code) {
    CheckBuffer();
    RecordRelocInfo(RelocInfo::COMMENT, reinterpret_cast<intptr_t>(msg));
  }
}


void Assembler::RecordPosition(int pos) {
  if (pos == RelocInfo::kNoPosition) return;
  ASSERT(pos >= 0);
  current_position_ = pos;
}


void Assembler::RecordStatementPosition(int pos) {
  if (pos == RelocInfo::kNoPosition) return;
  ASSERT(pos >= 0);
  current_statement_position_ = pos;
}


void Assembler::WriteRecordedPositions() {
  // Write the statement position if it is different from what was written last
  // time.
  if (current_statement_position_ != written_statement_position_) {
    CheckBuffer();
    RecordRelocInfo(RelocInfo::STATEMENT_POSITION, current_statement_position_);
    written_statement_position_ = current_statement_position_;
  }

  // Write the position if it is different from what was written last time and
  // also different from the written statement position.
  if (current_position_ != written_position_ &&
      current_position_ != written_statement_position_) {
    CheckBuffer();
    RecordRelocInfo(RelocInfo::POSITION, current_position_);
    written_position_ = current_position_;
  }
}


void Assembler::GrowBuffer() {
  if (!own_buffer_) FATAL("external code buffer is too small");

  // Compute new buffer size.
  CodeDesc desc;  // the new buffer
  if (buffer_size_ < 4*KB) {
    desc.buffer_size = 4*KB;
  } else if (buffer_size_ < 1*MB) {
    desc.buffer_size = 2*buffer_size_;
  } else {
    desc.buffer_size = buffer_size_ + 1*MB;
  }
  CHECK_GT(desc.buffer_size, 0);  // no overflow

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

  // None of our relocation types are pc relative pointing outside the code
  // buffer nor pc absolute pointing inside the code buffer, so there is no need
  // to relocate any emitted relocation entries.

  // Relocate pending relocation entries.
  for (int i = 0; i < num_prinfo_; i++) {
    RelocInfo& rinfo = prinfo_[i];
    ASSERT(rinfo.rmode() != RelocInfo::COMMENT &&
           rinfo.rmode() != RelocInfo::POSITION);
    if (rinfo.rmode() != RelocInfo::JS_RETURN) {
      rinfo.set_pc(rinfo.pc() + pc_delta);
    }
  }
}


void Assembler::RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data) {
  RelocInfo rinfo(pc_, rmode, data);  // we do not try to reuse pool constants
  if (rmode >= RelocInfo::JS_RETURN && rmode <= RelocInfo::STATEMENT_POSITION) {
    // Adjust code for new modes.
    ASSERT(RelocInfo::IsJSReturn(rmode)
           || RelocInfo::IsComment(rmode)
           || RelocInfo::IsPosition(rmode));
    // These modes do not need an entry in the constant pool.
  } else {
    ASSERT(num_prinfo_ < kMaxNumPRInfo);
    prinfo_[num_prinfo_++] = rinfo;
    // Make sure the constant pool is not emitted in place of the next
    // instruction for which we just recorded relocation info.
    BlockConstPoolBefore(pc_offset() + kInstrSize);
  }
  if (rinfo.rmode() != RelocInfo::NONE) {
    // Don't record external references unless the heap will be serialized.
    if (rmode == RelocInfo::EXTERNAL_REFERENCE) {
#ifdef DEBUG
      if (!Serializer::enabled()) {
        Serializer::TooLateToEnableNow();
      }
#endif
      if (!Serializer::enabled() && !FLAG_debug_code) {
        return;
      }
    }
    ASSERT(buffer_space() >= kMaxRelocSize);  // too late to grow buffer here
    reloc_info_writer.Write(&rinfo);
  }
}


void Assembler::CheckConstPool(bool force_emit, bool require_jump) {
  // Calculate the offset of the next check. It will be overwritten
  // when a const pool is generated or when const pools are being
  // blocked for a specific range.
  next_buffer_check_ = pc_offset() + kCheckConstInterval;

  // There is nothing to do if there are no pending relocation info entries.
  if (num_prinfo_ == 0) return;

  // We emit a constant pool at regular intervals of about kDistBetweenPools
  // or when requested by parameter force_emit (e.g. after each function).
  // We prefer not to emit a jump unless the max distance is reached or if we
  // are running low on slots, which can happen if a lot of constants are being
  // emitted (e.g. --debug-code and many static references).
  int dist = pc_offset() - last_const_pool_end_;
  if (!force_emit && dist < kMaxDistBetweenPools &&
      (require_jump || dist < kDistBetweenPools) &&
      // TODO(1236125): Cleanup the "magic" number below. We know that
      // the code generation will test every kCheckConstIntervalInst.
      // Thus we are safe as long as we generate less than 7 constant
      // entries per instruction.
      (num_prinfo_ < (kMaxNumPRInfo - (7 * kCheckConstIntervalInst)))) {
    return;
  }

  // If we did not return by now, we need to emit the constant pool soon.

  // However, some small sequences of instructions must not be broken up by the
  // insertion of a constant pool; such sequences are protected by setting
  // no_const_pool_before_, which is checked here. Also, recursive calls to
  // CheckConstPool are blocked by no_const_pool_before_.
  if (pc_offset() < no_const_pool_before_) {
    // Emission is currently blocked; make sure we try again as soon as
    // possible.
    next_buffer_check_ = no_const_pool_before_;

    // Something is wrong if emission is forced and blocked at the same time.
    ASSERT(!force_emit);
    return;
  }

  int jump_instr = require_jump ? kInstrSize : 0;

  // Check that the code buffer is large enough before emitting the constant
  // pool and relocation information (include the jump over the pool and the
  // constant pool marker).
  int max_needed_space =
      jump_instr + kInstrSize + num_prinfo_*(kInstrSize + kMaxRelocSize);
  while (buffer_space() <= (max_needed_space + kGap)) GrowBuffer();

  // Block recursive calls to CheckConstPool.
  BlockConstPoolBefore(pc_offset() + jump_instr + kInstrSize +
                       num_prinfo_*kInstrSize);
  // Don't bother to check for the emit calls below.
  next_buffer_check_ = no_const_pool_before_;

  // Emit jump over constant pool if necessary.
  Label after_pool;
  if (require_jump) b(&after_pool);

  RecordComment("[ Constant Pool");

  // Put down constant pool marker "Undefined instruction" as specified by
  // A3.1 Instruction set encoding.
  emit(0x03000000 | num_prinfo_);

  // Emit constant pool entries.
  for (int i = 0; i < num_prinfo_; i++) {
    RelocInfo& rinfo = prinfo_[i];
    ASSERT(rinfo.rmode() != RelocInfo::COMMENT &&
           rinfo.rmode() != RelocInfo::POSITION &&
           rinfo.rmode() != RelocInfo::STATEMENT_POSITION);
    Instr instr = instr_at(rinfo.pc());

    // Instruction to patch must be a ldr/str [pc, #offset].
    // P and U set, B and W clear, Rn == pc, offset12 still 0.
    ASSERT((instr & (7*B25 | P | U | B | W | 15*B16 | Off12Mask)) ==
           (2*B25 | P | U | pc.code()*B16));
    int delta = pc_ - rinfo.pc() - 8;
    ASSERT(delta >= -4);  // instr could be ldr pc, [pc, #-4] followed by targ32
    if (delta < 0) {
      instr &= ~U;
      delta = -delta;
    }
    ASSERT(is_uint12(delta));
    instr_at_put(rinfo.pc(), instr + delta);
    emit(rinfo.data());
  }
  num_prinfo_ = 0;
  last_const_pool_end_ = pc_offset();

  RecordComment("]");

  if (after_pool.is_linked()) {
    bind(&after_pool);
  }

  // Since a constant pool was just emitted, move the check offset forward by
  // the standard interval.
  next_buffer_check_ = pc_offset() + kCheckConstInterval;
}


} }  // namespace v8::internal
