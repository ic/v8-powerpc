// Copyright 2011 the V8 project authors. All rights reserved.

#include <stdlib.h>

#include "v8.h"

#include "execution.h"
#include "factory.h"
#include "macro-assembler.h"
#include "global-handles.h"
#include "cctest.h"

using namespace v8::internal;

static v8::Persistent<v8::Context> env;

static void InitializeVM() {
  if (env.IsEmpty()) env = v8::Context::New();
  v8::HandleScope scope;
  env->Enter();
}


static void CheckMap(Map* map, int type, int instance_size) {
  CHECK(map->IsHeapObject());
#ifdef DEBUG
  CHECK(HEAP->Contains(map));
#endif
  CHECK_EQ(HEAP->meta_map(), map->map());
  CHECK_EQ(type, map->instance_type());
  CHECK_EQ(instance_size, map->instance_size());
}


TEST(HeapMaps) {
  InitializeVM();
  CheckMap(HEAP->meta_map(), MAP_TYPE, Map::kSize);
  CheckMap(HEAP->heap_number_map(), HEAP_NUMBER_TYPE, HeapNumber::kSize);
  CheckMap(HEAP->fixed_array_map(), FIXED_ARRAY_TYPE, kVariableSizeSentinel);
  CheckMap(HEAP->string_map(), STRING_TYPE, kVariableSizeSentinel);
}


static void CheckOddball(Object* obj, const char* string) {
  CHECK(obj->IsOddball());
  bool exc;
  Object* print_string = *Execution::ToString(Handle<Object>(obj), &exc);
  CHECK(String::cast(print_string)->IsEqualTo(CStrVector(string)));
}


static void CheckSmi(int value, const char* string) {
  bool exc;
  Object* print_string =
      *Execution::ToString(Handle<Object>(Smi::FromInt(value)), &exc);
  CHECK(String::cast(print_string)->IsEqualTo(CStrVector(string)));
}


static void CheckNumber(double value, const char* string) {
  Object* obj = HEAP->NumberFromDouble(value)->ToObjectChecked();
  CHECK(obj->IsNumber());
  bool exc;
  Object* print_string = *Execution::ToString(Handle<Object>(obj), &exc);
  CHECK(String::cast(print_string)->IsEqualTo(CStrVector(string)));
}


static void CheckFindCodeObject() {
  // Test FindCodeObject
#define __ assm.

  Assembler assm(Isolate::Current(), NULL, 0);

  __ nop();  // supported on all architectures

  CodeDesc desc;
  assm.GetCode(&desc);
  Object* code = HEAP->CreateCode(
      desc,
      Code::ComputeFlags(Code::STUB),
      Handle<Object>(HEAP->undefined_value()))->ToObjectChecked();
  CHECK(code->IsCode());

  HeapObject* obj = HeapObject::cast(code);
  Address obj_addr = obj->address();

  for (int i = 0; i < obj->Size(); i += kPointerSize) {
    Object* found = HEAP->FindCodeObject(obj_addr + i);
    CHECK_EQ(code, found);
  }

  Object* copy = HEAP->CreateCode(
      desc,
      Code::ComputeFlags(Code::STUB),
      Handle<Object>(HEAP->undefined_value()))->ToObjectChecked();
  CHECK(copy->IsCode());
  HeapObject* obj_copy = HeapObject::cast(copy);
  Object* not_right = HEAP->FindCodeObject(obj_copy->address() +
                                           obj_copy->Size() / 2);
  CHECK(not_right != code);
}


TEST(HeapObjects) {
  InitializeVM();

  v8::HandleScope sc;
  Object* value = HEAP->NumberFromDouble(1.000123)->ToObjectChecked();
  CHECK(value->IsHeapNumber());
  CHECK(value->IsNumber());
  CHECK_EQ(1.000123, value->Number());

  value = HEAP->NumberFromDouble(1.0)->ToObjectChecked();
  CHECK(value->IsSmi());
  CHECK(value->IsNumber());
  CHECK_EQ(1.0, value->Number());

  value = HEAP->NumberFromInt32(1024)->ToObjectChecked();
  CHECK(value->IsSmi());
  CHECK(value->IsNumber());
  CHECK_EQ(1024.0, value->Number());

  value = HEAP->NumberFromInt32(Smi::kMinValue)->ToObjectChecked();
  CHECK(value->IsSmi());
  CHECK(value->IsNumber());
  CHECK_EQ(Smi::kMinValue, Smi::cast(value)->value());

  value = HEAP->NumberFromInt32(Smi::kMaxValue)->ToObjectChecked();
  CHECK(value->IsSmi());
  CHECK(value->IsNumber());
  CHECK_EQ(Smi::kMaxValue, Smi::cast(value)->value());

#ifndef V8_TARGET_ARCH_X64
  // TODO(lrn): We need a NumberFromIntptr function in order to test this.
  value = HEAP->NumberFromInt32(Smi::kMinValue - 1)->ToObjectChecked();
  CHECK(value->IsHeapNumber());
  CHECK(value->IsNumber());
  CHECK_EQ(static_cast<double>(Smi::kMinValue - 1), value->Number());
#endif

  MaybeObject* maybe_value =
      HEAP->NumberFromUint32(static_cast<uint32_t>(Smi::kMaxValue) + 1);
  value = maybe_value->ToObjectChecked();
  CHECK(value->IsHeapNumber());
  CHECK(value->IsNumber());
  CHECK_EQ(static_cast<double>(static_cast<uint32_t>(Smi::kMaxValue) + 1),
           value->Number());

  // nan oddball checks
  CHECK(HEAP->nan_value()->IsNumber());
  CHECK(isnan(HEAP->nan_value()->Number()));

  Handle<String> s = FACTORY->NewStringFromAscii(CStrVector("fisk hest "));
  CHECK(s->IsString());
  CHECK_EQ(10, s->length());

  String* object_symbol = String::cast(HEAP->Object_symbol());
  CHECK(
      Isolate::Current()->context()->global()->HasLocalProperty(object_symbol));

  // Check ToString for oddballs
  CheckOddball(HEAP->true_value(), "true");
  CheckOddball(HEAP->false_value(), "false");
  CheckOddball(HEAP->null_value(), "null");
  CheckOddball(HEAP->undefined_value(), "undefined");

  // Check ToString for Smis
  CheckSmi(0, "0");
  CheckSmi(42, "42");
  CheckSmi(-42, "-42");

  // Check ToString for Numbers
  CheckNumber(1.1, "1.1");

  CheckFindCodeObject();
}


TEST(Tagging) {
  InitializeVM();
  int request = 24;
  CHECK_EQ(request, static_cast<int>(OBJECT_POINTER_ALIGN(request)));
  CHECK(Smi::FromInt(42)->IsSmi());
  CHECK(Failure::RetryAfterGC(NEW_SPACE)->IsFailure());
  CHECK_EQ(NEW_SPACE,
           Failure::RetryAfterGC(NEW_SPACE)->allocation_space());
  CHECK_EQ(OLD_POINTER_SPACE,
           Failure::RetryAfterGC(OLD_POINTER_SPACE)->allocation_space());
  CHECK(Failure::Exception()->IsFailure());
  CHECK(Smi::FromInt(Smi::kMinValue)->IsSmi());
  CHECK(Smi::FromInt(Smi::kMaxValue)->IsSmi());
}


TEST(GarbageCollection) {
  InitializeVM();

  v8::HandleScope sc;
  // Check GC.
  HEAP->CollectGarbage(NEW_SPACE);

  Handle<String> name = FACTORY->LookupAsciiSymbol("theFunction");
  Handle<String> prop_name = FACTORY->LookupAsciiSymbol("theSlot");
  Handle<String> prop_namex = FACTORY->LookupAsciiSymbol("theSlotx");
  Handle<String> obj_name = FACTORY->LookupAsciiSymbol("theObject");

  {
    v8::HandleScope inner_scope;
    // Allocate a function and keep it in global object's property.
    Handle<JSFunction> function =
        FACTORY->NewFunction(name, FACTORY->undefined_value());
    Handle<Map> initial_map =
        FACTORY->NewMap(JS_OBJECT_TYPE, JSObject::kHeaderSize);
    function->set_initial_map(*initial_map);
    Isolate::Current()->context()->global()->SetProperty(
        *name, *function, NONE, kNonStrictMode)->ToObjectChecked();
    // Allocate an object.  Unrooted after leaving the scope.
    Handle<JSObject> obj = FACTORY->NewJSObject(function);
    obj->SetProperty(
        *prop_name, Smi::FromInt(23), NONE, kNonStrictMode)->ToObjectChecked();
    obj->SetProperty(
        *prop_namex, Smi::FromInt(24), NONE, kNonStrictMode)->ToObjectChecked();

    CHECK_EQ(Smi::FromInt(23), obj->GetProperty(*prop_name));
    CHECK_EQ(Smi::FromInt(24), obj->GetProperty(*prop_namex));
  }

  HEAP->CollectGarbage(NEW_SPACE);

  // Function should be alive.
  CHECK(Isolate::Current()->context()->global()->HasLocalProperty(*name));
  // Check function is retained.
  Object* func_value = Isolate::Current()->context()->global()->
      GetProperty(*name)->ToObjectChecked();
  CHECK(func_value->IsJSFunction());
  Handle<JSFunction> function(JSFunction::cast(func_value));

  {
    HandleScope inner_scope;
    // Allocate another object, make it reachable from global.
    Handle<JSObject> obj = FACTORY->NewJSObject(function);
    Isolate::Current()->context()->global()->SetProperty(
        *obj_name, *obj, NONE, kNonStrictMode)->ToObjectChecked();
    obj->SetProperty(
        *prop_name, Smi::FromInt(23), NONE, kNonStrictMode)->ToObjectChecked();
  }

  // After gc, it should survive.
  HEAP->CollectGarbage(NEW_SPACE);

  CHECK(Isolate::Current()->context()->global()->HasLocalProperty(*obj_name));
  CHECK(Isolate::Current()->context()->global()->
        GetProperty(*obj_name)->ToObjectChecked()->IsJSObject());
  Object* obj = Isolate::Current()->context()->global()->
      GetProperty(*obj_name)->ToObjectChecked();
  JSObject* js_obj = JSObject::cast(obj);
  CHECK_EQ(Smi::FromInt(23), js_obj->GetProperty(*prop_name));
}


static void VerifyStringAllocation(const char* string) {
  v8::HandleScope scope;
  Handle<String> s = FACTORY->NewStringFromUtf8(CStrVector(string));
  CHECK_EQ(StrLength(string), s->length());
  for (int index = 0; index < s->length(); index++) {
    CHECK_EQ(static_cast<uint16_t>(string[index]), s->Get(index));
  }
}


TEST(String) {
  InitializeVM();

  VerifyStringAllocation("a");
  VerifyStringAllocation("ab");
  VerifyStringAllocation("abc");
  VerifyStringAllocation("abcd");
  VerifyStringAllocation("fiskerdrengen er paa havet");
}


TEST(LocalHandles) {
  InitializeVM();

  v8::HandleScope scope;
  const char* name = "Kasper the spunky";
  Handle<String> string = FACTORY->NewStringFromAscii(CStrVector(name));
  CHECK_EQ(StrLength(name), string->length());
}


TEST(GlobalHandles) {
  InitializeVM();
  GlobalHandles* global_handles = Isolate::Current()->global_handles();

  Handle<Object> h1;
  Handle<Object> h2;
  Handle<Object> h3;
  Handle<Object> h4;

  {
    HandleScope scope;

    Handle<Object> i = FACTORY->NewStringFromAscii(CStrVector("fisk"));
    Handle<Object> u = FACTORY->NewNumber(1.12344);

    h1 = global_handles->Create(*i);
    h2 = global_handles->Create(*u);
    h3 = global_handles->Create(*i);
    h4 = global_handles->Create(*u);
  }

  // after gc, it should survive
  HEAP->CollectGarbage(NEW_SPACE);

  CHECK((*h1)->IsString());
  CHECK((*h2)->IsHeapNumber());
  CHECK((*h3)->IsString());
  CHECK((*h4)->IsHeapNumber());

  CHECK_EQ(*h3, *h1);
  global_handles->Destroy(h1.location());
  global_handles->Destroy(h3.location());

  CHECK_EQ(*h4, *h2);
  global_handles->Destroy(h2.location());
  global_handles->Destroy(h4.location());
}


static bool WeakPointerCleared = false;

static void TestWeakGlobalHandleCallback(v8::Persistent<v8::Value> handle,
                                         void* id) {
  if (1234 == reinterpret_cast<intptr_t>(id)) WeakPointerCleared = true;
  handle.Dispose();
}


TEST(WeakGlobalHandlesScavenge) {
  InitializeVM();
  GlobalHandles* global_handles = Isolate::Current()->global_handles();

  WeakPointerCleared = false;

  Handle<Object> h1;
  Handle<Object> h2;

  {
    HandleScope scope;

    Handle<Object> i = FACTORY->NewStringFromAscii(CStrVector("fisk"));
    Handle<Object> u = FACTORY->NewNumber(1.12344);

    h1 = global_handles->Create(*i);
    h2 = global_handles->Create(*u);
  }

  global_handles->MakeWeak(h2.location(),
                           reinterpret_cast<void*>(1234),
                           &TestWeakGlobalHandleCallback);

  // Scavenge treats weak pointers as normal roots.
  HEAP->PerformScavenge();

  CHECK((*h1)->IsString());
  CHECK((*h2)->IsHeapNumber());

  CHECK(!WeakPointerCleared);
  CHECK(!global_handles->IsNearDeath(h2.location()));
  CHECK(!global_handles->IsNearDeath(h1.location()));

  global_handles->Destroy(h1.location());
  global_handles->Destroy(h2.location());
}


TEST(WeakGlobalHandlesMark) {
  InitializeVM();
  GlobalHandles* global_handles = Isolate::Current()->global_handles();

  WeakPointerCleared = false;

  Handle<Object> h1;
  Handle<Object> h2;

  {
    HandleScope scope;

    Handle<Object> i = FACTORY->NewStringFromAscii(CStrVector("fisk"));
    Handle<Object> u = FACTORY->NewNumber(1.12344);

    h1 = global_handles->Create(*i);
    h2 = global_handles->Create(*u);
  }

  HEAP->CollectGarbage(OLD_POINTER_SPACE);
  HEAP->CollectGarbage(NEW_SPACE);
  // Make sure the object is promoted.

  global_handles->MakeWeak(h2.location(),
                           reinterpret_cast<void*>(1234),
                           &TestWeakGlobalHandleCallback);
  CHECK(!GlobalHandles::IsNearDeath(h1.location()));
  CHECK(!GlobalHandles::IsNearDeath(h2.location()));

  HEAP->CollectGarbage(OLD_POINTER_SPACE);

  CHECK((*h1)->IsString());

  CHECK(WeakPointerCleared);
  CHECK(!GlobalHandles::IsNearDeath(h1.location()));

  global_handles->Destroy(h1.location());
}

TEST(DeleteWeakGlobalHandle) {
  InitializeVM();
  GlobalHandles* global_handles = Isolate::Current()->global_handles();

  WeakPointerCleared = false;

  Handle<Object> h;

  {
    HandleScope scope;

    Handle<Object> i = FACTORY->NewStringFromAscii(CStrVector("fisk"));
    h = global_handles->Create(*i);
  }

  global_handles->MakeWeak(h.location(),
                           reinterpret_cast<void*>(1234),
                           &TestWeakGlobalHandleCallback);

  // Scanvenge does not recognize weak reference.
  HEAP->PerformScavenge();

  CHECK(!WeakPointerCleared);

  // Mark-compact treats weak reference properly.
  HEAP->CollectGarbage(OLD_POINTER_SPACE);

  CHECK(WeakPointerCleared);
}

static const char* not_so_random_string_table[] = {
  "abstract",
  "boolean",
  "break",
  "byte",
  "case",
  "catch",
  "char",
  "class",
  "const",
  "continue",
  "debugger",
  "default",
  "delete",
  "do",
  "double",
  "else",
  "enum",
  "export",
  "extends",
  "false",
  "final",
  "finally",
  "float",
  "for",
  "function",
  "goto",
  "if",
  "implements",
  "import",
  "in",
  "instanceof",
  "int",
  "interface",
  "long",
  "native",
  "new",
  "null",
  "package",
  "private",
  "protected",
  "public",
  "return",
  "short",
  "static",
  "super",
  "switch",
  "synchronized",
  "this",
  "throw",
  "throws",
  "transient",
  "true",
  "try",
  "typeof",
  "var",
  "void",
  "volatile",
  "while",
  "with",
  0
};


static void CheckSymbols(const char** strings) {
  for (const char* string = *strings; *strings != 0; string = *strings++) {
    Object* a;
    MaybeObject* maybe_a = HEAP->LookupAsciiSymbol(string);
    // LookupAsciiSymbol may return a failure if a GC is needed.
    if (!maybe_a->ToObject(&a)) continue;
    CHECK(a->IsSymbol());
    Object* b;
    MaybeObject* maybe_b = HEAP->LookupAsciiSymbol(string);
    if (!maybe_b->ToObject(&b)) continue;
    CHECK_EQ(b, a);
    CHECK(String::cast(b)->IsEqualTo(CStrVector(string)));
  }
}


TEST(SymbolTable) {
  InitializeVM();

  CheckSymbols(not_so_random_string_table);
  CheckSymbols(not_so_random_string_table);
}


TEST(FunctionAllocation) {
  InitializeVM();

  v8::HandleScope sc;
  Handle<String> name = FACTORY->LookupAsciiSymbol("theFunction");
  Handle<JSFunction> function =
      FACTORY->NewFunction(name, FACTORY->undefined_value());
  Handle<Map> initial_map =
      FACTORY->NewMap(JS_OBJECT_TYPE, JSObject::kHeaderSize);
  function->set_initial_map(*initial_map);

  Handle<String> prop_name = FACTORY->LookupAsciiSymbol("theSlot");
  Handle<JSObject> obj = FACTORY->NewJSObject(function);
  obj->SetProperty(
      *prop_name, Smi::FromInt(23), NONE, kNonStrictMode)->ToObjectChecked();
  CHECK_EQ(Smi::FromInt(23), obj->GetProperty(*prop_name));
  // Check that we can add properties to function objects.
  function->SetProperty(
      *prop_name, Smi::FromInt(24), NONE, kNonStrictMode)->ToObjectChecked();
  CHECK_EQ(Smi::FromInt(24), function->GetProperty(*prop_name));
}


TEST(ObjectProperties) {
  InitializeVM();

  v8::HandleScope sc;
  String* object_symbol = String::cast(HEAP->Object_symbol());
  Object* raw_object = Isolate::Current()->context()->global()->
      GetProperty(object_symbol)->ToObjectChecked();
  JSFunction* object_function = JSFunction::cast(raw_object);
  Handle<JSFunction> constructor(object_function);
  Handle<JSObject> obj = FACTORY->NewJSObject(constructor);
  Handle<String> first = FACTORY->LookupAsciiSymbol("first");
  Handle<String> second = FACTORY->LookupAsciiSymbol("second");

  // check for empty
  CHECK(!obj->HasLocalProperty(*first));

  // add first
  obj->SetProperty(
      *first, Smi::FromInt(1), NONE, kNonStrictMode)->ToObjectChecked();
  CHECK(obj->HasLocalProperty(*first));

  // delete first
  CHECK(obj->DeleteProperty(*first, JSObject::NORMAL_DELETION));
  CHECK(!obj->HasLocalProperty(*first));

  // add first and then second
  obj->SetProperty(
      *first, Smi::FromInt(1), NONE, kNonStrictMode)->ToObjectChecked();
  obj->SetProperty(
      *second, Smi::FromInt(2), NONE, kNonStrictMode)->ToObjectChecked();
  CHECK(obj->HasLocalProperty(*first));
  CHECK(obj->HasLocalProperty(*second));

  // delete first and then second
  CHECK(obj->DeleteProperty(*first, JSObject::NORMAL_DELETION));
  CHECK(obj->HasLocalProperty(*second));
  CHECK(obj->DeleteProperty(*second, JSObject::NORMAL_DELETION));
  CHECK(!obj->HasLocalProperty(*first));
  CHECK(!obj->HasLocalProperty(*second));

  // add first and then second
  obj->SetProperty(
      *first, Smi::FromInt(1), NONE, kNonStrictMode)->ToObjectChecked();
  obj->SetProperty(
      *second, Smi::FromInt(2), NONE, kNonStrictMode)->ToObjectChecked();
  CHECK(obj->HasLocalProperty(*first));
  CHECK(obj->HasLocalProperty(*second));

  // delete second and then first
  CHECK(obj->DeleteProperty(*second, JSObject::NORMAL_DELETION));
  CHECK(obj->HasLocalProperty(*first));
  CHECK(obj->DeleteProperty(*first, JSObject::NORMAL_DELETION));
  CHECK(!obj->HasLocalProperty(*first));
  CHECK(!obj->HasLocalProperty(*second));

  // check string and symbol match
  static const char* string1 = "fisk";
  Handle<String> s1 = FACTORY->NewStringFromAscii(CStrVector(string1));
  obj->SetProperty(
      *s1, Smi::FromInt(1), NONE, kNonStrictMode)->ToObjectChecked();
  Handle<String> s1_symbol = FACTORY->LookupAsciiSymbol(string1);
  CHECK(obj->HasLocalProperty(*s1_symbol));

  // check symbol and string match
  static const char* string2 = "fugl";
  Handle<String> s2_symbol = FACTORY->LookupAsciiSymbol(string2);
  obj->SetProperty(
      *s2_symbol, Smi::FromInt(1), NONE, kNonStrictMode)->ToObjectChecked();
  Handle<String> s2 = FACTORY->NewStringFromAscii(CStrVector(string2));
  CHECK(obj->HasLocalProperty(*s2));
}


TEST(JSObjectMaps) {
  InitializeVM();

  v8::HandleScope sc;
  Handle<String> name = FACTORY->LookupAsciiSymbol("theFunction");
  Handle<JSFunction> function =
      FACTORY->NewFunction(name, FACTORY->undefined_value());
  Handle<Map> initial_map =
      FACTORY->NewMap(JS_OBJECT_TYPE, JSObject::kHeaderSize);
  function->set_initial_map(*initial_map);

  Handle<String> prop_name = FACTORY->LookupAsciiSymbol("theSlot");
  Handle<JSObject> obj = FACTORY->NewJSObject(function);

  // Set a propery
  obj->SetProperty(
      *prop_name, Smi::FromInt(23), NONE, kNonStrictMode)->ToObjectChecked();
  CHECK_EQ(Smi::FromInt(23), obj->GetProperty(*prop_name));

  // Check the map has changed
  CHECK(*initial_map != obj->map());
}


TEST(JSArray) {
  InitializeVM();

  v8::HandleScope sc;
  Handle<String> name = FACTORY->LookupAsciiSymbol("Array");
  Object* raw_object = Isolate::Current()->context()->global()->
      GetProperty(*name)->ToObjectChecked();
  Handle<JSFunction> function = Handle<JSFunction>(
      JSFunction::cast(raw_object));

  // Allocate the object.
  Handle<JSObject> object = FACTORY->NewJSObject(function);
  Handle<JSArray> array = Handle<JSArray>::cast(object);
  // We just initialized the VM, no heap allocation failure yet.
  Object* ok = array->Initialize(0)->ToObjectChecked();

  // Set array length to 0.
  ok = array->SetElementsLength(Smi::FromInt(0))->ToObjectChecked();
  CHECK_EQ(Smi::FromInt(0), array->length());
  // Must be in fast mode.
  CHECK(array->HasFastTypeElements());

  // array[length] = name.
  ok = array->SetElement(0, *name, kNonStrictMode, true)->ToObjectChecked();
  CHECK_EQ(Smi::FromInt(1), array->length());
  CHECK_EQ(array->GetElement(0), *name);

  // Set array length with larger than smi value.
  Handle<Object> length =
      FACTORY->NewNumberFromUint(static_cast<uint32_t>(Smi::kMaxValue) + 1);
  ok = array->SetElementsLength(*length)->ToObjectChecked();

  uint32_t int_length = 0;
  CHECK(length->ToArrayIndex(&int_length));
  CHECK_EQ(*length, array->length());
  CHECK(array->HasDictionaryElements());  // Must be in slow mode.

  // array[length] = name.
  ok = array->SetElement(
      int_length, *name, kNonStrictMode, true)->ToObjectChecked();
  uint32_t new_int_length = 0;
  CHECK(array->length()->ToArrayIndex(&new_int_length));
  CHECK_EQ(static_cast<double>(int_length), new_int_length - 1);
  CHECK_EQ(array->GetElement(int_length), *name);
  CHECK_EQ(array->GetElement(0), *name);
}


TEST(JSObjectCopy) {
  InitializeVM();

  v8::HandleScope sc;
  String* object_symbol = String::cast(HEAP->Object_symbol());
  Object* raw_object = Isolate::Current()->context()->global()->
      GetProperty(object_symbol)->ToObjectChecked();
  JSFunction* object_function = JSFunction::cast(raw_object);
  Handle<JSFunction> constructor(object_function);
  Handle<JSObject> obj = FACTORY->NewJSObject(constructor);
  Handle<String> first = FACTORY->LookupAsciiSymbol("first");
  Handle<String> second = FACTORY->LookupAsciiSymbol("second");

  obj->SetProperty(
      *first, Smi::FromInt(1), NONE, kNonStrictMode)->ToObjectChecked();
  obj->SetProperty(
      *second, Smi::FromInt(2), NONE, kNonStrictMode)->ToObjectChecked();

  Object* ok =
      obj->SetElement(0, *first, kNonStrictMode, true)->ToObjectChecked();

  ok = obj->SetElement(1, *second, kNonStrictMode, true)->ToObjectChecked();

  // Make the clone.
  Handle<JSObject> clone = Copy(obj);
  CHECK(!clone.is_identical_to(obj));

  CHECK_EQ(obj->GetElement(0), clone->GetElement(0));
  CHECK_EQ(obj->GetElement(1), clone->GetElement(1));

  CHECK_EQ(obj->GetProperty(*first), clone->GetProperty(*first));
  CHECK_EQ(obj->GetProperty(*second), clone->GetProperty(*second));

  // Flip the values.
  clone->SetProperty(
      *first, Smi::FromInt(2), NONE, kNonStrictMode)->ToObjectChecked();
  clone->SetProperty(
      *second, Smi::FromInt(1), NONE, kNonStrictMode)->ToObjectChecked();

  ok = clone->SetElement(0, *second, kNonStrictMode, true)->ToObjectChecked();
  ok = clone->SetElement(1, *first, kNonStrictMode, true)->ToObjectChecked();

  CHECK_EQ(obj->GetElement(1), clone->GetElement(0));
  CHECK_EQ(obj->GetElement(0), clone->GetElement(1));

  CHECK_EQ(obj->GetProperty(*second), clone->GetProperty(*first));
  CHECK_EQ(obj->GetProperty(*first), clone->GetProperty(*second));
}


TEST(StringAllocation) {
  InitializeVM();


  const unsigned char chars[] = { 0xe5, 0xa4, 0xa7 };
  for (int length = 0; length < 100; length++) {
    v8::HandleScope scope;
    char* non_ascii = NewArray<char>(3 * length + 1);
    char* ascii = NewArray<char>(length + 1);
    non_ascii[3 * length] = 0;
    ascii[length] = 0;
    for (int i = 0; i < length; i++) {
      ascii[i] = 'a';
      non_ascii[3 * i] = chars[0];
      non_ascii[3 * i + 1] = chars[1];
      non_ascii[3 * i + 2] = chars[2];
    }
    Handle<String> non_ascii_sym =
        FACTORY->LookupSymbol(Vector<const char>(non_ascii, 3 * length));
    CHECK_EQ(length, non_ascii_sym->length());
    Handle<String> ascii_sym =
        FACTORY->LookupSymbol(Vector<const char>(ascii, length));
    CHECK_EQ(length, ascii_sym->length());
    Handle<String> non_ascii_str =
        FACTORY->NewStringFromUtf8(Vector<const char>(non_ascii, 3 * length));
    non_ascii_str->Hash();
    CHECK_EQ(length, non_ascii_str->length());
    Handle<String> ascii_str =
        FACTORY->NewStringFromUtf8(Vector<const char>(ascii, length));
    ascii_str->Hash();
    CHECK_EQ(length, ascii_str->length());
    DeleteArray(non_ascii);
    DeleteArray(ascii);
  }
}


static int ObjectsFoundInHeap(Handle<Object> objs[], int size) {
  // Count the number of objects found in the heap.
  int found_count = 0;
  HeapIterator iterator;
  for (HeapObject* obj = iterator.next(); obj != NULL; obj = iterator.next()) {
    for (int i = 0; i < size; i++) {
      if (*objs[i] == obj) {
        found_count++;
      }
    }
  }
  return found_count;
}


TEST(Iteration) {
  InitializeVM();
  v8::HandleScope scope;

  // Array of objects to scan haep for.
  const int objs_count = 6;
  Handle<Object> objs[objs_count];
  int next_objs_index = 0;

  // Allocate a JS array to OLD_POINTER_SPACE and NEW_SPACE
  objs[next_objs_index++] = FACTORY->NewJSArray(10);
  objs[next_objs_index++] = FACTORY->NewJSArray(10, TENURED);

  // Allocate a small string to OLD_DATA_SPACE and NEW_SPACE
  objs[next_objs_index++] =
      FACTORY->NewStringFromAscii(CStrVector("abcdefghij"));
  objs[next_objs_index++] =
      FACTORY->NewStringFromAscii(CStrVector("abcdefghij"), TENURED);

  // Allocate a large string (for large object space).
  int large_size = HEAP->MaxObjectSizeInPagedSpace() + 1;
  char* str = new char[large_size];
  for (int i = 0; i < large_size - 1; ++i) str[i] = 'a';
  str[large_size - 1] = '\0';
  objs[next_objs_index++] =
      FACTORY->NewStringFromAscii(CStrVector(str), TENURED);
  delete[] str;

  // Add a Map object to look for.
  objs[next_objs_index++] = Handle<Map>(HeapObject::cast(*objs[0])->map());

  CHECK_EQ(objs_count, next_objs_index);
  CHECK_EQ(objs_count, ObjectsFoundInHeap(objs, objs_count));
}


TEST(EmptyHandleEscapeFrom) {
  InitializeVM();

  v8::HandleScope scope;
  Handle<JSObject> runaway;

  {
      v8::HandleScope nested;
      Handle<JSObject> empty;
      runaway = empty.EscapeFrom(&nested);
  }

  CHECK(runaway.is_null());
}


static int LenFromSize(int size) {
  return (size - FixedArray::kHeaderSize) / kPointerSize;
}


TEST(Regression39128) {
  // Test case for crbug.com/39128.
  InitializeVM();

  // Increase the chance of 'bump-the-pointer' allocation in old space.
  HEAP->CollectAllGarbage(Heap::kNoGCFlags);

  v8::HandleScope scope;

  // The plan: create JSObject which references objects in new space.
  // Then clone this object (forcing it to go into old space) and check
  // that region dirty marks are updated correctly.

  // Step 1: prepare a map for the object.  We add 1 inobject property to it.
  Handle<JSFunction> object_ctor(
      Isolate::Current()->global_context()->object_function());
  CHECK(object_ctor->has_initial_map());
  Handle<Map> object_map(object_ctor->initial_map());
  // Create a map with single inobject property.
  Handle<Map> my_map = FACTORY->CopyMap(object_map, 1);
  int n_properties = my_map->inobject_properties();
  CHECK_GT(n_properties, 0);

  int object_size = my_map->instance_size();

  // Step 2: allocate a lot of objects so to almost fill new space: we need
  // just enough room to allocate JSObject and thus fill the newspace.

  int allocation_amount = Min(FixedArray::kMaxSize,
                              HEAP->MaxObjectSizeInNewSpace());
  int allocation_len = LenFromSize(allocation_amount);
  NewSpace* new_space = HEAP->new_space();
  Address* top_addr = new_space->allocation_top_address();
  Address* limit_addr = new_space->allocation_limit_address();
  while ((*limit_addr - *top_addr) > allocation_amount) {
    CHECK(!HEAP->always_allocate());
    Object* array = HEAP->AllocateFixedArray(allocation_len)->ToObjectChecked();
    CHECK(!array->IsFailure());
    CHECK(new_space->Contains(array));
  }

  // Step 3: now allocate fixed array and JSObject to fill the whole new space.
  int to_fill = static_cast<int>(*limit_addr - *top_addr - object_size);
  int fixed_array_len = LenFromSize(to_fill);
  CHECK(fixed_array_len < FixedArray::kMaxLength);

  CHECK(!HEAP->always_allocate());
  Object* array = HEAP->AllocateFixedArray(fixed_array_len)->ToObjectChecked();
  CHECK(!array->IsFailure());
  CHECK(new_space->Contains(array));

  Object* object = HEAP->AllocateJSObjectFromMap(*my_map)->ToObjectChecked();
  CHECK(new_space->Contains(object));
  JSObject* jsobject = JSObject::cast(object);
  CHECK_EQ(0, FixedArray::cast(jsobject->elements())->length());
  CHECK_EQ(0, jsobject->properties()->length());
  // Create a reference to object in new space in jsobject.
  jsobject->FastPropertyAtPut(-1, array);

  CHECK_EQ(0, static_cast<int>(*limit_addr - *top_addr));

  // Step 4: clone jsobject, but force always allocate first to create a clone
  // in old pointer space.
  Address old_pointer_space_top = HEAP->old_pointer_space()->top();
  AlwaysAllocateScope aa_scope;
  Object* clone_obj = HEAP->CopyJSObject(jsobject)->ToObjectChecked();
  JSObject* clone = JSObject::cast(clone_obj);
  if (clone->address() != old_pointer_space_top) {
    // Alas, got allocated from free list, we cannot do checks.
    return;
  }
  CHECK(HEAP->old_pointer_space()->Contains(clone->address()));
}


TEST(TestCodeFlushing) {
  i::FLAG_allow_natives_syntax = true;
  // If we do not flush code this test is invalid.
  if (!FLAG_flush_code) return;
  InitializeVM();
  v8::HandleScope scope;
  const char* source = "function foo() {"
                       "  var x = 42;"
                       "  var y = 42;"
                       "  var z = x + y;"
                       "};"
                       "foo()";
  Handle<String> foo_name = FACTORY->LookupAsciiSymbol("foo");

  // This compile will add the code to the compilation cache.
  { v8::HandleScope scope;
    CompileRun(source);
  }

  // Check function is compiled.
  Object* func_value = Isolate::Current()->context()->global()->
      GetProperty(*foo_name)->ToObjectChecked();
  CHECK(func_value->IsJSFunction());
  Handle<JSFunction> function(JSFunction::cast(func_value));
  CHECK(function->shared()->is_compiled());

  // TODO(1609) Currently incremental marker does not support code flushing.
  HEAP->CollectAllGarbage(Heap::kMakeHeapIterableMask);
  HEAP->CollectAllGarbage(Heap::kMakeHeapIterableMask);

  CHECK(function->shared()->is_compiled());

  HEAP->CollectAllGarbage(Heap::kMakeHeapIterableMask);
  HEAP->CollectAllGarbage(Heap::kMakeHeapIterableMask);
  HEAP->CollectAllGarbage(Heap::kMakeHeapIterableMask);
  HEAP->CollectAllGarbage(Heap::kMakeHeapIterableMask);
  HEAP->CollectAllGarbage(Heap::kMakeHeapIterableMask);
  HEAP->CollectAllGarbage(Heap::kMakeHeapIterableMask);

  // foo should no longer be in the compilation cache
  CHECK(!function->shared()->is_compiled() || function->IsOptimized());
  CHECK(!function->is_compiled() || function->IsOptimized());
  // Call foo to get it recompiled.
  CompileRun("foo()");
  CHECK(function->shared()->is_compiled());
  CHECK(function->is_compiled());
}


// Count the number of global contexts in the weak list of global contexts.
static int CountGlobalContexts() {
  int count = 0;
  Object* object = HEAP->global_contexts_list();
  while (!object->IsUndefined()) {
    count++;
    object = Context::cast(object)->get(Context::NEXT_CONTEXT_LINK);
  }
  return count;
}


// Count the number of user functions in the weak list of optimized
// functions attached to a global context.
static int CountOptimizedUserFunctions(v8::Handle<v8::Context> context) {
  int count = 0;
  Handle<Context> icontext = v8::Utils::OpenHandle(*context);
  Object* object = icontext->get(Context::OPTIMIZED_FUNCTIONS_LIST);
  while (object->IsJSFunction() && !JSFunction::cast(object)->IsBuiltin()) {
    count++;
    object = JSFunction::cast(object)->next_function_link();
  }
  return count;
}


TEST(TestInternalWeakLists) {
  v8::V8::Initialize();

  static const int kNumTestContexts = 10;

  v8::HandleScope scope;
  v8::Persistent<v8::Context> ctx[kNumTestContexts];

  CHECK_EQ(0, CountGlobalContexts());

  // Create a number of global contests which gets linked together.
  for (int i = 0; i < kNumTestContexts; i++) {
    ctx[i] = v8::Context::New();

    bool opt = (FLAG_always_opt && i::V8::UseCrankshaft());

    CHECK_EQ(i + 1, CountGlobalContexts());

    ctx[i]->Enter();

    // Create a handle scope so no function objects get stuch in the outer
    // handle scope
    v8::HandleScope scope;
    const char* source = "function f1() { };"
                         "function f2() { };"
                         "function f3() { };"
                         "function f4() { };"
                         "function f5() { };";
    CompileRun(source);
    CHECK_EQ(0, CountOptimizedUserFunctions(ctx[i]));
    CompileRun("f1()");
    CHECK_EQ(opt ? 1 : 0, CountOptimizedUserFunctions(ctx[i]));
    CompileRun("f2()");
    CHECK_EQ(opt ? 2 : 0, CountOptimizedUserFunctions(ctx[i]));
    CompileRun("f3()");
    CHECK_EQ(opt ? 3 : 0, CountOptimizedUserFunctions(ctx[i]));
    CompileRun("f4()");
    CHECK_EQ(opt ? 4 : 0, CountOptimizedUserFunctions(ctx[i]));
    CompileRun("f5()");
    CHECK_EQ(opt ? 5 : 0, CountOptimizedUserFunctions(ctx[i]));

    // Remove function f1, and
    CompileRun("f1=null");

    // Scavenge treats these references as strong.
    for (int j = 0; j < 10; j++) {
      HEAP->PerformScavenge();
      CHECK_EQ(opt ? 5 : 0, CountOptimizedUserFunctions(ctx[i]));
    }

    // Mark compact handles the weak references.
    HEAP->CollectAllGarbage(Heap::kNoGCFlags);
    CHECK_EQ(opt ? 4 : 0, CountOptimizedUserFunctions(ctx[i]));

    // Get rid of f3 and f5 in the same way.
    CompileRun("f3=null");
    for (int j = 0; j < 10; j++) {
      HEAP->PerformScavenge();
      CHECK_EQ(opt ? 4 : 0, CountOptimizedUserFunctions(ctx[i]));
    }
    HEAP->CollectAllGarbage(Heap::kNoGCFlags);
    CHECK_EQ(opt ? 3 : 0, CountOptimizedUserFunctions(ctx[i]));
    CompileRun("f5=null");
    for (int j = 0; j < 10; j++) {
      HEAP->PerformScavenge();
      CHECK_EQ(opt ? 3 : 0, CountOptimizedUserFunctions(ctx[i]));
    }
    HEAP->CollectAllGarbage(Heap::kNoGCFlags);
    CHECK_EQ(opt ? 2 : 0, CountOptimizedUserFunctions(ctx[i]));

    ctx[i]->Exit();
  }

  // Force compilation cache cleanup.
  HEAP->CollectAllGarbage(Heap::kNoGCFlags);

  // Dispose the global contexts one by one.
  for (int i = 0; i < kNumTestContexts; i++) {
    ctx[i].Dispose();
    ctx[i].Clear();

    // Scavenge treats these references as strong.
    for (int j = 0; j < 10; j++) {
      HEAP->PerformScavenge();
      CHECK_EQ(kNumTestContexts - i, CountGlobalContexts());
    }

    // Mark compact handles the weak references.
    HEAP->CollectAllGarbage(Heap::kNoGCFlags);
    CHECK_EQ(kNumTestContexts - i - 1, CountGlobalContexts());
  }

  CHECK_EQ(0, CountGlobalContexts());
}


// Count the number of global contexts in the weak list of global contexts
// causing a GC after the specified number of elements.
static int CountGlobalContextsWithGC(int n) {
  int count = 0;
  Handle<Object> object(HEAP->global_contexts_list());
  while (!object->IsUndefined()) {
    count++;
    if (count == n) HEAP->CollectAllGarbage(Heap::kNoGCFlags);
    object =
        Handle<Object>(Context::cast(*object)->get(Context::NEXT_CONTEXT_LINK));
  }
  return count;
}


// Count the number of user functions in the weak list of optimized
// functions attached to a global context causing a GC after the
// specified number of elements.
static int CountOptimizedUserFunctionsWithGC(v8::Handle<v8::Context> context,
                                             int n) {
  int count = 0;
  Handle<Context> icontext = v8::Utils::OpenHandle(*context);
  Handle<Object> object(icontext->get(Context::OPTIMIZED_FUNCTIONS_LIST));
  while (object->IsJSFunction() &&
         !Handle<JSFunction>::cast(object)->IsBuiltin()) {
    count++;
    if (count == n) HEAP->CollectAllGarbage(Heap::kNoGCFlags);
    object = Handle<Object>(
        Object::cast(JSFunction::cast(*object)->next_function_link()));
  }
  return count;
}


TEST(TestInternalWeakListsTraverseWithGC) {
  v8::V8::Initialize();

  static const int kNumTestContexts = 10;

  v8::HandleScope scope;
  v8::Persistent<v8::Context> ctx[kNumTestContexts];

  CHECK_EQ(0, CountGlobalContexts());

  // Create an number of contexts and check the length of the weak list both
  // with and without GCs while iterating the list.
  for (int i = 0; i < kNumTestContexts; i++) {
    ctx[i] = v8::Context::New();
    CHECK_EQ(i + 1, CountGlobalContexts());
    CHECK_EQ(i + 1, CountGlobalContextsWithGC(i / 2 + 1));
  }

  bool opt = (FLAG_always_opt && i::V8::UseCrankshaft());

  // Compile a number of functions the length of the weak list of optimized
  // functions both with and without GCs while iterating the list.
  ctx[0]->Enter();
  const char* source = "function f1() { };"
                       "function f2() { };"
                       "function f3() { };"
                       "function f4() { };"
                       "function f5() { };";
  CompileRun(source);
  CHECK_EQ(0, CountOptimizedUserFunctions(ctx[0]));
  CompileRun("f1()");
  CHECK_EQ(opt ? 1 : 0, CountOptimizedUserFunctions(ctx[0]));
  CHECK_EQ(opt ? 1 : 0, CountOptimizedUserFunctionsWithGC(ctx[0], 1));
  CompileRun("f2()");
  CHECK_EQ(opt ? 2 : 0, CountOptimizedUserFunctions(ctx[0]));
  CHECK_EQ(opt ? 2 : 0, CountOptimizedUserFunctionsWithGC(ctx[0], 1));
  CompileRun("f3()");
  CHECK_EQ(opt ? 3 : 0, CountOptimizedUserFunctions(ctx[0]));
  CHECK_EQ(opt ? 3 : 0, CountOptimizedUserFunctionsWithGC(ctx[0], 1));
  CompileRun("f4()");
  CHECK_EQ(opt ? 4 : 0, CountOptimizedUserFunctions(ctx[0]));
  CHECK_EQ(opt ? 4 : 0, CountOptimizedUserFunctionsWithGC(ctx[0], 2));
  CompileRun("f5()");
  CHECK_EQ(opt ? 5 : 0, CountOptimizedUserFunctions(ctx[0]));
  CHECK_EQ(opt ? 5 : 0, CountOptimizedUserFunctionsWithGC(ctx[0], 4));

  ctx[0]->Exit();
}


TEST(TestSizeOfObjectsVsHeapIteratorPrecision) {
  InitializeVM();
  HEAP->EnsureHeapIsIterable();
  intptr_t size_of_objects_1 = HEAP->SizeOfObjects();
  HeapIterator iterator;
  intptr_t size_of_objects_2 = 0;
  for (HeapObject* obj = iterator.next();
       obj != NULL;
       obj = iterator.next()) {
    size_of_objects_2 += obj->Size();
  }
  // Delta must be within 5% of the larger result.
  // TODO(gc): Tighten this up by distinguishing between byte
  // arrays that are real and those that merely mark free space
  // on the heap.
  if (size_of_objects_1 > size_of_objects_2) {
    intptr_t delta = size_of_objects_1 - size_of_objects_2;
    PrintF("Heap::SizeOfObjects: %" V8_PTR_PREFIX "d, "
           "Iterator: %" V8_PTR_PREFIX "d, "
           "delta: %" V8_PTR_PREFIX "d\n",
           size_of_objects_1, size_of_objects_2, delta);
    CHECK_GT(size_of_objects_1 / 20, delta);
  } else {
    intptr_t delta = size_of_objects_2 - size_of_objects_1;
    PrintF("Heap::SizeOfObjects: %" V8_PTR_PREFIX "d, "
           "Iterator: %" V8_PTR_PREFIX "d, "
           "delta: %" V8_PTR_PREFIX "d\n",
           size_of_objects_1, size_of_objects_2, delta);
    CHECK_GT(size_of_objects_2 / 20, delta);
  }
}


TEST(GrowAndShrinkNewSpace) {
  InitializeVM();
  NewSpace* new_space = HEAP->new_space();

  // Explicitly growing should double the space capacity.
  intptr_t old_capacity, new_capacity;
  old_capacity = new_space->Capacity();
  new_space->Grow();
  new_capacity = new_space->Capacity();
  CHECK(2 * old_capacity == new_capacity);

  // Fill up new space to the point that it is completely full. Make sure
  // that the scavenger does not undo the filling.
  old_capacity = new_space->Capacity();
  {
    v8::HandleScope scope;
    AlwaysAllocateScope always_allocate;
    intptr_t available = new_space->EffectiveCapacity() - new_space->Size();
    intptr_t number_of_fillers = (available / FixedArray::SizeFor(1000)) - 10;
    for (intptr_t i = 0; i < number_of_fillers; i++) {
      CHECK(HEAP->InNewSpace(*FACTORY->NewFixedArray(1000, NOT_TENURED)));
    }
  }
  new_capacity = new_space->Capacity();
  CHECK(old_capacity == new_capacity);

  // Explicitly shrinking should not affect space capacity.
  old_capacity = new_space->Capacity();
  new_space->Shrink();
  new_capacity = new_space->Capacity();
  CHECK(old_capacity == new_capacity);

  // Let the scavenger empty the new space.
  HEAP->CollectGarbage(NEW_SPACE);
  CHECK_LE(new_space->Size(), old_capacity);

  // Explicitly shrinking should halve the space capacity.
  old_capacity = new_space->Capacity();
  new_space->Shrink();
  new_capacity = new_space->Capacity();
  CHECK(old_capacity == 2 * new_capacity);

  // Consecutive shrinking should not affect space capacity.
  old_capacity = new_space->Capacity();
  new_space->Shrink();
  new_space->Shrink();
  new_space->Shrink();
  new_capacity = new_space->Capacity();
  CHECK(old_capacity == new_capacity);
}
