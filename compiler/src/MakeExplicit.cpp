#include <clang/AST/ExprCilk.h>
#include <clang/AST/StmtCilk.h>
#include <llvm/ADT/SetVector.h>
#include <set>
#include <unordered_map>

#include "IR.hpp"
#include "util.hpp"
#include "clang/AST/Expr.h"

using namespace llvm;

/////////////////////////////
// CreateContinuationFuns //
///////////////////////////

struct CreateContinuationFuns {
  struct ContFun {
    IRFunction *F;
    std::set<IRVarRef> Args;
    std::set<IRVarRef> Locals;
  };
  // Each "path" in this vector will become a function.
  // The first path corresponds to the original function.
  // Note that the sets need to be ordered to preserve DFS order,
  // allowing us to compute the values actually needed by the path
  std::vector<SetVector<IRBasicBlock *>> Paths;
  std::unordered_map<IRBasicBlock *, int> PathLookup;
  std::vector<ContFun> ContFuns;

private:
  IRBasicBlock *duplicateBasicBlock(IRBasicBlock *B,
                                    SetVector<IRBasicBlock *> &CurrPath) {
    IRBasicBlock *CloneBB = B->getParent()->createBlock();
    B->clone(CloneBB);

    // Copy over successors
    for (IRBasicBlock *Succ : B->Succs) {
      CloneBB->Succs.insert(Succ);
    }

    B->iteratePreds([&](IRBasicBlock *Pred) -> void {
      if (CurrPath.contains(Pred)) {
        Pred->Succs.remove(B);
        Pred->Succs.insert(CloneBB);
      }
    });
    return CloneBB;
  }

  void createSyncPaths(IRFunction &F) {
    Paths.resize(1);

    std::vector<std::pair<IRBasicBlock *, int>> Todo;
    Todo.push_back(std::make_pair(F.entry(), 0));

    int Fresh = 0;
    while (!Todo.empty()) {
      auto [B, CurrLevel] = Todo.back();
      Todo.pop_back();

      if (PathLookup.find(B) != PathLookup.end()) {
        // if we've seen it before but it is in the same level, we just skip
        if (PathLookup[B] == CurrLevel) {
          continue;
        } else {
          B = duplicateBasicBlock(B, Paths[CurrLevel]);
        }
      }

      Paths[CurrLevel].insert(B);
      PathLookup.insert(std::make_pair(B, CurrLevel));

      if (B->Term && isa<SyncIRStmt>(B->Term)) {
        // sync instruction should have only one successor
        IRBasicBlock *SISucc = *(B->Succs.begin());
        if (PathLookup.find(SISucc) != PathLookup.end()) {
          continue;
        }
        Fresh++;
        Paths.resize(Fresh + 1);
        CurrLevel = Fresh;
      }

      for (auto *Succ : B->Succs) {
        Todo.push_back(std::make_pair(Succ, CurrLevel));
      }
    }

    int i = 0;
    for (auto &Path : Paths) {
      llvm::outs() << "path " << i << ": ";
      for (auto &B : Path) {
        llvm::outs() << "BB" << B->getInd() << ", ";
      }
      llvm::outs() << "\n";
      i++;
    }
  }

  // TODO: this function is not going to handle more complex cases like
  // a store being present in only one branch and a load at the join
  // (so the value is free for the whole function)
  void analyzePath(ContFun &CF,
                   SetVector<IRBasicBlock *> &path,
                   std::set<IRVarRef> *inFrees) {
    std::set<IRVarRef> &free = CF.Args;
    std::set<IRVarRef> &refd = CF.Locals;

    // Values that will be used in the proceeding blocks regardless of whether
    // they are used here. Only removed if created here.
    if (inFrees) {
      for (auto &v : *inFrees) {
        free.insert(v);
      }
    }
    for (auto &bb : path) {
      for (auto &S : *bb) {
        auto V = ExprIdentifierVisitor(S.get());
        for (auto *D: V) {
          if (refd.find(D) == refd.end()) {
            free.insert(D);
            refd.insert(D);
          }
        }

        if (auto *CS = dyn_cast<CopyIRStmt>(S.get())) {
          if (refd.find(CS->Dest) == refd.end()) {
            refd.insert(CS->Dest);
            free.erase(CS->Dest);
          }
        }
      }
    }
    for (auto *v : free) {
      if (refd.find(v) != refd.end()) {
        refd.erase(v);
      }
    }
  }

public:
  CreateContinuationFuns(IRFunction &F) {
    createSyncPaths(F);

    if (Paths.size() == 1) {
      // Not a function with syncs
      return;
    }

    std::deque<IRBasicBlock *> todo;
    for (auto &B : F) {
      // don't care about the original function
      if (PathLookup[B.get()] == 0)
        continue;

      // every function we will need to create has its
      // own terminator block, add these
      if (B->Succs.empty()) {
        todo.push_back(B.get());
      }
    }

    for (int p = 0; p < Paths.size() - 1; p++) {
      ContFun CF = ContFun {
        .F = F.getParent()->createFunc(),
        .Args = std::set<IRVarRef>(),
        .Locals = std::set<IRVarRef>()
      };
      ContFuns.push_back(CF);
    }
    // just used for checking assumptions
    std::vector<bool> visited(Paths.size() - 1, 0);

    while (!todo.empty()) {
      auto *bb = todo.front();
      todo.pop_front();

      assert(PathLookup.find(bb) != PathLookup.end());
      if (PathLookup[bb] == 0)
        continue;
      int path = PathLookup[bb] - 1;
      // we should only visit a path once, because sync continue blocks should
      // only have one parent
      // TODO: does this assumption make sense?
      assert(!visited[path]);

      std::set<IRVarRef> *inFrees = NULL;
      if (!bb->Succs.empty()) {
        if (auto *succBb = *(bb->Succs.begin())) {
          assert(PathLookup.find(succBb) != PathLookup.end());
          inFrees = &(ContFuns[PathLookup[succBb] - 1].Args);
        }
      }

      analyzePath(ContFuns[path],
                  Paths[path + 1], inFrees);
      visited[path] = true;

      auto *startBb = Paths[path + 1][0];

      startBb->iteratePreds(
          [&](IRBasicBlock *Pred) -> void { todo.push_front(Pred); });
    }

    for (int p = 0; p < Paths.size(); p++) {
      auto &Path = Paths[p];

      if (p > 0) { 
        auto &CF = ContFuns[p-1];
        for (auto *Arg: CF.Args) {
          CF.F->Vars.push_back(IRVarDecl {
            .Type = Arg->Type,
            .Name = Arg->Name,
            .DeclLoc = IRVarDecl::ARG
          });
        }
        for (auto *Local: CF.Locals) {
          CF.F->Vars.push_back(IRVarDecl {
            .Type = Local->Type,
            .Name = Local->Name,
            .DeclLoc = IRVarDecl::LOCAL
          });
        }
      }

      for (auto *B : Path) {
        IRFunction *SpawnNextDest = nullptr;
        if (B->Term) {
          if (isa<SyncIRStmt>(B->Term)) {
            auto *succBb = *(B->Succs.begin());
            assert(succBb);
            assert(PathLookup.find(succBb) != PathLookup.end());
            assert(PathLookup[succBb] > 0);
            delete B->Term;
            B->Term = new SpawnNextIRStmt(ContFuns[PathLookup[succBb] - 1].F);
            B->Succs.clear();
          }
        }
        if (p > 0) {
          B->getParent()->moveBlock(B, ContFuns[p - 1].F);
        }
        if (SpawnNextDest) {
          B->getParent()->SpawnNext2Cont[B] = SpawnNextDest;
        }
      }
    }

    F.cleanVars();

    outs() << "Root:\n";
    F.printVars(outs());

    int I = 0;
    for (auto &CF : ContFuns) {
      outs() << "ContF" << CF.F->getInd() << ":\n";
      CF.F->printVars(outs());
      I++;
    }
  }
};

/*
///////////////////////
// ScopeStartMapper //
/////////////////////

class ScopeStartMapper : public ScopedIRTraverser {
private:
  std::unordered_map<IRBasicBlock *, IRBasicBlock *> &ScopeStarts;
  std::vector<IRBasicBlock *> ScopeStack;
  bool pushNext = false;

  void handleScope(ScopeEvent SE) override {
    if (SE == ScopeEvent::Open || SE == ScopeEvent::Else) {
      pushNext = true;
    }
    if (SE == ScopeEvent::Close || SE == ScopeEvent::Else) {
      ScopeStack.pop_back();
    }
  }

  void visitBlock(IRBasicBlock *B) override {
    if (pushNext) {
      ScopeStack.push_back(B);
      pushNext = false;
    }
    ScopeStarts[B] = ScopeStack.back();
  }

public:
  ScopeStartMapper(
      std::unordered_map<IRBasicBlock *, IRBasicBlock *> &ScopeStarts)
      : ScopeStarts(ScopeStarts) {}

  void reset(IRBasicBlock *Entry) {
    ScopeStack.clear();
    ScopeStack.push_back(Entry);
  }
};

//////////////////////
// SetupArgsLocals //
////////////////////

struct SetupArgsLocals {
  IRBasicBlock *DFSTillSpawnNext(IRBasicBlock *StartB) {
    std::vector<IRBasicBlock *> WorkList;
    std::set<IRBasicBlock *> SeenList;
    WorkList.push_back(StartB);

    IRBasicBlock *FoundSpawnNext = nullptr;

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
    for (auto &B : *F) {
      std::vector<IRStmt *> SpawnStatements;
      for (auto &S : *B) {
        if (auto *BS = dyn_cast<BinaryOperator>(S->innerStmt)) {
          // This case should never happen. Should have been incorporated into
          // the LHS before. Unless there is an assignment like a = b = c =
          // spawn, but that should be flagged before.
          assert(!isa<CilkSpawnExpr>(BS->getRHS()));
        } else if (isa<CilkSpawnExpr>(S->innerStmt)) {
          SpawnStatements.push_back(S.get());
        }
      }

      if (!SpawnStatements.empty()) {
        IRBasicBlock *SpawnNext = DFSTillSpawnNext(B.get());
        if (SpawnNext) {
          for (auto &SpawnS : SpawnStatements) {
            F->Spawn2SpawnNext[SpawnS] = SpawnNext;
          }
        }
      }
    }
  }

  void PushBackSpawnVars(IRFunction *F) {
    for (auto &B : *F) {
      for (auto &S : *B) {
        if (isa<CilkSpawnExpr>(S->innerStmt)) {
          S->Kind = IRStmt::VoidSpawn;
          if (S->Lhs) {
            if (F->Spawn2SpawnNext.find(S.get()) == F->Spawn2SpawnNext.end()) {
              PANIC("Spawn has a return value, but no corresponding spawn "
                    "next..");
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

  void AddSpawnNextDecls(
      IRFunction *F,
      std::unordered_map<IRBasicBlock *, IRBasicBlock *> &ScopeStarts) {
    for (auto &B : *F) {
      if (B->Terminator && B->Terminator->Kind == IRStmt::SpawnNext) {
        auto *DeclB = ScopeStarts[B.get()];
        assert(DeclB);

        auto *DeclS = new IRStmt(B->Terminator->innerStmt);
        DeclS->Kind = IRStmt::SpawnNextDecl;
        DeclS->SpawnNextDest = B->Terminator->SpawnNextDest;
        DeclB->pushStmtFront(DeclS);
      }
    }
  }

  SetupArgsLocals(IRFunction &Root, std::vector<IRFunction *> &ContFuns) {
    std::vector<IRFunction *> FnWorkList;
    FnWorkList.push_back(&Root);
    for (auto CF : ContFuns) {
      FnWorkList.push_back(CF);
    }

    std::unordered_map<IRBasicBlock *, IRBasicBlock *> ScopeStarts;
    ScopeStartMapper SSM(ScopeStarts);
    for (auto F : FnWorkList) {
      FindContForSpawns(F);
      PushBackSpawnVars(F);
      SSM.reset(F->entry());
      SSM.traverse(*F);
      AddSpawnNextDecls(F, ScopeStarts);
    }

    outs() << "Root:\n";
    Root.dumpArgs(outs());

    int I = 0;
    for (auto &CF : ContFuns) {
      outs() << "ContF" << CF->getInd() << ":\n";
      CF->dumpArgs(outs());
      I++;
    }
  }
};

///////////
// Glue //
/////////

void MarkContFuns(IRFunction *F) {
  // Mark functions that need continuations as such.
  for (auto &B : *F) {
    for (auto &S : *B) {
      if (auto *SpawnE = dyn_cast<CilkSpawnExpr>(S->innerStmt)) {
        if (auto *CallE = dyn_cast<CallExpr>(SpawnE->getSpawnedExpr())) {
          if (auto *ND = dyn_cast<NamedDecl>(CallE->getCalleeDecl())) {
            if (F->getParent()->RootFunLookup.find(ND->getNameAsString()) != F->getParent()->RootFunLookup.end()) {
              auto CF = F->getParent()->RootFunLookup[ND->getNameAsString()];
              CF->NeedsCont = true;
            }
          }
        }
      }
    }
  }
}*/

void MakeExplicit(IRProgram &P) {
  std::vector<IRFunction *> WorkList;
  for (auto &F : P) {
    WorkList.push_back(F.get());
  }

  for (auto &F : WorkList) {
    CreateContinuationFuns CCF(*F);
    //SetupArgsLocals SAL(*F, CCF.ContFuns);
    //MarkContFuns(F);
  }
}