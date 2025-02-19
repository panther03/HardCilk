#pragma once

#include <llvm/ADT/MapVector.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/SourceMgr.h>

#include <iostream>

#include "SpawnAnalysis.hpp"
#include "util.hpp"

using namespace llvm;

class ValidateCalls {
public:
  ValidateCalls(llvm::Module &module, SpawnAnalysis &sa) {
    for (llvm::Function &func : module) {

      // filter out junk instructions in basic blocks
      for (llvm::BasicBlock &bb : func) {
        if (sa.spawnDataMap.find(&bb) != sa.spawnDataMap.end()) {
          // detach basic block; don't clean calls here
          continue;
        }
        for (llvm::Instruction &inst : bb) {
          if (auto *callInst = dyn_cast<CallInst>(&inst)) {
            // Check that there are no calls to continuation functons
            if (sa.needsContinuation.find(callInst->getCalledFunction()) !=
                sa.needsContinuation.end()) {
              errs() << RED << "Detected call to function " << COLOR_RESET
                     << callInst->getCalledFunction()->getName() << RED
                     << " needing continuation outside of spawn block.\n";
              errs() << "Are you using -O0? OpenCilk inlines spawns, this is unsupported.\n" << COLOR_RESET;
              exit(EXIT_FAILURE);
            }
          }
        }
      }
    }
  }
};