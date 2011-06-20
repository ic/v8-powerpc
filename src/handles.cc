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

#include "accessors.h"
#include "api.h"
#include "arguments.h"
#include "bootstrapper.h"
#include "compiler.h"
#include "debug.h"
#include "execution.h"
#include "global-handles.h"
#include "natives.h"
#include "runtime.h"
#include "string-search.h"
#include "stub-cache.h"
#include "vm-state-inl.h"

namespace v8 {
namespace internal {


int HandleScope::NumberOfHandles() {
  Isolate* isolate = Isolate::Current();
  HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  int n = impl->blocks()->length();
  if (n == 0) return 0;
  return ((n - 1) * kHandleBlockSize) + static_cast<int>(
      (isolate->handle_scope_data()->next - impl->blocks()->last()));
}


Object** HandleScope::Extend() {
  Isolate* isolate = Isolate::Current();
  v8::ImplementationUtilities::HandleScopeData* current =
      isolate->handle_scope_data();

  Object** result = current->next;

  ASSERT(result == current->limit);
  // Make sure there's at least one scope on the stack and that the
  // top of the scope stack isn't a barrier.
  if (current->level == 0) {
    Utils::ReportApiFailure("v8::HandleScope::CreateHandle()",
                            "Cannot create a handle without a HandleScope");
    return NULL;
  }
  HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  // If there's more room in the last block, we use that. This is used
  // for fast creation of scopes after scope barriers.
  if (!impl->blocks()->is_empty()) {
    Object** limit = &impl->blocks()->last()[kHandleBlockSize];
    if (current->limit != limit) {
      current->limit = limit;
      ASSERT(limit - current->next < kHandleBlockSize);
    }
  }

  // If we still haven't found a slot for the handle, we extend the
  // current handle scope by allocating a new handle block.
  if (result == current->limit) {
    // If there's a spare block, use it for growing the current scope.
    result = impl->GetSpareOrNewBlock();
    // Add the extension to the global list of blocks, but count the
    // extension as part of the current scope.
    impl->blocks()->Add(result);
    current->limit = &result[kHandleBlockSize];
  }

  return result;
}


void HandleScope::DeleteExtensions(Isolate* isolate) {
  ASSERT(isolate == Isolate::Current());
  v8::ImplementationUtilities::HandleScopeData* current =
      isolate->handle_scope_data();
  isolate->handle_scope_implementer()->DeleteExtensions(current->limit);
}


void HandleScope::ZapRange(Object** start, Object** end) {
  ASSERT(end - start <= kHandleBlockSize);
  for (Object** p = start; p != end; p++) {
    *reinterpret_cast<Address*>(p) = v8::internal::kHandleZapValue;
  }
}


Address HandleScope::current_level_address() {
  return reinterpret_cast<Address>(
      &Isolate::Current()->handle_scope_data()->level);
}


Address HandleScope::current_next_address() {
  return reinterpret_cast<Address>(
      &Isolate::Current()->handle_scope_data()->next);
}


Address HandleScope::current_limit_address() {
  return reinterpret_cast<Address>(
      &Isolate::Current()->handle_scope_data()->limit);
}


Handle<FixedArray> AddKeysFromJSArray(Handle<FixedArray> content,
                                      Handle<JSArray> array) {
  CALL_HEAP_FUNCTION(content->GetIsolate(),
                     content->AddKeysFromJSArray(*array), FixedArray);
}


Handle<FixedArray> UnionOfKeys(Handle<FixedArray> first,
                               Handle<FixedArray> second) {
  CALL_HEAP_FUNCTION(first->GetIsolate(),
                     first->UnionOfKeys(*second), FixedArray);
}


Handle<JSGlobalProxy> ReinitializeJSGlobalProxy(
    Handle<JSFunction> constructor,
    Handle<JSGlobalProxy> global) {
  CALL_HEAP_FUNCTION(
      constructor->GetIsolate(),
      constructor->GetHeap()->ReinitializeJSGlobalProxy(*constructor, *global),
      JSGlobalProxy);
}


void SetExpectedNofProperties(Handle<JSFunction> func, int nof) {
  // If objects constructed from this function exist then changing
  // 'estimated_nof_properties' is dangerous since the previous value might
  // have been compiled into the fast construct stub. More over, the inobject
  // slack tracking logic might have adjusted the previous value, so even
  // passing the same value is risky.
  if (func->shared()->live_objects_may_exist()) return;

  func->shared()->set_expected_nof_properties(nof);
  if (func->has_initial_map()) {
    Handle<Map> new_initial_map =
        func->GetIsolate()->factory()->CopyMapDropTransitions(
            Handle<Map>(func->initial_map()));
    new_initial_map->set_unused_property_fields(nof);
    func->set_initial_map(*new_initial_map);
  }
}


void SetPrototypeProperty(Handle<JSFunction> func, Handle<JSObject> value) {
  CALL_HEAP_FUNCTION_VOID(func->GetIsolate(),
                          func->SetPrototype(*value));
}


static int ExpectedNofPropertiesFromEstimate(int estimate) {
  // If no properties are added in the constructor, they are more likely
  // to be added later.
  if (estimate == 0) estimate = 2;

  // We do not shrink objects that go into a snapshot (yet), so we adjust
  // the estimate conservatively.
  if (Serializer::enabled()) return estimate + 2;

  // Inobject slack tracking will reclaim redundant inobject space later,
  // so we can afford to adjust the estimate generously.
  return estimate + 8;
}


void SetExpectedNofPropertiesFromEstimate(Handle<SharedFunctionInfo> shared,
                                          int estimate) {
  // See the comment in SetExpectedNofProperties.
  if (shared->live_objects_may_exist()) return;

  shared->set_expected_nof_properties(
      ExpectedNofPropertiesFromEstimate(estimate));
}


void NormalizeProperties(Handle<JSObject> object,
                         PropertyNormalizationMode mode,
                         int expected_additional_properties) {
  CALL_HEAP_FUNCTION_VOID(object->GetIsolate(),
                          object->NormalizeProperties(
                              mode,
                              expected_additional_properties));
}


void NormalizeElements(Handle<JSObject> object) {
  CALL_HEAP_FUNCTION_VOID(object->GetIsolate(),
                          object->NormalizeElements());
}


void TransformToFastProperties(Handle<JSObject> object,
                               int unused_property_fields) {
  CALL_HEAP_FUNCTION_VOID(
      object->GetIsolate(),
      object->TransformToFastProperties(unused_property_fields));
}


Handle<NumberDictionary> NumberDictionarySet(
    Handle<NumberDictionary> dictionary,
    uint32_t index,
    Handle<Object> value,
    PropertyDetails details) {
  CALL_HEAP_FUNCTION(dictionary->GetIsolate(),
                     dictionary->Set(index, *value, details),
                     NumberDictionary);
}


void FlattenString(Handle<String> string) {
  CALL_HEAP_FUNCTION_VOID(string->GetIsolate(), string->TryFlatten());
}


Handle<String> FlattenGetString(Handle<String> string) {
  CALL_HEAP_FUNCTION(string->GetIsolate(), string->TryFlatten(), String);
}


Handle<Object> SetPrototype(Handle<JSFunction> function,
                            Handle<Object> prototype) {
  ASSERT(function->should_have_prototype());
  CALL_HEAP_FUNCTION(function->GetIsolate(),
                     Accessors::FunctionSetPrototype(*function,
                                                     *prototype,
                                                     NULL),
                     Object);
}


Handle<Object> SetProperty(Handle<JSReceiver> object,
                           Handle<String> key,
                           Handle<Object> value,
                           PropertyAttributes attributes,
                           StrictModeFlag strict_mode) {
  CALL_HEAP_FUNCTION(object->GetIsolate(),
                     object->SetProperty(*key, *value, attributes, strict_mode),
                     Object);
}


Handle<Object> SetProperty(Handle<Object> object,
                           Handle<Object> key,
                           Handle<Object> value,
                           PropertyAttributes attributes,
                           StrictModeFlag strict_mode) {
  Isolate* isolate = Isolate::Current();
  CALL_HEAP_FUNCTION(
      isolate,
      Runtime::SetObjectProperty(
          isolate, object, key, value, attributes, strict_mode),
      Object);
}


Handle<Object> ForceSetProperty(Handle<JSObject> object,
                                Handle<Object> key,
                                Handle<Object> value,
                                PropertyAttributes attributes) {
  Isolate* isolate = object->GetIsolate();
  CALL_HEAP_FUNCTION(
      isolate,
      Runtime::ForceSetObjectProperty(
          isolate, object, key, value, attributes),
      Object);
}


Handle<Object> SetNormalizedProperty(Handle<JSObject> object,
                                     Handle<String> key,
                                     Handle<Object> value,
                                     PropertyDetails details) {
  CALL_HEAP_FUNCTION(object->GetIsolate(),
                     object->SetNormalizedProperty(*key, *value, details),
                     Object);
}


Handle<Object> ForceDeleteProperty(Handle<JSObject> object,
                                   Handle<Object> key) {
  Isolate* isolate = object->GetIsolate();
  CALL_HEAP_FUNCTION(isolate,
                     Runtime::ForceDeleteObjectProperty(isolate, object, key),
                     Object);
}


Handle<Object> SetLocalPropertyIgnoreAttributes(
    Handle<JSObject> object,
    Handle<String> key,
    Handle<Object> value,
    PropertyAttributes attributes) {
  CALL_HEAP_FUNCTION(
    object->GetIsolate(),
    object->SetLocalPropertyIgnoreAttributes(*key, *value, attributes),
    Object);
}


void SetLocalPropertyNoThrow(Handle<JSObject> object,
                             Handle<String> key,
                             Handle<Object> value,
                             PropertyAttributes attributes) {
  Isolate* isolate = object->GetIsolate();
  ASSERT(!isolate->has_pending_exception());
  CHECK(!SetLocalPropertyIgnoreAttributes(
        object, key, value, attributes).is_null());
  CHECK(!isolate->has_pending_exception());
}


Handle<Object> SetPropertyWithInterceptor(Handle<JSObject> object,
                                          Handle<String> key,
                                          Handle<Object> value,
                                          PropertyAttributes attributes,
                                          StrictModeFlag strict_mode) {
  CALL_HEAP_FUNCTION(object->GetIsolate(),
                     object->SetPropertyWithInterceptor(*key,
                                                        *value,
                                                        attributes,
                                                        strict_mode),
                     Object);
}


Handle<Object> GetProperty(Handle<JSReceiver> obj,
                           const char* name) {
  Isolate* isolate = obj->GetIsolate();
  Handle<String> str = isolate->factory()->LookupAsciiSymbol(name);
  CALL_HEAP_FUNCTION(isolate, obj->GetProperty(*str), Object);
}


Handle<Object> GetProperty(Handle<Object> obj,
                           Handle<Object> key) {
  Isolate* isolate = Isolate::Current();
  CALL_HEAP_FUNCTION(isolate,
                     Runtime::GetObjectProperty(isolate, obj, key), Object);
}


Handle<Object> GetProperty(Handle<JSReceiver> obj,
                           Handle<String> name,
                           LookupResult* result) {
  PropertyAttributes attributes;
  Isolate* isolate = Isolate::Current();
  CALL_HEAP_FUNCTION(isolate,
                     obj->GetProperty(*obj, result, *name, &attributes),
                     Object);
}


Handle<Object> GetElement(Handle<Object> obj,
                          uint32_t index) {
  Isolate* isolate = Isolate::Current();
  CALL_HEAP_FUNCTION(isolate, Runtime::GetElement(obj, index), Object);
}


Handle<Object> GetPropertyWithInterceptor(Handle<JSObject> receiver,
                                          Handle<JSObject> holder,
                                          Handle<String> name,
                                          PropertyAttributes* attributes) {
  Isolate* isolate = receiver->GetIsolate();
  CALL_HEAP_FUNCTION(isolate,
                     holder->GetPropertyWithInterceptor(*receiver,
                                                        *name,
                                                        attributes),
                     Object);
}


Handle<Object> GetPrototype(Handle<Object> obj) {
  Handle<Object> result(obj->GetPrototype());
  return result;
}


Handle<Object> SetPrototype(Handle<JSObject> obj, Handle<Object> value) {
  const bool skip_hidden_prototypes = false;
  CALL_HEAP_FUNCTION(obj->GetIsolate(),
                     obj->SetPrototype(*value, skip_hidden_prototypes), Object);
}


Handle<Object> PreventExtensions(Handle<JSObject> object) {
  CALL_HEAP_FUNCTION(object->GetIsolate(), object->PreventExtensions(), Object);
}


Handle<Object> GetHiddenProperties(Handle<JSObject> obj,
                                   bool create_if_needed) {
  Isolate* isolate = obj->GetIsolate();
  Object* holder = obj->BypassGlobalProxy();
  if (holder->IsUndefined()) return isolate->factory()->undefined_value();
  obj = Handle<JSObject>(JSObject::cast(holder), isolate);

  if (obj->HasFastProperties()) {
    // If the object has fast properties, check whether the first slot
    // in the descriptor array matches the hidden symbol. Since the
    // hidden symbols hash code is zero (and no other string has hash
    // code zero) it will always occupy the first entry if present.
    DescriptorArray* descriptors = obj->map()->instance_descriptors();
    if ((descriptors->number_of_descriptors() > 0) &&
        (descriptors->GetKey(0) == isolate->heap()->hidden_symbol()) &&
        descriptors->IsProperty(0)) {
      ASSERT(descriptors->GetType(0) == FIELD);
      return Handle<Object>(obj->FastPropertyAt(descriptors->GetFieldIndex(0)),
                            isolate);
    }
  }

  // Only attempt to find the hidden properties in the local object and not
  // in the prototype chain.  Note that HasLocalProperty() can cause a GC in
  // the general case in the presence of interceptors.
  if (!obj->HasHiddenPropertiesObject()) {
    // Hidden properties object not found. Allocate a new hidden properties
    // object if requested. Otherwise return the undefined value.
    if (create_if_needed) {
      Handle<Object> hidden_obj =
          isolate->factory()->NewJSObject(isolate->object_function());
      CALL_HEAP_FUNCTION(isolate,
                         obj->SetHiddenPropertiesObject(*hidden_obj), Object);
    } else {
      return isolate->factory()->undefined_value();
    }
  }
  return Handle<Object>(obj->GetHiddenPropertiesObject(), isolate);
}


Handle<Object> DeleteElement(Handle<JSObject> obj,
                             uint32_t index) {
  CALL_HEAP_FUNCTION(obj->GetIsolate(),
                     obj->DeleteElement(index, JSObject::NORMAL_DELETION),
                     Object);
}


Handle<Object> DeleteProperty(Handle<JSObject> obj,
                              Handle<String> prop) {
  CALL_HEAP_FUNCTION(obj->GetIsolate(),
                     obj->DeleteProperty(*prop, JSObject::NORMAL_DELETION),
                     Object);
}


Handle<Object> LookupSingleCharacterStringFromCode(uint32_t index) {
  Isolate* isolate = Isolate::Current();
  CALL_HEAP_FUNCTION(
      isolate,
      isolate->heap()->LookupSingleCharacterStringFromCode(index), Object);
}


Handle<String> SubString(Handle<String> str,
                         int start,
                         int end,
                         PretenureFlag pretenure) {
  CALL_HEAP_FUNCTION(str->GetIsolate(),
                     str->SubString(start, end, pretenure), String);
}


Handle<Object> SetElement(Handle<JSObject> object,
                          uint32_t index,
                          Handle<Object> value,
                          StrictModeFlag strict_mode) {
  if (object->HasExternalArrayElements()) {
    if (!value->IsSmi() && !value->IsHeapNumber() && !value->IsUndefined()) {
      bool has_exception;
      Handle<Object> number = Execution::ToNumber(value, &has_exception);
      if (has_exception) return Handle<Object>();
      value = number;
    }
  }
  CALL_HEAP_FUNCTION(object->GetIsolate(),
                     object->SetElement(index, *value, strict_mode, true),
                     Object);
}


Handle<Object> SetOwnElement(Handle<JSObject> object,
                             uint32_t index,
                             Handle<Object> value,
                             StrictModeFlag strict_mode) {
  ASSERT(!object->HasExternalArrayElements());
  CALL_HEAP_FUNCTION(object->GetIsolate(),
                     object->SetElement(index, *value, strict_mode, false),
                     Object);
}


Handle<JSObject> Copy(Handle<JSObject> obj) {
  Isolate* isolate = obj->GetIsolate();
  CALL_HEAP_FUNCTION(isolate,
                     isolate->heap()->CopyJSObject(*obj), JSObject);
}


Handle<Object> SetAccessor(Handle<JSObject> obj, Handle<AccessorInfo> info) {
  CALL_HEAP_FUNCTION(obj->GetIsolate(), obj->DefineAccessor(*info), Object);
}


// Wrappers for scripts are kept alive and cached in weak global
// handles referred from foreign objects held by the scripts as long as
// they are used. When they are not used anymore, the garbage
// collector will call the weak callback on the global handle
// associated with the wrapper and get rid of both the wrapper and the
// handle.
static void ClearWrapperCache(Persistent<v8::Value> handle, void*) {
#ifdef ENABLE_HEAP_PROTECTION
  // Weak reference callbacks are called as if from outside V8.  We
  // need to reeenter to unprotect the heap.
  VMState state(OTHER);
#endif
  Handle<Object> cache = Utils::OpenHandle(*handle);
  JSValue* wrapper = JSValue::cast(*cache);
  Foreign* foreign = Script::cast(wrapper->value())->wrapper();
  ASSERT(foreign->address() == reinterpret_cast<Address>(cache.location()));
  foreign->set_address(0);
  Isolate* isolate = Isolate::Current();
  isolate->global_handles()->Destroy(cache.location());
  isolate->counters()->script_wrappers()->Decrement();
}


Handle<JSValue> GetScriptWrapper(Handle<Script> script) {
  if (script->wrapper()->address() != NULL) {
    // Return the script wrapper directly from the cache.
    return Handle<JSValue>(
        reinterpret_cast<JSValue**>(script->wrapper()->address()));
  }
  Isolate* isolate = Isolate::Current();
  // Construct a new script wrapper.
  isolate->counters()->script_wrappers()->Increment();
  Handle<JSFunction> constructor = isolate->script_function();
  Handle<JSValue> result =
      Handle<JSValue>::cast(isolate->factory()->NewJSObject(constructor));
  result->set_value(*script);

  // Create a new weak global handle and use it to cache the wrapper
  // for future use. The cache will automatically be cleared by the
  // garbage collector when it is not used anymore.
  Handle<Object> handle = isolate->global_handles()->Create(*result);
  isolate->global_handles()->MakeWeak(handle.location(), NULL,
                                      &ClearWrapperCache);
  script->wrapper()->set_address(reinterpret_cast<Address>(handle.location()));
  return result;
}


// Init line_ends array with code positions of line ends inside script
// source.
void InitScriptLineEnds(Handle<Script> script) {
  if (!script->line_ends()->IsUndefined()) return;

  Isolate* isolate = script->GetIsolate();

  if (!script->source()->IsString()) {
    ASSERT(script->source()->IsUndefined());
    Handle<FixedArray> empty = isolate->factory()->NewFixedArray(0);
    script->set_line_ends(*empty);
    ASSERT(script->line_ends()->IsFixedArray());
    return;
  }

  Handle<String> src(String::cast(script->source()), isolate);

  Handle<FixedArray> array = CalculateLineEnds(src, true);

  if (*array != isolate->heap()->empty_fixed_array()) {
    array->set_map(isolate->heap()->fixed_cow_array_map());
  }

  script->set_line_ends(*array);
  ASSERT(script->line_ends()->IsFixedArray());
}


template <typename SourceChar>
static void CalculateLineEnds(Isolate* isolate,
                              List<int>* line_ends,
                              Vector<const SourceChar> src,
                              bool with_last_line) {
  const int src_len = src.length();
  StringSearch<char, SourceChar> search(isolate, CStrVector("\n"));

  // Find and record line ends.
  int position = 0;
  while (position != -1 && position < src_len) {
    position = search.Search(src, position);
    if (position != -1) {
      line_ends->Add(position);
      position++;
    } else if (with_last_line) {
      // Even if the last line misses a line end, it is counted.
      line_ends->Add(src_len);
      return;
    }
  }
}


Handle<FixedArray> CalculateLineEnds(Handle<String> src,
                                     bool with_last_line) {
  src = FlattenGetString(src);
  // Rough estimate of line count based on a roughly estimated average
  // length of (unpacked) code.
  int line_count_estimate = src->length() >> 4;
  List<int> line_ends(line_count_estimate);
  Isolate* isolate = src->GetIsolate();
  {
    AssertNoAllocation no_heap_allocation;  // ensure vectors stay valid.
    // Dispatch on type of strings.
    if (src->IsAsciiRepresentation()) {
      CalculateLineEnds(isolate,
                        &line_ends,
                        src->ToAsciiVector(),
                        with_last_line);
    } else {
      CalculateLineEnds(isolate,
                        &line_ends,
                        src->ToUC16Vector(),
                        with_last_line);
    }
  }
  int line_count = line_ends.length();
  Handle<FixedArray> array = isolate->factory()->NewFixedArray(line_count);
  for (int i = 0; i < line_count; i++) {
    array->set(i, Smi::FromInt(line_ends[i]));
  }
  return array;
}


// Convert code position into line number.
int GetScriptLineNumber(Handle<Script> script, int code_pos) {
  InitScriptLineEnds(script);
  AssertNoAllocation no_allocation;
  FixedArray* line_ends_array = FixedArray::cast(script->line_ends());
  const int line_ends_len = line_ends_array->length();

  if (!line_ends_len) return -1;

  if ((Smi::cast(line_ends_array->get(0)))->value() >= code_pos) {
    return script->line_offset()->value();
  }

  int left = 0;
  int right = line_ends_len;
  while (int half = (right - left) / 2) {
    if ((Smi::cast(line_ends_array->get(left + half)))->value() > code_pos) {
      right -= half;
    } else {
      left += half;
    }
  }
  return right + script->line_offset()->value();
}


int GetScriptLineNumberSafe(Handle<Script> script, int code_pos) {
  AssertNoAllocation no_allocation;
  if (!script->line_ends()->IsUndefined()) {
    return GetScriptLineNumber(script, code_pos);
  }
  // Slow mode: we do not have line_ends. We have to iterate through source.
  if (!script->source()->IsString()) {
    return -1;
  }
  String* source = String::cast(script->source());
  int line = 0;
  int len = source->length();
  for (int pos = 0; pos < len; pos++) {
    if (pos == code_pos) {
      break;
    }
    if (source->Get(pos) == '\n') {
      line++;
    }
  }
  return line;
}


void CustomArguments::IterateInstance(ObjectVisitor* v) {
  v->VisitPointers(values_, values_ + ARRAY_SIZE(values_));
}


// Compute the property keys from the interceptor.
v8::Handle<v8::Array> GetKeysForNamedInterceptor(Handle<JSObject> receiver,
                                                 Handle<JSObject> object) {
  Isolate* isolate = receiver->GetIsolate();
  Handle<InterceptorInfo> interceptor(object->GetNamedInterceptor());
  CustomArguments args(isolate, interceptor->data(), *receiver, *object);
  v8::AccessorInfo info(args.end());
  v8::Handle<v8::Array> result;
  if (!interceptor->enumerator()->IsUndefined()) {
    v8::NamedPropertyEnumerator enum_fun =
        v8::ToCData<v8::NamedPropertyEnumerator>(interceptor->enumerator());
    LOG(isolate, ApiObjectAccess("interceptor-named-enum", *object));
    {
      // Leaving JavaScript.
      VMState state(isolate, EXTERNAL);
      result = enum_fun(info);
    }
  }
  return result;
}


// Compute the element keys from the interceptor.
v8::Handle<v8::Array> GetKeysForIndexedInterceptor(Handle<JSObject> receiver,
                                                   Handle<JSObject> object) {
  Isolate* isolate = receiver->GetIsolate();
  Handle<InterceptorInfo> interceptor(object->GetIndexedInterceptor());
  CustomArguments args(isolate, interceptor->data(), *receiver, *object);
  v8::AccessorInfo info(args.end());
  v8::Handle<v8::Array> result;
  if (!interceptor->enumerator()->IsUndefined()) {
    v8::IndexedPropertyEnumerator enum_fun =
        v8::ToCData<v8::IndexedPropertyEnumerator>(interceptor->enumerator());
    LOG(isolate, ApiObjectAccess("interceptor-indexed-enum", *object));
    {
      // Leaving JavaScript.
      VMState state(isolate, EXTERNAL);
      result = enum_fun(info);
    }
  }
  return result;
}


static bool ContainsOnlyValidKeys(Handle<FixedArray> array) {
  int len = array->length();
  for (int i = 0; i < len; i++) {
    Object* e = array->get(i);
    if (!(e->IsString() || e->IsNumber())) return false;
  }
  return true;
}


Handle<FixedArray> GetKeysInFixedArrayFor(Handle<JSObject> object,
                                          KeyCollectionType type) {
  USE(ContainsOnlyValidKeys);
  Isolate* isolate = object->GetIsolate();
  Handle<FixedArray> content = isolate->factory()->empty_fixed_array();
  Handle<JSObject> arguments_boilerplate = Handle<JSObject>(
      isolate->context()->global_context()->arguments_boilerplate(),
      isolate);
  Handle<JSFunction> arguments_function = Handle<JSFunction>(
      JSFunction::cast(arguments_boilerplate->map()->constructor()),
      isolate);

  // Only collect keys if access is permitted.
  for (Handle<Object> p = object;
       *p != isolate->heap()->null_value();
       p = Handle<Object>(p->GetPrototype(), isolate)) {
    Handle<JSObject> current(JSObject::cast(*p), isolate);

    // Check access rights if required.
    if (current->IsAccessCheckNeeded() &&
        !isolate->MayNamedAccess(*current,
                                 isolate->heap()->undefined_value(),
                                 v8::ACCESS_KEYS)) {
      isolate->ReportFailedAccessCheck(*current, v8::ACCESS_KEYS);
      break;
    }

    // Compute the element keys.
    Handle<FixedArray> element_keys =
        isolate->factory()->NewFixedArray(current->NumberOfEnumElements());
    current->GetEnumElementKeys(*element_keys);
    content = UnionOfKeys(content, element_keys);
    ASSERT(ContainsOnlyValidKeys(content));

    // Add the element keys from the interceptor.
    if (current->HasIndexedInterceptor()) {
      v8::Handle<v8::Array> result =
          GetKeysForIndexedInterceptor(object, current);
      if (!result.IsEmpty())
        content = AddKeysFromJSArray(content, v8::Utils::OpenHandle(*result));
      ASSERT(ContainsOnlyValidKeys(content));
    }

    // We can cache the computed property keys if access checks are
    // not needed and no interceptors are involved.
    //
    // We do not use the cache if the object has elements and
    // therefore it does not make sense to cache the property names
    // for arguments objects.  Arguments objects will always have
    // elements.
    // Wrapped strings have elements, but don't have an elements
    // array or dictionary.  So the fast inline test for whether to
    // use the cache says yes, so we should not create a cache.
    bool cache_enum_keys =
        ((current->map()->constructor() != *arguments_function) &&
         !current->IsJSValue() &&
         !current->IsAccessCheckNeeded() &&
         !current->HasNamedInterceptor() &&
         !current->HasIndexedInterceptor());
    // Compute the property keys and cache them if possible.
    content =
        UnionOfKeys(content, GetEnumPropertyKeys(current, cache_enum_keys));
    ASSERT(ContainsOnlyValidKeys(content));

    // Add the property keys from the interceptor.
    if (current->HasNamedInterceptor()) {
      v8::Handle<v8::Array> result =
          GetKeysForNamedInterceptor(object, current);
      if (!result.IsEmpty())
        content = AddKeysFromJSArray(content, v8::Utils::OpenHandle(*result));
      ASSERT(ContainsOnlyValidKeys(content));
    }

    // If we only want local properties we bail out after the first
    // iteration.
    if (type == LOCAL_ONLY)
      break;
  }
  return content;
}


Handle<JSArray> GetKeysFor(Handle<JSObject> object) {
  Isolate* isolate = object->GetIsolate();
  isolate->counters()->for_in()->Increment();
  Handle<FixedArray> elements = GetKeysInFixedArrayFor(object,
                                                       INCLUDE_PROTOS);
  return isolate->factory()->NewJSArrayWithElements(elements);
}


Handle<FixedArray> GetEnumPropertyKeys(Handle<JSObject> object,
                                       bool cache_result) {
  int index = 0;
  Isolate* isolate = object->GetIsolate();
  if (object->HasFastProperties()) {
    if (object->map()->instance_descriptors()->HasEnumCache()) {
      isolate->counters()->enum_cache_hits()->Increment();
      DescriptorArray* desc = object->map()->instance_descriptors();
      return Handle<FixedArray>(FixedArray::cast(desc->GetEnumCache()),
                                isolate);
    }
    isolate->counters()->enum_cache_misses()->Increment();
    int num_enum = object->NumberOfEnumProperties();
    Handle<FixedArray> storage = isolate->factory()->NewFixedArray(num_enum);
    Handle<FixedArray> sort_array = isolate->factory()->NewFixedArray(num_enum);
    Handle<DescriptorArray> descs =
        Handle<DescriptorArray>(object->map()->instance_descriptors(), isolate);
    for (int i = 0; i < descs->number_of_descriptors(); i++) {
      if (descs->IsProperty(i) && !descs->IsDontEnum(i)) {
        (*storage)->set(index, descs->GetKey(i));
        PropertyDetails details(descs->GetDetails(i));
        (*sort_array)->set(index, Smi::FromInt(details.index()));
        index++;
      }
    }
    (*storage)->SortPairs(*sort_array, sort_array->length());
    if (cache_result) {
      Handle<FixedArray> bridge_storage =
          isolate->factory()->NewFixedArray(
              DescriptorArray::kEnumCacheBridgeLength);
      DescriptorArray* desc = object->map()->instance_descriptors();
      desc->SetEnumCache(*bridge_storage, *storage);
    }
    ASSERT(storage->length() == index);
    return storage;
  } else {
    int num_enum = object->NumberOfEnumProperties();
    Handle<FixedArray> storage = isolate->factory()->NewFixedArray(num_enum);
    Handle<FixedArray> sort_array = isolate->factory()->NewFixedArray(num_enum);
    object->property_dictionary()->CopyEnumKeysTo(*storage, *sort_array);
    return storage;
  }
}


bool EnsureCompiled(Handle<SharedFunctionInfo> shared,
                    ClearExceptionFlag flag) {
  return shared->is_compiled() || CompileLazyShared(shared, flag);
}


static bool CompileLazyHelper(CompilationInfo* info,
                              ClearExceptionFlag flag) {
  // Compile the source information to a code object.
  ASSERT(info->IsOptimizing() || !info->shared_info()->is_compiled());
  ASSERT(!info->isolate()->has_pending_exception());
  bool result = Compiler::CompileLazy(info);
  ASSERT(result != Isolate::Current()->has_pending_exception());
  if (!result && flag == CLEAR_EXCEPTION) {
    info->isolate()->clear_pending_exception();
  }
  return result;
}


bool CompileLazyShared(Handle<SharedFunctionInfo> shared,
                       ClearExceptionFlag flag) {
  CompilationInfo info(shared);
  return CompileLazyHelper(&info, flag);
}


static bool CompileLazyFunction(Handle<JSFunction> function,
                                ClearExceptionFlag flag,
                                InLoopFlag in_loop_flag) {
  bool result = true;
  if (function->shared()->is_compiled()) {
    function->ReplaceCode(function->shared()->code());
    function->shared()->set_code_age(0);
  } else {
    CompilationInfo info(function);
    if (in_loop_flag == IN_LOOP) info.MarkAsInLoop();
    result = CompileLazyHelper(&info, flag);
    ASSERT(!result || function->is_compiled());
  }
  return result;
}


bool CompileLazy(Handle<JSFunction> function,
                 ClearExceptionFlag flag) {
  return CompileLazyFunction(function, flag, NOT_IN_LOOP);
}


bool CompileLazyInLoop(Handle<JSFunction> function,
                       ClearExceptionFlag flag) {
  return CompileLazyFunction(function, flag, IN_LOOP);
}


bool CompileOptimized(Handle<JSFunction> function,
                      int osr_ast_id,
                      ClearExceptionFlag flag) {
  CompilationInfo info(function);
  info.SetOptimizing(osr_ast_id);
  return CompileLazyHelper(&info, flag);
}

} }  // namespace v8::internal
