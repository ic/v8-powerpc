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

#ifndef V8_OBJECTS_H_
#define V8_OBJECTS_H_

#include "allocation.h"
#include "builtins.h"
#include "list.h"
#include "smart-pointer.h"
#include "unicode-inl.h"
#if V8_TARGET_ARCH_ARM
#include "arm/constants-arm.h"
#elif V8_TARGET_ARCH_MIPS
#include "mips/constants-mips.h"
#endif

//
// Most object types in the V8 JavaScript are described in this file.
//
// Inheritance hierarchy:
// - MaybeObject    (an object or a failure)
//   - Failure      (immediate for marking failed operation)
//   - Object
//     - Smi          (immediate small integer)
//     - HeapObject   (superclass for everything allocated in the heap)
//       - JSReceiver  (suitable for property access)
//         - JSObject
//           - JSArray
//           - JSRegExp
//           - JSFunction
//           - GlobalObject
//             - JSGlobalObject
//             - JSBuiltinsObject
//           - JSGlobalProxy
//           - JSValue
//           - JSMessageObject
//         - JSProxy
//           - JSFunctionProxy
//       - ByteArray
//       - ExternalArray
//         - ExternalPixelArray
//         - ExternalByteArray
//         - ExternalUnsignedByteArray
//         - ExternalShortArray
//         - ExternalUnsignedShortArray
//         - ExternalIntArray
//         - ExternalUnsignedIntArray
//         - ExternalFloatArray
//       - FixedArray
//         - DescriptorArray
//         - HashTable
//           - Dictionary
//           - SymbolTable
//           - CompilationCacheTable
//           - CodeCacheHashTable
//           - MapCache
//         - Context
//         - JSFunctionResultCache
//         - SerializedScopeInfo
//       - String
//         - SeqString
//           - SeqAsciiString
//           - SeqTwoByteString
//         - ConsString
//         - ExternalString
//           - ExternalAsciiString
//           - ExternalTwoByteString
//       - HeapNumber
//       - Code
//       - Map
//       - Oddball
//       - Foreign
//       - SharedFunctionInfo
//       - Struct
//         - AccessorInfo
//         - AccessCheckInfo
//         - InterceptorInfo
//         - CallHandlerInfo
//         - TemplateInfo
//           - FunctionTemplateInfo
//           - ObjectTemplateInfo
//         - Script
//         - SignatureInfo
//         - TypeSwitchInfo
//         - DebugInfo
//         - BreakPointInfo
//         - CodeCache
//
// Formats of Object*:
//  Smi:        [31 bit signed int] 0
//  HeapObject: [32 bit direct pointer] (4 byte aligned) | 01
//  Failure:    [30 bit signed int] 11

// Ecma-262 3rd 8.6.1
enum PropertyAttributes {
  NONE              = v8::None,
  READ_ONLY         = v8::ReadOnly,
  DONT_ENUM         = v8::DontEnum,
  DONT_DELETE       = v8::DontDelete,
  ABSENT            = 16  // Used in runtime to indicate a property is absent.
  // ABSENT can never be stored in or returned from a descriptor's attributes
  // bitfield.  It is only used as a return value meaning the attributes of
  // a non-existent property.
};

namespace v8 {
namespace internal {


// PropertyDetails captures type and attributes for a property.
// They are used both in property dictionaries and instance descriptors.
class PropertyDetails BASE_EMBEDDED {
 public:
  PropertyDetails(PropertyAttributes attributes,
                  PropertyType type,
                  int index = 0) {
    ASSERT(type != EXTERNAL_ARRAY_TRANSITION);
    ASSERT(TypeField::is_valid(type));
    ASSERT(AttributesField::is_valid(attributes));
    ASSERT(StorageField::is_valid(index));

    value_ = TypeField::encode(type)
        | AttributesField::encode(attributes)
        | StorageField::encode(index);

    ASSERT(type == this->type());
    ASSERT(attributes == this->attributes());
    ASSERT(index == this->index());
  }

  PropertyDetails(PropertyAttributes attributes,
                  PropertyType type,
                  ExternalArrayType array_type) {
    ASSERT(type == EXTERNAL_ARRAY_TRANSITION);
    ASSERT(TypeField::is_valid(type));
    ASSERT(AttributesField::is_valid(attributes));
    ASSERT(StorageField::is_valid(static_cast<int>(array_type)));

    value_ = TypeField::encode(type)
        | AttributesField::encode(attributes)
        | StorageField::encode(static_cast<int>(array_type));

    ASSERT(type == this->type());
    ASSERT(attributes == this->attributes());
    ASSERT(array_type == this->array_type());
  }

  // Conversion for storing details as Object*.
  explicit inline PropertyDetails(Smi* smi);
  inline Smi* AsSmi();

  PropertyType type() { return TypeField::decode(value_); }

  bool IsTransition() {
    PropertyType t = type();
    ASSERT(t != INTERCEPTOR);
    return t == MAP_TRANSITION || t == CONSTANT_TRANSITION ||
        t == EXTERNAL_ARRAY_TRANSITION;
  }

  bool IsProperty() {
    return type() < FIRST_PHANTOM_PROPERTY_TYPE;
  }

  PropertyAttributes attributes() { return AttributesField::decode(value_); }

  int index() { return StorageField::decode(value_); }

  ExternalArrayType array_type() {
    ASSERT(type() == EXTERNAL_ARRAY_TRANSITION);
    return static_cast<ExternalArrayType>(StorageField::decode(value_));
  }

  inline PropertyDetails AsDeleted();

  static bool IsValidIndex(int index) {
    return StorageField::is_valid(index);
  }

  bool IsReadOnly() { return (attributes() & READ_ONLY) != 0; }
  bool IsDontDelete() { return (attributes() & DONT_DELETE) != 0; }
  bool IsDontEnum() { return (attributes() & DONT_ENUM) != 0; }
  bool IsDeleted() { return DeletedField::decode(value_) != 0;}

  // Bit fields in value_ (type, shift, size). Must be public so the
  // constants can be embedded in generated code.
  class TypeField:       public BitField<PropertyType,       0, 4> {};
  class AttributesField: public BitField<PropertyAttributes, 4, 3> {};
  class DeletedField:    public BitField<uint32_t,           7, 1> {};
  class StorageField:    public BitField<uint32_t,           8, 32-8> {};

  static const int kInitialIndex = 1;

 private:
  uint32_t value_;
};


// Setter that skips the write barrier if mode is SKIP_WRITE_BARRIER.
enum WriteBarrierMode { SKIP_WRITE_BARRIER, UPDATE_WRITE_BARRIER };


// PropertyNormalizationMode is used to specify whether to keep
// inobject properties when normalizing properties of a JSObject.
enum PropertyNormalizationMode {
  CLEAR_INOBJECT_PROPERTIES,
  KEEP_INOBJECT_PROPERTIES
};


// NormalizedMapSharingMode is used to specify whether a map may be shared
// by different objects with normalized properties.
enum NormalizedMapSharingMode {
  UNIQUE_NORMALIZED_MAP,
  SHARED_NORMALIZED_MAP
};


// Instance size sentinel for objects of variable size.
static const int kVariableSizeSentinel = 0;


// All Maps have a field instance_type containing a InstanceType.
// It describes the type of the instances.
//
// As an example, a JavaScript object is a heap object and its map
// instance_type is JS_OBJECT_TYPE.
//
// The names of the string instance types are intended to systematically
// mirror their encoding in the instance_type field of the map.  The default
// encoding is considered TWO_BYTE.  It is not mentioned in the name.  ASCII
// encoding is mentioned explicitly in the name.  Likewise, the default
// representation is considered sequential.  It is not mentioned in the
// name.  The other representations (eg, CONS, EXTERNAL) are explicitly
// mentioned.  Finally, the string is either a SYMBOL_TYPE (if it is a
// symbol) or a STRING_TYPE (if it is not a symbol).
//
// NOTE: The following things are some that depend on the string types having
// instance_types that are less than those of all other types:
// HeapObject::Size, HeapObject::IterateBody, the typeof operator, and
// Object::IsString.
//
// NOTE: Everything following JS_VALUE_TYPE is considered a
// JSObject for GC purposes. The first four entries here have typeof
// 'object', whereas JS_FUNCTION_TYPE has typeof 'function'.
#define INSTANCE_TYPE_LIST_ALL(V)                                              \
  V(SYMBOL_TYPE)                                                               \
  V(ASCII_SYMBOL_TYPE)                                                         \
  V(CONS_SYMBOL_TYPE)                                                          \
  V(CONS_ASCII_SYMBOL_TYPE)                                                    \
  V(EXTERNAL_SYMBOL_TYPE)                                                      \
  V(EXTERNAL_SYMBOL_WITH_ASCII_DATA_TYPE)                                      \
  V(EXTERNAL_ASCII_SYMBOL_TYPE)                                                \
  V(STRING_TYPE)                                                               \
  V(ASCII_STRING_TYPE)                                                         \
  V(CONS_STRING_TYPE)                                                          \
  V(CONS_ASCII_STRING_TYPE)                                                    \
  V(EXTERNAL_STRING_TYPE)                                                      \
  V(EXTERNAL_STRING_WITH_ASCII_DATA_TYPE)                                      \
  V(EXTERNAL_ASCII_STRING_TYPE)                                                \
  V(PRIVATE_EXTERNAL_ASCII_STRING_TYPE)                                        \
                                                                               \
  V(MAP_TYPE)                                                                  \
  V(CODE_TYPE)                                                                 \
  V(ODDBALL_TYPE)                                                              \
  V(JS_GLOBAL_PROPERTY_CELL_TYPE)                                              \
                                                                               \
  V(HEAP_NUMBER_TYPE)                                                          \
  V(FOREIGN_TYPE)                                                              \
  V(BYTE_ARRAY_TYPE)                                                           \
  /* Note: the order of these external array */                                \
  /* types is relied upon in */                                                \
  /* Object::IsExternalArray(). */                                             \
  V(EXTERNAL_BYTE_ARRAY_TYPE)                                                  \
  V(EXTERNAL_UNSIGNED_BYTE_ARRAY_TYPE)                                         \
  V(EXTERNAL_SHORT_ARRAY_TYPE)                                                 \
  V(EXTERNAL_UNSIGNED_SHORT_ARRAY_TYPE)                                        \
  V(EXTERNAL_INT_ARRAY_TYPE)                                                   \
  V(EXTERNAL_UNSIGNED_INT_ARRAY_TYPE)                                          \
  V(EXTERNAL_FLOAT_ARRAY_TYPE)                                                 \
  V(EXTERNAL_PIXEL_ARRAY_TYPE)                                                 \
  V(FILLER_TYPE)                                                               \
                                                                               \
  V(ACCESSOR_INFO_TYPE)                                                        \
  V(ACCESS_CHECK_INFO_TYPE)                                                    \
  V(INTERCEPTOR_INFO_TYPE)                                                     \
  V(CALL_HANDLER_INFO_TYPE)                                                    \
  V(FUNCTION_TEMPLATE_INFO_TYPE)                                               \
  V(OBJECT_TEMPLATE_INFO_TYPE)                                                 \
  V(SIGNATURE_INFO_TYPE)                                                       \
  V(TYPE_SWITCH_INFO_TYPE)                                                     \
  V(SCRIPT_TYPE)                                                               \
  V(CODE_CACHE_TYPE)                                                           \
  V(POLYMORPHIC_CODE_CACHE_TYPE)                                               \
                                                                               \
  V(FIXED_ARRAY_TYPE)                                                          \
  V(SHARED_FUNCTION_INFO_TYPE)                                                 \
                                                                               \
  V(JS_MESSAGE_OBJECT_TYPE)                                                    \
                                                                               \
  V(JS_VALUE_TYPE)                                                             \
  V(JS_OBJECT_TYPE)                                                            \
  V(JS_CONTEXT_EXTENSION_OBJECT_TYPE)                                          \
  V(JS_GLOBAL_OBJECT_TYPE)                                                     \
  V(JS_BUILTINS_OBJECT_TYPE)                                                   \
  V(JS_GLOBAL_PROXY_TYPE)                                                      \
  V(JS_ARRAY_TYPE)                                                             \
  V(JS_PROXY_TYPE)                                                             \
  V(JS_REGEXP_TYPE)                                                            \
                                                                               \
  V(JS_FUNCTION_TYPE)                                                          \
  V(JS_FUNCTION_PROXY_TYPE)                                                    \

#ifdef ENABLE_DEBUGGER_SUPPORT
#define INSTANCE_TYPE_LIST_DEBUGGER(V)                                         \
  V(DEBUG_INFO_TYPE)                                                           \
  V(BREAK_POINT_INFO_TYPE)
#else
#define INSTANCE_TYPE_LIST_DEBUGGER(V)
#endif

#define INSTANCE_TYPE_LIST(V)                                                  \
  INSTANCE_TYPE_LIST_ALL(V)                                                    \
  INSTANCE_TYPE_LIST_DEBUGGER(V)


// Since string types are not consecutive, this macro is used to
// iterate over them.
#define STRING_TYPE_LIST(V)                                                    \
  V(SYMBOL_TYPE,                                                               \
    kVariableSizeSentinel,                                                     \
    symbol,                                                                    \
    Symbol)                                                                    \
  V(ASCII_SYMBOL_TYPE,                                                         \
    kVariableSizeSentinel,                                                     \
    ascii_symbol,                                                              \
    AsciiSymbol)                                                               \
  V(CONS_SYMBOL_TYPE,                                                          \
    ConsString::kSize,                                                         \
    cons_symbol,                                                               \
    ConsSymbol)                                                                \
  V(CONS_ASCII_SYMBOL_TYPE,                                                    \
    ConsString::kSize,                                                         \
    cons_ascii_symbol,                                                         \
    ConsAsciiSymbol)                                                           \
  V(EXTERNAL_SYMBOL_TYPE,                                                      \
    ExternalTwoByteString::kSize,                                              \
    external_symbol,                                                           \
    ExternalSymbol)                                                            \
  V(EXTERNAL_SYMBOL_WITH_ASCII_DATA_TYPE,                                      \
    ExternalTwoByteString::kSize,                                              \
    external_symbol_with_ascii_data,                                           \
    ExternalSymbolWithAsciiData)                                               \
  V(EXTERNAL_ASCII_SYMBOL_TYPE,                                                \
    ExternalAsciiString::kSize,                                                \
    external_ascii_symbol,                                                     \
    ExternalAsciiSymbol)                                                       \
  V(STRING_TYPE,                                                               \
    kVariableSizeSentinel,                                                     \
    string,                                                                    \
    String)                                                                    \
  V(ASCII_STRING_TYPE,                                                         \
    kVariableSizeSentinel,                                                     \
    ascii_string,                                                              \
    AsciiString)                                                               \
  V(CONS_STRING_TYPE,                                                          \
    ConsString::kSize,                                                         \
    cons_string,                                                               \
    ConsString)                                                                \
  V(CONS_ASCII_STRING_TYPE,                                                    \
    ConsString::kSize,                                                         \
    cons_ascii_string,                                                         \
    ConsAsciiString)                                                           \
  V(EXTERNAL_STRING_TYPE,                                                      \
    ExternalTwoByteString::kSize,                                              \
    external_string,                                                           \
    ExternalString)                                                            \
  V(EXTERNAL_STRING_WITH_ASCII_DATA_TYPE,                                      \
    ExternalTwoByteString::kSize,                                              \
    external_string_with_ascii_data,                                           \
    ExternalStringWithAsciiData)                                               \
  V(EXTERNAL_ASCII_STRING_TYPE,                                                \
    ExternalAsciiString::kSize,                                                \
    external_ascii_string,                                                     \
    ExternalAsciiString)

// A struct is a simple object a set of object-valued fields.  Including an
// object type in this causes the compiler to generate most of the boilerplate
// code for the class including allocation and garbage collection routines,
// casts and predicates.  All you need to define is the class, methods and
// object verification routines.  Easy, no?
//
// Note that for subtle reasons related to the ordering or numerical values of
// type tags, elements in this list have to be added to the INSTANCE_TYPE_LIST
// manually.
#define STRUCT_LIST_ALL(V)                                                     \
  V(ACCESSOR_INFO, AccessorInfo, accessor_info)                                \
  V(ACCESS_CHECK_INFO, AccessCheckInfo, access_check_info)                     \
  V(INTERCEPTOR_INFO, InterceptorInfo, interceptor_info)                       \
  V(CALL_HANDLER_INFO, CallHandlerInfo, call_handler_info)                     \
  V(FUNCTION_TEMPLATE_INFO, FunctionTemplateInfo, function_template_info)      \
  V(OBJECT_TEMPLATE_INFO, ObjectTemplateInfo, object_template_info)            \
  V(SIGNATURE_INFO, SignatureInfo, signature_info)                             \
  V(TYPE_SWITCH_INFO, TypeSwitchInfo, type_switch_info)                        \
  V(SCRIPT, Script, script)                                                    \
  V(CODE_CACHE, CodeCache, code_cache)                                         \
  V(POLYMORPHIC_CODE_CACHE, PolymorphicCodeCache, polymorphic_code_cache)

#ifdef ENABLE_DEBUGGER_SUPPORT
#define STRUCT_LIST_DEBUGGER(V)                                                \
  V(DEBUG_INFO, DebugInfo, debug_info)                                         \
  V(BREAK_POINT_INFO, BreakPointInfo, break_point_info)
#else
#define STRUCT_LIST_DEBUGGER(V)
#endif

#define STRUCT_LIST(V)                                                         \
  STRUCT_LIST_ALL(V)                                                           \
  STRUCT_LIST_DEBUGGER(V)

// We use the full 8 bits of the instance_type field to encode heap object
// instance types.  The high-order bit (bit 7) is set if the object is not a
// string, and cleared if it is a string.
const uint32_t kIsNotStringMask = 0x80;
const uint32_t kStringTag = 0x0;
const uint32_t kNotStringTag = 0x80;

// Bit 6 indicates that the object is a symbol (if set) or not (if cleared).
// There are not enough types that the non-string types (with bit 7 set) can
// have bit 6 set too.
const uint32_t kIsSymbolMask = 0x40;
const uint32_t kNotSymbolTag = 0x0;
const uint32_t kSymbolTag = 0x40;

// If bit 7 is clear then bit 2 indicates whether the string consists of
// two-byte characters or one-byte characters.
const uint32_t kStringEncodingMask = 0x4;
const uint32_t kTwoByteStringTag = 0x0;
const uint32_t kAsciiStringTag = 0x4;

// If bit 7 is clear, the low-order 2 bits indicate the representation
// of the string.
const uint32_t kStringRepresentationMask = 0x03;
enum StringRepresentationTag {
  kSeqStringTag = 0x0,
  kConsStringTag = 0x1,
  kExternalStringTag = 0x2
};
const uint32_t kIsConsStringMask = 0x1;

// If bit 7 is clear, then bit 3 indicates whether this two-byte
// string actually contains ascii data.
const uint32_t kAsciiDataHintMask = 0x08;
const uint32_t kAsciiDataHintTag = 0x08;


// A ConsString with an empty string as the right side is a candidate
// for being shortcut by the garbage collector unless it is a
// symbol. It's not common to have non-flat symbols, so we do not
// shortcut them thereby avoiding turning symbols into strings. See
// heap.cc and mark-compact.cc.
const uint32_t kShortcutTypeMask =
    kIsNotStringMask |
    kIsSymbolMask |
    kStringRepresentationMask;
const uint32_t kShortcutTypeTag = kConsStringTag;


enum InstanceType {
  // String types.
  SYMBOL_TYPE = kTwoByteStringTag | kSymbolTag | kSeqStringTag,
  ASCII_SYMBOL_TYPE = kAsciiStringTag | kSymbolTag | kSeqStringTag,
  CONS_SYMBOL_TYPE = kTwoByteStringTag | kSymbolTag | kConsStringTag,
  CONS_ASCII_SYMBOL_TYPE = kAsciiStringTag | kSymbolTag | kConsStringTag,
  EXTERNAL_SYMBOL_TYPE = kTwoByteStringTag | kSymbolTag | kExternalStringTag,
  EXTERNAL_SYMBOL_WITH_ASCII_DATA_TYPE =
      kTwoByteStringTag | kSymbolTag | kExternalStringTag | kAsciiDataHintTag,
  EXTERNAL_ASCII_SYMBOL_TYPE =
      kAsciiStringTag | kSymbolTag | kExternalStringTag,
  STRING_TYPE = kTwoByteStringTag | kSeqStringTag,
  ASCII_STRING_TYPE = kAsciiStringTag | kSeqStringTag,
  CONS_STRING_TYPE = kTwoByteStringTag | kConsStringTag,
  CONS_ASCII_STRING_TYPE = kAsciiStringTag | kConsStringTag,
  EXTERNAL_STRING_TYPE = kTwoByteStringTag | kExternalStringTag,
  EXTERNAL_STRING_WITH_ASCII_DATA_TYPE =
      kTwoByteStringTag | kExternalStringTag | kAsciiDataHintTag,
  // LAST_STRING_TYPE
  EXTERNAL_ASCII_STRING_TYPE = kAsciiStringTag | kExternalStringTag,
  PRIVATE_EXTERNAL_ASCII_STRING_TYPE = EXTERNAL_ASCII_STRING_TYPE,

  // Objects allocated in their own spaces (never in new space).
  MAP_TYPE = kNotStringTag,  // FIRST_NONSTRING_TYPE
  CODE_TYPE,
  ODDBALL_TYPE,
  JS_GLOBAL_PROPERTY_CELL_TYPE,

  // "Data", objects that cannot contain non-map-word pointers to heap
  // objects.
  HEAP_NUMBER_TYPE,
  FOREIGN_TYPE,
  BYTE_ARRAY_TYPE,
  EXTERNAL_BYTE_ARRAY_TYPE,  // FIRST_EXTERNAL_ARRAY_TYPE
  EXTERNAL_UNSIGNED_BYTE_ARRAY_TYPE,
  EXTERNAL_SHORT_ARRAY_TYPE,
  EXTERNAL_UNSIGNED_SHORT_ARRAY_TYPE,
  EXTERNAL_INT_ARRAY_TYPE,
  EXTERNAL_UNSIGNED_INT_ARRAY_TYPE,
  EXTERNAL_FLOAT_ARRAY_TYPE,
  EXTERNAL_DOUBLE_ARRAY_TYPE,
  EXTERNAL_PIXEL_ARRAY_TYPE,  // LAST_EXTERNAL_ARRAY_TYPE
  FIXED_DOUBLE_ARRAY_TYPE,
  FILLER_TYPE,  // LAST_DATA_TYPE

  // Structs.
  ACCESSOR_INFO_TYPE,
  ACCESS_CHECK_INFO_TYPE,
  INTERCEPTOR_INFO_TYPE,
  CALL_HANDLER_INFO_TYPE,
  FUNCTION_TEMPLATE_INFO_TYPE,
  OBJECT_TEMPLATE_INFO_TYPE,
  SIGNATURE_INFO_TYPE,
  TYPE_SWITCH_INFO_TYPE,
  SCRIPT_TYPE,
  CODE_CACHE_TYPE,
  POLYMORPHIC_CODE_CACHE_TYPE,
  // The following two instance types are only used when ENABLE_DEBUGGER_SUPPORT
  // is defined. However as include/v8.h contain some of the instance type
  // constants always having them avoids them getting different numbers
  // depending on whether ENABLE_DEBUGGER_SUPPORT is defined or not.
  DEBUG_INFO_TYPE,
  BREAK_POINT_INFO_TYPE,

  FIXED_ARRAY_TYPE,
  SHARED_FUNCTION_INFO_TYPE,

  JS_MESSAGE_OBJECT_TYPE,

  JS_VALUE_TYPE,  // FIRST_NON_CALLABLE_OBJECT_TYPE, FIRST_JS_RECEIVER_TYPE
  JS_OBJECT_TYPE,
  JS_CONTEXT_EXTENSION_OBJECT_TYPE,
  JS_GLOBAL_OBJECT_TYPE,
  JS_BUILTINS_OBJECT_TYPE,
  JS_GLOBAL_PROXY_TYPE,
  JS_ARRAY_TYPE,
  JS_PROXY_TYPE,

  JS_REGEXP_TYPE,  // LAST_NONCALLABLE_SPEC_OBJECT_TYPE

  JS_FUNCTION_TYPE,  // FIRST_CALLABLE_SPEC_OBJECT_TYPE
  JS_FUNCTION_PROXY_TYPE,  // LAST_CALLABLE_SPEC_OBJECT_TYPE

  // Pseudo-types
  FIRST_TYPE = 0x0,
  LAST_TYPE = JS_FUNCTION_PROXY_TYPE,
  INVALID_TYPE = FIRST_TYPE - 1,
  FIRST_NONSTRING_TYPE = MAP_TYPE,
  // Boundaries for testing for an external array.
  FIRST_EXTERNAL_ARRAY_TYPE = EXTERNAL_BYTE_ARRAY_TYPE,
  LAST_EXTERNAL_ARRAY_TYPE = EXTERNAL_PIXEL_ARRAY_TYPE,
  // Boundary for promotion to old data space/old pointer space.
  LAST_DATA_TYPE = FILLER_TYPE,
  // Boundary for objects represented as JSReceiver (i.e. JSObject or JSProxy).
  // Note that there is no range for JSObject or JSProxy, since their subtypes
  // are not continuous in this enum! The enum ranges instead reflect the
  // external class names, where proxies are treated as either ordinary objects,
  // or functions.
  FIRST_JS_RECEIVER_TYPE = JS_VALUE_TYPE,
  LAST_JS_RECEIVER_TYPE = LAST_TYPE,
  // Boundaries for testing the types for which typeof is "object".
  FIRST_NONCALLABLE_SPEC_OBJECT_TYPE = JS_VALUE_TYPE,
  LAST_NONCALLABLE_SPEC_OBJECT_TYPE = JS_REGEXP_TYPE,
  // Boundaries for testing the types for which typeof is "function".
  FIRST_CALLABLE_SPEC_OBJECT_TYPE = JS_FUNCTION_TYPE,
  LAST_CALLABLE_SPEC_OBJECT_TYPE = JS_FUNCTION_PROXY_TYPE,
  // Boundaries for testing whether the type is a JavaScript object.
  FIRST_SPEC_OBJECT_TYPE = FIRST_NONCALLABLE_SPEC_OBJECT_TYPE,
  LAST_SPEC_OBJECT_TYPE = LAST_CALLABLE_SPEC_OBJECT_TYPE
};

static const int kExternalArrayTypeCount = LAST_EXTERNAL_ARRAY_TYPE -
    FIRST_EXTERNAL_ARRAY_TYPE + 1;

STATIC_CHECK(JS_OBJECT_TYPE == Internals::kJSObjectType);
STATIC_CHECK(FIRST_NONSTRING_TYPE == Internals::kFirstNonstringType);
STATIC_CHECK(FOREIGN_TYPE == Internals::kForeignType);


enum CompareResult {
  LESS      = -1,
  EQUAL     =  0,
  GREATER   =  1,

  NOT_EQUAL = GREATER
};


#define DECL_BOOLEAN_ACCESSORS(name)   \
  inline bool name();                  \
  inline void set_##name(bool value);  \


#define DECL_ACCESSORS(name, type)                                      \
  inline type* name();                                                  \
  inline void set_##name(type* value,                                   \
                         WriteBarrierMode mode = UPDATE_WRITE_BARRIER); \


class StringStream;
class ObjectVisitor;

struct ValueInfo : public Malloced {
  ValueInfo() : type(FIRST_TYPE), ptr(NULL), str(NULL), number(0) { }
  InstanceType type;
  Object* ptr;
  const char* str;
  double number;
};


// A template-ized version of the IsXXX functions.
template <class C> static inline bool Is(Object* obj);

class Failure;

class MaybeObject BASE_EMBEDDED {
 public:
  inline bool IsFailure();
  inline bool IsRetryAfterGC();
  inline bool IsOutOfMemory();
  inline bool IsException();
  INLINE(bool IsTheHole());
  inline bool ToObject(Object** obj) {
    if (IsFailure()) return false;
    *obj = reinterpret_cast<Object*>(this);
    return true;
  }
  inline Failure* ToFailureUnchecked() {
    ASSERT(IsFailure());
    return reinterpret_cast<Failure*>(this);
  }
  inline Object* ToObjectUnchecked() {
    ASSERT(!IsFailure());
    return reinterpret_cast<Object*>(this);
  }
  inline Object* ToObjectChecked() {
    CHECK(!IsFailure());
    return reinterpret_cast<Object*>(this);
  }

  template<typename T>
  inline bool To(T** obj) {
    if (IsFailure()) return false;
    *obj = T::cast(reinterpret_cast<Object*>(this));
    return true;
  }

#ifdef OBJECT_PRINT
  // Prints this object with details.
  inline void Print() {
    Print(stdout);
  };
  inline void PrintLn() {
    PrintLn(stdout);
  }
  void Print(FILE* out);
  void PrintLn(FILE* out);
#endif
#ifdef DEBUG
  // Verifies the object.
  void Verify();
#endif
};


#define OBJECT_TYPE_LIST(V)                    \
  V(Smi)                                       \
  V(HeapObject)                                \
  V(Number)                                    \

#define HEAP_OBJECT_TYPE_LIST(V)               \
  V(HeapNumber)                                \
  V(String)                                    \
  V(Symbol)                                    \
  V(SeqString)                                 \
  V(ExternalString)                            \
  V(ConsString)                                \
  V(ExternalTwoByteString)                     \
  V(ExternalAsciiString)                       \
  V(SeqTwoByteString)                          \
  V(SeqAsciiString)                            \
                                               \
  V(ExternalArray)                             \
  V(ExternalByteArray)                         \
  V(ExternalUnsignedByteArray)                 \
  V(ExternalShortArray)                        \
  V(ExternalUnsignedShortArray)                \
  V(ExternalIntArray)                          \
  V(ExternalUnsignedIntArray)                  \
  V(ExternalFloatArray)                        \
  V(ExternalDoubleArray)                       \
  V(ExternalPixelArray)                        \
  V(ByteArray)                                 \
  V(JSReceiver)                                \
  V(JSObject)                                  \
  V(JSContextExtensionObject)                  \
  V(Map)                                       \
  V(DescriptorArray)                           \
  V(DeoptimizationInputData)                   \
  V(DeoptimizationOutputData)                  \
  V(FixedArray)                                \
  V(FixedDoubleArray)                          \
  V(Context)                                   \
  V(GlobalContext)                             \
  V(JSFunction)                                \
  V(Code)                                      \
  V(Oddball)                                   \
  V(SharedFunctionInfo)                        \
  V(JSValue)                                   \
  V(JSMessageObject)                           \
  V(StringWrapper)                             \
  V(Foreign)                                   \
  V(Boolean)                                   \
  V(JSArray)                                   \
  V(JSProxy)                                   \
  V(JSFunctionProxy)                           \
  V(JSRegExp)                                  \
  V(HashTable)                                 \
  V(Dictionary)                                \
  V(SymbolTable)                               \
  V(JSFunctionResultCache)                     \
  V(NormalizedMapCache)                        \
  V(CompilationCacheTable)                     \
  V(CodeCacheHashTable)                        \
  V(PolymorphicCodeCacheHashTable)             \
  V(MapCache)                                  \
  V(Primitive)                                 \
  V(GlobalObject)                              \
  V(JSGlobalObject)                            \
  V(JSBuiltinsObject)                          \
  V(JSGlobalProxy)                             \
  V(UndetectableObject)                        \
  V(AccessCheckNeeded)                         \
  V(JSGlobalPropertyCell)                      \

// Object is the abstract superclass for all classes in the
// object hierarchy.
// Object does not use any virtual functions to avoid the
// allocation of the C++ vtable.
// Since Smi and Failure are subclasses of Object no
// data members can be present in Object.
class Object : public MaybeObject {
 public:
  // Type testing.
#define IS_TYPE_FUNCTION_DECL(type_)  inline bool Is##type_();
  OBJECT_TYPE_LIST(IS_TYPE_FUNCTION_DECL)
  HEAP_OBJECT_TYPE_LIST(IS_TYPE_FUNCTION_DECL)
#undef IS_TYPE_FUNCTION_DECL

  // Returns true if this object is an instance of the specified
  // function template.
  inline bool IsInstanceOf(FunctionTemplateInfo* type);

  inline bool IsStruct();
#define DECLARE_STRUCT_PREDICATE(NAME, Name, name) inline bool Is##Name();
  STRUCT_LIST(DECLARE_STRUCT_PREDICATE)
#undef DECLARE_STRUCT_PREDICATE

  INLINE(bool IsSpecObject());

  // Oddball testing.
  INLINE(bool IsUndefined());
  INLINE(bool IsNull());
  INLINE(bool IsTheHole());  // Shadows MaybeObject's implementation.
  INLINE(bool IsTrue());
  INLINE(bool IsFalse());
  inline bool IsArgumentsMarker();

  // Extract the number.
  inline double Number();

  // Returns true if the object is of the correct type to be used as a
  // implementation of a JSObject's elements.
  inline bool HasValidElements();

  inline bool HasSpecificClassOf(String* name);

  MUST_USE_RESULT MaybeObject* ToObject();             // ECMA-262 9.9.
  Object* ToBoolean();                                 // ECMA-262 9.2.

  // Convert to a JSObject if needed.
  // global_context is used when creating wrapper object.
  MUST_USE_RESULT MaybeObject* ToObject(Context* global_context);

  // Converts this to a Smi if possible.
  // Failure is returned otherwise.
  MUST_USE_RESULT inline MaybeObject* ToSmi();

  void Lookup(String* name, LookupResult* result);

  // Property access.
  MUST_USE_RESULT inline MaybeObject* GetProperty(String* key);
  MUST_USE_RESULT inline MaybeObject* GetProperty(
      String* key,
      PropertyAttributes* attributes);
  MUST_USE_RESULT MaybeObject* GetPropertyWithReceiver(
      Object* receiver,
      String* key,
      PropertyAttributes* attributes);
  MUST_USE_RESULT MaybeObject* GetProperty(Object* receiver,
                                           LookupResult* result,
                                           String* key,
                                           PropertyAttributes* attributes);
  MUST_USE_RESULT MaybeObject* GetPropertyWithCallback(Object* receiver,
                                                       Object* structure,
                                                       String* name,
                                                       Object* holder);
  MUST_USE_RESULT MaybeObject* GetPropertyWithHandler(Object* receiver,
                                                      String* name,
                                                      Object* handler);
  MUST_USE_RESULT MaybeObject* GetPropertyWithDefinedGetter(Object* receiver,
                                                            JSFunction* getter);

  inline MaybeObject* GetElement(uint32_t index);
  // For use when we know that no exception can be thrown.
  inline Object* GetElementNoExceptionThrown(uint32_t index);
  MaybeObject* GetElementWithReceiver(Object* receiver, uint32_t index);

  // Return the object's prototype (might be Heap::null_value()).
  Object* GetPrototype();

  // Tries to convert an object to an array index.  Returns true and sets
  // the output parameter if it succeeds.
  inline bool ToArrayIndex(uint32_t* index);

  // Returns true if this is a JSValue containing a string and the index is
  // < the length of the string.  Used to implement [] on strings.
  inline bool IsStringObjectWithCharacterAt(uint32_t index);

#ifdef DEBUG
  // Verify a pointer is a valid object pointer.
  static void VerifyPointer(Object* p);
#endif

  // Prints this object without details.
  inline void ShortPrint() {
    ShortPrint(stdout);
  }
  void ShortPrint(FILE* out);

  // Prints this object without details to a message accumulator.
  void ShortPrint(StringStream* accumulator);

  // Casting: This cast is only needed to satisfy macros in objects-inl.h.
  static Object* cast(Object* value) { return value; }

  // Layout description.
  static const int kHeaderSize = 0;  // Object does not take up any space.

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Object);
};


// Smi represents integer Numbers that can be stored in 31 bits.
// Smis are immediate which means they are NOT allocated in the heap.
// The this pointer has the following format: [31 bit signed int] 0
// For long smis it has the following format:
//     [32 bit signed int] [31 bits zero padding] 0
// Smi stands for small integer.
class Smi: public Object {
 public:
  // Returns the integer value.
  inline int value();

  // Convert a value to a Smi object.
  static inline Smi* FromInt(int value);

  static inline Smi* FromIntptr(intptr_t value);

  // Returns whether value can be represented in a Smi.
  static inline bool IsValid(intptr_t value);

  // Casting.
  static inline Smi* cast(Object* object);

  // Dispatched behavior.
  inline void SmiPrint() {
    SmiPrint(stdout);
  }
  void SmiPrint(FILE* out);
  void SmiPrint(StringStream* accumulator);
#ifdef DEBUG
  void SmiVerify();
#endif

  static const int kMinValue = (-1 << (kSmiValueSize - 1));
  static const int kMaxValue = -(kMinValue + 1);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Smi);
};


// Failure is used for reporting out of memory situations and
// propagating exceptions through the runtime system.  Failure objects
// are transient and cannot occur as part of the object graph.
//
// Failures are a single word, encoded as follows:
// +-------------------------+---+--+--+
// |.........unused..........|sss|tt|11|
// +-------------------------+---+--+--+
//                          7 6 4 32 10
//
//
// The low two bits, 0-1, are the failure tag, 11.  The next two bits,
// 2-3, are a failure type tag 'tt' with possible values:
//   00 RETRY_AFTER_GC
//   01 EXCEPTION
//   10 INTERNAL_ERROR
//   11 OUT_OF_MEMORY_EXCEPTION
//
// The next three bits, 4-6, are an allocation space tag 'sss'.  The
// allocation space tag is 000 for all failure types except
// RETRY_AFTER_GC.  For RETRY_AFTER_GC, the possible values are the
// allocation spaces (the encoding is found in globals.h).

// Failure type tag info.
const int kFailureTypeTagSize = 2;
const int kFailureTypeTagMask = (1 << kFailureTypeTagSize) - 1;

class Failure: public MaybeObject {
 public:
  // RuntimeStubs assumes EXCEPTION = 1 in the compiler-generated code.
  enum Type {
    RETRY_AFTER_GC = 0,
    EXCEPTION = 1,       // Returning this marker tells the real exception
                         // is in Isolate::pending_exception.
    INTERNAL_ERROR = 2,
    OUT_OF_MEMORY_EXCEPTION = 3
  };

  inline Type type() const;

  // Returns the space that needs to be collected for RetryAfterGC failures.
  inline AllocationSpace allocation_space() const;

  inline bool IsInternalError() const;
  inline bool IsOutOfMemoryException() const;

  static inline Failure* RetryAfterGC(AllocationSpace space);
  static inline Failure* RetryAfterGC();  // NEW_SPACE
  static inline Failure* Exception();
  static inline Failure* InternalError();
  static inline Failure* OutOfMemoryException();
  // Casting.
  static inline Failure* cast(MaybeObject* object);

  // Dispatched behavior.
  inline void FailurePrint() {
    FailurePrint(stdout);
  }
  void FailurePrint(FILE* out);
  void FailurePrint(StringStream* accumulator);
#ifdef DEBUG
  void FailureVerify();
#endif

 private:
  inline intptr_t value() const;
  static inline Failure* Construct(Type type, intptr_t value = 0);

  DISALLOW_IMPLICIT_CONSTRUCTORS(Failure);
};


// Heap objects typically have a map pointer in their first word.  However,
// during GC other data (eg, mark bits, forwarding addresses) is sometimes
// encoded in the first word.  The class MapWord is an abstraction of the
// value in a heap object's first word.
class MapWord BASE_EMBEDDED {
 public:
  // Normal state: the map word contains a map pointer.

  // Create a map word from a map pointer.
  static inline MapWord FromMap(Map* map);

  // View this map word as a map pointer.
  inline Map* ToMap();


  // Scavenge collection: the map word of live objects in the from space
  // contains a forwarding address (a heap object pointer in the to space).

  // True if this map word is a forwarding address for a scavenge
  // collection.  Only valid during a scavenge collection (specifically,
  // when all map words are heap object pointers, ie. not during a full GC).
  inline bool IsForwardingAddress();

  // Create a map word from a forwarding address.
  static inline MapWord FromForwardingAddress(HeapObject* object);

  // View this map word as a forwarding address.
  inline HeapObject* ToForwardingAddress();

  // Marking phase of full collection: the map word of live objects is
  // marked, and may be marked as overflowed (eg, the object is live, its
  // children have not been visited, and it does not fit in the marking
  // stack).

  // True if this map word's mark bit is set.
  inline bool IsMarked();

  // Return this map word but with its mark bit set.
  inline void SetMark();

  // Return this map word but with its mark bit cleared.
  inline void ClearMark();

  // True if this map word's overflow bit is set.
  inline bool IsOverflowed();

  // Return this map word but with its overflow bit set.
  inline void SetOverflow();

  // Return this map word but with its overflow bit cleared.
  inline void ClearOverflow();


  // Compacting phase of a full compacting collection: the map word of live
  // objects contains an encoding of the original map address along with the
  // forwarding address (represented as an offset from the first live object
  // in the same page as the (old) object address).

  // Create a map word from a map address and a forwarding address offset.
  static inline MapWord EncodeAddress(Address map_address, int offset);

  // Return the map address encoded in this map word.
  inline Address DecodeMapAddress(MapSpace* map_space);

  // Return the forwarding offset encoded in this map word.
  inline int DecodeOffset();


  // During serialization: the map word is used to hold an encoded
  // address, and possibly a mark bit (set and cleared with SetMark
  // and ClearMark).

  // Create a map word from an encoded address.
  static inline MapWord FromEncodedAddress(Address address);

  inline Address ToEncodedAddress();

  // Bits used by the marking phase of the garbage collector.
  //
  // The first word of a heap object is normally a map pointer. The last two
  // bits are tagged as '01' (kHeapObjectTag). We reuse the last two bits to
  // mark an object as live and/or overflowed:
  //   last bit = 0, marked as alive
  //   second bit = 1, overflowed
  // An object is only marked as overflowed when it is marked as live while
  // the marking stack is overflowed.
  static const int kMarkingBit = 0;  // marking bit
  static const int kMarkingMask = (1 << kMarkingBit);  // marking mask
  static const int kOverflowBit = 1;  // overflow bit
  static const int kOverflowMask = (1 << kOverflowBit);  // overflow mask

  // Forwarding pointers and map pointer encoding. On 32 bit all the bits are
  // used.
  // +-----------------+------------------+-----------------+
  // |forwarding offset|page offset of map|page index of map|
  // +-----------------+------------------+-----------------+
  //          ^                 ^                  ^
  //          |                 |                  |
  //          |                 |          kMapPageIndexBits
  //          |         kMapPageOffsetBits
  // kForwardingOffsetBits
  static const int kMapPageOffsetBits = kPageSizeBits - kMapAlignmentBits;
  static const int kForwardingOffsetBits = kPageSizeBits - kObjectAlignmentBits;
#ifdef V8_HOST_ARCH_64_BIT
  static const int kMapPageIndexBits = 16;
#else
  // Use all the 32-bits to encode on a 32-bit platform.
  static const int kMapPageIndexBits =
      32 - (kMapPageOffsetBits + kForwardingOffsetBits);
#endif

  static const int kMapPageIndexShift = 0;
  static const int kMapPageOffsetShift =
      kMapPageIndexShift + kMapPageIndexBits;
  static const int kForwardingOffsetShift =
      kMapPageOffsetShift + kMapPageOffsetBits;

  // Bit masks covering the different parts the encoding.
  static const uintptr_t kMapPageIndexMask =
      (1 << kMapPageOffsetShift) - 1;
  static const uintptr_t kMapPageOffsetMask =
      ((1 << kForwardingOffsetShift) - 1) & ~kMapPageIndexMask;
  static const uintptr_t kForwardingOffsetMask =
      ~(kMapPageIndexMask | kMapPageOffsetMask);

 private:
  // HeapObject calls the private constructor and directly reads the value.
  friend class HeapObject;

  explicit MapWord(uintptr_t value) : value_(value) {}

  uintptr_t value_;
};


// HeapObject is the superclass for all classes describing heap allocated
// objects.
class HeapObject: public Object {
 public:
  // [map]: Contains a map which contains the object's reflective
  // information.
  inline Map* map();
  inline void set_map(Map* value);

  // During garbage collection, the map word of a heap object does not
  // necessarily contain a map pointer.
  inline MapWord map_word();
  inline void set_map_word(MapWord map_word);

  // The Heap the object was allocated in. Used also to access Isolate.
  // This method can not be used during GC, it ASSERTs this.
  inline Heap* GetHeap();
  // Convenience method to get current isolate. This method can be
  // accessed only when its result is the same as
  // Isolate::Current(), it ASSERTs this. See also comment for GetHeap.
  inline Isolate* GetIsolate();

  // Converts an address to a HeapObject pointer.
  static inline HeapObject* FromAddress(Address address);

  // Returns the address of this HeapObject.
  inline Address address();

  // Iterates over pointers contained in the object (including the Map)
  void Iterate(ObjectVisitor* v);

  // Iterates over all pointers contained in the object except the
  // first map pointer.  The object type is given in the first
  // parameter. This function does not access the map pointer in the
  // object, and so is safe to call while the map pointer is modified.
  void IterateBody(InstanceType type, int object_size, ObjectVisitor* v);

  // Returns the heap object's size in bytes
  inline int Size();

  // Given a heap object's map pointer, returns the heap size in bytes
  // Useful when the map pointer field is used for other purposes.
  // GC internal.
  inline int SizeFromMap(Map* map);

  // Support for the marking heap objects during the marking phase of GC.
  // True if the object is marked live.
  inline bool IsMarked();

  // Mutate this object's map pointer to indicate that the object is live.
  inline void SetMark();

  // Mutate this object's map pointer to remove the indication that the
  // object is live (ie, partially restore the map pointer).
  inline void ClearMark();

  // True if this object is marked as overflowed.  Overflowed objects have
  // been reached and marked during marking of the heap, but their children
  // have not necessarily been marked and they have not been pushed on the
  // marking stack.
  inline bool IsOverflowed();

  // Mutate this object's map pointer to indicate that the object is
  // overflowed.
  inline void SetOverflow();

  // Mutate this object's map pointer to remove the indication that the
  // object is overflowed (ie, partially restore the map pointer).
  inline void ClearOverflow();

  // Returns the field at offset in obj, as a read/write Object* reference.
  // Does no checking, and is safe to use during GC, while maps are invalid.
  // Does not invoke write barrier, so should only be assigned to
  // during marking GC.
  static inline Object** RawField(HeapObject* obj, int offset);

  // Casting.
  static inline HeapObject* cast(Object* obj);

  // Return the write barrier mode for this. Callers of this function
  // must be able to present a reference to an AssertNoAllocation
  // object as a sign that they are not going to use this function
  // from code that allocates and thus invalidates the returned write
  // barrier mode.
  inline WriteBarrierMode GetWriteBarrierMode(const AssertNoAllocation&);

  // Dispatched behavior.
  void HeapObjectShortPrint(StringStream* accumulator);
#ifdef OBJECT_PRINT
  inline void HeapObjectPrint() {
    HeapObjectPrint(stdout);
  }
  void HeapObjectPrint(FILE* out);
#endif
#ifdef DEBUG
  void HeapObjectVerify();
  inline void VerifyObjectField(int offset);
  inline void VerifySmiField(int offset);
#endif

#ifdef OBJECT_PRINT
  void PrintHeader(FILE* out, const char* id);
#endif

#ifdef DEBUG
  // Verify a pointer is a valid HeapObject pointer that points to object
  // areas in the heap.
  static void VerifyHeapPointer(Object* p);
#endif

  // Layout description.
  // First field in a heap object is map.
  static const int kMapOffset = Object::kHeaderSize;
  static const int kHeaderSize = kMapOffset + kPointerSize;

  STATIC_CHECK(kMapOffset == Internals::kHeapObjectMapOffset);

 protected:
  // helpers for calling an ObjectVisitor to iterate over pointers in the
  // half-open range [start, end) specified as integer offsets
  inline void IteratePointers(ObjectVisitor* v, int start, int end);
  // as above, for the single element at "offset"
  inline void IteratePointer(ObjectVisitor* v, int offset);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(HeapObject);
};


#define SLOT_ADDR(obj, offset) \
  reinterpret_cast<Object**>((obj)->address() + offset)

// This class describes a body of an object of a fixed size
// in which all pointer fields are located in the [start_offset, end_offset)
// interval.
template<int start_offset, int end_offset, int size>
class FixedBodyDescriptor {
 public:
  static const int kStartOffset = start_offset;
  static const int kEndOffset = end_offset;
  static const int kSize = size;

  static inline void IterateBody(HeapObject* obj, ObjectVisitor* v);

  template<typename StaticVisitor>
  static inline void IterateBody(HeapObject* obj) {
    StaticVisitor::VisitPointers(SLOT_ADDR(obj, start_offset),
                                 SLOT_ADDR(obj, end_offset));
  }
};


// This class describes a body of an object of a variable size
// in which all pointer fields are located in the [start_offset, object_size)
// interval.
template<int start_offset>
class FlexibleBodyDescriptor {
 public:
  static const int kStartOffset = start_offset;

  static inline void IterateBody(HeapObject* obj,
                                 int object_size,
                                 ObjectVisitor* v);

  template<typename StaticVisitor>
  static inline void IterateBody(HeapObject* obj, int object_size) {
    StaticVisitor::VisitPointers(SLOT_ADDR(obj, start_offset),
                                 SLOT_ADDR(obj, object_size));
  }
};

#undef SLOT_ADDR


// The HeapNumber class describes heap allocated numbers that cannot be
// represented in a Smi (small integer)
class HeapNumber: public HeapObject {
 public:
  // [value]: number value.
  inline double value();
  inline void set_value(double value);

  // Casting.
  static inline HeapNumber* cast(Object* obj);

  // Dispatched behavior.
  Object* HeapNumberToBoolean();
  inline void HeapNumberPrint() {
    HeapNumberPrint(stdout);
  }
  void HeapNumberPrint(FILE* out);
  void HeapNumberPrint(StringStream* accumulator);
#ifdef DEBUG
  void HeapNumberVerify();
#endif

  inline int get_exponent();
  inline int get_sign();

  // Layout description.
  static const int kValueOffset = HeapObject::kHeaderSize;
  // IEEE doubles are two 32 bit words.  The first is just mantissa, the second
  // is a mixture of sign, exponent and mantissa.  Our current platforms are all
  // little endian apart from non-EABI arm which is little endian with big
  // endian floating point word ordering!
  static const int kMantissaOffset = kValueOffset;
  static const int kExponentOffset = kValueOffset + 4;

  static const int kSize = kValueOffset + kDoubleSize;
  static const uint32_t kSignMask = 0x80000000u;
  static const uint32_t kExponentMask = 0x7ff00000u;
  static const uint32_t kMantissaMask = 0xfffffu;
  static const int kMantissaBits = 52;
  static const int kExponentBits = 11;
  static const int kExponentBias = 1023;
  static const int kExponentShift = 20;
  static const int kMantissaBitsInTopWord = 20;
  static const int kNonMantissaBitsInTopWord = 12;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(HeapNumber);
};


// JSReceiver includes types on which properties can be defined, i.e.,
// JSObject and JSProxy.
class JSReceiver: public HeapObject {
 public:
  enum DeleteMode {
    NORMAL_DELETION,
    STRICT_DELETION,
    FORCE_DELETION
  };

  // Casting.
  static inline JSReceiver* cast(Object* obj);

  // Can cause GC.
  MUST_USE_RESULT MaybeObject* SetProperty(String* key,
                                           Object* value,
                                           PropertyAttributes attributes,
                                           StrictModeFlag strict_mode);
  MUST_USE_RESULT MaybeObject* SetProperty(LookupResult* result,
                                           String* key,
                                           Object* value,
                                           PropertyAttributes attributes,
                                           StrictModeFlag strict_mode);

  MUST_USE_RESULT MaybeObject* DeleteProperty(String* name, DeleteMode mode);

  // Returns the class name ([[Class]] property in the specification).
  String* class_name();

  // Returns the constructor name (the name (possibly, inferred name) of the
  // function that was used to instantiate the object).
  String* constructor_name();

  inline PropertyAttributes GetPropertyAttribute(String* name);
  PropertyAttributes GetPropertyAttributeWithReceiver(JSReceiver* receiver,
                                                      String* name);
  PropertyAttributes GetLocalPropertyAttribute(String* name);

  // Can cause a GC.
  inline bool HasProperty(String* name);
  inline bool HasLocalProperty(String* name);

  // Return the object's prototype (might be Heap::null_value()).
  inline Object* GetPrototype();

  // Set the object's prototype (only JSReceiver and null are allowed).
  MUST_USE_RESULT MaybeObject* SetPrototype(Object* value,
                                            bool skip_hidden_prototypes);

  // Lookup a property.  If found, the result is valid and has
  // detailed information.
  void LocalLookup(String* name, LookupResult* result);
  void Lookup(String* name, LookupResult* result);

 private:
  PropertyAttributes GetPropertyAttribute(JSReceiver* receiver,
                                          LookupResult* result,
                                          String* name,
                                          bool continue_search);

  DISALLOW_IMPLICIT_CONSTRUCTORS(JSReceiver);
};

// The JSObject describes real heap allocated JavaScript objects with
// properties.
// Note that the map of JSObject changes during execution to enable inline
// caching.
class JSObject: public JSReceiver {
 public:
  enum ElementsKind {
    // The "fast" kind for tagged values. Must be first to make it possible
    // to efficiently check maps if they have fast elements.
    FAST_ELEMENTS,

    // The "fast" kind for unwrapped, non-tagged double values.
    FAST_DOUBLE_ELEMENTS,

    // The "slow" kind.
    DICTIONARY_ELEMENTS,
    NON_STRICT_ARGUMENTS_ELEMENTS,
    // The "fast" kind for external arrays
    EXTERNAL_BYTE_ELEMENTS,
    EXTERNAL_UNSIGNED_BYTE_ELEMENTS,
    EXTERNAL_SHORT_ELEMENTS,
    EXTERNAL_UNSIGNED_SHORT_ELEMENTS,
    EXTERNAL_INT_ELEMENTS,
    EXTERNAL_UNSIGNED_INT_ELEMENTS,
    EXTERNAL_FLOAT_ELEMENTS,
    EXTERNAL_DOUBLE_ELEMENTS,
    EXTERNAL_PIXEL_ELEMENTS,

    // Derived constants from ElementsKind
    FIRST_EXTERNAL_ARRAY_ELEMENTS_KIND = EXTERNAL_BYTE_ELEMENTS,
    LAST_EXTERNAL_ARRAY_ELEMENTS_KIND = EXTERNAL_PIXEL_ELEMENTS,
    FIRST_ELEMENTS_KIND = FAST_ELEMENTS,
    LAST_ELEMENTS_KIND = EXTERNAL_PIXEL_ELEMENTS
  };

  static const int kElementsKindCount =
    LAST_ELEMENTS_KIND - FIRST_ELEMENTS_KIND + 1;

  // [properties]: Backing storage for properties.
  // properties is a FixedArray in the fast case and a Dictionary in the
  // slow case.
  DECL_ACCESSORS(properties, FixedArray)  // Get and set fast properties.
  inline void initialize_properties();
  inline bool HasFastProperties();
  inline StringDictionary* property_dictionary();  // Gets slow properties.

  // [elements]: The elements (properties with names that are integers).
  //
  // Elements can be in two general modes: fast and slow. Each mode
  // corrensponds to a set of object representations of elements that
  // have something in common.
  //
  // In the fast mode elements is a FixedArray and so each element can
  // be quickly accessed. This fact is used in the generated code. The
  // elements array can have one of three maps in this mode:
  // fixed_array_map, non_strict_arguments_elements_map or
  // fixed_cow_array_map (for copy-on-write arrays). In the latter case
  // the elements array may be shared by a few objects and so before
  // writing to any element the array must be copied. Use
  // EnsureWritableFastElements in this case.
  //
  // In the slow mode the elements is either a NumberDictionary, an
  // ExternalArray, or a FixedArray parameter map for a (non-strict)
  // arguments object.
  DECL_ACCESSORS(elements, HeapObject)
  inline void initialize_elements();
  MUST_USE_RESULT inline MaybeObject* ResetElements();
  inline ElementsKind GetElementsKind();
  inline bool HasFastElements();
  inline bool HasFastDoubleElements();
  inline bool HasDictionaryElements();
  inline bool HasExternalPixelElements();
  inline bool HasExternalArrayElements();
  inline bool HasExternalByteElements();
  inline bool HasExternalUnsignedByteElements();
  inline bool HasExternalShortElements();
  inline bool HasExternalUnsignedShortElements();
  inline bool HasExternalIntElements();
  inline bool HasExternalUnsignedIntElements();
  inline bool HasExternalFloatElements();
  inline bool HasExternalDoubleElements();
  bool HasFastArgumentsElements();
  bool HasDictionaryArgumentsElements();
  inline bool AllowsSetElementsLength();
  inline NumberDictionary* element_dictionary();  // Gets slow elements.

  // Requires: HasFastElements().
  MUST_USE_RESULT inline MaybeObject* EnsureWritableFastElements();

  // Collects elements starting at index 0.
  // Undefined values are placed after non-undefined values.
  // Returns the number of non-undefined values.
  MUST_USE_RESULT MaybeObject* PrepareElementsForSort(uint32_t limit);
  // As PrepareElementsForSort, but only on objects where elements is
  // a dictionary, and it will stay a dictionary.
  MUST_USE_RESULT MaybeObject* PrepareSlowElementsForSort(uint32_t limit);

  MUST_USE_RESULT MaybeObject* SetPropertyForResult(LookupResult* result,
                                           String* key,
                                           Object* value,
                                           PropertyAttributes attributes,
                                           StrictModeFlag strict_mode);
  MUST_USE_RESULT MaybeObject* SetPropertyWithFailedAccessCheck(
      LookupResult* result,
      String* name,
      Object* value,
      bool check_prototype,
      StrictModeFlag strict_mode);
  MUST_USE_RESULT MaybeObject* SetPropertyWithCallback(
      Object* structure,
      String* name,
      Object* value,
      JSObject* holder,
      StrictModeFlag strict_mode);
  MUST_USE_RESULT MaybeObject* SetPropertyWithDefinedSetter(JSFunction* setter,
                                                            Object* value);
  MUST_USE_RESULT MaybeObject* SetPropertyWithInterceptor(
      String* name,
      Object* value,
      PropertyAttributes attributes,
      StrictModeFlag strict_mode);
  MUST_USE_RESULT MaybeObject* SetPropertyPostInterceptor(
      String* name,
      Object* value,
      PropertyAttributes attributes,
      StrictModeFlag strict_mode);
  MUST_USE_RESULT MaybeObject* SetLocalPropertyIgnoreAttributes(
      String* key,
      Object* value,
      PropertyAttributes attributes);

  // Retrieve a value in a normalized object given a lookup result.
  // Handles the special representation of JS global objects.
  Object* GetNormalizedProperty(LookupResult* result);

  // Sets the property value in a normalized object given a lookup result.
  // Handles the special representation of JS global objects.
  Object* SetNormalizedProperty(LookupResult* result, Object* value);

  // Sets the property value in a normalized object given (key, value, details).
  // Handles the special representation of JS global objects.
  MUST_USE_RESULT MaybeObject* SetNormalizedProperty(String* name,
                                                     Object* value,
                                                     PropertyDetails details);

  // Deletes the named property in a normalized object.
  MUST_USE_RESULT MaybeObject* DeleteNormalizedProperty(String* name,
                                                        DeleteMode mode);

  // Retrieve interceptors.
  InterceptorInfo* GetNamedInterceptor();
  InterceptorInfo* GetIndexedInterceptor();

  // Used from JSReceiver.
  PropertyAttributes GetPropertyAttributePostInterceptor(JSObject* receiver,
                                                         String* name,
                                                         bool continue_search);
  PropertyAttributes GetPropertyAttributeWithInterceptor(JSObject* receiver,
                                                         String* name,
                                                         bool continue_search);
  PropertyAttributes GetPropertyAttributeWithFailedAccessCheck(
      Object* receiver,
      LookupResult* result,
      String* name,
      bool continue_search);

  MUST_USE_RESULT MaybeObject* DefineAccessor(String* name,
                                              bool is_getter,
                                              Object* fun,
                                              PropertyAttributes attributes);
  Object* LookupAccessor(String* name, bool is_getter);

  MUST_USE_RESULT MaybeObject* DefineAccessor(AccessorInfo* info);

  // Used from Object::GetProperty().
  MaybeObject* GetPropertyWithFailedAccessCheck(
      Object* receiver,
      LookupResult* result,
      String* name,
      PropertyAttributes* attributes);
  MaybeObject* GetPropertyWithInterceptor(
      JSReceiver* receiver,
      String* name,
      PropertyAttributes* attributes);
  MaybeObject* GetPropertyPostInterceptor(
      JSReceiver* receiver,
      String* name,
      PropertyAttributes* attributes);
  MaybeObject* GetLocalPropertyPostInterceptor(JSReceiver* receiver,
                                               String* name,
                                               PropertyAttributes* attributes);

  // Returns true if this is an instance of an api function and has
  // been modified since it was created.  May give false positives.
  bool IsDirty();

  // If the receiver is a JSGlobalProxy this method will return its prototype,
  // otherwise the result is the receiver itself.
  inline Object* BypassGlobalProxy();

  // Accessors for hidden properties object.
  //
  // Hidden properties are not local properties of the object itself.
  // Instead they are stored on an auxiliary JSObject stored as a local
  // property with a special name Heap::hidden_symbol(). But if the
  // receiver is a JSGlobalProxy then the auxiliary object is a property
  // of its prototype.
  //
  // Has/Get/SetHiddenPropertiesObject methods don't allow the holder to be
  // a JSGlobalProxy. Use BypassGlobalProxy method above to get to the real
  // holder.
  //
  // These accessors do not touch interceptors or accessors.
  inline bool HasHiddenPropertiesObject();
  inline Object* GetHiddenPropertiesObject();
  MUST_USE_RESULT inline MaybeObject* SetHiddenPropertiesObject(
      Object* hidden_obj);

  MUST_USE_RESULT MaybeObject* DeleteProperty(String* name, DeleteMode mode);
  MUST_USE_RESULT MaybeObject* DeleteElement(uint32_t index, DeleteMode mode);

  // Tests for the fast common case for property enumeration.
  bool IsSimpleEnum();

  // Do we want to keep the elements in fast case when increasing the
  // capacity?
  bool ShouldConvertToSlowElements(int new_capacity);
  // Returns true if the backing storage for the slow-case elements of
  // this object takes up nearly as much space as a fast-case backing
  // storage would.  In that case the JSObject should have fast
  // elements.
  bool ShouldConvertToFastElements();
  // Returns true if the elements of JSObject contains only values that can be
  // represented in a FixedDoubleArray.
  bool CanConvertToFastDoubleElements();

  // Tells whether the index'th element is present.
  inline bool HasElement(uint32_t index);
  bool HasElementWithReceiver(JSReceiver* receiver, uint32_t index);

  // Computes the new capacity when expanding the elements of a JSObject.
  static int NewElementsCapacity(int old_capacity) {
    // (old_capacity + 50%) + 16
    return old_capacity + (old_capacity >> 1) + 16;
  }

  // Tells whether the index'th element is present and how it is stored.
  enum LocalElementType {
    // There is no element with given index.
    UNDEFINED_ELEMENT,

    // Element with given index is handled by interceptor.
    INTERCEPTED_ELEMENT,

    // Element with given index is character in string.
    STRING_CHARACTER_ELEMENT,

    // Element with given index is stored in fast backing store.
    FAST_ELEMENT,

    // Element with given index is stored in slow backing store.
    DICTIONARY_ELEMENT
  };

  LocalElementType HasLocalElement(uint32_t index);

  bool HasElementWithInterceptor(JSReceiver* receiver, uint32_t index);
  bool HasElementPostInterceptor(JSReceiver* receiver, uint32_t index);

  MUST_USE_RESULT MaybeObject* SetFastElement(uint32_t index,
                                              Object* value,
                                              StrictModeFlag strict_mode,
                                              bool check_prototype);
  MUST_USE_RESULT MaybeObject* SetDictionaryElement(uint32_t index,
                                                    Object* value,
                                                    StrictModeFlag strict_mode,
                                                    bool check_prototype);

  MUST_USE_RESULT MaybeObject* SetFastDoubleElement(
      uint32_t index,
      Object* value,
      StrictModeFlag strict_mode,
      bool check_prototype = true);

  // Set the index'th array element.
  // A Failure object is returned if GC is needed.
  MUST_USE_RESULT MaybeObject* SetElement(uint32_t index,
                                          Object* value,
                                          StrictModeFlag strict_mode,
                                          bool check_prototype);

  // Returns the index'th element.
  // The undefined object if index is out of bounds.
  MaybeObject* GetElementWithReceiver(Object* receiver, uint32_t index);
  MaybeObject* GetElementWithInterceptor(Object* receiver, uint32_t index);

  // Get external element value at index if there is one and undefined
  // otherwise. Can return a failure if allocation of a heap number
  // failed.
  MaybeObject* GetExternalElement(uint32_t index);

  // Replace the elements' backing store with fast elements of the given
  // capacity.  Update the length for JSArrays.  Returns the new backing
  // store.
  MUST_USE_RESULT MaybeObject* SetFastElementsCapacityAndLength(int capacity,
                                                                int length);
  MUST_USE_RESULT MaybeObject* SetFastDoubleElementsCapacityAndLength(
      int capacity,
      int length);
  MUST_USE_RESULT MaybeObject* SetSlowElements(Object* length);

  // Lookup interceptors are used for handling properties controlled by host
  // objects.
  inline bool HasNamedInterceptor();
  inline bool HasIndexedInterceptor();

  // Support functions for v8 api (needed for correct interceptor behavior).
  bool HasRealNamedProperty(String* key);
  bool HasRealElementProperty(uint32_t index);
  bool HasRealNamedCallbackProperty(String* key);

  // Initializes the array to a certain length
  MUST_USE_RESULT MaybeObject* SetElementsLength(Object* length);

  // Get the header size for a JSObject.  Used to compute the index of
  // internal fields as well as the number of internal fields.
  inline int GetHeaderSize();

  inline int GetInternalFieldCount();
  inline int GetInternalFieldOffset(int index);
  inline Object* GetInternalField(int index);
  inline void SetInternalField(int index, Object* value);

  // Lookup a property.  If found, the result is valid and has
  // detailed information.
  void LocalLookup(String* name, LookupResult* result);

  // The following lookup functions skip interceptors.
  void LocalLookupRealNamedProperty(String* name, LookupResult* result);
  void LookupRealNamedProperty(String* name, LookupResult* result);
  void LookupRealNamedPropertyInPrototypes(String* name, LookupResult* result);
  void LookupCallbackSetterInPrototypes(String* name, LookupResult* result);
  MUST_USE_RESULT MaybeObject* SetElementWithCallbackSetterInPrototypes(
      uint32_t index, Object* value, bool* found, StrictModeFlag strict_mode);
  void LookupCallback(String* name, LookupResult* result);

  // Returns the number of properties on this object filtering out properties
  // with the specified attributes (ignoring interceptors).
  int NumberOfLocalProperties(PropertyAttributes filter);
  // Returns the number of enumerable properties (ignoring interceptors).
  int NumberOfEnumProperties();
  // Fill in details for properties into storage starting at the specified
  // index.
  void GetLocalPropertyNames(FixedArray* storage, int index);

  // Returns the number of properties on this object filtering out properties
  // with the specified attributes (ignoring interceptors).
  int NumberOfLocalElements(PropertyAttributes filter);
  // Returns the number of enumerable elements (ignoring interceptors).
  int NumberOfEnumElements();
  // Returns the number of elements on this object filtering out elements
  // with the specified attributes (ignoring interceptors).
  int GetLocalElementKeys(FixedArray* storage, PropertyAttributes filter);
  // Count and fill in the enumerable elements into storage.
  // (storage->length() == NumberOfEnumElements()).
  // If storage is NULL, will count the elements without adding
  // them to any storage.
  // Returns the number of enumerable elements.
  int GetEnumElementKeys(FixedArray* storage);

  // Add a property to a fast-case object using a map transition to
  // new_map.
  MUST_USE_RESULT MaybeObject* AddFastPropertyUsingMap(Map* new_map,
                                                       String* name,
                                                       Object* value);

  // Add a constant function property to a fast-case object.
  // This leaves a CONSTANT_TRANSITION in the old map, and
  // if it is called on a second object with this map, a
  // normal property is added instead, with a map transition.
  // This avoids the creation of many maps with the same constant
  // function, all orphaned.
  MUST_USE_RESULT MaybeObject* AddConstantFunctionProperty(
      String* name,
      JSFunction* function,
      PropertyAttributes attributes);

  MUST_USE_RESULT MaybeObject* ReplaceSlowProperty(
      String* name,
      Object* value,
      PropertyAttributes attributes);

  // Converts a descriptor of any other type to a real field,
  // backed by the properties array.  Descriptors of visible
  // types, such as CONSTANT_FUNCTION, keep their enumeration order.
  // Converts the descriptor on the original object's map to a
  // map transition, and the the new field is on the object's new map.
  MUST_USE_RESULT MaybeObject* ConvertDescriptorToFieldAndMapTransition(
      String* name,
      Object* new_value,
      PropertyAttributes attributes);

  // Converts a descriptor of any other type to a real field,
  // backed by the properties array.  Descriptors of visible
  // types, such as CONSTANT_FUNCTION, keep their enumeration order.
  MUST_USE_RESULT MaybeObject* ConvertDescriptorToField(
      String* name,
      Object* new_value,
      PropertyAttributes attributes);

  // Add a property to a fast-case object.
  MUST_USE_RESULT MaybeObject* AddFastProperty(String* name,
                                               Object* value,
                                               PropertyAttributes attributes);

  // Add a property to a slow-case object.
  MUST_USE_RESULT MaybeObject* AddSlowProperty(String* name,
                                               Object* value,
                                               PropertyAttributes attributes);

  // Add a property to an object.
  MUST_USE_RESULT MaybeObject* AddProperty(String* name,
                                           Object* value,
                                           PropertyAttributes attributes,
                                           StrictModeFlag strict_mode);

  // Convert the object to use the canonical dictionary
  // representation. If the object is expected to have additional properties
  // added this number can be indicated to have the backing store allocated to
  // an initial capacity for holding these properties.
  MUST_USE_RESULT MaybeObject* NormalizeProperties(
      PropertyNormalizationMode mode,
      int expected_additional_properties);

  // Convert and update the elements backing store to be a NumberDictionary
  // dictionary.  Returns the backing after conversion.
  MUST_USE_RESULT MaybeObject* NormalizeElements();

  MUST_USE_RESULT MaybeObject* UpdateMapCodeCache(String* name, Code* code);

  // Transform slow named properties to fast variants.
  // Returns failure if allocation failed.
  MUST_USE_RESULT MaybeObject* TransformToFastProperties(
      int unused_property_fields);

  // Access fast-case object properties at index.
  inline Object* FastPropertyAt(int index);
  inline Object* FastPropertyAtPut(int index, Object* value);

  // Access to in object properties.
  inline int GetInObjectPropertyOffset(int index);
  inline Object* InObjectPropertyAt(int index);
  inline Object* InObjectPropertyAtPut(int index,
                                       Object* value,
                                       WriteBarrierMode mode
                                       = UPDATE_WRITE_BARRIER);

  // initializes the body after properties slot, properties slot is
  // initialized by set_properties
  // Note: this call does not update write barrier, it is caller's
  // reponsibility to ensure that *v* can be collected without WB here.
  inline void InitializeBody(int object_size, Object* value);

  // Check whether this object references another object
  bool ReferencesObject(Object* obj);

  // Casting.
  static inline JSObject* cast(Object* obj);

  // Disalow further properties to be added to the object.
  MUST_USE_RESULT MaybeObject* PreventExtensions();


  // Dispatched behavior.
  void JSObjectShortPrint(StringStream* accumulator);
#ifdef OBJECT_PRINT
  inline void JSObjectPrint() {
    JSObjectPrint(stdout);
  }
  void JSObjectPrint(FILE* out);
#endif
#ifdef DEBUG
  void JSObjectVerify();
#endif
#ifdef OBJECT_PRINT
  inline void PrintProperties() {
    PrintProperties(stdout);
  }
  void PrintProperties(FILE* out);

  inline void PrintElements() {
    PrintElements(stdout);
  }
  void PrintElements(FILE* out);
#endif

#ifdef DEBUG
  // Structure for collecting spill information about JSObjects.
  class SpillInformation {
   public:
    void Clear();
    void Print();
    int number_of_objects_;
    int number_of_objects_with_fast_properties_;
    int number_of_objects_with_fast_elements_;
    int number_of_fast_used_fields_;
    int number_of_fast_unused_fields_;
    int number_of_slow_used_properties_;
    int number_of_slow_unused_properties_;
    int number_of_fast_used_elements_;
    int number_of_fast_unused_elements_;
    int number_of_slow_used_elements_;
    int number_of_slow_unused_elements_;
  };

  void IncrementSpillStatistics(SpillInformation* info);
#endif
  Object* SlowReverseLookup(Object* value);

  // Maximal number of fast properties for the JSObject. Used to
  // restrict the number of map transitions to avoid an explosion in
  // the number of maps for objects used as dictionaries.
  inline int MaxFastProperties();

  // Maximal number of elements (numbered 0 .. kMaxElementCount - 1).
  // Also maximal value of JSArray's length property.
  static const uint32_t kMaxElementCount = 0xffffffffu;

  // Constants for heuristics controlling conversion of fast elements
  // to slow elements.

  // Maximal gap that can be introduced by adding an element beyond
  // the current elements length.
  static const uint32_t kMaxGap = 1024;

  // Maximal length of fast elements array that won't be checked for
  // being dense enough on expansion.
  static const int kMaxUncheckedFastElementsLength = 5000;

  // Same as above but for old arrays. This limit is more strict. We
  // don't want to be wasteful with long lived objects.
  static const int kMaxUncheckedOldFastElementsLength = 500;

  static const int kInitialMaxFastElementArray = 100000;
  static const int kMaxFastProperties = 12;
  static const int kMaxInstanceSize = 255 * kPointerSize;
  // When extending the backing storage for property values, we increase
  // its size by more than the 1 entry necessary, so sequentially adding fields
  // to the same object requires fewer allocations and copies.
  static const int kFieldsAdded = 3;

  // Layout description.
  static const int kPropertiesOffset = HeapObject::kHeaderSize;
  static const int kElementsOffset = kPropertiesOffset + kPointerSize;
  static const int kHeaderSize = kElementsOffset + kPointerSize;

  STATIC_CHECK(kHeaderSize == Internals::kJSObjectHeaderSize);

  class BodyDescriptor : public FlexibleBodyDescriptor<kPropertiesOffset> {
   public:
    static inline int SizeOf(Map* map, HeapObject* object);
  };

 private:
  MUST_USE_RESULT MaybeObject* GetElementWithCallback(Object* receiver,
                                                      Object* structure,
                                                      uint32_t index,
                                                      Object* holder);
  MaybeObject* SetElementWithCallback(Object* structure,
                                      uint32_t index,
                                      Object* value,
                                      JSObject* holder,
                                      StrictModeFlag strict_mode);
  MUST_USE_RESULT MaybeObject* SetElementWithInterceptor(
      uint32_t index,
      Object* value,
      StrictModeFlag strict_mode,
      bool check_prototype);
  MUST_USE_RESULT MaybeObject* SetElementWithoutInterceptor(
      uint32_t index,
      Object* value,
      StrictModeFlag strict_mode,
      bool check_prototype);

  MaybeObject* GetElementPostInterceptor(Object* receiver, uint32_t index);

  MUST_USE_RESULT MaybeObject* DeletePropertyPostInterceptor(String* name,
                                                             DeleteMode mode);
  MUST_USE_RESULT MaybeObject* DeletePropertyWithInterceptor(String* name);

  MUST_USE_RESULT MaybeObject* DeleteElementPostInterceptor(uint32_t index,
                                                            DeleteMode mode);
  MUST_USE_RESULT MaybeObject* DeleteElementWithInterceptor(uint32_t index);

  MUST_USE_RESULT MaybeObject* DeleteFastElement(uint32_t index);
  MUST_USE_RESULT MaybeObject* DeleteDictionaryElement(uint32_t index,
                                                       DeleteMode mode);

  bool ReferencesObjectFromElements(FixedArray* elements,
                                    ElementsKind kind,
                                    Object* object);
  bool HasElementInElements(FixedArray* elements,
                            ElementsKind kind,
                            uint32_t index);

  // Returns true if most of the elements backing storage is used.
  bool HasDenseElements();

  // Gets the current elements capacity and the number of used elements.
  void GetElementsCapacityAndUsage(int* capacity, int* used);

  bool CanSetCallback(String* name);
  MUST_USE_RESULT MaybeObject* SetElementCallback(
      uint32_t index,
      Object* structure,
      PropertyAttributes attributes);
  MUST_USE_RESULT MaybeObject* SetPropertyCallback(
      String* name,
      Object* structure,
      PropertyAttributes attributes);
  MUST_USE_RESULT MaybeObject* DefineGetterSetter(
      String* name,
      PropertyAttributes attributes);

  void LookupInDescriptor(String* name, LookupResult* result);

  DISALLOW_IMPLICIT_CONSTRUCTORS(JSObject);
};


// Common superclass for FixedArrays that allow implementations to share
// common accessors and some code paths.
class FixedArrayBase: public HeapObject {
 public:
  // [length]: length of the array.
  inline int length();
  inline void set_length(int value);

  inline static FixedArrayBase* cast(Object* object);

  // Layout description.
  // Length is smi tagged when it is stored.
  static const int kLengthOffset = HeapObject::kHeaderSize;
  static const int kHeaderSize = kLengthOffset + kPointerSize;
};

class FixedDoubleArray;

// FixedArray describes fixed-sized arrays with element type Object*.
class FixedArray: public FixedArrayBase {
 public:
  // Setter and getter for elements.
  inline Object* get(int index);
  // Setter that uses write barrier.
  inline void set(int index, Object* value);

  // Setter that doesn't need write barrier).
  inline void set(int index, Smi* value);
  // Setter with explicit barrier mode.
  inline void set(int index, Object* value, WriteBarrierMode mode);

  // Setters for frequently used oddballs located in old space.
  inline void set_undefined(int index);
  // TODO(isolates): duplicate.
  inline void set_undefined(Heap* heap, int index);
  inline void set_null(int index);
  // TODO(isolates): duplicate.
  inline void set_null(Heap* heap, int index);
  inline void set_the_hole(int index);

  // Setters with less debug checks for the GC to use.
  inline void set_unchecked(int index, Smi* value);
  inline void set_null_unchecked(Heap* heap, int index);
  inline void set_unchecked(Heap* heap, int index, Object* value,
                            WriteBarrierMode mode);

  // Gives access to raw memory which stores the array's data.
  inline Object** data_start();

  // Copy operations.
  MUST_USE_RESULT inline MaybeObject* Copy();
  MUST_USE_RESULT MaybeObject* CopySize(int new_length);

  // Add the elements of a JSArray to this FixedArray.
  MUST_USE_RESULT MaybeObject* AddKeysFromJSArray(JSArray* array);

  // Compute the union of this and other.
  MUST_USE_RESULT MaybeObject* UnionOfKeys(FixedArray* other);

  // Compute the union of this and other.
  MUST_USE_RESULT MaybeObject* UnionOfDoubleKeys(
      FixedDoubleArray* other);

  // Copy a sub array from the receiver to dest.
  void CopyTo(int pos, FixedArray* dest, int dest_pos, int len);

  // Garbage collection support.
  static int SizeFor(int length) { return kHeaderSize + length * kPointerSize; }

  // Code Generation support.
  static int OffsetOfElementAt(int index) { return SizeFor(index); }

  // Casting.
  static inline FixedArray* cast(Object* obj);

  // Maximal allowed size, in bytes, of a single FixedArray.
  // Prevents overflowing size computations, as well as extreme memory
  // consumption.
  static const int kMaxSize = 512 * MB;
  // Maximally allowed length of a FixedArray.
  static const int kMaxLength = (kMaxSize - kHeaderSize) / kPointerSize;

  // Dispatched behavior.
#ifdef OBJECT_PRINT
  inline void FixedArrayPrint() {
    FixedArrayPrint(stdout);
  }
  void FixedArrayPrint(FILE* out);
#endif
#ifdef DEBUG
  void FixedArrayVerify();
  // Checks if two FixedArrays have identical contents.
  bool IsEqualTo(FixedArray* other);
#endif

  // Swap two elements in a pair of arrays.  If this array and the
  // numbers array are the same object, the elements are only swapped
  // once.
  void SwapPairs(FixedArray* numbers, int i, int j);

  // Sort prefix of this array and the numbers array as pairs wrt. the
  // numbers.  If the numbers array and the this array are the same
  // object, the prefix of this array is sorted.
  void SortPairs(FixedArray* numbers, uint32_t len);

  class BodyDescriptor : public FlexibleBodyDescriptor<kHeaderSize> {
   public:
    static inline int SizeOf(Map* map, HeapObject* object) {
      return SizeFor(reinterpret_cast<FixedArray*>(object)->length());
    }
  };

 protected:
  // Set operation on FixedArray without using write barriers. Can
  // only be used for storing old space objects or smis.
  static inline void fast_set(FixedArray* array, int index, Object* value);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(FixedArray);
};


// FixedDoubleArray describes fixed-sized arrays with element type double.
class FixedDoubleArray: public FixedArrayBase {
 public:
  inline void Initialize(FixedArray* from);
  inline void Initialize(FixedDoubleArray* from);
  inline void Initialize(NumberDictionary* from);

  // Setter and getter for elements.
  inline double get(int index);
  inline void set(int index, double value);
  inline void set_the_hole(int index);

  // Checking for the hole.
  inline bool is_the_hole(int index);

  // Garbage collection support.
  inline static int SizeFor(int length) {
    return kHeaderSize + length * kDoubleSize;
  }

  // Code Generation support.
  static int OffsetOfElementAt(int index) { return SizeFor(index); }

  inline static bool is_the_hole_nan(double value);
  inline static double hole_nan_as_double();
  inline static double canonical_not_the_hole_nan_as_double();

  // Casting.
  static inline FixedDoubleArray* cast(Object* obj);

  // Maximal allowed size, in bytes, of a single FixedDoubleArray.
  // Prevents overflowing size computations, as well as extreme memory
  // consumption.
  static const int kMaxSize = 512 * MB;
  // Maximally allowed length of a FixedArray.
  static const int kMaxLength = (kMaxSize - kHeaderSize) / kDoubleSize;

  // Dispatched behavior.
#ifdef OBJECT_PRINT
  inline void FixedDoubleArrayPrint() {
    FixedDoubleArrayPrint(stdout);
  }
  void FixedDoubleArrayPrint(FILE* out);
#endif

#ifdef DEBUG
  void FixedDoubleArrayVerify();
#endif

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(FixedDoubleArray);
};


// DescriptorArrays are fixed arrays used to hold instance descriptors.
// The format of the these objects is:
// TODO(1399): It should be possible to make room for bit_field3 in the map
//             without overloading the instance descriptors field in the map
//             (and storing it in the DescriptorArray when the map has one).
//   [0]: storage for bit_field3 for Map owning this object (Smi)
//   [1]: point to a fixed array with (value, detail) pairs.
//   [2]: next enumeration index (Smi), or pointer to small fixed array:
//          [0]: next enumeration index (Smi)
//          [1]: pointer to fixed array with enum cache
//   [3]: first key
//   [length() - 1]: last key
//
class DescriptorArray: public FixedArray {
 public:
  // Returns true for both shared empty_descriptor_array and for smis, which the
  // map uses to encode additional bit fields when the descriptor array is not
  // yet used.
  inline bool IsEmpty();

  // Returns the number of descriptors in the array.
  int number_of_descriptors() {
    ASSERT(length() > kFirstIndex || IsEmpty());
    int len = length();
    return len <= kFirstIndex ? 0 : len - kFirstIndex;
  }

  int NextEnumerationIndex() {
    if (IsEmpty()) return PropertyDetails::kInitialIndex;
    Object* obj = get(kEnumerationIndexIndex);
    if (obj->IsSmi()) {
      return Smi::cast(obj)->value();
    } else {
      Object* index = FixedArray::cast(obj)->get(kEnumCacheBridgeEnumIndex);
      return Smi::cast(index)->value();
    }
  }

  // Set next enumeration index and flush any enum cache.
  void SetNextEnumerationIndex(int value) {
    if (!IsEmpty()) {
      fast_set(this, kEnumerationIndexIndex, Smi::FromInt(value));
    }
  }
  bool HasEnumCache() {
    return !IsEmpty() && !get(kEnumerationIndexIndex)->IsSmi();
  }

  Object* GetEnumCache() {
    ASSERT(HasEnumCache());
    FixedArray* bridge = FixedArray::cast(get(kEnumerationIndexIndex));
    return bridge->get(kEnumCacheBridgeCacheIndex);
  }

  // TODO(1399): It should be possible to make room for bit_field3 in the map
  //             without overloading the instance descriptors field in the map
  //             (and storing it in the DescriptorArray when the map has one).
  inline int bit_field3_storage();
  inline void set_bit_field3_storage(int value);

  // Initialize or change the enum cache,
  // using the supplied storage for the small "bridge".
  void SetEnumCache(FixedArray* bridge_storage, FixedArray* new_cache);

  // Accessors for fetching instance descriptor at descriptor number.
  inline String* GetKey(int descriptor_number);
  inline Object* GetValue(int descriptor_number);
  inline Smi* GetDetails(int descriptor_number);
  inline PropertyType GetType(int descriptor_number);
  inline int GetFieldIndex(int descriptor_number);
  inline JSFunction* GetConstantFunction(int descriptor_number);
  inline Object* GetCallbacksObject(int descriptor_number);
  inline AccessorDescriptor* GetCallbacks(int descriptor_number);
  inline bool IsProperty(int descriptor_number);
  inline bool IsTransition(int descriptor_number);
  inline bool IsNullDescriptor(int descriptor_number);
  inline bool IsDontEnum(int descriptor_number);

  // Accessor for complete descriptor.
  inline void Get(int descriptor_number, Descriptor* desc);
  inline void Set(int descriptor_number, Descriptor* desc);

  // Transfer complete descriptor from another descriptor array to
  // this one.
  inline void CopyFrom(int index, DescriptorArray* src, int src_index);

  // Copy the descriptor array, insert a new descriptor and optionally
  // remove map transitions.  If the descriptor is already present, it is
  // replaced.  If a replaced descriptor is a real property (not a transition
  // or null), its enumeration index is kept as is.
  // If adding a real property, map transitions must be removed.  If adding
  // a transition, they must not be removed.  All null descriptors are removed.
  MUST_USE_RESULT MaybeObject* CopyInsert(Descriptor* descriptor,
                                          TransitionFlag transition_flag);

  // Remove all transitions.  Return  a copy of the array with all transitions
  // removed, or a Failure object if the new array could not be allocated.
  MUST_USE_RESULT MaybeObject* RemoveTransitions();

  // Sort the instance descriptors by the hash codes of their keys.
  // Does not check for duplicates.
  void SortUnchecked();

  // Sort the instance descriptors by the hash codes of their keys.
  // Checks the result for duplicates.
  void Sort();

  // Search the instance descriptors for given name.
  inline int Search(String* name);

  // As the above, but uses DescriptorLookupCache and updates it when
  // necessary.
  inline int SearchWithCache(String* name);

  // Tells whether the name is present int the array.
  bool Contains(String* name) { return kNotFound != Search(name); }

  // Perform a binary search in the instance descriptors represented
  // by this fixed array.  low and high are descriptor indices.  If there
  // are three instance descriptors in this array it should be called
  // with low=0 and high=2.
  int BinarySearch(String* name, int low, int high);

  // Perform a linear search in the instance descriptors represented
  // by this fixed array.  len is the number of descriptor indices that are
  // valid.  Does not require the descriptors to be sorted.
  int LinearSearch(String* name, int len);

  // Allocates a DescriptorArray, but returns the singleton
  // empty descriptor array object if number_of_descriptors is 0.
  MUST_USE_RESULT static MaybeObject* Allocate(int number_of_descriptors);

  // Casting.
  static inline DescriptorArray* cast(Object* obj);

  // Constant for denoting key was not found.
  static const int kNotFound = -1;

  static const int kBitField3StorageIndex = 0;
  static const int kContentArrayIndex = 1;
  static const int kEnumerationIndexIndex = 2;
  static const int kFirstIndex = 3;

  // The length of the "bridge" to the enum cache.
  static const int kEnumCacheBridgeLength = 2;
  static const int kEnumCacheBridgeEnumIndex = 0;
  static const int kEnumCacheBridgeCacheIndex = 1;

  // Layout description.
  static const int kBitField3StorageOffset = FixedArray::kHeaderSize;
  static const int kContentArrayOffset = kBitField3StorageOffset + kPointerSize;
  static const int kEnumerationIndexOffset = kContentArrayOffset + kPointerSize;
  static const int kFirstOffset = kEnumerationIndexOffset + kPointerSize;

  // Layout description for the bridge array.
  static const int kEnumCacheBridgeEnumOffset = FixedArray::kHeaderSize;
  static const int kEnumCacheBridgeCacheOffset =
    kEnumCacheBridgeEnumOffset + kPointerSize;

#ifdef OBJECT_PRINT
  // Print all the descriptors.
  inline void PrintDescriptors() {
    PrintDescriptors(stdout);
  }
  void PrintDescriptors(FILE* out);
#endif

#ifdef DEBUG
  // Is the descriptor array sorted and without duplicates?
  bool IsSortedNoDuplicates();

  // Are two DescriptorArrays equal?
  bool IsEqualTo(DescriptorArray* other);
#endif

  // The maximum number of descriptors we want in a descriptor array (should
  // fit in a page).
  static const int kMaxNumberOfDescriptors = 1024 + 512;

 private:
  // Conversion from descriptor number to array indices.
  static int ToKeyIndex(int descriptor_number) {
    return descriptor_number+kFirstIndex;
  }

  static int ToDetailsIndex(int descriptor_number) {
    return (descriptor_number << 1) + 1;
  }

  static int ToValueIndex(int descriptor_number) {
    return descriptor_number << 1;
  }

  bool is_null_descriptor(int descriptor_number) {
    return PropertyDetails(GetDetails(descriptor_number)).type() ==
        NULL_DESCRIPTOR;
  }
  // Swap operation on FixedArray without using write barriers.
  static inline void fast_swap(FixedArray* array, int first, int second);

  // Swap descriptor first and second.
  inline void Swap(int first, int second);

  FixedArray* GetContentArray() {
    return FixedArray::cast(get(kContentArrayIndex));
  }
  DISALLOW_IMPLICIT_CONSTRUCTORS(DescriptorArray);
};


// HashTable is a subclass of FixedArray that implements a hash table
// that uses open addressing and quadratic probing.
//
// In order for the quadratic probing to work, elements that have not
// yet been used and elements that have been deleted are
// distinguished.  Probing continues when deleted elements are
// encountered and stops when unused elements are encountered.
//
// - Elements with key == undefined have not been used yet.
// - Elements with key == null have been deleted.
//
// The hash table class is parameterized with a Shape and a Key.
// Shape must be a class with the following interface:
//   class ExampleShape {
//    public:
//      // Tells whether key matches other.
//     static bool IsMatch(Key key, Object* other);
//     // Returns the hash value for key.
//     static uint32_t Hash(Key key);
//     // Returns the hash value for object.
//     static uint32_t HashForObject(Key key, Object* object);
//     // Convert key to an object.
//     static inline Object* AsObject(Key key);
//     // The prefix size indicates number of elements in the beginning
//     // of the backing storage.
//     static const int kPrefixSize = ..;
//     // The Element size indicates number of elements per entry.
//     static const int kEntrySize = ..;
//   };
// The prefix size indicates an amount of memory in the
// beginning of the backing storage that can be used for non-element
// information by subclasses.

template<typename Shape, typename Key>
class HashTable: public FixedArray {
 public:
  // Returns the number of elements in the hash table.
  int NumberOfElements() {
    return Smi::cast(get(kNumberOfElementsIndex))->value();
  }

  // Returns the number of deleted elements in the hash table.
  int NumberOfDeletedElements() {
    return Smi::cast(get(kNumberOfDeletedElementsIndex))->value();
  }

  // Returns the capacity of the hash table.
  int Capacity() {
    return Smi::cast(get(kCapacityIndex))->value();
  }

  // ElementAdded should be called whenever an element is added to a
  // hash table.
  void ElementAdded() { SetNumberOfElements(NumberOfElements() + 1); }

  // ElementRemoved should be called whenever an element is removed from
  // a hash table.
  void ElementRemoved() {
    SetNumberOfElements(NumberOfElements() - 1);
    SetNumberOfDeletedElements(NumberOfDeletedElements() + 1);
  }
  void ElementsRemoved(int n) {
    SetNumberOfElements(NumberOfElements() - n);
    SetNumberOfDeletedElements(NumberOfDeletedElements() + n);
  }

  // Returns a new HashTable object. Might return Failure.
  MUST_USE_RESULT static MaybeObject* Allocate(
      int at_least_space_for,
      PretenureFlag pretenure = NOT_TENURED);

  // Computes the required capacity for a table holding the given
  // number of elements. May be more than HashTable::kMaxCapacity.
  static int ComputeCapacity(int at_least_space_for);

  // Returns the key at entry.
  Object* KeyAt(int entry) { return get(EntryToIndex(entry)); }

  // Tells whether k is a real key.  Null and undefined are not allowed
  // as keys and can be used to indicate missing or deleted elements.
  bool IsKey(Object* k) {
    return !k->IsNull() && !k->IsUndefined();
  }

  // Garbage collection support.
  void IteratePrefix(ObjectVisitor* visitor);
  void IterateElements(ObjectVisitor* visitor);

  // Casting.
  static inline HashTable* cast(Object* obj);

  // Compute the probe offset (quadratic probing).
  INLINE(static uint32_t GetProbeOffset(uint32_t n)) {
    return (n + n * n) >> 1;
  }

  static const int kNumberOfElementsIndex = 0;
  static const int kNumberOfDeletedElementsIndex = 1;
  static const int kCapacityIndex = 2;
  static const int kPrefixStartIndex = 3;
  static const int kElementsStartIndex =
      kPrefixStartIndex + Shape::kPrefixSize;
  static const int kEntrySize = Shape::kEntrySize;
  static const int kElementsStartOffset =
      kHeaderSize + kElementsStartIndex * kPointerSize;
  static const int kCapacityOffset =
      kHeaderSize + kCapacityIndex * kPointerSize;

  // Constant used for denoting a absent entry.
  static const int kNotFound = -1;

  // Maximal capacity of HashTable. Based on maximal length of underlying
  // FixedArray. Staying below kMaxCapacity also ensures that EntryToIndex
  // cannot overflow.
  static const int kMaxCapacity =
      (FixedArray::kMaxLength - kElementsStartOffset) / kEntrySize;

  // Find entry for key otherwise return kNotFound.
  inline int FindEntry(Key key);
  int FindEntry(Isolate* isolate, Key key);

 protected:
  // Find the entry at which to insert element with the given key that
  // has the given hash value.
  uint32_t FindInsertionEntry(uint32_t hash);

  // Returns the index for an entry (of the key)
  static inline int EntryToIndex(int entry) {
    return (entry * kEntrySize) + kElementsStartIndex;
  }

  // Update the number of elements in the hash table.
  void SetNumberOfElements(int nof) {
    fast_set(this, kNumberOfElementsIndex, Smi::FromInt(nof));
  }

  // Update the number of deleted elements in the hash table.
  void SetNumberOfDeletedElements(int nod) {
    fast_set(this, kNumberOfDeletedElementsIndex, Smi::FromInt(nod));
  }

  // Sets the capacity of the hash table.
  void SetCapacity(int capacity) {
    // To scale a computed hash code to fit within the hash table, we
    // use bit-wise AND with a mask, so the capacity must be positive
    // and non-zero.
    ASSERT(capacity > 0);
    ASSERT(capacity <= kMaxCapacity);
    fast_set(this, kCapacityIndex, Smi::FromInt(capacity));
  }


  // Returns probe entry.
  static uint32_t GetProbe(uint32_t hash, uint32_t number, uint32_t size) {
    ASSERT(IsPowerOf2(size));
    return (hash + GetProbeOffset(number)) & (size - 1);
  }

  static uint32_t FirstProbe(uint32_t hash, uint32_t size) {
    return hash & (size - 1);
  }

  static uint32_t NextProbe(uint32_t last, uint32_t number, uint32_t size) {
    return (last + number) & (size - 1);
  }

  // Rehashes this hash-table into the new table.
  MUST_USE_RESULT MaybeObject* Rehash(HashTable* new_table, Key key);

  // Attempt to shrink hash table after removal of key.
  MUST_USE_RESULT MaybeObject* Shrink(Key key);

  // Ensure enough space for n additional elements.
  MUST_USE_RESULT MaybeObject* EnsureCapacity(int n, Key key);
};



// HashTableKey is an abstract superclass for virtual key behavior.
class HashTableKey {
 public:
  // Returns whether the other object matches this key.
  virtual bool IsMatch(Object* other) = 0;
  // Returns the hash value for this key.
  virtual uint32_t Hash() = 0;
  // Returns the hash value for object.
  virtual uint32_t HashForObject(Object* key) = 0;
  // Returns the key object for storing into the hash table.
  // If allocations fails a failure object is returned.
  MUST_USE_RESULT virtual MaybeObject* AsObject() = 0;
  // Required.
  virtual ~HashTableKey() {}
};

class SymbolTableShape {
 public:
  static inline bool IsMatch(HashTableKey* key, Object* value) {
    return key->IsMatch(value);
  }
  static inline uint32_t Hash(HashTableKey* key) {
    return key->Hash();
  }
  static inline uint32_t HashForObject(HashTableKey* key, Object* object) {
    return key->HashForObject(object);
  }
  MUST_USE_RESULT static inline MaybeObject* AsObject(HashTableKey* key) {
    return key->AsObject();
  }

  static const int kPrefixSize = 0;
  static const int kEntrySize = 1;
};

class SeqAsciiString;

// SymbolTable.
//
// No special elements in the prefix and the element size is 1
// because only the symbol itself (the key) needs to be stored.
class SymbolTable: public HashTable<SymbolTableShape, HashTableKey*> {
 public:
  // Find symbol in the symbol table.  If it is not there yet, it is
  // added.  The return value is the symbol table which might have
  // been enlarged.  If the return value is not a failure, the symbol
  // pointer *s is set to the symbol found.
  MUST_USE_RESULT MaybeObject* LookupSymbol(Vector<const char> str, Object** s);
  MUST_USE_RESULT MaybeObject* LookupAsciiSymbol(Vector<const char> str,
                                                 Object** s);
  MUST_USE_RESULT MaybeObject* LookupSubStringAsciiSymbol(
      Handle<SeqAsciiString> str,
      int from,
      int length,
      Object** s);
  MUST_USE_RESULT MaybeObject* LookupTwoByteSymbol(Vector<const uc16> str,
                                                   Object** s);
  MUST_USE_RESULT MaybeObject* LookupString(String* key, Object** s);

  // Looks up a symbol that is equal to the given string and returns
  // true if it is found, assigning the symbol to the given output
  // parameter.
  bool LookupSymbolIfExists(String* str, String** symbol);
  bool LookupTwoCharsSymbolIfExists(uint32_t c1, uint32_t c2, String** symbol);

  // Casting.
  static inline SymbolTable* cast(Object* obj);

 private:
  MUST_USE_RESULT MaybeObject* LookupKey(HashTableKey* key, Object** s);

  DISALLOW_IMPLICIT_CONSTRUCTORS(SymbolTable);
};


class MapCacheShape {
 public:
  static inline bool IsMatch(HashTableKey* key, Object* value) {
    return key->IsMatch(value);
  }
  static inline uint32_t Hash(HashTableKey* key) {
    return key->Hash();
  }

  static inline uint32_t HashForObject(HashTableKey* key, Object* object) {
    return key->HashForObject(object);
  }

  MUST_USE_RESULT static inline MaybeObject* AsObject(HashTableKey* key) {
    return key->AsObject();
  }

  static const int kPrefixSize = 0;
  static const int kEntrySize = 2;
};


// MapCache.
//
// Maps keys that are a fixed array of symbols to a map.
// Used for canonicalize maps for object literals.
class MapCache: public HashTable<MapCacheShape, HashTableKey*> {
 public:
  // Find cached value for a string key, otherwise return null.
  Object* Lookup(FixedArray* key);
  MUST_USE_RESULT MaybeObject* Put(FixedArray* key, Map* value);
  static inline MapCache* cast(Object* obj);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(MapCache);
};


template <typename Shape, typename Key>
class Dictionary: public HashTable<Shape, Key> {
 public:
  static inline Dictionary<Shape, Key>* cast(Object* obj) {
    return reinterpret_cast<Dictionary<Shape, Key>*>(obj);
  }

  // Returns the value at entry.
  Object* ValueAt(int entry) {
    return this->get(HashTable<Shape, Key>::EntryToIndex(entry)+1);
  }

  // Set the value for entry.
  // Returns false if the put wasn't performed due to property being read only.
  // Returns true on successful put.
  bool ValueAtPut(int entry, Object* value) {
    // Check that this value can actually be written.
    PropertyDetails details = DetailsAt(entry);
    // If a value has not been initilized we allow writing to it even if
    // it is read only (a declared const that has not been initialized).
    if (details.IsReadOnly() && !ValueAt(entry)->IsTheHole()) {
      return false;
    }
    this->set(HashTable<Shape, Key>::EntryToIndex(entry) + 1, value);
    return true;
  }

  // Returns the property details for the property at entry.
  PropertyDetails DetailsAt(int entry) {
    ASSERT(entry >= 0);  // Not found is -1, which is not caught by get().
    return PropertyDetails(
        Smi::cast(this->get(HashTable<Shape, Key>::EntryToIndex(entry) + 2)));
  }

  // Set the details for entry.
  void DetailsAtPut(int entry, PropertyDetails value) {
    this->set(HashTable<Shape, Key>::EntryToIndex(entry) + 2, value.AsSmi());
  }

  // Sorting support
  void CopyValuesTo(FixedArray* elements);

  // Delete a property from the dictionary.
  Object* DeleteProperty(int entry, JSObject::DeleteMode mode);

  // Attempt to shrink the dictionary after deletion of key.
  MUST_USE_RESULT MaybeObject* Shrink(Key key);

  // Returns the number of elements in the dictionary filtering out properties
  // with the specified attributes.
  int NumberOfElementsFilterAttributes(PropertyAttributes filter);

  // Returns the number of enumerable elements in the dictionary.
  int NumberOfEnumElements();

  enum SortMode { UNSORTED, SORTED };
  // Copies keys to preallocated fixed array.
  void CopyKeysTo(FixedArray* storage,
                  PropertyAttributes filter,
                  SortMode sort_mode);
  // Fill in details for properties into storage.
  void CopyKeysTo(FixedArray* storage, SortMode sort_mode);

  // Accessors for next enumeration index.
  void SetNextEnumerationIndex(int index) {
    this->fast_set(this, kNextEnumerationIndexIndex, Smi::FromInt(index));
  }

  int NextEnumerationIndex() {
    return Smi::cast(FixedArray::get(kNextEnumerationIndexIndex))->value();
  }

  // Returns a new array for dictionary usage. Might return Failure.
  MUST_USE_RESULT static MaybeObject* Allocate(int at_least_space_for);

  // Ensure enough space for n additional elements.
  MUST_USE_RESULT MaybeObject* EnsureCapacity(int n, Key key);

#ifdef OBJECT_PRINT
  inline void Print() {
    Print(stdout);
  }
  void Print(FILE* out);
#endif
  // Returns the key (slow).
  Object* SlowReverseLookup(Object* value);

  // Sets the entry to (key, value) pair.
  inline void SetEntry(int entry,
                       Object* key,
                       Object* value);
  inline void SetEntry(int entry,
                       Object* key,
                       Object* value,
                       PropertyDetails details);

  MUST_USE_RESULT MaybeObject* Add(Key key,
                                   Object* value,
                                   PropertyDetails details);

 protected:
  // Generic at put operation.
  MUST_USE_RESULT MaybeObject* AtPut(Key key, Object* value);

  // Add entry to dictionary.
  MUST_USE_RESULT MaybeObject* AddEntry(Key key,
                                        Object* value,
                                        PropertyDetails details,
                                        uint32_t hash);

  // Generate new enumeration indices to avoid enumeration index overflow.
  MUST_USE_RESULT MaybeObject* GenerateNewEnumerationIndices();
  static const int kMaxNumberKeyIndex =
      HashTable<Shape, Key>::kPrefixStartIndex;
  static const int kNextEnumerationIndexIndex = kMaxNumberKeyIndex + 1;
};


class StringDictionaryShape {
 public:
  static inline bool IsMatch(String* key, Object* other);
  static inline uint32_t Hash(String* key);
  static inline uint32_t HashForObject(String* key, Object* object);
  MUST_USE_RESULT static inline MaybeObject* AsObject(String* key);
  static const int kPrefixSize = 2;
  static const int kEntrySize = 3;
  static const bool kIsEnumerable = true;
};


class StringDictionary: public Dictionary<StringDictionaryShape, String*> {
 public:
  static inline StringDictionary* cast(Object* obj) {
    ASSERT(obj->IsDictionary());
    return reinterpret_cast<StringDictionary*>(obj);
  }

  // Copies enumerable keys to preallocated fixed array.
  void CopyEnumKeysTo(FixedArray* storage, FixedArray* sort_array);

  // For transforming properties of a JSObject.
  MUST_USE_RESULT MaybeObject* TransformPropertiesToFastFor(
      JSObject* obj,
      int unused_property_fields);

  // Find entry for key otherwise return kNotFound. Optimzed version of
  // HashTable::FindEntry.
  int FindEntry(String* key);
};


class NumberDictionaryShape {
 public:
  static inline bool IsMatch(uint32_t key, Object* other);
  static inline uint32_t Hash(uint32_t key);
  static inline uint32_t HashForObject(uint32_t key, Object* object);
  MUST_USE_RESULT static inline MaybeObject* AsObject(uint32_t key);
  static const int kPrefixSize = 2;
  static const int kEntrySize = 3;
  static const bool kIsEnumerable = false;
};


class NumberDictionary: public Dictionary<NumberDictionaryShape, uint32_t> {
 public:
  static NumberDictionary* cast(Object* obj) {
    ASSERT(obj->IsDictionary());
    return reinterpret_cast<NumberDictionary*>(obj);
  }

  // Type specific at put (default NONE attributes is used when adding).
  MUST_USE_RESULT MaybeObject* AtNumberPut(uint32_t key, Object* value);
  MUST_USE_RESULT MaybeObject* AddNumberEntry(uint32_t key,
                                              Object* value,
                                              PropertyDetails details);

  // Set an existing entry or add a new one if needed.
  MUST_USE_RESULT MaybeObject* Set(uint32_t key,
                                   Object* value,
                                   PropertyDetails details);

  void UpdateMaxNumberKey(uint32_t key);

  // If slow elements are required we will never go back to fast-case
  // for the elements kept in this dictionary.  We require slow
  // elements if an element has been added at an index larger than
  // kRequiresSlowElementsLimit or set_requires_slow_elements() has been called
  // when defining a getter or setter with a number key.
  inline bool requires_slow_elements();
  inline void set_requires_slow_elements();

  // Get the value of the max number key that has been added to this
  // dictionary.  max_number_key can only be called if
  // requires_slow_elements returns false.
  inline uint32_t max_number_key();

  // Remove all entries were key is a number and (from <= key && key < to).
  void RemoveNumberEntries(uint32_t from, uint32_t to);

  // Bit masks.
  static const int kRequiresSlowElementsMask = 1;
  static const int kRequiresSlowElementsTagSize = 1;
  static const uint32_t kRequiresSlowElementsLimit = (1 << 29) - 1;
};


// JSFunctionResultCache caches results of some JSFunction invocation.
// It is a fixed array with fixed structure:
//   [0]: factory function
//   [1]: finger index
//   [2]: current cache size
//   [3]: dummy field.
// The rest of array are key/value pairs.
class JSFunctionResultCache: public FixedArray {
 public:
  static const int kFactoryIndex = 0;
  static const int kFingerIndex = kFactoryIndex + 1;
  static const int kCacheSizeIndex = kFingerIndex + 1;
  static const int kDummyIndex = kCacheSizeIndex + 1;
  static const int kEntriesIndex = kDummyIndex + 1;

  static const int kEntrySize = 2;  // key + value

  static const int kFactoryOffset = kHeaderSize;
  static const int kFingerOffset = kFactoryOffset + kPointerSize;
  static const int kCacheSizeOffset = kFingerOffset + kPointerSize;

  inline void MakeZeroSize();
  inline void Clear();

  inline int size();
  inline void set_size(int size);
  inline int finger_index();
  inline void set_finger_index(int finger_index);

  // Casting
  static inline JSFunctionResultCache* cast(Object* obj);

#ifdef DEBUG
  void JSFunctionResultCacheVerify();
#endif
};


// The cache for maps used by normalized (dictionary mode) objects.
// Such maps do not have property descriptors, so a typical program
// needs very limited number of distinct normalized maps.
class NormalizedMapCache: public FixedArray {
 public:
  static const int kEntries = 64;

  MUST_USE_RESULT MaybeObject* Get(JSObject* object,
                                   PropertyNormalizationMode mode);

  void Clear();

  // Casting
  static inline NormalizedMapCache* cast(Object* obj);

#ifdef DEBUG
  void NormalizedMapCacheVerify();
#endif
};


// ByteArray represents fixed sized byte arrays.  Used by the outside world,
// such as PCRE, and also by the memory allocator and garbage collector to
// fill in free blocks in the heap.
class ByteArray: public HeapObject {
 public:
  // [length]: length of the array.
  inline int length();
  inline void set_length(int value);

  // Setter and getter.
  inline byte get(int index);
  inline void set(int index, byte value);

  // Treat contents as an int array.
  inline int get_int(int index);

  static int SizeFor(int length) {
    return OBJECT_POINTER_ALIGN(kHeaderSize + length);
  }
  // We use byte arrays for free blocks in the heap.  Given a desired size in
  // bytes that is a multiple of the word size and big enough to hold a byte
  // array, this function returns the number of elements a byte array should
  // have.
  static int LengthFor(int size_in_bytes) {
    ASSERT(IsAligned(size_in_bytes, kPointerSize));
    ASSERT(size_in_bytes >= kHeaderSize);
    return size_in_bytes - kHeaderSize;
  }

  // Returns data start address.
  inline Address GetDataStartAddress();

  // Returns a pointer to the ByteArray object for a given data start address.
  static inline ByteArray* FromDataStartAddress(Address address);

  // Casting.
  static inline ByteArray* cast(Object* obj);

  // Dispatched behavior.
  inline int ByteArraySize() {
    return SizeFor(this->length());
  }
#ifdef OBJECT_PRINT
  inline void ByteArrayPrint() {
    ByteArrayPrint(stdout);
  }
  void ByteArrayPrint(FILE* out);
#endif
#ifdef DEBUG
  void ByteArrayVerify();
#endif

  // Layout description.
  // Length is smi tagged when it is stored.
  static const int kLengthOffset = HeapObject::kHeaderSize;
  static const int kHeaderSize = kLengthOffset + kPointerSize;

  static const int kAlignedSize = OBJECT_POINTER_ALIGN(kHeaderSize);

  // Maximal memory consumption for a single ByteArray.
  static const int kMaxSize = 512 * MB;
  // Maximal length of a single ByteArray.
  static const int kMaxLength = kMaxSize - kHeaderSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ByteArray);
};


// An ExternalArray represents a fixed-size array of primitive values
// which live outside the JavaScript heap. Its subclasses are used to
// implement the CanvasArray types being defined in the WebGL
// specification. As of this writing the first public draft is not yet
// available, but Khronos members can access the draft at:
//   https://cvs.khronos.org/svn/repos/3dweb/trunk/doc/spec/WebGL-spec.html
//
// The semantics of these arrays differ from CanvasPixelArray.
// Out-of-range values passed to the setter are converted via a C
// cast, not clamping. Out-of-range indices cause exceptions to be
// raised rather than being silently ignored.
class ExternalArray: public HeapObject {
 public:
  // [length]: length of the array.
  inline int length();
  inline void set_length(int value);

  // [external_pointer]: The pointer to the external memory area backing this
  // external array.
  DECL_ACCESSORS(external_pointer, void)  // Pointer to the data store.

  // Casting.
  static inline ExternalArray* cast(Object* obj);

  // Maximal acceptable length for an external array.
  static const int kMaxLength = 0x3fffffff;

  // ExternalArray headers are not quadword aligned.
  static const int kLengthOffset = HeapObject::kHeaderSize;
  static const int kExternalPointerOffset =
      POINTER_SIZE_ALIGN(kLengthOffset + kIntSize);
  static const int kHeaderSize = kExternalPointerOffset + kPointerSize;
  static const int kAlignedSize = OBJECT_POINTER_ALIGN(kHeaderSize);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalArray);
};


// A ExternalPixelArray represents a fixed-size byte array with special
// semantics used for implementing the CanvasPixelArray object. Please see the
// specification at:

// http://www.whatwg.org/specs/web-apps/current-work/
//                      multipage/the-canvas-element.html#canvaspixelarray
// In particular, write access clamps the value written to 0 or 255 if the
// value written is outside this range.
class ExternalPixelArray: public ExternalArray {
 public:
  inline uint8_t* external_pixel_pointer();

  // Setter and getter.
  inline uint8_t get(int index);
  inline void set(int index, uint8_t value);

  // This accessor applies the correct conversion from Smi, HeapNumber and
  // undefined and clamps the converted value between 0 and 255.
  Object* SetValue(uint32_t index, Object* value);

  // Casting.
  static inline ExternalPixelArray* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void ExternalPixelArrayPrint() {
    ExternalPixelArrayPrint(stdout);
  }
  void ExternalPixelArrayPrint(FILE* out);
#endif
#ifdef DEBUG
  void ExternalPixelArrayVerify();
#endif  // DEBUG

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalPixelArray);
};


class ExternalByteArray: public ExternalArray {
 public:
  // Setter and getter.
  inline int8_t get(int index);
  inline void set(int index, int8_t value);

  // This accessor applies the correct conversion from Smi, HeapNumber
  // and undefined.
  MaybeObject* SetValue(uint32_t index, Object* value);

  // Casting.
  static inline ExternalByteArray* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void ExternalByteArrayPrint() {
    ExternalByteArrayPrint(stdout);
  }
  void ExternalByteArrayPrint(FILE* out);
#endif
#ifdef DEBUG
  void ExternalByteArrayVerify();
#endif  // DEBUG

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalByteArray);
};


class ExternalUnsignedByteArray: public ExternalArray {
 public:
  // Setter and getter.
  inline uint8_t get(int index);
  inline void set(int index, uint8_t value);

  // This accessor applies the correct conversion from Smi, HeapNumber
  // and undefined.
  MaybeObject* SetValue(uint32_t index, Object* value);

  // Casting.
  static inline ExternalUnsignedByteArray* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void ExternalUnsignedByteArrayPrint() {
    ExternalUnsignedByteArrayPrint(stdout);
  }
  void ExternalUnsignedByteArrayPrint(FILE* out);
#endif
#ifdef DEBUG
  void ExternalUnsignedByteArrayVerify();
#endif  // DEBUG

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalUnsignedByteArray);
};


class ExternalShortArray: public ExternalArray {
 public:
  // Setter and getter.
  inline int16_t get(int index);
  inline void set(int index, int16_t value);

  // This accessor applies the correct conversion from Smi, HeapNumber
  // and undefined.
  MaybeObject* SetValue(uint32_t index, Object* value);

  // Casting.
  static inline ExternalShortArray* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void ExternalShortArrayPrint() {
    ExternalShortArrayPrint(stdout);
  }
  void ExternalShortArrayPrint(FILE* out);
#endif
#ifdef DEBUG
  void ExternalShortArrayVerify();
#endif  // DEBUG

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalShortArray);
};


class ExternalUnsignedShortArray: public ExternalArray {
 public:
  // Setter and getter.
  inline uint16_t get(int index);
  inline void set(int index, uint16_t value);

  // This accessor applies the correct conversion from Smi, HeapNumber
  // and undefined.
  MaybeObject* SetValue(uint32_t index, Object* value);

  // Casting.
  static inline ExternalUnsignedShortArray* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void ExternalUnsignedShortArrayPrint() {
    ExternalUnsignedShortArrayPrint(stdout);
  }
  void ExternalUnsignedShortArrayPrint(FILE* out);
#endif
#ifdef DEBUG
  void ExternalUnsignedShortArrayVerify();
#endif  // DEBUG

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalUnsignedShortArray);
};


class ExternalIntArray: public ExternalArray {
 public:
  // Setter and getter.
  inline int32_t get(int index);
  inline void set(int index, int32_t value);

  // This accessor applies the correct conversion from Smi, HeapNumber
  // and undefined.
  MaybeObject* SetValue(uint32_t index, Object* value);

  // Casting.
  static inline ExternalIntArray* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void ExternalIntArrayPrint() {
    ExternalIntArrayPrint(stdout);
  }
  void ExternalIntArrayPrint(FILE* out);
#endif
#ifdef DEBUG
  void ExternalIntArrayVerify();
#endif  // DEBUG

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalIntArray);
};


class ExternalUnsignedIntArray: public ExternalArray {
 public:
  // Setter and getter.
  inline uint32_t get(int index);
  inline void set(int index, uint32_t value);

  // This accessor applies the correct conversion from Smi, HeapNumber
  // and undefined.
  MaybeObject* SetValue(uint32_t index, Object* value);

  // Casting.
  static inline ExternalUnsignedIntArray* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void ExternalUnsignedIntArrayPrint() {
    ExternalUnsignedIntArrayPrint(stdout);
  }
  void ExternalUnsignedIntArrayPrint(FILE* out);
#endif
#ifdef DEBUG
  void ExternalUnsignedIntArrayVerify();
#endif  // DEBUG

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalUnsignedIntArray);
};


class ExternalFloatArray: public ExternalArray {
 public:
  // Setter and getter.
  inline float get(int index);
  inline void set(int index, float value);

  // This accessor applies the correct conversion from Smi, HeapNumber
  // and undefined.
  MaybeObject* SetValue(uint32_t index, Object* value);

  // Casting.
  static inline ExternalFloatArray* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void ExternalFloatArrayPrint() {
    ExternalFloatArrayPrint(stdout);
  }
  void ExternalFloatArrayPrint(FILE* out);
#endif
#ifdef DEBUG
  void ExternalFloatArrayVerify();
#endif  // DEBUG

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalFloatArray);
};


class ExternalDoubleArray: public ExternalArray {
 public:
  // Setter and getter.
  inline double get(int index);
  inline void set(int index, double value);

  // This accessor applies the correct conversion from Smi, HeapNumber
  // and undefined.
  MaybeObject* SetValue(uint32_t index, Object* value);

  // Casting.
  static inline ExternalDoubleArray* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void ExternalDoubleArrayPrint() {
    ExternalDoubleArrayPrint(stdout);
  }
  void ExternalDoubleArrayPrint(FILE* out);
#endif  // OBJECT_PRINT
#ifdef DEBUG
  void ExternalDoubleArrayVerify();
#endif  // DEBUG

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalDoubleArray);
};


// DeoptimizationInputData is a fixed array used to hold the deoptimization
// data for code generated by the Hydrogen/Lithium compiler.  It also
// contains information about functions that were inlined.  If N different
// functions were inlined then first N elements of the literal array will
// contain these functions.
//
// It can be empty.
class DeoptimizationInputData: public FixedArray {
 public:
  // Layout description.  Indices in the array.
  static const int kTranslationByteArrayIndex = 0;
  static const int kInlinedFunctionCountIndex = 1;
  static const int kLiteralArrayIndex = 2;
  static const int kOsrAstIdIndex = 3;
  static const int kOsrPcOffsetIndex = 4;
  static const int kFirstDeoptEntryIndex = 5;

  // Offsets of deopt entry elements relative to the start of the entry.
  static const int kAstIdOffset = 0;
  static const int kTranslationIndexOffset = 1;
  static const int kArgumentsStackHeightOffset = 2;
  static const int kDeoptEntrySize = 3;

  // Simple element accessors.
#define DEFINE_ELEMENT_ACCESSORS(name, type)      \
  type* name() {                                  \
    return type::cast(get(k##name##Index));       \
  }                                               \
  void Set##name(type* value) {                   \
    set(k##name##Index, value);                   \
  }

  DEFINE_ELEMENT_ACCESSORS(TranslationByteArray, ByteArray)
  DEFINE_ELEMENT_ACCESSORS(InlinedFunctionCount, Smi)
  DEFINE_ELEMENT_ACCESSORS(LiteralArray, FixedArray)
  DEFINE_ELEMENT_ACCESSORS(OsrAstId, Smi)
  DEFINE_ELEMENT_ACCESSORS(OsrPcOffset, Smi)

  // Unchecked accessor to be used during GC.
  FixedArray* UncheckedLiteralArray() {
    return reinterpret_cast<FixedArray*>(get(kLiteralArrayIndex));
  }

#undef DEFINE_ELEMENT_ACCESSORS

  // Accessors for elements of the ith deoptimization entry.
#define DEFINE_ENTRY_ACCESSORS(name, type)                       \
  type* name(int i) {                                            \
    return type::cast(get(IndexForEntry(i) + k##name##Offset));  \
  }                                                              \
  void Set##name(int i, type* value) {                           \
    set(IndexForEntry(i) + k##name##Offset, value);              \
  }

  DEFINE_ENTRY_ACCESSORS(AstId, Smi)
  DEFINE_ENTRY_ACCESSORS(TranslationIndex, Smi)
  DEFINE_ENTRY_ACCESSORS(ArgumentsStackHeight, Smi)

#undef DEFINE_ENTRY_ACCESSORS

  int DeoptCount() {
    return (length() - kFirstDeoptEntryIndex) / kDeoptEntrySize;
  }

  // Allocates a DeoptimizationInputData.
  MUST_USE_RESULT static MaybeObject* Allocate(int deopt_entry_count,
                                               PretenureFlag pretenure);

  // Casting.
  static inline DeoptimizationInputData* cast(Object* obj);

#ifdef ENABLE_DISASSEMBLER
  void DeoptimizationInputDataPrint(FILE* out);
#endif

 private:
  static int IndexForEntry(int i) {
    return kFirstDeoptEntryIndex + (i * kDeoptEntrySize);
  }

  static int LengthFor(int entry_count) {
    return IndexForEntry(entry_count);
  }
};


// DeoptimizationOutputData is a fixed array used to hold the deoptimization
// data for code generated by the full compiler.
// The format of the these objects is
//   [i * 2]: Ast ID for ith deoptimization.
//   [i * 2 + 1]: PC and state of ith deoptimization
class DeoptimizationOutputData: public FixedArray {
 public:
  int DeoptPoints() { return length() / 2; }
  Smi* AstId(int index) { return Smi::cast(get(index * 2)); }
  void SetAstId(int index, Smi* id) { set(index * 2, id); }
  Smi* PcAndState(int index) { return Smi::cast(get(1 + index * 2)); }
  void SetPcAndState(int index, Smi* offset) { set(1 + index * 2, offset); }

  static int LengthOfFixedArray(int deopt_points) {
    return deopt_points * 2;
  }

  // Allocates a DeoptimizationOutputData.
  MUST_USE_RESULT static MaybeObject* Allocate(int number_of_deopt_points,
                                               PretenureFlag pretenure);

  // Casting.
  static inline DeoptimizationOutputData* cast(Object* obj);

#if defined(OBJECT_PRINT) || defined(ENABLE_DISASSEMBLER)
  void DeoptimizationOutputDataPrint(FILE* out);
#endif
};


class SafepointEntry;


// Code describes objects with on-the-fly generated machine code.
class Code: public HeapObject {
 public:
  // Opaque data type for encapsulating code flags like kind, inline
  // cache state, and arguments count.
  // FLAGS_MIN_VALUE and FLAGS_MAX_VALUE are specified to ensure that
  // enumeration type has correct value range (see Issue 830 for more details).
  enum Flags {
    FLAGS_MIN_VALUE = kMinInt,
    FLAGS_MAX_VALUE = kMaxInt
  };

  enum Kind {
    FUNCTION,
    OPTIMIZED_FUNCTION,
    STUB,
    BUILTIN,
    LOAD_IC,
    KEYED_LOAD_IC,
    CALL_IC,
    KEYED_CALL_IC,
    STORE_IC,
    KEYED_STORE_IC,
    UNARY_OP_IC,
    BINARY_OP_IC,
    COMPARE_IC,
    TO_BOOLEAN_IC,
    // No more than 16 kinds. The value currently encoded in four bits in
    // Flags.

    // Pseudo-kinds.
    REGEXP = BUILTIN,
    FIRST_IC_KIND = LOAD_IC,
    LAST_IC_KIND = TO_BOOLEAN_IC
  };

  enum {
    NUMBER_OF_KINDS = LAST_IC_KIND + 1
  };

  typedef int ExtraICState;

  static const ExtraICState kNoExtraICState = 0;

#ifdef ENABLE_DISASSEMBLER
  // Printing
  static const char* Kind2String(Kind kind);
  static const char* ICState2String(InlineCacheState state);
  static const char* PropertyType2String(PropertyType type);
  static void PrintExtraICState(FILE* out, Kind kind, ExtraICState extra);
  inline void Disassemble(const char* name) {
    Disassemble(name, stdout);
  }
  void Disassemble(const char* name, FILE* out);
#endif  // ENABLE_DISASSEMBLER

  // [instruction_size]: Size of the native instructions
  inline int instruction_size();
  inline void set_instruction_size(int value);

  // [relocation_info]: Code relocation information
  DECL_ACCESSORS(relocation_info, ByteArray)
  void InvalidateRelocation();

  // [deoptimization_data]: Array containing data for deopt.
  DECL_ACCESSORS(deoptimization_data, FixedArray)

  // [code_flushing_candidate]: Field only used during garbage
  // collection to hold code flushing candidates. The contents of this
  // field does not have to be traced during garbage collection since
  // it is only used by the garbage collector itself.
  DECL_ACCESSORS(next_code_flushing_candidate, Object)

  // Unchecked accessors to be used during GC.
  inline ByteArray* unchecked_relocation_info();
  inline FixedArray* unchecked_deoptimization_data();

  inline int relocation_size();

  // [flags]: Various code flags.
  inline Flags flags();
  inline void set_flags(Flags flags);

  // [flags]: Access to specific code flags.
  inline Kind kind();
  inline InlineCacheState ic_state();  // Only valid for IC stubs.
  inline ExtraICState extra_ic_state();  // Only valid for IC stubs.
  inline InLoopFlag ic_in_loop();  // Only valid for IC stubs.
  inline PropertyType type();  // Only valid for monomorphic IC stubs.
  inline int arguments_count();  // Only valid for call IC stubs.

  // Testers for IC stub kinds.
  inline bool is_inline_cache_stub();
  inline bool is_load_stub() { return kind() == LOAD_IC; }
  inline bool is_keyed_load_stub() { return kind() == KEYED_LOAD_IC; }
  inline bool is_store_stub() { return kind() == STORE_IC; }
  inline bool is_keyed_store_stub() { return kind() == KEYED_STORE_IC; }
  inline bool is_call_stub() { return kind() == CALL_IC; }
  inline bool is_keyed_call_stub() { return kind() == KEYED_CALL_IC; }
  inline bool is_unary_op_stub() { return kind() == UNARY_OP_IC; }
  inline bool is_binary_op_stub() { return kind() == BINARY_OP_IC; }
  inline bool is_compare_ic_stub() { return kind() == COMPARE_IC; }
  inline bool is_to_boolean_ic_stub() { return kind() == TO_BOOLEAN_IC; }

  // [major_key]: For kind STUB or BINARY_OP_IC, the major key.
  inline int major_key();
  inline void set_major_key(int value);

  // [optimizable]: For FUNCTION kind, tells if it is optimizable.
  inline bool optimizable();
  inline void set_optimizable(bool value);

  // [has_deoptimization_support]: For FUNCTION kind, tells if it has
  // deoptimization support.
  inline bool has_deoptimization_support();
  inline void set_has_deoptimization_support(bool value);

  // [allow_osr_at_loop_nesting_level]: For FUNCTION kind, tells for
  // how long the function has been marked for OSR and therefore which
  // level of loop nesting we are willing to do on-stack replacement
  // for.
  inline void set_allow_osr_at_loop_nesting_level(int level);
  inline int allow_osr_at_loop_nesting_level();

  // [stack_slots]: For kind OPTIMIZED_FUNCTION, the number of stack slots
  // reserved in the code prologue.
  inline unsigned stack_slots();
  inline void set_stack_slots(unsigned slots);

  // [safepoint_table_start]: For kind OPTIMIZED_CODE, the offset in
  // the instruction stream where the safepoint table starts.
  inline unsigned safepoint_table_offset();
  inline void set_safepoint_table_offset(unsigned offset);

  // [stack_check_table_start]: For kind FUNCTION, the offset in the
  // instruction stream where the stack check table starts.
  inline unsigned stack_check_table_offset();
  inline void set_stack_check_table_offset(unsigned offset);

  // [check type]: For kind CALL_IC, tells how to check if the
  // receiver is valid for the given call.
  inline CheckType check_type();
  inline void set_check_type(CheckType value);

  // [type-recording unary op type]: For kind UNARY_OP_IC.
  inline byte unary_op_type();
  inline void set_unary_op_type(byte value);

  // [type-recording binary op type]: For kind BINARY_OP_IC.
  inline byte binary_op_type();
  inline void set_binary_op_type(byte value);
  inline byte binary_op_result_type();
  inline void set_binary_op_result_type(byte value);

  // [compare state]: For kind COMPARE_IC, tells what state the stub is in.
  inline byte compare_state();
  inline void set_compare_state(byte value);

  // [to_boolean_foo]: For kind TO_BOOLEAN_IC tells what state the stub is in.
  inline byte to_boolean_state();
  inline void set_to_boolean_state(byte value);

  // Get the safepoint entry for the given pc.
  SafepointEntry GetSafepointEntry(Address pc);

  // Mark this code object as not having a stack check table.  Assumes kind
  // is FUNCTION.
  void SetNoStackCheckTable();

  // Find the first map in an IC stub.
  Map* FindFirstMap();

  // Flags operations.
  static inline Flags ComputeFlags(
      Kind kind,
      InLoopFlag in_loop = NOT_IN_LOOP,
      InlineCacheState ic_state = UNINITIALIZED,
      ExtraICState extra_ic_state = kNoExtraICState,
      PropertyType type = NORMAL,
      int argc = -1,
      InlineCacheHolderFlag holder = OWN_MAP);

  static inline Flags ComputeMonomorphicFlags(
      Kind kind,
      PropertyType type,
      ExtraICState extra_ic_state = kNoExtraICState,
      InlineCacheHolderFlag holder = OWN_MAP,
      InLoopFlag in_loop = NOT_IN_LOOP,
      int argc = -1);

  static inline Kind ExtractKindFromFlags(Flags flags);
  static inline InlineCacheState ExtractICStateFromFlags(Flags flags);
  static inline ExtraICState ExtractExtraICStateFromFlags(Flags flags);
  static inline InLoopFlag ExtractICInLoopFromFlags(Flags flags);
  static inline PropertyType ExtractTypeFromFlags(Flags flags);
  static inline int ExtractArgumentsCountFromFlags(Flags flags);
  static inline InlineCacheHolderFlag ExtractCacheHolderFromFlags(Flags flags);
  static inline Flags RemoveTypeFromFlags(Flags flags);

  // Convert a target address into a code object.
  static inline Code* GetCodeFromTargetAddress(Address address);

  // Convert an entry address into an object.
  static inline Object* GetObjectFromEntryAddress(Address location_of_address);

  // Returns the address of the first instruction.
  inline byte* instruction_start();

  // Returns the address right after the last instruction.
  inline byte* instruction_end();

  // Returns the size of the instructions, padding, and relocation information.
  inline int body_size();

  // Returns the address of the first relocation info (read backwards!).
  inline byte* relocation_start();

  // Code entry point.
  inline byte* entry();

  // Returns true if pc is inside this object's instructions.
  inline bool contains(byte* pc);

  // Relocate the code by delta bytes. Called to signal that this code
  // object has been moved by delta bytes.
  void Relocate(intptr_t delta);

  // Migrate code described by desc.
  void CopyFrom(const CodeDesc& desc);

  // Returns the object size for a given body (used for allocation).
  static int SizeFor(int body_size) {
    ASSERT_SIZE_TAG_ALIGNED(body_size);
    return RoundUp(kHeaderSize + body_size, kCodeAlignment);
  }

  // Calculate the size of the code object to report for log events. This takes
  // the layout of the code object into account.
  int ExecutableSize() {
    // Check that the assumptions about the layout of the code object holds.
    ASSERT_EQ(static_cast<int>(instruction_start() - address()),
              Code::kHeaderSize);
    return instruction_size() + Code::kHeaderSize;
  }

  // Locating source position.
  int SourcePosition(Address pc);
  int SourceStatementPosition(Address pc);

  // Casting.
  static inline Code* cast(Object* obj);

  // Dispatched behavior.
  int CodeSize() { return SizeFor(body_size()); }
  inline void CodeIterateBody(ObjectVisitor* v);

  template<typename StaticVisitor>
  inline void CodeIterateBody(Heap* heap);
#ifdef OBJECT_PRINT
  inline void CodePrint() {
    CodePrint(stdout);
  }
  void CodePrint(FILE* out);
#endif
#ifdef DEBUG
  void CodeVerify();
#endif

  // Returns the isolate/heap this code object belongs to.
  inline Isolate* isolate();
  inline Heap* heap();

  // Max loop nesting marker used to postpose OSR. We don't take loop
  // nesting that is deeper than 5 levels into account.
  static const int kMaxLoopNestingMarker = 6;

  // Layout description.
  static const int kInstructionSizeOffset = HeapObject::kHeaderSize;
  static const int kRelocationInfoOffset = kInstructionSizeOffset + kIntSize;
  static const int kDeoptimizationDataOffset =
      kRelocationInfoOffset + kPointerSize;
  static const int kNextCodeFlushingCandidateOffset =
      kDeoptimizationDataOffset + kPointerSize;
  static const int kFlagsOffset =
      kNextCodeFlushingCandidateOffset + kPointerSize;

  static const int kKindSpecificFlagsOffset = kFlagsOffset + kIntSize;
  static const int kKindSpecificFlagsSize = 2 * kIntSize;

  static const int kHeaderPaddingStart = kKindSpecificFlagsOffset +
      kKindSpecificFlagsSize;

  // Add padding to align the instruction start following right after
  // the Code object header.
  static const int kHeaderSize =
      (kHeaderPaddingStart + kCodeAlignmentMask) & ~kCodeAlignmentMask;

  // Byte offsets within kKindSpecificFlagsOffset.
  static const int kStubMajorKeyOffset = kKindSpecificFlagsOffset;
  static const int kOptimizableOffset = kKindSpecificFlagsOffset;
  static const int kStackSlotsOffset = kKindSpecificFlagsOffset;
  static const int kCheckTypeOffset = kKindSpecificFlagsOffset;

  static const int kUnaryOpTypeOffset = kStubMajorKeyOffset + 1;
  static const int kBinaryOpTypeOffset = kStubMajorKeyOffset + 1;
  static const int kCompareStateOffset = kStubMajorKeyOffset + 1;
  static const int kToBooleanTypeOffset = kStubMajorKeyOffset + 1;
  static const int kHasDeoptimizationSupportOffset = kOptimizableOffset + 1;

  static const int kBinaryOpReturnTypeOffset = kBinaryOpTypeOffset + 1;
  static const int kAllowOSRAtLoopNestingLevelOffset =
      kHasDeoptimizationSupportOffset + 1;

  static const int kSafepointTableOffsetOffset = kStackSlotsOffset + kIntSize;
  static const int kStackCheckTableOffsetOffset = kStackSlotsOffset + kIntSize;

  // Flags layout.
  static const int kFlagsICStateShift        = 0;
  static const int kFlagsICInLoopShift       = 3;
  static const int kFlagsTypeShift           = 4;
  static const int kFlagsKindShift           = 8;
  static const int kFlagsICHolderShift       = 12;
  static const int kFlagsExtraICStateShift   = 13;
  static const int kFlagsArgumentsCountShift = 15;

  static const int kFlagsICStateMask        = 0x00000007;  // 00000000111
  static const int kFlagsICInLoopMask       = 0x00000008;  // 00000001000
  static const int kFlagsTypeMask           = 0x000000F0;  // 00001110000
  static const int kFlagsKindMask           = 0x00000F00;  // 11110000000
  static const int kFlagsCacheInPrototypeMapMask = 0x00001000;
  static const int kFlagsExtraICStateMask   = 0x00006000;
  static const int kFlagsArgumentsCountMask = 0xFFFF8000;

  static const int kFlagsNotUsedInLookup =
      (kFlagsICInLoopMask | kFlagsTypeMask | kFlagsCacheInPrototypeMapMask);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Code);
};


// All heap objects have a Map that describes their structure.
//  A Map contains information about:
//  - Size information about the object
//  - How to iterate over an object (for garbage collection)
class Map: public HeapObject {
 public:
  // Instance size.
  // Size in bytes or kVariableSizeSentinel if instances do not have
  // a fixed size.
  inline int instance_size();
  inline void set_instance_size(int value);

  // Count of properties allocated in the object.
  inline int inobject_properties();
  inline void set_inobject_properties(int value);

  // Count of property fields pre-allocated in the object when first allocated.
  inline int pre_allocated_property_fields();
  inline void set_pre_allocated_property_fields(int value);

  // Instance type.
  inline InstanceType instance_type();
  inline void set_instance_type(InstanceType value);

  // Tells how many unused property fields are available in the
  // instance (only used for JSObject in fast mode).
  inline int unused_property_fields();
  inline void set_unused_property_fields(int value);

  // Bit field.
  inline byte bit_field();
  inline void set_bit_field(byte value);

  // Bit field 2.
  inline byte bit_field2();
  inline void set_bit_field2(byte value);

  // Bit field 3.
  // TODO(1399): It should be possible to make room for bit_field3 in the map
  // without overloading the instance descriptors field (and storing it in the
  // DescriptorArray when the map has one).
  inline int bit_field3();
  inline void set_bit_field3(int value);

  // Tells whether the object in the prototype property will be used
  // for instances created from this function.  If the prototype
  // property is set to a value that is not a JSObject, the prototype
  // property will not be used to create instances of the function.
  // See ECMA-262, 13.2.2.
  inline void set_non_instance_prototype(bool value);
  inline bool has_non_instance_prototype();

  // Tells whether function has special prototype property. If not, prototype
  // property will not be created when accessed (will return undefined),
  // and construction from this function will not be allowed.
  inline void set_function_with_prototype(bool value);
  inline bool function_with_prototype();

  // Tells whether the instance with this map should be ignored by the
  // __proto__ accessor.
  inline void set_is_hidden_prototype() {
    set_bit_field(bit_field() | (1 << kIsHiddenPrototype));
  }

  inline bool is_hidden_prototype() {
    return ((1 << kIsHiddenPrototype) & bit_field()) != 0;
  }

  // Records and queries whether the instance has a named interceptor.
  inline void set_has_named_interceptor() {
    set_bit_field(bit_field() | (1 << kHasNamedInterceptor));
  }

  inline bool has_named_interceptor() {
    return ((1 << kHasNamedInterceptor) & bit_field()) != 0;
  }

  // Records and queries whether the instance has an indexed interceptor.
  inline void set_has_indexed_interceptor() {
    set_bit_field(bit_field() | (1 << kHasIndexedInterceptor));
  }

  inline bool has_indexed_interceptor() {
    return ((1 << kHasIndexedInterceptor) & bit_field()) != 0;
  }

  // Tells whether the instance is undetectable.
  // An undetectable object is a special class of JSObject: 'typeof' operator
  // returns undefined, ToBoolean returns false. Otherwise it behaves like
  // a normal JS object.  It is useful for implementing undetectable
  // document.all in Firefox & Safari.
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=248549.
  inline void set_is_undetectable() {
    set_bit_field(bit_field() | (1 << kIsUndetectable));
  }

  inline bool is_undetectable() {
    return ((1 << kIsUndetectable) & bit_field()) != 0;
  }

  // Tells whether the instance has a call-as-function handler.
  inline void set_has_instance_call_handler() {
    set_bit_field(bit_field() | (1 << kHasInstanceCallHandler));
  }

  inline bool has_instance_call_handler() {
    return ((1 << kHasInstanceCallHandler) & bit_field()) != 0;
  }

  inline void set_is_extensible(bool value);
  inline bool is_extensible();

  inline void set_elements_kind(JSObject::ElementsKind elements_kind) {
    ASSERT(elements_kind < JSObject::kElementsKindCount);
    ASSERT(JSObject::kElementsKindCount <= (1 << kElementsKindBitCount));
    set_bit_field2((bit_field2() & ~kElementsKindMask) |
        (elements_kind << kElementsKindShift));
    ASSERT(this->elements_kind() == elements_kind);
  }

  inline JSObject::ElementsKind elements_kind() {
    return static_cast<JSObject::ElementsKind>(
        (bit_field2() & kElementsKindMask) >> kElementsKindShift);
  }

  // Tells whether the instance has fast elements.
  // Equivalent to instance->GetElementsKind() == FAST_ELEMENTS.
  inline bool has_fast_elements() {
    return elements_kind() == JSObject::FAST_ELEMENTS;
  }

  inline bool has_fast_double_elements() {
    return elements_kind() == JSObject::FAST_DOUBLE_ELEMENTS;
  }

  inline bool has_external_array_elements() {
    JSObject::ElementsKind kind(elements_kind());
    return kind >= JSObject::FIRST_EXTERNAL_ARRAY_ELEMENTS_KIND &&
        kind <= JSObject::LAST_EXTERNAL_ARRAY_ELEMENTS_KIND;
  }

  inline bool has_dictionary_elements() {
    return elements_kind() == JSObject::DICTIONARY_ELEMENTS;
  }

  // Tells whether the map is attached to SharedFunctionInfo
  // (for inobject slack tracking).
  inline void set_attached_to_shared_function_info(bool value);

  inline bool attached_to_shared_function_info();

  // Tells whether the map is shared between objects that may have different
  // behavior. If true, the map should never be modified, instead a clone
  // should be created and modified.
  inline void set_is_shared(bool value);

  inline bool is_shared();

  // Tells whether the instance needs security checks when accessing its
  // properties.
  inline void set_is_access_check_needed(bool access_check_needed);
  inline bool is_access_check_needed();

  // [prototype]: implicit prototype object.
  DECL_ACCESSORS(prototype, Object)

  // [constructor]: points back to the function responsible for this map.
  DECL_ACCESSORS(constructor, Object)

  inline JSFunction* unchecked_constructor();

  // Should only be called by the code that initializes map to set initial valid
  // value of the instance descriptor member.
  inline void init_instance_descriptors();

  // [instance descriptors]: describes the object.
  DECL_ACCESSORS(instance_descriptors, DescriptorArray)

  // Sets the instance descriptor array for the map to be an empty descriptor
  // array.
  inline void clear_instance_descriptors();

  // [stub cache]: contains stubs compiled for this map.
  DECL_ACCESSORS(code_cache, Object)

  // [prototype transitions]: cache of prototype transitions.
  // Prototype transition is a transition that happens
  // when we change object's prototype to a new one.
  // Cache format:
  //    0: finger - index of the first free cell in the cache
  //    1 + 2 * i: prototype
  //    2 + 2 * i: target map
  DECL_ACCESSORS(prototype_transitions, FixedArray)
  inline FixedArray* unchecked_prototype_transitions();

  static const int kProtoTransitionHeaderSize = 1;
  static const int kProtoTransitionNumberOfEntriesOffset = 0;
  static const int kProtoTransitionElementsPerEntry = 2;
  static const int kProtoTransitionPrototypeOffset = 0;
  static const int kProtoTransitionMapOffset = 1;

  inline int NumberOfProtoTransitions() {
    FixedArray* cache = unchecked_prototype_transitions();
    if (cache->length() == 0) return 0;
    return
        Smi::cast(cache->get(kProtoTransitionNumberOfEntriesOffset))->value();
  }

  inline void SetNumberOfProtoTransitions(int value) {
    FixedArray* cache = unchecked_prototype_transitions();
    ASSERT(cache->length() != 0);
    cache->set_unchecked(kProtoTransitionNumberOfEntriesOffset,
                         Smi::FromInt(value));
  }

  // Lookup in the map's instance descriptors and fill out the result
  // with the given holder if the name is found. The holder may be
  // NULL when this function is used from the compiler.
  void LookupInDescriptors(JSObject* holder,
                           String* name,
                           LookupResult* result);

  MUST_USE_RESULT MaybeObject* CopyDropDescriptors();

  MUST_USE_RESULT MaybeObject* CopyNormalized(PropertyNormalizationMode mode,
                                              NormalizedMapSharingMode sharing);

  // Returns a copy of the map, with all transitions dropped from the
  // instance descriptors.
  MUST_USE_RESULT MaybeObject* CopyDropTransitions();

  // Returns this map if it already has elements that are fast, otherwise
  // returns a copy of the map, with all transitions dropped from the
  // descriptors and the ElementsKind set to FAST_ELEMENTS.
  MUST_USE_RESULT inline MaybeObject* GetFastElementsMap();

  // Returns this map if it already has fast elements that are doubles,
  // otherwise returns a copy of the map, with all transitions dropped from the
  // descriptors and the ElementsKind set to FAST_DOUBLE_ELEMENTS.
  MUST_USE_RESULT inline MaybeObject* GetFastDoubleElementsMap();

  // Returns this map if already has dictionary elements, otherwise returns a
  // copy of the map, with all transitions dropped from the descriptors and the
  // ElementsKind set to DICTIONARY_ELEMENTS.
  MUST_USE_RESULT inline MaybeObject* GetSlowElementsMap();

  // Returns a new map with all transitions dropped from the descriptors and the
  // ElementsKind set to one of the value corresponding to array_type.
  MUST_USE_RESULT MaybeObject* GetExternalArrayElementsMap(
      ExternalArrayType array_type,
      bool safe_to_add_transition);

  // Returns the property index for name (only valid for FAST MODE).
  int PropertyIndexFor(String* name);

  // Returns the next free property index (only valid for FAST MODE).
  int NextFreePropertyIndex();

  // Returns the number of properties described in instance_descriptors.
  int NumberOfDescribedProperties();

  // Casting.
  static inline Map* cast(Object* obj);

  // Locate an accessor in the instance descriptor.
  AccessorDescriptor* FindAccessor(String* name);

  // Code cache operations.

  // Clears the code cache.
  inline void ClearCodeCache(Heap* heap);

  // Update code cache.
  MUST_USE_RESULT MaybeObject* UpdateCodeCache(String* name, Code* code);

  // Returns the found code or undefined if absent.
  Object* FindInCodeCache(String* name, Code::Flags flags);

  // Returns the non-negative index of the code object if it is in the
  // cache and -1 otherwise.
  int IndexInCodeCache(Object* name, Code* code);

  // Removes a code object from the code cache at the given index.
  void RemoveFromCodeCache(String* name, Code* code, int index);

  // For every transition in this map, makes the transition's
  // target's prototype pointer point back to this map.
  // This is undone in MarkCompactCollector::ClearNonLiveTransitions().
  void CreateBackPointers();

  // Set all map transitions from this map to dead maps to null.
  // Also, restore the original prototype on the targets of these
  // transitions, so that we do not process this map again while
  // following back pointers.
  void ClearNonLiveTransitions(Heap* heap, Object* real_prototype);

  // Computes a hash value for this map, to be used in HashTables and such.
  int Hash();

  // Compares this map to another to see if they describe equivalent objects.
  // If |mode| is set to CLEAR_INOBJECT_PROPERTIES, |other| is treated as if
  // it had exactly zero inobject properties.
  // The "shared" flags of both this map and |other| are ignored.
  bool EquivalentToForNormalization(Map* other, PropertyNormalizationMode mode);

  // Returns true if this map and |other| describe equivalent objects.
  // The "shared" flags of both this map and |other| are ignored.
  bool EquivalentTo(Map* other) {
    return EquivalentToForNormalization(other, KEEP_INOBJECT_PROPERTIES);
  }

  // Dispatched behavior.
#ifdef OBJECT_PRINT
  inline void MapPrint() {
    MapPrint(stdout);
  }
  void MapPrint(FILE* out);
#endif
#ifdef DEBUG
  void MapVerify();
  void SharedMapVerify();
#endif

  inline int visitor_id();
  inline void set_visitor_id(int visitor_id);

  // Returns the isolate/heap this map belongs to.
  inline Isolate* isolate();
  inline Heap* heap();

  typedef void (*TraverseCallback)(Map* map, void* data);

  void TraverseTransitionTree(TraverseCallback callback, void* data);

  static const int kMaxCachedPrototypeTransitions = 256;

  Object* GetPrototypeTransition(Object* prototype);

  MaybeObject* PutPrototypeTransition(Object* prototype, Map* map);

  static const int kMaxPreAllocatedPropertyFields = 255;

  // Layout description.
  static const int kInstanceSizesOffset = HeapObject::kHeaderSize;
  static const int kInstanceAttributesOffset = kInstanceSizesOffset + kIntSize;
  static const int kPrototypeOffset = kInstanceAttributesOffset + kIntSize;
  static const int kConstructorOffset = kPrototypeOffset + kPointerSize;
  // Storage for instance descriptors is overloaded to also contain additional
  // map flags when unused (bit_field3). When the map has instance descriptors,
  // the flags are transferred to the instance descriptor array and accessed
  // through an extra indirection.
  // TODO(1399): It should be possible to make room for bit_field3 in the map
  // without overloading the instance descriptors field, but the map is
  // currently perfectly aligned to 32 bytes and extending it at all would
  // double its size.  After the increment GC work lands, this size restriction
  // could be loosened and bit_field3 moved directly back in the map.
  static const int kInstanceDescriptorsOrBitField3Offset =
      kConstructorOffset + kPointerSize;
  static const int kCodeCacheOffset =
      kInstanceDescriptorsOrBitField3Offset + kPointerSize;
  static const int kPrototypeTransitionsOffset =
      kCodeCacheOffset + kPointerSize;
  static const int kPadStart = kPrototypeTransitionsOffset + kPointerSize;
  static const int kSize = MAP_POINTER_ALIGN(kPadStart);

  // Layout of pointer fields. Heap iteration code relies on them
  // being continiously allocated.
  static const int kPointerFieldsBeginOffset = Map::kPrototypeOffset;
  static const int kPointerFieldsEndOffset =
      Map::kPrototypeTransitionsOffset + kPointerSize;

  // Byte offsets within kInstanceSizesOffset.
  static const int kInstanceSizeOffset = kInstanceSizesOffset + 0;
  static const int kInObjectPropertiesByte = 1;
  static const int kInObjectPropertiesOffset =
      kInstanceSizesOffset + kInObjectPropertiesByte;
  static const int kPreAllocatedPropertyFieldsByte = 2;
  static const int kPreAllocatedPropertyFieldsOffset =
      kInstanceSizesOffset + kPreAllocatedPropertyFieldsByte;
  static const int kVisitorIdByte = 3;
  static const int kVisitorIdOffset = kInstanceSizesOffset + kVisitorIdByte;

  // Byte offsets within kInstanceAttributesOffset attributes.
  static const int kInstanceTypeOffset = kInstanceAttributesOffset + 0;
  static const int kUnusedPropertyFieldsOffset = kInstanceAttributesOffset + 1;
  static const int kBitFieldOffset = kInstanceAttributesOffset + 2;
  static const int kBitField2Offset = kInstanceAttributesOffset + 3;

  STATIC_CHECK(kInstanceTypeOffset == Internals::kMapInstanceTypeOffset);

  // Bit positions for bit field.
  static const int kUnused = 0;  // To be used for marking recently used maps.
  static const int kHasNonInstancePrototype = 1;
  static const int kIsHiddenPrototype = 2;
  static const int kHasNamedInterceptor = 3;
  static const int kHasIndexedInterceptor = 4;
  static const int kIsUndetectable = 5;
  static const int kHasInstanceCallHandler = 6;
  static const int kIsAccessCheckNeeded = 7;

  // Bit positions for bit field 2
  static const int kIsExtensible = 0;
  static const int kFunctionWithPrototype = 1;
  static const int kStringWrapperSafeForDefaultValueOf = 2;
  static const int kAttachedToSharedFunctionInfo = 3;
  // No bits can be used after kElementsKindFirstBit, they are all reserved for
  // storing ElementKind.  for anything other than storing the ElementKind.
  static const int kElementsKindShift = 4;
  static const int kElementsKindBitCount = 4;

  // Derived values from bit field 2
  static const int kElementsKindMask = (-1 << kElementsKindShift) &
      ((1 << (kElementsKindShift + kElementsKindBitCount)) - 1);
  static const int8_t kMaximumBitField2FastElementValue = static_cast<int8_t>(
      (JSObject::FAST_ELEMENTS + 1) << Map::kElementsKindShift) - 1;

  // Bit positions for bit field 3
  static const int kIsShared = 0;

  // Layout of the default cache. It holds alternating name and code objects.
  static const int kCodeCacheEntrySize = 2;
  static const int kCodeCacheEntryNameOffset = 0;
  static const int kCodeCacheEntryCodeOffset = 1;

  typedef FixedBodyDescriptor<kPointerFieldsBeginOffset,
                              kPointerFieldsEndOffset,
                              kSize> BodyDescriptor;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Map);
};


// An abstract superclass, a marker class really, for simple structure classes.
// It doesn't carry much functionality but allows struct classes to be
// identified in the type system.
class Struct: public HeapObject {
 public:
  inline void InitializeBody(int object_size);
  static inline Struct* cast(Object* that);
};


// Script describes a script which has been added to the VM.
class Script: public Struct {
 public:
  // Script types.
  enum Type {
    TYPE_NATIVE = 0,
    TYPE_EXTENSION = 1,
    TYPE_NORMAL = 2
  };

  // Script compilation types.
  enum CompilationType {
    COMPILATION_TYPE_HOST = 0,
    COMPILATION_TYPE_EVAL = 1
  };

  // [source]: the script source.
  DECL_ACCESSORS(source, Object)

  // [name]: the script name.
  DECL_ACCESSORS(name, Object)

  // [id]: the script id.
  DECL_ACCESSORS(id, Object)

  // [line_offset]: script line offset in resource from where it was extracted.
  DECL_ACCESSORS(line_offset, Smi)

  // [column_offset]: script column offset in resource from where it was
  // extracted.
  DECL_ACCESSORS(column_offset, Smi)

  // [data]: additional data associated with this script.
  DECL_ACCESSORS(data, Object)

  // [context_data]: context data for the context this script was compiled in.
  DECL_ACCESSORS(context_data, Object)

  // [wrapper]: the wrapper cache.
  DECL_ACCESSORS(wrapper, Foreign)

  // [type]: the script type.
  DECL_ACCESSORS(type, Smi)

  // [compilation]: how the the script was compiled.
  DECL_ACCESSORS(compilation_type, Smi)

  // [line_ends]: FixedArray of line ends positions.
  DECL_ACCESSORS(line_ends, Object)

  // [eval_from_shared]: for eval scripts the shared funcion info for the
  // function from which eval was called.
  DECL_ACCESSORS(eval_from_shared, Object)

  // [eval_from_instructions_offset]: the instruction offset in the code for the
  // function from which eval was called where eval was called.
  DECL_ACCESSORS(eval_from_instructions_offset, Smi)

  static inline Script* cast(Object* obj);

  // If script source is an external string, check that the underlying
  // resource is accessible. Otherwise, always return true.
  inline bool HasValidSource();

#ifdef OBJECT_PRINT
  inline void ScriptPrint() {
    ScriptPrint(stdout);
  }
  void ScriptPrint(FILE* out);
#endif
#ifdef DEBUG
  void ScriptVerify();
#endif

  static const int kSourceOffset = HeapObject::kHeaderSize;
  static const int kNameOffset = kSourceOffset + kPointerSize;
  static const int kLineOffsetOffset = kNameOffset + kPointerSize;
  static const int kColumnOffsetOffset = kLineOffsetOffset + kPointerSize;
  static const int kDataOffset = kColumnOffsetOffset + kPointerSize;
  static const int kContextOffset = kDataOffset + kPointerSize;
  static const int kWrapperOffset = kContextOffset + kPointerSize;
  static const int kTypeOffset = kWrapperOffset + kPointerSize;
  static const int kCompilationTypeOffset = kTypeOffset + kPointerSize;
  static const int kLineEndsOffset = kCompilationTypeOffset + kPointerSize;
  static const int kIdOffset = kLineEndsOffset + kPointerSize;
  static const int kEvalFromSharedOffset = kIdOffset + kPointerSize;
  static const int kEvalFrominstructionsOffsetOffset =
      kEvalFromSharedOffset + kPointerSize;
  static const int kSize = kEvalFrominstructionsOffsetOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Script);
};


// List of builtin functions we want to identify to improve code
// generation.
//
// Each entry has a name of a global object property holding an object
// optionally followed by ".prototype", a name of a builtin function
// on the object (the one the id is set for), and a label.
//
// Installation of ids for the selected builtin functions is handled
// by the bootstrapper.
//
// NOTE: Order is important: math functions should be at the end of
// the list and MathFloor should be the first math function.
#define FUNCTIONS_WITH_ID_LIST(V)                   \
  V(Array.prototype, push, ArrayPush)               \
  V(Array.prototype, pop, ArrayPop)                 \
  V(String.prototype, charCodeAt, StringCharCodeAt) \
  V(String.prototype, charAt, StringCharAt)         \
  V(String, fromCharCode, StringFromCharCode)       \
  V(Math, floor, MathFloor)                         \
  V(Math, round, MathRound)                         \
  V(Math, ceil, MathCeil)                           \
  V(Math, abs, MathAbs)                             \
  V(Math, log, MathLog)                             \
  V(Math, sin, MathSin)                             \
  V(Math, cos, MathCos)                             \
  V(Math, tan, MathTan)                             \
  V(Math, asin, MathASin)                           \
  V(Math, acos, MathACos)                           \
  V(Math, atan, MathATan)                           \
  V(Math, exp, MathExp)                             \
  V(Math, sqrt, MathSqrt)                           \
  V(Math, pow, MathPow)


enum BuiltinFunctionId {
#define DECLARE_FUNCTION_ID(ignored1, ignore2, name)    \
  k##name,
  FUNCTIONS_WITH_ID_LIST(DECLARE_FUNCTION_ID)
#undef DECLARE_FUNCTION_ID
  // Fake id for a special case of Math.pow. Note, it continues the
  // list of math functions.
  kMathPowHalf,
  kFirstMathFunctionId = kMathFloor
};


// SharedFunctionInfo describes the JSFunction information that can be
// shared by multiple instances of the function.
class SharedFunctionInfo: public HeapObject {
 public:
  // [name]: Function name.
  DECL_ACCESSORS(name, Object)

  // [code]: Function code.
  DECL_ACCESSORS(code, Code)

  // [scope_info]: Scope info.
  DECL_ACCESSORS(scope_info, SerializedScopeInfo)

  // [construct stub]: Code stub for constructing instances of this function.
  DECL_ACCESSORS(construct_stub, Code)

  inline Code* unchecked_code();

  // Returns if this function has been compiled to native code yet.
  inline bool is_compiled();

  // [length]: The function length - usually the number of declared parameters.
  // Use up to 2^30 parameters.
  inline int length();
  inline void set_length(int value);

  // [formal parameter count]: The declared number of parameters.
  inline int formal_parameter_count();
  inline void set_formal_parameter_count(int value);

  // Set the formal parameter count so the function code will be
  // called without using argument adaptor frames.
  inline void DontAdaptArguments();

  // [expected_nof_properties]: Expected number of properties for the function.
  inline int expected_nof_properties();
  inline void set_expected_nof_properties(int value);

  // Inobject slack tracking is the way to reclaim unused inobject space.
  //
  // The instance size is initially determined by adding some slack to
  // expected_nof_properties (to allow for a few extra properties added
  // after the constructor). There is no guarantee that the extra space
  // will not be wasted.
  //
  // Here is the algorithm to reclaim the unused inobject space:
  // - Detect the first constructor call for this SharedFunctionInfo.
  //   When it happens enter the "in progress" state: remember the
  //   constructor's initial_map and install a special construct stub that
  //   counts constructor calls.
  // - While the tracking is in progress create objects filled with
  //   one_pointer_filler_map instead of undefined_value. This way they can be
  //   resized quickly and safely.
  // - Once enough (kGenerousAllocationCount) objects have been created
  //   compute the 'slack' (traverse the map transition tree starting from the
  //   initial_map and find the lowest value of unused_property_fields).
  // - Traverse the transition tree again and decrease the instance size
  //   of every map. Existing objects will resize automatically (they are
  //   filled with one_pointer_filler_map). All further allocations will
  //   use the adjusted instance size.
  // - Decrease expected_nof_properties so that an allocations made from
  //   another context will use the adjusted instance size too.
  // - Exit "in progress" state by clearing the reference to the initial_map
  //   and setting the regular construct stub (generic or inline).
  //
  //  The above is the main event sequence. Some special cases are possible
  //  while the tracking is in progress:
  //
  // - GC occurs.
  //   Check if the initial_map is referenced by any live objects (except this
  //   SharedFunctionInfo). If it is, continue tracking as usual.
  //   If it is not, clear the reference and reset the tracking state. The
  //   tracking will be initiated again on the next constructor call.
  //
  // - The constructor is called from another context.
  //   Immediately complete the tracking, perform all the necessary changes
  //   to maps. This is  necessary because there is no efficient way to track
  //   multiple initial_maps.
  //   Proceed to create an object in the current context (with the adjusted
  //   size).
  //
  // - A different constructor function sharing the same SharedFunctionInfo is
  //   called in the same context. This could be another closure in the same
  //   context, or the first function could have been disposed.
  //   This is handled the same way as the previous case.
  //
  //  Important: inobject slack tracking is not attempted during the snapshot
  //  creation.

  static const int kGenerousAllocationCount = 8;

  // [construction_count]: Counter for constructor calls made during
  // the tracking phase.
  inline int construction_count();
  inline void set_construction_count(int value);

  // [initial_map]: initial map of the first function called as a constructor.
  // Saved for the duration of the tracking phase.
  // This is a weak link (GC resets it to undefined_value if no other live
  // object reference this map).
  DECL_ACCESSORS(initial_map, Object)

  // True if the initial_map is not undefined and the countdown stub is
  // installed.
  inline bool IsInobjectSlackTrackingInProgress();

  // Starts the tracking.
  // Stores the initial map and installs the countdown stub.
  // IsInobjectSlackTrackingInProgress is normally true after this call,
  // except when tracking have not been started (e.g. the map has no unused
  // properties or the snapshot is being built).
  void StartInobjectSlackTracking(Map* map);

  // Completes the tracking.
  // IsInobjectSlackTrackingInProgress is false after this call.
  void CompleteInobjectSlackTracking();

  // Clears the initial_map before the GC marking phase to ensure the reference
  // is weak. IsInobjectSlackTrackingInProgress is false after this call.
  void DetachInitialMap();

  // Restores the link to the initial map after the GC marking phase.
  // IsInobjectSlackTrackingInProgress is true after this call.
  void AttachInitialMap(Map* map);

  // False if there are definitely no live objects created from this function.
  // True if live objects _may_ exist (existence not guaranteed).
  // May go back from true to false after GC.
  DECL_BOOLEAN_ACCESSORS(live_objects_may_exist)

  // [instance class name]: class name for instances.
  DECL_ACCESSORS(instance_class_name, Object)

  // [function data]: This field holds some additional data for function.
  // Currently it either has FunctionTemplateInfo to make benefit the API
  // or Smi identifying a builtin function.
  // In the long run we don't want all functions to have this field but
  // we can fix that when we have a better model for storing hidden data
  // on objects.
  DECL_ACCESSORS(function_data, Object)

  inline bool IsApiFunction();
  inline FunctionTemplateInfo* get_api_func_data();
  inline bool HasBuiltinFunctionId();
  inline BuiltinFunctionId builtin_function_id();

  // [script info]: Script from which the function originates.
  DECL_ACCESSORS(script, Object)

  // [num_literals]: Number of literals used by this function.
  inline int num_literals();
  inline void set_num_literals(int value);

  // [start_position_and_type]: Field used to store both the source code
  // position, whether or not the function is a function expression,
  // and whether or not the function is a toplevel function. The two
  // least significants bit indicates whether the function is an
  // expression and the rest contains the source code position.
  inline int start_position_and_type();
  inline void set_start_position_and_type(int value);

  // [debug info]: Debug information.
  DECL_ACCESSORS(debug_info, Object)

  // [inferred name]: Name inferred from variable or property
  // assignment of this function. Used to facilitate debugging and
  // profiling of JavaScript code written in OO style, where almost
  // all functions are anonymous but are assigned to object
  // properties.
  DECL_ACCESSORS(inferred_name, String)

  // The function's name if it is non-empty, otherwise the inferred name.
  String* DebugName();

  // Position of the 'function' token in the script source.
  inline int function_token_position();
  inline void set_function_token_position(int function_token_position);

  // Position of this function in the script source.
  inline int start_position();
  inline void set_start_position(int start_position);

  // End position of this function in the script source.
  inline int end_position();
  inline void set_end_position(int end_position);

  // Is this function a function expression in the source code.
  inline bool is_expression();
  inline void set_is_expression(bool value);

  // Is this function a top-level function (scripts, evals).
  inline bool is_toplevel();
  inline void set_is_toplevel(bool value);

  // Bit field containing various information collected by the compiler to
  // drive optimization.
  inline int compiler_hints();
  inline void set_compiler_hints(int value);

  // A counter used to determine when to stress the deoptimizer with a
  // deopt.
  inline Smi* deopt_counter();
  inline void set_deopt_counter(Smi* counter);

  // Add information on assignments of the form this.x = ...;
  void SetThisPropertyAssignmentsInfo(
      bool has_only_simple_this_property_assignments,
      FixedArray* this_property_assignments);

  // Clear information on assignments of the form this.x = ...;
  void ClearThisPropertyAssignmentsInfo();

  // Indicate that this function only consists of assignments of the form
  // this.x = y; where y is either a constant or refers to an argument.
  inline bool has_only_simple_this_property_assignments();

  // Indicates if this function can be lazy compiled.
  // This is used to determine if we can safely flush code from a function
  // when doing GC if we expect that the function will no longer be used.
  DECL_BOOLEAN_ACCESSORS(allows_lazy_compilation)

  // Indicates how many full GCs this function has survived with assigned
  // code object. Used to determine when it is relatively safe to flush
  // this code object and replace it with lazy compilation stub.
  // Age is reset when GC notices that the code object is referenced
  // from the stack or compilation cache.
  inline int code_age();
  inline void set_code_age(int age);

  // Indicates whether optimizations have been disabled for this
  // shared function info. If a function is repeatedly optimized or if
  // we cannot optimize the function we disable optimization to avoid
  // spending time attempting to optimize it again.
  DECL_BOOLEAN_ACCESSORS(optimization_disabled)

  // Indicates whether the function is a strict mode function.
  DECL_BOOLEAN_ACCESSORS(strict_mode)

  // False if the function definitely does not allocate an arguments object.
  DECL_BOOLEAN_ACCESSORS(uses_arguments)

  // True if the function has any duplicated parameter names.
  DECL_BOOLEAN_ACCESSORS(has_duplicate_parameters)

  // Indicates whether the function is a native function.
  // These needs special threatment in .call and .apply since
  // null passed as the receiver should not be translated to the
  // global object.
  inline bool native();
  inline void set_native(bool value);

  // Indicates whether the function is a bound function created using
  // the bind function.
  inline bool bound();
  inline void set_bound(bool value);

  // Indicates whether or not the code in the shared function support
  // deoptimization.
  inline bool has_deoptimization_support();

  // Enable deoptimization support through recompiled code.
  void EnableDeoptimizationSupport(Code* recompiled);

  // Disable (further) attempted optimization of all functions sharing this
  // shared function info.  The function is the one we actually tried to
  // optimize.
  void DisableOptimization(JSFunction* function);

  // Lookup the bailout ID and ASSERT that it exists in the non-optimized
  // code, returns whether it asserted (i.e., always true if assertions are
  // disabled).
  bool VerifyBailoutId(int id);

  // Check whether a inlined constructor can be generated with the given
  // prototype.
  bool CanGenerateInlineConstructor(Object* prototype);

  // Prevents further attempts to generate inline constructors.
  // To be called if generation failed for any reason.
  void ForbidInlineConstructor();

  // For functions which only contains this property assignments this provides
  // access to the names for the properties assigned.
  DECL_ACCESSORS(this_property_assignments, Object)
  inline int this_property_assignments_count();
  inline void set_this_property_assignments_count(int value);
  String* GetThisPropertyAssignmentName(int index);
  bool IsThisPropertyAssignmentArgument(int index);
  int GetThisPropertyAssignmentArgument(int index);
  Object* GetThisPropertyAssignmentConstant(int index);

  // [source code]: Source code for the function.
  bool HasSourceCode();
  Object* GetSourceCode();

  inline int opt_count();
  inline void set_opt_count(int opt_count);

  // Source size of this function.
  int SourceSize();

  // Calculate the instance size.
  int CalculateInstanceSize();

  // Calculate the number of in-object properties.
  int CalculateInObjectProperties();

  // Dispatched behavior.
  // Set max_length to -1 for unlimited length.
  void SourceCodePrint(StringStream* accumulator, int max_length);
#ifdef OBJECT_PRINT
  inline void SharedFunctionInfoPrint() {
    SharedFunctionInfoPrint(stdout);
  }
  void SharedFunctionInfoPrint(FILE* out);
#endif
#ifdef DEBUG
  void SharedFunctionInfoVerify();
#endif

  // Casting.
  static inline SharedFunctionInfo* cast(Object* obj);

  // Constants.
  static const int kDontAdaptArgumentsSentinel = -1;

  // Layout description.
  // Pointer fields.
  static const int kNameOffset = HeapObject::kHeaderSize;
  static const int kCodeOffset = kNameOffset + kPointerSize;
  static const int kScopeInfoOffset = kCodeOffset + kPointerSize;
  static const int kConstructStubOffset = kScopeInfoOffset + kPointerSize;
  static const int kInstanceClassNameOffset =
      kConstructStubOffset + kPointerSize;
  static const int kFunctionDataOffset =
      kInstanceClassNameOffset + kPointerSize;
  static const int kScriptOffset = kFunctionDataOffset + kPointerSize;
  static const int kDebugInfoOffset = kScriptOffset + kPointerSize;
  static const int kInferredNameOffset = kDebugInfoOffset + kPointerSize;
  static const int kInitialMapOffset =
      kInferredNameOffset + kPointerSize;
  static const int kThisPropertyAssignmentsOffset =
      kInitialMapOffset + kPointerSize;
  static const int kDeoptCounterOffset =
      kThisPropertyAssignmentsOffset + kPointerSize;
#if V8_HOST_ARCH_32_BIT
  // Smi fields.
  static const int kLengthOffset =
      kDeoptCounterOffset + kPointerSize;
  static const int kFormalParameterCountOffset = kLengthOffset + kPointerSize;
  static const int kExpectedNofPropertiesOffset =
      kFormalParameterCountOffset + kPointerSize;
  static const int kNumLiteralsOffset =
      kExpectedNofPropertiesOffset + kPointerSize;
  static const int kStartPositionAndTypeOffset =
      kNumLiteralsOffset + kPointerSize;
  static const int kEndPositionOffset =
      kStartPositionAndTypeOffset + kPointerSize;
  static const int kFunctionTokenPositionOffset =
      kEndPositionOffset + kPointerSize;
  static const int kCompilerHintsOffset =
      kFunctionTokenPositionOffset + kPointerSize;
  static const int kThisPropertyAssignmentsCountOffset =
      kCompilerHintsOffset + kPointerSize;
  static const int kOptCountOffset =
      kThisPropertyAssignmentsCountOffset + kPointerSize;
  // Total size.
  static const int kSize = kOptCountOffset + kPointerSize;
#else
  // The only reason to use smi fields instead of int fields
  // is to allow iteration without maps decoding during
  // garbage collections.
  // To avoid wasting space on 64-bit architectures we use
  // the following trick: we group integer fields into pairs
  // First integer in each pair is shifted left by 1.
  // By doing this we guarantee that LSB of each kPointerSize aligned
  // word is not set and thus this word cannot be treated as pointer
  // to HeapObject during old space traversal.
  static const int kLengthOffset =
      kDeoptCounterOffset + kPointerSize;
  static const int kFormalParameterCountOffset =
      kLengthOffset + kIntSize;

  static const int kExpectedNofPropertiesOffset =
      kFormalParameterCountOffset + kIntSize;
  static const int kNumLiteralsOffset =
      kExpectedNofPropertiesOffset + kIntSize;

  static const int kEndPositionOffset =
      kNumLiteralsOffset + kIntSize;
  static const int kStartPositionAndTypeOffset =
      kEndPositionOffset + kIntSize;

  static const int kFunctionTokenPositionOffset =
      kStartPositionAndTypeOffset + kIntSize;
  static const int kCompilerHintsOffset =
      kFunctionTokenPositionOffset + kIntSize;

  static const int kThisPropertyAssignmentsCountOffset =
      kCompilerHintsOffset + kIntSize;
  static const int kOptCountOffset =
      kThisPropertyAssignmentsCountOffset + kIntSize;

  // Total size.
  static const int kSize = kOptCountOffset + kIntSize;

#endif

  // The construction counter for inobject slack tracking is stored in the
  // most significant byte of compiler_hints which is otherwise unused.
  // Its offset depends on the endian-ness of the architecture.
#if __BYTE_ORDER == __LITTLE_ENDIAN
  static const int kConstructionCountOffset = kCompilerHintsOffset + 3;
#elif __BYTE_ORDER == __BIG_ENDIAN
  static const int kConstructionCountOffset = kCompilerHintsOffset + 0;
#else
#error Unknown byte ordering
#endif

  static const int kAlignedSize = POINTER_SIZE_ALIGN(kSize);

  typedef FixedBodyDescriptor<kNameOffset,
                              kThisPropertyAssignmentsOffset + kPointerSize,
                              kSize> BodyDescriptor;

  // Bit positions in start_position_and_type.
  // The source code start position is in the 30 most significant bits of
  // the start_position_and_type field.
  static const int kIsExpressionBit = 0;
  static const int kIsTopLevelBit   = 1;
  static const int kStartPositionShift = 2;
  static const int kStartPositionMask = ~((1 << kStartPositionShift) - 1);

  // Bit positions in compiler_hints.
  static const int kCodeAgeSize = 3;
  static const int kCodeAgeMask = (1 << kCodeAgeSize) - 1;
  static const int kBoundFunction = 9;

  enum CompilerHints {
    kHasOnlySimpleThisPropertyAssignments,
    kAllowLazyCompilation,
    kLiveObjectsMayExist,
    kCodeAgeShift,
    kOptimizationDisabled = kCodeAgeShift + kCodeAgeSize,
    kStrictModeFunction,
    kUsesArguments,
    kHasDuplicateParameters,
    kNative
  };

 private:
#if V8_HOST_ARCH_32_BIT
  // On 32 bit platforms, compiler hints is a smi.
  static const int kCompilerHintsSmiTagSize = kSmiTagSize;
  static const int kCompilerHintsSize = kPointerSize;
#else
  // On 64 bit platforms, compiler hints is not a smi, see comment above.
  static const int kCompilerHintsSmiTagSize = 0;
  static const int kCompilerHintsSize = kIntSize;
#endif

 public:
  // Constants for optimizing codegen for strict mode function and
  // native tests.
  // Allows to use byte-widgh instructions.
  static const int kStrictModeBitWithinByte =
      (kStrictModeFunction + kCompilerHintsSmiTagSize) % kBitsPerByte;

  static const int kNativeBitWithinByte =
      (kNative + kCompilerHintsSmiTagSize) % kBitsPerByte;

#if __BYTE_ORDER == __LITTLE_ENDIAN
  static const int kStrictModeByteOffset = kCompilerHintsOffset +
      (kStrictModeFunction + kCompilerHintsSmiTagSize) / kBitsPerByte;
  static const int kNativeByteOffset = kCompilerHintsOffset +
      (kNative + kCompilerHintsSmiTagSize) / kBitsPerByte;
#elif __BYTE_ORDER == __BIG_ENDIAN
  static const int kStrictModeByteOffset = kCompilerHintsOffset +
      (kCompilerHintsSize - 1) -
      ((kStrictModeFunction + kCompilerHintsSmiTagSize) / kBitsPerByte);
  static const int kNativeByteOffset = kCompilerHintsOffset +
      (kCompilerHintsSize - 1) -
      ((kNative + kCompilerHintsSmiTagSize) / kBitsPerByte);
#else
#error Unknown byte ordering
#endif

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(SharedFunctionInfo);
};


// JSFunction describes JavaScript functions.
class JSFunction: public JSObject {
 public:
  // [prototype_or_initial_map]:
  DECL_ACCESSORS(prototype_or_initial_map, Object)

  // [shared_function_info]: The information about the function that
  // can be shared by instances.
  DECL_ACCESSORS(shared, SharedFunctionInfo)

  inline SharedFunctionInfo* unchecked_shared();

  // [context]: The context for this function.
  inline Context* context();
  inline Object* unchecked_context();
  inline void set_context(Object* context);

  // [code]: The generated code object for this function.  Executed
  // when the function is invoked, e.g. foo() or new foo(). See
  // [[Call]] and [[Construct]] description in ECMA-262, section
  // 8.6.2, page 27.
  inline Code* code();
  inline void set_code(Code* code);
  inline void ReplaceCode(Code* code);

  inline Code* unchecked_code();

  // Tells whether this function is builtin.
  inline bool IsBuiltin();

  // Tells whether or not the function needs arguments adaption.
  inline bool NeedsArgumentsAdaption();

  // Tells whether or not this function has been optimized.
  inline bool IsOptimized();

  // Tells whether or not this function can be optimized.
  inline bool IsOptimizable();

  // Mark this function for lazy recompilation. The function will be
  // recompiled the next time it is executed.
  void MarkForLazyRecompilation();

  // Tells whether or not the function is already marked for lazy
  // recompilation.
  inline bool IsMarkedForLazyRecompilation();

  // Check whether or not this function is inlineable.
  bool IsInlineable();

  // [literals]: Fixed array holding the materialized literals.
  //
  // If the function contains object, regexp or array literals, the
  // literals array prefix contains the object, regexp, and array
  // function to be used when creating these literals.  This is
  // necessary so that we do not dynamically lookup the object, regexp
  // or array functions.  Performing a dynamic lookup, we might end up
  // using the functions from a new context that we should not have
  // access to.
  DECL_ACCESSORS(literals, FixedArray)

  // The initial map for an object created by this constructor.
  inline Map* initial_map();
  inline void set_initial_map(Map* value);
  inline bool has_initial_map();

  // Get and set the prototype property on a JSFunction. If the
  // function has an initial map the prototype is set on the initial
  // map. Otherwise, the prototype is put in the initial map field
  // until an initial map is needed.
  inline bool has_prototype();
  inline bool has_instance_prototype();
  inline Object* prototype();
  inline Object* instance_prototype();
  Object* SetInstancePrototype(Object* value);
  MUST_USE_RESULT MaybeObject* SetPrototype(Object* value);

  // After prototype is removed, it will not be created when accessed, and
  // [[Construct]] from this function will not be allowed.
  Object* RemovePrototype();
  inline bool should_have_prototype();

  // Accessor for this function's initial map's [[class]]
  // property. This is primarily used by ECMA native functions.  This
  // method sets the class_name field of this function's initial map
  // to a given value. It creates an initial map if this function does
  // not have one. Note that this method does not copy the initial map
  // if it has one already, but simply replaces it with the new value.
  // Instances created afterwards will have a map whose [[class]] is
  // set to 'value', but there is no guarantees on instances created
  // before.
  Object* SetInstanceClassName(String* name);

  // Returns if this function has been compiled to native code yet.
  inline bool is_compiled();

  // [next_function_link]: Field for linking functions. This list is treated as
  // a weak list by the GC.
  DECL_ACCESSORS(next_function_link, Object)

  // Prints the name of the function using PrintF.
  inline void PrintName() {
    PrintName(stdout);
  }
  void PrintName(FILE* out);

  // Casting.
  static inline JSFunction* cast(Object* obj);

  // Iterates the objects, including code objects indirectly referenced
  // through pointers to the first instruction in the code object.
  void JSFunctionIterateBody(int object_size, ObjectVisitor* v);

  // Dispatched behavior.
#ifdef OBJECT_PRINT
  inline void JSFunctionPrint() {
    JSFunctionPrint(stdout);
  }
  void JSFunctionPrint(FILE* out);
#endif
#ifdef DEBUG
  void JSFunctionVerify();
#endif

  // Returns the number of allocated literals.
  inline int NumberOfLiterals();

  // Retrieve the global context from a function's literal array.
  static Context* GlobalContextFromLiterals(FixedArray* literals);

  // Layout descriptors. The last property (from kNonWeakFieldsEndOffset to
  // kSize) is weak and has special handling during garbage collection.
  static const int kCodeEntryOffset = JSObject::kHeaderSize;
  static const int kPrototypeOrInitialMapOffset =
      kCodeEntryOffset + kPointerSize;
  static const int kSharedFunctionInfoOffset =
      kPrototypeOrInitialMapOffset + kPointerSize;
  static const int kContextOffset = kSharedFunctionInfoOffset + kPointerSize;
  static const int kLiteralsOffset = kContextOffset + kPointerSize;
  static const int kNonWeakFieldsEndOffset = kLiteralsOffset + kPointerSize;
  static const int kNextFunctionLinkOffset = kNonWeakFieldsEndOffset;
  static const int kSize = kNextFunctionLinkOffset + kPointerSize;

  // Layout of the literals array.
  static const int kLiteralsPrefixSize = 1;
  static const int kLiteralGlobalContextIndex = 0;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSFunction);
};


// JSGlobalProxy's prototype must be a JSGlobalObject or null,
// and the prototype is hidden. JSGlobalProxy always delegates
// property accesses to its prototype if the prototype is not null.
//
// A JSGlobalProxy can be reinitialized which will preserve its identity.
//
// Accessing a JSGlobalProxy requires security check.

class JSGlobalProxy : public JSObject {
 public:
  // [context]: the owner global context of this global proxy object.
  // It is null value if this object is not used by any context.
  DECL_ACCESSORS(context, Object)

  // Casting.
  static inline JSGlobalProxy* cast(Object* obj);

  // Dispatched behavior.
#ifdef OBJECT_PRINT
  inline void JSGlobalProxyPrint() {
    JSGlobalProxyPrint(stdout);
  }
  void JSGlobalProxyPrint(FILE* out);
#endif
#ifdef DEBUG
  void JSGlobalProxyVerify();
#endif

  // Layout description.
  static const int kContextOffset = JSObject::kHeaderSize;
  static const int kSize = kContextOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSGlobalProxy);
};


// Forward declaration.
class JSBuiltinsObject;
class JSGlobalPropertyCell;

// Common super class for JavaScript global objects and the special
// builtins global objects.
class GlobalObject: public JSObject {
 public:
  // [builtins]: the object holding the runtime routines written in JS.
  DECL_ACCESSORS(builtins, JSBuiltinsObject)

  // [global context]: the global context corresponding to this global object.
  DECL_ACCESSORS(global_context, Context)

  // [global receiver]: the global receiver object of the context
  DECL_ACCESSORS(global_receiver, JSObject)

  // Retrieve the property cell used to store a property.
  JSGlobalPropertyCell* GetPropertyCell(LookupResult* result);

  // This is like GetProperty, but is used when you know the lookup won't fail
  // by throwing an exception.  This is for the debug and builtins global
  // objects, where it is known which properties can be expected to be present
  // on the object.
  Object* GetPropertyNoExceptionThrown(String* key) {
    Object* answer = GetProperty(key)->ToObjectUnchecked();
    return answer;
  }

  // Ensure that the global object has a cell for the given property name.
  MUST_USE_RESULT MaybeObject* EnsurePropertyCell(String* name);

  // Casting.
  static inline GlobalObject* cast(Object* obj);

  // Layout description.
  static const int kBuiltinsOffset = JSObject::kHeaderSize;
  static const int kGlobalContextOffset = kBuiltinsOffset + kPointerSize;
  static const int kGlobalReceiverOffset = kGlobalContextOffset + kPointerSize;
  static const int kHeaderSize = kGlobalReceiverOffset + kPointerSize;

 private:
  friend class AGCCVersionRequiresThisClassToHaveAFriendSoHereItIs;

  DISALLOW_IMPLICIT_CONSTRUCTORS(GlobalObject);
};


// JavaScript global object.
class JSGlobalObject: public GlobalObject {
 public:
  // Casting.
  static inline JSGlobalObject* cast(Object* obj);

  // Dispatched behavior.
#ifdef OBJECT_PRINT
  inline void JSGlobalObjectPrint() {
    JSGlobalObjectPrint(stdout);
  }
  void JSGlobalObjectPrint(FILE* out);
#endif
#ifdef DEBUG
  void JSGlobalObjectVerify();
#endif

  // Layout description.
  static const int kSize = GlobalObject::kHeaderSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSGlobalObject);
};


// Builtins global object which holds the runtime routines written in
// JavaScript.
class JSBuiltinsObject: public GlobalObject {
 public:
  // Accessors for the runtime routines written in JavaScript.
  inline Object* javascript_builtin(Builtins::JavaScript id);
  inline void set_javascript_builtin(Builtins::JavaScript id, Object* value);

  // Accessors for code of the runtime routines written in JavaScript.
  inline Code* javascript_builtin_code(Builtins::JavaScript id);
  inline void set_javascript_builtin_code(Builtins::JavaScript id, Code* value);

  // Casting.
  static inline JSBuiltinsObject* cast(Object* obj);

  // Dispatched behavior.
#ifdef OBJECT_PRINT
  inline void JSBuiltinsObjectPrint() {
    JSBuiltinsObjectPrint(stdout);
  }
  void JSBuiltinsObjectPrint(FILE* out);
#endif
#ifdef DEBUG
  void JSBuiltinsObjectVerify();
#endif

  // Layout description.  The size of the builtins object includes
  // room for two pointers per runtime routine written in javascript
  // (function and code object).
  static const int kJSBuiltinsCount = Builtins::id_count;
  static const int kJSBuiltinsOffset = GlobalObject::kHeaderSize;
  static const int kJSBuiltinsCodeOffset =
      GlobalObject::kHeaderSize + (kJSBuiltinsCount * kPointerSize);
  static const int kSize =
      kJSBuiltinsCodeOffset + (kJSBuiltinsCount * kPointerSize);

  static int OffsetOfFunctionWithId(Builtins::JavaScript id) {
    return kJSBuiltinsOffset + id * kPointerSize;
  }

  static int OffsetOfCodeWithId(Builtins::JavaScript id) {
    return kJSBuiltinsCodeOffset + id * kPointerSize;
  }

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSBuiltinsObject);
};


// Representation for JS Wrapper objects, String, Number, Boolean, Date, etc.
class JSValue: public JSObject {
 public:
  // [value]: the object being wrapped.
  DECL_ACCESSORS(value, Object)

  // Casting.
  static inline JSValue* cast(Object* obj);

  // Dispatched behavior.
#ifdef OBJECT_PRINT
  inline void JSValuePrint() {
    JSValuePrint(stdout);
  }
  void JSValuePrint(FILE* out);
#endif
#ifdef DEBUG
  void JSValueVerify();
#endif

  // Layout description.
  static const int kValueOffset = JSObject::kHeaderSize;
  static const int kSize = kValueOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSValue);
};


// Representation of message objects used for error reporting through
// the API. The messages are formatted in JavaScript so this object is
// a real JavaScript object. The information used for formatting the
// error messages are not directly accessible from JavaScript to
// prevent leaking information to user code called during error
// formatting.
class JSMessageObject: public JSObject {
 public:
  // [type]: the type of error message.
  DECL_ACCESSORS(type, String)

  // [arguments]: the arguments for formatting the error message.
  DECL_ACCESSORS(arguments, JSArray)

  // [script]: the script from which the error message originated.
  DECL_ACCESSORS(script, Object)

  // [stack_trace]: the stack trace for this error message.
  DECL_ACCESSORS(stack_trace, Object)

  // [stack_frames]: an array of stack frames for this error object.
  DECL_ACCESSORS(stack_frames, Object)

  // [start_position]: the start position in the script for the error message.
  inline int start_position();
  inline void set_start_position(int value);

  // [end_position]: the end position in the script for the error message.
  inline int end_position();
  inline void set_end_position(int value);

  // Casting.
  static inline JSMessageObject* cast(Object* obj);

  // Dispatched behavior.
#ifdef OBJECT_PRINT
  inline void JSMessageObjectPrint() {
    JSMessageObjectPrint(stdout);
  }
  void JSMessageObjectPrint(FILE* out);
#endif
#ifdef DEBUG
  void JSMessageObjectVerify();
#endif

  // Layout description.
  static const int kTypeOffset = JSObject::kHeaderSize;
  static const int kArgumentsOffset = kTypeOffset + kPointerSize;
  static const int kScriptOffset = kArgumentsOffset + kPointerSize;
  static const int kStackTraceOffset = kScriptOffset + kPointerSize;
  static const int kStackFramesOffset = kStackTraceOffset + kPointerSize;
  static const int kStartPositionOffset = kStackFramesOffset + kPointerSize;
  static const int kEndPositionOffset = kStartPositionOffset + kPointerSize;
  static const int kSize = kEndPositionOffset + kPointerSize;

  typedef FixedBodyDescriptor<HeapObject::kMapOffset,
                              kStackFramesOffset + kPointerSize,
                              kSize> BodyDescriptor;
};


// Regular expressions
// The regular expression holds a single reference to a FixedArray in
// the kDataOffset field.
// The FixedArray contains the following data:
// - tag : type of regexp implementation (not compiled yet, atom or irregexp)
// - reference to the original source string
// - reference to the original flag string
// If it is an atom regexp
// - a reference to a literal string to search for
// If it is an irregexp regexp:
// - a reference to code for ASCII inputs (bytecode or compiled), or a smi
// used for tracking the last usage (used for code flushing).
// - a reference to code for UC16 inputs (bytecode or compiled), or a smi
// used for tracking the last usage (used for code flushing)..
// - max number of registers used by irregexp implementations.
// - number of capture registers (output values) of the regexp.
class JSRegExp: public JSObject {
 public:
  // Meaning of Type:
  // NOT_COMPILED: Initial value. No data has been stored in the JSRegExp yet.
  // ATOM: A simple string to match against using an indexOf operation.
  // IRREGEXP: Compiled with Irregexp.
  // IRREGEXP_NATIVE: Compiled to native code with Irregexp.
  enum Type { NOT_COMPILED, ATOM, IRREGEXP };
  enum Flag { NONE = 0, GLOBAL = 1, IGNORE_CASE = 2, MULTILINE = 4 };

  class Flags {
   public:
    explicit Flags(uint32_t value) : value_(value) { }
    bool is_global() { return (value_ & GLOBAL) != 0; }
    bool is_ignore_case() { return (value_ & IGNORE_CASE) != 0; }
    bool is_multiline() { return (value_ & MULTILINE) != 0; }
    uint32_t value() { return value_; }
   private:
    uint32_t value_;
  };

  DECL_ACCESSORS(data, Object)

  inline Type TypeTag();
  inline int CaptureCount();
  inline Flags GetFlags();
  inline String* Pattern();
  inline Object* DataAt(int index);
  // Set implementation data after the object has been prepared.
  inline void SetDataAt(int index, Object* value);

  // Used during GC when flushing code or setting age.
  inline Object* DataAtUnchecked(int index);
  inline void SetDataAtUnchecked(int index, Object* value, Heap* heap);
  inline Type TypeTagUnchecked();

  static int code_index(bool is_ascii) {
    if (is_ascii) {
      return kIrregexpASCIICodeIndex;
    } else {
      return kIrregexpUC16CodeIndex;
    }
  }

  static int saved_code_index(bool is_ascii) {
    if (is_ascii) {
      return kIrregexpASCIICodeSavedIndex;
    } else {
      return kIrregexpUC16CodeSavedIndex;
    }
  }

  static inline JSRegExp* cast(Object* obj);

  // Dispatched behavior.
#ifdef DEBUG
  void JSRegExpVerify();
#endif

  static const int kDataOffset = JSObject::kHeaderSize;
  static const int kSize = kDataOffset + kPointerSize;

  // Indices in the data array.
  static const int kTagIndex = 0;
  static const int kSourceIndex = kTagIndex + 1;
  static const int kFlagsIndex = kSourceIndex + 1;
  static const int kDataIndex = kFlagsIndex + 1;
  // The data fields are used in different ways depending on the
  // value of the tag.
  // Atom regexps (literal strings).
  static const int kAtomPatternIndex = kDataIndex;

  static const int kAtomDataSize = kAtomPatternIndex + 1;

  // Irregexp compiled code or bytecode for ASCII. If compilation
  // fails, this fields hold an exception object that should be
  // thrown if the regexp is used again.
  static const int kIrregexpASCIICodeIndex = kDataIndex;
  // Irregexp compiled code or bytecode for UC16.  If compilation
  // fails, this fields hold an exception object that should be
  // thrown if the regexp is used again.
  static const int kIrregexpUC16CodeIndex = kDataIndex + 1;

  // Saved instance of Irregexp compiled code or bytecode for ASCII that
  // is a potential candidate for flushing.
  static const int kIrregexpASCIICodeSavedIndex = kDataIndex + 2;
  // Saved instance of Irregexp compiled code or bytecode for UC16 that is
  // a potential candidate for flushing.
  static const int kIrregexpUC16CodeSavedIndex = kDataIndex + 3;

  // Maximal number of registers used by either ASCII or UC16.
  // Only used to check that there is enough stack space
  static const int kIrregexpMaxRegisterCountIndex = kDataIndex + 4;
  // Number of captures in the compiled regexp.
  static const int kIrregexpCaptureCountIndex = kDataIndex + 5;

  static const int kIrregexpDataSize = kIrregexpCaptureCountIndex + 1;

  // Offsets directly into the data fixed array.
  static const int kDataTagOffset =
      FixedArray::kHeaderSize + kTagIndex * kPointerSize;
  static const int kDataAsciiCodeOffset =
      FixedArray::kHeaderSize + kIrregexpASCIICodeIndex * kPointerSize;
  static const int kDataUC16CodeOffset =
      FixedArray::kHeaderSize + kIrregexpUC16CodeIndex * kPointerSize;
  static const int kIrregexpCaptureCountOffset =
      FixedArray::kHeaderSize + kIrregexpCaptureCountIndex * kPointerSize;

  // In-object fields.
  static const int kSourceFieldIndex = 0;
  static const int kGlobalFieldIndex = 1;
  static const int kIgnoreCaseFieldIndex = 2;
  static const int kMultilineFieldIndex = 3;
  static const int kLastIndexFieldIndex = 4;
  static const int kInObjectFieldCount = 5;

  // The uninitialized value for a regexp code object.
  static const int kUninitializedValue = -1;

  // The compilation error value for the regexp code object. The real error
  // object is in the saved code field.
  static const int kCompilationErrorValue = -2;

  // When we store the sweep generation at which we moved the code from the
  // code index to the saved code index we mask it of to be in the [0:255]
  // range.
  static const int kCodeAgeMask = 0xff;
};


class CompilationCacheShape {
 public:
  static inline bool IsMatch(HashTableKey* key, Object* value) {
    return key->IsMatch(value);
  }

  static inline uint32_t Hash(HashTableKey* key) {
    return key->Hash();
  }

  static inline uint32_t HashForObject(HashTableKey* key, Object* object) {
    return key->HashForObject(object);
  }

  MUST_USE_RESULT static MaybeObject* AsObject(HashTableKey* key) {
    return key->AsObject();
  }

  static const int kPrefixSize = 0;
  static const int kEntrySize = 2;
};


class CompilationCacheTable: public HashTable<CompilationCacheShape,
                                              HashTableKey*> {
 public:
  // Find cached value for a string key, otherwise return null.
  Object* Lookup(String* src);
  Object* LookupEval(String* src, Context* context, StrictModeFlag strict_mode);
  Object* LookupRegExp(String* source, JSRegExp::Flags flags);
  MaybeObject* Put(String* src, Object* value);
  MaybeObject* PutEval(String* src,
                       Context* context,
                       SharedFunctionInfo* value);
  MaybeObject* PutRegExp(String* src, JSRegExp::Flags flags, FixedArray* value);

  // Remove given value from cache.
  void Remove(Object* value);

  static inline CompilationCacheTable* cast(Object* obj);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(CompilationCacheTable);
};


class CodeCache: public Struct {
 public:
  DECL_ACCESSORS(default_cache, FixedArray)
  DECL_ACCESSORS(normal_type_cache, Object)

  // Add the code object to the cache.
  MUST_USE_RESULT MaybeObject* Update(String* name, Code* code);

  // Lookup code object in the cache. Returns code object if found and undefined
  // if not.
  Object* Lookup(String* name, Code::Flags flags);

  // Get the internal index of a code object in the cache. Returns -1 if the
  // code object is not in that cache. This index can be used to later call
  // RemoveByIndex. The cache cannot be modified between a call to GetIndex and
  // RemoveByIndex.
  int GetIndex(Object* name, Code* code);

  // Remove an object from the cache with the provided internal index.
  void RemoveByIndex(Object* name, Code* code, int index);

  static inline CodeCache* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void CodeCachePrint() {
    CodeCachePrint(stdout);
  }
  void CodeCachePrint(FILE* out);
#endif
#ifdef DEBUG
  void CodeCacheVerify();
#endif

  static const int kDefaultCacheOffset = HeapObject::kHeaderSize;
  static const int kNormalTypeCacheOffset =
      kDefaultCacheOffset + kPointerSize;
  static const int kSize = kNormalTypeCacheOffset + kPointerSize;

 private:
  MUST_USE_RESULT MaybeObject* UpdateDefaultCache(String* name, Code* code);
  MUST_USE_RESULT MaybeObject* UpdateNormalTypeCache(String* name, Code* code);
  Object* LookupDefaultCache(String* name, Code::Flags flags);
  Object* LookupNormalTypeCache(String* name, Code::Flags flags);

  // Code cache layout of the default cache. Elements are alternating name and
  // code objects for non normal load/store/call IC's.
  static const int kCodeCacheEntrySize = 2;
  static const int kCodeCacheEntryNameOffset = 0;
  static const int kCodeCacheEntryCodeOffset = 1;

  DISALLOW_IMPLICIT_CONSTRUCTORS(CodeCache);
};


class CodeCacheHashTableShape {
 public:
  static inline bool IsMatch(HashTableKey* key, Object* value) {
    return key->IsMatch(value);
  }

  static inline uint32_t Hash(HashTableKey* key) {
    return key->Hash();
  }

  static inline uint32_t HashForObject(HashTableKey* key, Object* object) {
    return key->HashForObject(object);
  }

  MUST_USE_RESULT static MaybeObject* AsObject(HashTableKey* key) {
    return key->AsObject();
  }

  static const int kPrefixSize = 0;
  static const int kEntrySize = 2;
};


class CodeCacheHashTable: public HashTable<CodeCacheHashTableShape,
                                           HashTableKey*> {
 public:
  Object* Lookup(String* name, Code::Flags flags);
  MUST_USE_RESULT MaybeObject* Put(String* name, Code* code);

  int GetIndex(String* name, Code::Flags flags);
  void RemoveByIndex(int index);

  static inline CodeCacheHashTable* cast(Object* obj);

  // Initial size of the fixed array backing the hash table.
  static const int kInitialSize = 64;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(CodeCacheHashTable);
};


class PolymorphicCodeCache: public Struct {
 public:
  DECL_ACCESSORS(cache, Object)

  MUST_USE_RESULT MaybeObject* Update(MapList* maps,
                                      Code::Flags flags,
                                      Code* code);
  Object* Lookup(MapList* maps, Code::Flags flags);

  static inline PolymorphicCodeCache* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void PolymorphicCodeCachePrint() {
    PolymorphicCodeCachePrint(stdout);
  }
  void PolymorphicCodeCachePrint(FILE* out);
#endif
#ifdef DEBUG
  void PolymorphicCodeCacheVerify();
#endif

  static const int kCacheOffset = HeapObject::kHeaderSize;
  static const int kSize = kCacheOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(PolymorphicCodeCache);
};


class PolymorphicCodeCacheHashTable
    : public HashTable<CodeCacheHashTableShape, HashTableKey*> {
 public:
  Object* Lookup(MapList* maps, int code_kind);
  MUST_USE_RESULT MaybeObject* Put(MapList* maps, int code_kind, Code* code);

  static inline PolymorphicCodeCacheHashTable* cast(Object* obj);

  static const int kInitialSize = 64;
 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(PolymorphicCodeCacheHashTable);
};


enum AllowNullsFlag {ALLOW_NULLS, DISALLOW_NULLS};
enum RobustnessFlag {ROBUST_STRING_TRAVERSAL, FAST_STRING_TRAVERSAL};


class StringHasher {
 public:
  explicit inline StringHasher(int length);

  // Returns true if the hash of this string can be computed without
  // looking at the contents.
  inline bool has_trivial_hash();

  // Add a character to the hash and update the array index calculation.
  inline void AddCharacter(uc32 c);

  // Adds a character to the hash but does not update the array index
  // calculation.  This can only be called when it has been verified
  // that the input is not an array index.
  inline void AddCharacterNoIndex(uc32 c);

  // Returns the value to store in the hash field of a string with
  // the given length and contents.
  uint32_t GetHashField();

  // Returns true if the characters seen so far make up a legal array
  // index.
  bool is_array_index() { return is_array_index_; }

  bool is_valid() { return is_valid_; }

  void invalidate() { is_valid_ = false; }

  // Calculated hash value for a string consisting of 1 to
  // String::kMaxArrayIndexSize digits with no leading zeros (except "0").
  // value is represented decimal value.
  static uint32_t MakeArrayIndexHash(uint32_t value, int length);

 private:
  uint32_t array_index() {
    ASSERT(is_array_index());
    return array_index_;
  }

  inline uint32_t GetHash();

  int length_;
  uint32_t raw_running_hash_;
  uint32_t array_index_;
  bool is_array_index_;
  bool is_first_char_;
  bool is_valid_;
  friend class TwoCharHashTableKey;
};


// Calculates string hash.
template <typename schar>
inline uint32_t HashSequentialString(const schar* chars, int length);


// The characteristics of a string are stored in its map.  Retrieving these
// few bits of information is moderately expensive, involving two memory
// loads where the second is dependent on the first.  To improve efficiency
// the shape of the string is given its own class so that it can be retrieved
// once and used for several string operations.  A StringShape is small enough
// to be passed by value and is immutable, but be aware that flattening a
// string can potentially alter its shape.  Also be aware that a GC caused by
// something else can alter the shape of a string due to ConsString
// shortcutting.  Keeping these restrictions in mind has proven to be error-
// prone and so we no longer put StringShapes in variables unless there is a
// concrete performance benefit at that particular point in the code.
class StringShape BASE_EMBEDDED {
 public:
  inline explicit StringShape(String* s);
  inline explicit StringShape(Map* s);
  inline explicit StringShape(InstanceType t);
  inline bool IsSequential();
  inline bool IsExternal();
  inline bool IsCons();
  inline bool IsExternalAscii();
  inline bool IsExternalTwoByte();
  inline bool IsSequentialAscii();
  inline bool IsSequentialTwoByte();
  inline bool IsSymbol();
  inline StringRepresentationTag representation_tag();
  inline uint32_t full_representation_tag();
  inline uint32_t size_tag();
#ifdef DEBUG
  inline uint32_t type() { return type_; }
  inline void invalidate() { valid_ = false; }
  inline bool valid() { return valid_; }
#else
  inline void invalidate() { }
#endif

 private:
  uint32_t type_;
#ifdef DEBUG
  inline void set_valid() { valid_ = true; }
  bool valid_;
#else
  inline void set_valid() { }
#endif
};


// The String abstract class captures JavaScript string values:
//
// Ecma-262:
//  4.3.16 String Value
//    A string value is a member of the type String and is a finite
//    ordered sequence of zero or more 16-bit unsigned integer values.
//
// All string values have a length field.
class String: public HeapObject {
 public:
  // Get and set the length of the string.
  inline int length();
  inline void set_length(int value);

  // Get and set the hash field of the string.
  inline uint32_t hash_field();
  inline void set_hash_field(uint32_t value);

  inline bool IsAsciiRepresentation();
  inline bool IsTwoByteRepresentation();

  // Returns whether this string has ascii chars, i.e. all of them can
  // be ascii encoded.  This might be the case even if the string is
  // two-byte.  Such strings may appear when the embedder prefers
  // two-byte external representations even for ascii data.
  //
  // NOTE: this should be considered only a hint.  False negatives are
  // possible.
  inline bool HasOnlyAsciiChars();

  // Get and set individual two byte chars in the string.
  inline void Set(int index, uint16_t value);
  // Get individual two byte char in the string.  Repeated calls
  // to this method are not efficient unless the string is flat.
  inline uint16_t Get(int index);

  // Try to flatten the string.  Checks first inline to see if it is
  // necessary.  Does nothing if the string is not a cons string.
  // Flattening allocates a sequential string with the same data as
  // the given string and mutates the cons string to a degenerate
  // form, where the first component is the new sequential string and
  // the second component is the empty string.  If allocation fails,
  // this function returns a failure.  If flattening succeeds, this
  // function returns the sequential string that is now the first
  // component of the cons string.
  //
  // Degenerate cons strings are handled specially by the garbage
  // collector (see IsShortcutCandidate).
  //
  // Use FlattenString from Handles.cc to flatten even in case an
  // allocation failure happens.
  inline MaybeObject* TryFlatten(PretenureFlag pretenure = NOT_TENURED);

  // Convenience function.  Has exactly the same behavior as
  // TryFlatten(), except in the case of failure returns the original
  // string.
  inline String* TryFlattenGetString(PretenureFlag pretenure = NOT_TENURED);

  Vector<const char> ToAsciiVector();
  Vector<const uc16> ToUC16Vector();

  // Mark the string as an undetectable object. It only applies to
  // ascii and two byte string types.
  bool MarkAsUndetectable();

  // Return a substring.
  MUST_USE_RESULT MaybeObject* SubString(int from,
                                         int to,
                                         PretenureFlag pretenure = NOT_TENURED);

  // String equality operations.
  inline bool Equals(String* other);
  bool IsEqualTo(Vector<const char> str);
  bool IsAsciiEqualTo(Vector<const char> str);
  bool IsTwoByteEqualTo(Vector<const uc16> str);

  // Return a UTF8 representation of the string.  The string is null
  // terminated but may optionally contain nulls.  Length is returned
  // in length_output if length_output is not a null pointer  The string
  // should be nearly flat, otherwise the performance of this method may
  // be very slow (quadratic in the length).  Setting robustness_flag to
  // ROBUST_STRING_TRAVERSAL invokes behaviour that is robust  This means it
  // handles unexpected data without causing assert failures and it does not
  // do any heap allocations.  This is useful when printing stack traces.
  SmartPointer<char> ToCString(AllowNullsFlag allow_nulls,
                               RobustnessFlag robustness_flag,
                               int offset,
                               int length,
                               int* length_output = 0);
  SmartPointer<char> ToCString(
      AllowNullsFlag allow_nulls = DISALLOW_NULLS,
      RobustnessFlag robustness_flag = FAST_STRING_TRAVERSAL,
      int* length_output = 0);

  int Utf8Length();

  // Return a 16 bit Unicode representation of the string.
  // The string should be nearly flat, otherwise the performance of
  // of this method may be very bad.  Setting robustness_flag to
  // ROBUST_STRING_TRAVERSAL invokes behaviour that is robust  This means it
  // handles unexpected data without causing assert failures and it does not
  // do any heap allocations.  This is useful when printing stack traces.
  SmartPointer<uc16> ToWideCString(
      RobustnessFlag robustness_flag = FAST_STRING_TRAVERSAL);

  // Tells whether the hash code has been computed.
  inline bool HasHashCode();

  // Returns a hash value used for the property table
  inline uint32_t Hash();

  static uint32_t ComputeHashField(unibrow::CharacterStream* buffer,
                                   int length);

  static bool ComputeArrayIndex(unibrow::CharacterStream* buffer,
                                uint32_t* index,
                                int length);

  // Externalization.
  bool MakeExternal(v8::String::ExternalStringResource* resource);
  bool MakeExternal(v8::String::ExternalAsciiStringResource* resource);

  // Conversion.
  inline bool AsArrayIndex(uint32_t* index);

  // Casting.
  static inline String* cast(Object* obj);

  void PrintOn(FILE* out);

  // For use during stack traces.  Performs rudimentary sanity check.
  bool LooksValid();

  // Dispatched behavior.
  void StringShortPrint(StringStream* accumulator);
#ifdef OBJECT_PRINT
  inline void StringPrint() {
    StringPrint(stdout);
  }
  void StringPrint(FILE* out);
#endif
#ifdef DEBUG
  void StringVerify();
#endif
  inline bool IsFlat();

  // Layout description.
  static const int kLengthOffset = HeapObject::kHeaderSize;
  static const int kHashFieldOffset = kLengthOffset + kPointerSize;
  static const int kSize = kHashFieldOffset + kPointerSize;

  // Maximum number of characters to consider when trying to convert a string
  // value into an array index.
  static const int kMaxArrayIndexSize = 10;

  // Max ascii char code.
  static const int kMaxAsciiCharCode = unibrow::Utf8::kMaxOneByteChar;
  static const unsigned kMaxAsciiCharCodeU = unibrow::Utf8::kMaxOneByteChar;
  static const int kMaxUC16CharCode = 0xffff;

  // Minimum length for a cons string.
  static const int kMinNonFlatLength = 13;

  // Mask constant for checking if a string has a computed hash code
  // and if it is an array index.  The least significant bit indicates
  // whether a hash code has been computed.  If the hash code has been
  // computed the 2nd bit tells whether the string can be used as an
  // array index.
  static const int kHashNotComputedMask = 1;
  static const int kIsNotArrayIndexMask = 1 << 1;
  static const int kNofHashBitFields = 2;

  // Shift constant retrieving hash code from hash field.
  static const int kHashShift = kNofHashBitFields;

  // Array index strings this short can keep their index in the hash
  // field.
  static const int kMaxCachedArrayIndexLength = 7;

  // For strings which are array indexes the hash value has the string length
  // mixed into the hash, mainly to avoid a hash value of zero which would be
  // the case for the string '0'. 24 bits are used for the array index value.
  static const int kArrayIndexValueBits = 24;
  static const int kArrayIndexLengthBits =
      kBitsPerInt - kArrayIndexValueBits - kNofHashBitFields;

  STATIC_CHECK((kArrayIndexLengthBits > 0));
  STATIC_CHECK(kMaxArrayIndexSize < (1 << kArrayIndexLengthBits));

  static const int kArrayIndexHashLengthShift =
      kArrayIndexValueBits + kNofHashBitFields;

  static const int kArrayIndexHashMask = (1 << kArrayIndexHashLengthShift) - 1;

  static const int kArrayIndexValueMask =
      ((1 << kArrayIndexValueBits) - 1) << kHashShift;

  // Check that kMaxCachedArrayIndexLength + 1 is a power of two so we
  // could use a mask to test if the length of string is less than or equal to
  // kMaxCachedArrayIndexLength.
  STATIC_CHECK(IS_POWER_OF_TWO(kMaxCachedArrayIndexLength + 1));

  static const int kContainsCachedArrayIndexMask =
      (~kMaxCachedArrayIndexLength << kArrayIndexHashLengthShift) |
      kIsNotArrayIndexMask;

  // Value of empty hash field indicating that the hash is not computed.
  static const int kEmptyHashField =
      kIsNotArrayIndexMask | kHashNotComputedMask;

  // Value of hash field containing computed hash equal to zero.
  static const int kZeroHash = kIsNotArrayIndexMask;

  // Maximal string length.
  static const int kMaxLength = (1 << (32 - 2)) - 1;

  // Max length for computing hash. For strings longer than this limit the
  // string length is used as the hash value.
  static const int kMaxHashCalcLength = 16383;

  // Limit for truncation in short printing.
  static const int kMaxShortPrintLength = 1024;

  // Support for regular expressions.
  const uc16* GetTwoByteData();
  const uc16* GetTwoByteData(unsigned start);

  // Support for StringInputBuffer
  static const unibrow::byte* ReadBlock(String* input,
                                        unibrow::byte* util_buffer,
                                        unsigned capacity,
                                        unsigned* remaining,
                                        unsigned* offset);
  static const unibrow::byte* ReadBlock(String** input,
                                        unibrow::byte* util_buffer,
                                        unsigned capacity,
                                        unsigned* remaining,
                                        unsigned* offset);

  // Helper function for flattening strings.
  template <typename sinkchar>
  static void WriteToFlat(String* source,
                          sinkchar* sink,
                          int from,
                          int to);

  static inline bool IsAscii(const char* chars, int length) {
    const char* limit = chars + length;
#ifdef V8_HOST_CAN_READ_UNALIGNED
    ASSERT(kMaxAsciiCharCode == 0x7F);
    const uintptr_t non_ascii_mask = kUintptrAllBitsSet / 0xFF * 0x80;
    while (chars <= limit - sizeof(uintptr_t)) {
      if (*reinterpret_cast<const uintptr_t*>(chars) & non_ascii_mask) {
        return false;
      }
      chars += sizeof(uintptr_t);
    }
#endif
    while (chars < limit) {
      if (static_cast<uint8_t>(*chars) > kMaxAsciiCharCodeU) return false;
      ++chars;
    }
    return true;
  }

  static inline bool IsAscii(const uc16* chars, int length) {
    const uc16* limit = chars + length;
    while (chars < limit) {
      if (*chars > kMaxAsciiCharCodeU) return false;
      ++chars;
    }
    return true;
  }

 protected:
  class ReadBlockBuffer {
   public:
    ReadBlockBuffer(unibrow::byte* util_buffer_,
                    unsigned cursor_,
                    unsigned capacity_,
                    unsigned remaining_) :
      util_buffer(util_buffer_),
      cursor(cursor_),
      capacity(capacity_),
      remaining(remaining_) {
    }
    unibrow::byte* util_buffer;
    unsigned       cursor;
    unsigned       capacity;
    unsigned       remaining;
  };

  static inline const unibrow::byte* ReadBlock(String* input,
                                               ReadBlockBuffer* buffer,
                                               unsigned* offset,
                                               unsigned max_chars);
  static void ReadBlockIntoBuffer(String* input,
                                  ReadBlockBuffer* buffer,
                                  unsigned* offset_ptr,
                                  unsigned max_chars);

 private:
  // Try to flatten the top level ConsString that is hiding behind this
  // string.  This is a no-op unless the string is a ConsString.  Flatten
  // mutates the ConsString and might return a failure.
  MUST_USE_RESULT MaybeObject* SlowTryFlatten(PretenureFlag pretenure);

  static inline bool IsHashFieldComputed(uint32_t field);

  // Slow case of String::Equals.  This implementation works on any strings
  // but it is most efficient on strings that are almost flat.
  bool SlowEquals(String* other);

  // Slow case of AsArrayIndex.
  bool SlowAsArrayIndex(uint32_t* index);

  // Compute and set the hash code.
  uint32_t ComputeAndSetHash();

  DISALLOW_IMPLICIT_CONSTRUCTORS(String);
};


// The SeqString abstract class captures sequential string values.
class SeqString: public String {
 public:
  // Casting.
  static inline SeqString* cast(Object* obj);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(SeqString);
};


// The AsciiString class captures sequential ascii string objects.
// Each character in the AsciiString is an ascii character.
class SeqAsciiString: public SeqString {
 public:
  static const bool kHasAsciiEncoding = true;

  // Dispatched behavior.
  inline uint16_t SeqAsciiStringGet(int index);
  inline void SeqAsciiStringSet(int index, uint16_t value);

  // Get the address of the characters in this string.
  inline Address GetCharsAddress();

  inline char* GetChars();

  // Casting
  static inline SeqAsciiString* cast(Object* obj);

  // Garbage collection support.  This method is called by the
  // garbage collector to compute the actual size of an AsciiString
  // instance.
  inline int SeqAsciiStringSize(InstanceType instance_type);

  // Computes the size for an AsciiString instance of a given length.
  static int SizeFor(int length) {
    return OBJECT_POINTER_ALIGN(kHeaderSize + length * kCharSize);
  }

  // Layout description.
  static const int kHeaderSize = String::kSize;
  static const int kAlignedSize = POINTER_SIZE_ALIGN(kHeaderSize);

  // Maximal memory usage for a single sequential ASCII string.
  static const int kMaxSize = 512 * MB;
  // Maximal length of a single sequential ASCII string.
  // Q.v. String::kMaxLength which is the maximal size of concatenated strings.
  static const int kMaxLength = (kMaxSize - kHeaderSize);

  // Support for StringInputBuffer.
  inline void SeqAsciiStringReadBlockIntoBuffer(ReadBlockBuffer* buffer,
                                                unsigned* offset,
                                                unsigned chars);
  inline const unibrow::byte* SeqAsciiStringReadBlock(unsigned* remaining,
                                                      unsigned* offset,
                                                      unsigned chars);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(SeqAsciiString);
};


// The TwoByteString class captures sequential unicode string objects.
// Each character in the TwoByteString is a two-byte uint16_t.
class SeqTwoByteString: public SeqString {
 public:
  static const bool kHasAsciiEncoding = false;

  // Dispatched behavior.
  inline uint16_t SeqTwoByteStringGet(int index);
  inline void SeqTwoByteStringSet(int index, uint16_t value);

  // Get the address of the characters in this string.
  inline Address GetCharsAddress();

  inline uc16* GetChars();

  // For regexp code.
  const uint16_t* SeqTwoByteStringGetData(unsigned start);

  // Casting
  static inline SeqTwoByteString* cast(Object* obj);

  // Garbage collection support.  This method is called by the
  // garbage collector to compute the actual size of a TwoByteString
  // instance.
  inline int SeqTwoByteStringSize(InstanceType instance_type);

  // Computes the size for a TwoByteString instance of a given length.
  static int SizeFor(int length) {
    return OBJECT_POINTER_ALIGN(kHeaderSize + length * kShortSize);
  }

  // Layout description.
  static const int kHeaderSize = String::kSize;
  static const int kAlignedSize = POINTER_SIZE_ALIGN(kHeaderSize);

  // Maximal memory usage for a single sequential two-byte string.
  static const int kMaxSize = 512 * MB;
  // Maximal length of a single sequential two-byte string.
  // Q.v. String::kMaxLength which is the maximal size of concatenated strings.
  static const int kMaxLength = (kMaxSize - kHeaderSize) / sizeof(uint16_t);

  // Support for StringInputBuffer.
  inline void SeqTwoByteStringReadBlockIntoBuffer(ReadBlockBuffer* buffer,
                                                  unsigned* offset_ptr,
                                                  unsigned chars);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(SeqTwoByteString);
};


// The ConsString class describes string values built by using the
// addition operator on strings.  A ConsString is a pair where the
// first and second components are pointers to other string values.
// One or both components of a ConsString can be pointers to other
// ConsStrings, creating a binary tree of ConsStrings where the leaves
// are non-ConsString string values.  The string value represented by
// a ConsString can be obtained by concatenating the leaf string
// values in a left-to-right depth-first traversal of the tree.
class ConsString: public String {
 public:
  // First string of the cons cell.
  inline String* first();
  // Doesn't check that the result is a string, even in debug mode.  This is
  // useful during GC where the mark bits confuse the checks.
  inline Object* unchecked_first();
  inline void set_first(String* first,
                        WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // Second string of the cons cell.
  inline String* second();
  // Doesn't check that the result is a string, even in debug mode.  This is
  // useful during GC where the mark bits confuse the checks.
  inline Object* unchecked_second();
  inline void set_second(String* second,
                         WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // Dispatched behavior.
  uint16_t ConsStringGet(int index);

  // Casting.
  static inline ConsString* cast(Object* obj);

  // Layout description.
  static const int kFirstOffset = POINTER_SIZE_ALIGN(String::kSize);
  static const int kSecondOffset = kFirstOffset + kPointerSize;
  static const int kSize = kSecondOffset + kPointerSize;

  // Support for StringInputBuffer.
  inline const unibrow::byte* ConsStringReadBlock(ReadBlockBuffer* buffer,
                                                  unsigned* offset_ptr,
                                                  unsigned chars);
  inline void ConsStringReadBlockIntoBuffer(ReadBlockBuffer* buffer,
                                            unsigned* offset_ptr,
                                            unsigned chars);

  // Minimum length for a cons string.
  static const int kMinLength = 13;

  typedef FixedBodyDescriptor<kFirstOffset, kSecondOffset + kPointerSize, kSize>
          BodyDescriptor;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ConsString);
};


// The ExternalString class describes string values that are backed by
// a string resource that lies outside the V8 heap.  ExternalStrings
// consist of the length field common to all strings, a pointer to the
// external resource.  It is important to ensure (externally) that the
// resource is not deallocated while the ExternalString is live in the
// V8 heap.
//
// The API expects that all ExternalStrings are created through the
// API.  Therefore, ExternalStrings should not be used internally.
class ExternalString: public String {
 public:
  // Casting
  static inline ExternalString* cast(Object* obj);

  // Layout description.
  static const int kResourceOffset = POINTER_SIZE_ALIGN(String::kSize);
  static const int kSize = kResourceOffset + kPointerSize;

  STATIC_CHECK(kResourceOffset == Internals::kStringResourceOffset);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalString);
};


// The ExternalAsciiString class is an external string backed by an
// ASCII string.
class ExternalAsciiString: public ExternalString {
 public:
  static const bool kHasAsciiEncoding = true;

  typedef v8::String::ExternalAsciiStringResource Resource;

  // The underlying resource.
  inline Resource* resource();
  inline void set_resource(Resource* buffer);

  // Dispatched behavior.
  uint16_t ExternalAsciiStringGet(int index);

  // Casting.
  static inline ExternalAsciiString* cast(Object* obj);

  // Garbage collection support.
  inline void ExternalAsciiStringIterateBody(ObjectVisitor* v);

  template<typename StaticVisitor>
  inline void ExternalAsciiStringIterateBody();

  // Support for StringInputBuffer.
  const unibrow::byte* ExternalAsciiStringReadBlock(unsigned* remaining,
                                                    unsigned* offset,
                                                    unsigned chars);
  inline void ExternalAsciiStringReadBlockIntoBuffer(ReadBlockBuffer* buffer,
                                                     unsigned* offset,
                                                     unsigned chars);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalAsciiString);
};


// The ExternalTwoByteString class is an external string backed by a UTF-16
// encoded string.
class ExternalTwoByteString: public ExternalString {
 public:
  static const bool kHasAsciiEncoding = false;

  typedef v8::String::ExternalStringResource Resource;

  // The underlying string resource.
  inline Resource* resource();
  inline void set_resource(Resource* buffer);

  // Dispatched behavior.
  uint16_t ExternalTwoByteStringGet(int index);

  // For regexp code.
  const uint16_t* ExternalTwoByteStringGetData(unsigned start);

  // Casting.
  static inline ExternalTwoByteString* cast(Object* obj);

  // Garbage collection support.
  inline void ExternalTwoByteStringIterateBody(ObjectVisitor* v);

  template<typename StaticVisitor>
  inline void ExternalTwoByteStringIterateBody();


  // Support for StringInputBuffer.
  void ExternalTwoByteStringReadBlockIntoBuffer(ReadBlockBuffer* buffer,
                                                unsigned* offset_ptr,
                                                unsigned chars);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ExternalTwoByteString);
};


// Utility superclass for stack-allocated objects that must be updated
// on gc.  It provides two ways for the gc to update instances, either
// iterating or updating after gc.
class Relocatable BASE_EMBEDDED {
 public:
  explicit inline Relocatable(Isolate* isolate);
  inline virtual ~Relocatable();
  virtual void IterateInstance(ObjectVisitor* v) { }
  virtual void PostGarbageCollection() { }

  static void PostGarbageCollectionProcessing();
  static int ArchiveSpacePerThread();
  static char* ArchiveState(Isolate* isolate, char* to);
  static char* RestoreState(Isolate* isolate, char* from);
  static void Iterate(ObjectVisitor* v);
  static void Iterate(ObjectVisitor* v, Relocatable* top);
  static char* Iterate(ObjectVisitor* v, char* t);
 private:
  Isolate* isolate_;
  Relocatable* prev_;
};


// A flat string reader provides random access to the contents of a
// string independent of the character width of the string.  The handle
// must be valid as long as the reader is being used.
class FlatStringReader : public Relocatable {
 public:
  FlatStringReader(Isolate* isolate, Handle<String> str);
  FlatStringReader(Isolate* isolate, Vector<const char> input);
  void PostGarbageCollection();
  inline uc32 Get(int index);
  int length() { return length_; }
 private:
  String** str_;
  bool is_ascii_;
  int length_;
  const void* start_;
};


// Note that StringInputBuffers are not valid across a GC!  To fix this
// it would have to store a String Handle instead of a String* and
// AsciiStringReadBlock would have to be modified to use memcpy.
//
// StringInputBuffer is able to traverse any string regardless of how
// deeply nested a sequence of ConsStrings it is made of.  However,
// performance will be better if deep strings are flattened before they
// are traversed.  Since flattening requires memory allocation this is
// not always desirable, however (esp. in debugging situations).
class StringInputBuffer: public unibrow::InputBuffer<String, String*, 1024> {
 public:
  virtual void Seek(unsigned pos);
  inline StringInputBuffer(): unibrow::InputBuffer<String, String*, 1024>() {}
  explicit inline StringInputBuffer(String* backing):
      unibrow::InputBuffer<String, String*, 1024>(backing) {}
};


class SafeStringInputBuffer
  : public unibrow::InputBuffer<String, String**, 256> {
 public:
  virtual void Seek(unsigned pos);
  inline SafeStringInputBuffer()
      : unibrow::InputBuffer<String, String**, 256>() {}
  explicit inline SafeStringInputBuffer(String** backing)
      : unibrow::InputBuffer<String, String**, 256>(backing) {}
};


template <typename T>
class VectorIterator {
 public:
  VectorIterator(T* d, int l) : data_(Vector<const T>(d, l)), index_(0) { }
  explicit VectorIterator(Vector<const T> data) : data_(data), index_(0) { }
  T GetNext() { return data_[index_++]; }
  bool has_more() { return index_ < data_.length(); }
 private:
  Vector<const T> data_;
  int index_;
};


// The Oddball describes objects null, undefined, true, and false.
class Oddball: public HeapObject {
 public:
  // [to_string]: Cached to_string computed at startup.
  DECL_ACCESSORS(to_string, String)

  // [to_number]: Cached to_number computed at startup.
  DECL_ACCESSORS(to_number, Object)

  inline byte kind();
  inline void set_kind(byte kind);

  // Casting.
  static inline Oddball* cast(Object* obj);

  // Dispatched behavior.
#ifdef DEBUG
  void OddballVerify();
#endif

  // Initialize the fields.
  MUST_USE_RESULT MaybeObject* Initialize(const char* to_string,
                                          Object* to_number,
                                          byte kind);

  // Layout description.
  static const int kToStringOffset = HeapObject::kHeaderSize;
  static const int kToNumberOffset = kToStringOffset + kPointerSize;
  static const int kKindOffset = kToNumberOffset + kPointerSize;
  static const int kSize = kKindOffset + kPointerSize;

  static const byte kFalse = 0;
  static const byte kTrue = 1;
  static const byte kNotBooleanMask = ~1;
  static const byte kTheHole = 2;
  static const byte kNull = 3;
  static const byte kArgumentMarker = 4;
  static const byte kUndefined = 5;
  static const byte kOther = 6;

  typedef FixedBodyDescriptor<kToStringOffset,
                              kToNumberOffset + kPointerSize,
                              kSize> BodyDescriptor;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Oddball);
};


class JSGlobalPropertyCell: public HeapObject {
 public:
  // [value]: value of the global property.
  DECL_ACCESSORS(value, Object)

  // Casting.
  static inline JSGlobalPropertyCell* cast(Object* obj);

#ifdef DEBUG
  void JSGlobalPropertyCellVerify();
#endif
#ifdef OBJECT_PRINT
  inline void JSGlobalPropertyCellPrint() {
    JSGlobalPropertyCellPrint(stdout);
  }
  void JSGlobalPropertyCellPrint(FILE* out);
#endif

  // Layout description.
  static const int kValueOffset = HeapObject::kHeaderSize;
  static const int kSize = kValueOffset + kPointerSize;

  typedef FixedBodyDescriptor<kValueOffset,
                              kValueOffset + kPointerSize,
                              kSize> BodyDescriptor;

  // Returns the isolate/heap this cell object belongs to.
  inline Isolate* isolate();
  inline Heap* heap();

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSGlobalPropertyCell);
};


// The JSProxy describes EcmaScript Harmony proxies
class JSProxy: public JSReceiver {
 public:
  // [handler]: The handler property.
  DECL_ACCESSORS(handler, Object)

  // [padding]: The padding slot (unused, see below).
  DECL_ACCESSORS(padding, Object)

  // Casting.
  static inline JSProxy* cast(Object* obj);

  bool HasPropertyWithHandler(String* name);

  MUST_USE_RESULT MaybeObject* SetPropertyWithHandler(
      String* name,
      Object* value,
      PropertyAttributes attributes,
      StrictModeFlag strict_mode);

  MUST_USE_RESULT MaybeObject* DeletePropertyWithHandler(
      String* name,
      DeleteMode mode);

  MUST_USE_RESULT PropertyAttributes GetPropertyAttributeWithHandler(
      JSReceiver* receiver,
      String* name,
      bool* has_exception);

  // Turn this into an (empty) JSObject.
  void Fix();

  // Dispatched behavior.
#ifdef OBJECT_PRINT
  inline void JSProxyPrint() {
    JSProxyPrint(stdout);
  }
  void JSProxyPrint(FILE* out);
#endif
#ifdef DEBUG
  void JSProxyVerify();
#endif

  // Layout description. We add padding so that a proxy has the same
  // size as a virgin JSObject. This is essential for becoming a JSObject
  // upon freeze.
  static const int kHandlerOffset = HeapObject::kHeaderSize;
  static const int kPaddingOffset = kHandlerOffset + kPointerSize;
  static const int kSize = kPaddingOffset + kPointerSize;

  STATIC_CHECK(kSize == JSObject::kHeaderSize);

  typedef FixedBodyDescriptor<kHandlerOffset,
                              kHandlerOffset + kPointerSize,
                              kSize> BodyDescriptor;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSProxy);
};


// TODO(rossberg): Only a stub for now.
class JSFunctionProxy: public JSProxy {
 public:
  // Casting.
  static inline JSFunctionProxy* cast(Object* obj);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSFunctionProxy);
};


// Foreign describes objects pointing from JavaScript to C structures.
// Since they cannot contain references to JS HeapObjects they can be
// placed in old_data_space.
class Foreign: public HeapObject {
 public:
  // [address]: field containing the address.
  inline Address address();
  inline void set_address(Address value);

  // Casting.
  static inline Foreign* cast(Object* obj);

  // Dispatched behavior.
  inline void ForeignIterateBody(ObjectVisitor* v);

  template<typename StaticVisitor>
  inline void ForeignIterateBody();

#ifdef OBJECT_PRINT
  inline void ForeignPrint() {
    ForeignPrint(stdout);
  }
  void ForeignPrint(FILE* out);
#endif
#ifdef DEBUG
  void ForeignVerify();
#endif

  // Layout description.

  static const int kAddressOffset = HeapObject::kHeaderSize;
  static const int kSize = kAddressOffset + kPointerSize;

  STATIC_CHECK(kAddressOffset == Internals::kForeignAddressOffset);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Foreign);
};


// The JSArray describes JavaScript Arrays
//  Such an array can be in one of two modes:
//    - fast, backing storage is a FixedArray and length <= elements.length();
//       Please note: push and pop can be used to grow and shrink the array.
//    - slow, backing storage is a HashTable with numbers as keys.
class JSArray: public JSObject {
 public:
  // [length]: The length property.
  DECL_ACCESSORS(length, Object)

  // Overload the length setter to skip write barrier when the length
  // is set to a smi. This matches the set function on FixedArray.
  inline void set_length(Smi* length);

  MUST_USE_RESULT MaybeObject* JSArrayUpdateLengthFromIndex(uint32_t index,
                                                            Object* value);

  // Initialize the array with the given capacity. The function may
  // fail due to out-of-memory situations, but only if the requested
  // capacity is non-zero.
  MUST_USE_RESULT MaybeObject* Initialize(int capacity);

  // Set the content of the array to the content of storage.
  inline void SetContent(FixedArray* storage);

  // Casting.
  static inline JSArray* cast(Object* obj);

  // Uses handles.  Ensures that the fixed array backing the JSArray has at
  // least the stated size.
  inline void EnsureSize(int minimum_size_of_backing_fixed_array);

  // Dispatched behavior.
#ifdef OBJECT_PRINT
  inline void JSArrayPrint() {
    JSArrayPrint(stdout);
  }
  void JSArrayPrint(FILE* out);
#endif
#ifdef DEBUG
  void JSArrayVerify();
#endif

  // Number of element slots to pre-allocate for an empty array.
  static const int kPreallocatedArrayElements = 4;

  // Layout description.
  static const int kLengthOffset = JSObject::kHeaderSize;
  static const int kSize = kLengthOffset + kPointerSize;

 private:
  // Expand the fixed array backing of a fast-case JSArray to at least
  // the requested size.
  void Expand(int minimum_size_of_backing_fixed_array);

  DISALLOW_IMPLICIT_CONSTRUCTORS(JSArray);
};


// JSRegExpResult is just a JSArray with a specific initial map.
// This initial map adds in-object properties for "index" and "input"
// properties, as assigned by RegExp.prototype.exec, which allows
// faster creation of RegExp exec results.
// This class just holds constants used when creating the result.
// After creation the result must be treated as a JSArray in all regards.
class JSRegExpResult: public JSArray {
 public:
  // Offsets of object fields.
  static const int kIndexOffset = JSArray::kSize;
  static const int kInputOffset = kIndexOffset + kPointerSize;
  static const int kSize = kInputOffset + kPointerSize;
  // Indices of in-object properties.
  static const int kIndexIndex = 0;
  static const int kInputIndex = 1;
 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSRegExpResult);
};


// An accessor must have a getter, but can have no setter.
//
// When setting a property, V8 searches accessors in prototypes.
// If an accessor was found and it does not have a setter,
// the request is ignored.
//
// If the accessor in the prototype has the READ_ONLY property attribute, then
// a new value is added to the local object when the property is set.
// This shadows the accessor in the prototype.
class AccessorInfo: public Struct {
 public:
  DECL_ACCESSORS(getter, Object)
  DECL_ACCESSORS(setter, Object)
  DECL_ACCESSORS(data, Object)
  DECL_ACCESSORS(name, Object)
  DECL_ACCESSORS(flag, Smi)

  inline bool all_can_read();
  inline void set_all_can_read(bool value);

  inline bool all_can_write();
  inline void set_all_can_write(bool value);

  inline bool prohibits_overwriting();
  inline void set_prohibits_overwriting(bool value);

  inline PropertyAttributes property_attributes();
  inline void set_property_attributes(PropertyAttributes attributes);

  static inline AccessorInfo* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void AccessorInfoPrint() {
    AccessorInfoPrint(stdout);
  }
  void AccessorInfoPrint(FILE* out);
#endif
#ifdef DEBUG
  void AccessorInfoVerify();
#endif

  static const int kGetterOffset = HeapObject::kHeaderSize;
  static const int kSetterOffset = kGetterOffset + kPointerSize;
  static const int kDataOffset = kSetterOffset + kPointerSize;
  static const int kNameOffset = kDataOffset + kPointerSize;
  static const int kFlagOffset = kNameOffset + kPointerSize;
  static const int kSize = kFlagOffset + kPointerSize;

 private:
  // Bit positions in flag.
  static const int kAllCanReadBit = 0;
  static const int kAllCanWriteBit = 1;
  static const int kProhibitsOverwritingBit = 2;
  class AttributesField: public BitField<PropertyAttributes, 3, 3> {};

  DISALLOW_IMPLICIT_CONSTRUCTORS(AccessorInfo);
};


class AccessCheckInfo: public Struct {
 public:
  DECL_ACCESSORS(named_callback, Object)
  DECL_ACCESSORS(indexed_callback, Object)
  DECL_ACCESSORS(data, Object)

  static inline AccessCheckInfo* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void AccessCheckInfoPrint() {
    AccessCheckInfoPrint(stdout);
  }
  void AccessCheckInfoPrint(FILE* out);
#endif
#ifdef DEBUG
  void AccessCheckInfoVerify();
#endif

  static const int kNamedCallbackOffset   = HeapObject::kHeaderSize;
  static const int kIndexedCallbackOffset = kNamedCallbackOffset + kPointerSize;
  static const int kDataOffset = kIndexedCallbackOffset + kPointerSize;
  static const int kSize = kDataOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(AccessCheckInfo);
};


class InterceptorInfo: public Struct {
 public:
  DECL_ACCESSORS(getter, Object)
  DECL_ACCESSORS(setter, Object)
  DECL_ACCESSORS(query, Object)
  DECL_ACCESSORS(deleter, Object)
  DECL_ACCESSORS(enumerator, Object)
  DECL_ACCESSORS(data, Object)

  static inline InterceptorInfo* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void InterceptorInfoPrint() {
    InterceptorInfoPrint(stdout);
  }
  void InterceptorInfoPrint(FILE* out);
#endif
#ifdef DEBUG
  void InterceptorInfoVerify();
#endif

  static const int kGetterOffset = HeapObject::kHeaderSize;
  static const int kSetterOffset = kGetterOffset + kPointerSize;
  static const int kQueryOffset = kSetterOffset + kPointerSize;
  static const int kDeleterOffset = kQueryOffset + kPointerSize;
  static const int kEnumeratorOffset = kDeleterOffset + kPointerSize;
  static const int kDataOffset = kEnumeratorOffset + kPointerSize;
  static const int kSize = kDataOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(InterceptorInfo);
};


class CallHandlerInfo: public Struct {
 public:
  DECL_ACCESSORS(callback, Object)
  DECL_ACCESSORS(data, Object)

  static inline CallHandlerInfo* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void CallHandlerInfoPrint() {
    CallHandlerInfoPrint(stdout);
  }
  void CallHandlerInfoPrint(FILE* out);
#endif
#ifdef DEBUG
  void CallHandlerInfoVerify();
#endif

  static const int kCallbackOffset = HeapObject::kHeaderSize;
  static const int kDataOffset = kCallbackOffset + kPointerSize;
  static const int kSize = kDataOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(CallHandlerInfo);
};


class TemplateInfo: public Struct {
 public:
  DECL_ACCESSORS(tag, Object)
  DECL_ACCESSORS(property_list, Object)

#ifdef DEBUG
  void TemplateInfoVerify();
#endif

  static const int kTagOffset          = HeapObject::kHeaderSize;
  static const int kPropertyListOffset = kTagOffset + kPointerSize;
  static const int kHeaderSize         = kPropertyListOffset + kPointerSize;
 protected:
  friend class AGCCVersionRequiresThisClassToHaveAFriendSoHereItIs;
  DISALLOW_IMPLICIT_CONSTRUCTORS(TemplateInfo);
};


class FunctionTemplateInfo: public TemplateInfo {
 public:
  DECL_ACCESSORS(serial_number, Object)
  DECL_ACCESSORS(call_code, Object)
  DECL_ACCESSORS(property_accessors, Object)
  DECL_ACCESSORS(prototype_template, Object)
  DECL_ACCESSORS(parent_template, Object)
  DECL_ACCESSORS(named_property_handler, Object)
  DECL_ACCESSORS(indexed_property_handler, Object)
  DECL_ACCESSORS(instance_template, Object)
  DECL_ACCESSORS(class_name, Object)
  DECL_ACCESSORS(signature, Object)
  DECL_ACCESSORS(instance_call_handler, Object)
  DECL_ACCESSORS(access_check_info, Object)
  DECL_ACCESSORS(flag, Smi)

  // Following properties use flag bits.
  DECL_BOOLEAN_ACCESSORS(hidden_prototype)
  DECL_BOOLEAN_ACCESSORS(undetectable)
  // If the bit is set, object instances created by this function
  // requires access check.
  DECL_BOOLEAN_ACCESSORS(needs_access_check)
  DECL_BOOLEAN_ACCESSORS(read_only_prototype)

  static inline FunctionTemplateInfo* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void FunctionTemplateInfoPrint() {
    FunctionTemplateInfoPrint(stdout);
  }
  void FunctionTemplateInfoPrint(FILE* out);
#endif
#ifdef DEBUG
  void FunctionTemplateInfoVerify();
#endif

  static const int kSerialNumberOffset = TemplateInfo::kHeaderSize;
  static const int kCallCodeOffset = kSerialNumberOffset + kPointerSize;
  static const int kPropertyAccessorsOffset = kCallCodeOffset + kPointerSize;
  static const int kPrototypeTemplateOffset =
      kPropertyAccessorsOffset + kPointerSize;
  static const int kParentTemplateOffset =
      kPrototypeTemplateOffset + kPointerSize;
  static const int kNamedPropertyHandlerOffset =
      kParentTemplateOffset + kPointerSize;
  static const int kIndexedPropertyHandlerOffset =
      kNamedPropertyHandlerOffset + kPointerSize;
  static const int kInstanceTemplateOffset =
      kIndexedPropertyHandlerOffset + kPointerSize;
  static const int kClassNameOffset = kInstanceTemplateOffset + kPointerSize;
  static const int kSignatureOffset = kClassNameOffset + kPointerSize;
  static const int kInstanceCallHandlerOffset = kSignatureOffset + kPointerSize;
  static const int kAccessCheckInfoOffset =
      kInstanceCallHandlerOffset + kPointerSize;
  static const int kFlagOffset = kAccessCheckInfoOffset + kPointerSize;
  static const int kSize = kFlagOffset + kPointerSize;

 private:
  // Bit position in the flag, from least significant bit position.
  static const int kHiddenPrototypeBit   = 0;
  static const int kUndetectableBit      = 1;
  static const int kNeedsAccessCheckBit  = 2;
  static const int kReadOnlyPrototypeBit = 3;

  DISALLOW_IMPLICIT_CONSTRUCTORS(FunctionTemplateInfo);
};


class ObjectTemplateInfo: public TemplateInfo {
 public:
  DECL_ACCESSORS(constructor, Object)
  DECL_ACCESSORS(internal_field_count, Object)

  static inline ObjectTemplateInfo* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void ObjectTemplateInfoPrint() {
    ObjectTemplateInfoPrint(stdout);
  }
  void ObjectTemplateInfoPrint(FILE* out);
#endif
#ifdef DEBUG
  void ObjectTemplateInfoVerify();
#endif

  static const int kConstructorOffset = TemplateInfo::kHeaderSize;
  static const int kInternalFieldCountOffset =
      kConstructorOffset + kPointerSize;
  static const int kSize = kInternalFieldCountOffset + kPointerSize;
};


class SignatureInfo: public Struct {
 public:
  DECL_ACCESSORS(receiver, Object)
  DECL_ACCESSORS(args, Object)

  static inline SignatureInfo* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void SignatureInfoPrint() {
    SignatureInfoPrint(stdout);
  }
  void SignatureInfoPrint(FILE* out);
#endif
#ifdef DEBUG
  void SignatureInfoVerify();
#endif

  static const int kReceiverOffset = Struct::kHeaderSize;
  static const int kArgsOffset     = kReceiverOffset + kPointerSize;
  static const int kSize           = kArgsOffset + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(SignatureInfo);
};


class TypeSwitchInfo: public Struct {
 public:
  DECL_ACCESSORS(types, Object)

  static inline TypeSwitchInfo* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void TypeSwitchInfoPrint() {
    TypeSwitchInfoPrint(stdout);
  }
  void TypeSwitchInfoPrint(FILE* out);
#endif
#ifdef DEBUG
  void TypeSwitchInfoVerify();
#endif

  static const int kTypesOffset = Struct::kHeaderSize;
  static const int kSize        = kTypesOffset + kPointerSize;
};


#ifdef ENABLE_DEBUGGER_SUPPORT
// The DebugInfo class holds additional information for a function being
// debugged.
class DebugInfo: public Struct {
 public:
  // The shared function info for the source being debugged.
  DECL_ACCESSORS(shared, SharedFunctionInfo)
  // Code object for the original code.
  DECL_ACCESSORS(original_code, Code)
  // Code object for the patched code. This code object is the code object
  // currently active for the function.
  DECL_ACCESSORS(code, Code)
  // Fixed array holding status information for each active break point.
  DECL_ACCESSORS(break_points, FixedArray)

  // Check if there is a break point at a code position.
  bool HasBreakPoint(int code_position);
  // Get the break point info object for a code position.
  Object* GetBreakPointInfo(int code_position);
  // Clear a break point.
  static void ClearBreakPoint(Handle<DebugInfo> debug_info,
                              int code_position,
                              Handle<Object> break_point_object);
  // Set a break point.
  static void SetBreakPoint(Handle<DebugInfo> debug_info, int code_position,
                            int source_position, int statement_position,
                            Handle<Object> break_point_object);
  // Get the break point objects for a code position.
  Object* GetBreakPointObjects(int code_position);
  // Find the break point info holding this break point object.
  static Object* FindBreakPointInfo(Handle<DebugInfo> debug_info,
                                    Handle<Object> break_point_object);
  // Get the number of break points for this function.
  int GetBreakPointCount();

  static inline DebugInfo* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void DebugInfoPrint() {
    DebugInfoPrint(stdout);
  }
  void DebugInfoPrint(FILE* out);
#endif
#ifdef DEBUG
  void DebugInfoVerify();
#endif

  static const int kSharedFunctionInfoIndex = Struct::kHeaderSize;
  static const int kOriginalCodeIndex = kSharedFunctionInfoIndex + kPointerSize;
  static const int kPatchedCodeIndex = kOriginalCodeIndex + kPointerSize;
  static const int kActiveBreakPointsCountIndex =
      kPatchedCodeIndex + kPointerSize;
  static const int kBreakPointsStateIndex =
      kActiveBreakPointsCountIndex + kPointerSize;
  static const int kSize = kBreakPointsStateIndex + kPointerSize;

 private:
  static const int kNoBreakPointInfo = -1;

  // Lookup the index in the break_points array for a code position.
  int GetBreakPointInfoIndex(int code_position);

  DISALLOW_IMPLICIT_CONSTRUCTORS(DebugInfo);
};


// The BreakPointInfo class holds information for break points set in a
// function. The DebugInfo object holds a BreakPointInfo object for each code
// position with one or more break points.
class BreakPointInfo: public Struct {
 public:
  // The position in the code for the break point.
  DECL_ACCESSORS(code_position, Smi)
  // The position in the source for the break position.
  DECL_ACCESSORS(source_position, Smi)
  // The position in the source for the last statement before this break
  // position.
  DECL_ACCESSORS(statement_position, Smi)
  // List of related JavaScript break points.
  DECL_ACCESSORS(break_point_objects, Object)

  // Removes a break point.
  static void ClearBreakPoint(Handle<BreakPointInfo> info,
                              Handle<Object> break_point_object);
  // Set a break point.
  static void SetBreakPoint(Handle<BreakPointInfo> info,
                            Handle<Object> break_point_object);
  // Check if break point info has this break point object.
  static bool HasBreakPointObject(Handle<BreakPointInfo> info,
                                  Handle<Object> break_point_object);
  // Get the number of break points for this code position.
  int GetBreakPointCount();

  static inline BreakPointInfo* cast(Object* obj);

#ifdef OBJECT_PRINT
  inline void BreakPointInfoPrint() {
    BreakPointInfoPrint(stdout);
  }
  void BreakPointInfoPrint(FILE* out);
#endif
#ifdef DEBUG
  void BreakPointInfoVerify();
#endif

  static const int kCodePositionIndex = Struct::kHeaderSize;
  static const int kSourcePositionIndex = kCodePositionIndex + kPointerSize;
  static const int kStatementPositionIndex =
      kSourcePositionIndex + kPointerSize;
  static const int kBreakPointObjectsIndex =
      kStatementPositionIndex + kPointerSize;
  static const int kSize = kBreakPointObjectsIndex + kPointerSize;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(BreakPointInfo);
};
#endif  // ENABLE_DEBUGGER_SUPPORT


#undef DECL_BOOLEAN_ACCESSORS
#undef DECL_ACCESSORS


// Abstract base class for visiting, and optionally modifying, the
// pointers contained in Objects. Used in GC and serialization/deserialization.
class ObjectVisitor BASE_EMBEDDED {
 public:
  virtual ~ObjectVisitor() {}

  // Visits a contiguous arrays of pointers in the half-open range
  // [start, end). Any or all of the values may be modified on return.
  virtual void VisitPointers(Object** start, Object** end) = 0;

  // To allow lazy clearing of inline caches the visitor has
  // a rich interface for iterating over Code objects..

  // Visits a code target in the instruction stream.
  virtual void VisitCodeTarget(RelocInfo* rinfo);

  // Visits a code entry in a JS function.
  virtual void VisitCodeEntry(Address entry_address);

  // Visits a global property cell reference in the instruction stream.
  virtual void VisitGlobalPropertyCell(RelocInfo* rinfo);

  // Visits a runtime entry in the instruction stream.
  virtual void VisitRuntimeEntry(RelocInfo* rinfo) {}

  // Visits the resource of an ASCII or two-byte string.
  virtual void VisitExternalAsciiString(
      v8::String::ExternalAsciiStringResource** resource) {}
  virtual void VisitExternalTwoByteString(
      v8::String::ExternalStringResource** resource) {}

  // Visits a debug call target in the instruction stream.
  virtual void VisitDebugTarget(RelocInfo* rinfo);

  // Handy shorthand for visiting a single pointer.
  virtual void VisitPointer(Object** p) { VisitPointers(p, p + 1); }

  // Visits a contiguous arrays of external references (references to the C++
  // heap) in the half-open range [start, end). Any or all of the values
  // may be modified on return.
  virtual void VisitExternalReferences(Address* start, Address* end) {}

  inline void VisitExternalReference(Address* p) {
    VisitExternalReferences(p, p + 1);
  }

  // Visits a handle that has an embedder-assigned class ID.
  virtual void VisitEmbedderReference(Object** p, uint16_t class_id) {}

#ifdef DEBUG
  // Intended for serialization/deserialization checking: insert, or
  // check for the presence of, a tag at this position in the stream.
  virtual void Synchronize(const char* tag) {}
#else
  inline void Synchronize(const char* tag) {}
#endif
};


class StructBodyDescriptor : public
  FlexibleBodyDescriptor<HeapObject::kHeaderSize> {
 public:
  static inline int SizeOf(Map* map, HeapObject* object) {
    return map->instance_size();
  }
};


// BooleanBit is a helper class for setting and getting a bit in an
// integer or Smi.
class BooleanBit : public AllStatic {
 public:
  static inline bool get(Smi* smi, int bit_position) {
    return get(smi->value(), bit_position);
  }

  static inline bool get(int value, int bit_position) {
    return (value & (1 << bit_position)) != 0;
  }

  static inline Smi* set(Smi* smi, int bit_position, bool v) {
    return Smi::FromInt(set(smi->value(), bit_position, v));
  }

  static inline int set(int value, int bit_position, bool v) {
    if (v) {
      value |= (1 << bit_position);
    } else {
      value &= ~(1 << bit_position);
    }
    return value;
  }
};

} }  // namespace v8::internal

#endif  // V8_OBJECTS_H_
