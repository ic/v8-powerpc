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

// This file relies on the fact that the following declarations have been made
//
// in runtime.js:
// const $Object = global.Object;
// const $Boolean = global.Boolean;
// const $Number = global.Number;
// const $Function = global.Function;
// const $Array = global.Array;
// const $NaN = 0/0;
//
// in math.js:
// const $floor = MathFloor

const $isNaN = GlobalIsNaN;
const $isFinite = GlobalIsFinite;


// ----------------------------------------------------------------------------


// Helper function used to install functions on objects.
function InstallFunctions(object, attributes, functions) {
  if (functions.length >= 8) {
    %OptimizeObjectForAddingMultipleProperties(object, functions.length >> 1);
  }
  for (var i = 0; i < functions.length; i += 2) {
    var key = functions[i];
    var f = functions[i + 1];
    %FunctionSetName(f, key);
    %FunctionRemovePrototype(f);
    %SetProperty(object, key, f, attributes);
  }
  %ToFastProperties(object);
}

// Emulates JSC by installing functions on a hidden prototype that
// lies above the current object/prototype.  This lets you override
// functions on String.prototype etc. and then restore the old function
// with delete.  See http://code.google.com/p/chromium/issues/detail?id=1717
function InstallFunctionsOnHiddenPrototype(object, attributes, functions) {
  var hidden_prototype = new $Object();
  %SetHiddenPrototype(object, hidden_prototype);
  InstallFunctions(hidden_prototype, attributes, functions);
}


// ----------------------------------------------------------------------------


// ECMA 262 - 15.1.4
function GlobalIsNaN(number) {
  var n = ToNumber(number);
  return NUMBER_IS_NAN(n);
}


// ECMA 262 - 15.1.5
function GlobalIsFinite(number) {
  if (!IS_NUMBER(number)) number = NonNumberToNumber(number);

  // NaN - NaN == NaN, Infinity - Infinity == NaN, -Infinity - -Infinity == NaN.
  return %_IsSmi(number) || number - number == 0;
}


// ECMA-262 - 15.1.2.2
function GlobalParseInt(string, radix) {
  if (IS_UNDEFINED(radix)) {
    // Some people use parseInt instead of Math.floor.  This
    // optimization makes parseInt on a Smi 12 times faster (60ns
    // vs 800ns).  The following optimization makes parseInt on a
    // non-Smi number 9 times faster (230ns vs 2070ns).  Together
    // they make parseInt on a string 1.4% slower (274ns vs 270ns).
    if (%_IsSmi(string)) return string;
    if (IS_NUMBER(string) &&
        ((0.01 < string && string < 1e9) ||
            (-1e9 < string && string < -0.01))) {
      // Truncate number.
      return string | 0;
    }
    radix = 0;
  } else {
    radix = TO_INT32(radix);
    if (!(radix == 0 || (2 <= radix && radix <= 36)))
      return $NaN;
  }
  string = TO_STRING_INLINE(string);
  if (%_HasCachedArrayIndex(string) &&
      (radix == 0 || radix == 10)) {
    return %_GetCachedArrayIndex(string);
  }
  return %StringParseInt(string, radix);
}


// ECMA-262 - 15.1.2.3
function GlobalParseFloat(string) {
  string = TO_STRING_INLINE(string);
  if (%_HasCachedArrayIndex(string)) return %_GetCachedArrayIndex(string);
  return %StringParseFloat(string);
}


function GlobalEval(x) {
  if (!IS_STRING(x)) return x;

  var global_receiver = %GlobalReceiver(global);
  var this_is_global_receiver = (this === global_receiver);
  var global_is_detached = (global === global_receiver);

  if (!this_is_global_receiver || global_is_detached) {
    throw new $EvalError('The "this" object passed to eval must ' +
                         'be the global object from which eval originated');
  }

  var f = %CompileString(x);
  if (!IS_FUNCTION(f)) return f;

  return f.call(this);
}


// execScript for IE compatibility.
function GlobalExecScript(expr, lang) {
  // NOTE: We don't care about the character casing.
  if (!lang || /javascript/i.test(lang)) {
    var f = %CompileString(ToString(expr));
    f.call(%GlobalReceiver(global));
  }
  return null;
}


// ----------------------------------------------------------------------------


function SetupGlobal() {
  // ECMA 262 - 15.1.1.1.
  %SetProperty(global, "NaN", $NaN, DONT_ENUM | DONT_DELETE);

  // ECMA-262 - 15.1.1.2.
  %SetProperty(global, "Infinity", 1/0, DONT_ENUM | DONT_DELETE);

  // ECMA-262 - 15.1.1.3.
  %SetProperty(global, "undefined", void 0, DONT_ENUM | DONT_DELETE);

  // Setup non-enumerable function on the global object.
  InstallFunctions(global, DONT_ENUM, $Array(
    "isNaN", GlobalIsNaN,
    "isFinite", GlobalIsFinite,
    "parseInt", GlobalParseInt,
    "parseFloat", GlobalParseFloat,
    "eval", GlobalEval,
    "execScript", GlobalExecScript
  ));
}

SetupGlobal();


// ----------------------------------------------------------------------------
// Boolean (first part of definition)


%SetCode($Boolean, function(x) {
  if (%_IsConstructCall()) {
    %_SetValueOf(this, ToBoolean(x));
  } else {
    return ToBoolean(x);
  }
});

%FunctionSetPrototype($Boolean, new $Boolean(false));

%SetProperty($Boolean.prototype, "constructor", $Boolean, DONT_ENUM);

// ----------------------------------------------------------------------------
// Object

$Object.prototype.constructor = $Object;

// ECMA-262 - 15.2.4.2
function ObjectToString() {
  return "[object " + %_ClassOf(ToObject(this)) + "]";
}


// ECMA-262 - 15.2.4.3
function ObjectToLocaleString() {
  return this.toString();
}


// ECMA-262 - 15.2.4.4
function ObjectValueOf() {
  return ToObject(this);
}


// ECMA-262 - 15.2.4.5
function ObjectHasOwnProperty(V) {
  return %HasLocalProperty(ToObject(this), ToString(V));
}


// ECMA-262 - 15.2.4.6
function ObjectIsPrototypeOf(V) {
  if (!IS_SPEC_OBJECT(V)) return false;
  return %IsInPrototypeChain(this, V);
}


// ECMA-262 - 15.2.4.6
function ObjectPropertyIsEnumerable(V) {
  return %IsPropertyEnumerable(ToObject(this), ToString(V));
}


// Extensions for providing property getters and setters.
function ObjectDefineGetter(name, fun) {
  if (this == null && !IS_UNDETECTABLE(this)) {
    throw new $TypeError('Object.prototype.__defineGetter__: this is Null');
  }
  if (!IS_FUNCTION(fun)) {
    throw new $TypeError('Object.prototype.__defineGetter__: Expecting function');
  }
  return %DefineAccessor(ToObject(this), ToString(name), GETTER, fun);
}


function ObjectLookupGetter(name) {
  if (this == null && !IS_UNDETECTABLE(this)) {
    throw new $TypeError('Object.prototype.__lookupGetter__: this is Null');
  }
  return %LookupAccessor(ToObject(this), ToString(name), GETTER);
}


function ObjectDefineSetter(name, fun) {
  if (this == null && !IS_UNDETECTABLE(this)) {
    throw new $TypeError('Object.prototype.__defineSetter__: this is Null');
  }
  if (!IS_FUNCTION(fun)) {
    throw new $TypeError(
        'Object.prototype.__defineSetter__: Expecting function');
  }
  return %DefineAccessor(ToObject(this), ToString(name), SETTER, fun);
}


function ObjectLookupSetter(name) {
  if (this == null && !IS_UNDETECTABLE(this)) {
    throw new $TypeError('Object.prototype.__lookupSetter__: this is Null');
  }
  return %LookupAccessor(ToObject(this), ToString(name), SETTER);
}


function ObjectKeys(obj) {
  if (!IS_SPEC_OBJECT(obj))
    throw MakeTypeError("obj_ctor_property_non_object", ["keys"]);
  return %LocalKeys(obj);
}


// ES5 8.10.1.
function IsAccessorDescriptor(desc) {
  if (IS_UNDEFINED(desc)) return false;
  return desc.hasGetter_ || desc.hasSetter_;
}


// ES5 8.10.2.
function IsDataDescriptor(desc) {
  if (IS_UNDEFINED(desc)) return false;
  return desc.hasValue_ || desc.hasWritable_;
}


// ES5 8.10.3.
function IsGenericDescriptor(desc) {
  return !(IsAccessorDescriptor(desc) || IsDataDescriptor(desc));
}


function IsInconsistentDescriptor(desc) {
  return IsAccessorDescriptor(desc) && IsDataDescriptor(desc);
}

// ES5 8.10.4
function FromPropertyDescriptor(desc) {
  if (IS_UNDEFINED(desc)) return desc;
  var obj = new $Object();
  if (IsDataDescriptor(desc)) {
    obj.value = desc.getValue();
    obj.writable = desc.isWritable();
  }
  if (IsAccessorDescriptor(desc)) {
    obj.get = desc.getGet();
    obj.set = desc.getSet();
  }
  obj.enumerable = desc.isEnumerable();
  obj.configurable = desc.isConfigurable();
  return obj;
}

// ES5 8.10.5.
function ToPropertyDescriptor(obj) {
  if (!IS_SPEC_OBJECT(obj)) {
    throw MakeTypeError("property_desc_object", [obj]);
  }
  var desc = new PropertyDescriptor();

  if ("enumerable" in obj) {
    desc.setEnumerable(ToBoolean(obj.enumerable));
  }

  if ("configurable" in obj) {
    desc.setConfigurable(ToBoolean(obj.configurable));
  }

  if ("value" in obj) {
    desc.setValue(obj.value);
  }

  if ("writable" in obj) {
    desc.setWritable(ToBoolean(obj.writable));
  }

  if ("get" in obj) {
    var get = obj.get;
    if (!IS_UNDEFINED(get) && !IS_FUNCTION(get)) {
      throw MakeTypeError("getter_must_be_callable", [get]);
    }
    desc.setGet(get);
  }

  if ("set" in obj) {
    var set = obj.set;
    if (!IS_UNDEFINED(set) && !IS_FUNCTION(set)) {
      throw MakeTypeError("setter_must_be_callable", [set]);
    }
    desc.setSet(set);
  }

  if (IsInconsistentDescriptor(desc)) {
    throw MakeTypeError("value_and_accessor", [obj]);
  }
  return desc;
}


function PropertyDescriptor() {
  // Initialize here so they are all in-object and have the same map.
  // Default values from ES5 8.6.1.
  this.value_ = void 0;
  this.hasValue_ = false;
  this.writable_ = false;
  this.hasWritable_ = false;
  this.enumerable_ = false;
  this.hasEnumerable_ = false;
  this.configurable_ = false;
  this.hasConfigurable_ = false;
  this.get_ = void 0;
  this.hasGetter_ = false;
  this.set_ = void 0;
  this.hasSetter_ = false;
}


PropertyDescriptor.prototype.setValue = function(value) {
  this.value_ = value;
  this.hasValue_ = true;
}


PropertyDescriptor.prototype.getValue = function() {
  return this.value_;
}


PropertyDescriptor.prototype.hasValue = function() {
  return this.hasValue_;
}


PropertyDescriptor.prototype.setEnumerable = function(enumerable) {
  this.enumerable_ = enumerable;
  this.hasEnumerable_ = true;
}


PropertyDescriptor.prototype.isEnumerable = function () {
  return this.enumerable_;
}


PropertyDescriptor.prototype.hasEnumerable = function() {
  return this.hasEnumerable_;
}


PropertyDescriptor.prototype.setWritable = function(writable) {
  this.writable_ = writable;
  this.hasWritable_ = true;
}


PropertyDescriptor.prototype.isWritable = function() {
  return this.writable_;
}


PropertyDescriptor.prototype.hasWritable = function() {
  return this.hasWritable_;
}


PropertyDescriptor.prototype.setConfigurable = function(configurable) {
  this.configurable_ = configurable;
  this.hasConfigurable_ = true;
}


PropertyDescriptor.prototype.hasConfigurable = function() {
  return this.hasConfigurable_;
}


PropertyDescriptor.prototype.isConfigurable = function() {
  return this.configurable_;
}


PropertyDescriptor.prototype.setGet = function(get) {
  this.get_ = get;
  this.hasGetter_ = true;
}


PropertyDescriptor.prototype.getGet = function() {
  return this.get_;
}


PropertyDescriptor.prototype.hasGetter = function() {
  return this.hasGetter_;
}


PropertyDescriptor.prototype.setSet = function(set) {
  this.set_ = set;
  this.hasSetter_ = true;
}


PropertyDescriptor.prototype.getSet = function() {
  return this.set_;
}


PropertyDescriptor.prototype.hasSetter = function() {
  return this.hasSetter_;
}


// Converts an array returned from Runtime_GetOwnProperty to an actual
// property descriptor. For a description of the array layout please
// see the runtime.cc file.
function ConvertDescriptorArrayToDescriptor(desc_array) {
  if (desc_array == false) {
    throw 'Internal error: invalid desc_array';
  }

  if (IS_UNDEFINED(desc_array)) {
    return void 0;
  }

  var desc = new PropertyDescriptor();
  // This is an accessor.
  if (desc_array[IS_ACCESSOR_INDEX]) {
    desc.setGet(desc_array[GETTER_INDEX]);
    desc.setSet(desc_array[SETTER_INDEX]);
  } else {
    desc.setValue(desc_array[VALUE_INDEX]);
    desc.setWritable(desc_array[WRITABLE_INDEX]);
  }
  desc.setEnumerable(desc_array[ENUMERABLE_INDEX]);
  desc.setConfigurable(desc_array[CONFIGURABLE_INDEX]);

  return desc;
}


// ES5 section 8.12.2.
function GetProperty(obj, p) {
  var prop = GetOwnProperty(obj);
  if (!IS_UNDEFINED(prop)) return prop;
  var proto = obj.__proto__;
  if (IS_NULL(proto)) return void 0;
  return GetProperty(proto, p);
}


// ES5 section 8.12.6
function HasProperty(obj, p) {
  var desc = GetProperty(obj, p);
  return IS_UNDEFINED(desc) ? false : true;
}


// ES5 section 8.12.1.
function GetOwnProperty(obj, p) {
  // GetOwnProperty returns an array indexed by the constants
  // defined in macros.py.
  // If p is not a property on obj undefined is returned.
  var props = %GetOwnProperty(ToObject(obj), ToString(p));

  // A false value here means that access checks failed.
  if (props == false) return void 0;

  return ConvertDescriptorArrayToDescriptor(props);
}


// ES5 8.12.9.
function DefineOwnProperty(obj, p, desc, should_throw) {
  var current_or_access = %GetOwnProperty(ToObject(obj), ToString(p));
  // A false value here means that access checks failed.
  if (current_or_access == false) return void 0;

  var current = ConvertDescriptorArrayToDescriptor(current_or_access);
  var extensible = %IsExtensible(ToObject(obj));

  // Error handling according to spec.
  // Step 3
  if (IS_UNDEFINED(current) && !extensible)
    throw MakeTypeError("define_disallowed", ["defineProperty"]);

  if (!IS_UNDEFINED(current)) {
    // Step 5 and 6
    if ((IsGenericDescriptor(desc) ||
         IsDataDescriptor(desc) == IsDataDescriptor(current)) &&
        (!desc.hasEnumerable() ||
         SameValue(desc.isEnumerable(), current.isEnumerable())) &&
        (!desc.hasConfigurable() ||
         SameValue(desc.isConfigurable(), current.isConfigurable())) &&
        (!desc.hasWritable() ||
         SameValue(desc.isWritable(), current.isWritable())) &&
        (!desc.hasValue() ||
         SameValue(desc.getValue(), current.getValue())) &&
        (!desc.hasGetter() ||
         SameValue(desc.getGet(), current.getGet())) &&
        (!desc.hasSetter() ||
         SameValue(desc.getSet(), current.getSet()))) {
      return true;
    }
    if (!current.isConfigurable()) {
      // Step 7
      if (desc.isConfigurable() ||
          (desc.hasEnumerable() &&
           desc.isEnumerable() != current.isEnumerable())) {
        throw MakeTypeError("redefine_disallowed", ["defineProperty"]);
      }
      // Step 8
      if (!IsGenericDescriptor(desc)) {
        // Step 9a
        if (IsDataDescriptor(current) != IsDataDescriptor(desc)) {
          throw MakeTypeError("redefine_disallowed", ["defineProperty"]);
        }
        // Step 10a
        if (IsDataDescriptor(current) && IsDataDescriptor(desc)) {
          if (!current.isWritable() && desc.isWritable()) {
            throw MakeTypeError("redefine_disallowed", ["defineProperty"]);
          }
          if (!current.isWritable() && desc.hasValue() &&
              !SameValue(desc.getValue(), current.getValue())) {
            throw MakeTypeError("redefine_disallowed", ["defineProperty"]);
          }
        }
        // Step 11
        if (IsAccessorDescriptor(desc) && IsAccessorDescriptor(current)) {
          if (desc.hasSetter() && !SameValue(desc.getSet(), current.getSet())) {
            throw MakeTypeError("redefine_disallowed", ["defineProperty"]);
          }
          if (desc.hasGetter() && !SameValue(desc.getGet(),current.getGet())) {
            throw MakeTypeError("redefine_disallowed", ["defineProperty"]);
          }
        }
      }
    }
  }

  // Send flags - enumerable and configurable are common - writable is
  // only send to the data descriptor.
  // Take special care if enumerable and configurable is not defined on
  // desc (we need to preserve the existing values from current).
  var flag = NONE;
  if (desc.hasEnumerable()) {
    flag |= desc.isEnumerable() ? 0 : DONT_ENUM;
  } else if (!IS_UNDEFINED(current)) {
    flag |= current.isEnumerable() ? 0 : DONT_ENUM;
  } else {
    flag |= DONT_ENUM;
  }

  if (desc.hasConfigurable()) {
    flag |= desc.isConfigurable() ? 0 : DONT_DELETE;
  } else if (!IS_UNDEFINED(current)) {
    flag |= current.isConfigurable() ? 0 : DONT_DELETE;
  } else
    flag |= DONT_DELETE;

  if (IsDataDescriptor(desc) ||
      (IsGenericDescriptor(desc) &&
       (IS_UNDEFINED(current) || IsDataDescriptor(current)))) {
    // There are 3 cases that lead here:
    // Step 4a - defining a new data property.
    // Steps 9b & 12 - replacing an existing accessor property with a data
    //                 property.
    // Step 12 - updating an existing data property with a data or generic
    //           descriptor.

    if (desc.hasWritable()) {
      flag |= desc.isWritable() ? 0 : READ_ONLY;
    } else if (!IS_UNDEFINED(current)) {
      flag |= current.isWritable() ? 0 : READ_ONLY;
    } else {
      flag |= READ_ONLY;
    }

    var value = void 0;  // Default value is undefined.
    if (desc.hasValue()) {
      value = desc.getValue();
    } else if (!IS_UNDEFINED(current) && IsDataDescriptor(current)) {
      value = current.getValue();
    }

    %DefineOrRedefineDataProperty(obj, p, value, flag);
  } else if (IsGenericDescriptor(desc)) {
    // Step 12 - updating an existing accessor property with generic
    //           descriptor. Changing flags only.
    %DefineOrRedefineAccessorProperty(obj, p, GETTER, current.getGet(), flag);
  } else {
    // There are 3 cases that lead here:
    // Step 4b - defining a new accessor property.
    // Steps 9c & 12 - replacing an existing data property with an accessor
    //                 property.
    // Step 12 - updating an existing accessor property with an accessor
    //           descriptor.
    if (desc.hasGetter()) {
      %DefineOrRedefineAccessorProperty(obj, p, GETTER, desc.getGet(), flag);
    }
    if (desc.hasSetter()) {
      %DefineOrRedefineAccessorProperty(obj, p, SETTER, desc.getSet(), flag);
    }
  }
  return true;
}


// ES5 section 15.2.3.2.
function ObjectGetPrototypeOf(obj) {
  if (!IS_SPEC_OBJECT(obj))
    throw MakeTypeError("obj_ctor_property_non_object", ["getPrototypeOf"]);
  return obj.__proto__;
}


// ES5 section 15.2.3.3
function ObjectGetOwnPropertyDescriptor(obj, p) {
  if (!IS_SPEC_OBJECT(obj))
    throw MakeTypeError("obj_ctor_property_non_object", ["getOwnPropertyDescriptor"]);
  var desc = GetOwnProperty(obj, p);
  return FromPropertyDescriptor(desc);
}


// ES5 section 15.2.3.4.
function ObjectGetOwnPropertyNames(obj) {
  if (!IS_SPEC_OBJECT(obj))
    throw MakeTypeError("obj_ctor_property_non_object", ["getOwnPropertyNames"]);

  // Find all the indexed properties.

  // Get the local element names.
  var propertyNames = %GetLocalElementNames(obj);

  // Get names for indexed interceptor properties.
  if (%GetInterceptorInfo(obj) & 1) {
    var indexedInterceptorNames =
        %GetIndexedInterceptorElementNames(obj);
    if (indexedInterceptorNames)
      propertyNames = propertyNames.concat(indexedInterceptorNames);
  }

  // Find all the named properties.

  // Get the local property names.
  propertyNames = propertyNames.concat(%GetLocalPropertyNames(obj));

  // Get names for named interceptor properties if any.

  if (%GetInterceptorInfo(obj) & 2) {
    var namedInterceptorNames =
        %GetNamedInterceptorPropertyNames(obj);
    if (namedInterceptorNames) {
      propertyNames = propertyNames.concat(namedInterceptorNames);
    }
  }

  // Property names are expected to be unique strings.
  var propertySet = {};
  var j = 0;
  for (var i = 0; i < propertyNames.length; ++i) {
    var name = ToString(propertyNames[i]);
    // We need to check for the exact property value since for intrinsic
    // properties like toString if(propertySet["toString"]) will always
    // succeed.
    if (propertySet[name] === true)
      continue;
    propertySet[name] = true;
    propertyNames[j++] = name;
  }
  propertyNames.length = j;

  return propertyNames;
}


// ES5 section 15.2.3.5.
function ObjectCreate(proto, properties) {
  if (!IS_SPEC_OBJECT(proto) && proto !== null) {
    throw MakeTypeError("proto_object_or_null", [proto]);
  }
  var obj = new $Object();
  obj.__proto__ = proto;
  if (!IS_UNDEFINED(properties)) ObjectDefineProperties(obj, properties);
  return obj;
}


// ES5 section 15.2.3.6.
function ObjectDefineProperty(obj, p, attributes) {
  if (!IS_SPEC_OBJECT(obj)) {
    throw MakeTypeError("obj_ctor_property_non_object", ["defineProperty"]);
  }
  var name = ToString(p);
  var desc = ToPropertyDescriptor(attributes);
  DefineOwnProperty(obj, name, desc, true);
  return obj;
}


// ES5 section 15.2.3.7.
function ObjectDefineProperties(obj, properties) {
  if (!IS_SPEC_OBJECT(obj))
    throw MakeTypeError("obj_ctor_property_non_object", ["defineProperties"]);
  var props = ToObject(properties);
  var key_values = [];
  for (var key in props) {
    if (%HasLocalProperty(props, key)) {
      key_values.push(key);
      var value = props[key];
      var desc = ToPropertyDescriptor(value);
      key_values.push(desc);
    }
  }
  for (var i = 0; i < key_values.length; i += 2) {
    var key = key_values[i];
    var desc = key_values[i + 1];
    DefineOwnProperty(obj, key, desc, true);
  }
  return obj;
}


// ES5 section 15.2.3.8.
function ObjectSeal(obj) {
  if (!IS_SPEC_OBJECT(obj)) {
    throw MakeTypeError("obj_ctor_property_non_object", ["seal"]);
  }
  var names = ObjectGetOwnPropertyNames(obj);
  for (var i = 0; i < names.length; i++) {
    var name = names[i];
    var desc = GetOwnProperty(obj, name);
    if (desc.isConfigurable()) desc.setConfigurable(false);
    DefineOwnProperty(obj, name, desc, true);
  }
  return ObjectPreventExtension(obj);
}


// ES5 section 15.2.3.9.
function ObjectFreeze(obj) {
  if (!IS_SPEC_OBJECT(obj)) {
    throw MakeTypeError("obj_ctor_property_non_object", ["freeze"]);
  }
  var names = ObjectGetOwnPropertyNames(obj);
  for (var i = 0; i < names.length; i++) {
    var name = names[i];
    var desc = GetOwnProperty(obj, name);
    if (IsDataDescriptor(desc)) desc.setWritable(false);
    if (desc.isConfigurable()) desc.setConfigurable(false);
    DefineOwnProperty(obj, name, desc, true);
  }
  return ObjectPreventExtension(obj);
}


// ES5 section 15.2.3.10
function ObjectPreventExtension(obj) {
  if (!IS_SPEC_OBJECT(obj)) {
    throw MakeTypeError("obj_ctor_property_non_object", ["preventExtension"]);
  }
  %PreventExtensions(obj);
  return obj;
}


// ES5 section 15.2.3.11
function ObjectIsSealed(obj) {
  if (!IS_SPEC_OBJECT(obj)) {
    throw MakeTypeError("obj_ctor_property_non_object", ["isSealed"]);
  }
  var names = ObjectGetOwnPropertyNames(obj);
  for (var i = 0; i < names.length; i++) {
    var name = names[i];
    var desc = GetOwnProperty(obj, name);
    if (desc.isConfigurable()) return false;
  }
  if (!ObjectIsExtensible(obj)) {
    return true;
  }
  return false;
}


// ES5 section 15.2.3.12
function ObjectIsFrozen(obj) {
  if (!IS_SPEC_OBJECT(obj)) {
    throw MakeTypeError("obj_ctor_property_non_object", ["isFrozen"]);
  }
  var names = ObjectGetOwnPropertyNames(obj);
  for (var i = 0; i < names.length; i++) {
    var name = names[i];
    var desc = GetOwnProperty(obj, name);
    if (IsDataDescriptor(desc) && desc.isWritable()) return false;
    if (desc.isConfigurable()) return false;
  }
  if (!ObjectIsExtensible(obj)) {
    return true;
  }
  return false;
}


// ES5 section 15.2.3.13
function ObjectIsExtensible(obj) {
  if (!IS_SPEC_OBJECT(obj)) {
    throw MakeTypeError("obj_ctor_property_non_object", ["preventExtension"]);
  }
  return %IsExtensible(obj);
}


%SetCode($Object, function(x) {
  if (%_IsConstructCall()) {
    if (x == null) return this;
    return ToObject(x);
  } else {
    if (x == null) return { };
    return ToObject(x);
  }
});

%SetExpectedNumberOfProperties($Object, 4);

// ----------------------------------------------------------------------------


function SetupObject() {
  // Setup non-enumerable functions on the Object.prototype object.
  InstallFunctions($Object.prototype, DONT_ENUM, $Array(
    "toString", ObjectToString,
    "toLocaleString", ObjectToLocaleString,
    "valueOf", ObjectValueOf,
    "hasOwnProperty", ObjectHasOwnProperty,
    "isPrototypeOf", ObjectIsPrototypeOf,
    "propertyIsEnumerable", ObjectPropertyIsEnumerable,
    "__defineGetter__", ObjectDefineGetter,
    "__lookupGetter__", ObjectLookupGetter,
    "__defineSetter__", ObjectDefineSetter,
    "__lookupSetter__", ObjectLookupSetter
  ));
  InstallFunctions($Object, DONT_ENUM, $Array(
    "keys", ObjectKeys,
    "create", ObjectCreate,
    "defineProperty", ObjectDefineProperty,
    "defineProperties", ObjectDefineProperties,
    "freeze", ObjectFreeze,
    "getPrototypeOf", ObjectGetPrototypeOf,
    "getOwnPropertyDescriptor", ObjectGetOwnPropertyDescriptor,
    "getOwnPropertyNames", ObjectGetOwnPropertyNames,
    "isExtensible", ObjectIsExtensible,
    "isFrozen", ObjectIsFrozen,
    "isSealed", ObjectIsSealed,
    "preventExtensions", ObjectPreventExtension,
    "seal", ObjectSeal
  ));
}

SetupObject();


// ----------------------------------------------------------------------------
// Boolean

function BooleanToString() {
  // NOTE: Both Boolean objects and values can enter here as
  // 'this'. This is not as dictated by ECMA-262.
  var b = this;
  if (!IS_BOOLEAN(b)) {
    if (!IS_BOOLEAN_WRAPPER(b)) {
      throw new $TypeError('Boolean.prototype.toString is not generic');
    }
    b = %_ValueOf(b);
  }
  return b ? 'true' : 'false';
}


function BooleanValueOf() {
  // NOTE: Both Boolean objects and values can enter here as
  // 'this'. This is not as dictated by ECMA-262.
  if (!IS_BOOLEAN(this) && !IS_BOOLEAN_WRAPPER(this))
    throw new $TypeError('Boolean.prototype.valueOf is not generic');
  return %_ValueOf(this);
}


// ----------------------------------------------------------------------------


function SetupBoolean() {
  InstallFunctions($Boolean.prototype, DONT_ENUM, $Array(
    "toString", BooleanToString,
    "valueOf", BooleanValueOf
  ));
}

SetupBoolean();

// ----------------------------------------------------------------------------
// Number

// Set the Number function and constructor.
%SetCode($Number, function(x) {
  var value = %_ArgumentsLength() == 0 ? 0 : ToNumber(x);
  if (%_IsConstructCall()) {
    %_SetValueOf(this, value);
  } else {
    return value;
  }
});

%FunctionSetPrototype($Number, new $Number(0));

// ECMA-262 section 15.7.4.2.
function NumberToString(radix) {
  // NOTE: Both Number objects and values can enter here as
  // 'this'. This is not as dictated by ECMA-262.
  var number = this;
  if (!IS_NUMBER(this)) {
    if (!IS_NUMBER_WRAPPER(this))
      throw new $TypeError('Number.prototype.toString is not generic');
    // Get the value of this number in case it's an object.
    number = %_ValueOf(this);
  }
  // Fast case: Convert number in radix 10.
  if (IS_UNDEFINED(radix) || radix === 10) {
    return %_NumberToString(number);
  }

  // Convert the radix to an integer and check the range.
  radix = TO_INTEGER(radix);
  if (radix < 2 || radix > 36) {
    throw new $RangeError('toString() radix argument must be between 2 and 36');
  }
  // Convert the number to a string in the given radix.
  return %NumberToRadixString(number, radix);
}


// ECMA-262 section 15.7.4.3
function NumberToLocaleString() {
  return this.toString();
}


// ECMA-262 section 15.7.4.4
function NumberValueOf() {
  // NOTE: Both Number objects and values can enter here as
  // 'this'. This is not as dictated by ECMA-262.
  if (!IS_NUMBER(this) && !IS_NUMBER_WRAPPER(this))
    throw new $TypeError('Number.prototype.valueOf is not generic');
  return %_ValueOf(this);
}


// ECMA-262 section 15.7.4.5
function NumberToFixed(fractionDigits) {
  var f = TO_INTEGER(fractionDigits);
  if (f < 0 || f > 20) {
    throw new $RangeError("toFixed() digits argument must be between 0 and 20");
  }
  var x = ToNumber(this);
  return %NumberToFixed(x, f);
}


// ECMA-262 section 15.7.4.6
function NumberToExponential(fractionDigits) {
  var f = -1;
  if (!IS_UNDEFINED(fractionDigits)) {
    f = TO_INTEGER(fractionDigits);
    if (f < 0 || f > 20) {
      throw new $RangeError("toExponential() argument must be between 0 and 20");
    }
  }
  var x = ToNumber(this);
  return %NumberToExponential(x, f);
}


// ECMA-262 section 15.7.4.7
function NumberToPrecision(precision) {
  if (IS_UNDEFINED(precision)) return ToString(%_ValueOf(this));
  var p = TO_INTEGER(precision);
  if (p < 1 || p > 21) {
    throw new $RangeError("toPrecision() argument must be between 1 and 21");
  }
  var x = ToNumber(this);
  return %NumberToPrecision(x, p);
}


// ----------------------------------------------------------------------------

function SetupNumber() {
  %OptimizeObjectForAddingMultipleProperties($Number.prototype, 8);
  // Setup the constructor property on the Number prototype object.
  %SetProperty($Number.prototype, "constructor", $Number, DONT_ENUM);

  %OptimizeObjectForAddingMultipleProperties($Number, 5);
  // ECMA-262 section 15.7.3.1.
  %SetProperty($Number,
               "MAX_VALUE",
               1.7976931348623157e+308,
               DONT_ENUM | DONT_DELETE | READ_ONLY);

  // ECMA-262 section 15.7.3.2.
  %SetProperty($Number, "MIN_VALUE", 5e-324, DONT_ENUM | DONT_DELETE | READ_ONLY);

  // ECMA-262 section 15.7.3.3.
  %SetProperty($Number, "NaN", $NaN, DONT_ENUM | DONT_DELETE | READ_ONLY);

  // ECMA-262 section 15.7.3.4.
  %SetProperty($Number,
               "NEGATIVE_INFINITY",
               -1/0,
               DONT_ENUM | DONT_DELETE | READ_ONLY);

  // ECMA-262 section 15.7.3.5.
  %SetProperty($Number,
               "POSITIVE_INFINITY",
               1/0,
               DONT_ENUM | DONT_DELETE | READ_ONLY);
  %ToFastProperties($Number);

  // Setup non-enumerable functions on the Number prototype object.
  InstallFunctions($Number.prototype, DONT_ENUM, $Array(
    "toString", NumberToString,
    "toLocaleString", NumberToLocaleString,
    "valueOf", NumberValueOf,
    "toFixed", NumberToFixed,
    "toExponential", NumberToExponential,
    "toPrecision", NumberToPrecision
  ));
}

SetupNumber();


// ----------------------------------------------------------------------------
// Function

$Function.prototype.constructor = $Function;

function FunctionSourceString(func) {
  if (!IS_FUNCTION(func)) {
    throw new $TypeError('Function.prototype.toString is not generic');
  }

  var source = %FunctionGetSourceCode(func);
  if (!IS_STRING(source) || %FunctionIsBuiltin(func)) {
    var name = %FunctionGetName(func);
    if (name) {
      // Mimic what KJS does.
      return 'function ' + name + '() { [native code] }';
    } else {
      return 'function () { [native code] }';
    }
  }

  var name = %FunctionGetName(func);
  return 'function ' + name + source;
}


function FunctionToString() {
  return FunctionSourceString(this);
}


// ES5 15.3.4.5
function FunctionBind(this_arg) { // Length is 1.
  if (!IS_FUNCTION(this)) {
      throw new $TypeError('Bind must be called on a function');
  }
  // this_arg is not an argument that should be bound.
  var argc_bound = (%_ArgumentsLength() || 1) - 1;
  var fn = this;
  if (argc_bound == 0) {
    var result = function() {
      if (%_IsConstructCall()) {
        // %NewObjectFromBound implicitly uses arguments passed to this
        // function. We do not pass the arguments object explicitly to avoid
        // materializing it and guarantee that this function will be optimized.
        return %NewObjectFromBound(fn, null);
      }

      return fn.apply(this_arg, arguments);
    };
  } else {
    var bound_args = new $Array(argc_bound);
    for(var i = 0; i < argc_bound; i++) {
      bound_args[i] = %_Arguments(i+1);
    }

    var result = function() {
      // If this is a construct call we use a special runtime method
      // to generate the actual object using the bound function.
      if (%_IsConstructCall()) {
        // %NewObjectFromBound implicitly uses arguments passed to this
        // function. We do not pass the arguments object explicitly to avoid
        // materializing it and guarantee that this function will be optimized.
        return %NewObjectFromBound(fn, bound_args);
      }

      // Combine the args we got from the bind call with the args
      // given as argument to the invocation.
      var argc = %_ArgumentsLength();
      var args = new $Array(argc + argc_bound);
      // Add bound arguments.
      for (var i = 0; i < argc_bound; i++) {
        args[i] = bound_args[i];
      }
      // Add arguments from call.
      for (var i = 0; i < argc; i++) {
        args[argc_bound + i] = %_Arguments(i);
      }
      return fn.apply(this_arg, args);
    };
  }

  // We already have caller and arguments properties on functions,
  // which are non-configurable. It therefore makes no sence to
  // try to redefine these as defined by the spec. The spec says
  // that bind should make these throw a TypeError if get or set
  // is called and make them non-enumerable and non-configurable.
  // To be consistent with our normal functions we leave this as it is.

  // Set the correct length.
  var length = (this.length - argc_bound) > 0 ? this.length - argc_bound : 0;
  %FunctionSetLength(result, length);

  return result;
}


function NewFunction(arg1) {  // length == 1
  var n = %_ArgumentsLength();
  var p = '';
  if (n > 1) {
    p = new $Array(n - 1);
    for (var i = 0; i < n - 1; i++) p[i] = %_Arguments(i);
    p = Join(p, n - 1, ',', NonStringToString);
    // If the formal parameters string include ) - an illegal
    // character - it may make the combined function expression
    // compile. We avoid this problem by checking for this early on.
    if (p.indexOf(')') != -1) throw MakeSyntaxError('unable_to_parse',[]);
  }
  var body = (n > 0) ? ToString(%_Arguments(n - 1)) : '';
  var source = '(function(' + p + ') {\n' + body + '\n})';

  // The call to SetNewFunctionAttributes will ensure the prototype
  // property of the resulting function is enumerable (ECMA262, 15.3.5.2).
  var f = %CompileString(source)();
  %FunctionSetName(f, "anonymous");
  return %SetNewFunctionAttributes(f);
}

%SetCode($Function, NewFunction);

// ----------------------------------------------------------------------------

function SetupFunction() {
  InstallFunctions($Function.prototype, DONT_ENUM, $Array(
    "bind", FunctionBind,
    "toString", FunctionToString
  ));
}

SetupFunction();
