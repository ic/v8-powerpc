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

#include "v8.h"

#if defined(V8_TARGET_ARCH_IA32)

#include "codegen.h"
#include "ic-inl.h"
#include "runtime.h"
#include "stub-cache.h"

namespace v8 {
namespace internal {

// ----------------------------------------------------------------------------
// Static IC stub generators.
//

#define __ ACCESS_MASM(masm)


static void GenerateGlobalInstanceTypeCheck(MacroAssembler* masm,
                                            Register type,
                                            Label* global_object) {
  // Register usage:
  //   type: holds the receiver instance type on entry.
  __ cmp(type, JS_GLOBAL_OBJECT_TYPE);
  __ j(equal, global_object, not_taken);
  __ cmp(type, JS_BUILTINS_OBJECT_TYPE);
  __ j(equal, global_object, not_taken);
  __ cmp(type, JS_GLOBAL_PROXY_TYPE);
  __ j(equal, global_object, not_taken);
}


// Generated code falls through if the receiver is a regular non-global
// JS object with slow properties and no interceptors.
static void GenerateStringDictionaryReceiverCheck(MacroAssembler* masm,
                                                  Register receiver,
                                                  Register r0,
                                                  Register r1,
                                                  Label* miss) {
  // Register usage:
  //   receiver: holds the receiver on entry and is unchanged.
  //   r0: used to hold receiver instance type.
  //       Holds the property dictionary on fall through.
  //   r1: used to hold receivers map.

  // Check that the receiver isn't a smi.
  __ test(receiver, Immediate(kSmiTagMask));
  __ j(zero, miss, not_taken);

  // Check that the receiver is a valid JS object.
  __ mov(r1, FieldOperand(receiver, HeapObject::kMapOffset));
  __ movzx_b(r0, FieldOperand(r1, Map::kInstanceTypeOffset));
  __ cmp(r0, FIRST_JS_OBJECT_TYPE);
  __ j(below, miss, not_taken);

  // If this assert fails, we have to check upper bound too.
  ASSERT(LAST_TYPE == JS_FUNCTION_TYPE);

  GenerateGlobalInstanceTypeCheck(masm, r0, miss);

  // Check for non-global object that requires access check.
  __ test_b(FieldOperand(r1, Map::kBitFieldOffset),
            (1 << Map::kIsAccessCheckNeeded) |
            (1 << Map::kHasNamedInterceptor));
  __ j(not_zero, miss, not_taken);

  __ mov(r0, FieldOperand(receiver, JSObject::kPropertiesOffset));
  __ CheckMap(r0, FACTORY->hash_table_map(), miss, true);
}


// Probe the string dictionary in the |elements| register. Jump to the
// |done| label if a property with the given name is found leaving the
// index into the dictionary in |r0|. Jump to the |miss| label
// otherwise.
static void GenerateStringDictionaryProbes(MacroAssembler* masm,
                                           Label* miss,
                                           Label* done,
                                           Register elements,
                                           Register name,
                                           Register r0,
                                           Register r1) {
  // Assert that name contains a string.
  if (FLAG_debug_code) __ AbortIfNotString(name);

  // Compute the capacity mask.
  const int kCapacityOffset =
      StringDictionary::kHeaderSize +
      StringDictionary::kCapacityIndex * kPointerSize;
  __ mov(r1, FieldOperand(elements, kCapacityOffset));
  __ shr(r1, kSmiTagSize);  // convert smi to int
  __ dec(r1);

  // Generate an unrolled loop that performs a few probes before
  // giving up. Measurements done on Gmail indicate that 2 probes
  // cover ~93% of loads from dictionaries.
  static const int kProbes = 4;
  const int kElementsStartOffset =
      StringDictionary::kHeaderSize +
      StringDictionary::kElementsStartIndex * kPointerSize;
  for (int i = 0; i < kProbes; i++) {
    // Compute the masked index: (hash + i + i * i) & mask.
    __ mov(r0, FieldOperand(name, String::kHashFieldOffset));
    __ shr(r0, String::kHashShift);
    if (i > 0) {
      __ add(Operand(r0), Immediate(StringDictionary::GetProbeOffset(i)));
    }
    __ and_(r0, Operand(r1));

    // Scale the index by multiplying by the entry size.
    ASSERT(StringDictionary::kEntrySize == 3);
    __ lea(r0, Operand(r0, r0, times_2, 0));  // r0 = r0 * 3

    // Check if the key is identical to the name.
    __ cmp(name, Operand(elements, r0, times_4,
                         kElementsStartOffset - kHeapObjectTag));
    if (i != kProbes - 1) {
      __ j(equal, done, taken);
    } else {
      __ j(not_equal, miss, not_taken);
    }
  }
}



// Helper function used to load a property from a dictionary backing
// storage. This function may fail to load a property even though it is
// in the dictionary, so code at miss_label must always call a backup
// property load that is complete. This function is safe to call if
// name is not a symbol, and will jump to the miss_label in that
// case. The generated code assumes that the receiver has slow
// properties, is not a global object and does not have interceptors.
static void GenerateDictionaryLoad(MacroAssembler* masm,
                                   Label* miss_label,
                                   Register elements,
                                   Register name,
                                   Register r0,
                                   Register r1,
                                   Register result) {
  // Register use:
  //
  // elements - holds the property dictionary on entry and is unchanged.
  //
  // name - holds the name of the property on entry and is unchanged.
  //
  // Scratch registers:
  //
  // r0   - used for the index into the property dictionary
  //
  // r1   - used to hold the capacity of the property dictionary.
  //
  // result - holds the result on exit.

  Label done;

  // Probe the dictionary.
  GenerateStringDictionaryProbes(masm,
                                 miss_label,
                                 &done,
                                 elements,
                                 name,
                                 r0,
                                 r1);

  // If probing finds an entry in the dictionary, r0 contains the
  // index into the dictionary. Check that the value is a normal
  // property.
  __ bind(&done);
  const int kElementsStartOffset =
      StringDictionary::kHeaderSize +
      StringDictionary::kElementsStartIndex * kPointerSize;
  const int kDetailsOffset = kElementsStartOffset + 2 * kPointerSize;
  __ test(Operand(elements, r0, times_4, kDetailsOffset - kHeapObjectTag),
          Immediate(PropertyDetails::TypeField::mask() << kSmiTagSize));
  __ j(not_zero, miss_label, not_taken);

  // Get the value at the masked, scaled index.
  const int kValueOffset = kElementsStartOffset + kPointerSize;
  __ mov(result, Operand(elements, r0, times_4, kValueOffset - kHeapObjectTag));
}


// Helper function used to store a property to a dictionary backing
// storage. This function may fail to store a property eventhough it
// is in the dictionary, so code at miss_label must always call a
// backup property store that is complete. This function is safe to
// call if name is not a symbol, and will jump to the miss_label in
// that case. The generated code assumes that the receiver has slow
// properties, is not a global object and does not have interceptors.
static void GenerateDictionaryStore(MacroAssembler* masm,
                                    Label* miss_label,
                                    Register elements,
                                    Register name,
                                    Register value,
                                    Register r0,
                                    Register r1) {
  // Register use:
  //
  // elements - holds the property dictionary on entry and is clobbered.
  //
  // name - holds the name of the property on entry and is unchanged.
  //
  // value - holds the value to store and is unchanged.
  //
  // r0 - used for index into the property dictionary and is clobbered.
  //
  // r1 - used to hold the capacity of the property dictionary and is clobbered.
  Label done;


  // Probe the dictionary.
  GenerateStringDictionaryProbes(masm,
                                 miss_label,
                                 &done,
                                 elements,
                                 name,
                                 r0,
                                 r1);

  // If probing finds an entry in the dictionary, r0 contains the
  // index into the dictionary. Check that the value is a normal
  // property that is not read only.
  __ bind(&done);
  const int kElementsStartOffset =
      StringDictionary::kHeaderSize +
      StringDictionary::kElementsStartIndex * kPointerSize;
  const int kDetailsOffset = kElementsStartOffset + 2 * kPointerSize;
  const int kTypeAndReadOnlyMask
      = (PropertyDetails::TypeField::mask() |
         PropertyDetails::AttributesField::encode(READ_ONLY)) << kSmiTagSize;
  __ test(Operand(elements, r0, times_4, kDetailsOffset - kHeapObjectTag),
          Immediate(kTypeAndReadOnlyMask));
  __ j(not_zero, miss_label, not_taken);

  // Store the value at the masked, scaled index.
  const int kValueOffset = kElementsStartOffset + kPointerSize;
  __ lea(r0, Operand(elements, r0, times_4, kValueOffset - kHeapObjectTag));
  __ mov(Operand(r0, 0), value);

  // Update write barrier. Make sure not to clobber the value.
  __ mov(r1, value);
  __ RecordWrite(elements, r0, r1);
}


static void GenerateNumberDictionaryLoad(MacroAssembler* masm,
                                         Label* miss,
                                         Register elements,
                                         Register key,
                                         Register r0,
                                         Register r1,
                                         Register r2,
                                         Register result) {
  // Register use:
  //
  // elements - holds the slow-case elements of the receiver and is unchanged.
  //
  // key      - holds the smi key on entry and is unchanged.
  //
  // Scratch registers:
  //
  // r0 - holds the untagged key on entry and holds the hash once computed.
  //
  // r1 - used to hold the capacity mask of the dictionary
  //
  // r2 - used for the index into the dictionary.
  //
  // result - holds the result on exit if the load succeeds and we fall through.

  Label done;

  // Compute the hash code from the untagged key.  This must be kept in sync
  // with ComputeIntegerHash in utils.h.
  //
  // hash = ~hash + (hash << 15);
  __ mov(r1, r0);
  __ not_(r0);
  __ shl(r1, 15);
  __ add(r0, Operand(r1));
  // hash = hash ^ (hash >> 12);
  __ mov(r1, r0);
  __ shr(r1, 12);
  __ xor_(r0, Operand(r1));
  // hash = hash + (hash << 2);
  __ lea(r0, Operand(r0, r0, times_4, 0));
  // hash = hash ^ (hash >> 4);
  __ mov(r1, r0);
  __ shr(r1, 4);
  __ xor_(r0, Operand(r1));
  // hash = hash * 2057;
  __ imul(r0, r0, 2057);
  // hash = hash ^ (hash >> 16);
  __ mov(r1, r0);
  __ shr(r1, 16);
  __ xor_(r0, Operand(r1));

  // Compute capacity mask.
  __ mov(r1, FieldOperand(elements, NumberDictionary::kCapacityOffset));
  __ shr(r1, kSmiTagSize);  // convert smi to int
  __ dec(r1);

  // Generate an unrolled loop that performs a few probes before giving up.
  const int kProbes = 4;
  for (int i = 0; i < kProbes; i++) {
    // Use r2 for index calculations and keep the hash intact in r0.
    __ mov(r2, r0);
    // Compute the masked index: (hash + i + i * i) & mask.
    if (i > 0) {
      __ add(Operand(r2), Immediate(NumberDictionary::GetProbeOffset(i)));
    }
    __ and_(r2, Operand(r1));

    // Scale the index by multiplying by the entry size.
    ASSERT(NumberDictionary::kEntrySize == 3);
    __ lea(r2, Operand(r2, r2, times_2, 0));  // r2 = r2 * 3

    // Check if the key matches.
    __ cmp(key, FieldOperand(elements,
                             r2,
                             times_pointer_size,
                             NumberDictionary::kElementsStartOffset));
    if (i != (kProbes - 1)) {
      __ j(equal, &done, taken);
    } else {
      __ j(not_equal, miss, not_taken);
    }
  }

  __ bind(&done);
  // Check that the value is a normal propety.
  const int kDetailsOffset =
      NumberDictionary::kElementsStartOffset + 2 * kPointerSize;
  ASSERT_EQ(NORMAL, 0);
  __ test(FieldOperand(elements, r2, times_pointer_size, kDetailsOffset),
          Immediate(PropertyDetails::TypeField::mask() << kSmiTagSize));
  __ j(not_zero, miss);

  // Get the value at the masked, scaled index.
  const int kValueOffset =
      NumberDictionary::kElementsStartOffset + kPointerSize;
  __ mov(result, FieldOperand(elements, r2, times_pointer_size, kValueOffset));
}


// The offset from the inlined patch site to the start of the
// inlined load instruction.  It is 7 bytes (test eax, imm) plus
// 6 bytes (jne slow_label).
const int LoadIC::kOffsetToLoadInstruction = 13;


void LoadIC::GenerateArrayLength(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : receiver
  //  -- ecx    : name
  //  -- esp[0] : return address
  // -----------------------------------
  Label miss;

  StubCompiler::GenerateLoadArrayLength(masm, eax, edx, &miss);
  __ bind(&miss);
  StubCompiler::GenerateLoadMiss(masm, Code::LOAD_IC);
}


void LoadIC::GenerateStringLength(MacroAssembler* masm,
                                  bool support_wrappers) {
  // ----------- S t a t e -------------
  //  -- eax    : receiver
  //  -- ecx    : name
  //  -- esp[0] : return address
  // -----------------------------------
  Label miss;

  StubCompiler::GenerateLoadStringLength(masm, eax, edx, ebx, &miss,
                                         support_wrappers);
  __ bind(&miss);
  StubCompiler::GenerateLoadMiss(masm, Code::LOAD_IC);
}


void LoadIC::GenerateFunctionPrototype(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : receiver
  //  -- ecx    : name
  //  -- esp[0] : return address
  // -----------------------------------
  Label miss;

  StubCompiler::GenerateLoadFunctionPrototype(masm, eax, edx, ebx, &miss);
  __ bind(&miss);
  StubCompiler::GenerateLoadMiss(masm, Code::LOAD_IC);
}


// Checks the receiver for special cases (value type, slow case bits).
// Falls through for regular JS object.
static void GenerateKeyedLoadReceiverCheck(MacroAssembler* masm,
                                           Register receiver,
                                           Register map,
                                           int interceptor_bit,
                                           Label* slow) {
  // Register use:
  //   receiver - holds the receiver and is unchanged.
  // Scratch registers:
  //   map - used to hold the map of the receiver.

  // Check that the object isn't a smi.
  __ test(receiver, Immediate(kSmiTagMask));
  __ j(zero, slow, not_taken);

  // Get the map of the receiver.
  __ mov(map, FieldOperand(receiver, HeapObject::kMapOffset));

  // Check bit field.
  __ test_b(FieldOperand(map, Map::kBitFieldOffset),
            (1 << Map::kIsAccessCheckNeeded) | (1 << interceptor_bit));
  __ j(not_zero, slow, not_taken);
  // Check that the object is some kind of JS object EXCEPT JS Value type.
  // In the case that the object is a value-wrapper object,
  // we enter the runtime system to make sure that indexing
  // into string objects works as intended.
  ASSERT(JS_OBJECT_TYPE > JS_VALUE_TYPE);

  __ CmpInstanceType(map, JS_OBJECT_TYPE);
  __ j(below, slow, not_taken);
}


// Loads an indexed element from a fast case array.
// If not_fast_array is NULL, doesn't perform the elements map check.
static void GenerateFastArrayLoad(MacroAssembler* masm,
                                  Register receiver,
                                  Register key,
                                  Register scratch,
                                  Register result,
                                  Label* not_fast_array,
                                  Label* out_of_range) {
  // Register use:
  //   receiver - holds the receiver and is unchanged.
  //   key - holds the key and is unchanged (must be a smi).
  // Scratch registers:
  //   scratch - used to hold elements of the receiver and the loaded value.
  //   result - holds the result on exit if the load succeeds and
  //            we fall through.

  __ mov(scratch, FieldOperand(receiver, JSObject::kElementsOffset));
  if (not_fast_array != NULL) {
    // Check that the object is in fast mode and writable.
    __ CheckMap(scratch, FACTORY->fixed_array_map(), not_fast_array, true);
  } else {
    __ AssertFastElements(scratch);
  }
  // Check that the key (index) is within bounds.
  __ cmp(key, FieldOperand(scratch, FixedArray::kLengthOffset));
  __ j(above_equal, out_of_range);
  // Fast case: Do the load.
  ASSERT((kPointerSize == 4) && (kSmiTagSize == 1) && (kSmiTag == 0));
  __ mov(scratch, FieldOperand(scratch, key, times_2, FixedArray::kHeaderSize));
  __ cmp(Operand(scratch), Immediate(FACTORY->the_hole_value()));
  // In case the loaded value is the_hole we have to consult GetProperty
  // to ensure the prototype chain is searched.
  __ j(equal, out_of_range);
  if (!result.is(scratch)) {
    __ mov(result, scratch);
  }
}


// Checks whether a key is an array index string or a symbol string.
// Falls through if the key is a symbol.
static void GenerateKeyStringCheck(MacroAssembler* masm,
                                   Register key,
                                   Register map,
                                   Register hash,
                                   Label* index_string,
                                   Label* not_symbol) {
  // Register use:
  //   key - holds the key and is unchanged. Assumed to be non-smi.
  // Scratch registers:
  //   map - used to hold the map of the key.
  //   hash - used to hold the hash of the key.
  __ CmpObjectType(key, FIRST_NONSTRING_TYPE, map);
  __ j(above_equal, not_symbol);

  // Is the string an array index, with cached numeric value?
  __ mov(hash, FieldOperand(key, String::kHashFieldOffset));
  __ test(hash, Immediate(String::kContainsCachedArrayIndexMask));
  __ j(zero, index_string, not_taken);

  // Is the string a symbol?
  ASSERT(kSymbolTag != 0);
  __ test_b(FieldOperand(map, Map::kInstanceTypeOffset), kIsSymbolMask);
  __ j(zero, not_symbol, not_taken);
}


void KeyedLoadIC::GenerateGeneric(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : key
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------
  Label slow, check_string, index_smi, index_string, property_array_property;
  Label probe_dictionary, check_number_dictionary;

  // Check that the key is a smi.
  __ test(eax, Immediate(kSmiTagMask));
  __ j(not_zero, &check_string, not_taken);
  __ bind(&index_smi);
  // Now the key is known to be a smi. This place is also jumped to from
  // where a numeric string is converted to a smi.

  GenerateKeyedLoadReceiverCheck(
      masm, edx, ecx, Map::kHasIndexedInterceptor, &slow);

  // Check the "has fast elements" bit in the receiver's map which is
  // now in ecx.
  __ test_b(FieldOperand(ecx, Map::kBitField2Offset),
            1 << Map::kHasFastElements);
  __ j(zero, &check_number_dictionary, not_taken);

  GenerateFastArrayLoad(masm,
                        edx,
                        eax,
                        ecx,
                        eax,
                        NULL,
                        &slow);
  Isolate* isolate = masm->isolate();
  Counters* counters = isolate->counters();
  __ IncrementCounter(counters->keyed_load_generic_smi(), 1);
  __ ret(0);

  __ bind(&check_number_dictionary);
  __ mov(ebx, eax);
  __ SmiUntag(ebx);
  __ mov(ecx, FieldOperand(edx, JSObject::kElementsOffset));

  // Check whether the elements is a number dictionary.
  // edx: receiver
  // ebx: untagged index
  // eax: key
  // ecx: elements
  __ CheckMap(ecx, isolate->factory()->hash_table_map(), &slow, true);
  Label slow_pop_receiver;
  // Push receiver on the stack to free up a register for the dictionary
  // probing.
  __ push(edx);
  GenerateNumberDictionaryLoad(masm,
                               &slow_pop_receiver,
                               ecx,
                               eax,
                               ebx,
                               edx,
                               edi,
                               eax);
  // Pop receiver before returning.
  __ pop(edx);
  __ ret(0);

  __ bind(&slow_pop_receiver);
  // Pop the receiver from the stack and jump to runtime.
  __ pop(edx);

  __ bind(&slow);
  // Slow case: jump to runtime.
  // edx: receiver
  // eax: key
  __ IncrementCounter(counters->keyed_load_generic_slow(), 1);
  GenerateRuntimeGetProperty(masm);

  __ bind(&check_string);
  GenerateKeyStringCheck(masm, eax, ecx, ebx, &index_string, &slow);

  GenerateKeyedLoadReceiverCheck(
      masm, edx, ecx, Map::kHasNamedInterceptor, &slow);

  // If the receiver is a fast-case object, check the keyed lookup
  // cache. Otherwise probe the dictionary.
  __ mov(ebx, FieldOperand(edx, JSObject::kPropertiesOffset));
  __ cmp(FieldOperand(ebx, HeapObject::kMapOffset),
         Immediate(isolate->factory()->hash_table_map()));
  __ j(equal, &probe_dictionary);

  // Load the map of the receiver, compute the keyed lookup cache hash
  // based on 32 bits of the map pointer and the string hash.
  __ mov(ebx, FieldOperand(edx, HeapObject::kMapOffset));
  __ mov(ecx, ebx);
  __ shr(ecx, KeyedLookupCache::kMapHashShift);
  __ mov(edi, FieldOperand(eax, String::kHashFieldOffset));
  __ shr(edi, String::kHashShift);
  __ xor_(ecx, Operand(edi));
  __ and_(ecx, KeyedLookupCache::kCapacityMask);

  // Load the key (consisting of map and symbol) from the cache and
  // check for match.
  ExternalReference cache_keys =
      ExternalReference::keyed_lookup_cache_keys(masm->isolate());
  __ mov(edi, ecx);
  __ shl(edi, kPointerSizeLog2 + 1);
  __ cmp(ebx, Operand::StaticArray(edi, times_1, cache_keys));
  __ j(not_equal, &slow);
  __ add(Operand(edi), Immediate(kPointerSize));
  __ cmp(eax, Operand::StaticArray(edi, times_1, cache_keys));
  __ j(not_equal, &slow);

  // Get field offset.
  // edx     : receiver
  // ebx     : receiver's map
  // eax     : key
  // ecx     : lookup cache index
  ExternalReference cache_field_offsets =
      ExternalReference::keyed_lookup_cache_field_offsets(masm->isolate());
  __ mov(edi,
         Operand::StaticArray(ecx, times_pointer_size, cache_field_offsets));
  __ movzx_b(ecx, FieldOperand(ebx, Map::kInObjectPropertiesOffset));
  __ sub(edi, Operand(ecx));
  __ j(above_equal, &property_array_property);

  // Load in-object property.
  __ movzx_b(ecx, FieldOperand(ebx, Map::kInstanceSizeOffset));
  __ add(ecx, Operand(edi));
  __ mov(eax, FieldOperand(edx, ecx, times_pointer_size, 0));
  __ IncrementCounter(counters->keyed_load_generic_lookup_cache(), 1);
  __ ret(0);

  // Load property array property.
  __ bind(&property_array_property);
  __ mov(eax, FieldOperand(edx, JSObject::kPropertiesOffset));
  __ mov(eax, FieldOperand(eax, edi, times_pointer_size,
                           FixedArray::kHeaderSize));
  __ IncrementCounter(counters->keyed_load_generic_lookup_cache(), 1);
  __ ret(0);

  // Do a quick inline probe of the receiver's dictionary, if it
  // exists.
  __ bind(&probe_dictionary);

  __ mov(ecx, FieldOperand(edx, JSObject::kMapOffset));
  __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
  GenerateGlobalInstanceTypeCheck(masm, ecx, &slow);

  GenerateDictionaryLoad(masm, &slow, ebx, eax, ecx, edi, eax);
  __ IncrementCounter(counters->keyed_load_generic_symbol(), 1);
  __ ret(0);

  __ bind(&index_string);
  __ IndexFromHash(ebx, eax);
  // Now jump to the place where smi keys are handled.
  __ jmp(&index_smi);
}


void KeyedLoadIC::GenerateString(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : key (index)
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------
  Label miss;

  Register receiver = edx;
  Register index = eax;
  Register scratch1 = ebx;
  Register scratch2 = ecx;
  Register result = eax;

  StringCharAtGenerator char_at_generator(receiver,
                                          index,
                                          scratch1,
                                          scratch2,
                                          result,
                                          &miss,  // When not a string.
                                          &miss,  // When not a number.
                                          &miss,  // When index out of range.
                                          STRING_INDEX_IS_ARRAY_INDEX);
  char_at_generator.GenerateFast(masm);
  __ ret(0);

  StubRuntimeCallHelper call_helper;
  char_at_generator.GenerateSlow(masm, call_helper);

  __ bind(&miss);
  GenerateMiss(masm);
}


void KeyedLoadIC::GenerateIndexedInterceptor(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : key
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------
  Label slow;

  // Check that the receiver isn't a smi.
  __ test(edx, Immediate(kSmiTagMask));
  __ j(zero, &slow, not_taken);

  // Check that the key is an array index, that is Uint32.
  __ test(eax, Immediate(kSmiTagMask | kSmiSignMask));
  __ j(not_zero, &slow, not_taken);

  // Get the map of the receiver.
  __ mov(ecx, FieldOperand(edx, HeapObject::kMapOffset));

  // Check that it has indexed interceptor and access checks
  // are not enabled for this object.
  __ movzx_b(ecx, FieldOperand(ecx, Map::kBitFieldOffset));
  __ and_(Operand(ecx), Immediate(kSlowCaseBitFieldMask));
  __ cmp(Operand(ecx), Immediate(1 << Map::kHasIndexedInterceptor));
  __ j(not_zero, &slow, not_taken);

  // Everything is fine, call runtime.
  __ pop(ecx);
  __ push(edx);  // receiver
  __ push(eax);  // key
  __ push(ecx);  // return address

  // Perform tail call to the entry.
  ExternalReference ref =
      ExternalReference(IC_Utility(kKeyedLoadPropertyWithInterceptor),
                        masm->isolate());
  __ TailCallExternalReference(ref, 2, 1);

  __ bind(&slow);
  GenerateMiss(masm);
}


void KeyedStoreIC::GenerateGeneric(MacroAssembler* masm,
                                   StrictModeFlag strict_mode) {
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : key
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------
  Label slow, fast, array, extra;

  // Check that the object isn't a smi.
  __ test(edx, Immediate(kSmiTagMask));
  __ j(zero, &slow, not_taken);
  // Get the map from the receiver.
  __ mov(edi, FieldOperand(edx, HeapObject::kMapOffset));
  // Check that the receiver does not require access checks.  We need
  // to do this because this generic stub does not perform map checks.
  __ test_b(FieldOperand(edi, Map::kBitFieldOffset),
            1 << Map::kIsAccessCheckNeeded);
  __ j(not_zero, &slow, not_taken);
  // Check that the key is a smi.
  __ test(ecx, Immediate(kSmiTagMask));
  __ j(not_zero, &slow, not_taken);
  __ CmpInstanceType(edi, JS_ARRAY_TYPE);
  __ j(equal, &array);
  // Check that the object is some kind of JS object.
  __ CmpInstanceType(edi, FIRST_JS_OBJECT_TYPE);
  __ j(below, &slow, not_taken);

  // Object case: Check key against length in the elements array.
  // eax: value
  // edx: JSObject
  // ecx: key (a smi)
  __ mov(edi, FieldOperand(edx, JSObject::kElementsOffset));
  // Check that the object is in fast mode and writable.
  __ CheckMap(edi, FACTORY->fixed_array_map(), &slow, true);
  __ cmp(ecx, FieldOperand(edi, FixedArray::kLengthOffset));
  __ j(below, &fast, taken);

  // Slow case: call runtime.
  __ bind(&slow);
  GenerateRuntimeSetProperty(masm, strict_mode);

  // Extra capacity case: Check if there is extra capacity to
  // perform the store and update the length. Used for adding one
  // element to the array by writing to array[array.length].
  __ bind(&extra);
  // eax: value
  // edx: receiver, a JSArray
  // ecx: key, a smi.
  // edi: receiver->elements, a FixedArray
  // flags: compare (ecx, edx.length())
  __ j(not_equal, &slow, not_taken);  // do not leave holes in the array
  __ cmp(ecx, FieldOperand(edi, FixedArray::kLengthOffset));
  __ j(above_equal, &slow, not_taken);
  // Add 1 to receiver->length, and go to fast array write.
  __ add(FieldOperand(edx, JSArray::kLengthOffset),
         Immediate(Smi::FromInt(1)));
  __ jmp(&fast);

  // Array case: Get the length and the elements array from the JS
  // array. Check that the array is in fast mode (and writable); if it
  // is the length is always a smi.
  __ bind(&array);
  // eax: value
  // edx: receiver, a JSArray
  // ecx: key, a smi.
  __ mov(edi, FieldOperand(edx, JSObject::kElementsOffset));
  __ CheckMap(edi, FACTORY->fixed_array_map(), &slow, true);

  // Check the key against the length in the array, compute the
  // address to store into and fall through to fast case.
  __ cmp(ecx, FieldOperand(edx, JSArray::kLengthOffset));  // Compare smis.
  __ j(above_equal, &extra, not_taken);

  // Fast case: Do the store.
  __ bind(&fast);
  // eax: value
  // ecx: key (a smi)
  // edx: receiver
  // edi: FixedArray receiver->elements
  __ mov(CodeGenerator::FixedArrayElementOperand(edi, ecx), eax);
  // Update write barrier for the elements array address.
  __ mov(edx, Operand(eax));
  __ RecordWrite(edi, 0, edx, ecx);
  __ ret(0);
}


// The generated code does not accept smi keys.
// The generated code falls through if both probes miss.
static void GenerateMonomorphicCacheProbe(MacroAssembler* masm,
                                          int argc,
                                          Code::Kind kind) {
  // ----------- S t a t e -------------
  //  -- ecx                 : name
  //  -- edx                 : receiver
  // -----------------------------------
  Label number, non_number, non_string, boolean, probe, miss;

  // Probe the stub cache.
  Code::Flags flags = Code::ComputeFlags(kind,
                                         NOT_IN_LOOP,
                                         MONOMORPHIC,
                                         Code::kNoExtraICState,
                                         NORMAL,
                                         argc);
  Isolate::Current()->stub_cache()->GenerateProbe(masm, flags, edx, ecx, ebx,
                                                  eax);

  // If the stub cache probing failed, the receiver might be a value.
  // For value objects, we use the map of the prototype objects for
  // the corresponding JSValue for the cache and that is what we need
  // to probe.
  //
  // Check for number.
  __ test(edx, Immediate(kSmiTagMask));
  __ j(zero, &number, not_taken);
  __ CmpObjectType(edx, HEAP_NUMBER_TYPE, ebx);
  __ j(not_equal, &non_number, taken);
  __ bind(&number);
  StubCompiler::GenerateLoadGlobalFunctionPrototype(
      masm, Context::NUMBER_FUNCTION_INDEX, edx);
  __ jmp(&probe);

  // Check for string.
  __ bind(&non_number);
  __ CmpInstanceType(ebx, FIRST_NONSTRING_TYPE);
  __ j(above_equal, &non_string, taken);
  StubCompiler::GenerateLoadGlobalFunctionPrototype(
      masm, Context::STRING_FUNCTION_INDEX, edx);
  __ jmp(&probe);

  // Check for boolean.
  __ bind(&non_string);
  __ cmp(edx, FACTORY->true_value());
  __ j(equal, &boolean, not_taken);
  __ cmp(edx, FACTORY->false_value());
  __ j(not_equal, &miss, taken);
  __ bind(&boolean);
  StubCompiler::GenerateLoadGlobalFunctionPrototype(
      masm, Context::BOOLEAN_FUNCTION_INDEX, edx);

  // Probe the stub cache for the value object.
  __ bind(&probe);
  Isolate::Current()->stub_cache()->GenerateProbe(masm, flags, edx, ecx, ebx,
                                                  no_reg);
  __ bind(&miss);
}


static void GenerateFunctionTailCall(MacroAssembler* masm,
                                     int argc,
                                     Label* miss) {
  // ----------- S t a t e -------------
  //  -- ecx                 : name
  //  -- edi                 : function
  //  -- esp[0]              : return address
  //  -- esp[(argc - n) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- esp[(argc + 1) * 4] : receiver
  // -----------------------------------

  // Check that the result is not a smi.
  __ test(edi, Immediate(kSmiTagMask));
  __ j(zero, miss, not_taken);

  // Check that the value is a JavaScript function, fetching its map into eax.
  __ CmpObjectType(edi, JS_FUNCTION_TYPE, eax);
  __ j(not_equal, miss, not_taken);

  // Invoke the function.
  ParameterCount actual(argc);
  __ InvokeFunction(edi, actual, JUMP_FUNCTION);
}

// The generated code falls through if the call should be handled by runtime.
static void GenerateCallNormal(MacroAssembler* masm, int argc) {
  // ----------- S t a t e -------------
  //  -- ecx                 : name
  //  -- esp[0]              : return address
  //  -- esp[(argc - n) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- esp[(argc + 1) * 4] : receiver
  // -----------------------------------
  Label miss;

  // Get the receiver of the function from the stack; 1 ~ return address.
  __ mov(edx, Operand(esp, (argc + 1) * kPointerSize));

  GenerateStringDictionaryReceiverCheck(masm, edx, eax, ebx, &miss);

  // eax: elements
  // Search the dictionary placing the result in edi.
  GenerateDictionaryLoad(masm, &miss, eax, ecx, edi, ebx, edi);
  GenerateFunctionTailCall(masm, argc, &miss);

  __ bind(&miss);
}


static void GenerateCallMiss(MacroAssembler* masm,
                             int argc,
                             IC::UtilityId id) {
  // ----------- S t a t e -------------
  //  -- ecx                 : name
  //  -- esp[0]              : return address
  //  -- esp[(argc - n) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- esp[(argc + 1) * 4] : receiver
  // -----------------------------------

  Counters* counters = masm->isolate()->counters();
  if (id == IC::kCallIC_Miss) {
    __ IncrementCounter(counters->call_miss(), 1);
  } else {
    __ IncrementCounter(counters->keyed_call_miss(), 1);
  }

  // Get the receiver of the function from the stack; 1 ~ return address.
  __ mov(edx, Operand(esp, (argc + 1) * kPointerSize));

  // Enter an internal frame.
  __ EnterInternalFrame();

  // Push the receiver and the name of the function.
  __ push(edx);
  __ push(ecx);

  // Call the entry.
  CEntryStub stub(1);
  __ mov(eax, Immediate(2));
  __ mov(ebx, Immediate(ExternalReference(IC_Utility(id), masm->isolate())));
  __ CallStub(&stub);

  // Move result to edi and exit the internal frame.
  __ mov(edi, eax);
  __ LeaveInternalFrame();

  // Check if the receiver is a global object of some sort.
  // This can happen only for regular CallIC but not KeyedCallIC.
  if (id == IC::kCallIC_Miss) {
    Label invoke, global;
    __ mov(edx, Operand(esp, (argc + 1) * kPointerSize));  // receiver
    __ test(edx, Immediate(kSmiTagMask));
    __ j(zero, &invoke, not_taken);
    __ mov(ebx, FieldOperand(edx, HeapObject::kMapOffset));
    __ movzx_b(ebx, FieldOperand(ebx, Map::kInstanceTypeOffset));
    __ cmp(ebx, JS_GLOBAL_OBJECT_TYPE);
    __ j(equal, &global);
    __ cmp(ebx, JS_BUILTINS_OBJECT_TYPE);
    __ j(not_equal, &invoke);

    // Patch the receiver on the stack.
    __ bind(&global);
    __ mov(edx, FieldOperand(edx, GlobalObject::kGlobalReceiverOffset));
    __ mov(Operand(esp, (argc + 1) * kPointerSize), edx);
    __ bind(&invoke);
  }

  // Invoke the function.
  ParameterCount actual(argc);
  __ InvokeFunction(edi, actual, JUMP_FUNCTION);
}


void CallIC::GenerateMegamorphic(MacroAssembler* masm, int argc) {
  // ----------- S t a t e -------------
  //  -- ecx                 : name
  //  -- esp[0]              : return address
  //  -- esp[(argc - n) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- esp[(argc + 1) * 4] : receiver
  // -----------------------------------

  // Get the receiver of the function from the stack; 1 ~ return address.
  __ mov(edx, Operand(esp, (argc + 1) * kPointerSize));
  GenerateMonomorphicCacheProbe(masm, argc, Code::CALL_IC);
  GenerateMiss(masm, argc);
}


void CallIC::GenerateNormal(MacroAssembler* masm, int argc) {
  // ----------- S t a t e -------------
  //  -- ecx                 : name
  //  -- esp[0]              : return address
  //  -- esp[(argc - n) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- esp[(argc + 1) * 4] : receiver
  // -----------------------------------

  GenerateCallNormal(masm, argc);
  GenerateMiss(masm, argc);
}


void CallIC::GenerateMiss(MacroAssembler* masm, int argc) {
  // ----------- S t a t e -------------
  //  -- ecx                 : name
  //  -- esp[0]              : return address
  //  -- esp[(argc - n) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- esp[(argc + 1) * 4] : receiver
  // -----------------------------------

  GenerateCallMiss(masm, argc, IC::kCallIC_Miss);
}


void KeyedCallIC::GenerateMegamorphic(MacroAssembler* masm, int argc) {
  // ----------- S t a t e -------------
  //  -- ecx                 : name
  //  -- esp[0]              : return address
  //  -- esp[(argc - n) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- esp[(argc + 1) * 4] : receiver
  // -----------------------------------

  // Get the receiver of the function from the stack; 1 ~ return address.
  __ mov(edx, Operand(esp, (argc + 1) * kPointerSize));

  Label do_call, slow_call, slow_load, slow_reload_receiver;
  Label check_number_dictionary, check_string, lookup_monomorphic_cache;
  Label index_smi, index_string;

  // Check that the key is a smi.
  __ test(ecx, Immediate(kSmiTagMask));
  __ j(not_zero, &check_string, not_taken);

  __ bind(&index_smi);
  // Now the key is known to be a smi. This place is also jumped to from
  // where a numeric string is converted to a smi.

  GenerateKeyedLoadReceiverCheck(
      masm, edx, eax, Map::kHasIndexedInterceptor, &slow_call);

  GenerateFastArrayLoad(
      masm, edx, ecx, eax, edi, &check_number_dictionary, &slow_load);
  Isolate* isolate = masm->isolate();
  Counters* counters = isolate->counters();
  __ IncrementCounter(counters->keyed_call_generic_smi_fast(), 1);

  __ bind(&do_call);
  // receiver in edx is not used after this point.
  // ecx: key
  // edi: function
  GenerateFunctionTailCall(masm, argc, &slow_call);

  __ bind(&check_number_dictionary);
  // eax: elements
  // ecx: smi key
  // Check whether the elements is a number dictionary.
  __ CheckMap(eax, isolate->factory()->hash_table_map(), &slow_load, true);
  __ mov(ebx, ecx);
  __ SmiUntag(ebx);
  // ebx: untagged index
  // Receiver in edx will be clobbered, need to reload it on miss.
  GenerateNumberDictionaryLoad(
      masm, &slow_reload_receiver, eax, ecx, ebx, edx, edi, edi);
  __ IncrementCounter(counters->keyed_call_generic_smi_dict(), 1);
  __ jmp(&do_call);

  __ bind(&slow_reload_receiver);
  __ mov(edx, Operand(esp, (argc + 1) * kPointerSize));

  __ bind(&slow_load);
  // This branch is taken when calling KeyedCallIC_Miss is neither required
  // nor beneficial.
  __ IncrementCounter(counters->keyed_call_generic_slow_load(), 1);
  __ EnterInternalFrame();
  __ push(ecx);  // save the key
  __ push(edx);  // pass the receiver
  __ push(ecx);  // pass the key
  __ CallRuntime(Runtime::kKeyedGetProperty, 2);
  __ pop(ecx);  // restore the key
  __ LeaveInternalFrame();
  __ mov(edi, eax);
  __ jmp(&do_call);

  __ bind(&check_string);
  GenerateKeyStringCheck(masm, ecx, eax, ebx, &index_string, &slow_call);

  // The key is known to be a symbol.
  // If the receiver is a regular JS object with slow properties then do
  // a quick inline probe of the receiver's dictionary.
  // Otherwise do the monomorphic cache probe.
  GenerateKeyedLoadReceiverCheck(
      masm, edx, eax, Map::kHasNamedInterceptor, &lookup_monomorphic_cache);

  __ mov(ebx, FieldOperand(edx, JSObject::kPropertiesOffset));
  __ CheckMap(ebx,
              isolate->factory()->hash_table_map(),
              &lookup_monomorphic_cache,
              true);

  GenerateDictionaryLoad(masm, &slow_load, ebx, ecx, eax, edi, edi);
  __ IncrementCounter(counters->keyed_call_generic_lookup_dict(), 1);
  __ jmp(&do_call);

  __ bind(&lookup_monomorphic_cache);
  __ IncrementCounter(counters->keyed_call_generic_lookup_cache(), 1);
  GenerateMonomorphicCacheProbe(masm, argc, Code::KEYED_CALL_IC);
  // Fall through on miss.

  __ bind(&slow_call);
  // This branch is taken if:
  // - the receiver requires boxing or access check,
  // - the key is neither smi nor symbol,
  // - the value loaded is not a function,
  // - there is hope that the runtime will create a monomorphic call stub
  //   that will get fetched next time.
  __ IncrementCounter(counters->keyed_call_generic_slow(), 1);
  GenerateMiss(masm, argc);

  __ bind(&index_string);
  __ IndexFromHash(ebx, ecx);
  // Now jump to the place where smi keys are handled.
  __ jmp(&index_smi);
}


void KeyedCallIC::GenerateNormal(MacroAssembler* masm, int argc) {
  // ----------- S t a t e -------------
  //  -- ecx                 : name
  //  -- esp[0]              : return address
  //  -- esp[(argc - n) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- esp[(argc + 1) * 4] : receiver
  // -----------------------------------

  // Check if the name is a string.
  Label miss;
  __ test(ecx, Immediate(kSmiTagMask));
  __ j(zero, &miss);
  Condition cond = masm->IsObjectStringType(ecx, eax, eax);
  __ j(NegateCondition(cond), &miss);
  GenerateCallNormal(masm, argc);
  __ bind(&miss);
  GenerateMiss(masm, argc);
}


void KeyedCallIC::GenerateMiss(MacroAssembler* masm, int argc) {
  // ----------- S t a t e -------------
  //  -- ecx                 : name
  //  -- esp[0]              : return address
  //  -- esp[(argc - n) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- esp[(argc + 1) * 4] : receiver
  // -----------------------------------

  GenerateCallMiss(masm, argc, IC::kKeyedCallIC_Miss);
}


void LoadIC::GenerateMegamorphic(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : receiver
  //  -- ecx    : name
  //  -- esp[0] : return address
  // -----------------------------------

  // Probe the stub cache.
  Code::Flags flags = Code::ComputeFlags(Code::LOAD_IC,
                                         NOT_IN_LOOP,
                                         MONOMORPHIC);
  Isolate::Current()->stub_cache()->GenerateProbe(masm, flags, eax, ecx, ebx,
                                                  edx);

  // Cache miss: Jump to runtime.
  GenerateMiss(masm);
}


void LoadIC::GenerateNormal(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : receiver
  //  -- ecx    : name
  //  -- esp[0] : return address
  // -----------------------------------
  Label miss;

  GenerateStringDictionaryReceiverCheck(masm, eax, edx, ebx, &miss);

  // edx: elements
  // Search the dictionary placing the result in eax.
  GenerateDictionaryLoad(masm, &miss, edx, ecx, edi, ebx, eax);
  __ ret(0);

  // Cache miss: Jump to runtime.
  __ bind(&miss);
  GenerateMiss(masm);
}


void LoadIC::GenerateMiss(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : receiver
  //  -- ecx    : name
  //  -- esp[0] : return address
  // -----------------------------------

  __ IncrementCounter(masm->isolate()->counters()->load_miss(), 1);

  __ pop(ebx);
  __ push(eax);  // receiver
  __ push(ecx);  // name
  __ push(ebx);  // return address

  // Perform tail call to the entry.
  ExternalReference ref =
      ExternalReference(IC_Utility(kLoadIC_Miss), masm->isolate());
  __ TailCallExternalReference(ref, 2, 1);
}


bool LoadIC::PatchInlinedLoad(Address address, Object* map, int offset) {
  if (V8::UseCrankshaft()) return false;

  // The address of the instruction following the call.
  Address test_instruction_address =
      address + Assembler::kCallTargetAddressOffset;
  // If the instruction following the call is not a test eax, nothing
  // was inlined.
  if (*test_instruction_address != Assembler::kTestEaxByte) return false;

  Address delta_address = test_instruction_address + 1;
  // The delta to the start of the map check instruction.
  int delta = *reinterpret_cast<int*>(delta_address);

  // The map address is the last 4 bytes of the 7-byte
  // operand-immediate compare instruction, so we add 3 to get the
  // offset to the last 4 bytes.
  Address map_address = test_instruction_address + delta + 3;
  *(reinterpret_cast<Object**>(map_address)) = map;

  // The offset is in the last 4 bytes of a six byte
  // memory-to-register move instruction, so we add 2 to get the
  // offset to the last 4 bytes.
  Address offset_address =
      test_instruction_address + delta + kOffsetToLoadInstruction + 2;
  *reinterpret_cast<int*>(offset_address) = offset - kHeapObjectTag;
  return true;
}


// One byte opcode for mov ecx,0xXXXXXXXX.
// Marks inlined contextual loads using all kinds of cells. Generated
// code has the hole check:
//   mov reg, <cell>
//   mov reg, (<cell>, value offset)
//   cmp reg, <the hole>
//   je  slow
//   ;; use reg
static const byte kMovEcxByte = 0xB9;

// One byte opcode for mov edx,0xXXXXXXXX.
// Marks inlined contextual loads using only "don't delete"
// cells. Generated code doesn't have the hole check:
//   mov reg, <cell>
//   mov reg, (<cell>, value offset)
//   ;; use reg
static const byte kMovEdxByte = 0xBA;

bool LoadIC::PatchInlinedContextualLoad(Address address,
                                        Object* map,
                                        Object* cell,
                                        bool is_dont_delete) {
  if (V8::UseCrankshaft()) return false;

  // The address of the instruction following the call.
  Address mov_instruction_address =
      address + Assembler::kCallTargetAddressOffset;
  // If the instruction following the call is not a mov ecx/edx,
  // nothing was inlined.
  byte b = *mov_instruction_address;
  if (b != kMovEcxByte && b != kMovEdxByte) return false;
  // If we don't have the hole check generated, we can only support
  // "don't delete" cells.
  if (b == kMovEdxByte && !is_dont_delete) return false;

  Address delta_address = mov_instruction_address + 1;
  // The delta to the start of the map check instruction.
  int delta = *reinterpret_cast<int*>(delta_address);

  // The map address is the last 4 bytes of the 7-byte
  // operand-immediate compare instruction, so we add 3 to get the
  // offset to the last 4 bytes.
  Address map_address = mov_instruction_address + delta + 3;
  *(reinterpret_cast<Object**>(map_address)) = map;

  // The cell is in the last 4 bytes of a five byte mov reg, imm32
  // instruction, so we add 1 to get the offset to the last 4 bytes.
  Address offset_address =
      mov_instruction_address + delta + kOffsetToLoadInstruction + 1;
  *reinterpret_cast<Object**>(offset_address) = cell;
  return true;
}


bool StoreIC::PatchInlinedStore(Address address, Object* map, int offset) {
  if (V8::UseCrankshaft()) return false;

  // The address of the instruction following the call.
  Address test_instruction_address =
      address + Assembler::kCallTargetAddressOffset;

  // If the instruction following the call is not a test eax, nothing
  // was inlined.
  if (*test_instruction_address != Assembler::kTestEaxByte) return false;

  // Extract the encoded deltas from the test eax instruction.
  Address encoded_offsets_address = test_instruction_address + 1;
  int encoded_offsets = *reinterpret_cast<int*>(encoded_offsets_address);
  int delta_to_map_check = -(encoded_offsets & 0xFFFF);
  int delta_to_record_write = encoded_offsets >> 16;

  // Patch the map to check. The map address is the last 4 bytes of
  // the 7-byte operand-immediate compare instruction.
  Address map_check_address = test_instruction_address + delta_to_map_check;
  Address map_address = map_check_address + 3;
  *(reinterpret_cast<Object**>(map_address)) = map;

  // Patch the offset in the store instruction. The offset is in the
  // last 4 bytes of a six byte register-to-memory move instruction.
  Address offset_address =
      map_check_address + StoreIC::kOffsetToStoreInstruction + 2;
  // The offset should have initial value (kMaxInt - 1), cleared value
  // (-1) or we should be clearing the inlined version.
  ASSERT(*reinterpret_cast<int*>(offset_address) == kMaxInt - 1 ||
         *reinterpret_cast<int*>(offset_address) == -1 ||
         (offset == 0 && map == HEAP->null_value()));
  *reinterpret_cast<int*>(offset_address) = offset - kHeapObjectTag;

  // Patch the offset in the write-barrier code. The offset is the
  // last 4 bytes of a six byte lea instruction.
  offset_address = map_check_address + delta_to_record_write + 2;
  // The offset should have initial value (kMaxInt), cleared value
  // (-1) or we should be clearing the inlined version.
  ASSERT(*reinterpret_cast<int*>(offset_address) == kMaxInt ||
         *reinterpret_cast<int*>(offset_address) == -1 ||
         (offset == 0 && map == HEAP->null_value()));
  *reinterpret_cast<int*>(offset_address) = offset - kHeapObjectTag;

  return true;
}


static bool PatchInlinedMapCheck(Address address, Object* map) {
  if (V8::UseCrankshaft()) return false;

  Address test_instruction_address =
      address + Assembler::kCallTargetAddressOffset;
  // The keyed load has a fast inlined case if the IC call instruction
  // is immediately followed by a test instruction.
  if (*test_instruction_address != Assembler::kTestEaxByte) return false;

  // Fetch the offset from the test instruction to the map cmp
  // instruction.  This offset is stored in the last 4 bytes of the 5
  // byte test instruction.
  Address delta_address = test_instruction_address + 1;
  int delta = *reinterpret_cast<int*>(delta_address);
  // Compute the map address.  The map address is in the last 4 bytes
  // of the 7-byte operand-immediate compare instruction, so we add 3
  // to the offset to get the map address.
  Address map_address = test_instruction_address + delta + 3;
  // Patch the map check.
  *(reinterpret_cast<Object**>(map_address)) = map;
  return true;
}


bool KeyedLoadIC::PatchInlinedLoad(Address address, Object* map) {
  return PatchInlinedMapCheck(address, map);
}


bool KeyedStoreIC::PatchInlinedStore(Address address, Object* map) {
  return PatchInlinedMapCheck(address, map);
}


void KeyedLoadIC::GenerateMiss(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : key
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------

  __ IncrementCounter(masm->isolate()->counters()->keyed_load_miss(), 1);

  __ pop(ebx);
  __ push(edx);  // receiver
  __ push(eax);  // name
  __ push(ebx);  // return address

  // Perform tail call to the entry.
  ExternalReference ref =
      ExternalReference(IC_Utility(kKeyedLoadIC_Miss), masm->isolate());
  __ TailCallExternalReference(ref, 2, 1);
}


void KeyedLoadIC::GenerateRuntimeGetProperty(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : key
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------

  __ pop(ebx);
  __ push(edx);  // receiver
  __ push(eax);  // name
  __ push(ebx);  // return address

  // Perform tail call to the entry.
  __ TailCallRuntime(Runtime::kKeyedGetProperty, 2, 1);
}


void StoreIC::GenerateMegamorphic(MacroAssembler* masm,
                                  StrictModeFlag strict_mode) {
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : name
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------

  Code::Flags flags = Code::ComputeFlags(Code::STORE_IC,
                                         NOT_IN_LOOP,
                                         MONOMORPHIC,
                                         strict_mode);
  Isolate::Current()->stub_cache()->GenerateProbe(masm, flags, edx, ecx, ebx,
                                                  no_reg);

  // Cache miss: Jump to runtime.
  GenerateMiss(masm);
}


void StoreIC::GenerateMiss(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : name
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------

  __ pop(ebx);
  __ push(edx);
  __ push(ecx);
  __ push(eax);
  __ push(ebx);

  // Perform tail call to the entry.
  ExternalReference ref =
      ExternalReference(IC_Utility(kStoreIC_Miss), masm->isolate());
  __ TailCallExternalReference(ref, 3, 1);
}


// The offset from the inlined patch site to the start of the inlined
// store instruction.  It is 7 bytes (test reg, imm) plus 6 bytes (jne
// slow_label).
const int StoreIC::kOffsetToStoreInstruction = 13;


void StoreIC::GenerateArrayLength(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : name
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------
  //
  // This accepts as a receiver anything JSObject::SetElementsLength accepts
  // (currently anything except for external arrays which means anything with
  // elements of FixedArray type.), but currently is restricted to JSArray.
  // Value must be a number, but only smis are accepted as the most common case.

  Label miss;

  Register receiver = edx;
  Register value = eax;
  Register scratch = ebx;

  // Check that the receiver isn't a smi.
  __ test(receiver, Immediate(kSmiTagMask));
  __ j(zero, &miss, not_taken);

  // Check that the object is a JS array.
  __ CmpObjectType(receiver, JS_ARRAY_TYPE, scratch);
  __ j(not_equal, &miss, not_taken);

  // Check that elements are FixedArray.
  // We rely on StoreIC_ArrayLength below to deal with all types of
  // fast elements (including COW).
  __ mov(scratch, FieldOperand(receiver, JSArray::kElementsOffset));
  __ CmpObjectType(scratch, FIXED_ARRAY_TYPE, scratch);
  __ j(not_equal, &miss, not_taken);

  // Check that value is a smi.
  __ test(value, Immediate(kSmiTagMask));
  __ j(not_zero, &miss, not_taken);

  // Prepare tail call to StoreIC_ArrayLength.
  __ pop(scratch);
  __ push(receiver);
  __ push(value);
  __ push(scratch);  // return address

  ExternalReference ref =
      ExternalReference(IC_Utility(kStoreIC_ArrayLength), masm->isolate());
  __ TailCallExternalReference(ref, 2, 1);

  __ bind(&miss);

  GenerateMiss(masm);
}


void StoreIC::GenerateNormal(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : name
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------

  Label miss, restore_miss;

  GenerateStringDictionaryReceiverCheck(masm, edx, ebx, edi, &miss);

  // A lot of registers are needed for storing to slow case
  // objects. Push and restore receiver but rely on
  // GenerateDictionaryStore preserving the value and name.
  __ push(edx);
  GenerateDictionaryStore(masm, &restore_miss, ebx, ecx, eax, edx, edi);
  __ Drop(1);
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->store_normal_hit(), 1);
  __ ret(0);

  __ bind(&restore_miss);
  __ pop(edx);

  __ bind(&miss);
  __ IncrementCounter(counters->store_normal_miss(), 1);
  GenerateMiss(masm);
}


void StoreIC::GenerateGlobalProxy(MacroAssembler* masm,
                                  StrictModeFlag strict_mode) {
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : name
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------
  __ pop(ebx);
  __ push(edx);
  __ push(ecx);
  __ push(eax);
  __ push(Immediate(Smi::FromInt(NONE)));  // PropertyAttributes
  __ push(Immediate(Smi::FromInt(strict_mode)));
  __ push(ebx);  // return address

  // Do tail-call to runtime routine.
  __ TailCallRuntime(Runtime::kSetProperty, 5, 1);
}


void KeyedStoreIC::GenerateRuntimeSetProperty(MacroAssembler* masm,
                                              StrictModeFlag strict_mode) {
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : key
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------

  __ pop(ebx);
  __ push(edx);
  __ push(ecx);
  __ push(eax);
  __ push(Immediate(Smi::FromInt(NONE)));         // PropertyAttributes
  __ push(Immediate(Smi::FromInt(strict_mode)));  // Strict mode.
  __ push(ebx);   // return address

  // Do tail-call to runtime routine.
  __ TailCallRuntime(Runtime::kSetProperty, 5, 1);
}


void KeyedStoreIC::GenerateMiss(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : value
  //  -- ecx    : key
  //  -- edx    : receiver
  //  -- esp[0] : return address
  // -----------------------------------

  __ pop(ebx);
  __ push(edx);
  __ push(ecx);
  __ push(eax);
  __ push(ebx);

  // Do tail-call to runtime routine.
  ExternalReference ref =
      ExternalReference(IC_Utility(kKeyedStoreIC_Miss), masm->isolate());
  __ TailCallExternalReference(ref, 3, 1);
}


#undef __


Condition CompareIC::ComputeCondition(Token::Value op) {
  switch (op) {
    case Token::EQ_STRICT:
    case Token::EQ:
      return equal;
    case Token::LT:
      return less;
    case Token::GT:
      // Reverse left and right operands to obtain ECMA-262 conversion order.
      return less;
    case Token::LTE:
      // Reverse left and right operands to obtain ECMA-262 conversion order.
      return greater_equal;
    case Token::GTE:
      return greater_equal;
    default:
      UNREACHABLE();
      return no_condition;
  }
}


static bool HasInlinedSmiCode(Address address) {
  // The address of the instruction following the call.
  Address test_instruction_address =
      address + Assembler::kCallTargetAddressOffset;

  // If the instruction following the call is not a test al, nothing
  // was inlined.
  return *test_instruction_address == Assembler::kTestAlByte;
}


void CompareIC::UpdateCaches(Handle<Object> x, Handle<Object> y) {
  HandleScope scope;
  Handle<Code> rewritten;
  State previous_state = GetState();

  State state = TargetState(previous_state, HasInlinedSmiCode(address()), x, y);
  if (state == GENERIC) {
    CompareStub stub(GetCondition(), strict(), NO_COMPARE_FLAGS);
    rewritten = stub.GetCode();
  } else {
    ICCompareStub stub(op_, state);
    rewritten = stub.GetCode();
  }
  set_target(*rewritten);

#ifdef DEBUG
  if (FLAG_trace_ic) {
    PrintF("[CompareIC (%s->%s)#%s]\n",
           GetStateName(previous_state),
           GetStateName(state),
           Token::Name(op_));
  }
#endif

  // Activate inlined smi code.
  if (previous_state == UNINITIALIZED) {
    PatchInlinedSmiCode(address());
  }
}


void PatchInlinedSmiCode(Address address) {
  // The address of the instruction following the call.
  Address test_instruction_address =
      address + Assembler::kCallTargetAddressOffset;

  // If the instruction following the call is not a test al, nothing
  // was inlined.
  if (*test_instruction_address != Assembler::kTestAlByte) {
    ASSERT(*test_instruction_address == Assembler::kNopByte);
    return;
  }

  Address delta_address = test_instruction_address + 1;
  // The delta to the start of the map check instruction and the
  // condition code uses at the patched jump.
  int8_t delta = *reinterpret_cast<int8_t*>(delta_address);
  if (FLAG_trace_ic) {
    PrintF("[  patching ic at %p, test=%p, delta=%d\n",
           address, test_instruction_address, delta);
  }

  // Patch with a short conditional jump. There must be a
  // short jump-if-carry/not-carry at this position.
  Address jmp_address = test_instruction_address - delta;
  ASSERT(*jmp_address == Assembler::kJncShortOpcode ||
         *jmp_address == Assembler::kJcShortOpcode);
  Condition cc = *jmp_address == Assembler::kJncShortOpcode
      ? not_zero
      : zero;
  *jmp_address = static_cast<byte>(Assembler::kJccShortPrefix | cc);
}


} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_IA32
