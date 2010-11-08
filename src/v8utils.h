// Copyright 2010 the V8 project authors. All rights reserved.
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

#ifndef V8_V8UTILS_H_
#define V8_V8UTILS_H_

#include "utils.h"

namespace v8 {
namespace internal {

// ----------------------------------------------------------------------------
// I/O support.

#if __GNUC__ >= 4
// On gcc we can ask the compiler to check the types of %d-style format
// specifiers and their associated arguments.  TODO(erikcorry) fix this
// so it works on MacOSX.
#if defined(__MACH__) && defined(__APPLE__)
#define PRINTF_CHECKING
#else  // MacOsX.
#define PRINTF_CHECKING __attribute__ ((format (printf, 1, 2)))
#endif
#else
#define PRINTF_CHECKING
#endif

// Our version of printf().
void PRINTF_CHECKING PrintF(const char* format, ...);

// Our version of fflush.
void Flush();


// Read a line of characters after printing the prompt to stdout. The resulting
// char* needs to be disposed off with DeleteArray by the caller.
char* ReadLine(const char* prompt);


// Read and return the raw bytes in a file. the size of the buffer is returned
// in size.
// The returned buffer must be freed by the caller.
byte* ReadBytes(const char* filename, int* size, bool verbose = true);


// Write size chars from str to the file given by filename.
// The file is overwritten. Returns the number of chars written.
int WriteChars(const char* filename,
               const char* str,
               int size,
               bool verbose = true);


// Write size bytes to the file given by filename.
// The file is overwritten. Returns the number of bytes written.
int WriteBytes(const char* filename,
               const byte* bytes,
               int size,
               bool verbose = true);


// Write the C code
// const char* <varname> = "<str>";
// const int <varname>_len = <len>;
// to the file given by filename. Only the first len chars are written.
int WriteAsCFile(const char* filename, const char* varname,
                 const char* str, int size, bool verbose = true);


// Data structures

template <typename T>
inline Vector< Handle<Object> > HandleVector(v8::internal::Handle<T>* elms,
                                             int length) {
  return Vector< Handle<Object> >(
      reinterpret_cast<v8::internal::Handle<Object>*>(elms), length);
}

// Memory

// Copies data from |src| to |dst|.  The data spans MUST not overlap.
inline void CopyWords(Object** dst, Object** src, int num_words) {
  ASSERT(Min(dst, src) + num_words <= Max(dst, src));
  ASSERT(num_words > 0);

  // Use block copying memcpy if the segment we're copying is
  // enough to justify the extra call/setup overhead.
  static const int kBlockCopyLimit = 16;

  if (num_words >= kBlockCopyLimit) {
    memcpy(dst, src, num_words * kPointerSize);
  } else {
    int remaining = num_words;
    do {
      remaining--;
      *dst++ = *src++;
    } while (remaining > 0);
  }
}


template <typename T>
static inline void MemsetPointer(T** dest, T* value, int counter) {
#if defined(V8_HOST_ARCH_IA32)
#define STOS "stosl"
#elif defined(V8_HOST_ARCH_X64)
#define STOS "stosq"
#endif

#if defined(__GNUC__) && defined(STOS)
  asm volatile(
      "cld;"
      "rep ; " STOS
      : "+&c" (counter), "+&D" (dest)
      : "a" (value)
      : "memory", "cc");
#else
  for (int i = 0; i < counter; i++) {
    dest[i] = value;
  }
#endif

#undef STOS
}


// Simple wrapper that allows an ExternalString to refer to a
// Vector<const char>. Doesn't assume ownership of the data.
class AsciiStringAdapter: public v8::String::ExternalAsciiStringResource {
 public:
  explicit AsciiStringAdapter(Vector<const char> data) : data_(data) {}

  virtual const char* data() const { return data_.start(); }

  virtual size_t length() const { return data_.length(); }

 private:
  Vector<const char> data_;
};


// Simple support to read a file into a 0-terminated C-string.
// The returned buffer must be freed by the caller.
// On return, *exits tells whether the file existed.
Vector<const char> ReadFile(const char* filename,
                            bool* exists,
                            bool verbose = true);


// Helper class for building result strings in a character buffer. The
// purpose of the class is to use safe operations that checks the
// buffer bounds on all operations in debug mode.
class StringBuilder {
 public:
  // Create a string builder with a buffer of the given size. The
  // buffer is allocated through NewArray<char> and must be
  // deallocated by the caller of Finalize().
  explicit StringBuilder(int size);

  StringBuilder(char* buffer, int size)
      : buffer_(buffer, size), position_(0) { }

  ~StringBuilder() { if (!is_finalized()) Finalize(); }

  int size() const { return buffer_.length(); }

  // Get the current position in the builder.
  int position() const {
    ASSERT(!is_finalized());
    return position_;
  }

  // Reset the position.
  void Reset() { position_ = 0; }

  // Add a single character to the builder. It is not allowed to add
  // 0-characters; use the Finalize() method to terminate the string
  // instead.
  void AddCharacter(char c) {
    ASSERT(c != '\0');
    ASSERT(!is_finalized() && position_ < buffer_.length());
    buffer_[position_++] = c;
  }

  // Add an entire string to the builder. Uses strlen() internally to
  // compute the length of the input string.
  void AddString(const char* s);

  // Add the first 'n' characters of the given string 's' to the
  // builder. The input string must have enough characters.
  void AddSubstring(const char* s, int n);

  // Add formatted contents to the builder just like printf().
  void AddFormatted(const char* format, ...);

  // Add character padding to the builder. If count is non-positive,
  // nothing is added to the builder.
  void AddPadding(char c, int count);

  // Finalize the string by 0-terminating it and returning the buffer.
  char* Finalize();

 private:
  Vector<char> buffer_;
  int position_;

  bool is_finalized() const { return position_ < 0; }

  DISALLOW_IMPLICIT_CONSTRUCTORS(StringBuilder);
};


// Custom memcpy implementation for platforms where the standard version
// may not be good enough.
#if defined(V8_TARGET_ARCH_IA32)

// The default memcpy on ia32 architectures is generally not as efficient
// as possible. (If any further ia32 platforms are introduced where the
// memcpy function is efficient, exclude them from this branch).

typedef void (*MemCopyFunction)(void* dest, const void* src, size_t size);

// Implemented in codegen-<arch>.cc.
MemCopyFunction CreateMemCopyFunction();

// Copy memory area to disjoint memory area.
static inline void MemCopy(void* dest, const void* src, size_t size) {
  static MemCopyFunction memcopy = CreateMemCopyFunction();
  (*memcopy)(dest, src, size);
#ifdef DEBUG
  CHECK_EQ(0, memcmp(dest, src, size));
#endif
}

// Limit below which the extra overhead of the MemCopy function is likely
// to outweigh the benefits of faster copying.
static const int kMinComplexMemCopy = 64;

#else  // V8_TARGET_ARCH_IA32

static inline void MemCopy(void* dest, const void* src, size_t size) {
  memcpy(dest, src, size);
}

static const int kMinComplexMemCopy = 256;

#endif  // V8_TARGET_ARCH_IA32


// Copy from ASCII/16bit chars to ASCII/16bit chars.
template <typename sourcechar, typename sinkchar>
static inline void CopyChars(sinkchar* dest, const sourcechar* src, int chars) {
  sinkchar* limit = dest + chars;
#ifdef V8_HOST_CAN_READ_UNALIGNED
  if (sizeof(*dest) == sizeof(*src)) {
    if (chars >= static_cast<int>(kMinComplexMemCopy / sizeof(*dest))) {
      MemCopy(dest, src, chars * sizeof(*dest));
      return;
    }
    // Number of characters in a uintptr_t.
    static const int kStepSize = sizeof(uintptr_t) / sizeof(*dest);  // NOLINT
    while (dest <= limit - kStepSize) {
      *reinterpret_cast<uintptr_t*>(dest) =
          *reinterpret_cast<const uintptr_t*>(src);
      dest += kStepSize;
      src += kStepSize;
    }
  }
#endif
  while (dest < limit) {
    *dest++ = static_cast<sinkchar>(*src++);
  }
}

} }  // namespace v8::internal

#endif  // V8_V8UTILS_H_
