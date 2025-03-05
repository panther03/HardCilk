#pragma once

#include <clang/AST/Stmt.h>

#include <memory>
#include <vector>
#include <set>

#include "clang/AST/Decl.h"

using namespace clang;

typedef const clang::NamedDecl* IRVarRef;

class IRStmt;
class IRBasicBlock;
class IRFunction;
class IRProgram;

// TODO: make this an actual class hierarchy instead of stuffing everything into the base class
class IRStmt {
public:
  enum {
    Default,
    ForInc,
    ForInit,
    SpawnNext,
    VoidSpawn
  } Kind;
  const clang::Stmt *innerStmt;
  const IRFunction* SpawnNextDest = nullptr;
  // A declaration that we are sure this instruction only writes to,
  // and does not need the value of at all.
  IRVarRef Lhs = nullptr;
  IRStmt(const clang::Stmt *innerStmt, IRVarRef Lhs = nullptr) : innerStmt(innerStmt), Lhs(Lhs) {}

  void printAllIdentifiers();
};

class IRBasicBlock {
private:
  // TODO: no reason to have this last layer of indirection, should just store the irstmts here
  using IRStmtPtr = std::unique_ptr<IRStmt>;
  std::vector<IRStmtPtr> Stmts;
  IRFunction *Parent;
  unsigned Ind;

public:
  std::set<IRBasicBlock *> Succs;
  IRStmtPtr Terminator;
  friend class IRFunction;

  IRBasicBlock(unsigned Ind, IRFunction* Parent) : Ind(Ind), Parent(Parent) {}
  void iteratePreds(std::function<void(IRBasicBlock* B)> CB);

  void pushStmt(IRStmt *stmt) { Stmts.push_back(IRStmtPtr(stmt)); }
  // Clones contents of basic block. Does not clone predecessors and successors.
  void clone(IRBasicBlock *Dest);

  void graphPrintStmt(llvm::raw_ostream &out, clang::ASTContext &Context, const Stmt* S, const char *NewlineSymbol);

  void print(llvm::raw_ostream &out, clang::ASTContext &Context, const char* NewlineSymbol);

  void dumpGraph(llvm::raw_ostream &out, clang::ASTContext &Context);

  void moveBlock(IRFunction* NewParent);

  using IRBlockListTy = std::vector<IRStmtPtr>;
  using iterator = IRBlockListTy::iterator;
  using const_iterator = IRBlockListTy::const_iterator;

  IRStmtPtr &front() { return Stmts.front(); }
  IRStmtPtr &back() { return Stmts.back(); }

  iterator begin() { return Stmts.begin(); }
  iterator end() { return Stmts.end(); }
  const_iterator begin() const { return Stmts.begin(); }
  const_iterator end() const { return Stmts.end(); }

  IRFunction *getParent() const { return Parent; }
  unsigned getInd() const { return Ind; }
};

class IRFunction {
private:
  using IRBlockPtr = std::unique_ptr<IRBasicBlock>;
  std::list<IRBlockPtr> Blocks;
  IRProgram *Parent;
  unsigned Ind;

public:
  const FunctionDecl* RootFun = nullptr;
  IRBasicBlock *Entry = nullptr;
  std::set<IRVarRef> Args;
  std::set<IRVarRef> Locals;
  std::set<IRVarRef> Materialized;
  friend class IRBasicBlock;
  friend class IRProgram;
  std::unordered_map<const IRStmt*, IRBasicBlock*> Spawn2SpawnNext;
  std::unordered_map<const IRBasicBlock*, IRFunction*> SpawnNext2Cont;

  IRFunction(unsigned Ind, IRProgram *Parent) : Parent(Parent), Ind(Ind) {}
  IRBasicBlock *createBlock();

  void print(llvm::raw_ostream &out, clang::ASTContext &Context);

  void dumpGraph(llvm::raw_ostream &out, clang::ASTContext &Context);

  void dumpArgs(llvm::raw_ostream &out);

  void moveBlock(IRBasicBlock *B, IRFunction *Dest);

  using IRBlockListTy = std::list<IRBlockPtr>;
  using iterator = IRBlockListTy::iterator;
  using const_iterator = IRBlockListTy::const_iterator;

  bool empty() { return Blocks.empty(); }
  IRBlockPtr &front() { return Blocks.front(); }
  IRBlockPtr &back() { return Blocks.back(); }
  IRBasicBlock *entry() { return Entry;  }
  // This will I think?
  IRBasicBlock *exit() { return Blocks.front().get(); }

  iterator begin() { return Blocks.begin(); }
  iterator end() { return Blocks.end(); }
  const_iterator begin() const { return Blocks.begin(); }
  const_iterator end() const { return Blocks.end(); }

  IRProgram *getParent() const { return Parent; }
  unsigned getInd() const { return Ind; }
};

class IRProgram {
private:
  using IRFuncPtr = std::unique_ptr<IRFunction>;
  std::vector<IRFuncPtr> Funcs;

public:
  std::unordered_map<const Stmt *, IRBasicBlock *> Ast2IrDestination;

  IRProgram() {}
  IRFunction *createFunc();

  void print(llvm::raw_ostream &out, clang::ASTContext &Context);  
  void dumpGraph(llvm::raw_ostream &out, clang::ASTContext &Context);

  using IRFuncListTy = std::vector<IRFuncPtr>;
  using iterator = IRFuncListTy::iterator;
  using const_iterator = IRFuncListTy::const_iterator;

  IRFuncPtr &front() { return Funcs.front(); }
  IRFuncPtr &back() { return Funcs.back(); }

  iterator begin() { return Funcs.begin(); }
  iterator end() { return Funcs.end(); }
  const_iterator begin() const { return Funcs.begin(); }
  const_iterator end() const { return Funcs.end(); }
};