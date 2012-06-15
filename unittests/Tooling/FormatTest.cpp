//===- unittest/Tooling/FormatTest.cpp - Formatting unit tests ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RewriterTestContext.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Format.h"
#include "gtest/gtest.h"

namespace clang {
namespace tooling {

class  FormatTest : public ::testing::Test {
protected:
  std::string Format(llvm::StringRef Code, unsigned offset, unsigned length) {
    RewriterTestContext Context;
    FileID ID = Context.createInMemoryFile("input.cc", Code);
    std::vector<CodeRange> Ranges(1, CodeRange(offset, length));
    Lexer Lex(ID, Context.Sources.getBuffer(ID), Context.Sources, LangOptions());
    Replacements Replace = reformat(Lex, Context.Sources, Ranges);
    EXPECT_TRUE(applyAllReplacements(Replace, Context.Rewrite));
    return Context.getRewrittenText(ID);
  }
};

TEST_F(FormatTest, DoesNotChangeCorrectlyFormatedCode) {
  EXPECT_EQ(";", Format(";", 0, 1));
}

TEST_F(FormatTest, FormatsGlobalStatementsAt0) {
  EXPECT_EQ("int i;", Format("  int i;", 0, 1));
  EXPECT_EQ("\nint i;", Format(" \n\t \r  int i;", 0, 1));
  EXPECT_EQ("int i; int j;", Format("    int i; int j;", 0, 1));
  EXPECT_EQ("int i;\nint j;", Format("    int i;\n  int j;", 0, 1));
}

TEST_F(FormatTest, FormatsContinuationsAtFirstFormat) {
  EXPECT_EQ("int\n  i;", Format("int\ni;", 0, 1));
}

} // end namespace tooling
} // end namespace clang
