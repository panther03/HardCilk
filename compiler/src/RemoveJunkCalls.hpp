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

class RemoveJunkCalls {
public:
  RemoveJunkCalls(llvm::Module &module) {
    std::vector<llvm::Instruction *> toRemove;
    for (llvm::Function &func : module) {
      // filter out junk instructions in basic blocks
      for (llvm::BasicBlock &bb : func) {
        for (llvm::Instruction &inst : bb) {
          if (auto *callInst = dyn_cast<CallInst>(&inst)) {
            // LLVM intrinsic functions need to be cleaned.
            if (callInst->getCalledFunction()->getName().starts_with("llvm")) {
              // If it returns a token it's the syncregion start instruction.
              // Simply replace the token with none.
              if (callInst->getType()->isTokenTy()) {
                callInst->replaceAllUsesWith(
                    llvm::ConstantTokenNone::get(module.getContext()));
              } else if (!callInst->getType()->isVoidTy()) {
                // If it returns a value, replace it with undef.
                // TODO: untested
                callInst->replaceAllUsesWith(
                    llvm::UndefValue::get(callInst->getType()));
              }
              toRemove.push_back(callInst);
            }
          }
        }
      }
    }

    for (llvm::Instruction *inst : toRemove) {
      inst->eraseFromParent();
    }
  }
};