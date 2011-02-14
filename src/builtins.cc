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
#include "gdb-jit.h"
#include "ic-inl.h"
#include "vm-state-inl.h"

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

#define BUILTIN(name)                                                \
  MUST_USE_RESULT static MaybeObject* Builtin_Impl_##name(           \
      name##ArgumentsType args);                                     \
  MUST_USE_RESULT static MaybeObject* Builtin_##name(                \
      name##ArgumentsType args) {     \
    args.Verify();                                                   \
    return Builtin_Impl_##name(args);                                \
  }                                                                  \
  MUST_USE_RESULT static MaybeObject* Builtin_Impl_##name(           \
    name##ArgumentsType args)

#else  // For release mode.

#define BUILTIN(name)                                                \
  static MaybeObject* Builtin_##name(name##ArgumentsType args)

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
    Object* obj;
    { MaybeObject* maybe_obj = Heap::AllocateJSObject(constructor);
      if (!maybe_obj->ToObject(&obj)) return maybe_obj;
    }
    array = JSArray::cast(obj);
  }

  // 'array' now contains the JSArray we should initialize.
  ASSERT(array->HasFastElements());

  // Optimize the case where there is one argument and the argument is a
  // small smi.
  if (args.length() == 2) {
    Object* obj = args[1];
    if (obj->IsSmi()) {
      int len = Smi::cast(obj)->value();
      if (len >= 0 && len < JSObject::kInitialMaxFastElementArray) {
        Object* obj;
        { MaybeObject* maybe_obj = Heap::AllocateFixedArrayWithHoles(len);
          if (!maybe_obj->ToObject(&obj)) return maybe_obj;
        }
        array->SetContent(FixedArray::cast(obj));
        return array;
      }
    }
    // Take the argument as the length.
    { MaybeObject* maybe_obj = array->Initialize(0);
      if (!maybe_obj->ToObject(&obj)) return maybe_obj;
    }
    return array->SetElementsLength(args[1]);
  }

  // Optimize the case where there are no parameters passed.
  if (args.length() == 1) {
    return array->Initialize(JSArray::kPreallocatedArrayElements);
  }

  // Take the arguments as elements.
  int number_of_elements = args.length() - 1;
  Smi* len = Smi::FromInt(number_of_elements);
  Object* obj;
  { MaybeObject* maybe_obj = Heap::AllocateFixedArrayWithHoles(len->value());
    if (!maybe_obj->ToObject(&obj)) return maybe_obj;
  }

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


MUST_USE_RESULT static MaybeObject* AllocateJSArray() {
  JSFunction* array_function =
      Top::context()->global_context()->array_function();
  Object* result;
  { MaybeObject* maybe_result = Heap::AllocateJSObject(array_function);
    if (!maybe_result->ToObject(&result)) return maybe_result;
  }
  return result;
}


MUST_USE_RESULT static MaybeObject* AllocateEmptyJSArray() {
  Object* result;
  { MaybeObject* maybe_result = AllocateJSArray();
    if (!maybe_result->ToObject(&result)) return maybe_result;
  }
  JSArray* result_array = JSArray::cast(result);
  result_array->set_length(Smi::FromInt(0));
  result_array->set_elements(Heap::empty_fixed_array());
  return result_array;
}


static void CopyElements(AssertNoAllocation* no_gc,
                         FixedArray* dst,
                         int dst_index,
                         FixedArray* src,
                         int src_index,
                         int len) {
  ASSERT(dst != src);  // Use MoveElements instead.
  ASSERT(dst->map() != Heap::fixed_cow_array_map());
  ASSERT(len > 0);
  CopyWords(dst->data_start() + dst_index,
            src->data_start() + src_index,
            len);
  WriteBarrierMode mode = dst->GetWriteBarrierMode(*no_gc);
  if (mode == UPDATE_WRITE_BARRIER) {
    Heap::RecordWrites(dst->address(), dst->OffsetOfElementAt(dst_index), len);
  }
}


static void MoveElements(AssertNoAllocation* no_gc,
                         FixedArray* dst,
                         int dst_index,
                         FixedArray* src,
                         int src_index,
                         int len) {
  ASSERT(dst->map() != Heap::fixed_cow_array_map());
  memmove(dst->data_start() + dst_index,
          src->data_start() + src_index,
          len * kPointerSize);
  WriteBarrierMode mode = dst->GetWriteBarrierMode(*no_gc);
  if (mode == UPDATE_WRITE_BARRIER) {
    Heap::RecordWrites(dst->address(), dst->OffsetOfElementAt(dst_index), len);
  }
}


static void FillWithHoles(FixedArray* dst, int from, int to) {
  ASSERT(dst->map() != Heap::fixed_cow_array_map());
  MemsetPointer(dst->data_start() + from, Heap::the_hole_value(), to - from);
}


static FixedArray* LeftTrimFixedArray(FixedArray* elms, int to_trim) {
  ASSERT(elms->map() != Heap::fixed_cow_array_map());
  // For now this trick is only applied to fixed arrays in new and paged space.
  // In large object space the object's start must coincide with chunk
  // and thus the trick is just not applicable.
  ASSERT(!Heap::lo_space()->Contains(elms));

  STATIC_ASSERT(FixedArray::kMapOffset == 0);
  STATIC_ASSERT(FixedArray::kLengthOffset == kPointerSize);
  STATIC_ASSERT(FixedArray::kHeaderSize == 2 * kPointerSize);

  Object** former_start = HeapObject::RawField(elms, 0);

  const int len = elms->length();

  if (to_trim > FixedArray::kHeaderSize / kPointerSize &&
      !Heap::new_space()->Contains(elms)) {
    // If we are doing a big trim in old space then we zap the space that was
    // formerly part of the array so that the GC (aided by the card-based
    // remembered set) won't find pointers to new-space there.
    Object** zap = reinterpret_cast<Object**>(elms->address());
    zap++;  // Header of filler must be at least one word so skip that.
    for (int i = 1; i < to_trim; i++) {
      *zap++ = Smi::FromInt(0);
    }
  }
  // Technically in new space this write might be omitted (except for
  // debug mode which iterates through the heap), but to play safer
  // we still do it.
  Heap::CreateFillerObjectAt(elms->address(), to_trim * kPointerSize);

  former_start[to_trim] = Heap::fixed_array_map();
  former_start[to_trim + 1] = Smi::FromInt(len - to_trim);

  return FixedArray::cast(HeapObject::FromAddress(
      elms->address() + to_trim * kPointerSize));
}


static bool ArrayPrototypeHasNoElements(Context* global_context,
                                        JSObject* array_proto) {
  // This method depends on non writability of Object and Array prototype
  // fields.
  if (array_proto->elements() != Heap::empty_fixed_array()) return false;
  // Hidden prototype
  array_proto = JSObject::cast(array_proto->GetPrototype());
  ASSERT(array_proto->elements() == Heap::empty_fixed_array());
  // Object.prototype
  Object* proto = array_proto->GetPrototype();
  if (proto == Heap::null_value()) return false;
  array_proto = JSObject::cast(proto);
  if (array_proto != global_context->initial_object_prototype()) return false;
  if (array_proto->elements() != Heap::empty_fixed_array()) return false;
  ASSERT(array_proto->GetPrototype()->IsNull());
  return true;
}


MUST_USE_RESULT
static inline MaybeObject* EnsureJSArrayWithWritableFastElements(
    Object* receiver) {
  if (!receiver->IsJSArray()) return NULL;
  JSArray* array = JSArray::cast(receiver);
  HeapObject* elms = array->elements();
  if (elms->map() == Heap::fixed_array_map()) return elms;
  if (elms->map() == Heap::fixed_cow_array_map()) {
    return array->EnsureWritableFastElements();
  }
  return NULL;
}


static inline bool IsJSArrayFastElementMovingAllowed(JSArray* receiver) {
  Context* global_context = Top::context()->global_context();
  JSObject* array_proto =
      JSObject::cast(global_context->array_function()->prototype());
  return receiver->GetPrototype() == array_proto &&
         ArrayPrototypeHasNoElements(global_context, array_proto);
}


MUST_USE_RESULT static MaybeObject* CallJsBuiltin(
    const char* name,
    BuiltinArguments<NO_EXTRA_ARGUMENTS> args) {
  HandleScope handleScope;

  Handle<Object> js_builtin =
      GetProperty(Handle<JSObject>(Top::global_context()->builtins()),
                  name);
  ASSERT(js_builtin->IsJSFunction());
  Handle<JSFunction> function(Handle<JSFunction>::cast(js_builtin));
  ScopedVector<Object**> argv(args.length() - 1);
  int n_args = args.length() - 1;
  for (int i = 0; i < n_args; i++) {
    argv[i] = args.at<Object>(i + 1).location();
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


BUILTIN(ArrayPush) {
  Object* receiver = *args.receiver();
  Object* elms_obj;
  { MaybeObject* maybe_elms_obj =
        EnsureJSArrayWithWritableFastElements(receiver);
    if (maybe_elms_obj == NULL) return CallJsBuiltin("ArrayPush", args);
    if (!maybe_elms_obj->ToObject(&elms_obj)) return maybe_elms_obj;
  }
  FixedArray* elms = FixedArray::cast(elms_obj);
  JSArray* array = JSArray::cast(receiver);

  int len = Smi::cast(array->length())->value();
  int to_add = args.length() - 1;
  if (to_add == 0) {
    return Smi::FromInt(len);
  }
  // Currently fixed arrays cannot grow too big, so
  // we should never hit this case.
  ASSERT(to_add <= (Smi::kMaxValue - len));

  int new_length = len + to_add;

  if (new_length > elms->length()) {
    // New backing storage is needed.
    int capacity = new_length + (new_length >> 1) + 16;
    Object* obj;
    { MaybeObject* maybe_obj = Heap::AllocateUninitializedFixedArray(capacity);
      if (!maybe_obj->ToObject(&obj)) return maybe_obj;
    }
    FixedArray* new_elms = FixedArray::cast(obj);

    AssertNoAllocation no_gc;
    if (len > 0) {
      CopyElements(&no_gc, new_elms, 0, elms, 0, len);
    }
    FillWithHoles(new_elms, new_length, capacity);

    elms = new_elms;
    array->set_elements(elms);
  }

  // Add the provided values.
  AssertNoAllocation no_gc;
  WriteBarrierMode mode = elms->GetWriteBarrierMode(no_gc);
  for (int index = 0; index < to_add; index++) {
    elms->set(index + len, args[index + 1], mode);
  }

  // Set the length.
  array->set_length(Smi::FromInt(new_length));
  return Smi::FromInt(new_length);
}


BUILTIN(ArrayPop) {
  Object* receiver = *args.receiver();
  Object* elms_obj;
  { MaybeObject* maybe_elms_obj =
        EnsureJSArrayWithWritableFastElements(receiver);
    if (maybe_elms_obj == NULL) return CallJsBuiltin("ArrayPop", args);
    if (!maybe_elms_obj->ToObject(&elms_obj)) return maybe_elms_obj;
  }
  FixedArray* elms = FixedArray::cast(elms_obj);
  JSArray* array = JSArray::cast(receiver);

  int len = Smi::cast(array->length())->value();
  if (len == 0) return Heap::undefined_value();

  // Get top element
  MaybeObject* top = elms->get(len - 1);

  // Set the length.
  array->set_length(Smi::FromInt(len - 1));

  if (!top->IsTheHole()) {
    // Delete the top element.
    elms->set_the_hole(len - 1);
    return top;
  }

  top = array->GetPrototype()->GetElement(len - 1);

  return top;
}


BUILTIN(ArrayShift) {
  Object* receiver = *args.receiver();
  Object* elms_obj;
  { MaybeObject* maybe_elms_obj =
        EnsureJSArrayWithWritableFastElements(receiver);
    if (maybe_elms_obj == NULL) return CallJsBuiltin("ArrayShift", args);
    if (!maybe_elms_obj->ToObject(&elms_obj)) return maybe_elms_obj;
  }
  if (!IsJSArrayFastElementMovingAllowed(JSArray::cast(receiver))) {
    return CallJsBuiltin("ArrayShift", args);
  }
  FixedArray* elms = FixedArray::cast(elms_obj);
  JSArray* array = JSArray::cast(receiver);
  ASSERT(array->HasFastElements());

  int len = Smi::cast(array->length())->value();
  if (len == 0) return Heap::undefined_value();

  // Get first element
  Object* first = elms->get(0);
  if (first->IsTheHole()) {
    first = Heap::undefined_value();
  }

  if (!Heap::lo_space()->Contains(elms)) {
    // As elms still in the same space they used to be,
    // there is no need to update region dirty mark.
    array->set_elements(LeftTrimFixedArray(elms, 1), SKIP_WRITE_BARRIER);
  } else {
    // Shift the elements.
    AssertNoAllocation no_gc;
    MoveElements(&no_gc, elms, 0, elms, 1, len - 1);
    elms->set(len - 1, Heap::the_hole_value());
  }

  // Set the length.
  array->set_length(Smi::FromInt(len - 1));

  return first;
}


BUILTIN(ArrayUnshift) {
  Object* receiver = *args.receiver();
  Object* elms_obj;
  { MaybeObject* maybe_elms_obj =
        EnsureJSArrayWithWritableFastElements(receiver);
    if (maybe_elms_obj == NULL) return CallJsBuiltin("ArrayUnshift", args);
    if (!maybe_elms_obj->ToObject(&elms_obj)) return maybe_elms_obj;
  }
  if (!IsJSArrayFastElementMovingAllowed(JSArray::cast(receiver))) {
    return CallJsBuiltin("ArrayUnshift", args);
  }
  FixedArray* elms = FixedArray::cast(elms_obj);
  JSArray* array = JSArray::cast(receiver);
  ASSERT(array->HasFastElements());

  int len = Smi::cast(array->length())->value();
  int to_add = args.length() - 1;
  int new_length = len + to_add;
  // Currently fixed arrays cannot grow too big, so
  // we should never hit this case.
  ASSERT(to_add <= (Smi::kMaxValue - len));

  if (new_length > elms->length()) {
    // New backing storage is needed.
    int capacity = new_length + (new_length >> 1) + 16;
    Object* obj;
    { MaybeObject* maybe_obj = Heap::AllocateUninitializedFixedArray(capacity);
      if (!maybe_obj->ToObject(&obj)) return maybe_obj;
    }
    FixedArray* new_elms = FixedArray::cast(obj);

    AssertNoAllocation no_gc;
    if (len > 0) {
      CopyElements(&no_gc, new_elms, to_add, elms, 0, len);
    }
    FillWithHoles(new_elms, new_length, capacity);

    elms = new_elms;
    array->set_elements(elms);
  } else {
    AssertNoAllocation no_gc;
    MoveElements(&no_gc, elms, to_add, elms, 0, len);
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


BUILTIN(ArraySlice) {
  Object* receiver = *args.receiver();
  FixedArray* elms;
  int len = -1;
  if (receiver->IsJSArray()) {
    JSArray* array = JSArray::cast(receiver);
    if (!array->HasFastElements() ||
        !IsJSArrayFastElementMovingAllowed(array)) {
      return CallJsBuiltin("ArraySlice", args);
    }

    elms = FixedArray::cast(array->elements());
    len = Smi::cast(array->length())->value();
  } else {
    // Array.slice(arguments, ...) is quite a common idiom (notably more
    // than 50% of invocations in Web apps).  Treat it in C++ as well.
    Map* arguments_map =
        Top::context()->global_context()->arguments_boilerplate()->map();

    bool is_arguments_object_with_fast_elements =
        receiver->IsJSObject()
        && JSObject::cast(receiver)->map() == arguments_map
        && JSObject::cast(receiver)->HasFastElements();
    if (!is_arguments_object_with_fast_elements) {
      return CallJsBuiltin("ArraySlice", args);
    }
    elms = FixedArray::cast(JSObject::cast(receiver)->elements());
    Object* len_obj = JSObject::cast(receiver)
        ->InObjectPropertyAt(Heap::arguments_length_index);
    if (!len_obj->IsSmi()) {
      return CallJsBuiltin("ArraySlice", args);
    }
    len = Smi::cast(len_obj)->value();
    if (len > elms->length()) {
      return CallJsBuiltin("ArraySlice", args);
    }
    for (int i = 0; i < len; i++) {
      if (elms->get(i) == Heap::the_hole_value()) {
        return CallJsBuiltin("ArraySlice", args);
      }
    }
  }
  ASSERT(len >= 0);
  int n_arguments = args.length() - 1;

  // Note carefully choosen defaults---if argument is missing,
  // it's undefined which gets converted to 0 for relative_start
  // and to len for relative_end.
  int relative_start = 0;
  int relative_end = len;
  if (n_arguments > 0) {
    Object* arg1 = args[1];
    if (arg1->IsSmi()) {
      relative_start = Smi::cast(arg1)->value();
    } else if (!arg1->IsUndefined()) {
      return CallJsBuiltin("ArraySlice", args);
    }
    if (n_arguments > 1) {
      Object* arg2 = args[2];
      if (arg2->IsSmi()) {
        relative_end = Smi::cast(arg2)->value();
      } else if (!arg2->IsUndefined()) {
        return CallJsBuiltin("ArraySlice", args);
      }
    }
  }

  // ECMAScript 232, 3rd Edition, Section 15.4.4.10, step 6.
  int k = (relative_start < 0) ? Max(len + relative_start, 0)
                               : Min(relative_start, len);

  // ECMAScript 232, 3rd Edition, Section 15.4.4.10, step 8.
  int final = (relative_end < 0) ? Max(len + relative_end, 0)
                                 : Min(relative_end, len);

  // Calculate the length of result array.
  int result_len = final - k;
  if (result_len <= 0) {
    return AllocateEmptyJSArray();
  }

  Object* result;
  { MaybeObject* maybe_result = AllocateJSArray();
    if (!maybe_result->ToObject(&result)) return maybe_result;
  }
  JSArray* result_array = JSArray::cast(result);

  { MaybeObject* maybe_result =
        Heap::AllocateUninitializedFixedArray(result_len);
    if (!maybe_result->ToObject(&result)) return maybe_result;
  }
  FixedArray* result_elms = FixedArray::cast(result);

  AssertNoAllocation no_gc;
  CopyElements(&no_gc, result_elms, 0, elms, k, result_len);

  // Set elements.
  result_array->set_elements(result_elms);

  // Set the length.
  result_array->set_length(Smi::FromInt(result_len));
  return result_array;
}


BUILTIN(ArraySplice) {
  Object* receiver = *args.receiver();
  Object* elms_obj;
  { MaybeObject* maybe_elms_obj =
        EnsureJSArrayWithWritableFastElements(receiver);
    if (maybe_elms_obj == NULL) return CallJsBuiltin("ArraySplice", args);
    if (!maybe_elms_obj->ToObject(&elms_obj)) return maybe_elms_obj;
  }
  if (!IsJSArrayFastElementMovingAllowed(JSArray::cast(receiver))) {
    return CallJsBuiltin("ArraySplice", args);
  }
  FixedArray* elms = FixedArray::cast(elms_obj);
  JSArray* array = JSArray::cast(receiver);
  ASSERT(array->HasFastElements());

  int len = Smi::cast(array->length())->value();

  int n_arguments = args.length() - 1;

  int relative_start = 0;
  if (n_arguments > 0) {
    Object* arg1 = args[1];
    if (arg1->IsSmi()) {
      relative_start = Smi::cast(arg1)->value();
    } else if (!arg1->IsUndefined()) {
      return CallJsBuiltin("ArraySplice", args);
    }
  }
  int actual_start = (relative_start < 0) ? Max(len + relative_start, 0)
                                          : Min(relative_start, len);

  // SpiderMonkey, TraceMonkey and JSC treat the case where no delete count is
  // given as a request to delete all the elements from the start.
  // And it differs from the case of undefined delete count.
  // This does not follow ECMA-262, but we do the same for
  // compatibility.
  int actual_delete_count;
  if (n_arguments == 1) {
    ASSERT(len - actual_start >= 0);
    actual_delete_count = len - actual_start;
  } else {
    int value = 0;  // ToInteger(undefined) == 0
    if (n_arguments > 1) {
      Object* arg2 = args[2];
      if (arg2->IsSmi()) {
        value = Smi::cast(arg2)->value();
      } else {
        return CallJsBuiltin("ArraySplice", args);
      }
    }
    actual_delete_count = Min(Max(value, 0), len - actual_start);
  }

  JSArray* result_array = NULL;
  if (actual_delete_count == 0) {
    Object* result;
    { MaybeObject* maybe_result = AllocateEmptyJSArray();
      if (!maybe_result->ToObject(&result)) return maybe_result;
    }
    result_array = JSArray::cast(result);
  } else {
    // Allocate result array.
    Object* result;
    { MaybeObject* maybe_result = AllocateJSArray();
      if (!maybe_result->ToObject(&result)) return maybe_result;
    }
    result_array = JSArray::cast(result);

    { MaybeObject* maybe_result =
          Heap::AllocateUninitializedFixedArray(actual_delete_count);
      if (!maybe_result->ToObject(&result)) return maybe_result;
    }
    FixedArray* result_elms = FixedArray::cast(result);

    AssertNoAllocation no_gc;
    // Fill newly created array.
    CopyElements(&no_gc,
                 result_elms, 0,
                 elms, actual_start,
                 actual_delete_count);

    // Set elements.
    result_array->set_elements(result_elms);

    // Set the length.
    result_array->set_length(Smi::FromInt(actual_delete_count));
  }

  int item_count = (n_arguments > 1) ? (n_arguments - 2) : 0;

  int new_length = len - actual_delete_count + item_count;

  if (item_count < actual_delete_count) {
    // Shrink the array.
    const bool trim_array = !Heap::lo_space()->Contains(elms) &&
      ((actual_start + item_count) <
          (len - actual_delete_count - actual_start));
    if (trim_array) {
      const int delta = actual_delete_count - item_count;

      if (actual_start > 0) {
        Object** start = elms->data_start();
        memmove(start + delta, start, actual_start * kPointerSize);
      }

      elms = LeftTrimFixedArray(elms, delta);
      array->set_elements(elms, SKIP_WRITE_BARRIER);
    } else {
      AssertNoAllocation no_gc;
      MoveElements(&no_gc,
                   elms, actual_start + item_count,
                   elms, actual_start + actual_delete_count,
                   (len - actual_delete_count - actual_start));
      FillWithHoles(elms, new_length, len);
    }
  } else if (item_count > actual_delete_count) {
    // Currently fixed arrays cannot grow too big, so
    // we should never hit this case.
    ASSERT((item_count - actual_delete_count) <= (Smi::kMaxValue - len));

    // Check if array need to grow.
    if (new_length > elms->length()) {
      // New backing storage is needed.
      int capacity = new_length + (new_length >> 1) + 16;
      Object* obj;
      { MaybeObject* maybe_obj =
            Heap::AllocateUninitializedFixedArray(capacity);
        if (!maybe_obj->ToObject(&obj)) return maybe_obj;
      }
      FixedArray* new_elms = FixedArray::cast(obj);

      AssertNoAllocation no_gc;
      // Copy the part before actual_start as is.
      if (actual_start > 0) {
        CopyElements(&no_gc, new_elms, 0, elms, 0, actual_start);
      }
      const int to_copy = len - actual_delete_count - actual_start;
      if (to_copy > 0) {
        CopyElements(&no_gc,
                     new_elms, actual_start + item_count,
                     elms, actual_start + actual_delete_count,
                     to_copy);
      }
      FillWithHoles(new_elms, new_length, capacity);

      elms = new_elms;
      array->set_elements(elms);
    } else {
      AssertNoAllocation no_gc;
      MoveElements(&no_gc,
                   elms, actual_start + item_count,
                   elms, actual_start + actual_delete_count,
                   (len - actual_delete_count - actual_start));
    }
  }

  AssertNoAllocation no_gc;
  WriteBarrierMode mode = elms->GetWriteBarrierMode(no_gc);
  for (int k = actual_start; k < actual_start + item_count; k++) {
    elms->set(k, args[3 + k - actual_start], mode);
  }

  // Set the length.
  array->set_length(Smi::FromInt(new_length));

  return result_array;
}


BUILTIN(ArrayConcat) {
  Context* global_context = Top::context()->global_context();
  JSObject* array_proto =
      JSObject::cast(global_context->array_function()->prototype());
  if (!ArrayPrototypeHasNoElements(global_context, array_proto)) {
    return CallJsBuiltin("ArrayConcat", args);
  }

  // Iterate through all the arguments performing checks
  // and calculating total length.
  int n_arguments = args.length();
  int result_len = 0;
  for (int i = 0; i < n_arguments; i++) {
    Object* arg = args[i];
    if (!arg->IsJSArray() || !JSArray::cast(arg)->HasFastElements()
        || JSArray::cast(arg)->GetPrototype() != array_proto) {
      return CallJsBuiltin("ArrayConcat", args);
    }

    int len = Smi::cast(JSArray::cast(arg)->length())->value();

    // We shouldn't overflow when adding another len.
    const int kHalfOfMaxInt = 1 << (kBitsPerInt - 2);
    STATIC_ASSERT(FixedArray::kMaxLength < kHalfOfMaxInt);
    USE(kHalfOfMaxInt);
    result_len += len;
    ASSERT(result_len >= 0);

    if (result_len > FixedArray::kMaxLength) {
      return CallJsBuiltin("ArrayConcat", args);
    }
  }

  if (result_len == 0) {
    return AllocateEmptyJSArray();
  }

  // Allocate result.
  Object* result;
  { MaybeObject* maybe_result = AllocateJSArray();
    if (!maybe_result->ToObject(&result)) return maybe_result;
  }
  JSArray* result_array = JSArray::cast(result);

  { MaybeObject* maybe_result =
        Heap::AllocateUninitializedFixedArray(result_len);
    if (!maybe_result->ToObject(&result)) return maybe_result;
  }
  FixedArray* result_elms = FixedArray::cast(result);

  // Copy data.
  AssertNoAllocation no_gc;
  int start_pos = 0;
  for (int i = 0; i < n_arguments; i++) {
    JSArray* array = JSArray::cast(args[i]);
    int len = Smi::cast(array->length())->value();
    if (len > 0) {
      FixedArray* elms = FixedArray::cast(array->elements());
      CopyElements(&no_gc, result_elms, start_pos, elms, 0, len);
      start_pos += len;
    }
  }
  ASSERT(start_pos == result_len);

  // Set the length and elements.
  result_array->set_length(Smi::FromInt(result_len));
  result_array->set_elements(result_elms);

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
MUST_USE_RESULT static MaybeObject* HandleApiCallHelper(
    BuiltinArguments<NEEDS_CALLED_FUNCTION> args) {
  ASSERT(is_construct == CalledAsConstructor());

  HandleScope scope;
  Handle<JSFunction> function = args.called_function();
  ASSERT(function->shared()->IsApiFunction());

  FunctionTemplateInfo* fun_data = function->shared()->get_api_func_data();
  if (is_construct) {
    Handle<FunctionTemplateInfo> desc(fun_data);
    bool pending_exception = false;
    Factory::ConfigureInstance(desc, Handle<JSObject>::cast(args.receiver()),
                               &pending_exception);
    ASSERT(Top::has_pending_exception() == pending_exception);
    if (pending_exception) return Failure::Exception();
    fun_data = *desc;
  }

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

    LOG(ApiObjectAccess("call", JSObject::cast(*args.receiver())));
    ASSERT(raw_holder->IsJSObject());

    CustomArguments custom;
    v8::ImplementationUtilities::PrepareArgumentsData(custom.end(),
        data_obj, *function, raw_holder);

    v8::Arguments new_args = v8::ImplementationUtilities::NewArguments(
        custom.end(),
        &args[0] - 1,
        args.length() - 1,
        is_construct);

    v8::Handle<v8::Value> value;
    {
      // Leaving JavaScript.
      VMState state(EXTERNAL);
      ExternalCallbackScope call_scope(v8::ToCData<Address>(callback_obj));
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
  ASSERT(function->shared()->IsApiFunction());
  FunctionTemplateInfo* info = function->shared()->get_api_func_data();
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

  // We expect four more arguments: callback, function, call data, and holder.
  const int args_length = args.length() - 4;
  ASSERT(args_length >= 0);

  Object* callback_obj = args[args_length];

  v8::Arguments new_args = v8::ImplementationUtilities::NewArguments(
      &args[args_length + 1],
      &args[0] - 1,
      args_length - 1,
      is_construct);

#ifdef DEBUG
  VerifyTypeCheck(Utils::OpenHandle(*new_args.Holder()),
                  Utils::OpenHandle(*new_args.Callee()));
#endif
  HandleScope scope;
  Object* result;
  v8::Handle<v8::Value> value;
  {
    // Leaving JavaScript.
    VMState state(EXTERNAL);
    ExternalCallbackScope call_scope(v8::ToCData<Address>(callback_obj));
    v8::InvocationCallback callback =
        v8::ToCData<v8::InvocationCallback>(callback_obj);

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
MUST_USE_RESULT static MaybeObject* HandleApiCallAsFunctionOrConstructor(
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
  ASSERT(constructor->shared()->IsApiFunction());
  Object* handler =
      constructor->shared()->get_api_func_data()->instance_call_handler();
  ASSERT(!handler->IsUndefined());
  CallHandlerInfo* call_data = CallHandlerInfo::cast(handler);
  Object* callback_obj = call_data->callback();
  v8::InvocationCallback callback =
      v8::ToCData<v8::InvocationCallback>(callback_obj);

  // Get the data for the call and perform the callback.
  Object* result;
  {
    HandleScope scope;

    LOG(ApiObjectAccess("call non-function", obj));

    CustomArguments custom;
    v8::ImplementationUtilities::PrepareArgumentsData(custom.end(),
        call_data->data(), constructor, obj);
    v8::Arguments new_args = v8::ImplementationUtilities::NewArguments(
        custom.end(),
        &args[0] - 1,
        args.length() - 1,
        is_construct_call);
    v8::Handle<v8::Value> value;
    {
      // Leaving JavaScript.
      VMState state(EXTERNAL);
      ExternalCallbackScope call_scope(v8::ToCData<Address>(callback_obj));
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
  LoadIC::GenerateStringLength(masm, false);
}


static void Generate_LoadIC_StringWrapperLength(MacroAssembler* masm) {
  LoadIC::GenerateStringLength(masm, true);
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


static void Generate_KeyedLoadIC_PreMonomorphic(MacroAssembler* masm) {
  KeyedLoadIC::GeneratePreMonomorphic(masm);
}

static void Generate_KeyedLoadIC_IndexedInterceptor(MacroAssembler* masm) {
  KeyedLoadIC::GenerateIndexedInterceptor(masm);
}


static void Generate_StoreIC_Initialize(MacroAssembler* masm) {
  StoreIC::GenerateInitialize(masm);
}


static void Generate_StoreIC_Initialize_Strict(MacroAssembler* masm) {
  StoreIC::GenerateInitialize(masm);
}


static void Generate_StoreIC_Miss(MacroAssembler* masm) {
  StoreIC::GenerateMiss(masm);
}


static void Generate_StoreIC_Normal(MacroAssembler* masm) {
  StoreIC::GenerateNormal(masm);
}


static void Generate_StoreIC_Normal_Strict(MacroAssembler* masm) {
  StoreIC::GenerateNormal(masm);
}


static void Generate_StoreIC_Megamorphic(MacroAssembler* masm) {
  StoreIC::GenerateMegamorphic(masm, StoreIC::kStoreICNonStrict);
}


static void Generate_StoreIC_Megamorphic_Strict(MacroAssembler* masm) {
  StoreIC::GenerateMegamorphic(masm, StoreIC::kStoreICStrict);
}


static void Generate_StoreIC_ArrayLength(MacroAssembler* masm) {
  StoreIC::GenerateArrayLength(masm);
}


static void Generate_StoreIC_ArrayLength_Strict(MacroAssembler* masm) {
  StoreIC::GenerateArrayLength(masm);
}


static void Generate_StoreIC_GlobalProxy(MacroAssembler* masm) {
  StoreIC::GenerateGlobalProxy(masm);
}


static void Generate_StoreIC_GlobalProxy_Strict(MacroAssembler* masm) {
  StoreIC::GenerateGlobalProxy(masm);
}


static void Generate_KeyedStoreIC_Generic(MacroAssembler* masm) {
  KeyedStoreIC::GenerateGeneric(masm);
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


static void Generate_Slot_DebugBreak(MacroAssembler* masm) {
  Debug::GenerateSlotDebugBreak(masm);
}


static void Generate_PlainReturn_LiveEdit(MacroAssembler* masm) {
  Debug::GeneratePlainReturnLiveEdit(masm);
}


static void Generate_FrameDropper_LiveEdit(MacroAssembler* masm) {
  Debug::GenerateFrameDropperLiveEdit(masm);
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

#define DEF_FUNCTION_PTR_A(name, kind, state, extra)              \
    { FUNCTION_ADDR(Generate_##name),                             \
      NULL,                                                       \
      #name,                                                      \
      name,                                                       \
      Code::ComputeFlags(Code::kind, NOT_IN_LOOP, state, extra),  \
      NO_EXTRA_ARGUMENTS                                          \
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
      Object* code = NULL;
      {
        // During startup it's OK to always allocate and defer GC to later.
        // This simplifies things because we don't need to retry.
        AlwaysAllocateScope __scope__;
        { MaybeObject* maybe_code =
              Heap::CreateCode(desc, flags, masm.CodeObject());
          if (!maybe_code->ToObject(&code)) {
            v8::internal::V8::FatalProcessOutOfMemory("CreateCode");
          }
        }
      }
      // Log the event and add the code to the builtins array.
      PROFILE(CodeCreateEvent(Logger::BUILTIN_TAG,
                              Code::cast(code),
                              functions[i].s_name));
      GDBJIT(AddCode(GDBJITInterface::BUILTIN,
                     functions[i].s_name,
                     Code::cast(code)));
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
