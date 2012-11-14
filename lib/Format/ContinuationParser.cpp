//===--- ContinuationParser.cpp - Format C++ code -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This is EXPERIMENTAL code under heavy development. It is not in a state yet,
//  where it can be used to format real code.
//
//===----------------------------------------------------------------------===//

#include "ContinuationParser.h"

#include "llvm/Support/raw_ostream.h"

namespace clang {
namespace format {

ContinuationParser::ContinuationParser(Lexer &Lex, SourceManager &Sources,
                                       ContinuationConsumer &Callback)
    : /*StartLevel(0),*/ Level(0), Eof(false), Lex(Lex), Sources(Sources), Callback(Callback) {
  Lex.SetKeepWhitespaceMode(true);
}

void ContinuationParser::parse() {
  parseToken();
  parseLevel();
}

  void ContinuationParser::parseLevel() {
    do {
      switch(current().Tok.getKind()) {
        case tok::hash:
          parsePPDirective();
          break;
        case tok::comment:
          parseComment();
          break;
        case tok::l_brace:
          parseBlock();
          break;
        case tok::r_brace:
          return;
        default:
          parseStatement();
          break;
      }
    } while (!eof());
  }

  void ContinuationParser::parseBlock() {
    nextToken();
    addContinuation();
    ++Level;
    parseLevel();
    --Level;
    if (current().Tok.getKind() != tok::r_brace) abort();
    nextToken();
    addContinuation();
    if (!eof() && current().Tok.getKind() == tok::semi)
      nextToken();
  }

  void ContinuationParser::parsePPDirective() {
    while (nextToken() ) {
      if (current().NewlinesBefore > 0) return;
    }
  }

  void ContinuationParser::parseComment() {
    //size_t Start = Index;
    while (nextToken()) {
      if (current().NewlinesBefore > 0) {
        addContinuation(); //Start, Index - 1, Level);
        return;
      }
    }
  }

  void ContinuationParser::parseStatement() {
    //size_t Start = Index;
    do {
      switch (current().Tok.getKind()) {
        case tok::semi:
          {
            //size_t End = Index;
            llvm::outs() << "Semi\n";
            nextToken();
            addContinuation(); //Start, End, Level);
            return;
          }
        case tok::l_paren:
          parseParens();
          break;
        case tok::l_brace:
          {
            //size_t End = Index;
            parseBlock(  ); // TODO: Test
            return;
          }
        case tok::raw_identifier:
          {
            StringRef Data(Sources.getCharacterData(current().Tok.getLocation()),
                           current().Tok.getLength());
            if (Data == "if") {
              parseIfThenElse();
              return;
            }
          }
        default:
          nextToken();
          break;
      }
    } while (!eof());
  }

  void ContinuationParser::parseParens() {
    if (current().Tok.getKind() != tok::l_paren) abort();
    nextToken();
    do {
      switch (current().Tok.getKind()) {
        case tok::l_paren:
          parseParens();
          break;
        case tok::r_paren:
          nextToken();
          return;
        default:
          nextToken();
          break;
      }
    } while (!eof());
  }

  void ContinuationParser::parseIfThenElse() {
    //size_t Start = Index;
    if (current().Tok.getKind() != tok::raw_identifier) abort();
    if (!nextToken()) return;
    parseParens();
    if (current().Tok.getKind() == tok::l_brace) {
      parseBlock(); // TODO: Level Test
    } else {
      addContinuation(); //Start, Index - 1, Level);
      ++Level;
      parseStatement();
      --Level;
    }
    //if (!nextToken()) return;
    if (current().Tok.getKind() == tok::raw_identifier) {
      StringRef Data(Sources.getCharacterData(current().Tok.getLocation()),
                     current().Tok.getLength());
      if (Data == "else") {
        if (!nextToken()) return;
        parseStatement();
      }
    }
  }

  void ContinuationParser::addContinuation() {
    llvm::outs() << "addContinuation " << Cont.Level << ":";
    for (int i = 0; i < Cont.Tokens.size(); ++i) {
      llvm::outs() << Cont.Tokens[i].Tok.getName() << " ";
    }
    llvm::outs() << "\n";
    /*Continuation Cont;
    Cont.Tokens = Seq;
    Cont.Level = StartLevel;*/
    Cont.Level = Level;
    Callback.formatContinuation(Cont);
    Cont = Continuation();
    /*Seq.clear();
    StartLevel = Level;
    */
    /*
    Continuation Cont;
    Cont.Tokens = ArrayRef<FormatToken>(Tokens).slice(Start, End+1-Start);
    Cont.Level = Level;
    formatContinuation(Cont);
   */ 
  }

bool ContinuationParser::eof() const {
  return current().Tok.getKind() == tok::eof;
}

FormatToken &ContinuationParser::current() {
  return FormatTok;
}

const FormatToken &ContinuationParser::current() const {
  return FormatTok;
}

bool ContinuationParser::nextToken() {
  if (eof()) return false;
  Cont.Tokens.push_back(FormatTok);
  return parseToken();
}

bool ContinuationParser::parseToken() {
  FormatTok = FormatToken();
  Eof = Lex.LexFromRawLexer(current().Tok);
  current().WhiteSpaceStart = current().Tok.getLocation();

  // Consume and record whitespace until we find a 
  while (current().Tok.getKind() == tok::unknown) {
    StringRef Data(Sources.getCharacterData(current().Tok.getLocation()),
                   current().Tok.getLength());
    if (std::find(Data.begin(), Data.end(), '\n') != Data.end())
      ++current().NewlinesBefore;
    current().WhiteSpaceLength += current().Tok.getLength();

    if (eof()) return false;
    Eof = Lex.LexFromRawLexer(current().Tok);
  }
  llvm::outs() << current().Tok.getName() << "\n";
  return true;
}

} // end namespace format
} // end namespace clang
