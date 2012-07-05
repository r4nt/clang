//=- tools/rename/ClangRename.cpp - Rename Declarations =//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Rename takes a declaration under a cursor (whose lines and column are 
// specified by the -l and -c flags) respectively and renames them to the 
// target text -t. 
// 
// 
// FIXME: This is an early first draft that needs clean-up.
//
//===----------------------------------------------------------------------===//

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceManager.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/system_error.h"

#include "clang/AST/DeclTemplate.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace llvm;
using clang::tooling::newFrontendActionFactory;
using clang::tooling::Replacement;
using clang::tooling::CompilationDatabase;

template <typename T>
class TestVisitor : public clang::RecursiveASTVisitor<T> {
public:
  clang::ASTConsumer *newASTConsumer() { return new FindConsumer(this); }

  bool shouldVisitTemplateInstantiations() const {
    return true;
  }

protected:
  clang::ASTContext *Context;

private:
  class FindConsumer : public clang::ASTConsumer {
  public:
    FindConsumer(TestVisitor *Visitor) : Visitor(Visitor) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
      Visitor->Context = &Context;
      Visitor->TraverseDecl(Context.getTranslationUnitDecl());
    }

  private:
    TestVisitor *Visitor;
  };
};

struct Xaver {
  void rvgl();
  void c() {
    rvgl();
  }
};

class Yaver : public Xaver {
  using Xaver::rvgl;

  void d() {
    rvgl();
  }
};

cl::opt<std::string> LocationPath("f", cl::desc("<location-path>"));
cl::opt<std::string> RenameTo("t", cl::desc("<rename-to>"));
cl::opt<unsigned> LocationLine("l", cl::desc("<location-line>"));
cl::opt<unsigned> LocationColumn("c", cl::desc("<location-column>"));

SourceRange getIdentifierRange(Decl* D) {
  SourceRange TokenRange;
  if (FunctionDecl *F = dyn_cast<FunctionDecl>(D)) {
    TokenRange = F->getNameInfo().getSourceRange();
  } else if (VarDecl *V = dyn_cast<VarDecl>(D)) {
    TokenRange = SourceRange(V->getLocation(), V->getLocation());
  } else if (UsingDecl *U = dyn_cast<UsingDecl>(D)) {
    TokenRange = U->getNameInfo().getSourceRange();
  } else {
/*    llvm::errs() << "Not supported:\n";
    D->dump();
    llvm::errs() << "\n";*/
    return SourceRange();
  }
  return TokenRange;
}

struct ExpandedIdentifierRange {
  ExpandedIdentifierRange() : Line(0), StartColumn(0), EndColumn(0) {}
  std::string File;
  unsigned Line;
  unsigned StartColumn;
  unsigned EndColumn;
};

ExpandedIdentifierRange getExpandedIdentifierRange(SourceLocation L, ASTContext *Context) {
  SourceLocation S = Context->getSourceManager().getSpellingLoc(L);
  FileID ID = Context->getSourceManager().getFileID(S);
  const FileEntry *Entry = Context->getSourceManager().getFileEntryForID(ID);
  ExpandedIdentifierRange IR;
  if (Entry == NULL)
    return IR;
  IR.Line = Context->getSourceManager().getSpellingLineNumber(S);
  IR.StartColumn = Context->getSourceManager().getSpellingColumnNumber(S);
  IR.File = Entry->getName();
  IR.EndColumn = IR.StartColumn + Lexer::MeasureTokenLength(S, Context->getSourceManager(), LangOptions());
  return IR;
}

std::string getDeclarationKey(Decl *DL, ASTContext *Context) {
  Decl *D = DL->getCanonicalDecl();
  while (UsingDecl *U = dyn_cast<UsingDecl>(D)) {
    // FIXME: If there are multiple shadow decls, we need to get that information up and warn / error.
    D = (*U->shadow_begin())->getTargetDecl()->getCanonicalDecl();
  }
  ExpandedIdentifierRange R = getExpandedIdentifierRange(getIdentifierRange(D).getBegin(), Context);
  std::string Location;
  if (R.File.empty())
    return Location;
  llvm::raw_string_ostream S(Location);
  S << R.File << ":" << R.Line << ":" << R.StartColumn;
  S.flush();
  return Location;
}

bool UnderCursor(SourceLocation IR, ASTContext *Context) {
  ExpandedIdentifierRange R = getExpandedIdentifierRange(IR, Context);
  if (R.File.empty())
    return false;
//  llvm::outs() << R.File << ":" << R.Line << ":" << R.StartColumn << "\n";
  StringRef FileName(R.File);
  return FileName.endswith(LocationPath) && R.Line == LocationLine && R.StartColumn <= LocationColumn && LocationColumn <= R.EndColumn;
}

class GetIdentifierAtVisitor : public TestVisitor<GetIdentifierAtVisitor> {
public:
//  bool TraverseDeclarationNameInfo(DeclarationNameInfo NI) {
  bool VisitDeclRefExpr(DeclRefExpr *DR) {
    if (UnderCursor(DR->getLocation(), Context))
      Key = getDeclarationKey(DR->getDecl(), Context);
    return true;
  }

  bool VisitDecl(Decl *D) {
    if (UnderCursor(getIdentifierRange(D).getBegin(), Context))
      Key = getDeclarationKey(D, Context);
    return true;
  }

  bool VisitMemberExpr(MemberExpr *C) {
    if (UnderCursor(C->getMemberLoc(), Context))
      Key = getDeclarationKey(C->getMemberDecl(), Context);
    return true;
  }

  std::string Key;
};

class RenameIdentifierVisitor : public TestVisitor<RenameIdentifierVisitor> {
public:
  RenameIdentifierVisitor(std::string Key, tooling::Replacements *Replacements) : Key(Key), Replacements(Replacements) {}

  bool VisitDeclRefExpr(DeclRefExpr *DR) {
    if (getDeclarationKey(DR->getDecl(), Context) == Key) {
      llvm::outs() << Key << "\n";
      Replacement R(Context->getSourceManager(), DR, RenameTo);
      llvm::outs() << R.getFilePath() << ":" << R.getOffset() << ":" << R.getLength() << "\n";
      Replacements->insert(R);
    }
    return true;
  }

  bool VisitDecl(Decl *D) {
    if (getDeclarationKey(D, Context) == Key) {
      Replacement R(Context->getSourceManager(), CharSourceRange::getTokenRange(SourceRange(D->getLocation())), RenameTo);
      llvm::outs() << R.getFilePath() << ":" << R.getOffset() << ":" << R.getLength() << "\n";
      Replacements->insert(R);
    }
    return true;
  }

  bool VisitMemberExpr(MemberExpr *C) {
    if (getDeclarationKey(C->getMemberDecl(), Context) == Key) {
      Replacement R(Context->getSourceManager(), CharSourceRange::getTokenRange(SourceRange(C->getMemberLoc())), RenameTo);
      llvm::outs() << R.getFilePath() << ":" << R.getOffset() << ":" << R.getLength() << "\n";
      Replacements->insert(R);
    }
    return true;
  }

  std::string Key;
  tooling::Replacements *Replacements;
};

cl::opt<std::string> BuildPath(
  cl::Positional,
  cl::desc("<build-path>"));

cl::list<std::string> SourcePaths(
  cl::Positional,
  cl::desc("<source0> [... <sourceN>]"),
  cl::OneOrMore);

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv);
  std::string ErrorMessage;
  llvm::OwningPtr<CompilationDatabase> Compilations(
    CompilationDatabase::loadFromDirectory(BuildPath, ErrorMessage));
  if (!Compilations)
    llvm::report_fatal_error(ErrorMessage);
  tooling::RefactoringTool Tool(*Compilations, SourcePaths);
  GetIdentifierAtVisitor GetIdentifierVisitor;
  Tool.run(newFrontendActionFactory(&GetIdentifierVisitor));
  llvm::outs() << GetIdentifierVisitor.Key << "\n";
  if (GetIdentifierVisitor.Key.empty()) {
    llvm::errs() << "No key to replace, is the specified cursor location valid?\n";
    return -1;
  }
  RenameIdentifierVisitor Renamer(GetIdentifierVisitor.Key, &Tool.getReplacements());
  return Tool.run(newFrontendActionFactory(&Renamer));
}

/*
// FIXME: Pull out helper methods in here into more fitting places.

template <typename T>
std::string getFile(const clang::SourceManager& source_manager, const T& node) {
  clang::SourceLocation start_spelling_location =
      source_manager.getSpellingLoc(node.getLocStart());
  if (!start_spelling_location.isValid()) return std::string();
  clang::FileID file_id = source_manager.getFileID(start_spelling_location);
  const clang::FileEntry* file_entry =
      source_manager.getFileEntryForID(file_id);
  if (file_entry == NULL) return std::string();
  return file_entry->getName();
}

// Returns the text that makes up 'node' in the source.
// Returns an empty string if the text cannot be found.
static std::string getText(const SourceManager &SourceManager,
                           SourceLocation LocStart, SourceLocation LocEnd) {
  SourceLocation StartSpellingLocatino =
      SourceManager.getSpellingLoc(LocStart);
  SourceLocation EndSpellingLocation =
      SourceManager.getSpellingLoc(LocEnd);
  if (!StartSpellingLocatino.isValid() || !EndSpellingLocation.isValid()) {
    return std::string();
}
  bool Invalid = true;
  const char *Text =
    SourceManager.getCharacterData(StartSpellingLocatino, &Invalid);
  if (Invalid) {
    return std::string();
  }
  std::pair<FileID, unsigned> Start =
      SourceManager.getDecomposedLoc(StartSpellingLocatino);
  std::pair<FileID, unsigned> End =
      SourceManager.getDecomposedLoc(Lexer::getLocForEndOfToken(
          EndSpellingLocation, 0, SourceManager, LangOptions()));
  if (Start.first != End.first) {
    // Start and end are in different files.
    return std::string();
  }
  if (End.second < Start.second) {
    // Shuffling text with macros may cause this.
    return std::string();
  }
  return std::string(Text, End.second - Start.second);
}

template <typename T>
static std::string getText(const SourceManager &SourceManager, const T &Node) {
  return GetText(SourceManager, Node.getLocStart(), Node.getLocEnd());
}

namespace {

bool hasMethod(const CXXRecordDecl &Decl, StringRef MethodName, ASTContext &Context) {
  clang::IdentifierInfo& Identifier = Context.Idents.get(MethodName);
  clang::DeclContext::lookup_const_result Result = Decl.lookup(&Identifier);
  return Result.first != Result.second;
}

bool allParentsMatch(SourceManager *SM, ASTContext &Context, const CXXRecordDecl *Decl, StringRef MethodName, llvm::Regex &EditFilesExpression) {
  if (Decl == NULL)
    return true;
  if (!EditFilesExpression.match(getFile(*SM, *Decl)) &&
      hasMethod(*Decl, MethodName, Context)) {
    //llvm::outs() << GetFile(*SM, *Decl) << "\n";
    return false;
  }
  typedef clang::CXXRecordDecl::base_class_const_iterator BaseIterator;
  for (BaseIterator It = Decl->bases_begin(),
                    End = Decl->bases_end(); It != End; ++It) {
    const clang::Type *TypeNode = It->getType().getTypePtr();
    clang::CXXRecordDecl *
      ClassDecl = TypeNode->getAsCXXRecordDecl();
    if (!allParentsMatch(SM, Context, ClassDecl, MethodName, EditFilesExpression)) {
      return false;
    }
  }
  return true;
}

class FixLLVMStyle: public ast_matchers::MatchFinder::MatchCallback {
 public:
  FixLLVMStyle(tooling::Replacements *Replace)
      : Replace(Replace), 

  virtual void run(const ast_matchers::MatchFinder::MatchResult &Result) {
    if (const CallExpr *Call = Result.Nodes.getStmtAs<CallExpr>("call")) {
      return;
    }
    Replacement ReplaceText;
    std::string Name;
    std::string OldName;
    std::string SedCommand;
    if (const NamedDecl *Declaration =
          Result.Nodes.getDeclAs<NamedDecl>("declaration")) {
      if (!EditFilesExpression.match(getFile(*Result.SourceManager, *Declaration)))
        return;
      Name = Declaration->getNameAsString();
      OldName = Name;
      if (const CXXMethodDecl *Method =
            llvm::dyn_cast<CXXMethodDecl>(Declaration)) {
        if (//Method->size_overridden_methods() > 0 &&
            !allParentsMatch(Result.SourceManager, *Result.Context, Method->getParent(), OldName, EditFilesExpression)) {
 //         llvm::errs() << "NotAllParentsMatch: " << OldName << "\n";
          return;
        }
      }
      if (isupper(Name[0])) {
        Name[0] = tolower(Name[0]);
        if (Name == "new") Name = "create";

        if (const DeclRefExpr *Reference = Result.Nodes.getStmtAs<DeclRefExpr>("ref")) {
          ReplaceText = Replacement(*Result.SourceManager, Reference, Name);
        } else if (const Expr *Callee = Result.Nodes.getStmtAs<Expr>("callee")) {
          if (const MemberExpr *Member = dyn_cast<MemberExpr>(Callee)) {
  //          llvm::errs() << OldName << "\n";
            assert(Member != NULL);
//          std::string CalleeText = GetText(*Result.SourceManager, *Callee);
//          llvm::outs() << "Callee: " << CalleeText << "\n";
//          std::string ReplacementText =
//            Name + CalleeText.substr(OldName.size(), CalleeText.size() - OldName.size());
            ReplaceText = Replacement(*Result.SourceManager, CharSourceRange::getTokenRange(SourceRange(Member->getMemberLoc(), Member->getMemberLoc())),
                                      Name);
          } else if (const DeclRefExpr *Ref = dyn_cast<DeclRefExpr>(Callee)) {
            (void)Ref;
    //        llvm::errs() << "XXX " << GetFile(*Result.SourceManager, *Callee) << "\n";
          } else {
    //        llvm::errs() << "*** " << GetFile(*Result.SourceManager, *Callee) << "\n";
            //Callee->dump();
          }
        } else {
          DeclarationNameInfo NameInfo;
          if (const FunctionDecl *Function = llvm::dyn_cast<FunctionDecl>(Declaration)) {
            NameInfo = Function->getNameInfo();
          } else if (const UsingDecl *Using = llvm::dyn_cast<UsingDecl>(Declaration)) {
            NameInfo = Using->getNameInfo();
          }
          ReplaceText = Replacement(*Result.SourceManager, &NameInfo, Name);
          if (!ReplaceText.isApplicable()) {
   //         llvm::errs() << "Not applicable: " << Name << "\n";
          }
        }
      }
    }
    if (EditFilesExpression.match(ReplaceText.getFilePath())) {
      //llvm::errs() << GetPosition(*Result.Nodes.GetDeclAs<NamedDecl>("declaration"), *Result.SourceManager) << "\n";
      //llvm::errs
      llvm::errs() << ReplaceText.getFilePath() << ":" << ReplaceText.getOffset() << ", " << ReplaceText.getLength() << ": s/" << OldName << "/" << Name << "/g;\n";
      Replace->insert(ReplaceText);
    } else {
//     llvm::errs() << ReplaceText.GetFilePath() << ":" << ReplaceText.GetOffset() << ", " << ReplaceText.GetLength() << ": s/" << OldName << "/" << Name << "/g;\n";
    }
  }

 private:
  tooling::Replacements *Replace;
  llvm::Regex EditFilesExpression;
};
} // end namespace

const internal::VariadicDynCastAllOfMatcher<clang::Decl, clang::UsingDecl> UsingDeclaration;
namespace clang { namespace ast_matchers {
AST_MATCHER_P(clang::UsingDecl, HasAnyUsingShadowDeclaration,
              internal::Matcher<clang::UsingShadowDecl>, InnerMatcher) {
  for (clang::UsingDecl::shadow_iterator I = Node.shadow_begin();
       I != Node.shadow_end(); ++I) {
    if (InnerMatcher.matches(**I, Finder, Builder)) {
      return true;
    }
  }
  return false;
}
AST_MATCHER_P(clang::UsingShadowDecl, HasTargetDeclaration,
              internal::Matcher<clang::NamedDecl>, InnerMatcher) {
  return InnerMatcher.matches(*Node.getTargetDecl(), Finder, Builder);
}
AST_MATCHER_P(clang::QualType, HasClassDeclaration,
              internal::Matcher<clang::CXXRecordDecl>, InnerMatcher) {
  if (const clang::CXXRecordDecl *Decl = Node->getAsCXXRecordDecl()) {
    return InnerMatcher.matches(*Decl, Finder, Builder);
  }
  if (const clang::TemplateSpecializationType *T = Node->getAs<clang::TemplateSpecializationType>()) {
    if (const clang::NamedDecl *N = T->getTemplateName().getAsTemplateDecl()->getTemplatedDecl()) {
      if (const clang::CXXRecordDecl *Decl = dyn_cast<CXXRecordDecl>(N)) {
        return InnerMatcher.matches(*Decl, Finder, Builder);
      }
    }
  }
  return false;
}
AST_MATCHER_P(clang::FunctionDecl, HasReturnType,
              internal::Matcher<clang::QualType>, InnerMatcher) {
  llvm::errs() << Node.getNameAsString() << "\n";
  Node.getResultType().dump();
  llvm::errs() << "\n";
  const clang::TemplateSpecializationType* T = Node.getResultType()->getAs<clang::TemplateSpecializationType>();
  if (T != NULL) {
    const NamedDecl *N = T->getTemplateName().getAsTemplateDecl()->getTemplatedDecl();
    
    if (N != NULL) {
      llvm::errs() << dyn_cast<CXXRecordDecl>(N) << "\n";
    }

  }
  return InnerMatcher.matches(Node.getResultType(), Finder, Builder);
}
AST_MATCHER_P(clang::NamedDecl, HasName2, std::string, name) {
  assert(!name.empty());
  const std::string full_name_string = "::" + Node.getQualifiedNameAsString();
  const llvm::StringRef full_name = full_name_string;
  llvm::errs() << full_name << "\n";
  const llvm::StringRef pattern = name;
  if (pattern.startswith("::")) {
    return full_name == pattern;
  } else {
    return full_name.endswith(("::" + pattern).str());
  }
}
} }


cl::opt<std::string> BuildPath(
  cl::Positional,
  cl::desc("<build-path>"));

cl::list<std::string> SourcePaths(
  cl::Positional,
  cl::desc("<source0> [... <sourceN>]"),
  cl::OneOrMore);

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv);
  std::string ErrorMessage;
  llvm::OwningPtr<CompilationDatabase> Compilations(
    CompilationDatabase::loadFromDirectory(BuildPath, ErrorMessage));
  if (!Compilations)
    llvm::report_fatal_error(ErrorMessage);
  tooling::RefactoringTool Tool(*Compilations, SourcePaths);
  ast_matchers::MatchFinder Finder;

  DeclarationMatcher FunctionMatch = Function(Not(HasReturnType(HasClassDeclaration(
    AnyOf(HasName2("internal::Matcher"),
                HasName("internal::PolymorphicMatcherWithParam0"),
                HasName("internal::PolymorphicMatcherWithParam1"),
                HasName("internal::PolymorphicMatcherWithParam2")
        )))));

  FixLLVMStyle Callback(&Tool.getReplacements());
  Finder.addMatcher(StatementMatcher(AnyOf(
      StatementMatcher(Id("ref", DeclarationReference(To(Id("declaration", FunctionMatch))))),
      Call(Callee(Id("declaration", FunctionMatch)),
           Callee(Id("callee", Expression()))))),
      &Callback);

  Finder.addMatcher(
      DeclarationMatcher(AnyOf(
        Id("declaration", UsingDeclaration(HasAnyUsingShadowDeclaration(HasTargetDeclaration(FunctionMatch)))),
        AllOf(
          Id("declaration", FunctionMatch),
          Not(Constructor())))
        ),
      &Callback);
  return Tool.run(newFrontendActionFactory(&Finder));
}
*/
