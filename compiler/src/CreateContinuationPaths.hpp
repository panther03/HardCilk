#pragma once

#include <set>
#include <string>
#include <unordered_map>

#include "IR.hpp"

struct ContFunctionInfo {
  std::set<std::string> args;
  std::set<std::string> locals;
};

struct CreateContinuationPaths {
  // Each "path" in this vector will become a function.
  // The first path corresponds to the original function.
  // Note that the sets need to be ordered to preserve DFS order,
  // allowing us to compute the values actually needed by the path
  std::vector<std::set<IRBasicBlock *>> Paths;
  std::unordered_map<IRBasicBlock *, int> PathLookup;
  std::vector<ContFunctionInfo> Infos;

private:
  IRBasicBlock *duplicateBasicBlock(IRBasicBlock *B,
                                    std::set<IRBasicBlock *> &CurrPath) {
    IRBasicBlock *CloneBB = B->getParent()->createBlock();
    B->clone(CloneBB);

    // Copy over successors
    for (IRBasicBlock *Succ : B->Succs) {
      CloneBB->Succs.insert(Succ);
    }
    // Copy over predecessors that belong to the current path
    std::vector<IRBasicBlock *> RemoveList;
    for (IRBasicBlock *Pred : B->Preds) {
      if (CurrPath.find(Pred) != CurrPath.end()) {
        Pred->Succs.erase(B);
        Pred->Succs.insert(CloneBB);
        RemoveList.push_back(Pred);
        CloneBB->Preds.insert(Pred);
      }
    }
    for (IRBasicBlock *ToRemove : RemoveList) {
      B->Preds.erase(ToRemove);
    }
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

      if (B->Terminator.get() &&
          isa<CilkSyncStmt>(B->Terminator.get()->innerStmt)) {
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
  /*void analyzePath(ContFunctionInfo &c, SetVector<BasicBlock *> &path,
                   std::set<Value *> *inFrees) {
    std::set<Value *> &free = c.args;
    std::set<Value *> &refd = c.locals;
    std::set<Value *> seen;

    // Values that will be used in the proceeding blocks regardless of whether
    // they are used here. Only removed if created here.
    if (inFrees) {
      for (auto &v : *inFrees) {
        free.insert(v);
      }
    }
    for (auto &bb : path) {
      for (auto &I : *bb) {
        unsigned opstart = 0;
        unsigned opend = I.getNumOperands();
        if (auto *loadInst = dyn_cast<LoadInst>(&I)) {
          auto *op = loadInst->getPointerOperand();
          refd.insert(op);
        } else if (auto *storeInst = dyn_cast<StoreInst>(&I)) {
          // Only marking this destination as seen if not already referenced
          // (i.e. loaded or stored before.)
          auto *op = storeInst->getPointerOperand();
          if (refd.find(op) == refd.end()) {
            refd.insert(op);
            free.erase(op);
            seen.insert(op);
          }
          opstart = 0;
          opend = storeInst->getPointerOperandIndex();
        }
        for (unsigned i = opstart; i < opend; ++i) {
          Value *operand = I.getOperand(i);
          if ((isa<Argument>(operand) || isa<Instruction>(operand)) &&
              (seen.find(operand) == seen.end())) {
            free.insert(operand);
          }
        }
        free.erase(&I);
        seen.insert(&I);
      }
    }
    for (auto *v : free) {
      if (refd.find(v) != refd.end()) {
        refd.erase(v);
      }
    }
    outs() << "args: ";
    for (auto *v : free) {
      outs() << v->getName() << ", ";
    }
    outs() << "\n alloc: ";
    for (auto *v : refd) {
      outs() << v->getName() << ", ";
    }
    outs() << "\n";
  }*/

public:
  CreateContinuationPaths(IRFunction &F) {
    createSyncPaths(F);

    if (Paths.size() == 1) {
      // Not a function with syncs
      return;
    }
    /*
    std::deque<BasicBlock *> todo;
    for (auto &bb : func) {
      // don't care about the original function
      if (pathLookup[&bb] == 0)
        continue;

      if (auto *returnInst = dyn_cast<ReturnInst>(bb.getTerminator())) {
        todo.push_back(&bb);
      }
    }

    infos.resize(paths.size() - 1);
    // just used for checking assumptions
    std::vector<bool> visited(paths.size() - 1, 0);

    while (!todo.empty()) {
      auto *bb = todo.front();
      todo.pop_front();

      assert(pathLookup.find(bb) != pathLookup.end());
      if (pathLookup[bb] == 0)
        continue;
      int path = pathLookup[bb] - 1;
      // we should only visit a path once, because sync continue blocks should
      // only have one parent
      // TODO: does this assumption make sense?
      assert(!visited[path]);

      std::set<Value *> *inFrees = NULL;
      if (auto *succBb = bb->getSingleSuccessor()) {
        outs() << bb->getName() << "\n";
        assert(pathLookup.find(succBb) != pathLookup.end());
        inFrees = &(infos[pathLookup[succBb] - 1].args);
      }

      analyzePath(infos[path], paths[path + 1], inFrees);
      visited[path] = true;

      auto *startBb = paths[path + 1][0];
      for (auto *predBb : predecessors(startBb)) {
        todo.push_front(predBb);
      }
    }
    */
  }
};