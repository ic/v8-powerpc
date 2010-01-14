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

#include "accessors.h"
#include "api.h"
#include "bootstrapper.h"
#include "compiler.h"
#include "debug.h"
#include "execution.h"
#include "global-handles.h"
#include "macro-assembler.h"
#include "natives.h"
#include "snapshot.h"

namespace v8 {
namespace internal {

// A SourceCodeCache uses a FixedArray to store pairs of
// (AsciiString*, JSFunction*), mapping names of native code files
// (runtime.js, etc.) to precompiled functions. Instead of mapping
// names to functions it might make sense to let the JS2C tool
// generate an index for each native JS file.
class SourceCodeCache BASE_EMBEDDED {
 public:
  explicit SourceCodeCache(Script::Type type): type_(type), cache_(NULL) { }

  void Initialize(bool create_heap_objects) {
    cache_ = create_heap_objects ? Heap::empty_fixed_array() : NULL;
  }

  void Iterate(ObjectVisitor* v) {
    v->VisitPointer(bit_cast<Object**, FixedArray**>(&cache_));
  }


  bool Lookup(Vector<const char> name, Handle<JSFunction>* handle) {
    for (int i = 0; i < cache_->length(); i+=2) {
      SeqAsciiString* str = SeqAsciiString::cast(cache_->get(i));
      if (str->IsEqualTo(name)) {
        *handle = Handle<JSFunction>(JSFunction::cast(cache_->get(i + 1)));
        return true;
      }
    }
    return false;
  }


  void Add(Vector<const char> name, Handle<JSFunction> fun) {
    ASSERT(fun->IsBoilerplate());
    HandleScope scope;
    int length = cache_->length();
    Handle<FixedArray> new_array =
        Factory::NewFixedArray(length + 2, TENURED);
    cache_->CopyTo(0, *new_array, 0, cache_->length());
    cache_ = *new_array;
    Handle<String> str = Factory::NewStringFromAscii(name, TENURED);
    cache_->set(length, *str);
    cache_->set(length + 1, *fun);
    Script::cast(fun->shared()->script())->set_type(Smi::FromInt(type_));
  }

 private:
  Script::Type type_;
  FixedArray* cache_;
  DISALLOW_COPY_AND_ASSIGN(SourceCodeCache);
};

static SourceCodeCache natives_cache(Script::TYPE_NATIVE);
static SourceCodeCache extensions_cache(Script::TYPE_EXTENSION);
// This is for delete, not delete[].
static List<char*>* delete_these_non_arrays_on_tear_down = NULL;
// This is for delete[]
static List<char*>* delete_these_arrays_on_tear_down = NULL;


NativesExternalStringResource::NativesExternalStringResource(const char* source)
    : data_(source), length_(StrLength(source)) {
  if (delete_these_non_arrays_on_tear_down == NULL) {
    delete_these_non_arrays_on_tear_down = new List<char*>(2);
  }
  // The resources are small objects and we only make a fixed number of
  // them, but let's clean them up on exit for neatness.
  delete_these_non_arrays_on_tear_down->
      Add(reinterpret_cast<char*>(this));
}


Handle<String> Bootstrapper::NativesSourceLookup(int index) {
  ASSERT(0 <= index && index < Natives::GetBuiltinsCount());
  if (Heap::natives_source_cache()->get(index)->IsUndefined()) {
    if (!Snapshot::IsEnabled() || FLAG_new_snapshot) {
      // We can use external strings for the natives.
      NativesExternalStringResource* resource =
          new NativesExternalStringResource(
              Natives::GetScriptSource(index).start());
      Handle<String> source_code =
          Factory::NewExternalStringFromAscii(resource);
      Heap::natives_source_cache()->set(index, *source_code);
    } else {
      // Old snapshot code can't cope with external strings at all.
      Handle<String> source_code =
        Factory::NewStringFromAscii(Natives::GetScriptSource(index));
      Heap::natives_source_cache()->set(index, *source_code);
    }
  }
  Handle<Object> cached_source(Heap::natives_source_cache()->get(index));
  return Handle<String>::cast(cached_source);
}


bool Bootstrapper::NativesCacheLookup(Vector<const char> name,
                                      Handle<JSFunction>* handle) {
  return natives_cache.Lookup(name, handle);
}


void Bootstrapper::NativesCacheAdd(Vector<const char> name,
                                   Handle<JSFunction> fun) {
  natives_cache.Add(name, fun);
}


void Bootstrapper::Initialize(bool create_heap_objects) {
  natives_cache.Initialize(create_heap_objects);
  extensions_cache.Initialize(create_heap_objects);
}


char* Bootstrapper::AllocateAutoDeletedArray(int bytes) {
  char* memory = new char[bytes];
  if (memory != NULL) {
    if (delete_these_arrays_on_tear_down == NULL) {
      delete_these_arrays_on_tear_down = new List<char*>(2);
    }
    delete_these_arrays_on_tear_down->Add(memory);
  }
  return memory;
}


void Bootstrapper::TearDown() {
  if (delete_these_non_arrays_on_tear_down != NULL) {
    int len = delete_these_non_arrays_on_tear_down->length();
    ASSERT(len < 20);  // Don't use this mechanism for unbounded allocations.
    for (int i = 0; i < len; i++) {
      delete delete_these_non_arrays_on_tear_down->at(i);
      delete_these_non_arrays_on_tear_down->at(i) = NULL;
    }
    delete delete_these_non_arrays_on_tear_down;
    delete_these_non_arrays_on_tear_down = NULL;
  }

  if (delete_these_arrays_on_tear_down != NULL) {
    int len = delete_these_arrays_on_tear_down->length();
    ASSERT(len < 1000);  // Don't use this mechanism for unbounded allocations.
    for (int i = 0; i < len; i++) {
      delete[] delete_these_arrays_on_tear_down->at(i);
      delete_these_arrays_on_tear_down->at(i) = NULL;
    }
    delete delete_these_arrays_on_tear_down;
    delete_these_arrays_on_tear_down = NULL;
  }

  natives_cache.Initialize(false);  // Yes, symmetrical
  extensions_cache.Initialize(false);
}


// Pending fixups are code positions that refer to builtin code
// objects that were not available at the time the code was generated.
// The pending list is processed whenever an environment has been
// created.
class PendingFixups : public AllStatic {
 public:
  static void Add(Code* code, MacroAssembler* masm);
  static bool Process(Handle<JSBuiltinsObject> builtins);

  static void Iterate(ObjectVisitor* v);

 private:
  static List<Object*> code_;
  static List<const char*> name_;
  static List<int> pc_;
  static List<uint32_t> flags_;

  static void Clear();
};


List<Object*> PendingFixups::code_(0);
List<const char*> PendingFixups::name_(0);
List<int> PendingFixups::pc_(0);
List<uint32_t> PendingFixups::flags_(0);


void PendingFixups::Add(Code* code, MacroAssembler* masm) {
  // Note this code is not only called during bootstrapping.
  List<MacroAssembler::Unresolved>* unresolved = masm->unresolved();
  int n = unresolved->length();
  for (int i = 0; i < n; i++) {
    const char* name = unresolved->at(i).name;
    code_.Add(code);
    name_.Add(name);
    pc_.Add(unresolved->at(i).pc);
    flags_.Add(unresolved->at(i).flags);
    LOG(StringEvent("unresolved", name));
  }
}


bool PendingFixups::Process(Handle<JSBuiltinsObject> builtins) {
  HandleScope scope;
  // NOTE: Extra fixups may be added to the list during the iteration
  // due to lazy compilation of functions during the processing. Do not
  // cache the result of getting the length of the code list.
  for (int i = 0; i < code_.length(); i++) {
    const char* name = name_[i];
    uint32_t flags = flags_[i];
    Handle<String> symbol = Factory::LookupAsciiSymbol(name);
    Object* o = builtins->GetProperty(*symbol);
#ifdef DEBUG
    if (!o->IsJSFunction()) {
      V8_Fatal(__FILE__, __LINE__, "Cannot resolve call to builtin %s", name);
    }
#endif
    Handle<JSFunction> f = Handle<JSFunction>(JSFunction::cast(o));
    // Make sure the number of parameters match the formal parameter count.
    int argc = Bootstrapper::FixupFlagsArgumentsCount::decode(flags);
    USE(argc);
    ASSERT(f->shared()->formal_parameter_count() == argc);
    if (!f->is_compiled()) {
      // Do lazy compilation and check for stack overflows.
      if (!CompileLazy(f, CLEAR_EXCEPTION)) {
        Clear();
        return false;
      }
    }
    Code* code = Code::cast(code_[i]);
    Address pc = code->instruction_start() + pc_[i];
    RelocInfo target(pc, RelocInfo::CODE_TARGET, 0);
    bool use_code_object = Bootstrapper::FixupFlagsUseCodeObject::decode(flags);
    if (use_code_object) {
      target.set_target_object(f->code());
    } else {
      target.set_target_address(f->code()->instruction_start());
    }
    LOG(StringEvent("resolved", name));
  }
  Clear();

  // TODO(1240818): We should probably try to avoid doing this for all
  // the V8 builtin JS files. It should only happen after running
  // runtime.js - just like there shouldn't be any fixups left after
  // that.
  for (int i = 0; i < Builtins::NumberOfJavaScriptBuiltins(); i++) {
    Builtins::JavaScript id = static_cast<Builtins::JavaScript>(i);
    Handle<String> name = Factory::LookupAsciiSymbol(Builtins::GetName(id));
    JSFunction* function = JSFunction::cast(builtins->GetProperty(*name));
    builtins->set_javascript_builtin(id, function);
  }

  return true;
}


void PendingFixups::Clear() {
  code_.Clear();
  name_.Clear();
  pc_.Clear();
  flags_.Clear();
}


void PendingFixups::Iterate(ObjectVisitor* v) {
  if (!code_.is_empty()) {
    v->VisitPointers(&code_[0], &code_[0] + code_.length());
  }
}


class Genesis BASE_EMBEDDED {
 public:
  Genesis(Handle<Object> global_object,
          v8::Handle<v8::ObjectTemplate> global_template,
          v8::ExtensionConfiguration* extensions);
  ~Genesis();

  Handle<Context> result() { return result_; }

  Genesis* previous() { return previous_; }
  static Genesis* current() { return current_; }

  // Support for thread preemption.
  static int ArchiveSpacePerThread();
  static char* ArchiveState(char* to);
  static char* RestoreState(char* from);

 private:
  Handle<Context> global_context_;

  // There may be more than one active genesis object: When GC is
  // triggered during environment creation there may be weak handle
  // processing callbacks which may create new environments.
  Genesis* previous_;
  static Genesis* current_;

  Handle<Context> global_context() { return global_context_; }

  void CreateRoots(v8::Handle<v8::ObjectTemplate> global_template,
                   Handle<Object> global_object);
  void InstallNativeFunctions();
  bool InstallNatives();
  bool InstallExtensions(v8::ExtensionConfiguration* extensions);
  bool InstallExtension(const char* name);
  bool InstallExtension(v8::RegisteredExtension* current);
  bool InstallSpecialObjects();
  bool ConfigureApiObject(Handle<JSObject> object,
                          Handle<ObjectTemplateInfo> object_template);
  bool ConfigureGlobalObjects(v8::Handle<v8::ObjectTemplate> global_template);

  // Migrates all properties from the 'from' object to the 'to'
  // object and overrides the prototype in 'to' with the one from
  // 'from'.
  void TransferObject(Handle<JSObject> from, Handle<JSObject> to);
  void TransferNamedProperties(Handle<JSObject> from, Handle<JSObject> to);
  void TransferIndexedProperties(Handle<JSObject> from, Handle<JSObject> to);

  Handle<DescriptorArray> ComputeFunctionInstanceDescriptor(
      bool make_prototype_read_only,
      bool make_prototype_enumerable = false);
  void MakeFunctionInstancePrototypeWritable();

  void AddSpecialFunction(Handle<JSObject> prototype,
                          const char* name,
                          Handle<Code> code);

  void BuildSpecialFunctionTable();

  static bool CompileBuiltin(int index);
  static bool CompileNative(Vector<const char> name, Handle<String> source);
  static bool CompileScriptCached(Vector<const char> name,
                                  Handle<String> source,
                                  SourceCodeCache* cache,
                                  v8::Extension* extension,
                                  bool use_runtime_context);

  Handle<Context> result_;
};

Genesis* Genesis::current_ = NULL;


void Bootstrapper::Iterate(ObjectVisitor* v) {
  natives_cache.Iterate(v);
  v->Synchronize("NativesCache");
  extensions_cache.Iterate(v);
  v->Synchronize("Extensions");
  PendingFixups::Iterate(v);
  v->Synchronize("PendingFixups");
}


// While setting up the environment, we collect code positions that
// need to be patched before we can run any code in the environment.
void Bootstrapper::AddFixup(Code* code, MacroAssembler* masm) {
  PendingFixups::Add(code, masm);
}


bool Bootstrapper::IsActive() {
  return Genesis::current() != NULL;
}


Handle<Context> Bootstrapper::CreateEnvironment(
    Handle<Object> global_object,
    v8::Handle<v8::ObjectTemplate> global_template,
    v8::ExtensionConfiguration* extensions) {
  Genesis genesis(global_object, global_template, extensions);
  return genesis.result();
}


static void SetObjectPrototype(Handle<JSObject> object, Handle<Object> proto) {
  // object.__proto__ = proto;
  Handle<Map> old_to_map = Handle<Map>(object->map());
  Handle<Map> new_to_map = Factory::CopyMapDropTransitions(old_to_map);
  new_to_map->set_prototype(*proto);
  object->set_map(*new_to_map);
}


void Bootstrapper::DetachGlobal(Handle<Context> env) {
  JSGlobalProxy::cast(env->global_proxy())->set_context(*Factory::null_value());
  SetObjectPrototype(Handle<JSObject>(env->global_proxy()),
                     Factory::null_value());
  env->set_global_proxy(env->global());
  env->global()->set_global_receiver(env->global());
}


Genesis::~Genesis() {
  ASSERT(current_ == this);
  current_ = previous_;
}


static Handle<JSFunction> InstallFunction(Handle<JSObject> target,
                                          const char* name,
                                          InstanceType type,
                                          int instance_size,
                                          Handle<JSObject> prototype,
                                          Builtins::Name call,
                                          bool is_ecma_native) {
  Handle<String> symbol = Factory::LookupAsciiSymbol(name);
  Handle<Code> call_code = Handle<Code>(Builtins::builtin(call));
  Handle<JSFunction> function =
    Factory::NewFunctionWithPrototype(symbol,
                                      type,
                                      instance_size,
                                      prototype,
                                      call_code,
                                      is_ecma_native);
  SetProperty(target, symbol, function, DONT_ENUM);
  if (is_ecma_native) {
    function->shared()->set_instance_class_name(*symbol);
  }
  return function;
}


Handle<DescriptorArray> Genesis::ComputeFunctionInstanceDescriptor(
    bool make_prototype_read_only,
    bool make_prototype_enumerable) {
  Handle<DescriptorArray> result = Factory::empty_descriptor_array();

  // Add prototype.
  PropertyAttributes attributes = static_cast<PropertyAttributes>(
      (make_prototype_enumerable ? 0 : DONT_ENUM)
      | DONT_DELETE
      | (make_prototype_read_only ? READ_ONLY : 0));
  result =
      Factory::CopyAppendProxyDescriptor(
          result,
          Factory::prototype_symbol(),
          Factory::NewProxy(&Accessors::FunctionPrototype),
          attributes);

  attributes =
      static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE | READ_ONLY);
  // Add length.
  result =
      Factory::CopyAppendProxyDescriptor(
          result,
          Factory::length_symbol(),
          Factory::NewProxy(&Accessors::FunctionLength),
          attributes);

  // Add name.
  result =
      Factory::CopyAppendProxyDescriptor(
          result,
          Factory::name_symbol(),
          Factory::NewProxy(&Accessors::FunctionName),
          attributes);

  // Add arguments.
  result =
      Factory::CopyAppendProxyDescriptor(
          result,
          Factory::arguments_symbol(),
          Factory::NewProxy(&Accessors::FunctionArguments),
          attributes);

  // Add caller.
  result =
      Factory::CopyAppendProxyDescriptor(
          result,
          Factory::caller_symbol(),
          Factory::NewProxy(&Accessors::FunctionCaller),
          attributes);

  return result;
}


void Genesis::CreateRoots(v8::Handle<v8::ObjectTemplate> global_template,
                          Handle<Object> global_object) {
  HandleScope scope;
  // Allocate the global context FixedArray first and then patch the
  // closure and extension object later (we need the empty function
  // and the global object, but in order to create those, we need the
  // global context).
  global_context_ =
      Handle<Context>::cast(
          GlobalHandles::Create(*Factory::NewGlobalContext()));
  Top::set_context(*global_context());

  // Allocate the message listeners object.
  v8::NeanderArray listeners;
  global_context()->set_message_listeners(*listeners.value());

  // Allocate the map for function instances.
  Handle<Map> fm = Factory::NewMap(JS_FUNCTION_TYPE, JSFunction::kSize);
  global_context()->set_function_instance_map(*fm);
  // Please note that the prototype property for function instances must be
  // writable.
  Handle<DescriptorArray> function_map_descriptors =
      ComputeFunctionInstanceDescriptor(false, false);
  fm->set_instance_descriptors(*function_map_descriptors);

  // Allocate the function map first and then patch the prototype later
  fm = Factory::NewMap(JS_FUNCTION_TYPE, JSFunction::kSize);
  global_context()->set_function_map(*fm);
  function_map_descriptors = ComputeFunctionInstanceDescriptor(true);
  fm->set_instance_descriptors(*function_map_descriptors);

  Handle<String> object_name = Handle<String>(Heap::Object_symbol());

  {  // --- O b j e c t ---
    Handle<JSFunction> object_fun =
        Factory::NewFunction(object_name, Factory::null_value());
    Handle<Map> object_function_map =
        Factory::NewMap(JS_OBJECT_TYPE, JSObject::kHeaderSize);
    object_fun->set_initial_map(*object_function_map);
    object_function_map->set_constructor(*object_fun);

    global_context()->set_object_function(*object_fun);

    // Allocate a new prototype for the object function.
    Handle<JSObject> prototype = Factory::NewJSObject(Top::object_function(),
                                                      TENURED);

    global_context()->set_initial_object_prototype(*prototype);
    SetPrototype(object_fun, prototype);
    object_function_map->
      set_instance_descriptors(Heap::empty_descriptor_array());
  }

  // Allocate the empty function as the prototype for function ECMAScript
  // 262 15.3.4.
  Handle<String> symbol = Factory::LookupAsciiSymbol("Empty");
  Handle<JSFunction> empty_function =
      Factory::NewFunction(symbol, Factory::null_value());

  {  // --- E m p t y ---
    Handle<Code> code =
        Handle<Code>(Builtins::builtin(Builtins::EmptyFunction));
    empty_function->set_code(*code);
    Handle<String> source = Factory::NewStringFromAscii(CStrVector("() {}"));
    Handle<Script> script = Factory::NewScript(source);
    script->set_type(Smi::FromInt(Script::TYPE_NATIVE));
    empty_function->shared()->set_script(*script);
    empty_function->shared()->set_start_position(0);
    empty_function->shared()->set_end_position(source->length());
    empty_function->shared()->DontAdaptArguments();
    global_context()->function_map()->set_prototype(*empty_function);
    global_context()->function_instance_map()->set_prototype(*empty_function);

    // Allocate the function map first and then patch the prototype later
    Handle<Map> empty_fm = Factory::CopyMapDropDescriptors(fm);
    empty_fm->set_instance_descriptors(*function_map_descriptors);
    empty_fm->set_prototype(global_context()->object_function()->prototype());
    empty_function->set_map(*empty_fm);
  }

  {  // --- G l o b a l ---
    // Step 1: create a fresh inner JSGlobalObject
    Handle<GlobalObject> object;
    {
      Handle<JSFunction> js_global_function;
      Handle<ObjectTemplateInfo> js_global_template;
      if (!global_template.IsEmpty()) {
        // Get prototype template of the global_template
        Handle<ObjectTemplateInfo> data =
            v8::Utils::OpenHandle(*global_template);
        Handle<FunctionTemplateInfo> global_constructor =
            Handle<FunctionTemplateInfo>(
                FunctionTemplateInfo::cast(data->constructor()));
        Handle<Object> proto_template(global_constructor->prototype_template());
        if (!proto_template->IsUndefined()) {
          js_global_template =
              Handle<ObjectTemplateInfo>::cast(proto_template);
        }
      }

      if (js_global_template.is_null()) {
        Handle<String> name = Handle<String>(Heap::empty_symbol());
        Handle<Code> code = Handle<Code>(Builtins::builtin(Builtins::Illegal));
        js_global_function =
            Factory::NewFunction(name, JS_GLOBAL_OBJECT_TYPE,
                                 JSGlobalObject::kSize, code, true);
        // Change the constructor property of the prototype of the
        // hidden global function to refer to the Object function.
        Handle<JSObject> prototype =
            Handle<JSObject>(
                JSObject::cast(js_global_function->instance_prototype()));
        SetProperty(prototype, Factory::constructor_symbol(),
                    Top::object_function(), NONE);
      } else {
        Handle<FunctionTemplateInfo> js_global_constructor(
            FunctionTemplateInfo::cast(js_global_template->constructor()));
        js_global_function =
            Factory::CreateApiFunction(js_global_constructor,
                                       Factory::InnerGlobalObject);
      }

      js_global_function->initial_map()->set_is_hidden_prototype();
      object = Factory::NewGlobalObject(js_global_function);
    }

    // Set the global context for the global object.
    object->set_global_context(*global_context());

    // Step 2: create or re-initialize the global proxy object.
    Handle<JSGlobalProxy> global_proxy;
    {
      Handle<JSFunction> global_proxy_function;
      if (global_template.IsEmpty()) {
        Handle<String> name = Handle<String>(Heap::empty_symbol());
        Handle<Code> code = Handle<Code>(Builtins::builtin(Builtins::Illegal));
        global_proxy_function =
            Factory::NewFunction(name, JS_GLOBAL_PROXY_TYPE,
                                 JSGlobalProxy::kSize, code, true);
      } else {
        Handle<ObjectTemplateInfo> data =
            v8::Utils::OpenHandle(*global_template);
        Handle<FunctionTemplateInfo> global_constructor(
                FunctionTemplateInfo::cast(data->constructor()));
        global_proxy_function =
            Factory::CreateApiFunction(global_constructor,
                                       Factory::OuterGlobalObject);
      }

      Handle<String> global_name = Factory::LookupAsciiSymbol("global");
      global_proxy_function->shared()->set_instance_class_name(*global_name);
      global_proxy_function->initial_map()->set_is_access_check_needed(true);

      // Set global_proxy.__proto__ to js_global after ConfigureGlobalObjects

      if (global_object.location() != NULL) {
        ASSERT(global_object->IsJSGlobalProxy());
        global_proxy =
            ReinitializeJSGlobalProxy(
                global_proxy_function,
                Handle<JSGlobalProxy>::cast(global_object));
      } else {
        global_proxy = Handle<JSGlobalProxy>::cast(
            Factory::NewJSObject(global_proxy_function, TENURED));
      }

      // Security setup: Set the security token of the global object to
      // its the inner global. This makes the security check between two
      // different contexts fail by default even in case of global
      // object reinitialization.
      object->set_global_receiver(*global_proxy);
      global_proxy->set_context(*global_context());
    }

    {  // --- G l o b a l   C o n t e x t ---
      // use the empty function as closure (no scope info)
      global_context()->set_closure(*empty_function);
      global_context()->set_fcontext(*global_context());
      global_context()->set_previous(NULL);

      // set extension and global object
      global_context()->set_extension(*object);
      global_context()->set_global(*object);
      global_context()->set_global_proxy(*global_proxy);
      // use inner global object as security token by default
      global_context()->set_security_token(*object);
    }

    Handle<JSObject> global = Handle<JSObject>(global_context()->global());
    SetProperty(global, object_name, Top::object_function(), DONT_ENUM);
  }

  Handle<JSObject> global = Handle<JSObject>(global_context()->global());

  // Install global Function object
  InstallFunction(global, "Function", JS_FUNCTION_TYPE, JSFunction::kSize,
                  empty_function, Builtins::Illegal, true);  // ECMA native.

  {  // --- A r r a y ---
    Handle<JSFunction> array_function =
        InstallFunction(global, "Array", JS_ARRAY_TYPE, JSArray::kSize,
                        Top::initial_object_prototype(), Builtins::ArrayCode,
                        true);
    array_function->shared()->set_construct_stub(
        Builtins::builtin(Builtins::ArrayConstructCode));
    array_function->shared()->DontAdaptArguments();

    // This seems a bit hackish, but we need to make sure Array.length
    // is 1.
    array_function->shared()->set_length(1);
    Handle<DescriptorArray> array_descriptors =
        Factory::CopyAppendProxyDescriptor(
            Factory::empty_descriptor_array(),
            Factory::length_symbol(),
            Factory::NewProxy(&Accessors::ArrayLength),
            static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE));

    // Cache the fast JavaScript array map
    global_context()->set_js_array_map(array_function->initial_map());
    global_context()->js_array_map()->set_instance_descriptors(
        *array_descriptors);
    // array_function is used internally. JS code creating array object should
    // search for the 'Array' property on the global object and use that one
    // as the constructor. 'Array' property on a global object can be
    // overwritten by JS code.
    global_context()->set_array_function(*array_function);
  }

  {  // --- N u m b e r ---
    Handle<JSFunction> number_fun =
        InstallFunction(global, "Number", JS_VALUE_TYPE, JSValue::kSize,
                        Top::initial_object_prototype(), Builtins::Illegal,
                        true);
    global_context()->set_number_function(*number_fun);
  }

  {  // --- B o o l e a n ---
    Handle<JSFunction> boolean_fun =
        InstallFunction(global, "Boolean", JS_VALUE_TYPE, JSValue::kSize,
                        Top::initial_object_prototype(), Builtins::Illegal,
                        true);
    global_context()->set_boolean_function(*boolean_fun);
  }

  {  // --- S t r i n g ---
    Handle<JSFunction> string_fun =
        InstallFunction(global, "String", JS_VALUE_TYPE, JSValue::kSize,
                        Top::initial_object_prototype(), Builtins::Illegal,
                        true);
    global_context()->set_string_function(*string_fun);
    // Add 'length' property to strings.
    Handle<DescriptorArray> string_descriptors =
        Factory::CopyAppendProxyDescriptor(
            Factory::empty_descriptor_array(),
            Factory::length_symbol(),
            Factory::NewProxy(&Accessors::StringLength),
            static_cast<PropertyAttributes>(DONT_ENUM |
                                            DONT_DELETE |
                                            READ_ONLY));

    Handle<Map> string_map =
        Handle<Map>(global_context()->string_function()->initial_map());
    string_map->set_instance_descriptors(*string_descriptors);
  }

  {  // --- D a t e ---
    // Builtin functions for Date.prototype.
    Handle<JSFunction> date_fun =
        InstallFunction(global, "Date", JS_VALUE_TYPE, JSValue::kSize,
                        Top::initial_object_prototype(), Builtins::Illegal,
                        true);

    global_context()->set_date_function(*date_fun);
  }


  {  // -- R e g E x p
    // Builtin functions for RegExp.prototype.
    Handle<JSFunction> regexp_fun =
        InstallFunction(global, "RegExp", JS_REGEXP_TYPE, JSRegExp::kSize,
                        Top::initial_object_prototype(), Builtins::Illegal,
                        true);

    global_context()->set_regexp_function(*regexp_fun);
  }

  {  // -- J S O N
    Handle<String> name = Factory::NewStringFromAscii(CStrVector("JSON"));
    Handle<JSFunction> cons = Factory::NewFunction(
        name,
        Factory::the_hole_value());
    cons->SetInstancePrototype(global_context()->initial_object_prototype());
    cons->SetInstanceClassName(*name);
    Handle<JSObject> json_object = Factory::NewJSObject(cons, TENURED);
    ASSERT(json_object->IsJSObject());
    SetProperty(global, name, json_object, DONT_ENUM);
    global_context()->set_json_object(*json_object);
  }

  {  // --- arguments_boilerplate_
    // Make sure we can recognize argument objects at runtime.
    // This is done by introducing an anonymous function with
    // class_name equals 'Arguments'.
    Handle<String> symbol = Factory::LookupAsciiSymbol("Arguments");
    Handle<Code> code = Handle<Code>(Builtins::builtin(Builtins::Illegal));
    Handle<JSObject> prototype =
        Handle<JSObject>(
            JSObject::cast(global_context()->object_function()->prototype()));

    Handle<JSFunction> function =
        Factory::NewFunctionWithPrototype(symbol,
                                          JS_OBJECT_TYPE,
                                          JSObject::kHeaderSize,
                                          prototype,
                                          code,
                                          false);
    ASSERT(!function->has_initial_map());
    function->shared()->set_instance_class_name(*symbol);
    function->shared()->set_expected_nof_properties(2);
    Handle<JSObject> result = Factory::NewJSObject(function);

    global_context()->set_arguments_boilerplate(*result);
    // Note: callee must be added as the first property and
    //       length must be added as the second property.
    SetProperty(result, Factory::callee_symbol(),
                Factory::undefined_value(),
                DONT_ENUM);
    SetProperty(result, Factory::length_symbol(),
                Factory::undefined_value(),
                DONT_ENUM);

#ifdef DEBUG
    LookupResult lookup;
    result->LocalLookup(Heap::callee_symbol(), &lookup);
    ASSERT(lookup.IsValid() && (lookup.type() == FIELD));
    ASSERT(lookup.GetFieldIndex() == Heap::arguments_callee_index);

    result->LocalLookup(Heap::length_symbol(), &lookup);
    ASSERT(lookup.IsValid() && (lookup.type() == FIELD));
    ASSERT(lookup.GetFieldIndex() == Heap::arguments_length_index);

    ASSERT(result->map()->inobject_properties() > Heap::arguments_callee_index);
    ASSERT(result->map()->inobject_properties() > Heap::arguments_length_index);

    // Check the state of the object.
    ASSERT(result->HasFastProperties());
    ASSERT(result->HasFastElements());
#endif
  }

  {  // --- context extension
    // Create a function for the context extension objects.
    Handle<Code> code = Handle<Code>(Builtins::builtin(Builtins::Illegal));
    Handle<JSFunction> context_extension_fun =
        Factory::NewFunction(Factory::empty_symbol(),
                             JS_CONTEXT_EXTENSION_OBJECT_TYPE,
                             JSObject::kHeaderSize,
                             code,
                             true);

    Handle<String> name = Factory::LookupAsciiSymbol("context_extension");
    context_extension_fun->shared()->set_instance_class_name(*name);
    global_context()->set_context_extension_function(*context_extension_fun);
  }


  {
    // Setup the call-as-function delegate.
    Handle<Code> code =
        Handle<Code>(Builtins::builtin(Builtins::HandleApiCallAsFunction));
    Handle<JSFunction> delegate =
        Factory::NewFunction(Factory::empty_symbol(), JS_OBJECT_TYPE,
                             JSObject::kHeaderSize, code, true);
    global_context()->set_call_as_function_delegate(*delegate);
    delegate->shared()->DontAdaptArguments();
  }

  {
    // Setup the call-as-constructor delegate.
    Handle<Code> code =
        Handle<Code>(Builtins::builtin(Builtins::HandleApiCallAsConstructor));
    Handle<JSFunction> delegate =
        Factory::NewFunction(Factory::empty_symbol(), JS_OBJECT_TYPE,
                             JSObject::kHeaderSize, code, true);
    global_context()->set_call_as_constructor_delegate(*delegate);
    delegate->shared()->DontAdaptArguments();
  }

  global_context()->set_special_function_table(Heap::empty_fixed_array());

  // Initialize the out of memory slot.
  global_context()->set_out_of_memory(Heap::false_value());

  // Initialize the data slot.
  global_context()->set_data(Heap::undefined_value());
}


bool Genesis::CompileBuiltin(int index) {
  Vector<const char> name = Natives::GetScriptName(index);
  Handle<String> source_code = Bootstrapper::NativesSourceLookup(index);
  return CompileNative(name, source_code);
}


bool Genesis::CompileNative(Vector<const char> name, Handle<String> source) {
  HandleScope scope;
#ifdef ENABLE_DEBUGGER_SUPPORT
  Debugger::set_compiling_natives(true);
#endif
  bool result =
      CompileScriptCached(name, source, &natives_cache, NULL, true);
  ASSERT(Top::has_pending_exception() != result);
  if (!result) Top::clear_pending_exception();
#ifdef ENABLE_DEBUGGER_SUPPORT
  Debugger::set_compiling_natives(false);
#endif
  return result;
}


bool Genesis::CompileScriptCached(Vector<const char> name,
                                  Handle<String> source,
                                  SourceCodeCache* cache,
                                  v8::Extension* extension,
                                  bool use_runtime_context) {
  HandleScope scope;
  Handle<JSFunction> boilerplate;

  // If we can't find the function in the cache, we compile a new
  // function and insert it into the cache.
  if (!cache->Lookup(name, &boilerplate)) {
    ASSERT(source->IsAsciiRepresentation());
    Handle<String> script_name = Factory::NewStringFromUtf8(name);
    boilerplate =
        Compiler::Compile(source, script_name, 0, 0, extension, NULL);
    if (boilerplate.is_null()) return false;
    cache->Add(name, boilerplate);
  }

  // Setup the function context. Conceptually, we should clone the
  // function before overwriting the context but since we're in a
  // single-threaded environment it is not strictly necessary.
  ASSERT(Top::context()->IsGlobalContext());
  Handle<Context> context =
      Handle<Context>(use_runtime_context
                      ? Top::context()->runtime_context()
                      : Top::context());
  Handle<JSFunction> fun =
      Factory::NewFunctionFromBoilerplate(boilerplate, context);

  // Call function using the either the runtime object or the global
  // object as the receiver. Provide no parameters.
  Handle<Object> receiver =
      Handle<Object>(use_runtime_context
                     ? Top::context()->builtins()
                     : Top::context()->global());
  bool has_pending_exception;
  Handle<Object> result =
      Execution::Call(fun, receiver, 0, NULL, &has_pending_exception);
  if (has_pending_exception) return false;
  return PendingFixups::Process(
      Handle<JSBuiltinsObject>(Top::context()->builtins()));
}


#define INSTALL_NATIVE(Type, name, var)                                  \
  Handle<String> var##_name = Factory::LookupAsciiSymbol(name);          \
  global_context()->set_##var(Type::cast(global_context()->              \
                                           builtins()->                  \
                                             GetProperty(*var##_name)));

void Genesis::InstallNativeFunctions() {
  HandleScope scope;
  INSTALL_NATIVE(JSFunction, "CreateDate", create_date_fun);
  INSTALL_NATIVE(JSFunction, "ToNumber", to_number_fun);
  INSTALL_NATIVE(JSFunction, "ToString", to_string_fun);
  INSTALL_NATIVE(JSFunction, "ToDetailString", to_detail_string_fun);
  INSTALL_NATIVE(JSFunction, "ToObject", to_object_fun);
  INSTALL_NATIVE(JSFunction, "ToInteger", to_integer_fun);
  INSTALL_NATIVE(JSFunction, "ToUint32", to_uint32_fun);
  INSTALL_NATIVE(JSFunction, "ToInt32", to_int32_fun);
  INSTALL_NATIVE(JSFunction, "ToBoolean", to_boolean_fun);
  INSTALL_NATIVE(JSFunction, "GlobalEval", global_eval_fun);
  INSTALL_NATIVE(JSFunction, "Instantiate", instantiate_fun);
  INSTALL_NATIVE(JSFunction, "ConfigureTemplateInstance",
                 configure_instance_fun);
  INSTALL_NATIVE(JSFunction, "MakeMessage", make_message_fun);
  INSTALL_NATIVE(JSFunction, "GetStackTraceLine", get_stack_trace_line_fun);
  INSTALL_NATIVE(JSObject, "functionCache", function_cache);
}

#undef INSTALL_NATIVE


bool Genesis::InstallNatives() {
  HandleScope scope;

  // Create a function for the builtins object. Allocate space for the
  // JavaScript builtins, a reference to the builtins object
  // (itself) and a reference to the global_context directly in the object.
  Handle<Code> code = Handle<Code>(Builtins::builtin(Builtins::Illegal));
  Handle<JSFunction> builtins_fun =
      Factory::NewFunction(Factory::empty_symbol(), JS_BUILTINS_OBJECT_TYPE,
                           JSBuiltinsObject::kSize, code, true);

  Handle<String> name = Factory::LookupAsciiSymbol("builtins");
  builtins_fun->shared()->set_instance_class_name(*name);

  // Allocate the builtins object.
  Handle<JSBuiltinsObject> builtins =
      Handle<JSBuiltinsObject>::cast(Factory::NewGlobalObject(builtins_fun));
  builtins->set_builtins(*builtins);
  builtins->set_global_context(*global_context());
  builtins->set_global_receiver(*builtins);

  // Setup the 'global' properties of the builtins object. The
  // 'global' property that refers to the global object is the only
  // way to get from code running in the builtins context to the
  // global object.
  static const PropertyAttributes attributes =
      static_cast<PropertyAttributes>(READ_ONLY | DONT_DELETE);
  SetProperty(builtins, Factory::LookupAsciiSymbol("global"),
              Handle<Object>(global_context()->global()), attributes);

  // Setup the reference from the global object to the builtins object.
  JSGlobalObject::cast(global_context()->global())->set_builtins(*builtins);

  // Create a bridge function that has context in the global context.
  Handle<JSFunction> bridge =
      Factory::NewFunction(Factory::empty_symbol(), Factory::undefined_value());
  ASSERT(bridge->context() == *Top::global_context());

  // Allocate the builtins context.
  Handle<Context> context =
    Factory::NewFunctionContext(Context::MIN_CONTEXT_SLOTS, bridge);
  context->set_global(*builtins);  // override builtins global object

  global_context()->set_runtime_context(*context);

  {  // -- S c r i p t
    // Builtin functions for Script.
    Handle<JSFunction> script_fun =
        InstallFunction(builtins, "Script", JS_VALUE_TYPE, JSValue::kSize,
                        Top::initial_object_prototype(), Builtins::Illegal,
                        false);
    Handle<JSObject> prototype =
        Factory::NewJSObject(Top::object_function(), TENURED);
    SetPrototype(script_fun, prototype);
    global_context()->set_script_function(*script_fun);

    // Add 'source' and 'data' property to scripts.
    PropertyAttributes common_attributes =
        static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE | READ_ONLY);
    Handle<Proxy> proxy_source = Factory::NewProxy(&Accessors::ScriptSource);
    Handle<DescriptorArray> script_descriptors =
        Factory::CopyAppendProxyDescriptor(
            Factory::empty_descriptor_array(),
            Factory::LookupAsciiSymbol("source"),
            proxy_source,
            common_attributes);
    Handle<Proxy> proxy_name = Factory::NewProxy(&Accessors::ScriptName);
    script_descriptors =
        Factory::CopyAppendProxyDescriptor(
            script_descriptors,
            Factory::LookupAsciiSymbol("name"),
            proxy_name,
            common_attributes);
    Handle<Proxy> proxy_id = Factory::NewProxy(&Accessors::ScriptId);
    script_descriptors =
        Factory::CopyAppendProxyDescriptor(
            script_descriptors,
            Factory::LookupAsciiSymbol("id"),
            proxy_id,
            common_attributes);
    Handle<Proxy> proxy_line_offset =
        Factory::NewProxy(&Accessors::ScriptLineOffset);
    script_descriptors =
        Factory::CopyAppendProxyDescriptor(
            script_descriptors,
            Factory::LookupAsciiSymbol("line_offset"),
            proxy_line_offset,
            common_attributes);
    Handle<Proxy> proxy_column_offset =
        Factory::NewProxy(&Accessors::ScriptColumnOffset);
    script_descriptors =
        Factory::CopyAppendProxyDescriptor(
            script_descriptors,
            Factory::LookupAsciiSymbol("column_offset"),
            proxy_column_offset,
            common_attributes);
    Handle<Proxy> proxy_data = Factory::NewProxy(&Accessors::ScriptData);
    script_descriptors =
        Factory::CopyAppendProxyDescriptor(
            script_descriptors,
            Factory::LookupAsciiSymbol("data"),
            proxy_data,
            common_attributes);
    Handle<Proxy> proxy_type = Factory::NewProxy(&Accessors::ScriptType);
    script_descriptors =
        Factory::CopyAppendProxyDescriptor(
            script_descriptors,
            Factory::LookupAsciiSymbol("type"),
            proxy_type,
            common_attributes);
    Handle<Proxy> proxy_compilation_type =
        Factory::NewProxy(&Accessors::ScriptCompilationType);
    script_descriptors =
        Factory::CopyAppendProxyDescriptor(
            script_descriptors,
            Factory::LookupAsciiSymbol("compilation_type"),
            proxy_compilation_type,
            common_attributes);
    Handle<Proxy> proxy_line_ends =
        Factory::NewProxy(&Accessors::ScriptLineEnds);
    script_descriptors =
        Factory::CopyAppendProxyDescriptor(
            script_descriptors,
            Factory::LookupAsciiSymbol("line_ends"),
            proxy_line_ends,
            common_attributes);
    Handle<Proxy> proxy_context_data =
        Factory::NewProxy(&Accessors::ScriptContextData);
    script_descriptors =
        Factory::CopyAppendProxyDescriptor(
            script_descriptors,
            Factory::LookupAsciiSymbol("context_data"),
            proxy_context_data,
            common_attributes);
    Handle<Proxy> proxy_eval_from_script =
        Factory::NewProxy(&Accessors::ScriptEvalFromScript);
    script_descriptors =
        Factory::CopyAppendProxyDescriptor(
            script_descriptors,
            Factory::LookupAsciiSymbol("eval_from_script"),
            proxy_eval_from_script,
            common_attributes);
    Handle<Proxy> proxy_eval_from_script_position =
        Factory::NewProxy(&Accessors::ScriptEvalFromScriptPosition);
    script_descriptors =
        Factory::CopyAppendProxyDescriptor(
            script_descriptors,
            Factory::LookupAsciiSymbol("eval_from_script_position"),
            proxy_eval_from_script_position,
            common_attributes);
    Handle<Proxy> proxy_eval_from_function_name =
        Factory::NewProxy(&Accessors::ScriptEvalFromFunctionName);
    script_descriptors =
        Factory::CopyAppendProxyDescriptor(
            script_descriptors,
            Factory::LookupAsciiSymbol("eval_from_function_name"),
            proxy_eval_from_function_name,
            common_attributes);

    Handle<Map> script_map = Handle<Map>(script_fun->initial_map());
    script_map->set_instance_descriptors(*script_descriptors);

    // Allocate the empty script.
    Handle<Script> script = Factory::NewScript(Factory::empty_string());
    script->set_type(Smi::FromInt(Script::TYPE_NATIVE));
    global_context()->set_empty_script(*script);
  }

  if (FLAG_natives_file == NULL) {
    // Without natives file, install default natives.
    for (int i = Natives::GetDelayCount();
         i < Natives::GetBuiltinsCount();
         i++) {
      if (!CompileBuiltin(i)) return false;
    }

    // Setup natives with lazy loading.
    SetupLazy(Handle<JSFunction>(global_context()->date_function()),
              Natives::GetIndex("date"),
              Top::global_context(),
              Handle<Context>(Top::context()->runtime_context()));
    SetupLazy(Handle<JSFunction>(global_context()->regexp_function()),
              Natives::GetIndex("regexp"),
              Top::global_context(),
              Handle<Context>(Top::context()->runtime_context()));
    SetupLazy(Handle<JSObject>(global_context()->json_object()),
              Natives::GetIndex("json"),
              Top::global_context(),
              Handle<Context>(Top::context()->runtime_context()));

  } else if (strlen(FLAG_natives_file) != 0) {
    // Otherwise install natives from natives file if file exists and
    // compiles.
    bool exists;
    Vector<const char> source = ReadFile(FLAG_natives_file, &exists);
    Handle<String> source_string = Factory::NewStringFromAscii(source);
    if (source.is_empty()) return false;
    bool result = CompileNative(CStrVector(FLAG_natives_file), source_string);
    if (!result) return false;

  } else {
    // Empty natives file name - do not install any natives.
    PrintF("Warning: Running without installed natives!\n");
    return true;
  }

  InstallNativeFunctions();

  // Install Function.prototype.call and apply.
  { Handle<String> key = Factory::function_class_symbol();
    Handle<JSFunction> function =
        Handle<JSFunction>::cast(GetProperty(Top::global(), key));
    Handle<JSObject> proto =
        Handle<JSObject>(JSObject::cast(function->instance_prototype()));

    // Install the call and the apply functions.
    Handle<JSFunction> call =
        InstallFunction(proto, "call", JS_OBJECT_TYPE, JSObject::kHeaderSize,
                        Factory::NewJSObject(Top::object_function(), TENURED),
                        Builtins::FunctionCall,
                        false);
    Handle<JSFunction> apply =
        InstallFunction(proto, "apply", JS_OBJECT_TYPE, JSObject::kHeaderSize,
                        Factory::NewJSObject(Top::object_function(), TENURED),
                        Builtins::FunctionApply,
                        false);

    // Make sure that Function.prototype.call appears to be compiled.
    // The code will never be called, but inline caching for call will
    // only work if it appears to be compiled.
    call->shared()->DontAdaptArguments();
    ASSERT(call->is_compiled());

    // Set the expected parameters for apply to 2; required by builtin.
    apply->shared()->set_formal_parameter_count(2);

    // Set the lengths for the functions to satisfy ECMA-262.
    call->shared()->set_length(1);
    apply->shared()->set_length(2);
  }

#ifdef DEBUG
  builtins->Verify();
#endif
  return true;
}


bool Genesis::InstallSpecialObjects() {
  HandleScope scope;
  Handle<JSGlobalObject> js_global(
      JSGlobalObject::cast(global_context()->global()));
  // Expose the natives in global if a name for it is specified.
  if (FLAG_expose_natives_as != NULL && strlen(FLAG_expose_natives_as) != 0) {
    Handle<String> natives_string =
        Factory::LookupAsciiSymbol(FLAG_expose_natives_as);
    SetProperty(js_global, natives_string,
                Handle<JSObject>(js_global->builtins()), DONT_ENUM);
  }

  Handle<Object> Error = GetProperty(js_global, "Error");
  if (Error->IsJSObject()) {
    Handle<String> name = Factory::LookupAsciiSymbol("stackTraceLimit");
    SetProperty(Handle<JSObject>::cast(Error),
                name,
                Handle<Smi>(Smi::FromInt(FLAG_stack_trace_limit)),
                NONE);
  }

#ifdef ENABLE_DEBUGGER_SUPPORT
  // Expose the debug global object in global if a name for it is specified.
  if (FLAG_expose_debug_as != NULL && strlen(FLAG_expose_debug_as) != 0) {
    // If loading fails we just bail out without installing the
    // debugger but without tanking the whole context.
    if (!Debug::Load())
      return true;
    // Set the security token for the debugger context to the same as
    // the shell global context to allow calling between these (otherwise
    // exposing debug global object doesn't make much sense).
    Debug::debug_context()->set_security_token(
        global_context()->security_token());

    Handle<String> debug_string =
        Factory::LookupAsciiSymbol(FLAG_expose_debug_as);
    SetProperty(js_global, debug_string,
        Handle<Object>(Debug::debug_context()->global_proxy()), DONT_ENUM);
  }
#endif

  return true;
}


bool Genesis::InstallExtensions(v8::ExtensionConfiguration* extensions) {
  // Clear coloring of extension list
  v8::RegisteredExtension* current = v8::RegisteredExtension::first_extension();
  while (current != NULL) {
    current->set_state(v8::UNVISITED);
    current = current->next();
  }
  // Install auto extensions
  current = v8::RegisteredExtension::first_extension();
  while (current != NULL) {
    if (current->extension()->auto_enable())
      InstallExtension(current);
    current = current->next();
  }

  if (FLAG_expose_gc) InstallExtension("v8/gc");

  if (extensions == NULL) return true;
  // Install required extensions
  int count = v8::ImplementationUtilities::GetNameCount(extensions);
  const char** names = v8::ImplementationUtilities::GetNames(extensions);
  for (int i = 0; i < count; i++) {
    if (!InstallExtension(names[i]))
      return false;
  }

  return true;
}


// Installs a named extension.  This methods is unoptimized and does
// not scale well if we want to support a large number of extensions.
bool Genesis::InstallExtension(const char* name) {
  v8::RegisteredExtension* current = v8::RegisteredExtension::first_extension();
  // Loop until we find the relevant extension
  while (current != NULL) {
    if (strcmp(name, current->extension()->name()) == 0) break;
    current = current->next();
  }
  // Didn't find the extension; fail.
  if (current == NULL) {
    v8::Utils::ReportApiFailure(
        "v8::Context::New()", "Cannot find required extension");
    return false;
  }
  return InstallExtension(current);
}


bool Genesis::InstallExtension(v8::RegisteredExtension* current) {
  HandleScope scope;

  if (current->state() == v8::INSTALLED) return true;
  // The current node has already been visited so there must be a
  // cycle in the dependency graph; fail.
  if (current->state() == v8::VISITED) {
    v8::Utils::ReportApiFailure(
        "v8::Context::New()", "Circular extension dependency");
    return false;
  }
  ASSERT(current->state() == v8::UNVISITED);
  current->set_state(v8::VISITED);
  v8::Extension* extension = current->extension();
  // Install the extension's dependencies
  for (int i = 0; i < extension->dependency_count(); i++) {
    if (!InstallExtension(extension->dependencies()[i])) return false;
  }
  Vector<const char> source = CStrVector(extension->source());
  Handle<String> source_code = Factory::NewStringFromAscii(source);
  bool result = CompileScriptCached(CStrVector(extension->name()),
                                    source_code,
                                    &extensions_cache, extension,
                                    false);
  ASSERT(Top::has_pending_exception() != result);
  if (!result) {
    Top::clear_pending_exception();
  }
  current->set_state(v8::INSTALLED);
  return result;
}


bool Genesis::ConfigureGlobalObjects(
    v8::Handle<v8::ObjectTemplate> global_proxy_template) {
  Handle<JSObject> global_proxy(
      JSObject::cast(global_context()->global_proxy()));
  Handle<JSObject> js_global(JSObject::cast(global_context()->global()));

  if (!global_proxy_template.IsEmpty()) {
    // Configure the global proxy object.
    Handle<ObjectTemplateInfo> proxy_data =
        v8::Utils::OpenHandle(*global_proxy_template);
    if (!ConfigureApiObject(global_proxy, proxy_data)) return false;

    // Configure the inner global object.
    Handle<FunctionTemplateInfo> proxy_constructor(
        FunctionTemplateInfo::cast(proxy_data->constructor()));
    if (!proxy_constructor->prototype_template()->IsUndefined()) {
      Handle<ObjectTemplateInfo> inner_data(
          ObjectTemplateInfo::cast(proxy_constructor->prototype_template()));
      if (!ConfigureApiObject(js_global, inner_data)) return false;
    }
  }

  SetObjectPrototype(global_proxy, js_global);
  return true;
}


bool Genesis::ConfigureApiObject(Handle<JSObject> object,
    Handle<ObjectTemplateInfo> object_template) {
  ASSERT(!object_template.is_null());
  ASSERT(object->IsInstanceOf(
      FunctionTemplateInfo::cast(object_template->constructor())));

  bool pending_exception = false;
  Handle<JSObject> obj =
      Execution::InstantiateObject(object_template, &pending_exception);
  if (pending_exception) {
    ASSERT(Top::has_pending_exception());
    Top::clear_pending_exception();
    return false;
  }
  TransferObject(obj, object);
  return true;
}


void Genesis::TransferNamedProperties(Handle<JSObject> from,
                                      Handle<JSObject> to) {
  if (from->HasFastProperties()) {
    Handle<DescriptorArray> descs =
        Handle<DescriptorArray>(from->map()->instance_descriptors());
    for (int i = 0; i < descs->number_of_descriptors(); i++) {
      PropertyDetails details = PropertyDetails(descs->GetDetails(i));
      switch (details.type()) {
        case FIELD: {
          HandleScope inner;
          Handle<String> key = Handle<String>(descs->GetKey(i));
          int index = descs->GetFieldIndex(i);
          Handle<Object> value = Handle<Object>(from->FastPropertyAt(index));
          SetProperty(to, key, value, details.attributes());
          break;
        }
        case CONSTANT_FUNCTION: {
          HandleScope inner;
          Handle<String> key = Handle<String>(descs->GetKey(i));
          Handle<JSFunction> fun =
              Handle<JSFunction>(descs->GetConstantFunction(i));
          SetProperty(to, key, fun, details.attributes());
          break;
        }
        case CALLBACKS: {
          LookupResult result;
          to->LocalLookup(descs->GetKey(i), &result);
          // If the property is already there we skip it
          if (result.IsValid()) continue;
          HandleScope inner;
          Handle<DescriptorArray> inst_descs =
              Handle<DescriptorArray>(to->map()->instance_descriptors());
          Handle<String> key = Handle<String>(descs->GetKey(i));
          Handle<Object> entry = Handle<Object>(descs->GetCallbacksObject(i));
          inst_descs = Factory::CopyAppendProxyDescriptor(inst_descs,
                                                          key,
                                                          entry,
                                                          details.attributes());
          to->map()->set_instance_descriptors(*inst_descs);
          break;
        }
        case MAP_TRANSITION:
        case CONSTANT_TRANSITION:
        case NULL_DESCRIPTOR:
          // Ignore non-properties.
          break;
        case NORMAL:
          // Do not occur since the from object has fast properties.
        case INTERCEPTOR:
          // No element in instance descriptors have interceptor type.
          UNREACHABLE();
          break;
      }
    }
  } else {
    Handle<StringDictionary> properties =
        Handle<StringDictionary>(from->property_dictionary());
    int capacity = properties->Capacity();
    for (int i = 0; i < capacity; i++) {
      Object* raw_key(properties->KeyAt(i));
      if (properties->IsKey(raw_key)) {
        ASSERT(raw_key->IsString());
        // If the property is already there we skip it.
        LookupResult result;
        to->LocalLookup(String::cast(raw_key), &result);
        if (result.IsValid()) continue;
        // Set the property.
        Handle<String> key = Handle<String>(String::cast(raw_key));
        Handle<Object> value = Handle<Object>(properties->ValueAt(i));
        if (value->IsJSGlobalPropertyCell()) {
          value = Handle<Object>(JSGlobalPropertyCell::cast(*value)->value());
        }
        PropertyDetails details = properties->DetailsAt(i);
        SetProperty(to, key, value, details.attributes());
      }
    }
  }
}


void Genesis::TransferIndexedProperties(Handle<JSObject> from,
                                        Handle<JSObject> to) {
  // Cloning the elements array is sufficient.
  Handle<FixedArray> from_elements =
      Handle<FixedArray>(FixedArray::cast(from->elements()));
  Handle<FixedArray> to_elements = Factory::CopyFixedArray(from_elements);
  to->set_elements(*to_elements);
}


void Genesis::TransferObject(Handle<JSObject> from, Handle<JSObject> to) {
  HandleScope outer;

  ASSERT(!from->IsJSArray());
  ASSERT(!to->IsJSArray());

  TransferNamedProperties(from, to);
  TransferIndexedProperties(from, to);

  // Transfer the prototype (new map is needed).
  Handle<Map> old_to_map = Handle<Map>(to->map());
  Handle<Map> new_to_map = Factory::CopyMapDropTransitions(old_to_map);
  new_to_map->set_prototype(from->map()->prototype());
  to->set_map(*new_to_map);
}


void Genesis::MakeFunctionInstancePrototypeWritable() {
  // Make a new function map so all future functions
  // will have settable and enumerable prototype properties.
  HandleScope scope;

  Handle<DescriptorArray> function_map_descriptors =
      ComputeFunctionInstanceDescriptor(false);
  Handle<Map> fm = Factory::CopyMapDropDescriptors(Top::function_map());
  fm->set_instance_descriptors(*function_map_descriptors);
  Top::context()->global_context()->set_function_map(*fm);
}


void Genesis::AddSpecialFunction(Handle<JSObject> prototype,
                                 const char* name,
                                 Handle<Code> code) {
  Handle<String> key = Factory::LookupAsciiSymbol(name);
  Handle<Object> value = Handle<Object>(prototype->GetProperty(*key));
  if (value->IsJSFunction()) {
    Handle<JSFunction> optimized = Factory::NewFunction(key,
                                                        JS_OBJECT_TYPE,
                                                        JSObject::kHeaderSize,
                                                        code,
                                                        false);
    optimized->shared()->DontAdaptArguments();
    int len = global_context()->special_function_table()->length();
    Handle<FixedArray> new_array = Factory::NewFixedArray(len + 3);
    for (int index = 0; index < len; index++) {
      new_array->set(index,
                     global_context()->special_function_table()->get(index));
    }
    new_array->set(len+0, *prototype);
    new_array->set(len+1, *value);
    new_array->set(len+2, *optimized);
    global_context()->set_special_function_table(*new_array);
  }
}


void Genesis::BuildSpecialFunctionTable() {
  HandleScope scope;
  Handle<JSObject> global = Handle<JSObject>(global_context()->global());
  // Add special versions for Array.prototype.pop and push.
  Handle<JSFunction> function =
      Handle<JSFunction>(
          JSFunction::cast(global->GetProperty(Heap::Array_symbol())));
  Handle<JSObject> visible_prototype =
      Handle<JSObject>(JSObject::cast(function->prototype()));
  // Remember to put push and pop on the hidden prototype if it's there.
  Handle<JSObject> push_and_pop_prototype;
  Handle<Object> superproto(visible_prototype->GetPrototype());
  if (superproto->IsJSObject() &&
      JSObject::cast(*superproto)->map()->is_hidden_prototype()) {
    push_and_pop_prototype = Handle<JSObject>::cast(superproto);
  } else {
    push_and_pop_prototype = visible_prototype;
  }
  AddSpecialFunction(push_and_pop_prototype, "pop",
                     Handle<Code>(Builtins::builtin(Builtins::ArrayPop)));
  AddSpecialFunction(push_and_pop_prototype, "push",
                     Handle<Code>(Builtins::builtin(Builtins::ArrayPush)));
}


Genesis::Genesis(Handle<Object> global_object,
                 v8::Handle<v8::ObjectTemplate> global_template,
                 v8::ExtensionConfiguration* extensions) {
  // Link this genesis object into the stacked genesis chain. This
  // must be done before any early exits because the destructor
  // will always do unlinking.
  previous_ = current_;
  current_  = this;
  result_ = Handle<Context>::null();

  // If V8 isn't running and cannot be initialized, just return.
  if (!V8::IsRunning() && !V8::Initialize(NULL)) return;

  // Before creating the roots we must save the context and restore it
  // on all function exits.
  HandleScope scope;
  SaveContext context;

  CreateRoots(global_template, global_object);

  if (!InstallNatives()) return;

  MakeFunctionInstancePrototypeWritable();
  BuildSpecialFunctionTable();

  if (!ConfigureGlobalObjects(global_template)) return;

  if (!InstallExtensions(extensions)) return;

  if (!InstallSpecialObjects()) return;

  result_ = global_context_;
}


// Support for thread preemption.

// Reserve space for statics needing saving and restoring.
int Bootstrapper::ArchiveSpacePerThread() {
  return Genesis::ArchiveSpacePerThread();
}


// Archive statics that are thread local.
char* Bootstrapper::ArchiveState(char* to) {
  return Genesis::ArchiveState(to);
}


// Restore statics that are thread local.
char* Bootstrapper::RestoreState(char* from) {
  return Genesis::RestoreState(from);
}


// Called when the top-level V8 mutex is destroyed.
void Bootstrapper::FreeThreadResources() {
  ASSERT(Genesis::current() == NULL);
}


// Reserve space for statics needing saving and restoring.
int Genesis::ArchiveSpacePerThread() {
  return sizeof(current_);
}


// Archive statics that are thread local.
char* Genesis::ArchiveState(char* to) {
  *reinterpret_cast<Genesis**>(to) = current_;
  current_ = NULL;
  return to + sizeof(current_);
}


// Restore statics that are thread local.
char* Genesis::RestoreState(char* from) {
  current_ = *reinterpret_cast<Genesis**>(from);
  return from + sizeof(current_);
}

} }  // namespace v8::internal
