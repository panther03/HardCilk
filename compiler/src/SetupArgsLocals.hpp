#pragma once

#include <clang/AST/Expr.h>
#include <clang/AST/ExprCilk.h>

#include "IR.hpp"
#include "util.hpp"

using namespace llvm;

struct SetupArgsLocals {
  IRBasicBlock* DFSTillSpawnNext(IRBasicBlock *StartB) {
    std::vector<IRBasicBlock*> WorkList;
    std::set<IRBasicBlock*> SeenList;
    WorkList.push_back(StartB);

    IRBasicBlock* FoundSpawnNext = nullptr;

    while (!WorkList.empty()) {
      IRBasicBlock *B = WorkList.back();
      WorkList.pop_back();
      SeenList.insert(B);
      
      if (B->Terminator && (B->Terminator->Kind == IRStmt::SpawnNext)) {
        if (FoundSpawnNext) {
          // TODO improve this error message
          PANIC("Ambiguous spawnNext for spawn!");
        } else {
          FoundSpawnNext = B;
        }
      }

      for (auto &Succ : B->Succs) {
        if (SeenList.find(Succ) == SeenList.end()) {
          WorkList.push_back(Succ);
        }
      }
    }

    return FoundSpawnNext;
  }

  void FindContForSpawns(IRFunction *F) {
    for (auto &B: *F) {
      std::vector <IRStmt*> SpawnStatements;
      for (auto &S: *B) {
        if (auto *BS = dyn_cast<BinaryOperator>(S->innerStmt)) {
          // This case should never happen. Should have been incorporated into the LHS before.
          // Unless there is an assignment like a = b = c = spawn, but that should be flagged before.
          assert(!isa<CilkSpawnExpr>(BS->getRHS()));
        } else if (isa<CilkSpawnExpr>(S->innerStmt)) {
          SpawnStatements.push_back(S.get());
        }
      }

      if (!SpawnStatements.empty()) {
        IRBasicBlock *SpawnNext = DFSTillSpawnNext(B.get());
        if (SpawnNext) {
          for (auto &SpawnS: SpawnStatements) {
            F->Spawn2SpawnNext[SpawnS] = SpawnNext;
          }
        }
      }
    }
  }

  void PushBackSpawnVars(IRFunction *F) {
    for (auto &B: *F) {
      for (auto &S: *B) {
        if (isa<CilkSpawnExpr>(S->innerStmt)) {
          S->Kind = IRStmt::VoidSpawn;
          if (S->Lhs) {
            if (F->Spawn2SpawnNext.find(S.get()) == F->Spawn2SpawnNext.end()) {
              PANIC("Spawn has a return value, but no corresponding spawn next..");
            }
            auto *SpawnNextF = F->SpawnNext2Cont[F->Spawn2SpawnNext[S.get()]];
            F->Locals.erase(S->Lhs);
            SpawnNextF->Args.erase(S->Lhs);
            SpawnNextF->Materialized.insert(S->Lhs);            
          }
        }
      }
    }
  }

  SetupArgsLocals(IRFunction &Root, std::vector<IRFunction *> &ContFuns) {
    std::vector<IRFunction *> FnWorkList;
    FnWorkList.push_back(&Root);
    for (auto CF : ContFuns) {
      FnWorkList.push_back(CF);
    }

    for (auto F : FnWorkList) {
      FindContForSpawns(F);
    }

    for (auto F: FnWorkList) {
      PushBackSpawnVars(F);
    }

    outs() << "Root:\n";
    Root.dumpArgs(outs());

    int I = 0;
    for (auto &CF: ContFuns) {
      outs() << "ContF" << CF->getInd() << ":\n";
      CF->dumpArgs(outs());
      I++;
    }
  }
};