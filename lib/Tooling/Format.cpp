//===--- Format.cpp - Format C++ code -------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Implements Format.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Format.h"

#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

namespace clang {
namespace tooling {

Replacements reformat(Lexer &Lex, SourceManager &Sources,
                      std::vector<CodeRange> Ranges) {
  Replacements Replaces;
  Token tok;
  Lex.SetKeepWhitespaceMode(true);
  unsigned line = 0;
  unsigned indent = 0;
  bool First = true;
  bool Continuation = false;
  while (true) {
    bool end = Lex.LexFromRawLexer(tok);
    SourceLocation loc = tok.getLocation();
    StringRef Data(Sources.getCharacterData(loc), tok.getLength());
    llvm::outs() << tok.getKind() << " " << tok.getName() << " " << tok.getLength() << "\n";
    llvm::outs() << "\"" << Data << "\"\n";
    if (tok.getKind() == tok::semi) {
      Continuation = false;
    } else if (tok.getKind() != tok::unknown) {
      Continuation = true;
    }
    if (Data[0] == '\n' || (First && tok.getKind() == tok::unknown)) {
      std::string rep = First ?  "" : "\n";
      if (Continuation) rep += "  ";
      Replacement Replace(Sources, loc, tok.getLength(), rep);
      Replaces.insert(Replace);
    }

    if (end)
      break;
    First = false;
  }
  return Replaces;
}

}  // namespace tooling
}  // namespace clang
