#pragma once

#include <clang/AST/Stmt.h>

#include <memory>
#include <vector>
#include <regex>

#include "util.hpp"

using namespace clang;

class IRStmt;
class IRBasicBlock;
class IRFunction;
class IRProgram;

class IRStmt {
public:
  const clang::Stmt *innerStmt;
  IRStmt(const clang::Stmt *innerStmt) : innerStmt(innerStmt) {}
};

class IRBasicBlock {
private:
  using IRStmtPtr = std::unique_ptr<IRStmt>;
  std::vector<IRStmtPtr> Stmts;
  IRFunction *Parent;
  unsigned Ind;

public:
  std::set<IRBasicBlock *> Preds;
  std::set<IRBasicBlock *> Succs;
  IRStmtPtr Terminator;

  IRBasicBlock(unsigned Ind, IRFunction* Parent) : Ind(Ind), Parent(Parent) {}
  void pushStmt(IRStmt *stmt) { Stmts.push_back(IRStmtPtr(stmt)); }
  // Clones contents of basic block. Does not clone predecessors and successors.
  void clone(IRBasicBlock *Dest) {
    for (auto &Stmt : Stmts) {
      Dest->pushStmt(new IRStmt(Stmt.get()->innerStmt));
    }
    if (Terminator != nullptr) {
      Dest->Terminator = std::make_unique<IRStmt>(Terminator.get()->innerStmt);
    }
  }

  void graphPrintStmt(clang::ASTContext &Context, const Stmt* S, const char *NewlineSymbol) {
    llvm::SmallString<256> MsgBuffer;
    llvm::raw_svector_ostream Msg(MsgBuffer);

    S->printPretty(Msg, nullptr, Context.getPrintingPolicy(), 0U, NewlineSymbol);
    
    // uhhhhhh
    // yyea
    auto s = std::regex_replace(MsgBuffer.str().str(), std::regex("<"), "\\<");
    //s = std::regex_replace(s, std::regex(">"), "\\>");
    llvm::outs() << s;
  }

  void print(clang::ASTContext &Context, const char* NewlineSymbol)  {
    int I = 1;
    for (auto &Stmt : Stmts) {
      llvm::outs() << "   " << I << ": ";
      graphPrintStmt(Context, Stmt->innerStmt, NewlineSymbol);
      if (isa<Expr>(Stmt->innerStmt)) 
        llvm::outs() << ";" << NewlineSymbol;
      I++;
    }
    if (Terminator != nullptr) {
      const Stmt *S = Terminator->innerStmt;
      if (auto *IS = dyn_cast<IfStmt>(S)) {
        llvm::outs() << "   T: if (";
        graphPrintStmt(Context, IS->getCond(), NewlineSymbol);
        llvm::outs() << ")" << NewlineSymbol;
      } else if (isa<CilkSyncStmt>(S)){
        llvm::outs() << "   T: sync" << NewlineSymbol;
      }
    }
  }
  void dumpGraph(clang::ASTContext &Context) {
    llvm::outs() << "\"{ [B" << getInd();
    llvm::outs() << "]\\l";
    print(Context, "\\l");
    llvm::outs() << "}\"";
  }

  using IRBlockListTy = std::vector<IRStmtPtr>;
  using iterator = IRBlockListTy::iterator;
  using const_iterator = IRBlockListTy::const_iterator;

  IRStmtPtr &front() { return Stmts.front(); }
  IRStmtPtr &back() { return Stmts.back(); }

  iterator begin() { return Stmts.begin(); }
  iterator end() { return Stmts.end(); }
  const_iterator begin() const { return Stmts.begin(); }
  const_iterator end() const { return Stmts.end(); }

  IRFunction *getParent() { return Parent; }
  unsigned getInd() { return Ind; }
};

class IRFunction {
private:
  using IRBlockPtr = std::unique_ptr<IRBasicBlock>;
  std::vector<IRBlockPtr> Blocks;
  IRProgram *Parent;

public:
  IRFunction(IRProgram *Parent) : Parent(Parent) {}
  IRBasicBlock *createBlock() {
    IRBlockPtr B = std::make_unique<IRBasicBlock>(Blocks.size(), this);
    IRBasicBlock *Bp = B.get();
    Blocks.push_back(std::move(B));
    return Bp;
  }
  void print(clang::ASTContext &Context) {
    int i = 0;
    for (auto &B: Blocks) {
      fprintf(stdout, BHGREEN "Block %d" COLOR_RESET "\n", i);
      llvm::outs() << "PREDS: ";
      for (auto *Pred: B->Preds) {
        llvm::outs() << Pred->getInd() << " ";
      }
      llvm::outs() << "\n";
      B->print(Context, "\n");
      llvm::outs() << "SUCCS: ";
      for (auto *Succ: B->Succs) {
        llvm::outs() << Succ->getInd() << " ";
      }
      llvm::outs() << "\n\n\n";
      i++;
    }
  }

  void dumpGraph(clang::ASTContext &Context) {
    llvm::outs() << "digraph unnamed {\n";
    for (auto &B: Blocks) {
      auto *BB = B.get();
      llvm::outs() << "    Node" << BB->getInd();
      llvm::outs() << " [shape=record,label=";
      BB->dumpGraph(Context);
      llvm::outs() << " ];\n";
    }
    
    for (auto &B: Blocks) {
      for (auto &Succ: B.get()->Succs) {
        llvm::outs() << "    Node" << B.get()->getInd();
        llvm::outs() << " -> Node" << Succ->getInd();
        llvm::outs() << ";\n";
      }

      for (auto &Pred: B.get()->Preds) {
        llvm::outs() << "    Node" << B.get()->getInd();
        llvm::outs() << " -> Node" << Pred->getInd();
        llvm::outs() << ";\n";
      }
    }
    llvm::outs() << "}\n";
  }

  using IRBlockListTy = std::vector<IRBlockPtr>;
  using iterator = IRBlockListTy::iterator;
  using const_iterator = IRBlockListTy::const_iterator;

  bool empty() { return Blocks.empty(); }
  IRBlockPtr &front() { return Blocks.front(); }
  IRBlockPtr &back() { return Blocks.back(); }
  // TODO: this will not stay this way, if we add more blocks
  IRBasicBlock *entry() { return Blocks.back().get(); }
  // This will I think?
  IRBasicBlock *exit() { return Blocks.front().get(); }

  iterator begin() { return Blocks.begin(); }
  iterator end() { return Blocks.end(); }
  const_iterator begin() const { return Blocks.begin(); }
  const_iterator end() const { return Blocks.end(); }

  IRProgram *getParent() { return Parent; }
};

class IRProgram {
private:
  using IRFuncPtr = std::unique_ptr<IRFunction>;
  std::vector<IRFuncPtr> Funcs;

public:
  IRProgram() {}
  IRFunction *createFunc() {
    IRFuncPtr F = std::make_unique<IRFunction>(this);
    IRFunction *Fp = F.get();
    Funcs.push_back(std::move(F));
    return Fp;
  }

  void print(clang::ASTContext &Context) {
    for (auto &F: Funcs) {
      F.get()->print(Context);
    }
  }


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