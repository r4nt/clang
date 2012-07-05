//=- tools/fix-llvm-style/FixLLVMStyle.cpp - Automatic LLVM style correction =//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// FIXME: This is an early first draft that needs clean-up.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/FrontendActions.h"
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
      : Replace(Replace), EditFilesExpression(".*/ASTMatchers/.*") {}

  virtual void run(const ast_matchers::MatchFinder::MatchResult &Result) {
    if (Result.Nodes.getStmtAs<CallExpr>("call")) {
   /*   llvm::errs() << "Skipping: "
                   << GetText(*Result.SourceManager, *Call) << "\n";*/
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
        if (Name == "true") Name = "anything";
        if (Name == "not") Name = "unless";
        if (Name == "class") Name = "record";
        if (Name == "do") Name = "doStmt";
        if (Name == "for") Name = "forStmt";
        if (Name == "if") Name = "ifStmt";
        if (Name == "while") Name = "whileStmt";

        if (const DeclRefExpr *Reference = Result.Nodes.getStmtAs<DeclRefExpr>("ref")) {
          ReplaceText = Replacement(*Result.SourceManager, CharSourceRange::getTokenRange(SourceRange(Reference->getLocation(), Reference->getLocation())), Name);
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
        } else if (const VarDecl *Var = llvm::dyn_cast<VarDecl>(Declaration)) {
      llvm::outs() << "Here " << Name << "\n";
          ReplaceText = Replacement(*Result.SourceManager, CharSourceRange::getTokenRange(SourceRange(Var->getLocation(), Var->getLocation())), Name);
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
    //if (EditFilesExpression.match(ReplaceText.getFilePath())) {
      //llvm::errs() << GetPosition(*Result.Nodes.GetDeclAs<NamedDecl>("declaration"), *Result.SourceManager) << "\n";
      //llvm::errs
//      llvm::outs() << ReplaceText.getFilePath() << ":" << ReplaceText.getOffset() << ", " << ReplaceText.getLength() << ": s/" << OldName << "/" << Name << "/g;\n";
      Replace->insert(ReplaceText);
    //} else {
//     llvm::errs() << ReplaceText.GetFilePath() << ":" << ReplaceText.GetOffset() << ", " << ReplaceText.GetLength() << ": s/" << OldName << "/" << Name << "/g;\n";
    //}
  }

 private:
  tooling::Replacements *Replace;
  llvm::Regex EditFilesExpression;
};
} // end namespace

const internal::VariadicDynCastAllOfMatcher<clang::Decl, clang::UsingDecl> UsingDeclaration;
namespace clang { namespace ast_matchers {
AST_MATCHER_P(clang::UsingDecl, hasAnyUsingShadowDeclaration,
              internal::Matcher<clang::UsingShadowDecl>, InnerMatcher) {
  for (clang::UsingDecl::shadow_iterator I = Node.shadow_begin();
       I != Node.shadow_end(); ++I) {
    if (InnerMatcher.matches(**I, Finder, Builder)) {
      return true;
    }
  }
  return false;
}
AST_MATCHER_P(clang::UsingShadowDecl, hasTargetDeclaration,
              internal::Matcher<clang::NamedDecl>, InnerMatcher) {
  return InnerMatcher.matches(*Node.getTargetDecl(), Finder, Builder);
}
AST_MATCHER_P(clang::QualType, hasClassDeclaration,
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
AST_MATCHER_P(clang::FunctionDecl, hasReturnType,
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
/*    T->desugar().dump();
    llvm::errs() <<  T->desugar()->getTypeClass() << "\n";
    const clang::TemplateSpecializationType* T2 = T->desugar()->getAs<clang::TemplateSpecializationType>();
    if (T2 != NULL) {
      T2->desugar().dump();
    }*/

  }
  return InnerMatcher.matches(Node.getResultType(), Finder, Builder);
}
AST_MATCHER_P(clang::NamedDecl, hasName2, std::string, name) {
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

  FixLLVMStyle Callback(&Tool.getReplacements());
  Finder.addMatcher(StatementMatcher(anyOf(
      StatementMatcher(id("ref", declarationReference(to(id("declaration", 
                DeclarationMatcher(anyOf(function(), variable(
                      hasType(record(isDerivedFrom("VariadicFunction"))))))))))),
      call(callee(id("declaration", function())),
           callee(id("callee", expression()))))),
      &Callback);

  Finder.addMatcher(
      DeclarationMatcher(anyOf(
        id("declaration", UsingDeclaration(hasAnyUsingShadowDeclaration(hasTargetDeclaration(function())))),
        allOf(
          id("declaration", DeclarationMatcher(anyOf(function(), variable(hasType(record(isDerivedFrom("VariadicFunction"))))))),
          unless(constructor())))
        ),
      &Callback);
  return Tool.run(newFrontendActionFactory(&Finder));
}

