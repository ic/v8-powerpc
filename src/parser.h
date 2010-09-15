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

#ifndef V8_PARSER_H_
#define V8_PARSER_H_

#include "scanner.h"
#include "allocation.h"

namespace v8 {
namespace internal {


class ParserMessage : public Malloced {
 public:
  ParserMessage(Scanner::Location loc, const char* message,
                Vector<const char*> args)
      : loc_(loc),
        message_(message),
        args_(args) { }
  ~ParserMessage();
  Scanner::Location location() { return loc_; }
  const char* message() { return message_; }
  Vector<const char*> args() { return args_; }
 private:
  Scanner::Location loc_;
  const char* message_;
  Vector<const char*> args_;
};


class FunctionEntry BASE_EMBEDDED {
 public:
  explicit FunctionEntry(Vector<unsigned> backing) : backing_(backing) { }
  FunctionEntry() : backing_(Vector<unsigned>::empty()) { }

  int start_pos() { return backing_[kStartPosOffset]; }
  void set_start_pos(int value) { backing_[kStartPosOffset] = value; }

  int end_pos() { return backing_[kEndPosOffset]; }
  void set_end_pos(int value) { backing_[kEndPosOffset] = value; }

  int literal_count() { return backing_[kLiteralCountOffset]; }
  void set_literal_count(int value) { backing_[kLiteralCountOffset] = value; }

  int property_count() { return backing_[kPropertyCountOffset]; }
  void set_property_count(int value) {
    backing_[kPropertyCountOffset] = value;
  }

  int predata_function_skip() { return backing_[kPredataFunctionSkipOffset]; }
  void set_predata_function_skip(int value) {
    backing_[kPredataFunctionSkipOffset] = value;
  }

  int predata_symbol_skip() { return backing_[kPredataSymbolSkipOffset]; }
  void set_predata_symbol_skip(int value) {
    backing_[kPredataSymbolSkipOffset] = value;
  }

  bool is_valid() { return backing_.length() > 0; }

  static const int kSize = 6;

 private:
  Vector<unsigned> backing_;
  static const int kStartPosOffset = 0;
  static const int kEndPosOffset = 1;
  static const int kLiteralCountOffset = 2;
  static const int kPropertyCountOffset = 3;
  static const int kPredataFunctionSkipOffset = 4;
  static const int kPredataSymbolSkipOffset = 5;
};


class ScriptDataImpl : public ScriptData {
 public:
  explicit ScriptDataImpl(Vector<unsigned> store)
      : store_(store),
        function_index_(kHeaderSize),
        owns_store_(true) {
    Initialize();
  }

  // Create an empty ScriptDataImpl that is guaranteed to not satisfy
  // a SanityCheck.
  ScriptDataImpl() : store_(Vector<unsigned>()), owns_store_(false) { }

  virtual ~ScriptDataImpl();
  virtual int Length();
  virtual const char* Data();
  virtual bool HasError();

  void Initialize();
  void ReadNextSymbolPosition();

  FunctionEntry GetFunctionEntry(int start);
  int GetSymbolIdentifier();
  void SkipFunctionEntry(int start);
  bool SanityCheck();

  Scanner::Location MessageLocation();
  const char* BuildMessage();
  Vector<const char*> BuildArgs();

  int symbol_count() {
    return (store_.length() > kHeaderSize) ? store_[kSymbolCountOffset] : 0;
  }
  // The following functions should only be called if SanityCheck has
  // returned true.
  bool has_error() { return store_[kHasErrorOffset]; }
  unsigned magic() { return store_[kMagicOffset]; }
  unsigned version() { return store_[kVersionOffset]; }

  // Skip forward in the preparser data by the given number
  // of unsigned ints of function entries and the given number of bytes of
  // symbol id encoding.
  void Skip(int function_entries, int symbol_entries) {
    ASSERT(function_entries >= 0);
    ASSERT(function_entries
           <= (static_cast<int>(store_[kFunctionsSizeOffset])
               - (function_index_ - kHeaderSize)));
    ASSERT(symbol_entries >= 0);
    ASSERT(symbol_entries <= symbol_data_end_ - symbol_data_);

    unsigned max_function_skip = store_[kFunctionsSizeOffset] -
        static_cast<unsigned>(function_index_ - kHeaderSize);
    function_index_ +=
        Min(static_cast<unsigned>(function_entries), max_function_skip);
    symbol_data_ +=
        Min(static_cast<unsigned>(symbol_entries),
            static_cast<unsigned>(symbol_data_end_ - symbol_data_));
  }

  static const unsigned kMagicNumber = 0xBadDead;
  static const unsigned kCurrentVersion = 3;

  static const int kMagicOffset = 0;
  static const int kVersionOffset = 1;
  static const int kHasErrorOffset = 2;
  static const int kFunctionsSizeOffset = 3;
  static const int kSymbolCountOffset = 4;
  static const int kSizeOffset = 5;
  static const int kHeaderSize = 6;

  // If encoding a message, the following positions are fixed.
  static const int kMessageStartPos = 0;
  static const int kMessageEndPos = 1;
  static const int kMessageArgCountPos = 2;
  static const int kMessageTextPos = 3;

  static const byte kNumberTerminator = 0x80u;

 private:
  Vector<unsigned> store_;
  unsigned char* symbol_data_;
  unsigned char* symbol_data_end_;
  int function_index_;
  bool owns_store_;

  unsigned Read(int position);
  unsigned* ReadAddress(int position);
  // Reads a number from the current symbols
  int ReadNumber(byte** source);

  ScriptDataImpl(const char* backing_store, int length)
      : store_(reinterpret_cast<unsigned*>(const_cast<char*>(backing_store)),
               length / sizeof(unsigned)),
        function_index_(kHeaderSize),
        owns_store_(false) {
    ASSERT_EQ(0, reinterpret_cast<intptr_t>(backing_store) % sizeof(unsigned));
    Initialize();
  }

  // Read strings written by ParserRecorder::WriteString.
  static const char* ReadString(unsigned* start, int* chars);

  friend class ScriptData;
};


// The parser: Takes a script and and context information, and builds a
// FunctionLiteral AST node. Returns NULL and deallocates any allocated
// AST nodes if parsing failed.
FunctionLiteral* MakeAST(bool compile_in_global_context,
                         Handle<Script> script,
                         v8::Extension* extension,
                         ScriptDataImpl* pre_data,
                         bool is_json = false);

// Generic preparser generating full preparse data.
ScriptDataImpl* PreParse(Handle<String> source,
                         unibrow::CharacterStream* stream,
                         v8::Extension* extension);

// Preparser that only does preprocessing that makes sense if only used
// immediately after.
ScriptDataImpl* PartialPreParse(Handle<String> source,
                                unibrow::CharacterStream* stream,
                                v8::Extension* extension);


bool ParseRegExp(FlatStringReader* input,
                 bool multiline,
                 RegExpCompileData* result);


// Support for doing lazy compilation. The script is the script containing full
// source of the script where the function is declared. The start_position and
// end_position specifies the part of the script source which has the source
// for the function declaration in the form:
//
//    (<formal parameters>) { <function body> }
//
// without any function keyword or name.
//
FunctionLiteral* MakeLazyAST(Handle<Script> script,
                             Handle<String> name,
                             int start_position,
                             int end_position,
                             bool is_expression);


// Support for handling complex values (array and object literals) that
// can be fully handled at compile time.
class CompileTimeValue: public AllStatic {
 public:
  enum Type {
    OBJECT_LITERAL_FAST_ELEMENTS,
    OBJECT_LITERAL_SLOW_ELEMENTS,
    ARRAY_LITERAL
  };

  static bool IsCompileTimeValue(Expression* expression);

  static bool ArrayLiteralElementNeedsInitialization(Expression* value);

  // Get the value as a compile time value.
  static Handle<FixedArray> GetValue(Expression* expression);

  // Get the type of a compile time value returned by GetValue().
  static Type GetType(Handle<FixedArray> value);

  // Get the elements array of a compile time value returned by GetValue().
  static Handle<FixedArray> GetElements(Handle<FixedArray> value);

 private:
  static const int kTypeSlot = 0;
  static const int kElementsSlot = 1;

  DISALLOW_IMPLICIT_CONSTRUCTORS(CompileTimeValue);
};


} }  // namespace v8::internal

#endif  // V8_PARSER_H_
