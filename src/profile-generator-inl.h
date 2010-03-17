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

#ifndef V8_PROFILE_GENERATOR_INL_H_
#define V8_PROFILE_GENERATOR_INL_H_

#include "profile-generator.h"

namespace v8 {
namespace internal {


bool CodeEntry::is_js_function() {
  return tag_ == Logger::FUNCTION_TAG
      || tag_ == Logger::LAZY_COMPILE_TAG
      || tag_ == Logger::SCRIPT_TAG;
}


StaticNameCodeEntry::StaticNameCodeEntry(Logger::LogEventsAndTags tag,
                                         const char* name)
    : CodeEntry(tag),
      name_(name) {
}


ManagedNameCodeEntry::ManagedNameCodeEntry(Logger::LogEventsAndTags tag,
                                           String* name,
                                           const char* resource_name,
                                           int line_number)
    : CodeEntry(tag),
      name_(name->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL).Detach()),
      resource_name_(resource_name),
      line_number_(line_number) {
}


ProfileNode::ProfileNode(CodeEntry* entry)
    : entry_(entry),
      total_ticks_(0),
      self_ticks_(0),
      children_(CodeEntriesMatch) {
}


void CodeMap::AddCode(Address addr, CodeEntry* entry, unsigned size) {
  CodeTree::Locator locator;
  tree_.Insert(addr, &locator);
  locator.set_value(CodeEntryInfo(entry, size));
}


void CodeMap::MoveCode(Address from, Address to) {
  tree_.Move(from, to);
}

void CodeMap::DeleteCode(Address addr) {
  tree_.Remove(addr);
}


} }  // namespace v8::internal

#endif  // V8_PROFILE_GENERATOR_INL_H_
