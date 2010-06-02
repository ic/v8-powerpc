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

#include "v8.h"

#if defined(V8_TARGET_ARCH_ARM)

#include "assembler-arm.h"
#include "codegen.h"
#include "codegen-inl.h"
#include "disasm.h"
#include "ic-inl.h"
#include "runtime.h"
#include "stub-cache.h"

namespace v8 {
namespace internal {


// ----------------------------------------------------------------------------
// Static IC stub generators.
//

#define __ ACCESS_MASM(masm)

// Helper function used from LoadIC/CallIC GenerateNormal.
// receiver: Receiver. It is not clobbered if a jump to the miss label is
//           done
// name:     Property name. It is not clobbered if a jump to the miss label is
//           done
// result:   Register for the result. It is only updated if a jump to the miss
//           label is not done. Can be the same as receiver or name clobbering
//           one of these in the case of not jumping to the miss label.
// The three scratch registers need to be different from the receiver, name and
// result.
static void GenerateDictionaryLoad(MacroAssembler* masm,
                                   Label* miss,
                                   Register receiver,
                                   Register name,
                                   Register result,
                                   Register scratch1,
                                   Register scratch2,
                                   Register scratch3,
                                   DictionaryCheck check_dictionary) {
  // Main use of the scratch registers.
  // scratch1: Used to hold the property dictionary.
  // scratch2: Used as temporary and to hold the capacity of the property
  //           dictionary.
  // scratch3: Used as temporary.

  Label done;

  // Check for the absence of an interceptor.
  // Load the map into scratch1.
  __ ldr(scratch1, FieldMemOperand(receiver, JSObject::kMapOffset));

  // Bail out if the receiver has a named interceptor.
  __ ldrb(scratch2, FieldMemOperand(scratch1, Map::kBitFieldOffset));
  __ tst(scratch2, Operand(1 << Map::kHasNamedInterceptor));
  __ b(nz, miss);

  // Bail out if we have a JS global proxy object.
  __ ldrb(scratch2, FieldMemOperand(scratch1, Map::kInstanceTypeOffset));
  __ cmp(scratch2, Operand(JS_GLOBAL_PROXY_TYPE));
  __ b(eq, miss);

  // Possible work-around for http://crbug.com/16276.
  // See also: http://codereview.chromium.org/155418.
  __ cmp(scratch2, Operand(JS_GLOBAL_OBJECT_TYPE));
  __ b(eq, miss);
  __ cmp(scratch2, Operand(JS_BUILTINS_OBJECT_TYPE));
  __ b(eq, miss);

  // Load the properties array.
  __ ldr(scratch1, FieldMemOperand(receiver, JSObject::kPropertiesOffset));

  // Check that the properties array is a dictionary.
  if (check_dictionary == CHECK_DICTIONARY) {
    __ ldr(scratch2, FieldMemOperand(scratch1, HeapObject::kMapOffset));
    __ LoadRoot(ip, Heap::kHashTableMapRootIndex);
    __ cmp(scratch2, ip);
    __ b(ne, miss);
  }

  // Compute the capacity mask.
  const int kCapacityOffset = StringDictionary::kHeaderSize +
      StringDictionary::kCapacityIndex * kPointerSize;
  __ ldr(scratch2, FieldMemOperand(scratch1, kCapacityOffset));
  __ mov(scratch2, Operand(scratch2, ASR, kSmiTagSize));  // convert smi to int
  __ sub(scratch2, scratch2, Operand(1));

  const int kElementsStartOffset = StringDictionary::kHeaderSize +
      StringDictionary::kElementsStartIndex * kPointerSize;

  // Generate an unrolled loop that performs a few probes before
  // giving up. Measurements done on Gmail indicate that 2 probes
  // cover ~93% of loads from dictionaries.
  static const int kProbes = 4;
  for (int i = 0; i < kProbes; i++) {
    // Compute the masked index: (hash + i + i * i) & mask.
    __ ldr(scratch3, FieldMemOperand(name, String::kHashFieldOffset));
    if (i > 0) {
      // Add the probe offset (i + i * i) left shifted to avoid right shifting
      // the hash in a separate instruction. The value hash + i + i * i is right
      // shifted in the following and instruction.
      ASSERT(StringDictionary::GetProbeOffset(i) <
             1 << (32 - String::kHashFieldOffset));
      __ add(scratch3, scratch3, Operand(
          StringDictionary::GetProbeOffset(i) << String::kHashShift));
    }
    __ and_(scratch3, scratch2, Operand(scratch3, LSR, String::kHashShift));

    // Scale the index by multiplying by the element size.
    ASSERT(StringDictionary::kEntrySize == 3);
    // scratch3 = scratch3 * 3.
    __ add(scratch3, scratch3, Operand(scratch3, LSL, 1));

    // Check if the key is identical to the name.
    __ add(scratch3, scratch1, Operand(scratch3, LSL, 2));
    __ ldr(ip, FieldMemOperand(scratch3, kElementsStartOffset));
    __ cmp(name, Operand(ip));
    if (i != kProbes - 1) {
      __ b(eq, &done);
    } else {
      __ b(ne, miss);
    }
  }

  // Check that the value is a normal property.
  __ bind(&done);  // scratch3 == scratch1 + 4 * index
  __ ldr(scratch2,
         FieldMemOperand(scratch3, kElementsStartOffset + 2 * kPointerSize));
  __ tst(scratch2, Operand(PropertyDetails::TypeField::mask() << kSmiTagSize));
  __ b(ne, miss);

  // Get the value at the masked, scaled index and return.
  __ ldr(result,
         FieldMemOperand(scratch3, kElementsStartOffset + 1 * kPointerSize));
}


static void GenerateNumberDictionaryLoad(MacroAssembler* masm,
                                         Label* miss,
                                         Register elements,
                                         Register key,
                                         Register t0,
                                         Register t1,
                                         Register t2) {
  // Register use:
  //
  // elements - holds the slow-case elements of the receiver and is unchanged.
  //
  // key      - holds the smi key on entry and is unchanged if a branch is
  //            performed to the miss label.
  //            Holds the result on exit if the load succeeded.
  //
  // Scratch registers:
  //
  // t0 - holds the untagged key on entry and holds the hash once computed.
  //
  // t1 - used to hold the capacity mask of the dictionary
  //
  // t2 - used for the index into the dictionary.
  Label done;

  // Compute the hash code from the untagged key.  This must be kept in sync
  // with ComputeIntegerHash in utils.h.
  //
  // hash = ~hash + (hash << 15);
  __ mvn(t1, Operand(t0));
  __ add(t0, t1, Operand(t0, LSL, 15));
  // hash = hash ^ (hash >> 12);
  __ eor(t0, t0, Operand(t0, LSR, 12));
  // hash = hash + (hash << 2);
  __ add(t0, t0, Operand(t0, LSL, 2));
  // hash = hash ^ (hash >> 4);
  __ eor(t0, t0, Operand(t0, LSR, 4));
  // hash = hash * 2057;
  __ mov(t1, Operand(2057));
  __ mul(t0, t0, t1);
  // hash = hash ^ (hash >> 16);
  __ eor(t0, t0, Operand(t0, LSR, 16));

  // Compute the capacity mask.
  __ ldr(t1, FieldMemOperand(elements, NumberDictionary::kCapacityOffset));
  __ mov(t1, Operand(t1, ASR, kSmiTagSize));  // convert smi to int
  __ sub(t1, t1, Operand(1));

  // Generate an unrolled loop that performs a few probes before giving up.
  static const int kProbes = 4;
  for (int i = 0; i < kProbes; i++) {
    // Use t2 for index calculations and keep the hash intact in t0.
    __ mov(t2, t0);
    // Compute the masked index: (hash + i + i * i) & mask.
    if (i > 0) {
      __ add(t2, t2, Operand(NumberDictionary::GetProbeOffset(i)));
    }
    __ and_(t2, t2, Operand(t1));

    // Scale the index by multiplying by the element size.
    ASSERT(NumberDictionary::kEntrySize == 3);
    __ add(t2, t2, Operand(t2, LSL, 1));  // t2 = t2 * 3

    // Check if the key is identical to the name.
    __ add(t2, elements, Operand(t2, LSL, kPointerSizeLog2));
    __ ldr(ip, FieldMemOperand(t2, NumberDictionary::kElementsStartOffset));
    __ cmp(key, Operand(ip));
    if (i != kProbes - 1) {
      __ b(eq, &done);
    } else {
      __ b(ne, miss);
    }
  }

  __ bind(&done);
  // Check that the value is a normal property.
  // t2: elements + (index * kPointerSize)
  const int kDetailsOffset =
      NumberDictionary::kElementsStartOffset + 2 * kPointerSize;
  __ ldr(t1, FieldMemOperand(t2, kDetailsOffset));
  __ tst(t1, Operand(Smi::FromInt(PropertyDetails::TypeField::mask())));
  __ b(ne, miss);

  // Get the value at the masked, scaled index and return.
  const int kValueOffset =
      NumberDictionary::kElementsStartOffset + kPointerSize;
  __ ldr(key, FieldMemOperand(t2, kValueOffset));
}


void LoadIC::GenerateArrayLength(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- r0    : receiver
  //  -- sp[0] : receiver
  // -----------------------------------
  Label miss;

  StubCompiler::GenerateLoadArrayLength(masm, r0, r3, &miss);
  __ bind(&miss);
  StubCompiler::GenerateLoadMiss(masm, Code::LOAD_IC);
}


void LoadIC::GenerateStringLength(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- r0    : receiver
  //  -- sp[0] : receiver
  // -----------------------------------
  Label miss;

  StubCompiler::GenerateLoadStringLength(masm, r0, r1, r3, &miss);
  // Cache miss: Jump to runtime.
  __ bind(&miss);
  StubCompiler::GenerateLoadMiss(masm, Code::LOAD_IC);
}


void LoadIC::GenerateFunctionPrototype(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- r0    : receiver
  //  -- sp[0] : receiver
  // -----------------------------------
  Label miss;

  StubCompiler::GenerateLoadFunctionPrototype(masm, r0, r1, r3, &miss);
  __ bind(&miss);
  StubCompiler::GenerateLoadMiss(masm, Code::LOAD_IC);
}


// Defined in ic.cc.
Object* CallIC_Miss(Arguments args);

void CallIC::GenerateMegamorphic(MacroAssembler* masm, int argc) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  // -----------------------------------
  Label number, non_number, non_string, boolean, probe, miss;

  // Get the receiver of the function from the stack into r1.
  __ ldr(r1, MemOperand(sp, argc * kPointerSize));

  // Probe the stub cache.
  Code::Flags flags =
      Code::ComputeFlags(Code::CALL_IC, NOT_IN_LOOP, MONOMORPHIC, NORMAL, argc);
  StubCache::GenerateProbe(masm, flags, r1, r2, r3, no_reg);

  // If the stub cache probing failed, the receiver might be a value.
  // For value objects, we use the map of the prototype objects for
  // the corresponding JSValue for the cache and that is what we need
  // to probe.
  //
  // Check for number.
  __ tst(r1, Operand(kSmiTagMask));
  __ b(eq, &number);
  __ CompareObjectType(r1, r3, r3, HEAP_NUMBER_TYPE);
  __ b(ne, &non_number);
  __ bind(&number);
  StubCompiler::GenerateLoadGlobalFunctionPrototype(
      masm, Context::NUMBER_FUNCTION_INDEX, r1);
  __ b(&probe);

  // Check for string.
  __ bind(&non_number);
  __ cmp(r3, Operand(FIRST_NONSTRING_TYPE));
  __ b(hs, &non_string);
  StubCompiler::GenerateLoadGlobalFunctionPrototype(
      masm, Context::STRING_FUNCTION_INDEX, r1);
  __ b(&probe);

  // Check for boolean.
  __ bind(&non_string);
  __ LoadRoot(ip, Heap::kTrueValueRootIndex);
  __ cmp(r1, ip);
  __ b(eq, &boolean);
  __ LoadRoot(ip, Heap::kFalseValueRootIndex);
  __ cmp(r1, ip);
  __ b(ne, &miss);
  __ bind(&boolean);
  StubCompiler::GenerateLoadGlobalFunctionPrototype(
      masm, Context::BOOLEAN_FUNCTION_INDEX, r1);

  // Probe the stub cache for the value object.
  __ bind(&probe);
  StubCache::GenerateProbe(masm, flags, r1, r2, r3, no_reg);

  // Cache miss: Jump to runtime.
  __ bind(&miss);
  GenerateMiss(masm, argc);
}


static void GenerateNormalHelper(MacroAssembler* masm,
                                 int argc,
                                 bool is_global_object,
                                 Label* miss,
                                 Register scratch) {
  // Search dictionary - put result in register r1.
  GenerateDictionaryLoad(masm, miss, r1, r2, r1, r0, r3, r4, CHECK_DICTIONARY);

  // Check that the value isn't a smi.
  __ tst(r1, Operand(kSmiTagMask));
  __ b(eq, miss);

  // Check that the value is a JSFunction.
  __ CompareObjectType(r1, scratch, scratch, JS_FUNCTION_TYPE);
  __ b(ne, miss);

  // Patch the receiver with the global proxy if necessary.
  if (is_global_object) {
    __ ldr(r0, MemOperand(sp, argc * kPointerSize));
    __ ldr(r0, FieldMemOperand(r0, GlobalObject::kGlobalReceiverOffset));
    __ str(r0, MemOperand(sp, argc * kPointerSize));
  }

  // Invoke the function.
  ParameterCount actual(argc);
  __ InvokeFunction(r1, actual, JUMP_FUNCTION);
}


void CallIC::GenerateNormal(MacroAssembler* masm, int argc) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss, global_object, non_global_object;

  // Get the receiver of the function from the stack into r1.
  __ ldr(r1, MemOperand(sp, argc * kPointerSize));

  // Check that the receiver isn't a smi.
  __ tst(r1, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the receiver is a valid JS object.  Put the map in r3.
  __ CompareObjectType(r1, r3, r0, FIRST_JS_OBJECT_TYPE);
  __ b(lt, &miss);

  // If this assert fails, we have to check upper bound too.
  ASSERT(LAST_TYPE == JS_FUNCTION_TYPE);

  // Check for access to global object.
  __ cmp(r0, Operand(JS_GLOBAL_OBJECT_TYPE));
  __ b(eq, &global_object);
  __ cmp(r0, Operand(JS_BUILTINS_OBJECT_TYPE));
  __ b(ne, &non_global_object);

  // Accessing global object: Load and invoke.
  __ bind(&global_object);
  // Check that the global object does not require access checks.
  __ ldrb(r3, FieldMemOperand(r3, Map::kBitFieldOffset));
  __ tst(r3, Operand(1 << Map::kIsAccessCheckNeeded));
  __ b(ne, &miss);
  GenerateNormalHelper(masm, argc, true, &miss, r4);

  // Accessing non-global object: Check for access to global proxy.
  Label global_proxy, invoke;
  __ bind(&non_global_object);
  __ cmp(r0, Operand(JS_GLOBAL_PROXY_TYPE));
  __ b(eq, &global_proxy);
  // Check that the non-global, non-global-proxy object does not
  // require access checks.
  __ ldrb(r3, FieldMemOperand(r3, Map::kBitFieldOffset));
  __ tst(r3, Operand(1 << Map::kIsAccessCheckNeeded));
  __ b(ne, &miss);
  __ bind(&invoke);
  GenerateNormalHelper(masm, argc, false, &miss, r4);

  // Global object access: Check access rights.
  __ bind(&global_proxy);
  __ CheckAccessGlobalProxy(r1, r0, &miss);
  __ b(&invoke);

  // Cache miss: Jump to runtime.
  __ bind(&miss);
  GenerateMiss(masm, argc);
}


void CallIC::GenerateMiss(MacroAssembler* masm, int argc) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  // -----------------------------------

  // Get the receiver of the function from the stack.
  __ ldr(r3, MemOperand(sp, argc * kPointerSize));

  __ EnterInternalFrame();

  // Push the receiver and the name of the function.
  __ Push(r3, r2);

  // Call the entry.
  __ mov(r0, Operand(2));
  __ mov(r1, Operand(ExternalReference(IC_Utility(kCallIC_Miss))));

  CEntryStub stub(1);
  __ CallStub(&stub);

  // Move result to r1 and leave the internal frame.
  __ mov(r1, Operand(r0));
  __ LeaveInternalFrame();

  // Check if the receiver is a global object of some sort.
  Label invoke, global;
  __ ldr(r2, MemOperand(sp, argc * kPointerSize));  // receiver
  __ tst(r2, Operand(kSmiTagMask));
  __ b(eq, &invoke);
  __ CompareObjectType(r2, r3, r3, JS_GLOBAL_OBJECT_TYPE);
  __ b(eq, &global);
  __ cmp(r3, Operand(JS_BUILTINS_OBJECT_TYPE));
  __ b(ne, &invoke);

  // Patch the receiver on the stack.
  __ bind(&global);
  __ ldr(r2, FieldMemOperand(r2, GlobalObject::kGlobalReceiverOffset));
  __ str(r2, MemOperand(sp, argc * kPointerSize));

  // Invoke the function.
  ParameterCount actual(argc);
  __ bind(&invoke);
  __ InvokeFunction(r1, actual, JUMP_FUNCTION);
}


// Defined in ic.cc.
Object* LoadIC_Miss(Arguments args);

void LoadIC::GenerateMegamorphic(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- r0    : receiver
  //  -- sp[0] : receiver
  // -----------------------------------

  // Probe the stub cache.
  Code::Flags flags = Code::ComputeFlags(Code::LOAD_IC,
                                         NOT_IN_LOOP,
                                         MONOMORPHIC);
  StubCache::GenerateProbe(masm, flags, r0, r2, r3, no_reg);

  // Cache miss: Jump to runtime.
  GenerateMiss(masm);
}


void LoadIC::GenerateNormal(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- r0    : receiver
  //  -- sp[0] : receiver
  // -----------------------------------
  Label miss, probe, global;

  // Check that the receiver isn't a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &miss);

  // Check that the receiver is a valid JS object.  Put the map in r3.
  __ CompareObjectType(r0, r3, r1, FIRST_JS_OBJECT_TYPE);
  __ b(lt, &miss);
  // If this assert fails, we have to check upper bound too.
  ASSERT(LAST_TYPE == JS_FUNCTION_TYPE);

  // Check for access to global object (unlikely).
  __ cmp(r1, Operand(JS_GLOBAL_PROXY_TYPE));
  __ b(eq, &global);

  // Check for non-global object that requires access check.
  __ ldrb(r3, FieldMemOperand(r3, Map::kBitFieldOffset));
  __ tst(r3, Operand(1 << Map::kIsAccessCheckNeeded));
  __ b(ne, &miss);

  __ bind(&probe);
  GenerateDictionaryLoad(masm, &miss, r0, r2, r0, r1, r3, r4, CHECK_DICTIONARY);
  __ Ret();

  // Global object access: Check access rights.
  __ bind(&global);
  __ CheckAccessGlobalProxy(r0, r1, &miss);
  __ b(&probe);

  // Cache miss: Jump to runtime.
  __ bind(&miss);
  GenerateMiss(masm);
}


void LoadIC::GenerateMiss(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r2    : name
  //  -- lr    : return address
  //  -- r0    : receiver
  //  -- sp[0] : receiver
  // -----------------------------------

  __ mov(r3, r0);
  __ Push(r3, r2);

  // Perform tail call to the entry.
  ExternalReference ref = ExternalReference(IC_Utility(kLoadIC_Miss));
  __ TailCallExternalReference(ref, 2, 1);
}


static inline bool IsInlinedICSite(Address address,
                                   Address* inline_end_address) {
  // If the instruction after the call site is not the pseudo instruction nop1
  // then this is not related to an inlined in-object property load. The nop1
  // instruction is located just after the call to the IC in the deferred code
  // handling the miss in the inlined code. After the nop1 instruction there is
  // a branch instruction for jumping back from the deferred code.
  Address address_after_call = address + Assembler::kCallTargetAddressOffset;
  Instr instr_after_call = Assembler::instr_at(address_after_call);
  if (!Assembler::IsNop(instr_after_call, PROPERTY_ACCESS_INLINED)) {
    return false;
  }
  Address address_after_nop = address_after_call + Assembler::kInstrSize;
  Instr instr_after_nop = Assembler::instr_at(address_after_nop);
  // There may be some reg-reg move and frame merging code to skip over before
  // the branch back from the DeferredReferenceGetKeyedValue code to the inlined
  // code.
  while (!Assembler::IsBranch(instr_after_nop)) {
    address_after_nop += Assembler::kInstrSize;
    instr_after_nop = Assembler::instr_at(address_after_nop);
  }

  // Find the end of the inlined code for handling the load.
  int b_offset =
      Assembler::GetBranchOffset(instr_after_nop) + Assembler::kPcLoadDelta;
  ASSERT(b_offset < 0);  // Jumping back from deferred code.
  *inline_end_address = address_after_nop + b_offset;

  return true;
}


void LoadIC::ClearInlinedVersion(Address address) {
  // Reset the map check of the inlined in-object property load (if present) to
  // guarantee failure by holding an invalid map (the null value). The offset
  // can be patched to anything.
  PatchInlinedLoad(address, Heap::null_value(), 0);
}


bool LoadIC::PatchInlinedLoad(Address address, Object* map, int offset) {
  // Find the end of the inlined code for handling the load if this is an
  // inlined IC call site.
  Address inline_end_address;
  if (!IsInlinedICSite(address, &inline_end_address)) return false;

  // Patch the offset of the property load instruction (ldr r0, [r1, #+XXX]).
  // The immediate must be representable in 12 bits.
  ASSERT((JSObject::kMaxInstanceSize - JSObject::kHeaderSize) < (1 << 12));
  Address ldr_property_instr_address =
      inline_end_address - Assembler::kInstrSize;
  ASSERT(Assembler::IsLdrRegisterImmediate(
      Assembler::instr_at(ldr_property_instr_address)));
  Instr ldr_property_instr = Assembler::instr_at(ldr_property_instr_address);
  ldr_property_instr = Assembler::SetLdrRegisterImmediateOffset(
      ldr_property_instr, offset - kHeapObjectTag);
  Assembler::instr_at_put(ldr_property_instr_address, ldr_property_instr);

  // Indicate that code has changed.
  CPU::FlushICache(ldr_property_instr_address, 1 * Assembler::kInstrSize);

  // Patch the map check.
  Address ldr_map_instr_address =
      inline_end_address - 4 * Assembler::kInstrSize;
  Assembler::set_target_address_at(ldr_map_instr_address,
                                   reinterpret_cast<Address>(map));
  return true;
}


void KeyedLoadIC::ClearInlinedVersion(Address address) {
  // Reset the map check of the inlined keyed load (if present) to
  // guarantee failure by holding an invalid map (the null value).
  PatchInlinedLoad(address, Heap::null_value());
}


bool KeyedLoadIC::PatchInlinedLoad(Address address, Object* map) {
  Address inline_end_address;
  if (!IsInlinedICSite(address, &inline_end_address)) return false;

  // Patch the map check.
  Address ldr_map_instr_address =
      inline_end_address -
      (CodeGenerator::kInlinedKeyedLoadInstructionsAfterPatch *
      Assembler::kInstrSize);
  Assembler::set_target_address_at(ldr_map_instr_address,
                                   reinterpret_cast<Address>(map));
  return true;
}


void KeyedStoreIC::ClearInlinedVersion(Address address) {
  // Insert null as the elements map to check for.  This will make
  // sure that the elements fast-case map check fails so that control
  // flows to the IC instead of the inlined version.
  PatchInlinedStore(address, Heap::null_value());
}


void KeyedStoreIC::RestoreInlinedVersion(Address address) {
  // Restore the fast-case elements map check so that the inlined
  // version can be used again.
  PatchInlinedStore(address, Heap::fixed_array_map());
}


bool KeyedStoreIC::PatchInlinedStore(Address address, Object* map) {
  // Find the end of the inlined code for handling the store if this is an
  // inlined IC call site.
  Address inline_end_address;
  if (!IsInlinedICSite(address, &inline_end_address)) return false;

  // Patch the map check.
  Address ldr_map_instr_address =
      inline_end_address -
      (CodeGenerator::kInlinedKeyedStoreInstructionsAfterPatch *
      Assembler::kInstrSize);
  Assembler::set_target_address_at(ldr_map_instr_address,
                                   reinterpret_cast<Address>(map));
  return true;
}


Object* KeyedLoadIC_Miss(Arguments args);


void KeyedLoadIC::GenerateMiss(MacroAssembler* masm) {
  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r0     : key
  //  -- r1     : receiver
  // -----------------------------------

  __ Push(r1, r0);

  ExternalReference ref = ExternalReference(IC_Utility(kKeyedLoadIC_Miss));
  __ TailCallExternalReference(ref, 2, 1);
}


void KeyedLoadIC::GenerateRuntimeGetProperty(MacroAssembler* masm) {
  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r0     : key
  //  -- r1     : receiver
  // -----------------------------------

  __ Push(r1, r0);

  __ TailCallRuntime(Runtime::kKeyedGetProperty, 2, 1);
}


void KeyedLoadIC::GenerateGeneric(MacroAssembler* masm) {
  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r0     : key
  //  -- r1     : receiver
  // -----------------------------------
  Label slow, check_string, index_smi, index_string;
  Label check_pixel_array, probe_dictionary, check_number_dictionary;

  Register key = r0;
  Register receiver = r1;

  // Check that the object isn't a smi.
  __ BranchOnSmi(receiver, &slow);
  // Get the map of the receiver.
  __ ldr(r2, FieldMemOperand(receiver, HeapObject::kMapOffset));
  // Check bit field.
  __ ldrb(r3, FieldMemOperand(r2, Map::kBitFieldOffset));
  __ tst(r3, Operand(kSlowCaseBitFieldMask));
  __ b(ne, &slow);
  // Check that the object is some kind of JS object EXCEPT JS Value type.
  // In the case that the object is a value-wrapper object,
  // we enter the runtime system to make sure that indexing into string
  // objects work as intended.
  ASSERT(JS_OBJECT_TYPE > JS_VALUE_TYPE);
  __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
  __ cmp(r2, Operand(JS_OBJECT_TYPE));
  __ b(lt, &slow);

  // Check that the key is a smi.
  __ BranchOnNotSmi(key, &check_string);
  __ bind(&index_smi);
  // Now the key is known to be a smi. This place is also jumped to from below
  // where a numeric string is converted to a smi.
  __ ldr(r4, FieldMemOperand(receiver, JSObject::kElementsOffset));
  // Check that the object is in fast mode (not dictionary).
  __ ldr(r3, FieldMemOperand(r4, HeapObject::kMapOffset));
  __ LoadRoot(ip, Heap::kFixedArrayMapRootIndex);
  __ cmp(r3, ip);
  __ b(ne, &check_pixel_array);
  // Check that the key (index) is within bounds.
  __ ldr(r3, FieldMemOperand(r4, FixedArray::kLengthOffset));
  __ cmp(key, Operand(r3));
  __ b(hs, &slow);
  // Fast case: Do the load.
  __ add(r3, r4, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  // The key is a smi.
  ASSERT(kSmiTag == 0 && kSmiTagSize < kPointerSizeLog2);
  __ ldr(r2, MemOperand(r3, key, LSL, kPointerSizeLog2 - kSmiTagSize));
  __ LoadRoot(ip, Heap::kTheHoleValueRootIndex);
  __ cmp(r2, ip);
  // In case the loaded value is the_hole we have to consult GetProperty
  // to ensure the prototype chain is searched.
  __ b(eq, &slow);
  __ mov(r0, r2);
  __ IncrementCounter(&Counters::keyed_load_generic_smi, 1, r2, r3);
  __ Ret();

  // Check whether the elements is a pixel array.
  // r0: key
  // r3: elements map
  // r4: elements
  __ bind(&check_pixel_array);
  __ LoadRoot(ip, Heap::kPixelArrayMapRootIndex);
  __ cmp(r3, ip);
  __ b(ne, &check_number_dictionary);
  __ ldr(ip, FieldMemOperand(r4, PixelArray::kLengthOffset));
  __ mov(r2, Operand(key, ASR, kSmiTagSize));
  __ cmp(r2, ip);
  __ b(hs, &slow);
  __ ldr(ip, FieldMemOperand(r4, PixelArray::kExternalPointerOffset));
  __ ldrb(r2, MemOperand(ip, r2));
  __ mov(r0, Operand(r2, LSL, kSmiTagSize));  // Tag result as smi.
  __ Ret();

  __ bind(&check_number_dictionary);
  // Check whether the elements is a number dictionary.
  // r0: key
  // r3: elements map
  // r4: elements
  __ LoadRoot(ip, Heap::kHashTableMapRootIndex);
  __ cmp(r3, ip);
  __ b(ne, &slow);
  __ mov(r2, Operand(r0, ASR, kSmiTagSize));
  GenerateNumberDictionaryLoad(masm, &slow, r4, r0, r2, r3, r5);
  __ Ret();

  // Slow case, key and receiver still in r0 and r1.
  __ bind(&slow);
  __ IncrementCounter(&Counters::keyed_load_generic_slow, 1, r2, r3);
  GenerateRuntimeGetProperty(masm);

  __ bind(&check_string);
  // The key is not a smi.
  // Is it a string?
  // r0: key
  // r1: receiver
  __ CompareObjectType(r0, r2, r3, FIRST_NONSTRING_TYPE);
  __ b(ge, &slow);

  // Is the string an array index, with cached numeric value?
  __ ldr(r3, FieldMemOperand(r0, String::kHashFieldOffset));
  __ tst(r3, Operand(String::kIsArrayIndexMask));
  __ b(ne, &index_string);

  // Is the string a symbol?
  // r2: key map
  __ ldrb(r3, FieldMemOperand(r2, Map::kInstanceTypeOffset));
  ASSERT(kSymbolTag != 0);
  __ tst(r3, Operand(kIsSymbolMask));
  __ b(eq, &slow);

  // If the receiver is a fast-case object, check the keyed lookup
  // cache. Otherwise probe the dictionary.
  __ ldr(r3, FieldMemOperand(r1, JSObject::kPropertiesOffset));
  __ ldr(r3, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ LoadRoot(ip, Heap::kHashTableMapRootIndex);
  __ cmp(r3, ip);
  __ b(eq, &probe_dictionary);

  // Load the map of the receiver, compute the keyed lookup cache hash
  // based on 32 bits of the map pointer and the string hash.
  __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));
  __ mov(r3, Operand(r2, ASR, KeyedLookupCache::kMapHashShift));
  __ ldr(r4, FieldMemOperand(r0, String::kHashFieldOffset));
  __ eor(r3, r3, Operand(r4, ASR, String::kHashShift));
  __ and_(r3, r3, Operand(KeyedLookupCache::kCapacityMask));

  // Load the key (consisting of map and symbol) from the cache and
  // check for match.
  ExternalReference cache_keys = ExternalReference::keyed_lookup_cache_keys();
  __ mov(r4, Operand(cache_keys));
  __ add(r4, r4, Operand(r3, LSL, kPointerSizeLog2 + 1));
  __ ldr(r5, MemOperand(r4, kPointerSize, PostIndex));  // Move r4 to symbol.
  __ cmp(r2, r5);
  __ b(ne, &slow);
  __ ldr(r5, MemOperand(r4));
  __ cmp(r0, r5);
  __ b(ne, &slow);

  // Get field offset and check that it is an in-object property.
  // r0     : key
  // r1     : receiver
  // r2     : receiver's map
  // r3     : lookup cache index
  ExternalReference cache_field_offsets
      = ExternalReference::keyed_lookup_cache_field_offsets();
  __ mov(r4, Operand(cache_field_offsets));
  __ ldr(r5, MemOperand(r4, r3, LSL, kPointerSizeLog2));
  __ ldrb(r6, FieldMemOperand(r2, Map::kInObjectPropertiesOffset));
  __ cmp(r5, r6);
  __ b(ge, &slow);

  // Load in-object property.
  __ sub(r5, r5, r6);  // Index from end of object.
  __ ldrb(r6, FieldMemOperand(r2, Map::kInstanceSizeOffset));
  __ add(r6, r6, r5);  // Index from start of object.
  __ sub(r1, r1, Operand(kHeapObjectTag));  // Remove the heap tag.
  __ ldr(r0, MemOperand(r1, r6, LSL, kPointerSizeLog2));
  __ IncrementCounter(&Counters::keyed_load_generic_lookup_cache, 1, r2, r3);
  __ Ret();

  // Do a quick inline probe of the receiver's dictionary, if it
  // exists.
  __ bind(&probe_dictionary);
  // Load the property to r0.
  GenerateDictionaryLoad(
      masm, &slow, r1, r0, r0, r2, r3, r4, DICTIONARY_CHECK_DONE);
  __ IncrementCounter(&Counters::keyed_load_generic_symbol, 1, r2, r3);
  __ Ret();

  __ b(&slow);
  // If the hash field contains an array index pick it out. The assert checks
  // that the constants for the maximum number of digits for an array index
  // cached in the hash field and the number of bits reserved for it does not
  // conflict.
  ASSERT(TenToThe(String::kMaxCachedArrayIndexLength) <
         (1 << String::kArrayIndexValueBits));
  __ bind(&index_string);
  // r0: key (string)
  // r1: receiver
  // r3: hash field
  // We want the smi-tagged index in r0.  kArrayIndexValueMask has zeros in
  // the low kHashShift bits.
  ASSERT(String::kHashShift >= kSmiTagSize);
  __ and_(r3, r3, Operand(String::kArrayIndexValueMask));
  // Here we actually clobber the key (r0) which will be used if calling into
  // runtime later. However as the new key is the numeric value of a string key
  // there is no difference in using either key.
  __ mov(r0, Operand(r3, ASR, String::kHashShift - kSmiTagSize));
  // Now jump to the place where smi keys are handled.
  __ jmp(&index_smi);
}


void KeyedLoadIC::GenerateString(MacroAssembler* masm) {
  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r0     : key (index)
  //  -- r1     : receiver
  // -----------------------------------
  Label miss;
  Label index_out_of_range;

  Register receiver = r1;
  Register index = r0;
  Register scratch1 = r2;
  Register scratch2 = r3;
  Register result = r0;

  StringCharAtGenerator char_at_generator(receiver,
                                          index,
                                          scratch1,
                                          scratch2,
                                          result,
                                          &miss,  // When not a string.
                                          &miss,  // When not a number.
                                          &index_out_of_range,
                                          STRING_INDEX_IS_ARRAY_INDEX);
  char_at_generator.GenerateFast(masm);
  __ Ret();

  ICRuntimeCallHelper call_helper;
  char_at_generator.GenerateSlow(masm, call_helper);

  __ bind(&index_out_of_range);
  __ LoadRoot(r0, Heap::kUndefinedValueRootIndex);
  __ Ret();

  __ bind(&miss);
  GenerateMiss(masm);
}


// Convert unsigned integer with specified number of leading zeroes in binary
// representation to IEEE 754 double.
// Integer to convert is passed in register hiword.
// Resulting double is returned in registers hiword:loword.
// This functions does not work correctly for 0.
static void GenerateUInt2Double(MacroAssembler* masm,
                                Register hiword,
                                Register loword,
                                Register scratch,
                                int leading_zeroes) {
  const int meaningful_bits = kBitsPerInt - leading_zeroes - 1;
  const int biased_exponent = HeapNumber::kExponentBias + meaningful_bits;

  const int mantissa_shift_for_hi_word =
      meaningful_bits - HeapNumber::kMantissaBitsInTopWord;

  const int mantissa_shift_for_lo_word =
      kBitsPerInt - mantissa_shift_for_hi_word;

  __ mov(scratch, Operand(biased_exponent << HeapNumber::kExponentShift));
  if (mantissa_shift_for_hi_word > 0) {
    __ mov(loword, Operand(hiword, LSL, mantissa_shift_for_lo_word));
    __ orr(hiword, scratch, Operand(hiword, LSR, mantissa_shift_for_hi_word));
  } else {
    __ mov(loword, Operand(0));
    __ orr(hiword, scratch, Operand(hiword, LSL, mantissa_shift_for_hi_word));
  }

  // If least significant bit of biased exponent was not 1 it was corrupted
  // by most significant bit of mantissa so we should fix that.
  if (!(biased_exponent & 1)) {
    __ bic(hiword, hiword, Operand(1 << HeapNumber::kExponentShift));
  }
}


void KeyedLoadIC::GenerateExternalArray(MacroAssembler* masm,
                                        ExternalArrayType array_type) {
  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r0     : key
  //  -- r1     : receiver
  // -----------------------------------
  Label slow, failed_allocation;

  Register key = r0;
  Register receiver = r1;

  // Check that the object isn't a smi
  __ BranchOnSmi(receiver, &slow);

  // Check that the key is a smi.
  __ BranchOnNotSmi(key, &slow);

  // Check that the object is a JS object. Load map into r2.
  __ CompareObjectType(receiver, r2, r3, FIRST_JS_OBJECT_TYPE);
  __ b(lt, &slow);

  // Check that the receiver does not require access checks.  We need
  // to check this explicitly since this generic stub does not perform
  // map checks.
  __ ldrb(r3, FieldMemOperand(r2, Map::kBitFieldOffset));
  __ tst(r3, Operand(1 << Map::kIsAccessCheckNeeded));
  __ b(ne, &slow);

  // Check that the elements array is the appropriate type of
  // ExternalArray.
  __ ldr(r3, FieldMemOperand(receiver, JSObject::kElementsOffset));
  __ ldr(r2, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ LoadRoot(ip, Heap::RootIndexForExternalArrayType(array_type));
  __ cmp(r2, ip);
  __ b(ne, &slow);

  // Check that the index is in range.
  __ ldr(ip, FieldMemOperand(r3, ExternalArray::kLengthOffset));
  __ cmp(ip, Operand(key, ASR, kSmiTagSize));
  // Unsigned comparison catches both negative and too-large values.
  __ b(lo, &slow);

  // r3: elements array
  __ ldr(r3, FieldMemOperand(r3, ExternalArray::kExternalPointerOffset));
  // r3: base pointer of external storage

  // We are not untagging smi key and instead work with it
  // as if it was premultiplied by 2.
  ASSERT((kSmiTag == 0) && (kSmiTagSize == 1));

  Register value = r2;
  switch (array_type) {
    case kExternalByteArray:
      __ ldrsb(value, MemOperand(r3, key, LSR, 1));
      break;
    case kExternalUnsignedByteArray:
      __ ldrb(value, MemOperand(r3, key, LSR, 1));
      break;
    case kExternalShortArray:
      __ ldrsh(value, MemOperand(r3, key, LSL, 0));
      break;
    case kExternalUnsignedShortArray:
      __ ldrh(value, MemOperand(r3, key, LSL, 0));
      break;
    case kExternalIntArray:
    case kExternalUnsignedIntArray:
      __ ldr(value, MemOperand(r3, key, LSL, 1));
      break;
    case kExternalFloatArray:
      if (CpuFeatures::IsSupported(VFP3)) {
        CpuFeatures::Scope scope(VFP3);
        __ add(r2, r3, Operand(key, LSL, 1));
        __ vldr(s0, r2, 0);
      } else {
        __ ldr(value, MemOperand(r3, key, LSL, 1));
      }
      break;
    default:
      UNREACHABLE();
      break;
  }

  // For integer array types:
  // r2: value
  // For floating-point array type
  // s0: value (if VFP3 is supported)
  // r2: value (if VFP3 is not supported)

  if (array_type == kExternalIntArray) {
    // For the Int and UnsignedInt array types, we need to see whether
    // the value can be represented in a Smi. If not, we need to convert
    // it to a HeapNumber.
    Label box_int;
    __ cmp(value, Operand(0xC0000000));
    __ b(mi, &box_int);
    // Tag integer as smi and return it.
    __ mov(r0, Operand(value, LSL, kSmiTagSize));
    __ Ret();

    __ bind(&box_int);
    // Allocate a HeapNumber for the result and perform int-to-double
    // conversion. Use r0 for result as key is not needed any more.
    __ AllocateHeapNumber(r0, r3, r4, &slow);

    if (CpuFeatures::IsSupported(VFP3)) {
      CpuFeatures::Scope scope(VFP3);
      __ vmov(s0, value);
      __ vcvt_f64_s32(d0, s0);
      __ sub(r3, r0, Operand(kHeapObjectTag));
      __ vstr(d0, r3, HeapNumber::kValueOffset);
      __ Ret();
    } else {
      WriteInt32ToHeapNumberStub stub(value, r0, r3);
      __ TailCallStub(&stub);
    }
  } else if (array_type == kExternalUnsignedIntArray) {
    // The test is different for unsigned int values. Since we need
    // the value to be in the range of a positive smi, we can't
    // handle either of the top two bits being set in the value.
    if (CpuFeatures::IsSupported(VFP3)) {
      CpuFeatures::Scope scope(VFP3);
      Label box_int, done;
      __ tst(value, Operand(0xC0000000));
      __ b(ne, &box_int);
      // Tag integer as smi and return it.
      __ mov(r0, Operand(value, LSL, kSmiTagSize));
      __ Ret();

      __ bind(&box_int);
      __ vmov(s0, value);
      // Allocate a HeapNumber for the result and perform int-to-double
      // conversion. Don't use r0 and r1 as AllocateHeapNumber clobbers all
      // registers - also when jumping due to exhausted young space.
      __ AllocateHeapNumber(r2, r3, r4, &slow);

      __ vcvt_f64_u32(d0, s0);
      __ sub(r1, r2, Operand(kHeapObjectTag));
      __ vstr(d0, r1, HeapNumber::kValueOffset);

      __ mov(r0, r2);
      __ Ret();
    } else {
      // Check whether unsigned integer fits into smi.
      Label box_int_0, box_int_1, done;
      __ tst(value, Operand(0x80000000));
      __ b(ne, &box_int_0);
      __ tst(value, Operand(0x40000000));
      __ b(ne, &box_int_1);
      // Tag integer as smi and return it.
      __ mov(r0, Operand(value, LSL, kSmiTagSize));
      __ Ret();

      Register hiword = value;  // r2.
      Register loword = r3;

      __ bind(&box_int_0);
      // Integer does not have leading zeros.
      GenerateUInt2Double(masm, hiword, loword, r4, 0);
      __ b(&done);

      __ bind(&box_int_1);
      // Integer has one leading zero.
      GenerateUInt2Double(masm, hiword, loword, r4, 1);


      __ bind(&done);
      // Integer was converted to double in registers hiword:loword.
      // Wrap it into a HeapNumber. Don't use r0 and r1 as AllocateHeapNumber
      // clobbers all registers - also when jumping due to exhausted young
      // space.
      __ AllocateHeapNumber(r4, r5, r6, &slow);

      __ str(hiword, FieldMemOperand(r4, HeapNumber::kExponentOffset));
      __ str(loword, FieldMemOperand(r4, HeapNumber::kMantissaOffset));

      __ mov(r0, r4);
      __ Ret();
    }
  } else if (array_type == kExternalFloatArray) {
    // For the floating-point array type, we need to always allocate a
    // HeapNumber.
    if (CpuFeatures::IsSupported(VFP3)) {
      CpuFeatures::Scope scope(VFP3);
      // Allocate a HeapNumber for the result. Don't use r0 and r1 as
      // AllocateHeapNumber clobbers all registers - also when jumping due to
      // exhausted young space.
      __ AllocateHeapNumber(r2, r3, r4, &slow);
      __ vcvt_f64_f32(d0, s0);
      __ sub(r1, r2, Operand(kHeapObjectTag));
      __ vstr(d0, r1, HeapNumber::kValueOffset);

      __ mov(r0, r2);
      __ Ret();
    } else {
      // Allocate a HeapNumber for the result. Don't use r0 and r1 as
      // AllocateHeapNumber clobbers all registers - also when jumping due to
      // exhausted young space.
      __ AllocateHeapNumber(r3, r4, r5, &slow);
      // VFP is not available, do manual single to double conversion.

      // r2: floating point value (binary32)
      // r3: heap number for result

      // Extract mantissa to r0. OK to clobber r0 now as there are no jumps to
      // the slow case from here.
      __ and_(r0, value, Operand(kBinary32MantissaMask));

      // Extract exponent to r1. OK to clobber r1 now as there are no jumps to
      // the slow case from here.
      __ mov(r1, Operand(value, LSR, kBinary32MantissaBits));
      __ and_(r1, r1, Operand(kBinary32ExponentMask >> kBinary32MantissaBits));

      Label exponent_rebiased;
      __ teq(r1, Operand(0x00));
      __ b(eq, &exponent_rebiased);

      __ teq(r1, Operand(0xff));
      __ mov(r1, Operand(0x7ff), LeaveCC, eq);
      __ b(eq, &exponent_rebiased);

      // Rebias exponent.
      __ add(r1,
             r1,
             Operand(-kBinary32ExponentBias + HeapNumber::kExponentBias));

      __ bind(&exponent_rebiased);
      __ and_(r2, value, Operand(kBinary32SignMask));
      value = no_reg;
      __ orr(r2, r2, Operand(r1, LSL, HeapNumber::kMantissaBitsInTopWord));

      // Shift mantissa.
      static const int kMantissaShiftForHiWord =
          kBinary32MantissaBits - HeapNumber::kMantissaBitsInTopWord;

      static const int kMantissaShiftForLoWord =
          kBitsPerInt - kMantissaShiftForHiWord;

      __ orr(r2, r2, Operand(r0, LSR, kMantissaShiftForHiWord));
      __ mov(r0, Operand(r0, LSL, kMantissaShiftForLoWord));

      __ str(r2, FieldMemOperand(r3, HeapNumber::kExponentOffset));
      __ str(r0, FieldMemOperand(r3, HeapNumber::kMantissaOffset));

      __ mov(r0, r3);
      __ Ret();
    }

  } else {
    // Tag integer as smi and return it.
    __ mov(r0, Operand(value, LSL, kSmiTagSize));
    __ Ret();
  }

  // Slow case, key and receiver still in r0 and r1.
  __ bind(&slow);
  __ IncrementCounter(&Counters::keyed_load_external_array_slow, 1, r2, r3);
  GenerateRuntimeGetProperty(masm);
}


void KeyedLoadIC::GenerateIndexedInterceptor(MacroAssembler* masm) {
  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r0     : key
  //  -- r1     : receiver
  // -----------------------------------
  Label slow;

  // Check that the receiver isn't a smi.
  __ BranchOnSmi(r1, &slow);

  // Check that the key is a smi.
  __ BranchOnNotSmi(r0, &slow);

  // Get the map of the receiver.
  __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));

  // Check that it has indexed interceptor and access checks
  // are not enabled for this object.
  __ ldrb(r3, FieldMemOperand(r2, Map::kBitFieldOffset));
  __ and_(r3, r3, Operand(kSlowCaseBitFieldMask));
  __ cmp(r3, Operand(1 << Map::kHasIndexedInterceptor));
  __ b(ne, &slow);

  // Everything is fine, call runtime.
  __ Push(r1, r0);  // Receiver, key.

  // Perform tail call to the entry.
  __ TailCallExternalReference(ExternalReference(
        IC_Utility(kKeyedLoadPropertyWithInterceptor)), 2, 1);

  __ bind(&slow);
  GenerateMiss(masm);
}


void KeyedStoreIC::GenerateMiss(MacroAssembler* masm) {
  // ---------- S t a t e --------------
  //  -- r0     : value
  //  -- r1     : key
  //  -- r2     : receiver
  //  -- lr     : return address
  // -----------------------------------

  // Push receiver, key and value for runtime call.
  __ Push(r2, r1, r0);

  ExternalReference ref = ExternalReference(IC_Utility(kKeyedStoreIC_Miss));
  __ TailCallExternalReference(ref, 3, 1);
}


void KeyedStoreIC::GenerateRuntimeSetProperty(MacroAssembler* masm) {
  // ---------- S t a t e --------------
  //  -- r0     : value
  //  -- r1     : key
  //  -- r2     : receiver
  //  -- lr     : return address
  // -----------------------------------

  // Push receiver, key and value for runtime call.
  __ Push(r2, r1, r0);

  __ TailCallRuntime(Runtime::kSetProperty, 3, 1);
}


void KeyedStoreIC::GenerateGeneric(MacroAssembler* masm) {
  // ---------- S t a t e --------------
  //  -- r0     : value
  //  -- r1     : key
  //  -- r2     : receiver
  //  -- lr     : return address
  // -----------------------------------
  Label slow, fast, array, extra, check_pixel_array;

  // Register usage.
  Register value = r0;
  Register key = r1;
  Register receiver = r2;
  Register elements = r3;  // Elements array of the receiver.
  // r4 and r5 are used as general scratch registers.

  // Check that the key is a smi.
  __ tst(key, Operand(kSmiTagMask));
  __ b(ne, &slow);
  // Check that the object isn't a smi.
  __ tst(receiver, Operand(kSmiTagMask));
  __ b(eq, &slow);
  // Get the map of the object.
  __ ldr(r4, FieldMemOperand(receiver, HeapObject::kMapOffset));
  // Check that the receiver does not require access checks.  We need
  // to do this because this generic stub does not perform map checks.
  __ ldrb(ip, FieldMemOperand(r4, Map::kBitFieldOffset));
  __ tst(ip, Operand(1 << Map::kIsAccessCheckNeeded));
  __ b(ne, &slow);
  // Check if the object is a JS array or not.
  __ ldrb(r4, FieldMemOperand(r4, Map::kInstanceTypeOffset));
  __ cmp(r4, Operand(JS_ARRAY_TYPE));
  __ b(eq, &array);
  // Check that the object is some kind of JS object.
  __ cmp(r4, Operand(FIRST_JS_OBJECT_TYPE));
  __ b(lt, &slow);

  // Object case: Check key against length in the elements array.
  __ ldr(elements, FieldMemOperand(receiver, JSObject::kElementsOffset));
  // Check that the object is in fast mode (not dictionary).
  __ ldr(r4, FieldMemOperand(elements, HeapObject::kMapOffset));
  __ LoadRoot(ip, Heap::kFixedArrayMapRootIndex);
  __ cmp(r4, ip);
  __ b(ne, &check_pixel_array);
  // Check array bounds. Both the key and the length of FixedArray are smis.
  __ ldr(ip, FieldMemOperand(elements, FixedArray::kLengthOffset));
  __ cmp(key, Operand(ip));
  __ b(lo, &fast);

  // Slow case, handle jump to runtime.
  __ bind(&slow);
  // Entry registers are intact.
  // r0: value.
  // r1: key.
  // r2: receiver.
  GenerateRuntimeSetProperty(masm);

  // Check whether the elements is a pixel array.
  // r4: elements map.
  __ bind(&check_pixel_array);
  __ LoadRoot(ip, Heap::kPixelArrayMapRootIndex);
  __ cmp(r4, ip);
  __ b(ne, &slow);
  // Check that the value is a smi. If a conversion is needed call into the
  // runtime to convert and clamp.
  __ BranchOnNotSmi(value, &slow);
  __ mov(r4, Operand(key, ASR, kSmiTagSize));  // Untag the key.
  __ ldr(ip, FieldMemOperand(elements, PixelArray::kLengthOffset));
  __ cmp(r4, Operand(ip));
  __ b(hs, &slow);
  __ mov(r5, Operand(value, ASR, kSmiTagSize));  // Untag the value.
  {  // Clamp the value to [0..255].
    Label done;
    __ tst(r5, Operand(0xFFFFFF00));
    __ b(eq, &done);
    __ mov(r5, Operand(0), LeaveCC, mi);  // 0 if negative.
    __ mov(r5, Operand(255), LeaveCC, pl);  // 255 if positive.
    __ bind(&done);
  }
  // Get the pointer to the external array. This clobbers elements.
  __ ldr(elements,
         FieldMemOperand(elements, PixelArray::kExternalPointerOffset));
  __ strb(r5, MemOperand(elements, r4));  // Elements is now external array.
  __ Ret();

  // Extra capacity case: Check if there is extra capacity to
  // perform the store and update the length. Used for adding one
  // element to the array by writing to array[array.length].
  __ bind(&extra);
  // Condition code from comparing key and array length is still available.
  __ b(ne, &slow);  // Only support writing to writing to array[array.length].
  // Check for room in the elements backing store.
  // Both the key and the length of FixedArray are smis.
  __ ldr(ip, FieldMemOperand(elements, FixedArray::kLengthOffset));
  __ cmp(key, Operand(ip));
  __ b(hs, &slow);
  // Calculate key + 1 as smi.
  ASSERT_EQ(0, kSmiTag);
  __ add(r4, key, Operand(Smi::FromInt(1)));
  __ str(r4, FieldMemOperand(receiver, JSArray::kLengthOffset));
  __ b(&fast);

  // Array case: Get the length and the elements array from the JS
  // array. Check that the array is in fast mode; if it is the
  // length is always a smi.
  __ bind(&array);
  __ ldr(elements, FieldMemOperand(receiver, JSObject::kElementsOffset));
  __ ldr(r4, FieldMemOperand(elements, HeapObject::kMapOffset));
  __ LoadRoot(ip, Heap::kFixedArrayMapRootIndex);
  __ cmp(r4, ip);
  __ b(ne, &slow);

  // Check the key against the length in the array.
  __ ldr(ip, FieldMemOperand(receiver, JSArray::kLengthOffset));
  __ cmp(key, Operand(ip));
  __ b(hs, &extra);
  // Fall through to fast case.

  __ bind(&fast);
  // Fast case, store the value to the elements backing store.
  __ add(r5, elements, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  __ add(r5, r5, Operand(key, LSL, kPointerSizeLog2 - kSmiTagSize));
  __ str(value, MemOperand(r5));
  // Skip write barrier if the written value is a smi.
  __ tst(value, Operand(kSmiTagMask));
  __ Ret(eq);
  // Update write barrier for the elements array address.
  __ sub(r4, r5, Operand(elements));
  __ RecordWrite(elements, r4, r5);

  __ Ret();
}


// Convert int passed in register ival to IEE 754 single precision
// floating point value and store it into register fval.
// If VFP3 is available use it for conversion.
static void ConvertIntToFloat(MacroAssembler* masm,
                              Register ival,
                              Register fval,
                              Register scratch1,
                              Register scratch2) {
  if (CpuFeatures::IsSupported(VFP3)) {
    CpuFeatures::Scope scope(VFP3);
    __ vmov(s0, ival);
    __ vcvt_f32_s32(s0, s0);
    __ vmov(fval, s0);
  } else {
    Label not_special, done;
    // Move sign bit from source to destination.  This works because the sign
    // bit in the exponent word of the double has the same position and polarity
    // as the 2's complement sign bit in a Smi.
    ASSERT(kBinary32SignMask == 0x80000000u);

    __ and_(fval, ival, Operand(kBinary32SignMask), SetCC);
    // Negate value if it is negative.
    __ rsb(ival, ival, Operand(0), LeaveCC, ne);

    // We have -1, 0 or 1, which we treat specially. Register ival contains
    // absolute value: it is either equal to 1 (special case of -1 and 1),
    // greater than 1 (not a special case) or less than 1 (special case of 0).
    __ cmp(ival, Operand(1));
    __ b(gt, &not_special);

    // For 1 or -1 we need to or in the 0 exponent (biased).
    static const uint32_t exponent_word_for_1 =
        kBinary32ExponentBias << kBinary32ExponentShift;

    __ orr(fval, fval, Operand(exponent_word_for_1), LeaveCC, eq);
    __ b(&done);

    __ bind(&not_special);
    // Count leading zeros.
    // Gets the wrong answer for 0, but we already checked for that case above.
    Register zeros = scratch2;
    __ CountLeadingZeros(ival, scratch1, zeros);

    // Compute exponent and or it into the exponent register.
    __ rsb(scratch1,
           zeros,
           Operand((kBitsPerInt - 1) + kBinary32ExponentBias));

    __ orr(fval,
           fval,
           Operand(scratch1, LSL, kBinary32ExponentShift));

    // Shift up the source chopping the top bit off.
    __ add(zeros, zeros, Operand(1));
    // This wouldn't work for 1 and -1 as the shift would be 32 which means 0.
    __ mov(ival, Operand(ival, LSL, zeros));
    // And the top (top 20 bits).
    __ orr(fval,
           fval,
           Operand(ival, LSR, kBitsPerInt - kBinary32MantissaBits));

    __ bind(&done);
  }
}


static bool IsElementTypeSigned(ExternalArrayType array_type) {
  switch (array_type) {
    case kExternalByteArray:
    case kExternalShortArray:
    case kExternalIntArray:
      return true;

    case kExternalUnsignedByteArray:
    case kExternalUnsignedShortArray:
    case kExternalUnsignedIntArray:
      return false;

    default:
      UNREACHABLE();
      return false;
  }
}


void KeyedStoreIC::GenerateExternalArray(MacroAssembler* masm,
                                         ExternalArrayType array_type) {
  // ---------- S t a t e --------------
  //  -- r0     : value
  //  -- r1     : key
  //  -- r2     : receiver
  //  -- lr     : return address
  // -----------------------------------
  Label slow, check_heap_number;

  // Register usage.
  Register value = r0;
  Register key = r1;
  Register receiver = r2;
  // r3 mostly holds the elements array or the destination external array.

  // Check that the object isn't a smi.
  __ BranchOnSmi(receiver, &slow);

  // Check that the object is a JS object. Load map into r3.
  __ CompareObjectType(receiver, r3, r4, FIRST_JS_OBJECT_TYPE);
  __ b(le, &slow);

  // Check that the receiver does not require access checks.  We need
  // to do this because this generic stub does not perform map checks.
  __ ldrb(ip, FieldMemOperand(r3, Map::kBitFieldOffset));
  __ tst(ip, Operand(1 << Map::kIsAccessCheckNeeded));
  __ b(ne, &slow);

  // Check that the key is a smi.
  __ BranchOnNotSmi(key, &slow);

  // Check that the elements array is the appropriate type of ExternalArray.
  __ ldr(r3, FieldMemOperand(receiver, JSObject::kElementsOffset));
  __ ldr(r4, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ LoadRoot(ip, Heap::RootIndexForExternalArrayType(array_type));
  __ cmp(r4, ip);
  __ b(ne, &slow);

  // Check that the index is in range.
  __ mov(r4, Operand(key, ASR, kSmiTagSize));  // Untag the index.
  __ ldr(ip, FieldMemOperand(r3, ExternalArray::kLengthOffset));
  __ cmp(r4, ip);
  // Unsigned comparison catches both negative and too-large values.
  __ b(hs, &slow);

  // Handle both smis and HeapNumbers in the fast path. Go to the
  // runtime for all other kinds of values.
  // r3: external array.
  // r4: key (integer).
  __ BranchOnNotSmi(value, &check_heap_number);
  __ mov(r5, Operand(value, ASR, kSmiTagSize));  // Untag the value.
  __ ldr(r3, FieldMemOperand(r3, ExternalArray::kExternalPointerOffset));

  // r3: base pointer of external storage.
  // r4: key (integer).
  // r5: value (integer).
  switch (array_type) {
    case kExternalByteArray:
    case kExternalUnsignedByteArray:
      __ strb(r5, MemOperand(r3, r4, LSL, 0));
      break;
    case kExternalShortArray:
    case kExternalUnsignedShortArray:
      __ strh(r5, MemOperand(r3, r4, LSL, 1));
      break;
    case kExternalIntArray:
    case kExternalUnsignedIntArray:
      __ str(r5, MemOperand(r3, r4, LSL, 2));
      break;
    case kExternalFloatArray:
      // Need to perform int-to-float conversion.
      ConvertIntToFloat(masm, r5, r6, r7, r9);
      __ str(r6, MemOperand(r3, r4, LSL, 2));
      break;
    default:
      UNREACHABLE();
      break;
  }

  // Entry registers are intact, r0 holds the value which is the return value.
  __ Ret();


  // r3: external array.
  // r4: index (integer).
  __ bind(&check_heap_number);
  __ CompareObjectType(value, r5, r6, HEAP_NUMBER_TYPE);
  __ b(ne, &slow);

  __ ldr(r3, FieldMemOperand(r3, ExternalArray::kExternalPointerOffset));

  // r3: base pointer of external storage.
  // r4: key (integer).

  // The WebGL specification leaves the behavior of storing NaN and
  // +/-Infinity into integer arrays basically undefined. For more
  // reproducible behavior, convert these to zero.
  if (CpuFeatures::IsSupported(VFP3)) {
    CpuFeatures::Scope scope(VFP3);

    // vldr requires offset to be a multiple of 4 so we can not
    // include -kHeapObjectTag into it.
    __ sub(r5, r0, Operand(kHeapObjectTag));
    __ vldr(d0, r5, HeapNumber::kValueOffset);

    if (array_type == kExternalFloatArray) {
      __ vcvt_f32_f64(s0, d0);
      __ vmov(r5, s0);
      __ str(r5, MemOperand(r3, r4, LSL, 2));
    } else {
      Label done;

      // Need to perform float-to-int conversion.
      // Test for NaN.
      __ vcmp(d0, d0);
      // Move vector status bits to normal status bits.
      __ vmrs(v8::internal::pc);
      __ mov(r5, Operand(0), LeaveCC, vs);  // NaN converts to 0.
      __ b(vs, &done);

      // Test whether exponent equal to 0x7FF (infinity or NaN).
      __ vmov(r6, r7, d0);
      __ mov(r5, Operand(0x7FF00000));
      __ and_(r6, r6, Operand(r5));
      __ teq(r6, Operand(r5));
      __ mov(r6, Operand(0), LeaveCC, eq);

      // Not infinity or NaN simply convert to int.
      if (IsElementTypeSigned(array_type)) {
        __ vcvt_s32_f64(s0, d0, ne);
      } else {
        __ vcvt_u32_f64(s0, d0, ne);
      }

      __ vmov(r5, s0, ne);

      __ bind(&done);
      switch (array_type) {
        case kExternalByteArray:
        case kExternalUnsignedByteArray:
          __ strb(r5, MemOperand(r3, r4, LSL, 0));
          break;
        case kExternalShortArray:
        case kExternalUnsignedShortArray:
          __ strh(r5, MemOperand(r3, r4, LSL, 1));
          break;
        case kExternalIntArray:
        case kExternalUnsignedIntArray:
          __ str(r5, MemOperand(r3, r4, LSL, 2));
          break;
        default:
          UNREACHABLE();
          break;
      }
    }

    // Entry registers are intact, r0 holds the value which is the return value.
    __ Ret();
  } else {
    // VFP3 is not available do manual conversions.
    __ ldr(r5, FieldMemOperand(value, HeapNumber::kExponentOffset));
    __ ldr(r6, FieldMemOperand(value, HeapNumber::kMantissaOffset));

    if (array_type == kExternalFloatArray) {
      Label done, nan_or_infinity_or_zero;
      static const int kMantissaInHiWordShift =
          kBinary32MantissaBits - HeapNumber::kMantissaBitsInTopWord;

      static const int kMantissaInLoWordShift =
          kBitsPerInt - kMantissaInHiWordShift;

      // Test for all special exponent values: zeros, subnormal numbers, NaNs
      // and infinities. All these should be converted to 0.
      __ mov(r7, Operand(HeapNumber::kExponentMask));
      __ and_(r9, r5, Operand(r7), SetCC);
      __ b(eq, &nan_or_infinity_or_zero);

      __ teq(r9, Operand(r7));
      __ mov(r9, Operand(kBinary32ExponentMask), LeaveCC, eq);
      __ b(eq, &nan_or_infinity_or_zero);

      // Rebias exponent.
      __ mov(r9, Operand(r9, LSR, HeapNumber::kExponentShift));
      __ add(r9,
             r9,
             Operand(kBinary32ExponentBias - HeapNumber::kExponentBias));

      __ cmp(r9, Operand(kBinary32MaxExponent));
      __ and_(r5, r5, Operand(HeapNumber::kSignMask), LeaveCC, gt);
      __ orr(r5, r5, Operand(kBinary32ExponentMask), LeaveCC, gt);
      __ b(gt, &done);

      __ cmp(r9, Operand(kBinary32MinExponent));
      __ and_(r5, r5, Operand(HeapNumber::kSignMask), LeaveCC, lt);
      __ b(lt, &done);

      __ and_(r7, r5, Operand(HeapNumber::kSignMask));
      __ and_(r5, r5, Operand(HeapNumber::kMantissaMask));
      __ orr(r7, r7, Operand(r5, LSL, kMantissaInHiWordShift));
      __ orr(r7, r7, Operand(r6, LSR, kMantissaInLoWordShift));
      __ orr(r5, r7, Operand(r9, LSL, kBinary32ExponentShift));

      __ bind(&done);
      __ str(r5, MemOperand(r3, r4, LSL, 2));
      // Entry registers are intact, r0 holds the value which is the return
      // value.
      __ Ret();

      __ bind(&nan_or_infinity_or_zero);
      __ and_(r7, r5, Operand(HeapNumber::kSignMask));
      __ and_(r5, r5, Operand(HeapNumber::kMantissaMask));
      __ orr(r9, r9, r7);
      __ orr(r9, r9, Operand(r5, LSL, kMantissaInHiWordShift));
      __ orr(r5, r9, Operand(r6, LSR, kMantissaInLoWordShift));
      __ b(&done);
    } else {
      bool is_signed_type = IsElementTypeSigned(array_type);
      int meaningfull_bits = is_signed_type ? (kBitsPerInt - 1) : kBitsPerInt;
      int32_t min_value = is_signed_type ? 0x80000000 : 0x00000000;

      Label done, sign;

      // Test for all special exponent values: zeros, subnormal numbers, NaNs
      // and infinities. All these should be converted to 0.
      __ mov(r7, Operand(HeapNumber::kExponentMask));
      __ and_(r9, r5, Operand(r7), SetCC);
      __ mov(r5, Operand(0), LeaveCC, eq);
      __ b(eq, &done);

      __ teq(r9, Operand(r7));
      __ mov(r5, Operand(0), LeaveCC, eq);
      __ b(eq, &done);

      // Unbias exponent.
      __ mov(r9, Operand(r9, LSR, HeapNumber::kExponentShift));
      __ sub(r9, r9, Operand(HeapNumber::kExponentBias), SetCC);
      // If exponent is negative than result is 0.
      __ mov(r5, Operand(0), LeaveCC, mi);
      __ b(mi, &done);

      // If exponent is too big than result is minimal value.
      __ cmp(r9, Operand(meaningfull_bits - 1));
      __ mov(r5, Operand(min_value), LeaveCC, ge);
      __ b(ge, &done);

      __ and_(r7, r5, Operand(HeapNumber::kSignMask), SetCC);
      __ and_(r5, r5, Operand(HeapNumber::kMantissaMask));
      __ orr(r5, r5, Operand(1u << HeapNumber::kMantissaBitsInTopWord));

      __ rsb(r9, r9, Operand(HeapNumber::kMantissaBitsInTopWord), SetCC);
      __ mov(r5, Operand(r5, LSR, r9), LeaveCC, pl);
      __ b(pl, &sign);

      __ rsb(r9, r9, Operand(0));
      __ mov(r5, Operand(r5, LSL, r9));
      __ rsb(r9, r9, Operand(meaningfull_bits));
      __ orr(r5, r5, Operand(r6, LSR, r9));

      __ bind(&sign);
      __ teq(r7, Operand(0));
      __ rsb(r5, r5, Operand(0), LeaveCC, ne);

      __ bind(&done);
      switch (array_type) {
        case kExternalByteArray:
        case kExternalUnsignedByteArray:
          __ strb(r5, MemOperand(r3, r4, LSL, 0));
          break;
        case kExternalShortArray:
        case kExternalUnsignedShortArray:
          __ strh(r5, MemOperand(r3, r4, LSL, 1));
          break;
        case kExternalIntArray:
        case kExternalUnsignedIntArray:
          __ str(r5, MemOperand(r3, r4, LSL, 2));
          break;
        default:
          UNREACHABLE();
          break;
      }
    }
  }

  // Slow case: call runtime.
  __ bind(&slow);

  // Entry registers are intact.
  // r0: value
  // r1: key
  // r2: receiver
  GenerateRuntimeSetProperty(masm);
}


void StoreIC::GenerateMegamorphic(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r0    : value
  //  -- r1    : receiver
  //  -- r2    : name
  //  -- lr    : return address
  // -----------------------------------

  // Get the receiver from the stack and probe the stub cache.
  Code::Flags flags = Code::ComputeFlags(Code::STORE_IC,
                                         NOT_IN_LOOP,
                                         MONOMORPHIC);
  StubCache::GenerateProbe(masm, flags, r1, r2, r3, no_reg);

  // Cache miss: Jump to runtime.
  GenerateMiss(masm);
}


void StoreIC::GenerateMiss(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r0    : value
  //  -- r1    : receiver
  //  -- r2    : name
  //  -- lr    : return address
  // -----------------------------------

  __ Push(r1, r2, r0);

  // Perform tail call to the entry.
  ExternalReference ref = ExternalReference(IC_Utility(kStoreIC_Miss));
  __ TailCallExternalReference(ref, 3, 1);
}


void StoreIC::GenerateArrayLength(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r0    : value
  //  -- r1    : receiver
  //  -- r2    : name
  //  -- lr    : return address
  // -----------------------------------
  //
  // This accepts as a receiver anything JSObject::SetElementsLength accepts
  // (currently anything except for external and pixel arrays which means
  // anything with elements of FixedArray type.), but currently is restricted
  // to JSArray.
  // Value must be a number, but only smis are accepted as the most common case.

  Label miss;

  Register receiver = r1;
  Register value = r0;
  Register scratch = r3;

  // Check that the receiver isn't a smi.
  __ BranchOnSmi(receiver, &miss);

  // Check that the object is a JS array.
  __ CompareObjectType(receiver, scratch, scratch, JS_ARRAY_TYPE);
  __ b(ne, &miss);

  // Check that elements are FixedArray.
  __ ldr(scratch, FieldMemOperand(receiver, JSArray::kElementsOffset));
  __ CompareObjectType(scratch, scratch, scratch, FIXED_ARRAY_TYPE);
  __ b(ne, &miss);

  // Check that value is a smi.
  __ BranchOnNotSmi(value, &miss);

  // Prepare tail call to StoreIC_ArrayLength.
  __ Push(receiver, value);

  ExternalReference ref = ExternalReference(IC_Utility(kStoreIC_ArrayLength));
  __ TailCallExternalReference(ref, 2, 1);

  __ bind(&miss);

  GenerateMiss(masm);
}


#undef __


} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_ARM
