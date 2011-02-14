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

#ifndef V8_EXECUTION_H_
#define V8_EXECUTION_H_

namespace v8 {
namespace internal {


// Flag used to set the interrupt causes.
enum InterruptFlag {
  INTERRUPT = 1 << 0,
  DEBUGBREAK = 1 << 1,
  DEBUGCOMMAND = 1 << 2,
  PREEMPT = 1 << 3,
  TERMINATE = 1 << 4,
  RUNTIME_PROFILER_TICK = 1 << 5
};

class Execution : public AllStatic {
 public:
  // Call a function, the caller supplies a receiver and an array
  // of arguments. Arguments are Object* type. After function returns,
  // pointers in 'args' might be invalid.
  //
  // *pending_exception tells whether the invoke resulted in
  // a pending exception.
  //
  static Handle<Object> Call(Handle<JSFunction> func,
                             Handle<Object> receiver,
                             int argc,
                             Object*** args,
                             bool* pending_exception);

  // Construct object from function, the caller supplies an array of
  // arguments. Arguments are Object* type. After function returns,
  // pointers in 'args' might be invalid.
  //
  // *pending_exception tells whether the invoke resulted in
  // a pending exception.
  //
  static Handle<Object> New(Handle<JSFunction> func,
                            int argc,
                            Object*** args,
                            bool* pending_exception);

  // Call a function, just like Call(), but make sure to silently catch
  // any thrown exceptions. The return value is either the result of
  // calling the function (if caught exception is false) or the exception
  // that occurred (if caught exception is true).
  static Handle<Object> TryCall(Handle<JSFunction> func,
                                Handle<Object> receiver,
                                int argc,
                                Object*** args,
                                bool* caught_exception);

  // ECMA-262 9.2
  static Handle<Object> ToBoolean(Handle<Object> obj);

  // ECMA-262 9.3
  static Handle<Object> ToNumber(Handle<Object> obj, bool* exc);

  // ECMA-262 9.4
  static Handle<Object> ToInteger(Handle<Object> obj, bool* exc);

  // ECMA-262 9.5
  static Handle<Object> ToInt32(Handle<Object> obj, bool* exc);

  // ECMA-262 9.6
  static Handle<Object> ToUint32(Handle<Object> obj, bool* exc);

  // ECMA-262 9.8
  static Handle<Object> ToString(Handle<Object> obj, bool* exc);

  // ECMA-262 9.8
  static Handle<Object> ToDetailString(Handle<Object> obj, bool* exc);

  // ECMA-262 9.9
  static Handle<Object> ToObject(Handle<Object> obj, bool* exc);

  // Create a new date object from 'time'.
  static Handle<Object> NewDate(double time, bool* exc);

  // Create a new regular expression object from 'pattern' and 'flags'.
  static Handle<JSRegExp> NewJSRegExp(Handle<String> pattern,
                                      Handle<String> flags,
                                      bool* exc);

  // Used to implement [] notation on strings (calls JS code)
  static Handle<Object> CharAt(Handle<String> str, uint32_t index);

  static Handle<Object> GetFunctionFor();
  static Handle<JSFunction> InstantiateFunction(
      Handle<FunctionTemplateInfo> data, bool* exc);
  static Handle<JSObject> InstantiateObject(Handle<ObjectTemplateInfo> data,
                                            bool* exc);
  static void ConfigureInstance(Handle<Object> instance,
                                Handle<Object> data,
                                bool* exc);
  static Handle<String> GetStackTraceLine(Handle<Object> recv,
                                          Handle<JSFunction> fun,
                                          Handle<Object> pos,
                                          Handle<Object> is_global);
#ifdef ENABLE_DEBUGGER_SUPPORT
  static Object* DebugBreakHelper();
  static void ProcessDebugMesssages(bool debug_command_only);
#endif

  // If the stack guard is triggered, but it is not an actual
  // stack overflow, then handle the interruption accordingly.
  MUST_USE_RESULT static MaybeObject* HandleStackGuardInterrupt();

  // Get a function delegate (or undefined) for the given non-function
  // object. Used for support calling objects as functions.
  static Handle<Object> GetFunctionDelegate(Handle<Object> object);

  // Get a function delegate (or undefined) for the given non-function
  // object. Used for support calling objects as constructors.
  static Handle<Object> GetConstructorDelegate(Handle<Object> object);
};


class ExecutionAccess;


// StackGuard contains the handling of the limits that are used to limit the
// number of nested invocations of JavaScript and the stack size used in each
// invocation.
class StackGuard : public AllStatic {
 public:
  // Pass the address beyond which the stack should not grow.  The stack
  // is assumed to grow downwards.
  static void SetStackLimit(uintptr_t limit);

  // Threading support.
  static char* ArchiveStackGuard(char* to);
  static char* RestoreStackGuard(char* from);
  static int ArchiveSpacePerThread();
  static void FreeThreadResources();
  // Sets up the default stack guard for this thread if it has not
  // already been set up.
  static void InitThread(const ExecutionAccess& lock);
  // Clears the stack guard for this thread so it does not look as if
  // it has been set up.
  static void ClearThread(const ExecutionAccess& lock);

  static bool IsStackOverflow();
  static bool IsPreempted();
  static void Preempt();
  static bool IsInterrupted();
  static void Interrupt();
  static bool IsTerminateExecution();
  static void TerminateExecution();
  static bool IsRuntimeProfilerTick();
  static void RequestRuntimeProfilerTick();
#ifdef ENABLE_DEBUGGER_SUPPORT
  static bool IsDebugBreak();
  static void DebugBreak();
  static bool IsDebugCommand();
  static void DebugCommand();
#endif
  static void Continue(InterruptFlag after_what);

  // This provides an asynchronous read of the stack limits for the current
  // thread.  There are no locks protecting this, but it is assumed that you
  // have the global V8 lock if you are using multiple V8 threads.
  static uintptr_t climit() {
    return thread_local_.climit_;
  }
  static uintptr_t real_climit() {
    return thread_local_.real_climit_;
  }
  static uintptr_t jslimit() {
    return thread_local_.jslimit_;
  }
  static uintptr_t real_jslimit() {
    return thread_local_.real_jslimit_;
  }
  static Address address_of_jslimit() {
    return reinterpret_cast<Address>(&thread_local_.jslimit_);
  }
  static Address address_of_real_jslimit() {
    return reinterpret_cast<Address>(&thread_local_.real_jslimit_);
  }

 private:
  // You should hold the ExecutionAccess lock when calling this method.
  static bool has_pending_interrupts(const ExecutionAccess& lock) {
    // Sanity check: We shouldn't be asking about pending interrupts
    // unless we're not postponing them anymore.
    ASSERT(!should_postpone_interrupts(lock));
    return thread_local_.interrupt_flags_ != 0;
  }

  // You should hold the ExecutionAccess lock when calling this method.
  static bool should_postpone_interrupts(const ExecutionAccess& lock) {
    return thread_local_.postpone_interrupts_nesting_ > 0;
  }

  // You should hold the ExecutionAccess lock when calling this method.
  static void set_interrupt_limits(const ExecutionAccess& lock) {
    // Ignore attempts to interrupt when interrupts are postponed.
    if (should_postpone_interrupts(lock)) return;
    thread_local_.jslimit_ = kInterruptLimit;
    thread_local_.climit_ = kInterruptLimit;
    Heap::SetStackLimits();
  }

  // Reset limits to actual values. For example after handling interrupt.
  // You should hold the ExecutionAccess lock when calling this method.
  static void reset_limits(const ExecutionAccess& lock) {
    thread_local_.jslimit_ = thread_local_.real_jslimit_;
    thread_local_.climit_ = thread_local_.real_climit_;
    Heap::SetStackLimits();
  }

  // Enable or disable interrupts.
  static void EnableInterrupts();
  static void DisableInterrupts();

#ifdef V8_TARGET_ARCH_X64
  static const uintptr_t kInterruptLimit = V8_UINT64_C(0xfffffffffffffffe);
  static const uintptr_t kIllegalLimit = V8_UINT64_C(0xfffffffffffffff8);
#else
  static const uintptr_t kInterruptLimit = 0xfffffffe;
  static const uintptr_t kIllegalLimit = 0xfffffff8;
#endif

  class ThreadLocal {
   public:
    ThreadLocal() { Clear(); }
    // You should hold the ExecutionAccess lock when you call Initialize or
    // Clear.
    void Initialize();
    void Clear();

    // The stack limit is split into a JavaScript and a C++ stack limit. These
    // two are the same except when running on a simulator where the C++ and
    // JavaScript stacks are separate. Each of the two stack limits have two
    // values. The one eith the real_ prefix is the actual stack limit
    // set for the VM. The one without the real_ prefix has the same value as
    // the actual stack limit except when there is an interruption (e.g. debug
    // break or preemption) in which case it is lowered to make stack checks
    // fail. Both the generated code and the runtime system check against the
    // one without the real_ prefix.
    uintptr_t real_jslimit_;  // Actual JavaScript stack limit set for the VM.
    uintptr_t jslimit_;
    uintptr_t real_climit_;  // Actual C++ stack limit set for the VM.
    uintptr_t climit_;

    int nesting_;
    int postpone_interrupts_nesting_;
    int interrupt_flags_;
  };

  static ThreadLocal thread_local_;

  friend class StackLimitCheck;
  friend class PostponeInterruptsScope;
};


// Support for checking for stack-overflows in C++ code.
class StackLimitCheck BASE_EMBEDDED {
 public:
  bool HasOverflowed() const {
    // Stack has overflowed in C++ code only if stack pointer exceeds the C++
    // stack guard and the limits are not set to interrupt values.
    // TODO(214): Stack overflows are ignored if a interrupt is pending. This
    // code should probably always use the initial C++ limit.
    return (reinterpret_cast<uintptr_t>(this) < StackGuard::climit()) &&
           StackGuard::IsStackOverflow();
  }
};


// Support for temporarily postponing interrupts. When the outermost
// postpone scope is left the interrupts will be re-enabled and any
// interrupts that occurred while in the scope will be taken into
// account.
class PostponeInterruptsScope BASE_EMBEDDED {
 public:
  PostponeInterruptsScope() {
    StackGuard::thread_local_.postpone_interrupts_nesting_++;
    StackGuard::DisableInterrupts();
  }

  ~PostponeInterruptsScope() {
    if (--StackGuard::thread_local_.postpone_interrupts_nesting_ == 0) {
      StackGuard::EnableInterrupts();
    }
  }
};

} }  // namespace v8::internal

#endif  // V8_EXECUTION_H_
