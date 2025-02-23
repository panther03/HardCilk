#include <llvm/ADT/MapVector.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>

#include <iostream>

#include "FunctionDatabase.hpp"

#include "SplitContsIntoFuns.hpp"
#include "CreateContinuationPaths.hpp"
#include "EliminateSyncPhis.hpp"
#include "RemoveJunkCalls.hpp"
#include "SpawnAnalysis.hpp"
#include "ValidateCalls.hpp"

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Expected path to input LLVM bytecode (.ll)." << std::endl;
    return 1;
  }

  std::unique_ptr<llvm::LLVMContext> llvmCtx =
      std::make_unique<llvm::LLVMContext>();

  llvm::SMDiagnostic err;
  std::unique_ptr<llvm::Module> llvmModule =
      llvm::parseIRFile(argv[1], err, *llvmCtx);
  if (!llvmModule || llvm::verifyModule(*llvmModule, &llvm::errs())) {
    err.print("tapir2vitis", llvm::errs());
    return 1;
  }

  FunctionDatabase fd;

  RemoveJunkCalls rj(*llvmModule);
  SpawnAnalysis sa(*llvmModule);
  ValidateCalls vc(*llvmModule, sa);
  // This pass is not necessary for now because on -O0 phi is not used to merge
  // the path between the continuations. Instead, a local variable is used.
  // EliminateSyncPhis es(*llvmModule);

  std::vector<Function *> workList;
  for (auto &func : *llvmModule) {
    workList.push_back(&func);
  }

  for (auto &func : workList) {
    if (sa.needsContinuation.find(func) == sa.needsContinuation.end()) {
      continue;
    }
    CreateContinuationPaths ccp(*func);
    SplitContsIntoFuns scf(*func, ccp);
  }

  llvmModule->print(outs(), NULL);
}