// Copyright 2006-2009 the V8 project authors. All rights reserved.
//
// Tests of profiler-related functions from log.h

#ifdef ENABLE_LOGGING_AND_PROFILING

#include <stdlib.h>

#include "v8.h"

#include "codegen.h"
#include "log.h"
#include "top.h"
#include "cctest.h"
#include "disassembler.h"
#include "register-allocator-inl.h"

using v8::Function;
using v8::Local;
using v8::Object;
using v8::Script;
using v8::String;
using v8::Value;

using v8::internal::byte;
using v8::internal::Address;
using v8::internal::Handle;
using v8::internal::JSFunction;
using v8::internal::StackTracer;
using v8::internal::TickSample;
using v8::internal::Top;

namespace i = v8::internal;


static v8::Persistent<v8::Context> env;


static struct {
  TickSample* sample;
} trace_env = { NULL };


static void InitTraceEnv(TickSample* sample) {
  trace_env.sample = sample;
}


static void DoTrace(Address fp) {
  trace_env.sample->fp = fp;
  // sp is only used to define stack high bound
  trace_env.sample->sp =
      reinterpret_cast<Address>(trace_env.sample) - 10240;
  StackTracer::Trace(trace_env.sample);
}


// Hide c_entry_fp to emulate situation when sampling is done while
// pure JS code is being executed
static void DoTraceHideCEntryFPAddress(Address fp) {
  v8::internal::Address saved_c_frame_fp = *(Top::c_entry_fp_address());
  CHECK(saved_c_frame_fp);
  *(Top::c_entry_fp_address()) = 0;
  DoTrace(fp);
  *(Top::c_entry_fp_address()) = saved_c_frame_fp;
}


// --- T r a c e   E x t e n s i o n ---

class TraceExtension : public v8::Extension {
 public:
  TraceExtension() : v8::Extension("v8/trace", kSource) { }
  virtual v8::Handle<v8::FunctionTemplate> GetNativeFunction(
      v8::Handle<String> name);
  static v8::Handle<v8::Value> Trace(const v8::Arguments& args);
  static v8::Handle<v8::Value> JSTrace(const v8::Arguments& args);
  static v8::Handle<v8::Value> JSEntrySP(const v8::Arguments& args);
  static v8::Handle<v8::Value> JSEntrySPLevel2(const v8::Arguments& args);
 private:
  static Address GetFP(const v8::Arguments& args);
  static const char* kSource;
};


const char* TraceExtension::kSource =
    "native function trace();"
    "native function js_trace();"
    "native function js_entry_sp();"
    "native function js_entry_sp_level2();";

v8::Handle<v8::FunctionTemplate> TraceExtension::GetNativeFunction(
    v8::Handle<String> name) {
  if (name->Equals(String::New("trace"))) {
    return v8::FunctionTemplate::New(TraceExtension::Trace);
  } else if (name->Equals(String::New("js_trace"))) {
    return v8::FunctionTemplate::New(TraceExtension::JSTrace);
  } else if (name->Equals(String::New("js_entry_sp"))) {
    return v8::FunctionTemplate::New(TraceExtension::JSEntrySP);
  } else if (name->Equals(String::New("js_entry_sp_level2"))) {
    return v8::FunctionTemplate::New(TraceExtension::JSEntrySPLevel2);
  } else {
    CHECK(false);
    return v8::Handle<v8::FunctionTemplate>();
  }
}


Address TraceExtension::GetFP(const v8::Arguments& args) {
  CHECK_EQ(1, args.Length());
  // CodeGenerator::GenerateGetFramePointer pushes EBP / RBP value
  // on stack. In 64-bit mode we can't use Smi operations code because
  // they check that value is within Smi bounds.
  Address fp = *reinterpret_cast<Address*>(*args[0]);
  printf("Trace: %p\n", fp);
  return fp;
}


v8::Handle<v8::Value> TraceExtension::Trace(const v8::Arguments& args) {
  DoTrace(GetFP(args));
  return v8::Undefined();
}


v8::Handle<v8::Value> TraceExtension::JSTrace(const v8::Arguments& args) {
  DoTraceHideCEntryFPAddress(GetFP(args));
  return v8::Undefined();
}


static Address GetJsEntrySp() {
  CHECK_NE(NULL, Top::GetCurrentThread());
  return Top::js_entry_sp(Top::GetCurrentThread());
}


v8::Handle<v8::Value> TraceExtension::JSEntrySP(const v8::Arguments& args) {
  CHECK_NE(0, GetJsEntrySp());
  return v8::Undefined();
}


v8::Handle<v8::Value> TraceExtension::JSEntrySPLevel2(
    const v8::Arguments& args) {
  v8::HandleScope scope;
  const Address js_entry_sp = GetJsEntrySp();
  CHECK_NE(0, js_entry_sp);
  CompileRun("js_entry_sp();");
  CHECK_EQ(js_entry_sp, GetJsEntrySp());
  return v8::Undefined();
}


static TraceExtension kTraceExtension;
v8::DeclareExtension kTraceExtensionDeclaration(&kTraceExtension);


static void InitializeVM() {
  if (env.IsEmpty()) {
    v8::HandleScope scope;
    const char* extensions[] = { "v8/trace" };
    v8::ExtensionConfiguration config(1, extensions);
    env = v8::Context::New(&config);
  }
  v8::HandleScope scope;
  env->Enter();
}


static Handle<JSFunction> CompileFunction(const char* source) {
  Handle<JSFunction> result(JSFunction::cast(
      *v8::Utils::OpenHandle(*Script::Compile(String::New(source)))));
  return result;
}


static Local<Value> GetGlobalProperty(const char* name) {
  return env->Global()->Get(String::New(name));
}


static Handle<JSFunction> GetGlobalJSFunction(const char* name) {
  Handle<JSFunction> result(JSFunction::cast(
      *v8::Utils::OpenHandle(*GetGlobalProperty(name))));
  return result;
}


static void CheckObjectIsJSFunction(const char* func_name,
                                    Address addr) {
  i::Object* obj = reinterpret_cast<i::Object*>(addr);
  CHECK(obj->IsJSFunction());
  CHECK(JSFunction::cast(obj)->shared()->name()->IsString());
  i::SmartPointer<char> found_name =
      i::String::cast(
          JSFunction::cast(
              obj)->shared()->name())->ToCString();
  CHECK_EQ(func_name, *found_name);
}


static void SetGlobalProperty(const char* name, Local<Value> value) {
  env->Global()->Set(String::New(name), value);
}


static Handle<v8::internal::String> NewString(const char* s) {
  return i::Factory::NewStringFromAscii(i::CStrVector(s));
}


namespace v8 {
namespace internal {

class CodeGeneratorPatcher {
 public:
  CodeGeneratorPatcher() {
    CodeGenerator::InlineRuntimeLUT genGetFramePointer =
        {&CodeGenerator::GenerateGetFramePointer, "_GetFramePointer", 0};
    // _RandomHeapNumber is just used as a dummy function that has zero
    // arguments, the same as the _GetFramePointer function we actually patch
    // in.
    bool result = CodeGenerator::PatchInlineRuntimeEntry(
        NewString("_RandomHeapNumber"),
        genGetFramePointer, &oldInlineEntry);
    CHECK(result);
  }

  ~CodeGeneratorPatcher() {
    CHECK(CodeGenerator::PatchInlineRuntimeEntry(
        NewString("_GetFramePointer"),
        oldInlineEntry, NULL));
  }

 private:
  CodeGenerator::InlineRuntimeLUT oldInlineEntry;
};

} }  // namespace v8::internal


// Creates a global function named 'func_name' that calls the tracing
// function 'trace_func_name' with an actual EBP register value,
// shifted right to be presented as Smi.
static void CreateTraceCallerFunction(const char* func_name,
                                      const char* trace_func_name) {
  i::EmbeddedVector<char, 256> trace_call_buf;
  i::OS::SNPrintF(trace_call_buf, "%s(%%_GetFramePointer());", trace_func_name);

  // Compile the script.
  i::CodeGeneratorPatcher patcher;
  bool allow_natives_syntax = i::FLAG_allow_natives_syntax;
  i::FLAG_allow_natives_syntax = true;
  Handle<JSFunction> func = CompileFunction(trace_call_buf.start());
  CHECK(!func.is_null());
  i::FLAG_allow_natives_syntax = allow_natives_syntax;
  func->shared()->set_name(*NewString(func_name));

#ifdef DEBUG
  v8::internal::Code* func_code = func->code();
  CHECK(func_code->IsCode());
  func_code->Print();
#endif

  SetGlobalProperty(func_name, v8::ToApi<Value>(func));
  CHECK_EQ(*func, *GetGlobalJSFunction(func_name));
}


// This test verifies that stack tracing works when called during
// execution of a native function called from JS code. In this case,
// StackTracer uses Top::c_entry_fp as a starting point for stack
// walking.
TEST(CFromJSStackTrace) {
  // TODO(711) The hack of replacing the inline runtime function
  // RandomHeapNumber with GetFrameNumber does not work with the way the full
  // compiler generates inline runtime calls.
  i::FLAG_always_full_compiler = false;

  TickSample sample;
  InitTraceEnv(&sample);

  InitializeVM();
  v8::HandleScope scope;
  // Create global function JSFuncDoTrace which calls
  // extension function trace() with the current frame pointer value.
  CreateTraceCallerFunction("JSFuncDoTrace", "trace");
  Local<Value> result = CompileRun(
      "function JSTrace() {"
      "         JSFuncDoTrace();"
      "};\n"
      "JSTrace();\n"
      "true;");
  CHECK(!result.IsEmpty());
  // When stack tracer is invoked, the stack should look as follows:
  // script [JS]
  //   JSTrace() [JS]
  //     JSFuncDoTrace() [JS] [captures EBP value and encodes it as Smi]
  //       trace(EBP encoded as Smi) [native (extension)]
  //         DoTrace(EBP) [native]
  //           StackTracer::Trace
  CHECK_GT(sample.frames_count, 1);
  // Stack tracing will start from the first JS function, i.e. "JSFuncDoTrace"
  CheckObjectIsJSFunction("JSFuncDoTrace", sample.stack[0]);
  CheckObjectIsJSFunction("JSTrace", sample.stack[1]);
}


// This test verifies that stack tracing works when called during
// execution of JS code. However, as calling StackTracer requires
// entering native code, we can only emulate pure JS by erasing
// Top::c_entry_fp value. In this case, StackTracer uses passed frame
// pointer value as a starting point for stack walking.
TEST(PureJSStackTrace) {
  // TODO(711) The hack of replacing the inline runtime function
  // RandomHeapNumber with GetFrameNumber does not work with the way the full
  // compiler generates inline runtime calls.
  i::FLAG_always_full_compiler = false;

  TickSample sample;
  InitTraceEnv(&sample);

  InitializeVM();
  v8::HandleScope scope;
  // Create global function JSFuncDoTrace which calls
  // extension function js_trace() with the current frame pointer value.
  CreateTraceCallerFunction("JSFuncDoTrace", "js_trace");
  Local<Value> result = CompileRun(
      "function JSTrace() {"
      "         JSFuncDoTrace();"
      "};\n"
      "function OuterJSTrace() {"
      "         JSTrace();"
      "};\n"
      "OuterJSTrace();\n"
      "true;");
  CHECK(!result.IsEmpty());
  // When stack tracer is invoked, the stack should look as follows:
  // script [JS]
  //   OuterJSTrace() [JS]
  //     JSTrace() [JS]
  //       JSFuncDoTrace() [JS] [captures EBP value and encodes it as Smi]
  //         js_trace(EBP encoded as Smi) [native (extension)]
  //           DoTraceHideCEntryFPAddress(EBP) [native]
  //             StackTracer::Trace
  //
  // The last JS function called. It is only visible through
  // sample.function, as its return address is above captured EBP value.
  CHECK_EQ(GetGlobalJSFunction("JSFuncDoTrace")->address(),
           sample.function);
  CHECK_GT(sample.frames_count, 1);
  // Stack sampling will start from the caller of JSFuncDoTrace, i.e. "JSTrace"
  CheckObjectIsJSFunction("JSTrace", sample.stack[0]);
  CheckObjectIsJSFunction("OuterJSTrace", sample.stack[1]);
}


static void CFuncDoTrace(byte dummy_parameter) {
  Address fp;
#ifdef __GNUC__
  fp = reinterpret_cast<Address>(__builtin_frame_address(0));
#elif defined _MSC_VER
  // Approximate a frame pointer address. We compile without base pointers,
  // so we can't trust ebp/rbp.
  fp = &dummy_parameter - 2 * sizeof(void*);  // NOLINT
#else
#error Unexpected platform.
#endif
  DoTrace(fp);
}


static int CFunc(int depth) {
  if (depth <= 0) {
    CFuncDoTrace(0);
    return 0;
  } else {
    return CFunc(depth - 1) + 1;
  }
}


// This test verifies that stack tracing doesn't crash when called on
// pure native code. StackTracer only unrolls JS code, so we can't
// get any meaningful info here.
TEST(PureCStackTrace) {
  TickSample sample;
  InitTraceEnv(&sample);
  // Check that sampler doesn't crash
  CHECK_EQ(10, CFunc(10));
}


TEST(JsEntrySp) {
  InitializeVM();
  v8::HandleScope scope;
  CHECK_EQ(0, GetJsEntrySp());
  CompileRun("a = 1; b = a + 1;");
  CHECK_EQ(0, GetJsEntrySp());
  CompileRun("js_entry_sp();");
  CHECK_EQ(0, GetJsEntrySp());
  CompileRun("js_entry_sp_level2();");
  CHECK_EQ(0, GetJsEntrySp());
}

#endif  // ENABLE_LOGGING_AND_PROFILING
