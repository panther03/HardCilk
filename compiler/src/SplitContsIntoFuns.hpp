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

#include <deque>
#include <iostream>

#include "CreateContinuationPaths.hpp"
#include "util.hpp"

using namespace llvm;

class SplitContsIntoFuns {

private:
  void introduceEmptyFunctions(Function &func, CreateContinuationPaths &ccp) {
    auto &ctx = func.getContext();
    for (int i = 1; i < ccp.paths.size(); i++) {
      std::string newName = func.getName().str() + "_cont" + std::to_string(i);
      std::vector<Type*> argTypes;
      for (auto &arg : ccp.infos[i-1].args) {
        // seems weird to use a pointer like this
        argTypes.push_back(arg->getType());
      }
      auto ftype = FunctionType::get(
        Type::getVoidTy(ctx),
        argTypes,
        false
      );

      Function *newFunc =
          Function::Create(ftype, func.getLinkage(),
                           newName, func.getParent());
      newFunc->copyAttributesFrom(&func);
      //func.getParent()->getFunctionList().insert(func.getIterator(), newFunc);

      BasicBlock *prologue = BasicBlock::Create(ctx, "entry", newFunc);
      IRBuilder<> builder(ctx);
      builder.SetInsertPoint(prologue);
      for (auto &local : ccp.infos[i-1].locals) {
        builder.CreateAlloca(local->getType(), NULL, local->getName());
      }
    }
  }

public:
  SplitContsIntoFuns(Function &func, CreateContinuationPaths &ccp) {
    if (ccp.paths.size() == 1) {
      // Not a function with syncs
      return;
    }

    outs() << "a\n";
    introduceEmptyFunctions(func, ccp);
    //replaceSyncWithCalls();
    //moveBlocks();
  }
};