#pragma once

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Analysis/CFG.h>

#include "IR.hpp"
#include "util.hpp"
#include "clang/AST/Stmt.h"

class Cilk2IRVisitor : public clang::RecursiveASTVisitor<Cilk2IRVisitor> {
private:
  clang::ASTContext *Context;
  IRProgram &P;

  void functionCFG2IR(const FunctionDecl* Decl, CFG *Cfg) {
    std::unordered_map<CFGBlock *, std::pair<IRBasicBlock *, IRBasicBlock *>>
        Cfg2IRLookup;
    auto *IrF = P.createFunc();
    IrF->RootFun = Decl;
    for (auto *CfgB : *Cfg) {
      auto *IrB = IrF->createBlock();
      auto *IrBStart = IrB;

      int CfgEInd = 0;
      for (auto &CfgE : *CfgB) {
        switch (CfgE.getKind()) {
        case CFGElement::Kind::Statement: {
          CFGStmt CfgS = CfgE.castAs<CFGStmt>();
          const Stmt *S = CfgS.getStmt();

          if (isa<CilkSyncStmt>(S) || isa<ReturnStmt>(S)) {
            IrB->Terminator = std::make_unique<IRStmt>(S);
            // bruh aint no way
            IRBasicBlock *NewIrB = nullptr;
            auto &CfgBR = *CfgB;
            if ((CfgEInd == (CfgB->size()-1)) &&
                !CfgB->getTerminator().isValid()) {
              NewIrB = IrB;
            } else {
              NewIrB = IrF->createBlock();
              IrB->Succs.insert(NewIrB);
            }
            IrB = NewIrB;
          } else {
            P.Ast2IrDestination[S] = IrB;
          }

          break;
        }
        default:
          PANIC("Unsupported CFGElement: %d", CfgE.getKind());
        }
        CfgEInd++;
      }
      if (CfgB->getTerminator().isValid()) {
        IrB->Terminator =
            std::make_unique<IRStmt>(CfgB->getTerminator().getStmt());
      }
      Cfg2IRLookup[CfgB] = std::make_pair(IrBStart, IrB);
    }
    IrF->Entry = Cfg2IRLookup[&(Cfg->getEntry())].first;
    for (auto *CfgB : *Cfg) {
      if (Cfg2IRLookup.find(CfgB) == Cfg2IRLookup.end()) {
        PANIC("not traversed all cfg blocks?");
      }
      auto [block_start, block_end] = Cfg2IRLookup[CfgB];
      /*for (auto PredI = CfgB->pred_begin(); PredI != CfgB->pred_end();
      ++PredI) { if (auto *Pred = PredI->getReachableBlock()) { if
      (Cfg2IRLookup.find(Pred) == Cfg2IRLookup.end()) { PANIC("predecessor not
      found in lookup");
          }
          block_start->Preds.insert(Cfg2IRLookup[Pred].second);
        }
      }*/
      for (auto SuccI = CfgB->succ_begin(); SuccI != CfgB->succ_end();
           ++SuccI) {
        if (auto *Succ = SuccI->getReachableBlock()) {
          if (Cfg2IRLookup.find(Succ) == Cfg2IRLookup.end()) {
            PANIC("successor not found in lookup");
          }
          block_end->Succs.insert(Cfg2IRLookup[Succ].first);
        }
      }
    }
  }

  IRStmt *makeIRStmt(const Stmt *S) {

    if (auto *DS = dyn_cast<DeclStmt>(S)) {
      if (DS->child_begin() != DS->child_end()) {
        if (!DS->isSingleDecl()) {
          PANIC("unsupported: assignment to multiple declarations");
        }
        IRStmt *IrS = new IRStmt(*(DS->child_begin()));
        if (auto *D = dyn_cast<NamedDecl>(DS->getSingleDecl())) {
          IrS->Lhs = D;
        } else {
          PANIC("unsupported: assignment to non-named declaration");
        }
        return IrS;
      } else {
        // We don't need to include a declaration with no children in the IR.
        return nullptr;
      }
    } else if (auto *BS = dyn_cast<BinaryOperator>(S)) {
      // note: don't care about +=, -=, etc.
      // these depend on the previous value so not an LHS
      if (BS->isAssignmentOp()) {
        IRVarRef D = nullptr;
        if (auto *ICE = dyn_cast<ImplicitCastExpr>(BS->getLHS())) {
          if (auto *DRE = dyn_cast<DeclRefExpr>(ICE)) {
            D = DRE->getDecl();
          }
        } else if (auto *DRE = dyn_cast<DeclRefExpr>(BS->getLHS())) {
          D = DRE->getDecl();
        }

        IRStmt *IrS;
        if (D) {
          IrS = new IRStmt(BS->getRHS());
          IrS->Lhs = D;
        } else {
          IrS = new IRStmt(BS);
        }
        return IrS;
      }
    }
    return new IRStmt(S);
  }

public:
  explicit Cilk2IRVisitor(clang::ASTContext *Context, IRProgram &P)
      : Context(Context), P(P) {}

  bool VisitFunctionDecl(clang::FunctionDecl *Decl) {
    CFG::BuildOptions Options;
    auto Cfg = CFG::buildCFG(nullptr, (Decl->getBody()), Context, Options);
    if (Cfg == nullptr) {
      PANIC("Could not build CFG for function %s",
            Decl->getName().str().c_str());
      return false;
    }
    functionCFG2IR(Decl, Cfg.get());
    //Cfg->print(llvm::outs(), Context->getLangOpts(), true);
    //Cfg->viewCFG(Context->getLangOpts());
    return true;
  }

  // bool VisitReturnStmt(const ReturnStmt *stmt) {
  //   std::unique_ptr<IRStmt> s = std::make_unique<IRStmt>(stmt);
  //   if (TraverseContext.func) {
  //   std::cout << "test2" << std::endl;
  //     TraverseContext.func->newStmt(std::move(s));
  //   }
  //
  //  return true;
  //}

  bool VisitStmt(const Stmt *S) {
    if (isa<Expr>(S) || isa<DeclStmt>(S) || isa<ReturnStmt>(S)) {
      return true;
    }
    std::unordered_map<const Stmt *, IRStmt*> ToReplace;
    if (auto *FS = dyn_cast<ForStmt>(S)) {
      auto IRS = new IRStmt(FS, nullptr);
      IRS->Kind = IRStmt::ForInit;
      ToReplace[FS->getInit()] = IRS;
      IRS = new IRStmt(FS, nullptr);
      IRS->Kind = IRStmt::ForInc;
      ToReplace[FS->getInc()] = std::move(IRS);
      ToReplace[FS->getCond()] = nullptr;
    } else if (auto *IS = dyn_cast<IfStmt>(S)) {
      ToReplace[IS->getCond()] = nullptr;
    }

    for (const auto *child : S->children()) {
      if (P.Ast2IrDestination.find(child) != P.Ast2IrDestination.end()) {
        if (ToReplace.find(child) != ToReplace.end()) {
          auto Replacement = ToReplace[child];
          ToReplace.erase(child);
          if (Replacement) {
            P.Ast2IrDestination[child]->pushStmt(Replacement);
          }         
        } else {
          IRStmt *IrS = makeIRStmt(child);
          if (IrS) {
            P.Ast2IrDestination[child]->pushStmt(IrS);
          }
        }
      }
    }

    // Cleanup statements we didn't get to replace
    for (const auto & [K, V] : ToReplace) {
      if (V) {
        delete V;
      }
    }
    return true;
  }
};