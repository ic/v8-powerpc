// Copyright 2006-2008 the V8 project authors. All rights reserved.
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

#ifndef V8_CODE_STUBS_H_
#define V8_CODE_STUBS_H_

#include "globals.h"

namespace v8 {
namespace internal {

// List of code stubs used on all platforms. The order in this list is important
// as only the stubs up to and including RecordWrite allows nested stub calls.
#define CODE_STUB_LIST_ALL_PLATFORMS(V)  \
  V(CallFunction)                        \
  V(GenericBinaryOp)                     \
  V(StringAdd)                           \
  V(SubString)                           \
  V(StringCompare)                       \
  V(SmiOp)                               \
  V(Compare)                             \
  V(RecordWrite)                         \
  V(ConvertToDouble)                     \
  V(WriteInt32ToHeapNumber)              \
  V(StackCheck)                          \
  V(FastNewClosure)                      \
  V(FastNewContext)                      \
  V(FastCloneShallowArray)               \
  V(TranscendentalCache)                 \
  V(GenericUnaryOp)                      \
  V(RevertToNumber)                      \
  V(ToBoolean)                           \
  V(Instanceof)                          \
  V(CounterOp)                           \
  V(ArgumentsAccess)                     \
  V(RegExpExec)                          \
  V(NumberToString)                      \
  V(CEntry)                              \
  V(JSEntry)                             \
  V(DebuggerStatement)

// List of code stubs only used on ARM platforms.
#ifdef V8_TARGET_ARCH_ARM
#define CODE_STUB_LIST_ARM(V)  \
  V(GetProperty)               \
  V(SetProperty)               \
  V(InvokeBuiltin)             \
  V(RegExpCEntry)
#else
#define CODE_STUB_LIST_ARM(V)
#endif

// Combined list of code stubs.
#define CODE_STUB_LIST(V)            \
  CODE_STUB_LIST_ALL_PLATFORMS(V)    \
  CODE_STUB_LIST_ARM(V)

// Stub is base classes of all stubs.
class CodeStub BASE_EMBEDDED {
 public:
  enum Major {
#define DEF_ENUM(name) name,
    CODE_STUB_LIST(DEF_ENUM)
#undef DEF_ENUM
    NoCache,  // marker for stubs that do custom caching
    NUMBER_OF_IDS
  };

  // Retrieve the code for the stub. Generate the code if needed.
  Handle<Code> GetCode();

  // Retrieve the code for the stub if already generated.  Do not
  // generate the code if not already generated and instead return a
  // retry after GC Failure object.
  Object* TryGetCode();

  static Major MajorKeyFromKey(uint32_t key) {
    return static_cast<Major>(MajorKeyBits::decode(key));
  };
  static int MinorKeyFromKey(uint32_t key) {
    return MinorKeyBits::decode(key);
  };
  static const char* MajorName(Major major_key, bool allow_unknown_keys);

  virtual ~CodeStub() {}

  // Override these methods to provide a custom caching mechanism for
  // an individual type of code stub.
  virtual bool GetCustomCache(Code** code_out) { return false; }
  virtual void SetCustomCache(Code* value) { }
  virtual bool has_custom_cache() { return false; }

 protected:
  static const int kMajorBits = 5;
  static const int kMinorBits = kBitsPerInt - kSmiTagSize - kMajorBits;

 private:
  // Lookup the code in the (possibly custom) cache.
  bool FindCodeInCache(Code** code_out);

  // Nonvirtual wrapper around the stub-specific Generate function.  Call
  // this function to set up the macro assembler and generate the code.
  void GenerateCode(MacroAssembler* masm);

  // Generates the assembler code for the stub.
  virtual void Generate(MacroAssembler* masm) = 0;

  // Perform bookkeeping required after code generation when stub code is
  // initially generated.
  void RecordCodeGeneration(Code* code, MacroAssembler* masm);

  // Returns information for computing the number key.
  virtual Major MajorKey() = 0;
  virtual int MinorKey() = 0;

  // The CallFunctionStub needs to override this so it can encode whether a
  // lazily generated function should be fully optimized or not.
  virtual InLoopFlag InLoop() { return NOT_IN_LOOP; }

  // GenericBinaryOpStub needs to override this.
  virtual int GetCodeKind();

  // GenericBinaryOpStub needs to override this.
  virtual InlineCacheState GetICState() {
    return UNINITIALIZED;
  }

  // Returns a name for logging/debugging purposes.
  virtual const char* GetName() { return MajorName(MajorKey(), false); }

#ifdef DEBUG
  virtual void Print() { PrintF("%s\n", GetName()); }
#endif

  // Computes the key based on major and minor.
  uint32_t GetKey() {
    ASSERT(static_cast<int>(MajorKey()) < NUMBER_OF_IDS);
    return MinorKeyBits::encode(MinorKey()) |
           MajorKeyBits::encode(MajorKey());
  }

  bool AllowsStubCalls() { return MajorKey() <= RecordWrite; }

  class MajorKeyBits: public BitField<uint32_t, 0, kMajorBits> {};
  class MinorKeyBits: public BitField<uint32_t, kMajorBits, kMinorBits> {};

  friend class BreakPointIterator;
};

} }  // namespace v8::internal

#endif  // V8_CODE_STUBS_H_
