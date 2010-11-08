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
// Copyright 2006-2009 the V8 project authors. All rights reserved.

#ifndef V8_ASSEMBLER_H_
#define V8_ASSEMBLER_H_

#include "runtime.h"
#include "top.h"
#include "token.h"
#include "objects.h"

namespace v8 {
namespace internal {


// -----------------------------------------------------------------------------
// Labels represent pc locations; they are typically jump or call targets.
// After declaration, a label can be freely used to denote known or (yet)
// unknown pc location. Assembler::bind() is used to bind a label to the
// current pc. A label can be bound only once.

class Label BASE_EMBEDDED {
 public:
  INLINE(Label())                 { Unuse(); }
  INLINE(~Label())                { ASSERT(!is_linked()); }

  INLINE(void Unuse())            { pos_ = 0; }

  INLINE(bool is_bound() const)  { return pos_ <  0; }
  INLINE(bool is_unused() const)  { return pos_ == 0; }
  INLINE(bool is_linked() const)  { return pos_ >  0; }

  // Returns the position of bound or linked labels. Cannot be used
  // for unused labels.
  int pos() const;

 private:
  // pos_ encodes both the binding state (via its sign)
  // and the binding position (via its value) of a label.
  //
  // pos_ <  0  bound label, pos() returns the jump target position
  // pos_ == 0  unused label
  // pos_ >  0  linked label, pos() returns the last reference position
  int pos_;

  void bind_to(int pos)  {
    pos_ = -pos - 1;
    ASSERT(is_bound());
  }
  void link_to(int pos)  {
    pos_ =  pos + 1;
    ASSERT(is_linked());
  }

  friend class Assembler;
  friend class RegexpAssembler;
  friend class Displacement;
  friend class ShadowTarget;
  friend class RegExpMacroAssemblerIrregexp;
};


// -----------------------------------------------------------------------------
// NearLabels are labels used for short jumps (in Intel jargon).
// NearLabels should be used if it can be guaranteed that the jump range is
// within -128 to +127. We already use short jumps when jumping backwards,
// so using a NearLabel will only have performance impact if used for forward
// jumps.
class NearLabel BASE_EMBEDDED {
 public:
  NearLabel() { Unuse(); }
  ~NearLabel() { ASSERT(!is_linked()); }

  void Unuse() {
    pos_ = -1;
    unresolved_branches_ = 0;
#ifdef DEBUG
    for (int i = 0; i < kMaxUnresolvedBranches; i++) {
      unresolved_positions_[i] = -1;
    }
#endif
  }

  int pos() {
    ASSERT(is_bound());
    return pos_;
  }

  bool is_bound() { return pos_ >= 0; }
  bool is_linked() { return !is_bound() && unresolved_branches_ > 0; }
  bool is_unused() { return !is_bound() && unresolved_branches_ == 0; }

  void bind_to(int position) {
    ASSERT(!is_bound());
    pos_ = position;
  }

  void link_to(int position) {
    ASSERT(!is_bound());
    ASSERT(unresolved_branches_ < kMaxUnresolvedBranches);
    unresolved_positions_[unresolved_branches_++] = position;
  }

 private:
  static const int kMaxUnresolvedBranches = 8;
  int pos_;
  int unresolved_branches_;
  int unresolved_positions_[kMaxUnresolvedBranches];

  friend class Assembler;
};


// -----------------------------------------------------------------------------
// Relocation information


// Relocation information consists of the address (pc) of the datum
// to which the relocation information applies, the relocation mode
// (rmode), and an optional data field. The relocation mode may be
// "descriptive" and not indicate a need for relocation, but simply
// describe a property of the datum. Such rmodes are useful for GC
// and nice disassembly output.

class RelocInfo BASE_EMBEDDED {
 public:
  // The constant kNoPosition is used with the collecting of source positions
  // in the relocation information. Two types of source positions are collected
  // "position" (RelocMode position) and "statement position" (RelocMode
  // statement_position). The "position" is collected at places in the source
  // code which are of interest when making stack traces to pin-point the source
  // location of a stack frame as close as possible. The "statement position" is
  // collected at the beginning at each statement, and is used to indicate
  // possible break locations. kNoPosition is used to indicate an
  // invalid/uninitialized position value.
  static const int kNoPosition = -1;

  enum Mode {
    // Please note the order is important (see IsCodeTarget, IsGCRelocMode).
    CONSTRUCT_CALL,  // code target that is a call to a JavaScript constructor.
    CODE_TARGET_CONTEXT,  // Code target used for contextual loads.
    DEBUG_BREAK,  // Code target for the debugger statement.
    CODE_TARGET,  // Code target which is not any of the above.
    EMBEDDED_OBJECT,

    // Everything after runtime_entry (inclusive) is not GC'ed.
    RUNTIME_ENTRY,
    JS_RETURN,  // Marks start of the ExitJSFrame code.
    COMMENT,
    POSITION,  // See comment for kNoPosition above.
    STATEMENT_POSITION,  // See comment for kNoPosition above.
    DEBUG_BREAK_SLOT,  // Additional code inserted for debug break slot.
    EXTERNAL_REFERENCE,  // The address of an external C++ function.
    INTERNAL_REFERENCE,  // An address inside the same function.

    // add more as needed
    // Pseudo-types
    NUMBER_OF_MODES,  // must be no greater than 14 - see RelocInfoWriter
    NONE,  // never recorded
    LAST_CODE_ENUM = CODE_TARGET,
    LAST_GCED_ENUM = EMBEDDED_OBJECT
  };


  RelocInfo() {}
  RelocInfo(byte* pc, Mode rmode, intptr_t data)
      : pc_(pc), rmode_(rmode), data_(data) {
  }

  static inline bool IsConstructCall(Mode mode) {
    return mode == CONSTRUCT_CALL;
  }
  static inline bool IsCodeTarget(Mode mode) {
    return mode <= LAST_CODE_ENUM;
  }
  // Is the relocation mode affected by GC?
  static inline bool IsGCRelocMode(Mode mode) {
    return mode <= LAST_GCED_ENUM;
  }
  static inline bool IsJSReturn(Mode mode) {
    return mode == JS_RETURN;
  }
  static inline bool IsComment(Mode mode) {
    return mode == COMMENT;
  }
  static inline bool IsPosition(Mode mode) {
    return mode == POSITION || mode == STATEMENT_POSITION;
  }
  static inline bool IsStatementPosition(Mode mode) {
    return mode == STATEMENT_POSITION;
  }
  static inline bool IsExternalReference(Mode mode) {
    return mode == EXTERNAL_REFERENCE;
  }
  static inline bool IsInternalReference(Mode mode) {
    return mode == INTERNAL_REFERENCE;
  }
  static inline bool IsDebugBreakSlot(Mode mode) {
    return mode == DEBUG_BREAK_SLOT;
  }
  static inline int ModeMask(Mode mode) { return 1 << mode; }

  // Accessors
  byte* pc() const { return pc_; }
  void set_pc(byte* pc) { pc_ = pc; }
  Mode rmode() const {  return rmode_; }
  intptr_t data() const { return data_; }

  // Apply a relocation by delta bytes
  INLINE(void apply(intptr_t delta));

  // Is the pointer this relocation info refers to coded like a plain pointer
  // or is it strange in some way (eg relative or patched into a series of
  // instructions).
  bool IsCodedSpecially();

  // Read/modify the code target in the branch/call instruction
  // this relocation applies to;
  // can only be called if IsCodeTarget(rmode_) || rmode_ == RUNTIME_ENTRY
  INLINE(Address target_address());
  INLINE(void set_target_address(Address target));
  INLINE(Object* target_object());
  INLINE(Handle<Object> target_object_handle(Assembler* origin));
  INLINE(Object** target_object_address());
  INLINE(void set_target_object(Object* target));

  // Read the address of the word containing the target_address in an
  // instruction stream.  What this means exactly is architecture-independent.
  // The only architecture-independent user of this function is the serializer.
  // The serializer uses it to find out how many raw bytes of instruction to
  // output before the next target.  Architecture-independent code shouldn't
  // dereference the pointer it gets back from this.
  INLINE(Address target_address_address());
  // This indicates how much space a target takes up when deserializing a code
  // stream.  For most architectures this is just the size of a pointer.  For
  // an instruction like movw/movt where the target bits are mixed into the
  // instruction bits the size of the target will be zero, indicating that the
  // serializer should not step forwards in memory after a target is resolved
  // and written.  In this case the target_address_address function above
  // should return the end of the instructions to be patched, allowing the
  // deserializer to deserialize the instructions as raw bytes and put them in
  // place, ready to be patched with the target.
  INLINE(int target_address_size());

  // Read/modify the reference in the instruction this relocation
  // applies to; can only be called if rmode_ is external_reference
  INLINE(Address* target_reference_address());

  // Read/modify the address of a call instruction. This is used to relocate
  // the break points where straight-line code is patched with a call
  // instruction.
  INLINE(Address call_address());
  INLINE(void set_call_address(Address target));
  INLINE(Object* call_object());
  INLINE(void set_call_object(Object* target));
  INLINE(Object** call_object_address());

  template<typename StaticVisitor> inline void Visit();
  inline void Visit(ObjectVisitor* v);

  // Patch the code with some other code.
  void PatchCode(byte* instructions, int instruction_count);

  // Patch the code with a call.
  void PatchCodeWithCall(Address target, int guard_bytes);

  // Check whether this return sequence has been patched
  // with a call to the debugger.
  INLINE(bool IsPatchedReturnSequence());

  // Check whether this debug break slot has been patched with a call to the
  // debugger.
  INLINE(bool IsPatchedDebugBreakSlotSequence());

#ifdef ENABLE_DISASSEMBLER
  // Printing
  static const char* RelocModeName(Mode rmode);
  void Print();
#endif  // ENABLE_DISASSEMBLER
#ifdef DEBUG
  // Debugging
  void Verify();
#endif

  static const int kCodeTargetMask = (1 << (LAST_CODE_ENUM + 1)) - 1;
  static const int kPositionMask = 1 << POSITION | 1 << STATEMENT_POSITION;
  static const int kDebugMask = kPositionMask | 1 << COMMENT;
  static const int kApplyMask;  // Modes affected by apply. Depends on arch.

 private:
  // On ARM, note that pc_ is the address of the constant pool entry
  // to be relocated and not the address of the instruction
  // referencing the constant pool entry (except when rmode_ ==
  // comment).
  byte* pc_;
  Mode rmode_;
  intptr_t data_;
  friend class RelocIterator;
};


// RelocInfoWriter serializes a stream of relocation info. It writes towards
// lower addresses.
class RelocInfoWriter BASE_EMBEDDED {
 public:
  RelocInfoWriter() : pos_(NULL), last_pc_(NULL), last_data_(0) {}
  RelocInfoWriter(byte* pos, byte* pc) : pos_(pos), last_pc_(pc),
                                         last_data_(0) {}

  byte* pos() const { return pos_; }
  byte* last_pc() const { return last_pc_; }

  void Write(const RelocInfo* rinfo);

  // Update the state of the stream after reloc info buffer
  // and/or code is moved while the stream is active.
  void Reposition(byte* pos, byte* pc) {
    pos_ = pos;
    last_pc_ = pc;
  }

  // Max size (bytes) of a written RelocInfo. Longest encoding is
  // ExtraTag, VariableLengthPCJump, ExtraTag, pc_delta, ExtraTag, data_delta.
  // On ia32 and arm this is 1 + 4 + 1 + 1 + 1 + 4 = 12.
  // On x64 this is 1 + 4 + 1 + 1 + 1 + 8 == 16;
  // Here we use the maximum of the two.
  static const int kMaxSize = 16;

 private:
  inline uint32_t WriteVariableLengthPCJump(uint32_t pc_delta);
  inline void WriteTaggedPC(uint32_t pc_delta, int tag);
  inline void WriteExtraTaggedPC(uint32_t pc_delta, int extra_tag);
  inline void WriteExtraTaggedData(intptr_t data_delta, int top_tag);
  inline void WriteTaggedData(intptr_t data_delta, int tag);
  inline void WriteExtraTag(int extra_tag, int top_tag);

  byte* pos_;
  byte* last_pc_;
  intptr_t last_data_;
  DISALLOW_COPY_AND_ASSIGN(RelocInfoWriter);
};


// A RelocIterator iterates over relocation information.
// Typical use:
//
//   for (RelocIterator it(code); !it.done(); it.next()) {
//     // do something with it.rinfo() here
//   }
//
// A mask can be specified to skip unwanted modes.
class RelocIterator: public Malloced {
 public:
  // Create a new iterator positioned at
  // the beginning of the reloc info.
  // Relocation information with mode k is included in the
  // iteration iff bit k of mode_mask is set.
  explicit RelocIterator(Code* code, int mode_mask = -1);
  explicit RelocIterator(const CodeDesc& desc, int mode_mask = -1);

  // Iteration
  bool done() const { return done_; }
  void next();

  // Return pointer valid until next next().
  RelocInfo* rinfo() {
    ASSERT(!done());
    return &rinfo_;
  }

 private:
  // Advance* moves the position before/after reading.
  // *Read* reads from current byte(s) into rinfo_.
  // *Get* just reads and returns info on current byte.
  void Advance(int bytes = 1) { pos_ -= bytes; }
  int AdvanceGetTag();
  int GetExtraTag();
  int GetTopTag();
  void ReadTaggedPC();
  void AdvanceReadPC();
  void AdvanceReadData();
  void AdvanceReadVariableLengthPCJump();
  int GetPositionTypeTag();
  void ReadTaggedData();

  static RelocInfo::Mode DebugInfoModeFromTag(int tag);

  // If the given mode is wanted, set it in rinfo_ and return true.
  // Else return false. Used for efficiently skipping unwanted modes.
  bool SetMode(RelocInfo::Mode mode) {
    return (mode_mask_ & 1 << mode) ? (rinfo_.rmode_ = mode, true) : false;
  }

  byte* pos_;
  byte* end_;
  RelocInfo rinfo_;
  bool done_;
  int mode_mask_;
  DISALLOW_COPY_AND_ASSIGN(RelocIterator);
};


//------------------------------------------------------------------------------
// External function

//----------------------------------------------------------------------------
class IC_Utility;
class SCTableReference;
#ifdef ENABLE_DEBUGGER_SUPPORT
class Debug_Address;
#endif


typedef void* ExternalReferenceRedirector(void* original, bool fp_return);


// An ExternalReference represents a C++ address used in the generated
// code. All references to C++ functions and variables must be encapsulated in
// an ExternalReference instance. This is done in order to track the origin of
// all external references in the code so that they can be bound to the correct
// addresses when deserializing a heap.
class ExternalReference BASE_EMBEDDED {
 public:
  explicit ExternalReference(Builtins::CFunctionId id);

  explicit ExternalReference(ApiFunction* ptr);

  explicit ExternalReference(Builtins::Name name);

  explicit ExternalReference(Runtime::FunctionId id);

  explicit ExternalReference(Runtime::Function* f);

  explicit ExternalReference(const IC_Utility& ic_utility);

#ifdef ENABLE_DEBUGGER_SUPPORT
  explicit ExternalReference(const Debug_Address& debug_address);
#endif

  explicit ExternalReference(StatsCounter* counter);

  explicit ExternalReference(Top::AddressId id);

  explicit ExternalReference(const SCTableReference& table_ref);

  // One-of-a-kind references. These references are not part of a general
  // pattern. This means that they have to be added to the
  // ExternalReferenceTable in serialize.cc manually.

  static ExternalReference perform_gc_function();
  static ExternalReference fill_heap_number_with_random_function();
  static ExternalReference random_uint32_function();
  static ExternalReference transcendental_cache_array_address();
  static ExternalReference delete_handle_scope_extensions();

  // Static data in the keyed lookup cache.
  static ExternalReference keyed_lookup_cache_keys();
  static ExternalReference keyed_lookup_cache_field_offsets();

  // Static variable Factory::the_hole_value.location()
  static ExternalReference the_hole_value_location();

  // Static variable Heap::roots_address()
  static ExternalReference roots_address();

  // Static variable StackGuard::address_of_jslimit()
  static ExternalReference address_of_stack_limit();

  // Static variable StackGuard::address_of_real_jslimit()
  static ExternalReference address_of_real_stack_limit();

  // Static variable RegExpStack::limit_address()
  static ExternalReference address_of_regexp_stack_limit();

  // Static variables for RegExp.
  static ExternalReference address_of_static_offsets_vector();
  static ExternalReference address_of_regexp_stack_memory_address();
  static ExternalReference address_of_regexp_stack_memory_size();

  // Static variable Heap::NewSpaceStart()
  static ExternalReference new_space_start();
  static ExternalReference new_space_mask();
  static ExternalReference heap_always_allocate_scope_depth();

  // Used for fast allocation in generated code.
  static ExternalReference new_space_allocation_top_address();
  static ExternalReference new_space_allocation_limit_address();

  static ExternalReference double_fp_operation(Token::Value operation);
  static ExternalReference compare_doubles();

  static ExternalReference handle_scope_next_address();
  static ExternalReference handle_scope_limit_address();
  static ExternalReference handle_scope_level_address();

  static ExternalReference scheduled_exception_address();

  Address address() const {return reinterpret_cast<Address>(address_);}

#ifdef ENABLE_DEBUGGER_SUPPORT
  // Function Debug::Break()
  static ExternalReference debug_break();

  // Used to check if single stepping is enabled in generated code.
  static ExternalReference debug_step_in_fp_address();
#endif

#ifndef V8_INTERPRETED_REGEXP
  // C functions called from RegExp generated code.

  // Function NativeRegExpMacroAssembler::CaseInsensitiveCompareUC16()
  static ExternalReference re_case_insensitive_compare_uc16();

  // Function RegExpMacroAssembler*::CheckStackGuardState()
  static ExternalReference re_check_stack_guard_state();

  // Function NativeRegExpMacroAssembler::GrowStack()
  static ExternalReference re_grow_stack();

  // byte NativeRegExpMacroAssembler::word_character_bitmap
  static ExternalReference re_word_character_map();

#endif

  // This lets you register a function that rewrites all external references.
  // Used by the ARM simulator to catch calls to external references.
  static void set_redirector(ExternalReferenceRedirector* redirector) {
    ASSERT(redirector_ == NULL);  // We can't stack them.
    redirector_ = redirector;
  }

 private:
  explicit ExternalReference(void* address)
      : address_(address) {}

  static ExternalReferenceRedirector* redirector_;

  static void* Redirect(void* address, bool fp_return = false) {
    if (redirector_ == NULL) return address;
    void* answer = (*redirector_)(address, fp_return);
    return answer;
  }

  static void* Redirect(Address address_arg, bool fp_return = false) {
    void* address = reinterpret_cast<void*>(address_arg);
    void* answer = (redirector_ == NULL) ?
                   address :
                   (*redirector_)(address, fp_return);
    return answer;
  }

  void* address_;
};


// -----------------------------------------------------------------------------
// Position recording support

enum PositionRecordingType { FORCED_POSITION, NORMAL_POSITION };

class PositionsRecorder BASE_EMBEDDED {
 public:
  explicit PositionsRecorder(Assembler* assembler)
      : assembler_(assembler),
        current_position_(RelocInfo::kNoPosition),
        current_position_recording_type_(NORMAL_POSITION),
        written_position_(RelocInfo::kNoPosition),
        current_statement_position_(RelocInfo::kNoPosition),
        written_statement_position_(RelocInfo::kNoPosition) { }

  // Set current position to pos. If recording_type is FORCED_POSITION then
  // WriteRecordedPositions will write this position even if it is equal to
  // statement position previously written for another pc.
  void RecordPosition(int pos,
                      PositionRecordingType recording_type = NORMAL_POSITION);

  // Set current statement position to pos.
  void RecordStatementPosition(int pos);

  // Write recorded positions to relocation information.
  bool WriteRecordedPositions();

  int current_position() const { return current_position_; }

  int current_statement_position() const { return current_statement_position_; }

 private:
  Assembler* assembler_;

  int current_position_;
  PositionRecordingType current_position_recording_type_;
  int written_position_;

  int current_statement_position_;
  int written_statement_position_;
};


class PreserveStatementPositionScope BASE_EMBEDDED {
 public:
  explicit PreserveStatementPositionScope(PositionsRecorder* positions_recorder)
      : positions_recorder_(positions_recorder),
        statement_position_(positions_recorder->current_statement_position()) {}

  ~PreserveStatementPositionScope() {
    if (statement_position_ != RelocInfo::kNoPosition) {
      positions_recorder_->RecordStatementPosition(statement_position_);
    }
  }

 private:
  PositionsRecorder* positions_recorder_;
  int statement_position_;
};


// -----------------------------------------------------------------------------
// Utility functions

static inline bool is_intn(int x, int n)  {
  return -(1 << (n-1)) <= x && x < (1 << (n-1));
}

static inline bool is_int8(int x)  { return is_intn(x, 8); }
static inline bool is_int16(int x)  { return is_intn(x, 16); }
static inline bool is_int18(int x)  { return is_intn(x, 18); }
static inline bool is_int24(int x)  { return is_intn(x, 24); }

static inline bool is_uintn(int x, int n) {
  return (x & -(1 << n)) == 0;
}

static inline bool is_uint2(int x)  { return is_uintn(x, 2); }
static inline bool is_uint3(int x)  { return is_uintn(x, 3); }
static inline bool is_uint4(int x)  { return is_uintn(x, 4); }
static inline bool is_uint5(int x)  { return is_uintn(x, 5); }
static inline bool is_uint6(int x)  { return is_uintn(x, 6); }
static inline bool is_uint8(int x)  { return is_uintn(x, 8); }
static inline bool is_uint10(int x)  { return is_uintn(x, 10); }
static inline bool is_uint12(int x)  { return is_uintn(x, 12); }
static inline bool is_uint16(int x)  { return is_uintn(x, 16); }
static inline bool is_uint24(int x)  { return is_uintn(x, 24); }
static inline bool is_uint26(int x)  { return is_uintn(x, 26); }
static inline bool is_uint28(int x)  { return is_uintn(x, 28); }

static inline int NumberOfBitsSet(uint32_t x) {
  unsigned int num_bits_set;
  for (num_bits_set = 0; x; x >>= 1) {
    num_bits_set += x & 1;
  }
  return num_bits_set;
}

} }  // namespace v8::internal

#endif  // V8_ASSEMBLER_H_
