#pragma once

#include <llvm/ADT/MapVector.h>
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

#include "SpawnAnalysis.hpp"
#include "util.hpp"

using namespace llvm;

class SplitFunIntoContinuations {
  std::vector<std::set<BasicBlock *>> paths;
  ValueMap<BasicBlock *, int> pathLookup;

private:
  BasicBlock *duplicateBasicBlock(BasicBlock *bb,
                                  std::set<BasicBlock *> currPath) {
    BasicBlock *cloned = BasicBlock::Create(
        bb->getContext(), bb->getName() + ".clone", bb->getParent());
    ValueToValueMapTy vMap;
    vMap[bb] = cloned;
    for (auto &I : *bb) {
      Instruction *newInst = I.clone();
      newInst->insertInto(cloned, cloned->end());
    }
    for (BasicBlock *pred : predecessors(bb)) {
      if (currPath.find(pred) != currPath.end()) {
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

  void createSyncPaths(std::vector<std::set<BasicBlock *>> &paths,
                       llvm::Function &func) {
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

public:
  SplitFunIntoContinuations(Function &func) {
    createSyncPaths(paths, func);

    if (paths.size() == 1) {
      // Not a function with syncs
      return;
    }
  }
};

class CreateContinuationFuns {
private:
public:
  CreateContinuationFuns(llvm::Module &module, SpawnAnalysis &sa) {
    for (llvm::Function &func : module) {
      if (!func.empty()) {
        SplitFunIntoContinuations sf(func);
      }
    }
  }
};