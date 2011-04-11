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

#ifndef V8_IA32_CODE_STUBS_IA32_H_
#define V8_IA32_CODE_STUBS_IA32_H_

#include "macro-assembler.h"
#include "code-stubs.h"
#include "ic-inl.h"

namespace v8 {
namespace internal {


// Compute a transcendental math function natively, or call the
// TranscendentalCache runtime function.
class TranscendentalCacheStub: public CodeStub {
 public:
  enum ArgumentType {
    TAGGED = 0,
    UNTAGGED = 1 << TranscendentalCache::kTranscendentalTypeBits
  };

  TranscendentalCacheStub(TranscendentalCache::Type type,
                          ArgumentType argument_type)
      : type_(type), argument_type_(argument_type) {}
  void Generate(MacroAssembler* masm);
 private:
  TranscendentalCache::Type type_;
  ArgumentType argument_type_;

  Major MajorKey() { return TranscendentalCache; }
  int MinorKey() { return type_ | argument_type_; }
  Runtime::FunctionId RuntimeFunction();
  void GenerateOperation(MacroAssembler* masm);
};


class ToBooleanStub: public CodeStub {
 public:
  ToBooleanStub() { }

  void Generate(MacroAssembler* masm);

 private:
  Major MajorKey() { return ToBoolean; }
  int MinorKey() { return 0; }
};


class TypeRecordingBinaryOpStub: public CodeStub {
 public:
  TypeRecordingBinaryOpStub(Token::Value op, OverwriteMode mode)
      : op_(op),
        mode_(mode),
        operands_type_(TRBinaryOpIC::UNINITIALIZED),
        result_type_(TRBinaryOpIC::UNINITIALIZED),
        name_(NULL) {
    use_sse3_ = CpuFeatures::IsSupported(SSE3);
    ASSERT(OpBits::is_valid(Token::NUM_TOKENS));
  }

  TypeRecordingBinaryOpStub(
      int key,
      TRBinaryOpIC::TypeInfo operands_type,
      TRBinaryOpIC::TypeInfo result_type = TRBinaryOpIC::UNINITIALIZED)
      : op_(OpBits::decode(key)),
        mode_(ModeBits::decode(key)),
        use_sse3_(SSE3Bits::decode(key)),
        operands_type_(operands_type),
        result_type_(result_type),
        name_(NULL) { }

 private:
  enum SmiCodeGenerateHeapNumberResults {
    ALLOW_HEAPNUMBER_RESULTS,
    NO_HEAPNUMBER_RESULTS
  };

  Token::Value op_;
  OverwriteMode mode_;
  bool use_sse3_;

  // Operand type information determined at runtime.
  TRBinaryOpIC::TypeInfo operands_type_;
  TRBinaryOpIC::TypeInfo result_type_;

  char* name_;

  const char* GetName();

#ifdef DEBUG
  void Print() {
    PrintF("TypeRecordingBinaryOpStub %d (op %s), "
           "(mode %d, runtime_type_info %s)\n",
           MinorKey(),
           Token::String(op_),
           static_cast<int>(mode_),
           TRBinaryOpIC::GetName(operands_type_));
  }
#endif

  // Minor key encoding in 16 bits RRRTTTSOOOOOOOMM.
  class ModeBits: public BitField<OverwriteMode, 0, 2> {};
  class OpBits: public BitField<Token::Value, 2, 7> {};
  class SSE3Bits: public BitField<bool, 9, 1> {};
  class OperandTypeInfoBits: public BitField<TRBinaryOpIC::TypeInfo, 10, 3> {};
  class ResultTypeInfoBits: public BitField<TRBinaryOpIC::TypeInfo, 13, 3> {};

  Major MajorKey() { return TypeRecordingBinaryOp; }
  int MinorKey() {
    return OpBits::encode(op_)
           | ModeBits::encode(mode_)
           | SSE3Bits::encode(use_sse3_)
           | OperandTypeInfoBits::encode(operands_type_)
           | ResultTypeInfoBits::encode(result_type_);
  }

  void Generate(MacroAssembler* masm);
  void GenerateGeneric(MacroAssembler* masm);
  void GenerateSmiCode(MacroAssembler* masm,
                       Label* slow,
                       SmiCodeGenerateHeapNumberResults heapnumber_results);
  void GenerateLoadArguments(MacroAssembler* masm);
  void GenerateReturn(MacroAssembler* masm);
  void GenerateUninitializedStub(MacroAssembler* masm);
  void GenerateSmiStub(MacroAssembler* masm);
  void GenerateInt32Stub(MacroAssembler* masm);
  void GenerateHeapNumberStub(MacroAssembler* masm);
  void GenerateOddballStub(MacroAssembler* masm);
  void GenerateStringStub(MacroAssembler* masm);
  void GenerateGenericStub(MacroAssembler* masm);
  void GenerateAddStrings(MacroAssembler* masm);

  void GenerateHeapResultAllocation(MacroAssembler* masm, Label* alloc_failure);
  void GenerateRegisterArgsPush(MacroAssembler* masm);
  void GenerateTypeTransition(MacroAssembler* masm);
  void GenerateTypeTransitionWithSavedArgs(MacroAssembler* masm);

  virtual int GetCodeKind() { return Code::TYPE_RECORDING_BINARY_OP_IC; }

  virtual InlineCacheState GetICState() {
    return TRBinaryOpIC::ToState(operands_type_);
  }

  virtual void FinishCode(Code* code) {
    code->set_type_recording_binary_op_type(operands_type_);
    code->set_type_recording_binary_op_result_type(result_type_);
  }

  friend class CodeGenerator;
};


class StringHelper : public AllStatic {
 public:
  // Generate code for copying characters using a simple loop. This should only
  // be used in places where the number of characters is small and the
  // additional setup and checking in GenerateCopyCharactersREP adds too much
  // overhead. Copying of overlapping regions is not supported.
  static void GenerateCopyCharacters(MacroAssembler* masm,
                                     Register dest,
                                     Register src,
                                     Register count,
                                     Register scratch,
                                     bool ascii);

  // Generate code for copying characters using the rep movs instruction.
  // Copies ecx characters from esi to edi. Copying of overlapping regions is
  // not supported.
  static void GenerateCopyCharactersREP(MacroAssembler* masm,
                                        Register dest,     // Must be edi.
                                        Register src,      // Must be esi.
                                        Register count,    // Must be ecx.
                                        Register scratch,  // Neither of above.
                                        bool ascii);

  // Probe the symbol table for a two character string. If the string
  // requires non-standard hashing a jump to the label not_probed is
  // performed and registers c1 and c2 are preserved. In all other
  // cases they are clobbered. If the string is not found by probing a
  // jump to the label not_found is performed. This jump does not
  // guarantee that the string is not in the symbol table. If the
  // string is found the code falls through with the string in
  // register eax.
  static void GenerateTwoCharacterSymbolTableProbe(MacroAssembler* masm,
                                                   Register c1,
                                                   Register c2,
                                                   Register scratch1,
                                                   Register scratch2,
                                                   Register scratch3,
                                                   Label* not_probed,
                                                   Label* not_found);

  // Generate string hash.
  static void GenerateHashInit(MacroAssembler* masm,
                               Register hash,
                               Register character,
                               Register scratch);
  static void GenerateHashAddCharacter(MacroAssembler* masm,
                                       Register hash,
                                       Register character,
                                       Register scratch);
  static void GenerateHashGetHash(MacroAssembler* masm,
                                  Register hash,
                                  Register scratch);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(StringHelper);
};


// Flag that indicates how to generate code for the stub StringAddStub.
enum StringAddFlags {
  NO_STRING_ADD_FLAGS = 0,
  // Omit left string check in stub (left is definitely a string).
  NO_STRING_CHECK_LEFT_IN_STUB = 1 << 0,
  // Omit right string check in stub (right is definitely a string).
  NO_STRING_CHECK_RIGHT_IN_STUB = 1 << 1,
  // Omit both string checks in stub.
  NO_STRING_CHECK_IN_STUB =
      NO_STRING_CHECK_LEFT_IN_STUB | NO_STRING_CHECK_RIGHT_IN_STUB
};


class StringAddStub: public CodeStub {
 public:
  explicit StringAddStub(StringAddFlags flags) : flags_(flags) {}

 private:
  Major MajorKey() { return StringAdd; }
  int MinorKey() { return flags_; }

  void Generate(MacroAssembler* masm);

  void GenerateConvertArgument(MacroAssembler* masm,
                               int stack_offset,
                               Register arg,
                               Register scratch1,
                               Register scratch2,
                               Register scratch3,
                               Label* slow);

  const StringAddFlags flags_;
};


class SubStringStub: public CodeStub {
 public:
  SubStringStub() {}

 private:
  Major MajorKey() { return SubString; }
  int MinorKey() { return 0; }

  void Generate(MacroAssembler* masm);
};


class StringCompareStub: public CodeStub {
 public:
  explicit StringCompareStub() {
  }

  // Compare two flat ascii strings and returns result in eax after popping two
  // arguments from the stack.
  static void GenerateCompareFlatAsciiStrings(MacroAssembler* masm,
                                              Register left,
                                              Register right,
                                              Register scratch1,
                                              Register scratch2,
                                              Register scratch3);

 private:
  Major MajorKey() { return StringCompare; }
  int MinorKey() { return 0; }

  void Generate(MacroAssembler* masm);
};


class NumberToStringStub: public CodeStub {
 public:
  NumberToStringStub() { }

  // Generate code to do a lookup in the number string cache. If the number in
  // the register object is found in the cache the generated code falls through
  // with the result in the result register. The object and the result register
  // can be the same. If the number is not found in the cache the code jumps to
  // the label not_found with only the content of register object unchanged.
  static void GenerateLookupNumberStringCache(MacroAssembler* masm,
                                              Register object,
                                              Register result,
                                              Register scratch1,
                                              Register scratch2,
                                              bool object_is_smi,
                                              Label* not_found);

 private:
  Major MajorKey() { return NumberToString; }
  int MinorKey() { return 0; }

  void Generate(MacroAssembler* masm);

  const char* GetName() { return "NumberToStringStub"; }

#ifdef DEBUG
  void Print() {
    PrintF("NumberToStringStub\n");
  }
#endif
};

} }  // namespace v8::internal

#endif  // V8_IA32_CODE_STUBS_IA32_H_
