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

#include "api.h"
#include "arguments.h"
#include "bootstrapper.h"
#include "builtins.h"
#include "ic-inl.h"

namespace v8 {
namespace internal {

namespace {

// Arguments object passed to C++ builtins.
template <BuiltinExtraArguments extra_args>
class BuiltinArguments : public Arguments {
 public:
  BuiltinArguments(int length, Object** arguments)
      : Arguments(length, arguments) { }

  Object*& operator[] (int index) {
    ASSERT(index < length());
    return Arguments::operator[](index);
  }

  template <class S> Handle<S> at(int index) {
    ASSERT(index < length());
    return Arguments::at<S>(index);
  }

  Handle<Object> receiver() {
    return Arguments::at<Object>(0);
  }

  Handle<JSFunction> called_function() {
    STATIC_ASSERT(extra_args == NEEDS_CALLED_FUNCTION);
    return Arguments::at<JSFunction>(Arguments::length() - 1);
  }

  // Gets the total number of arguments including the receiver (but
  // excluding extra arguments).
  int length() const {
    STATIC_ASSERT(extra_args == NO_EXTRA_ARGUMENTS);
    return Arguments::length();
  }

#ifdef DEBUG
  void Verify() {
    // Check we have at least the receiver.
    ASSERT(Arguments::length() >= 1);
  }
#endif
};


// Specialize BuiltinArguments for the called function extra argument.

template <>
int BuiltinArguments<NEEDS_CALLED_FUNCTION>::length() const {
  return Arguments::length() - 1;
}

#ifdef DEBUG
template <>
void BuiltinArguments<NEEDS_CALLED_FUNCTION>::Verify() {
  // Check we have at least the receiver and the called function.
  ASSERT(Arguments::length() >= 2);
  // Make sure cast to JSFunction succeeds.
  called_function();
}
#endif


#define DEF_ARG_TYPE(name, spec)                      \
  typedef BuiltinArguments<spec> name##ArgumentsType;
BUILTIN_LIST_C(DEF_ARG_TYPE)
#undef DEF_ARG_TYPE

}  // namespace


// ----------------------------------------------------------------------------
// Support macro for defining builtins in C++.
// ----------------------------------------------------------------------------
//
// A builtin function is defined by writing:
//
//   BUILTIN(name) {
//     ...
//   }
//
// In the body of the builtin function the arguments can be accessed
// through the BuiltinArguments object args.

#ifdef DEBUG

#define BUILTIN(name)                                           \
  static Object* Builtin_Impl_##name(name##ArgumentsType args); \
  static Object* Builtin_##name(name##ArgumentsType args) {     \
    args.Verify();                                              \
    return Builtin_Impl_##name(args);                           \
  }                                                             \
  static Object* Builtin_Impl_##name(name##ArgumentsType args)

#else  // For release mode.

#define BUILTIN(name)                                           \
  static Object* Builtin_##name(name##ArgumentsType args)

#endif


static inline bool CalledAsConstructor() {
#ifdef DEBUG
  // Calculate the result using a full stack frame iterator and check
  // that the state of the stack is as we assume it to be in the
  // code below.
  StackFrameIterator it;
  ASSERT(it.frame()->is_exit());
  it.Advance();
  StackFrame* frame = it.frame();
  bool reference_result = frame->is_construct();
#endif
  Address fp = Top::c_entry_fp(Top::GetCurrentThread());
  // Because we know fp points to an exit frame we can use the relevant
  // part of ExitFrame::ComputeCallerState directly.
  const int kCallerOffset = ExitFrameConstants::kCallerFPOffset;
  Address caller_fp = Memory::Address_at(fp + kCallerOffset);
  // This inlines the part of StackFrame::ComputeType that grabs the
  // type of the current frame.  Note that StackFrame::ComputeType
  // has been specialized for each architecture so if any one of them
  // changes this code has to be changed as well.
  const int kMarkerOffset = StandardFrameConstants::kMarkerOffset;
  const Smi* kConstructMarker = Smi::FromInt(StackFrame::CONSTRUCT);
  Object* marker = Memory::Object_at(caller_fp + kMarkerOffset);
  bool result = (marker == kConstructMarker);
  ASSERT_EQ(result, reference_result);
  return result;
}

// ----------------------------------------------------------------------------


BUILTIN(Illegal) {
  UNREACHABLE();
  return Heap::undefined_value();  // Make compiler happy.
}


BUILTIN(EmptyFunction) {
  return Heap::undefined_value();
}


BUILTIN(ArrayCodeGeneric) {
  Counters::array_function_runtime.Increment();

  JSArray* array;
  if (CalledAsConstructor()) {
    array = JSArray::cast(*args.receiver());
  } else {
    // Allocate the JS Array
    JSFunction* constructor =
        Top::context()->global_context()->array_function();
    Object* obj = Heap::AllocateJSObject(constructor);
    if (obj->IsFailure()) return obj;
    array = JSArray::cast(obj);
  }

  // 'array' now contains the JSArray we should initialize.

  // Optimize the case where there is one argument and the argument is a
  // small smi.
  if (args.length() == 2) {
    Object* obj = args[1];
    if (obj->IsSmi()) {
      int len = Smi::cast(obj)->value();
      if (len >= 0 && len < JSObject::kInitialMaxFastElementArray) {
        Object* obj = Heap::AllocateFixedArrayWithHoles(len);
        if (obj->IsFailure()) return obj;
        array->SetContent(FixedArray::cast(obj));
        return array;
      }
    }
    // Take the argument as the length.
    obj = array->Initialize(0);
    if (obj->IsFailure()) return obj;
    return array->SetElementsLength(args[1]);
  }

  // Optimize the case where there are no parameters passed.
  if (args.length() == 1) {
    return array->Initialize(JSArray::kPreallocatedArrayElements);
  }

  // Take the arguments as elements.
  int number_of_elements = args.length() - 1;
  Smi* len = Smi::FromInt(number_of_elements);
  Object* obj = Heap::AllocateFixedArrayWithHoles(len->value());
  if (obj->IsFailure()) return obj;

  AssertNoAllocation no_gc;
  FixedArray* elms = FixedArray::cast(obj);
  WriteBarrierMode mode = elms->GetWriteBarrierMode(no_gc);
  // Fill in the content
  for (int index = 0; index < number_of_elements; index++) {
    elms->set(index, args[index+1], mode);
  }

  // Set length and elements on the array.
  array->set_elements(FixedArray::cast(obj));
  array->set_length(len);

  return array;
}


BUILTIN(ArrayPush) {
  JSArray* array = JSArray::cast(*args.receiver());
  ASSERT(array->HasFastElements());

  int len = Smi::cast(array->length())->value();
  int to_add = args.length() - 1;
  if (to_add == 0) {
    return Smi::FromInt(len);
  }
  // Currently fixed arrays cannot grow too big, so
  // we should never hit this case.
  ASSERT(to_add <= (Smi::kMaxValue - len));

  int new_length = len + to_add;
  FixedArray* elms = FixedArray::cast(array->elements());

  if (new_length > elms->length()) {
    // New backing storage is needed.
    int capacity = new_length + (new_length >> 1) + 16;
    Object* obj = Heap::AllocateFixedArrayWithHoles(capacity);
    if (obj->IsFailure()) return obj;

    AssertNoAllocation no_gc;
    FixedArray* new_elms = FixedArray::cast(obj);
    WriteBarrierMode mode = new_elms->GetWriteBarrierMode(no_gc);
    // Fill out the new array with old elements.
    for (int i = 0; i < len; i++) new_elms->set(i, elms->get(i), mode);
    elms = new_elms;
    array->set_elements(elms);
  }

  AssertNoAllocation no_gc;
  WriteBarrierMode mode = elms->GetWriteBarrierMode(no_gc);

  // Add the provided values.
  for (int index = 0; index < to_add; index++) {
    elms->set(index + len, args[index + 1], mode);
  }

  // Set the length.
  array->set_length(Smi::FromInt(new_length));
  return Smi::FromInt(new_length);
}


BUILTIN(ArrayPop) {
  JSArray* array = JSArray::cast(*args.receiver());
  ASSERT(array->HasFastElements());
  Object* undefined = Heap::undefined_value();

  int len = Smi::cast(array->length())->value();
  if (len == 0) return undefined;

  // Get top element
  FixedArray* elms = FixedArray::cast(array->elements());
  Object* top = elms->get(len - 1);

  // Set the length.
  array->set_length(Smi::FromInt(len - 1));

  if (!top->IsTheHole()) {
    // Delete the top element.
    elms->set_the_hole(len - 1);
    return top;
  }

  // Remember to check the prototype chain.
  JSFunction* array_function =
      Top::context()->global_context()->array_function();
  JSObject* prototype = JSObject::cast(array_function->prototype());
  top = prototype->GetElement(len - 1);

  return top;
}


static Object* GetElementToMove(uint32_t index,
                                FixedArray* elms,
                                JSObject* prototype) {
  Object* e = elms->get(index);
  if (e->IsTheHole() && prototype->HasElement(index)) {
    e = prototype->GetElement(index);
  }
  return e;
}


BUILTIN(ArrayShift) {
  JSArray* array = JSArray::cast(*args.receiver());
  ASSERT(array->HasFastElements());

  int len = Smi::cast(array->length())->value();
  if (len == 0) return Heap::undefined_value();

  // Fetch the prototype.
  JSFunction* array_function =
      Top::context()->global_context()->array_function();
  JSObject* prototype = JSObject::cast(array_function->prototype());

  FixedArray* elms = FixedArray::cast(array->elements());

  // Get first element
  Object* first = elms->get(0);
  if (first->IsTheHole()) {
    first = prototype->GetElement(0);
  }

  // Shift the elements.
  for (int i = 0; i < len - 1; i++) {
    elms->set(i, GetElementToMove(i + 1, elms, prototype));
  }
  elms->set(len - 1, Heap::the_hole_value());

  // Set the length.
  array->set_length(Smi::FromInt(len - 1));

  return first;
}


BUILTIN(ArrayUnshift) {
  JSArray* array = JSArray::cast(*args.receiver());
  ASSERT(array->HasFastElements());

  int len = Smi::cast(array->length())->value();
  int to_add = args.length() - 1;
  // Note that we cannot quit early if to_add == 0 as
  // values should be lifted from prototype into
  // the array.

  int new_length = len + to_add;
  // Currently fixed arrays cannot grow too big, so
  // we should never hit this case.
  ASSERT(to_add <= (Smi::kMaxValue - len));

  FixedArray* elms = FixedArray::cast(array->elements());

  // Fetch the prototype.
  JSFunction* array_function =
      Top::context()->global_context()->array_function();
  JSObject* prototype = JSObject::cast(array_function->prototype());

  if (new_length > elms->length()) {
    // New backing storage is needed.
    int capacity = new_length + (new_length >> 1) + 16;
    Object* obj = Heap::AllocateFixedArrayWithHoles(capacity);
    if (obj->IsFailure()) return obj;

    AssertNoAllocation no_gc;
    FixedArray* new_elms = FixedArray::cast(obj);
    WriteBarrierMode mode = new_elms->GetWriteBarrierMode(no_gc);
    // Fill out the new array with old elements.
    for (int i = 0; i < len; i++)
      new_elms->set(to_add + i,
                    GetElementToMove(i, elms, prototype),
                    mode);

    elms = new_elms;
    array->set_elements(elms);
  } else {
    AssertNoAllocation no_gc;
    WriteBarrierMode mode = elms->GetWriteBarrierMode(no_gc);

    // Move elements to the right
    for (int i = 0; i < len; i++) {
      elms->set(new_length - i - 1,
                GetElementToMove(len - i - 1, elms, prototype),
                mode);
    }
  }

  // Add the provided values.
  AssertNoAllocation no_gc;
  WriteBarrierMode mode = elms->GetWriteBarrierMode(no_gc);
  for (int i = 0; i < to_add; i++) {
    elms->set(i, args[i + 1], mode);
  }

  // Set the length.
  array->set_length(Smi::FromInt(new_length));
  return Smi::FromInt(new_length);
}


static Object* CallJsBuiltin(const char* name,
                             BuiltinArguments<NO_EXTRA_ARGUMENTS> args) {
  HandleScope handleScope;

  Handle<Object> js_builtin =
      GetProperty(Handle<JSObject>(Top::global_context()->builtins()),
                  name);
  ASSERT(js_builtin->IsJSFunction());
  Handle<JSFunction> function(Handle<JSFunction>::cast(js_builtin));
  Vector<Object**> argv(Vector<Object**>::New(args.length() - 1));
  int n_args = args.length() - 1;
  for (int i = 0; i < n_args; i++) {
    argv[i] = &args[i + 1];
  }
  bool pending_exception = false;
  Handle<Object> result = Execution::Call(function,
                                          args.receiver(),
                                          n_args,
                                          argv.start(),
                                          &pending_exception);
  if (pending_exception) return Failure::Exception();
  return *result;
}


BUILTIN(ArraySlice) {
  JSArray* array = JSArray::cast(*args.receiver());
  ASSERT(array->HasFastElements());

  int len = Smi::cast(array->length())->value();

  int n_arguments = args.length() - 1;

  // Note carefully choosen defaults---if argument is missing,
  // it's undefined which gets converted to 0 for relativeStart
  // and to len for relativeEnd.
  int relativeStart = 0;
  int relativeEnd = len;
  if (n_arguments > 0) {
    Object* arg1 = args[1];
    if (arg1->IsSmi()) {
      relativeStart = Smi::cast(arg1)->value();
    } else if (!arg1->IsUndefined()) {
      return CallJsBuiltin("ArraySlice", args);
    }
    if (n_arguments > 1) {
      Object* arg2 = args[2];
      if (arg2->IsSmi()) {
        relativeEnd = Smi::cast(arg2)->value();
      } else if (!arg2->IsUndefined()) {
        return CallJsBuiltin("ArraySlice", args);
      }
    }
  }

  // ECMAScript 232, 3rd Edition, Section 15.4.4.10, step 6.
  int k = (relativeStart < 0) ? Max(len + relativeStart, 0)
                              : Min(relativeStart, len);

  // ECMAScript 232, 3rd Edition, Section 15.4.4.10, step 8.
  int final = (relativeEnd < 0) ? Max(len + relativeEnd, 0)
                                : Min(relativeEnd, len);

  // Calculate the length of result array.
  int result_len = final - k;
  if (result_len < 0) {
    result_len = 0;
  }

  JSFunction* array_function =
      Top::context()->global_context()->array_function();
  Object* result = Heap::AllocateJSObject(array_function);
  if (result->IsFailure()) return result;
  JSArray* result_array = JSArray::cast(result);

  result = Heap::AllocateFixedArrayWithHoles(result_len);
  if (result->IsFailure()) return result;
  FixedArray* result_elms = FixedArray::cast(result);

  FixedArray* elms = FixedArray::cast(array->elements());

  // Fetch the prototype.
  JSObject* prototype = JSObject::cast(array_function->prototype());

  AssertNoAllocation no_gc;
  WriteBarrierMode mode = result_elms->GetWriteBarrierMode(no_gc);

  // Fill newly created array.
  for (int i = 0; i < result_len; i++) {
    result_elms->set(i,
                     GetElementToMove(k + i, elms, prototype),
                     mode);
  }

  // Set elements.
  result_array->set_elements(result_elms);

  // Set the length.
  result_array->set_length(Smi::FromInt(result_len));
  return result_array;
}


BUILTIN(ArraySplice) {
  JSArray* array = JSArray::cast(*args.receiver());
  ASSERT(array->HasFastElements());

  int len = Smi::cast(array->length())->value();

  int n_arguments = args.length() - 1;

  // SpiderMonkey and JSC return undefined in the case where no
  // arguments are given instead of using the implicit undefined
  // arguments.  This does not follow ECMA-262, but we do the same for
  // compatibility.
  // TraceMonkey follows ECMA-262 though.
  if (n_arguments == 0) {
    return Heap::undefined_value();
  }

  int relativeStart = 0;
  Object* arg1 = args[1];
  if (arg1->IsSmi()) {
    relativeStart = Smi::cast(arg1)->value();
  } else if (!arg1->IsUndefined()) {
    return CallJsBuiltin("ArraySplice", args);
  }
  int actualStart = (relativeStart < 0) ? Max(len + relativeStart, 0)
                                        : Min(relativeStart, len);

  // SpiderMonkey, TraceMonkey and JSC treat the case where no delete count is
  // given differently from when an undefined delete count is given.
  // This does not follow ECMA-262, but we do the same for
  // compatibility.
  int deleteCount = len;
  if (n_arguments > 1) {
    Object* arg2 = args[2];
    if (arg2->IsSmi()) {
      deleteCount = Smi::cast(arg2)->value();
    } else {
      return CallJsBuiltin("ArraySplice", args);
    }
  }
  int actualDeleteCount = Min(Max(deleteCount, 0), len - actualStart);

  JSFunction* array_function =
      Top::context()->global_context()->array_function();

  // Allocate result array.
  Object* result = Heap::AllocateJSObject(array_function);
  if (result->IsFailure()) return result;
  JSArray* result_array = JSArray::cast(result);

  result = Heap::AllocateFixedArrayWithHoles(actualDeleteCount);
  if (result->IsFailure()) return result;
  FixedArray* result_elms = FixedArray::cast(result);

  FixedArray* elms = FixedArray::cast(array->elements());

  // Fetch the prototype.
  JSObject* prototype = JSObject::cast(array_function->prototype());

  AssertNoAllocation no_gc;
  WriteBarrierMode mode = result_elms->GetWriteBarrierMode(no_gc);

  // Fill newly created array.
  for (int k = 0; k < actualDeleteCount; k++) {
    result_elms->set(k,
                     GetElementToMove(actualStart + k, elms, prototype),
                     mode);
  }

  // Set elements.
  result_array->set_elements(result_elms);

  // Set the length.
  result_array->set_length(Smi::FromInt(actualDeleteCount));

  int itemCount = (n_arguments > 1) ? (n_arguments - 2) : 0;

  int new_length = len - actualDeleteCount + itemCount;

  mode = elms->GetWriteBarrierMode(no_gc);
  if (itemCount < actualDeleteCount) {
    // Shrink the array.
    for (int k = actualStart; k < (len - actualDeleteCount); k++) {
      elms->set(k + itemCount,
                GetElementToMove(k + actualDeleteCount, elms, prototype),
                mode);
    }

    for (int k = len; k > new_length; k--) {
      elms->set(k - 1, Heap::the_hole_value());
    }
  } else if (itemCount > actualDeleteCount) {
    // Currently fixed arrays cannot grow too big, so
    // we should never hit this case.
    ASSERT((itemCount - actualDeleteCount) <= (Smi::kMaxValue - len));

    FixedArray* source_elms = elms;

    // Check if array need to grow.
    if (new_length > elms->length()) {
      // New backing storage is needed.
      int capacity = new_length + (new_length >> 1) + 16;
      Object* obj = Heap::AllocateFixedArrayWithHoles(capacity);
      if (obj->IsFailure()) return obj;

      FixedArray* new_elms = FixedArray::cast(obj);
      mode = new_elms->GetWriteBarrierMode(no_gc);

      // Copy the part before actualStart as is.
      for (int k = 0; k < actualStart; k++) {
        new_elms->set(k, elms->get(k), mode);
      }

      source_elms = elms;
      elms = new_elms;
      array->set_elements(elms);
    }

    for (int k = len - actualDeleteCount; k > actualStart; k--) {
      elms->set(k + itemCount - 1,
                GetElementToMove(k + actualDeleteCount - 1,
                                 source_elms,
                                 prototype),
                mode);
    }
  }

  for (int k = actualStart; k < actualStart + itemCount; k++) {
    elms->set(k, args[3 + k - actualStart], mode);
  }

  // Set the length.
  array->set_length(Smi::FromInt(new_length));

  return result_array;
}


// -----------------------------------------------------------------------------
//


// Returns the holder JSObject if the function can legally be called
// with this receiver.  Returns Heap::null_value() if the call is
// illegal.  Any arguments that don't fit the expected type is
// overwritten with undefined.  Arguments that do fit the expected
// type is overwritten with the object in the prototype chain that
// actually has that type.
static inline Object* TypeCheck(int argc,
                                Object** argv,
                                FunctionTemplateInfo* info) {
  Object* recv = argv[0];
  Object* sig_obj = info->signature();
  if (sig_obj->IsUndefined()) return recv;
  SignatureInfo* sig = SignatureInfo::cast(sig_obj);
  // If necessary, check the receiver
  Object* recv_type = sig->receiver();

  Object* holder = recv;
  if (!recv_type->IsUndefined()) {
    for (; holder != Heap::null_value(); holder = holder->GetPrototype()) {
      if (holder->IsInstanceOf(FunctionTemplateInfo::cast(recv_type))) {
        break;
      }
    }
    if (holder == Heap::null_value()) return holder;
  }
  Object* args_obj = sig->args();
  // If there is no argument signature we're done
  if (args_obj->IsUndefined()) return holder;
  FixedArray* args = FixedArray::cast(args_obj);
  int length = args->length();
  if (argc <= length) length = argc - 1;
  for (int i = 0; i < length; i++) {
    Object* argtype = args->get(i);
    if (argtype->IsUndefined()) continue;
    Object** arg = &argv[-1 - i];
    Object* current = *arg;
    for (; current != Heap::null_value(); current = current->GetPrototype()) {
      if (current->IsInstanceOf(FunctionTemplateInfo::cast(argtype))) {
        *arg = current;
        break;
      }
    }
    if (current == Heap::null_value()) *arg = Heap::undefined_value();
  }
  return holder;
}


template <bool is_construct>
static Object* HandleApiCallHelper(
    BuiltinArguments<NEEDS_CALLED_FUNCTION> args) {
  ASSERT(is_construct == CalledAsConstructor());

  HandleScope scope;
  Handle<JSFunction> function = args.called_function();

  if (is_construct) {
    Handle<FunctionTemplateInfo> desc =
        Handle<FunctionTemplateInfo>(
            FunctionTemplateInfo::cast(function->shared()->function_data()));
    bool pending_exception = false;
    Factory::ConfigureInstance(desc, Handle<JSObject>::cast(args.receiver()),
                               &pending_exception);
    ASSERT(Top::has_pending_exception() == pending_exception);
    if (pending_exception) return Failure::Exception();
  }

  FunctionTemplateInfo* fun_data =
      FunctionTemplateInfo::cast(function->shared()->function_data());
  Object* raw_holder = TypeCheck(args.length(), &args[0], fun_data);

  if (raw_holder->IsNull()) {
    // This function cannot be called with the given receiver.  Abort!
    Handle<Object> obj =
        Factory::NewTypeError("illegal_invocation", HandleVector(&function, 1));
    return Top::Throw(*obj);
  }

  Object* raw_call_data = fun_data->call_code();
  if (!raw_call_data->IsUndefined()) {
    CallHandlerInfo* call_data = CallHandlerInfo::cast(raw_call_data);
    Object* callback_obj = call_data->callback();
    v8::InvocationCallback callback =
        v8::ToCData<v8::InvocationCallback>(callback_obj);
    Object* data_obj = call_data->data();
    Object* result;

    Handle<Object> data_handle(data_obj);
    v8::Local<v8::Value> data = v8::Utils::ToLocal(data_handle);
    ASSERT(raw_holder->IsJSObject());
    v8::Local<v8::Function> callee = v8::Utils::ToLocal(function);
    Handle<JSObject> holder_handle(JSObject::cast(raw_holder));
    v8::Local<v8::Object> holder = v8::Utils::ToLocal(holder_handle);
    LOG(ApiObjectAccess("call", JSObject::cast(*args.receiver())));
    v8::Arguments new_args = v8::ImplementationUtilities::NewArguments(
        data,
        holder,
        callee,
        is_construct,
        reinterpret_cast<void**>(&args[0] - 1),
        args.length() - 1);

    v8::Handle<v8::Value> value;
    {
      // Leaving JavaScript.
      VMState state(EXTERNAL);
#ifdef ENABLE_LOGGING_AND_PROFILING
      state.set_external_callback(v8::ToCData<Address>(callback_obj));
#endif
      value = callback(new_args);
    }
    if (value.IsEmpty()) {
      result = Heap::undefined_value();
    } else {
      result = *reinterpret_cast<Object**>(*value);
    }

    RETURN_IF_SCHEDULED_EXCEPTION();
    if (!is_construct || result->IsJSObject()) return result;
  }

  return *args.receiver();
}


BUILTIN(HandleApiCall) {
  return HandleApiCallHelper<false>(args);
}


BUILTIN(HandleApiCallConstruct) {
  return HandleApiCallHelper<true>(args);
}


#ifdef DEBUG

static void VerifyTypeCheck(Handle<JSObject> object,
                            Handle<JSFunction> function) {
  FunctionTemplateInfo* info =
      FunctionTemplateInfo::cast(function->shared()->function_data());
  if (info->signature()->IsUndefined()) return;
  SignatureInfo* signature = SignatureInfo::cast(info->signature());
  Object* receiver_type = signature->receiver();
  if (receiver_type->IsUndefined()) return;
  FunctionTemplateInfo* type = FunctionTemplateInfo::cast(receiver_type);
  ASSERT(object->IsInstanceOf(type));
}

#endif


BUILTIN(FastHandleApiCall) {
  ASSERT(!CalledAsConstructor());
  const bool is_construct = false;

  // We expect four more arguments: function, callback, call data, and holder.
  const int args_length = args.length() - 4;
  ASSERT(args_length >= 0);

  Handle<JSFunction> function = args.at<JSFunction>(args_length);
  Object* callback_obj = args[args_length + 1];
  Handle<Object> data_handle = args.at<Object>(args_length + 2);
  Handle<JSObject> checked_holder = args.at<JSObject>(args_length + 3);

#ifdef DEBUG
  VerifyTypeCheck(checked_holder, function);
#endif

  v8::Local<v8::Object> holder = v8::Utils::ToLocal(checked_holder);
  v8::Local<v8::Function> callee = v8::Utils::ToLocal(function);
  v8::InvocationCallback callback =
      v8::ToCData<v8::InvocationCallback>(callback_obj);
  v8::Local<v8::Value> data = v8::Utils::ToLocal(data_handle);

  v8::Arguments new_args = v8::ImplementationUtilities::NewArguments(
      data,
      holder,
      callee,
      is_construct,
      reinterpret_cast<void**>(&args[0] - 1),
      args_length - 1);

  HandleScope scope;
  Object* result;
  v8::Handle<v8::Value> value;
  {
    // Leaving JavaScript.
    VMState state(EXTERNAL);
#ifdef ENABLE_LOGGING_AND_PROFILING
    state.set_external_callback(v8::ToCData<Address>(callback_obj));
#endif
    value = callback(new_args);
  }
  if (value.IsEmpty()) {
    result = Heap::undefined_value();
  } else {
    result = *reinterpret_cast<Object**>(*value);
  }

  RETURN_IF_SCHEDULED_EXCEPTION();
  return result;
}


// Helper function to handle calls to non-function objects created through the
// API. The object can be called as either a constructor (using new) or just as
// a function (without new).
static Object* HandleApiCallAsFunctionOrConstructor(
    bool is_construct_call,
    BuiltinArguments<NO_EXTRA_ARGUMENTS> args) {
  // Non-functions are never called as constructors. Even if this is an object
  // called as a constructor the delegate call is not a construct call.
  ASSERT(!CalledAsConstructor());

  Handle<Object> receiver = args.at<Object>(0);

  // Get the object called.
  JSObject* obj = JSObject::cast(*args.receiver());

  // Get the invocation callback from the function descriptor that was
  // used to create the called object.
  ASSERT(obj->map()->has_instance_call_handler());
  JSFunction* constructor = JSFunction::cast(obj->map()->constructor());
  Object* template_info = constructor->shared()->function_data();
  Object* handler =
      FunctionTemplateInfo::cast(template_info)->instance_call_handler();
  ASSERT(!handler->IsUndefined());
  CallHandlerInfo* call_data = CallHandlerInfo::cast(handler);
  Object* callback_obj = call_data->callback();
  v8::InvocationCallback callback =
      v8::ToCData<v8::InvocationCallback>(callback_obj);

  // Get the data for the call and perform the callback.
  Object* data_obj = call_data->data();
  Object* result;
  { HandleScope scope;
    v8::Local<v8::Object> self =
        v8::Utils::ToLocal(Handle<JSObject>::cast(args.receiver()));
    Handle<Object> data_handle(data_obj);
    v8::Local<v8::Value> data = v8::Utils::ToLocal(data_handle);
    Handle<JSFunction> callee_handle(constructor);
    v8::Local<v8::Function> callee = v8::Utils::ToLocal(callee_handle);
    LOG(ApiObjectAccess("call non-function", JSObject::cast(*args.receiver())));
    v8::Arguments new_args = v8::ImplementationUtilities::NewArguments(
        data,
        self,
        callee,
        is_construct_call,
        reinterpret_cast<void**>(&args[0] - 1),
        args.length() - 1);
    v8::Handle<v8::Value> value;
    {
      // Leaving JavaScript.
      VMState state(EXTERNAL);
#ifdef ENABLE_LOGGING_AND_PROFILING
      state.set_external_callback(v8::ToCData<Address>(callback_obj));
#endif
      value = callback(new_args);
    }
    if (value.IsEmpty()) {
      result = Heap::undefined_value();
    } else {
      result = *reinterpret_cast<Object**>(*value);
    }
  }
  // Check for exceptions and return result.
  RETURN_IF_SCHEDULED_EXCEPTION();
  return result;
}


// Handle calls to non-function objects created through the API. This delegate
// function is used when the call is a normal function call.
BUILTIN(HandleApiCallAsFunction) {
  return HandleApiCallAsFunctionOrConstructor(false, args);
}


// Handle calls to non-function objects created through the API. This delegate
// function is used when the call is a construct call.
BUILTIN(HandleApiCallAsConstructor) {
  return HandleApiCallAsFunctionOrConstructor(true, args);
}


static void Generate_LoadIC_ArrayLength(MacroAssembler* masm) {
  LoadIC::GenerateArrayLength(masm);
}


static void Generate_LoadIC_StringLength(MacroAssembler* masm) {
  LoadIC::GenerateStringLength(masm);
}


static void Generate_LoadIC_FunctionPrototype(MacroAssembler* masm) {
  LoadIC::GenerateFunctionPrototype(masm);
}


static void Generate_LoadIC_Initialize(MacroAssembler* masm) {
  LoadIC::GenerateInitialize(masm);
}


static void Generate_LoadIC_PreMonomorphic(MacroAssembler* masm) {
  LoadIC::GeneratePreMonomorphic(masm);
}


static void Generate_LoadIC_Miss(MacroAssembler* masm) {
  LoadIC::GenerateMiss(masm);
}


static void Generate_LoadIC_Megamorphic(MacroAssembler* masm) {
  LoadIC::GenerateMegamorphic(masm);
}


static void Generate_LoadIC_Normal(MacroAssembler* masm) {
  LoadIC::GenerateNormal(masm);
}


static void Generate_KeyedLoadIC_Initialize(MacroAssembler* masm) {
  KeyedLoadIC::GenerateInitialize(masm);
}


static void Generate_KeyedLoadIC_Miss(MacroAssembler* masm) {
  KeyedLoadIC::GenerateMiss(masm);
}


static void Generate_KeyedLoadIC_Generic(MacroAssembler* masm) {
  KeyedLoadIC::GenerateGeneric(masm);
}


static void Generate_KeyedLoadIC_String(MacroAssembler* masm) {
  KeyedLoadIC::GenerateString(masm);
}


static void Generate_KeyedLoadIC_ExternalByteArray(MacroAssembler* masm) {
  KeyedLoadIC::GenerateExternalArray(masm, kExternalByteArray);
}


static void Generate_KeyedLoadIC_ExternalUnsignedByteArray(
    MacroAssembler* masm) {
  KeyedLoadIC::GenerateExternalArray(masm, kExternalUnsignedByteArray);
}


static void Generate_KeyedLoadIC_ExternalShortArray(MacroAssembler* masm) {
  KeyedLoadIC::GenerateExternalArray(masm, kExternalShortArray);
}


static void Generate_KeyedLoadIC_ExternalUnsignedShortArray(
    MacroAssembler* masm) {
  KeyedLoadIC::GenerateExternalArray(masm, kExternalUnsignedShortArray);
}


static void Generate_KeyedLoadIC_ExternalIntArray(MacroAssembler* masm) {
  KeyedLoadIC::GenerateExternalArray(masm, kExternalIntArray);
}


static void Generate_KeyedLoadIC_ExternalUnsignedIntArray(
    MacroAssembler* masm) {
  KeyedLoadIC::GenerateExternalArray(masm, kExternalUnsignedIntArray);
}


static void Generate_KeyedLoadIC_ExternalFloatArray(MacroAssembler* masm) {
  KeyedLoadIC::GenerateExternalArray(masm, kExternalFloatArray);
}


static void Generate_KeyedLoadIC_PreMonomorphic(MacroAssembler* masm) {
  KeyedLoadIC::GeneratePreMonomorphic(masm);
}

static void Generate_KeyedLoadIC_IndexedInterceptor(MacroAssembler* masm) {
  KeyedLoadIC::GenerateIndexedInterceptor(masm);
}


static void Generate_StoreIC_Initialize(MacroAssembler* masm) {
  StoreIC::GenerateInitialize(masm);
}


static void Generate_StoreIC_Miss(MacroAssembler* masm) {
  StoreIC::GenerateMiss(masm);
}


static void Generate_StoreIC_Megamorphic(MacroAssembler* masm) {
  StoreIC::GenerateMegamorphic(masm);
}


static void Generate_StoreIC_ArrayLength(MacroAssembler* masm) {
  StoreIC::GenerateArrayLength(masm);
}


static void Generate_KeyedStoreIC_Generic(MacroAssembler* masm) {
  KeyedStoreIC::GenerateGeneric(masm);
}


static void Generate_KeyedStoreIC_ExternalByteArray(MacroAssembler* masm) {
  KeyedStoreIC::GenerateExternalArray(masm, kExternalByteArray);
}


static void Generate_KeyedStoreIC_ExternalUnsignedByteArray(
    MacroAssembler* masm) {
  KeyedStoreIC::GenerateExternalArray(masm, kExternalUnsignedByteArray);
}


static void Generate_KeyedStoreIC_ExternalShortArray(MacroAssembler* masm) {
  KeyedStoreIC::GenerateExternalArray(masm, kExternalShortArray);
}


static void Generate_KeyedStoreIC_ExternalUnsignedShortArray(
    MacroAssembler* masm) {
  KeyedStoreIC::GenerateExternalArray(masm, kExternalUnsignedShortArray);
}


static void Generate_KeyedStoreIC_ExternalIntArray(MacroAssembler* masm) {
  KeyedStoreIC::GenerateExternalArray(masm, kExternalIntArray);
}


static void Generate_KeyedStoreIC_ExternalUnsignedIntArray(
    MacroAssembler* masm) {
  KeyedStoreIC::GenerateExternalArray(masm, kExternalUnsignedIntArray);
}


static void Generate_KeyedStoreIC_ExternalFloatArray(MacroAssembler* masm) {
  KeyedStoreIC::GenerateExternalArray(masm, kExternalFloatArray);
}


static void Generate_KeyedStoreIC_Miss(MacroAssembler* masm) {
  KeyedStoreIC::GenerateMiss(masm);
}


static void Generate_KeyedStoreIC_Initialize(MacroAssembler* masm) {
  KeyedStoreIC::GenerateInitialize(masm);
}


#ifdef ENABLE_DEBUGGER_SUPPORT
static void Generate_LoadIC_DebugBreak(MacroAssembler* masm) {
  Debug::GenerateLoadICDebugBreak(masm);
}


static void Generate_StoreIC_DebugBreak(MacroAssembler* masm) {
  Debug::GenerateStoreICDebugBreak(masm);
}


static void Generate_KeyedLoadIC_DebugBreak(MacroAssembler* masm) {
  Debug::GenerateKeyedLoadICDebugBreak(masm);
}


static void Generate_KeyedStoreIC_DebugBreak(MacroAssembler* masm) {
  Debug::GenerateKeyedStoreICDebugBreak(masm);
}


static void Generate_ConstructCall_DebugBreak(MacroAssembler* masm) {
  Debug::GenerateConstructCallDebugBreak(masm);
}


static void Generate_Return_DebugBreak(MacroAssembler* masm) {
  Debug::GenerateReturnDebugBreak(masm);
}


static void Generate_StubNoRegisters_DebugBreak(MacroAssembler* masm) {
  Debug::GenerateStubNoRegistersDebugBreak(masm);
}
#endif

Object* Builtins::builtins_[builtin_count] = { NULL, };
const char* Builtins::names_[builtin_count] = { NULL, };

#define DEF_ENUM_C(name, ignore) FUNCTION_ADDR(Builtin_##name),
  Address Builtins::c_functions_[cfunction_count] = {
    BUILTIN_LIST_C(DEF_ENUM_C)
  };
#undef DEF_ENUM_C

#define DEF_JS_NAME(name, ignore) #name,
#define DEF_JS_ARGC(ignore, argc) argc,
const char* Builtins::javascript_names_[id_count] = {
  BUILTINS_LIST_JS(DEF_JS_NAME)
};

int Builtins::javascript_argc_[id_count] = {
  BUILTINS_LIST_JS(DEF_JS_ARGC)
};
#undef DEF_JS_NAME
#undef DEF_JS_ARGC

static bool is_initialized = false;
void Builtins::Setup(bool create_heap_objects) {
  ASSERT(!is_initialized);

  // Create a scope for the handles in the builtins.
  HandleScope scope;

  struct BuiltinDesc {
    byte* generator;
    byte* c_code;
    const char* s_name;  // name is only used for generating log information.
    int name;
    Code::Flags flags;
    BuiltinExtraArguments extra_args;
  };

#define DEF_FUNCTION_PTR_C(name, extra_args) \
    { FUNCTION_ADDR(Generate_Adaptor),            \
      FUNCTION_ADDR(Builtin_##name),              \
      #name,                                      \
      c_##name,                                   \
      Code::ComputeFlags(Code::BUILTIN),          \
      extra_args                                  \
    },

#define DEF_FUNCTION_PTR_A(name, kind, state)              \
    { FUNCTION_ADDR(Generate_##name),                      \
      NULL,                                                \
      #name,                                               \
      name,                                                \
      Code::ComputeFlags(Code::kind, NOT_IN_LOOP, state),  \
      NO_EXTRA_ARGUMENTS                                   \
    },

  // Define array of pointers to generators and C builtin functions.
  static BuiltinDesc functions[] = {
      BUILTIN_LIST_C(DEF_FUNCTION_PTR_C)
      BUILTIN_LIST_A(DEF_FUNCTION_PTR_A)
      BUILTIN_LIST_DEBUG_A(DEF_FUNCTION_PTR_A)
      // Terminator:
      { NULL, NULL, NULL, builtin_count, static_cast<Code::Flags>(0),
        NO_EXTRA_ARGUMENTS }
  };

#undef DEF_FUNCTION_PTR_C
#undef DEF_FUNCTION_PTR_A

  // For now we generate builtin adaptor code into a stack-allocated
  // buffer, before copying it into individual code objects.
  byte buffer[4*KB];

  // Traverse the list of builtins and generate an adaptor in a
  // separate code object for each one.
  for (int i = 0; i < builtin_count; i++) {
    if (create_heap_objects) {
      MacroAssembler masm(buffer, sizeof buffer);
      // Generate the code/adaptor.
      typedef void (*Generator)(MacroAssembler*, int, BuiltinExtraArguments);
      Generator g = FUNCTION_CAST<Generator>(functions[i].generator);
      // We pass all arguments to the generator, but it may not use all of
      // them.  This works because the first arguments are on top of the
      // stack.
      g(&masm, functions[i].name, functions[i].extra_args);
      // Move the code into the object heap.
      CodeDesc desc;
      masm.GetCode(&desc);
      Code::Flags flags =  functions[i].flags;
      Object* code;
      {
        // During startup it's OK to always allocate and defer GC to later.
        // This simplifies things because we don't need to retry.
        AlwaysAllocateScope __scope__;
        code = Heap::CreateCode(desc, NULL, flags, masm.CodeObject());
        if (code->IsFailure()) {
          v8::internal::V8::FatalProcessOutOfMemory("CreateCode");
        }
      }
      // Log the event and add the code to the builtins array.
      LOG(CodeCreateEvent(Logger::BUILTIN_TAG,
                          Code::cast(code), functions[i].s_name));
      builtins_[i] = code;
#ifdef ENABLE_DISASSEMBLER
      if (FLAG_print_builtin_code) {
        PrintF("Builtin: %s\n", functions[i].s_name);
        Code::cast(code)->Disassemble(functions[i].s_name);
        PrintF("\n");
      }
#endif
    } else {
      // Deserializing. The values will be filled in during IterateBuiltins.
      builtins_[i] = NULL;
    }
    names_[i] = functions[i].s_name;
  }

  // Mark as initialized.
  is_initialized = true;
}


void Builtins::TearDown() {
  is_initialized = false;
}


void Builtins::IterateBuiltins(ObjectVisitor* v) {
  v->VisitPointers(&builtins_[0], &builtins_[0] + builtin_count);
}


const char* Builtins::Lookup(byte* pc) {
  if (is_initialized) {  // may be called during initialization (disassembler!)
    for (int i = 0; i < builtin_count; i++) {
      Code* entry = Code::cast(builtins_[i]);
      if (entry->contains(pc)) {
        return names_[i];
      }
    }
  }
  return NULL;
}


} }  // namespace v8::internal
