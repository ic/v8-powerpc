// Copyright 2006-2009 the V8 project authors. All rights reserved.
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

// Platform specific code for OpenBSD goes here. For the POSIX comaptible parts
// the implementation is in platform-posix.cc.

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <stdlib.h>

#include <sys/types.h>  // mmap & munmap
#include <sys/mman.h>   // mmap & munmap
#include <sys/stat.h>   // open
#include <sys/fcntl.h>  // open
#include <unistd.h>     // getpagesize
#include <execinfo.h>   // backtrace, backtrace_symbols
#include <strings.h>    // index
#include <errno.h>
#include <stdarg.h>
#include <limits.h>

#undef MAP_TYPE

#include "v8.h"

#include "platform.h"
#include "vm-state-inl.h"


namespace v8 {
namespace internal {

// 0 is never a valid thread id on OpenBSD since tids and pids share a
// name space and pid 0 is used to kill the group (see man 2 kill).
static const pthread_t kNoThread = (pthread_t) 0;


double ceiling(double x) {
    // Correct as on OS X
    if (-1.0 < x && x < 0.0) {
        return -0.0;
    } else {
        return ceil(x);
    }
}


void OS::Setup() {
  // Seed the random number generator.
  // Convert the current time to a 64-bit integer first, before converting it
  // to an unsigned. Going directly can cause an overflow and the seed to be
  // set to all ones. The seed will be identical for different instances that
  // call this setup code within the same millisecond.
  uint64_t seed = static_cast<uint64_t>(TimeCurrentMillis());
  srandom(static_cast<unsigned int>(seed));
}


void OS::ReleaseStore(volatile AtomicWord* ptr, AtomicWord value) {
  __asm__ __volatile__("" : : : "memory");
  *ptr = value;
}


uint64_t OS::CpuFeaturesImpliedByPlatform() {
  return 0;  // OpenBSD runs on anything.
}


int OS::ActivationFrameAlignment() {
  // 16 byte alignment on OpenBSD
  return 16;
}


const char* OS::LocalTimezone(double time) {
  if (isnan(time)) return "";
  time_t tv = static_cast<time_t>(floor(time/msPerSecond));
  struct tm* t = localtime(&tv);
  if (NULL == t) return "";
  return t->tm_zone;
}


double OS::LocalTimeOffset() {
  time_t tv = time(NULL);
  struct tm* t = localtime(&tv);
  // tm_gmtoff includes any daylight savings offset, so subtract it.
  return static_cast<double>(t->tm_gmtoff * msPerSecond -
                             (t->tm_isdst > 0 ? 3600 * msPerSecond : 0));
}


// We keep the lowest and highest addresses mapped as a quick way of
// determining that pointers are outside the heap (used mostly in assertions
// and verification).  The estimate is conservative, ie, not all addresses in
// 'allocated' space are actually allocated to our heap.  The range is
// [lowest, highest), inclusive on the low and and exclusive on the high end.
static void* lowest_ever_allocated = reinterpret_cast<void*>(-1);
static void* highest_ever_allocated = reinterpret_cast<void*>(0);


static void UpdateAllocatedSpaceLimits(void* address, int size) {
  lowest_ever_allocated = Min(lowest_ever_allocated, address);
  highest_ever_allocated =
      Max(highest_ever_allocated,
          reinterpret_cast<void*>(reinterpret_cast<char*>(address) + size));
}


bool OS::IsOutsideAllocatedSpace(void* address) {
  return address < lowest_ever_allocated || address >= highest_ever_allocated;
}


size_t OS::AllocateAlignment() {
  return getpagesize();
}


void* OS::Allocate(const size_t requested,
                   size_t* allocated,
                   bool executable) {
  const size_t msize = RoundUp(requested, getpagesize());
  int prot = PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0);
  void* mbase = mmap(NULL, msize, prot, MAP_PRIVATE | MAP_ANON, -1, 0);

  if (mbase == MAP_FAILED) {
    LOG(ISOLATE, StringEvent("OS::Allocate", "mmap failed"));
    return NULL;
  }
  *allocated = msize;
  UpdateAllocatedSpaceLimits(mbase, msize);
  return mbase;
}


void OS::Free(void* buf, const size_t length) {
  int result = munmap(buf, length);
  USE(result);
  ASSERT(result == 0);
}


#ifdef ENABLE_HEAP_PROTECTION

void OS::Protect(void* address, size_t size) {
  UNIMPLEMENTED();
}


void OS::Unprotect(void* address, size_t size, bool is_executable) {
  UNIMPLEMENTED();
}

#endif


void OS::Sleep(int milliseconds) {
  unsigned int ms = static_cast<unsigned int>(milliseconds);
  usleep(1000 * ms);
}


void OS::Abort() {
  // Redirect to std abort to signal abnormal program termination.
  abort();
}


void OS::DebugBreak() {
#if (defined(__arm__) || defined(__thumb__))
# if defined(CAN_USE_ARMV5_INSTRUCTIONS)
  asm("bkpt 0");
# endif
#else
  asm("int $3");
#endif
}


class PosixMemoryMappedFile : public OS::MemoryMappedFile {
 public:
  PosixMemoryMappedFile(FILE* file, void* memory, int size)
    : file_(file), memory_(memory), size_(size) { }
  virtual ~PosixMemoryMappedFile();
  virtual void* memory() { return memory_; }
  virtual int size() { return size_; }
 private:
  FILE* file_;
  void* memory_;
  int size_;
};


OS::MemoryMappedFile* OS::MemoryMappedFile::open(const char* name) {
  FILE* file = fopen(name, "r+");
  if (file == NULL) return NULL;

  fseek(file, 0, SEEK_END);
  int size = ftell(file);

  void* memory =
      mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(file), 0);
  return new PosixMemoryMappedFile(file, memory, size);
}


OS::MemoryMappedFile* OS::MemoryMappedFile::create(const char* name, int size,
    void* initial) {
  FILE* file = fopen(name, "w+");
  if (file == NULL) return NULL;
  int result = fwrite(initial, size, 1, file);
  if (result < 1) {
    fclose(file);
    return NULL;
  }
  void* memory =
      mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(file), 0);
  return new PosixMemoryMappedFile(file, memory, size);
}


PosixMemoryMappedFile::~PosixMemoryMappedFile() {
  if (memory_) munmap(memory_, size_);
  fclose(file_);
}


#ifdef ENABLE_LOGGING_AND_PROFILING
static unsigned StringToLong(char* buffer) {
  return static_cast<unsigned>(strtol(buffer, NULL, 16));  // NOLINT
}
#endif


void OS::LogSharedLibraryAddresses() {
#ifdef ENABLE_LOGGING_AND_PROFILING
  static const int MAP_LENGTH = 1024;
  int fd = open("/proc/self/maps", O_RDONLY);
  if (fd < 0) return;
  while (true) {
    char addr_buffer[11];
    addr_buffer[0] = '0';
    addr_buffer[1] = 'x';
    addr_buffer[10] = 0;
    int result = read(fd, addr_buffer + 2, 8);
    if (result < 8) break;
    unsigned start = StringToLong(addr_buffer);
    result = read(fd, addr_buffer + 2, 1);
    if (result < 1) break;
    if (addr_buffer[2] != '-') break;
    result = read(fd, addr_buffer + 2, 8);
    if (result < 8) break;
    unsigned end = StringToLong(addr_buffer);
    char buffer[MAP_LENGTH];
    int bytes_read = -1;
    do {
      bytes_read++;
      if (bytes_read >= MAP_LENGTH - 1)
        break;
      result = read(fd, buffer + bytes_read, 1);
      if (result < 1) break;
    } while (buffer[bytes_read] != '\n');
    buffer[bytes_read] = 0;
    // Ignore mappings that are not executable.
    if (buffer[3] != 'x') continue;
    char* start_of_path = index(buffer, '/');
    // There may be no filename in this line.  Skip to next.
    if (start_of_path == NULL) continue;
    buffer[bytes_read] = 0;
    LOG(SharedLibraryEvent(start_of_path, start, end));
  }
  close(fd);
#endif
}


void OS::SignalCodeMovingGC() {
}


int OS::StackWalk(Vector<OS::StackFrame> frames) {
  UNIMPLEMENTED();
  return 1;
}


// Constants used for mmap.
static const int kMmapFd = -1;
static const int kMmapFdOffset = 0;


VirtualMemory::VirtualMemory(size_t size) {
  address_ = mmap(NULL, size, PROT_NONE,
                  MAP_PRIVATE | MAP_ANON | MAP_NORESERVE,
                  kMmapFd, kMmapFdOffset);
  size_ = size;
}


VirtualMemory::~VirtualMemory() {
  if (IsReserved()) {
    if (0 == munmap(address(), size())) address_ = MAP_FAILED;
  }
}


bool VirtualMemory::IsReserved() {
  return address_ != MAP_FAILED;
}


bool VirtualMemory::Commit(void* address, size_t size, bool executable) {
  int prot = PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0);
  if (MAP_FAILED == mmap(address, size, prot,
                         MAP_PRIVATE | MAP_ANON | MAP_FIXED,
                         kMmapFd, kMmapFdOffset)) {
    return false;
  }

  UpdateAllocatedSpaceLimits(address, size);
  return true;
}


bool VirtualMemory::Uncommit(void* address, size_t size) {
  return mmap(address, size, PROT_NONE,
              MAP_PRIVATE | MAP_ANON | MAP_NORESERVE,
              kMmapFd, kMmapFdOffset) != MAP_FAILED;
}


class Thread::PlatformData : public Malloced {
 public:
  PlatformData() : thread_(kNoThread) {}

  pthread_t thread_;  // Thread handle for pthread.
};


Thread::Thread(Isolate* isolate, const Options& options)
    : data_(new PlatformData()),
      isolate_(isolate),
      stack_size_(options.stack_size) {
  set_name(options.name);
}


Thread::Thread(Isolate* isolate, const char* name)
    : data_(new PlatfromData()),
      isolate_(isolate),
      stack_size_(0) {
  set_name(name);
}


Thread::~Thread() {
  delete data_;
}


static void* ThreadEntry(void* arg) {
  Thread* thread = reinterpret_cast<Thread*>(arg);
  // This is also initialized by the first argument to pthread_create() but we
  // don't know which thread will run first (the original thread or the new
  // one) so we initialize it here too.
  thread->data()->thread_ = pthread_self();
  ASSERT(thread->data()->thread_ != kNoThread);
  Thread::SetThreadLocal(Isolate::isolate_key(), thread->isolate());
  thread->Run();
  return NULL;
}


void Thread::set_name(const char* name) {
  strncpy(name_, name, sizeof(name_));
  name_[sizeof(name_) - 1] = '\0';
}


void Thread::Start() {
  pthread_attr_t* attr_ptr = NULL;
  pthread_attr_t attr;
  if (stack_size_ > 0) {
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, static_cast<size_t>(stack_size_));
    attr_ptr = &attr;
  }
  pthread_create(&data_->thread_, attr_ptr, ThreadEntry, this);
  ASSERT(IsValid());
}


void Thread::Join() {
  pthread_join(data_->thread_, NULL);
}


Thread::LocalStorageKey Thread::CreateThreadLocalKey() {
  pthread_key_t key;
  int result = pthread_key_create(&key, NULL);
  USE(result);
  ASSERT(result == 0);
  return static_cast<LocalStorageKey>(key);
}


void Thread::DeleteThreadLocalKey(LocalStorageKey key) {
  pthread_key_t pthread_key = static_cast<pthread_key_t>(key);
  int result = pthread_key_delete(pthread_key);
  USE(result);
  ASSERT(result == 0);
}


void* Thread::GetThreadLocal(LocalStorageKey key) {
  pthread_key_t pthread_key = static_cast<pthread_key_t>(key);
  return pthread_getspecific(pthread_key);
}


void Thread::SetThreadLocal(LocalStorageKey key, void* value) {
  pthread_key_t pthread_key = static_cast<pthread_key_t>(key);
  pthread_setspecific(pthread_key, value);
}


void Thread::YieldCPU() {
  sched_yield();
}


class OpenBSDMutex : public Mutex {
 public:

  OpenBSDMutex() {
    pthread_mutexattr_t attrs;
    int result = pthread_mutexattr_init(&attrs);
    ASSERT(result == 0);
    result = pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_RECURSIVE);
    ASSERT(result == 0);
    result = pthread_mutex_init(&mutex_, &attrs);
    ASSERT(result == 0);
  }

  virtual ~OpenBSDMutex() { pthread_mutex_destroy(&mutex_); }

  virtual int Lock() {
    int result = pthread_mutex_lock(&mutex_);
    return result;
  }

  virtual int Unlock() {
    int result = pthread_mutex_unlock(&mutex_);
    return result;
  }

 private:
  pthread_mutex_t mutex_;   // Pthread mutex for POSIX platforms.
};


Mutex* OS::CreateMutex() {
  return new OpenBSDMutex();
}


class OpenBSDSemaphore : public Semaphore {
 public:
  explicit OpenBSDSemaphore(int count) {  sem_init(&sem_, 0, count); }
  virtual ~OpenBSDSemaphore() { sem_destroy(&sem_); }

  virtual void Wait();
  virtual bool Wait(int timeout);
  virtual void Signal() { sem_post(&sem_); }
 private:
  sem_t sem_;
};


void OpenBSDSemaphore::Wait() {
  while (true) {
    int result = sem_wait(&sem_);
    if (result == 0) return;  // Successfully got semaphore.
    CHECK(result == -1 && errno == EINTR);  // Signal caused spurious wakeup.
  }
}


bool OpenBSDSemaphore::Wait(int timeout) {
  const long kOneSecondMicros = 1000000;  // NOLINT

  // Split timeout into second and nanosecond parts.
  struct timeval delta;
  delta.tv_usec = timeout % kOneSecondMicros;
  delta.tv_sec = timeout / kOneSecondMicros;

  struct timeval current_time;
  // Get the current time.
  if (gettimeofday(&current_time, NULL) == -1) {
    return false;
  }

  // Calculate time for end of timeout.
  struct timeval end_time;
  timeradd(&current_time, &delta, &end_time);

  struct timespec ts;
  TIMEVAL_TO_TIMESPEC(&end_time, &ts);
  while (true) {
    int result = sem_trywait(&sem_);
    if (result == 0) return true;  // Successfully got semaphore.
    if (result == -1 && errno == ETIMEDOUT) return false;  // Timeout.
    CHECK(result == -1 && errno == EINTR);  // Signal caused spurious wakeup.
  }
}


Semaphore* OS::CreateSemaphore(int count) {
  return new OpenBSDSemaphore(count);
}


#ifdef ENABLE_LOGGING_AND_PROFILING

static Sampler* active_sampler_ = NULL;

static void ProfilerSignalHandler(int signal, siginfo_t* info, void* context) {
  USE(info);
  if (signal != SIGPROF) return;
  if (active_sampler_ == NULL) return;

  TickSample sample;

  // We always sample the VM state.
  sample.state = VMState::current_state();

  active_sampler_->Tick(&sample);
}


class Sampler::PlatformData : public Malloced {
 public:
  PlatformData() {
    signal_handler_installed_ = false;
  }

  bool signal_handler_installed_;
  struct sigaction old_signal_handler_;
  struct itimerval old_timer_value_;
};


Sampler::Sampler(Isolate* isolate, int interval)
    : isolate_(isolate),
      interval_(interval),
      profiling_(false),
      active_(false),
      samples_taken_(0) {
  data_ = new PlatformData();
}


Sampler::~Sampler() {
  delete data_;
}


void Sampler::Start() {
  // There can only be one active sampler at the time on POSIX
  // platforms.
  if (active_sampler_ != NULL) return;

  // Request profiling signals.
  struct sigaction sa;
  sa.sa_sigaction = ProfilerSignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO;
  if (sigaction(SIGPROF, &sa, &data_->old_signal_handler_) != 0) return;
  data_->signal_handler_installed_ = true;

  // Set the itimer to generate a tick for each interval.
  itimerval itimer;
  itimer.it_interval.tv_sec = interval_ / 1000;
  itimer.it_interval.tv_usec = (interval_ % 1000) * 1000;
  itimer.it_value.tv_sec = itimer.it_interval.tv_sec;
  itimer.it_value.tv_usec = itimer.it_interval.tv_usec;
  setitimer(ITIMER_PROF, &itimer, &data_->old_timer_value_);

  // Set this sampler as the active sampler.
  active_sampler_ = this;
  active_ = true;
}


void Sampler::Stop() {
  // Restore old signal handler
  if (data_->signal_handler_installed_) {
    setitimer(ITIMER_PROF, &data_->old_timer_value_, NULL);
    sigaction(SIGPROF, &data_->old_signal_handler_, 0);
    data_->signal_handler_installed_ = false;
  }

  // This sampler is no longer the active sampler.
  active_sampler_ = NULL;
  active_ = false;
}

#endif  // ENABLE_LOGGING_AND_PROFILING

} }  // namespace v8::internal
