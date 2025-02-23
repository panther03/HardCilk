#pragma once

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceManager.h>

#include <iostream>

#include "IR.hpp"

class Cilk2IRVisitor : public clang::RecursiveASTVisitor<Cilk2IRVisitor> {
public:
  explicit Cilk2IRVisitor(clang::ASTContext *Context, IRProgram &P) : Context(Context), P(P) {}

  bool VisitFunctionDecl(clang::FunctionDecl *Decl) {
    std::unique_ptr<IRFunction> f = std::make_unique<IRFunction>();
    TraverseContext.func = f.get();
    std::cout << (TraverseContext.func == NULL) << std::endl;
    P.newFunction(std::move(f));
    std::cout << "test1" << std::endl;
    return true;
  }

  bool VisitReturnStmt(const ReturnStmt *stmt) {
    std::unique_ptr<IRStmt> s = std::make_unique<IRStmt>(stmt);
    if (TraverseContext.func) { 
    std::cout << "test2" << std::endl;
      TraverseContext.func->newStmt(std::move(s));
    }

    return true;
  }

private:
  clang::ASTContext *Context;
  struct {
    IRFunction* func;
  } TraverseContext;
  IRProgram &P;
};