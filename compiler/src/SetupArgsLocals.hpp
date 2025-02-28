#pragma once

#include <clang/AST/Expr.h>
#include <clang/AST/ExprCilk.h>

#include "IR.hpp"
#include "util.hpp"

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
          SpawnStatements.push_back(S.get());
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

  SetupArgsLocals(IRFunction &Root, std::vector<IRFunction *> &ContFuns) {
    std::vector<IRFunction *> FnWorkList;
    FnWorkList.push_back(&Root);
    for (auto CF : ContFuns) {
      FnWorkList.push_back(CF);
    }

    for (auto F : FnWorkList) {
      FindContForSpawns(F);
    }
  }
};