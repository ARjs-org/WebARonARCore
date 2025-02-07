/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "modules/indexeddb/IDBKeyPath.h"

#include "bindings/core/v8/SerializedScriptValue.h"
#include "bindings/modules/v8/V8BindingForModules.h"
#include "modules/indexeddb/IDBKey.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "wtf/Vector.h"

namespace blink {
namespace {

void checkKeyPath(const String& keyPath,
                  const Vector<String>& expected,
                  int parserError) {
  IDBKeyPath idbKeyPath(keyPath);
  ASSERT_EQ(idbKeyPath.getType(), IDBKeyPath::StringType);
  ASSERT_EQ(idbKeyPath.isValid(), (parserError == IDBKeyPathParseErrorNone));

  IDBKeyPathParseError error;
  Vector<String> keyPathElements;
  IDBParseKeyPath(keyPath, keyPathElements, error);
  ASSERT_EQ(parserError, error);
  if (error != IDBKeyPathParseErrorNone)
    return;
  ASSERT_EQ(expected.size(), keyPathElements.size());
  for (size_t i = 0; i < expected.size(); ++i)
    ASSERT_TRUE(expected[i] == keyPathElements[i]) << i;
}

TEST(IDBKeyPathTest, ValidKeyPath0) {
  Vector<String> expected;
  String keyPath("");
  checkKeyPath(keyPath, expected, IDBKeyPathParseErrorNone);
}

TEST(IDBKeyPathTest, ValidKeyPath1) {
  Vector<String> expected;
  String keyPath("foo");
  expected.push_back(String("foo"));
  checkKeyPath(keyPath, expected, IDBKeyPathParseErrorNone);
}

TEST(IDBKeyPathTest, ValidKeyPath2) {
  Vector<String> expected;
  String keyPath("foo.bar.baz");
  expected.push_back(String("foo"));
  expected.push_back(String("bar"));
  expected.push_back(String("baz"));
  checkKeyPath(keyPath, expected, IDBKeyPathParseErrorNone);
}

TEST(IDBKeyPathTest, InvalidKeyPath0) {
  Vector<String> expected;
  String keyPath(" ");
  checkKeyPath(keyPath, expected, IDBKeyPathParseErrorIdentifier);
}

TEST(IDBKeyPathTest, InvalidKeyPath1) {
  Vector<String> expected;
  String keyPath("+foo.bar.baz");
  checkKeyPath(keyPath, expected, IDBKeyPathParseErrorIdentifier);
}

TEST(IDBKeyPathTest, InvalidKeyPath2) {
  Vector<String> expected;
  String keyPath("foo bar baz");
  expected.push_back(String("foo"));
  checkKeyPath(keyPath, expected, IDBKeyPathParseErrorIdentifier);
}

TEST(IDBKeyPathTest, InvalidKeyPath3) {
  Vector<String> expected;
  String keyPath("foo .bar .baz");
  expected.push_back(String("foo"));
  checkKeyPath(keyPath, expected, IDBKeyPathParseErrorIdentifier);
}

TEST(IDBKeyPathTest, InvalidKeyPath4) {
  Vector<String> expected;
  String keyPath("foo. bar. baz");
  expected.push_back(String("foo"));
  checkKeyPath(keyPath, expected, IDBKeyPathParseErrorIdentifier);
}

TEST(IDBKeyPathTest, InvalidKeyPath5) {
  Vector<String> expected;
  String keyPath("foo..bar..baz");
  expected.push_back(String("foo"));
  checkKeyPath(keyPath, expected, IDBKeyPathParseErrorIdentifier);
}

}  // namespace
}  // namespace blink
