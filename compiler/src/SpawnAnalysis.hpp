#pragma once

#include <llvm/ADT/MapVector.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/SourceMgr.h>

#include <iostream>

#include "util.hpp"

using namespace llvm;

struct SpawnData {
  std::string fn_name;
  Value *cont_ptr;
};

class SpawnAnalysis {

private:
  ErrorOr<SpawnData> extractSpawnFromDetachBlock(BasicBlock &bb) {
    // Find a call instruction in bb
    CallInst *spawnCall = NULL;
    Function *spawnFunc = NULL;
    for (auto &inst : bb) {
      if (spawnCall = dyn_cast<CallInst>(&inst)) {
        spawnFunc = spawnCall->getCalledFunction();
        if (spawnFunc)
          break;
      }
    }

    if (!spawnFunc || !spawnCall) {
      errs() << RED
             << "No function call in detach block, or function name invalid\n"
             << COLOR_RESET;
      return std::error_code();
    }

    SpawnData s;
    s.cont_ptr = NULL;
    s.fn_name = spawnFunc->getName();

    // loop instructions in basic block searching for store instructions
    for (auto &inst : bb) {
      if (auto *storeInst = dyn_cast<StoreInst>(&inst)) {
        // check if the store value of storeInst is the return value of the call
        if (storeInst->getValueOperand() == spawnCall) {
          s.cont_ptr = storeInst->getPointerOperand();
          break;
        }
      }
    }

    if (s.cont_ptr == NULL) {
      errs() << RED << "Expected store instruction, not found in detach block\n"
             << COLOR_RESET;
      return std::error_code();
    }

    needsContinuation.insert(spawnFunc);

    return s;
  }

  void analyzeFunction(Function &func) {
    for (auto &bb : func) {
      if (auto *detachInst = dyn_cast<DetachInst>(bb.getTerminator())) {
        auto detachedBb = detachInst->getDetached();
        auto spawnData = extractSpawnFromDetachBlock(*detachedBb);
        if (!spawnData) {
          // Print debugging location of detachInst if it failed
          errs() << RED << "Failed to extract spawn from detach instruction: "
                 << COLOR_RESET;
          detachInst->print(errs(), true);
          errs() << " (function " << func.getName() << ")\n";
          exit(EXIT_FAILURE);
        }
        std::cout << spawnData.get().fn_name << std::endl;
        spawnDataMap.insert(std::make_pair(detachedBb, spawnData.get()));
      }
    }
  }

public:
  // Map the basic block corresponding to the detached branch of a detach
  // instruction to the SpawnData.
  MapVector<BasicBlock *, SpawnData> spawnDataMap;
  // All functions in this set are spawned somewhere
  // and need a continuation as an argument.
  std::set<Function *> needsContinuation;

  SpawnAnalysis(llvm::Module &module) {
    for (auto &func : module) {
      analyzeFunction(func);
    }
  }
};