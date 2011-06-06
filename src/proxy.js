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

global.Proxy = new $Object();

var $Proxy = global.Proxy

var fundamentalTraps = [
  "getOwnPropertyDescriptor",
  "getPropertyDescriptor",
  "getOwnPropertyNames",
  "getPropertyNames",
  "defineProperty",
  "delete",
  "fix",
]

var derivedTraps = [
  "has",
  "hasOwn",
  "get",
  "set",
  "enumerate",
  "keys",
]

var functionTraps = [
  "callTrap",
  "constructTrap",
]

$Proxy.createFunction = function(handler, callTrap, constructTrap) {
  handler.callTrap = callTrap
  handler.constructTrap = constructTrap
  $Proxy.create(handler)
}

$Proxy.create = function(handler, proto) {
  if (!IS_SPEC_OBJECT(handler))
    throw MakeTypeError("handler_non_object", ["create"])
  if (!IS_SPEC_OBJECT(proto)) proto = null  // Mozilla does this...
  return %CreateJSProxy(handler, proto)
}




////////////////////////////////////////////////////////////////////////////////
// Builtins
////////////////////////////////////////////////////////////////////////////////

function DerivedGetTrap(receiver, name) {
  var desc = this.getPropertyDescriptor(name)
  if (IS_UNDEFINED(desc)) { return desc }
  if ('value' in desc) {
    return desc.value
  } else {
    if (IS_UNDEFINED(desc.get)) { return desc.get }
    // The proposal says: desc.get.call(receiver)
    return %_CallFunction(receiver, desc.get)
  }
}

function DerivedSetTrap(receiver, name, val) {
  var desc = this.getOwnPropertyDescriptor(name)
  if (desc) {
    if ('writable' in desc) {
      if (desc.writable) {
        desc.value = val
        this.defineProperty(name, desc)
        return true
      } else {
        return false
      }
    } else { // accessor
      if (desc.set) {
        // The proposal says: desc.set.call(receiver, val)
        %_CallFunction(receiver, val, desc.set)
        return true
      } else {
        return false
      }
    }
  }
  desc = this.getPropertyDescriptor(name)
  if (desc) {
    if ('writable' in desc) {
      if (desc.writable) {
        // fall through
      } else {
        return false
      }
    } else { // accessor
      if (desc.set) {
        // The proposal says: desc.set.call(receiver, val)
        %_CallFunction(receiver, val, desc.set)
        return true
      } else {
        return false
      }
    }
  }
  this.defineProperty(name, {
    value: val,
    writable: true,
    enumerable: true,
    configurable: true});
  return true;
}

function DerivedHasTrap(name) {
  return !!this.getPropertyDescriptor(name)
}
