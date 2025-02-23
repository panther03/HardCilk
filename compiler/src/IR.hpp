#pragma once

#include <clang/AST/Stmt.h>

#include <memory>
#include <vector>

using namespace clang;

class IRStmt {
private:
  const clang::Stmt *innerStmt;

public:
  IRStmt(const clang::Stmt *innerStmt): innerStmt(innerStmt) {}
};

class IRFunction {
private:
  std::vector<std::unique_ptr<IRStmt>> stmts;

public:
  IRFunction() {}
  void newStmt(std::unique_ptr<IRStmt> stmt) {
    stmts.push_back(std::move(stmt));
  }

  const std::vector<std::unique_ptr<IRStmt>>& getStmts() {
    return stmts;
  }
};

class IRProgram {
private:
  std::vector<std::unique_ptr<IRFunction>> funcs;

public:
  IRProgram() {}
  void newFunction(std::unique_ptr<IRFunction> func) {
    funcs.push_back(std::move(func));
  }

  const std::vector<std::unique_ptr<IRFunction>>& getFuncs() {
    return funcs;
  }
};