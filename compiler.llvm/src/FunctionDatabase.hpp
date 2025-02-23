#pragma once

#include <llvm/ADT/MapVector.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>

struct FunctionData {
  bool root;
  std::set<std::string> spawnNextList;
  std::set<std::string> spawnList;
  std::set<std::string> sendArgumentList;
};

using FunctionDatabase = std::unordered_map<std::string, FunctionData>;