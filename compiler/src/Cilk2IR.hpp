#pragma once

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Analysis/CFG.h>

#include <iostream>

#include "IR.hpp"
#include "util.hpp"

class Cilk2IRVisitor : public clang::RecursiveASTVisitor<Cilk2IRVisitor> {
private:
  clang::ASTContext *Context;
  struct {
    IRFunction* func;
  } TraverseContext;
  IRProgram &P;
  std::unordered_map<const Stmt*, IRBasicBlock*> Ast2IrDestination;
  const Stmt* lastStmt;

  void functionCFG2IR (CFG* Cfg) {
    std::unordered_map<CFGBlock*, std::pair<IRBasicBlock*, IRBasicBlock*>> Cfg2IRLookup;
    auto *IrF = P.createFunc();
    for (auto *CfgB: *Cfg) {
      auto *IrB = IrF->createBlock();
      auto *IrBStart = IrB;

      for (auto &CfgE: *CfgB) {
        switch (CfgE.getKind()) {
          case CFGElement::Kind::Statement: {
            CFGStmt CfgS = CfgE.castAs<CFGStmt>();
            const Stmt *S = CfgS.getStmt();

            if (isa<CilkSyncStmt>(S)) {
              IrB->Terminator = std::make_unique<IRStmt>(S);
              auto *NewIrB = IrF->createBlock();
              IrB->Succs.insert(NewIrB);
              NewIrB->Preds.insert(IrB);
              IrB = NewIrB;
            } else {
              Ast2IrDestination[S] = IrB;
            }
            
            break;
          }
          default: PANIC("Unsupported CFGElement: %d", CfgE.getKind());
        }
      }
      if (CfgB->getTerminator().isValid()) {
        IrB->Terminator = std::make_unique<IRStmt>(CfgB->getTerminator().getStmt());
      }
      Cfg2IRLookup[CfgB] = std::make_pair(IrBStart, IrB);
    }
    for (auto *CfgB: *Cfg) {
      if (Cfg2IRLookup.find(CfgB) == Cfg2IRLookup.end()) {
        PANIC("not traversed all cfg blocks?");
      }
      auto [block_start, block_end] = Cfg2IRLookup[CfgB];
      for (auto PredI = CfgB->pred_begin(); PredI != CfgB->pred_end();  ++PredI) {
        if (auto *Pred = PredI->getReachableBlock()) {
          if (Cfg2IRLookup.find(Pred) == Cfg2IRLookup.end()) {
            PANIC("predecessor not found in lookup");
          }
          block_start->Preds.insert(Cfg2IRLookup[Pred].second);
        }
      }
      for (auto SuccI = CfgB->succ_begin(); SuccI != CfgB->succ_end();  ++SuccI) {
        if (auto *Succ = SuccI->getReachableBlock()) {
          if (Cfg2IRLookup.find(Succ) == Cfg2IRLookup.end()) {
            PANIC("successor not found in lookup");
          }
          block_end->Succs.insert(Cfg2IRLookup[Succ].first);
        }
      }
    }
  }

public:
  explicit Cilk2IRVisitor(clang::ASTContext *Context, IRProgram &P) : Context(Context), P(P) {}

  bool VisitFunctionDecl(clang::FunctionDecl *Decl) {
    std::cout << "test1" << std::endl;

    CFG::BuildOptions Options;
    auto Cfg = CFG::buildCFG(nullptr, (Decl->getBody()) , Context, Options);
    if (Cfg == nullptr) {
      PANIC("Could not build CFG for function %s", Decl->getName().str().c_str());
      return false;
    }
    functionCFG2IR(Cfg.get());
    Cfg->print(llvm::outs(), Context->getLangOpts(), true);
    Cfg->viewCFG(Context->getLangOpts());
    return true;
  }

  //bool VisitReturnStmt(const ReturnStmt *stmt) {
  //  std::unique_ptr<IRStmt> s = std::make_unique<IRStmt>(stmt);
  //  if (TraverseContext.func) { 
  //  std::cout << "test2" << std::endl;
  //    TraverseContext.func->newStmt(std::move(s));
  //  }
//
  //  return true;
  //}

  bool VisitStmt(const Stmt *Stmt) {
    if (isa<Expr>(Stmt) || isa<DeclStmt>(Stmt) || isa<ReturnStmt>(Stmt)) {
      return true;
    }
    for (const auto *child : Stmt->children()) {
      if (Ast2IrDestination.find(child) != Ast2IrDestination.end()) {
        Ast2IrDestination[child]->pushStmt(new IRStmt(child));
      }
    }
    
    return true;
  }


};