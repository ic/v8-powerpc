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
#include "ast.h"
#include "deoptimizer.h"
#include "execution.h"
#include "factory.h"
#include "list-inl.h"
#include "safepoint-table.h"
#include "scopeinfo.h"

namespace v8 {
namespace internal {


template <class C>
static C* FindInPrototypeChain(Object* obj, bool* found_it) {
  ASSERT(!*found_it);
  Heap* heap = HEAP;
  while (!Is<C>(obj)) {
    if (obj == heap->null_value()) return NULL;
    obj = obj->GetPrototype();
  }
  *found_it = true;
  return C::cast(obj);
}


// Entry point that never should be called.
MaybeObject* Accessors::IllegalSetter(JSObject*, Object*, void*) {
  UNREACHABLE();
  return NULL;
}


Object* Accessors::IllegalGetAccessor(Object* object, void*) {
  UNREACHABLE();
  return object;
}


MaybeObject* Accessors::ReadOnlySetAccessor(JSObject*, Object* value, void*) {
  // According to ECMA-262, section 8.6.2.2, page 28, setting
  // read-only properties must be silently ignored.
  return value;
}


//
// Accessors::ArrayLength
//


MaybeObject* Accessors::ArrayGetLength(Object* object, void*) {
  // Traverse the prototype chain until we reach an array.
  bool found_it = false;
  JSArray* holder = FindInPrototypeChain<JSArray>(object, &found_it);
  if (!found_it) return Smi::FromInt(0);
  return holder->length();
}


// The helper function will 'flatten' Number objects.
Object* Accessors::FlattenNumber(Object* value) {
  if (value->IsNumber() || !value->IsJSValue()) return value;
  JSValue* wrapper = JSValue::cast(value);
  ASSERT(Isolate::Current()->context()->global_context()->number_function()->
      has_initial_map());
  Map* number_map = Isolate::Current()->context()->global_context()->
      number_function()->initial_map();
  if (wrapper->map() == number_map) return wrapper->value();
  return value;
}


MaybeObject* Accessors::ArraySetLength(JSObject* object, Object* value, void*) {
  Isolate* isolate = object->GetIsolate();

  // This means one of the object's prototypes is a JSArray and the
  // object does not have a 'length' property.  Calling SetProperty
  // causes an infinite loop.
  if (!object->IsJSArray()) {
    return object->SetLocalPropertyIgnoreAttributes(
        isolate->heap()->length_symbol(), value, NONE);
  }

  value = FlattenNumber(value);

  // Need to call methods that may trigger GC.
  HandleScope scope(isolate);

  // Protect raw pointers.
  Handle<JSObject> object_handle(object, isolate);
  Handle<Object> value_handle(value, isolate);

  bool has_exception;
  Handle<Object> uint32_v = Execution::ToUint32(value_handle, &has_exception);
  if (has_exception) return Failure::Exception();
  Handle<Object> number_v = Execution::ToNumber(value_handle, &has_exception);
  if (has_exception) return Failure::Exception();

  if (uint32_v->Number() == number_v->Number()) {
    return Handle<JSArray>::cast(object_handle)->SetElementsLength(*uint32_v);
  }
  return isolate->Throw(
      *isolate->factory()->NewRangeError("invalid_array_length",
                                         HandleVector<Object>(NULL, 0)));
}


const AccessorDescriptor Accessors::ArrayLength = {
  ArrayGetLength,
  ArraySetLength,
  0
};


//
// Accessors::StringLength
//


MaybeObject* Accessors::StringGetLength(Object* object, void*) {
  Object* value = object;
  if (object->IsJSValue()) value = JSValue::cast(object)->value();
  if (value->IsString()) return Smi::FromInt(String::cast(value)->length());
  // If object is not a string we return 0 to be compatible with WebKit.
  // Note: Firefox returns the length of ToString(object).
  return Smi::FromInt(0);
}


const AccessorDescriptor Accessors::StringLength = {
  StringGetLength,
  IllegalSetter,
  0
};


//
// Accessors::ScriptSource
//


MaybeObject* Accessors::ScriptGetSource(Object* object, void*) {
  Object* script = JSValue::cast(object)->value();
  return Script::cast(script)->source();
}


const AccessorDescriptor Accessors::ScriptSource = {
  ScriptGetSource,
  IllegalSetter,
  0
};


//
// Accessors::ScriptName
//


MaybeObject* Accessors::ScriptGetName(Object* object, void*) {
  Object* script = JSValue::cast(object)->value();
  return Script::cast(script)->name();
}


const AccessorDescriptor Accessors::ScriptName = {
  ScriptGetName,
  IllegalSetter,
  0
};


//
// Accessors::ScriptId
//


MaybeObject* Accessors::ScriptGetId(Object* object, void*) {
  Object* script = JSValue::cast(object)->value();
  return Script::cast(script)->id();
}


const AccessorDescriptor Accessors::ScriptId = {
  ScriptGetId,
  IllegalSetter,
  0
};


//
// Accessors::ScriptLineOffset
//


MaybeObject* Accessors::ScriptGetLineOffset(Object* object, void*) {
  Object* script = JSValue::cast(object)->value();
  return Script::cast(script)->line_offset();
}


const AccessorDescriptor Accessors::ScriptLineOffset = {
  ScriptGetLineOffset,
  IllegalSetter,
  0
};


//
// Accessors::ScriptColumnOffset
//


MaybeObject* Accessors::ScriptGetColumnOffset(Object* object, void*) {
  Object* script = JSValue::cast(object)->value();
  return Script::cast(script)->column_offset();
}


const AccessorDescriptor Accessors::ScriptColumnOffset = {
  ScriptGetColumnOffset,
  IllegalSetter,
  0
};


//
// Accessors::ScriptData
//


MaybeObject* Accessors::ScriptGetData(Object* object, void*) {
  Object* script = JSValue::cast(object)->value();
  return Script::cast(script)->data();
}


const AccessorDescriptor Accessors::ScriptData = {
  ScriptGetData,
  IllegalSetter,
  0
};


//
// Accessors::ScriptType
//


MaybeObject* Accessors::ScriptGetType(Object* object, void*) {
  Object* script = JSValue::cast(object)->value();
  return Script::cast(script)->type();
}


const AccessorDescriptor Accessors::ScriptType = {
  ScriptGetType,
  IllegalSetter,
  0
};


//
// Accessors::ScriptCompilationType
//


MaybeObject* Accessors::ScriptGetCompilationType(Object* object, void*) {
  Object* script = JSValue::cast(object)->value();
  return Script::cast(script)->compilation_type();
}


const AccessorDescriptor Accessors::ScriptCompilationType = {
  ScriptGetCompilationType,
  IllegalSetter,
  0
};


//
// Accessors::ScriptGetLineEnds
//


MaybeObject* Accessors::ScriptGetLineEnds(Object* object, void*) {
  JSValue* wrapper = JSValue::cast(object);
  Isolate* isolate = wrapper->GetIsolate();
  HandleScope scope(isolate);
  Handle<Script> script(Script::cast(wrapper->value()), isolate);
  InitScriptLineEnds(script);
  ASSERT(script->line_ends()->IsFixedArray());
  Handle<FixedArray> line_ends(FixedArray::cast(script->line_ends()));
  // We do not want anyone to modify this array from JS.
  ASSERT(*line_ends == isolate->heap()->empty_fixed_array() ||
         line_ends->map() == isolate->heap()->fixed_cow_array_map());
  Handle<JSArray> js_array =
      isolate->factory()->NewJSArrayWithElements(line_ends);
  return *js_array;
}


const AccessorDescriptor Accessors::ScriptLineEnds = {
  ScriptGetLineEnds,
  IllegalSetter,
  0
};


//
// Accessors::ScriptGetContextData
//


MaybeObject* Accessors::ScriptGetContextData(Object* object, void*) {
  Object* script = JSValue::cast(object)->value();
  return Script::cast(script)->context_data();
}


const AccessorDescriptor Accessors::ScriptContextData = {
  ScriptGetContextData,
  IllegalSetter,
  0
};


//
// Accessors::ScriptGetEvalFromScript
//


MaybeObject* Accessors::ScriptGetEvalFromScript(Object* object, void*) {
  Object* script = JSValue::cast(object)->value();
  if (!Script::cast(script)->eval_from_shared()->IsUndefined()) {
    Handle<SharedFunctionInfo> eval_from_shared(
        SharedFunctionInfo::cast(Script::cast(script)->eval_from_shared()));

    if (eval_from_shared->script()->IsScript()) {
      Handle<Script> eval_from_script(Script::cast(eval_from_shared->script()));
      return *GetScriptWrapper(eval_from_script);
    }
  }
  return HEAP->undefined_value();
}


const AccessorDescriptor Accessors::ScriptEvalFromScript = {
  ScriptGetEvalFromScript,
  IllegalSetter,
  0
};


//
// Accessors::ScriptGetEvalFromScriptPosition
//


MaybeObject* Accessors::ScriptGetEvalFromScriptPosition(Object* object, void*) {
  HandleScope scope;
  Handle<Script> script(Script::cast(JSValue::cast(object)->value()));

  // If this is not a script compiled through eval there is no eval position.
  int compilation_type = Smi::cast(script->compilation_type())->value();
  if (compilation_type != Script::COMPILATION_TYPE_EVAL) {
    return HEAP->undefined_value();
  }

  // Get the function from where eval was called and find the source position
  // from the instruction offset.
  Handle<Code> code(SharedFunctionInfo::cast(
      script->eval_from_shared())->code());
  return Smi::FromInt(code->SourcePosition(code->instruction_start() +
                      script->eval_from_instructions_offset()->value()));
}


const AccessorDescriptor Accessors::ScriptEvalFromScriptPosition = {
  ScriptGetEvalFromScriptPosition,
  IllegalSetter,
  0
};


//
// Accessors::ScriptGetEvalFromFunctionName
//


MaybeObject* Accessors::ScriptGetEvalFromFunctionName(Object* object, void*) {
  Object* script = JSValue::cast(object)->value();
  Handle<SharedFunctionInfo> shared(SharedFunctionInfo::cast(
      Script::cast(script)->eval_from_shared()));


  // Find the name of the function calling eval.
  if (!shared->name()->IsUndefined()) {
    return shared->name();
  } else {
    return shared->inferred_name();
  }
}


const AccessorDescriptor Accessors::ScriptEvalFromFunctionName = {
  ScriptGetEvalFromFunctionName,
  IllegalSetter,
  0
};


//
// Accessors::FunctionPrototype
//


MaybeObject* Accessors::FunctionGetPrototype(Object* object, void*) {
  Heap* heap = Isolate::Current()->heap();
  bool found_it = false;
  JSFunction* function = FindInPrototypeChain<JSFunction>(object, &found_it);
  if (!found_it) return heap->undefined_value();
  while (!function->should_have_prototype()) {
    found_it = false;
    function = FindInPrototypeChain<JSFunction>(object->GetPrototype(),
                                                &found_it);
    // There has to be one because we hit the getter.
    ASSERT(found_it);
  }

  if (!function->has_prototype()) {
    Object* prototype;
    { MaybeObject* maybe_prototype = heap->AllocateFunctionPrototype(function);
      if (!maybe_prototype->ToObject(&prototype)) return maybe_prototype;
    }
    Object* result;
    { MaybeObject* maybe_result = function->SetPrototype(prototype);
      if (!maybe_result->ToObject(&result)) return maybe_result;
    }
  }
  return function->prototype();
}


MaybeObject* Accessors::FunctionSetPrototype(JSObject* object,
                                             Object* value,
                                             void*) {
  Heap* heap = object->GetHeap();
  bool found_it = false;
  JSFunction* function = FindInPrototypeChain<JSFunction>(object, &found_it);
  if (!found_it) return heap->undefined_value();
  if (!function->should_have_prototype()) {
    // Since we hit this accessor, object will have no prototype property.
    return object->SetLocalPropertyIgnoreAttributes(heap->prototype_symbol(),
                                                    value,
                                                    NONE);
  }

  if (function->has_initial_map()) {
    // If the function has allocated the initial map
    // replace it with a copy containing the new prototype.
    Object* new_map;
    { MaybeObject* maybe_new_map =
          function->initial_map()->CopyDropTransitions();
      if (!maybe_new_map->ToObject(&new_map)) return maybe_new_map;
    }
    function->set_initial_map(Map::cast(new_map));
  }
  Object* prototype;
  { MaybeObject* maybe_prototype = function->SetPrototype(value);
    if (!maybe_prototype->ToObject(&prototype)) return maybe_prototype;
  }
  ASSERT(function->prototype() == value);
  return function;
}


const AccessorDescriptor Accessors::FunctionPrototype = {
  FunctionGetPrototype,
  FunctionSetPrototype,
  0
};


//
// Accessors::FunctionLength
//


MaybeObject* Accessors::FunctionGetLength(Object* object, void*) {
  bool found_it = false;
  JSFunction* function = FindInPrototypeChain<JSFunction>(object, &found_it);
  if (!found_it) return Smi::FromInt(0);
  // Check if already compiled.
  if (!function->shared()->is_compiled()) {
    // If the function isn't compiled yet, the length is not computed
    // correctly yet. Compile it now and return the right length.
    HandleScope scope;
    Handle<JSFunction> handle(function);
    if (!CompileLazy(handle, KEEP_EXCEPTION)) return Failure::Exception();
    return Smi::FromInt(handle->shared()->length());
  } else {
    return Smi::FromInt(function->shared()->length());
  }
}


const AccessorDescriptor Accessors::FunctionLength = {
  FunctionGetLength,
  ReadOnlySetAccessor,
  0
};


//
// Accessors::FunctionName
//


MaybeObject* Accessors::FunctionGetName(Object* object, void*) {
  bool found_it = false;
  JSFunction* holder = FindInPrototypeChain<JSFunction>(object, &found_it);
  if (!found_it) return HEAP->undefined_value();
  return holder->shared()->name();
}


const AccessorDescriptor Accessors::FunctionName = {
  FunctionGetName,
  ReadOnlySetAccessor,
  0
};


//
// Accessors::FunctionArguments
//


static MaybeObject* ConstructArgumentsObjectForInlinedFunction(
    JavaScriptFrame* frame,
    Handle<JSFunction> inlined_function,
    int inlined_frame_index) {
  Factory* factory = Isolate::Current()->factory();
  int args_count = inlined_function->shared()->formal_parameter_count();
  ScopedVector<SlotRef> args_slots(args_count);
  SlotRef::ComputeSlotMappingForArguments(frame,
                                          inlined_frame_index,
                                          &args_slots);
  Handle<JSObject> arguments =
      factory->NewArgumentsObject(inlined_function, args_count);
  Handle<FixedArray> array = factory->NewFixedArray(args_count);
  for (int i = 0; i < args_count; ++i) {
    Handle<Object> value = args_slots[i].GetValue();
    array->set(i, *value);
  }
  arguments->set_elements(*array);

  // Return the freshly allocated arguments object.
  return *arguments;
}


MaybeObject* Accessors::FunctionGetArguments(Object* object, void*) {
  Isolate* isolate = Isolate::Current();
  HandleScope scope(isolate);
  bool found_it = false;
  JSFunction* holder = FindInPrototypeChain<JSFunction>(object, &found_it);
  if (!found_it) return isolate->heap()->undefined_value();
  Handle<JSFunction> function(holder, isolate);

  // Find the top invocation of the function by traversing frames.
  List<JSFunction*> functions(2);
  for (JavaScriptFrameIterator it(isolate); !it.done(); it.Advance()) {
    JavaScriptFrame* frame = it.frame();
    frame->GetFunctions(&functions);
    for (int i = functions.length() - 1; i >= 0; i--) {
      // Skip all frames that aren't invocations of the given function.
      if (functions[i] != *function) continue;

      if (i > 0) {
        // The function in question was inlined.  Inlined functions have the
        // correct number of arguments and no allocated arguments object, so
        // we can construct a fresh one by interpreting the function's
        // deoptimization input data.
        return ConstructArgumentsObjectForInlinedFunction(frame, function, i);
      }

      if (!frame->is_optimized()) {
        // If there is an arguments variable in the stack, we return that.
        Handle<SerializedScopeInfo> info(function->shared()->scope_info());
        int index = info->StackSlotIndex(isolate->heap()->arguments_symbol());
        if (index >= 0) {
          Handle<Object> arguments(frame->GetExpression(index), isolate);
          if (!arguments->IsArgumentsMarker()) return *arguments;
        }
      }

      // If there is no arguments variable in the stack or we have an
      // optimized frame, we find the frame that holds the actual arguments
      // passed to the function.
      it.AdvanceToArgumentsFrame();
      frame = it.frame();

      // Get the number of arguments and construct an arguments object
      // mirror for the right frame.
      const int length = frame->ComputeParametersCount();
      Handle<JSObject> arguments = isolate->factory()->NewArgumentsObject(
          function, length);
      Handle<FixedArray> array = isolate->factory()->NewFixedArray(length);

      // Copy the parameters to the arguments object.
      ASSERT(array->length() == length);
      for (int i = 0; i < length; i++) array->set(i, frame->GetParameter(i));
      arguments->set_elements(*array);

      // Return the freshly allocated arguments object.
      return *arguments;
    }
    functions.Rewind(0);
  }

  // No frame corresponding to the given function found. Return null.
  return isolate->heap()->null_value();
}


const AccessorDescriptor Accessors::FunctionArguments = {
  FunctionGetArguments,
  ReadOnlySetAccessor,
  0
};


//
// Accessors::FunctionCaller
//


static MaybeObject* CheckNonStrictCallerOrThrow(
    Isolate* isolate,
    JSFunction* caller) {
  DisableAssertNoAllocation enable_allocation;
  if (caller->shared()->strict_mode()) {
    return isolate->Throw(
        *isolate->factory()->NewTypeError("strict_caller",
                                          HandleVector<Object>(NULL, 0)));
  }
  return caller;
}


MaybeObject* Accessors::FunctionGetCaller(Object* object, void*) {
  Isolate* isolate = Isolate::Current();
  HandleScope scope(isolate);
  AssertNoAllocation no_alloc;
  bool found_it = false;
  JSFunction* holder = FindInPrototypeChain<JSFunction>(object, &found_it);
  if (!found_it) return isolate->heap()->undefined_value();
  Handle<JSFunction> function(holder, isolate);

  List<JSFunction*> functions(2);
  for (JavaScriptFrameIterator it(isolate); !it.done(); it.Advance()) {
    JavaScriptFrame* frame = it.frame();
    frame->GetFunctions(&functions);
    for (int i = functions.length() - 1; i >= 0; i--) {
      if (functions[i] == *function) {
        // Once we have found the frame, we need to go to the caller
        // frame. This may require skipping through a number of top-level
        // frames, e.g. frames for scripts not functions.
        if (i > 0) {
          ASSERT(!functions[i - 1]->shared()->is_toplevel());
          return CheckNonStrictCallerOrThrow(isolate, functions[i - 1]);
        } else {
          for (it.Advance(); !it.done(); it.Advance()) {
            frame = it.frame();
            functions.Rewind(0);
            frame->GetFunctions(&functions);
            if (!functions.last()->shared()->is_toplevel()) {
              return CheckNonStrictCallerOrThrow(isolate, functions.last());
            }
            ASSERT(functions.length() == 1);
          }
          if (it.done()) return isolate->heap()->null_value();
          break;
        }
      }
    }
    functions.Rewind(0);
  }

  // No frame corresponding to the given function found. Return null.
  return isolate->heap()->null_value();
}


const AccessorDescriptor Accessors::FunctionCaller = {
  FunctionGetCaller,
  ReadOnlySetAccessor,
  0
};


//
// Accessors::ObjectPrototype
//


MaybeObject* Accessors::ObjectGetPrototype(Object* receiver, void*) {
  Object* current = receiver->GetPrototype();
  while (current->IsJSObject() &&
         JSObject::cast(current)->map()->is_hidden_prototype()) {
    current = current->GetPrototype();
  }
  return current;
}


MaybeObject* Accessors::ObjectSetPrototype(JSObject* receiver,
                                           Object* value,
                                           void*) {
  const bool skip_hidden_prototypes = true;
  // To be consistent with other Set functions, return the value.
  return receiver->SetPrototype(value, skip_hidden_prototypes);
}


const AccessorDescriptor Accessors::ObjectPrototype = {
  ObjectGetPrototype,
  ObjectSetPrototype,
  0
};

} }  // namespace v8::internal
