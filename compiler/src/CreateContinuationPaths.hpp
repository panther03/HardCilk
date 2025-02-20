#pragma once

#include <llvm/ADT/MapVector.h>
#include <llvm/ADT/SetVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

#include <iostream>
#include <deque>

#include "util.hpp"

using namespace llvm;

struct ContFunctionInfo {
  std::set<Value*> args;
  std::set<Value*> locals;
};

struct CreateContinuationPaths {
  // Each "path" in this vector will become a function.
  // The first path corresponds to the original function.
  // Note that the sets need to be ordered to preserve DFS order,
  // allowing us to compute the values actually needed by the path
  std::vector<SetVector<BasicBlock *>> paths;
  ValueMap<BasicBlock *, int> pathLookup;
  std::vector<ContFunctionInfo> infos;

private:
  BasicBlock *duplicateBasicBlock(BasicBlock *bb,
                                  SetVector<BasicBlock *> &currPath) {
    BasicBlock *cloned = BasicBlock::Create(
        bb->getContext(), bb->getName() + ".clone", bb->getParent());
    ValueToValueMapTy vMap;
    vMap[bb] = cloned;
    for (auto &I : *bb) {
      Instruction *newInst = I.clone();
      newInst->insertInto(cloned, cloned->end());
    }
    for (BasicBlock *pred : predecessors(bb)) {
      if (currPath.contains(pred)) {
        // basic blocks in our path should point to the cloned block.
        RemapInstruction(pred->getTerminator(), vMap,
                         RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
      }
    }
    // note: we leave this function with multiple basic blocks pointing to the
    // same block, since we cloned the old one, and no valid phi function. this
    // would be a problem, but we are supposed to eventually encounter a sync,
    // which we will replace with a function call.
    // TODO: is this a correct assumption? will llvm asserts trip before then?
    return cloned;
  }

  void createSyncPaths(llvm::Function &func) {
    paths.resize(1);

    std::vector<std::pair<BasicBlock *, int>> todo;
    todo.push_back(std::make_pair(&func.getEntryBlock(), 0));

    int fresh = 0;
    while (!todo.empty()) {
      auto [bb, currLevel] = todo.back();
      todo.pop_back();

      if (pathLookup.find(bb) != pathLookup.end()) {
        // if we've seen it before but it is in the same level, we just skip
        if (pathLookup[bb] == currLevel) {
          continue;
        } else {
          bb = duplicateBasicBlock(bb, paths[currLevel]);
        }
      }

      paths[currLevel].insert(bb);
      pathLookup.insert(std::make_pair(bb, currLevel));

      if (auto *syncInst = dyn_cast<SyncInst>(bb->getTerminator())) {
        auto *syncSucc = syncInst->getSuccessor(0);
        if (pathLookup.find(syncSucc) != pathLookup.end()) {
          continue;
        }
        fresh++;
        paths.resize(fresh + 1);
        currLevel = fresh;
      }

      for (auto *succ : successors(bb)) {
        todo.push_back(std::make_pair(succ, currLevel));
      }
    }

    int i = 0;
    for (auto &path : paths) {
      outs() << "path " << i << ": ";
      for (auto &bb : path) {
        outs() << bb->getName() << ", ";
      }
      outs() << "\n";
      i++;
    }
  }

  // TODO: this function is not going to handle more complex cases like
  // a store being present in only one branch and a load at the join
  // (so the value is free for the whole function)
  void analyzePath(ContFunctionInfo &c, SetVector<BasicBlock *> &path, std::set<Value*> *inFrees) {
    std::set<Value*> &free = c.args;
    std::set<Value*> &refd = c.locals;
    std::set<Value*> seen;

    // Values that will be used in the proceeding blocks regardless of whether they are used here.
    // Only removed if created here.
    if (inFrees) {
      for (auto &v: *inFrees) {
        free.insert(v);
      }
    }
    for (auto &bb : path) {
      for (auto &I: *bb) {
        unsigned opstart = 0;
        unsigned opend = I.getNumOperands();
        if (auto *loadInst = dyn_cast<LoadInst>(&I)) {
          auto *op = loadInst->getPointerOperand();
          refd.insert(op);
        } else if (auto *storeInst = dyn_cast<StoreInst>(&I)) {
          // Only marking this destination as seen if not already referenced (i.e. loaded or stored before.)
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
          if ((isa<Argument>(operand) || isa<Instruction>(operand)) && (seen.find(operand) == seen.end())) {
            free.insert(operand);
          }
        }
        free.erase(&I);
        seen.insert(&I);
      }
    }
    for (auto *v: free) {
      if (refd.find(v) != refd.end()) {
        refd.erase(v);
      }
    }
    outs() << "args: ";
    for (auto *v: free) {
      outs() << v->getName() << ", ";
    }
    outs() << "\n alloc: ";
    for (auto *v: refd) {
      outs() << v->getName() << ", ";
    }
    outs() << "\n";
  }

public:
  CreateContinuationPaths(Function &func) {
    createSyncPaths(func);

    if (paths.size() == 1) {
      // Not a function with syncs
      return;
    }

    std::deque<BasicBlock*> todo;
    for (auto &bb : func) {
      // don't care about the original function
      if (pathLookup[&bb] == 0) continue;

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
      if (pathLookup[bb] == 0) continue;
      int path = pathLookup[bb] - 1;
      // we should only visit a path once, because sync continue blocks should only have one parent
      // TODO: does this assumption make sense?
      assert(!visited[path]);

      std::set<Value*>* inFrees = NULL;
      if (auto *succBb = bb->getSingleSuccessor()) {
        outs() << bb->getName() << "\n";
        assert(pathLookup.find(succBb) != pathLookup.end());
        inFrees = &(infos[pathLookup[succBb] - 1].args);
      }

      analyzePath(infos[path], paths[path + 1], inFrees);
      visited[path] = true;

      auto *startBb = paths[path+1][0];
      for (auto *predBb: predecessors(startBb)) {
        todo.push_front(predBb);
      }
    }
  }
};