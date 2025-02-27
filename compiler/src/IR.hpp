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
  // A declaration that we are sure this instruction only writes to,
  // and does not need the value of at all.
  const clang::NamedDecl *Lhs = nullptr;
  IRStmt(const clang::Stmt *innerStmt, const clang::NamedDecl *Lhs = nullptr) : innerStmt(innerStmt), Lhs(Lhs) {}

  void printAllIdentifiers() {
    for (auto it = ExprIdentifierIterator(innerStmt); !it.done(); ++it) {
      llvm::outs() << (*it)->getNameInfo().getAsString() << "\n";
    }
  }
};

class IRBasicBlock {
private:
  using IRStmtPtr = std::unique_ptr<IRStmt>;
  std::vector<IRStmtPtr> Stmts;
  IRFunction *Parent;
  unsigned Ind;

public:
  std::set<IRBasicBlock *> Succs;
  IRStmtPtr Terminator;

  IRBasicBlock(unsigned Ind, IRFunction* Parent) : Ind(Ind), Parent(Parent) {}
  void iteratePreds(std::function<void(IRBasicBlock* B)> CB);

  void pushStmt(IRStmt *stmt) { Stmts.push_back(IRStmtPtr(stmt)); }
  // Clones contents of basic block. Does not clone predecessors and successors.
  void clone(IRBasicBlock *Dest) {
    for (auto &Stmt : Stmts) {
      Dest->pushStmt(new IRStmt(Stmt.get()->innerStmt, Stmt.get()->Lhs));
    }
    if (Terminator != nullptr) {
      Dest->Terminator = std::make_unique<IRStmt>(Terminator.get()->innerStmt);
    }
  }

  void graphPrintStmt(llvm::raw_ostream &out, clang::ASTContext &Context, const Stmt* S, const char *NewlineSymbol) {
    llvm::SmallString<256> MsgBuffer;
    llvm::raw_svector_ostream Msg(MsgBuffer);

    S->printPretty(Msg, nullptr, Context.getPrintingPolicy(), 0U, NewlineSymbol);
    
    // uhhhhhh
    // yyea
    auto s = std::regex_replace(MsgBuffer.str().str(), std::regex("<"), "\\<");
    //s = std::regex_replace(s, std::regex(">"), "\\>");
    out << s;
  }

  void print(llvm::raw_ostream &out, clang::ASTContext &Context, const char* NewlineSymbol)  {
    int I = 1;
    int j = 0;
    for (auto &Stmt : Stmts) {
      out << "   " << I << ": ";
      if (Stmt->Lhs) {
        out << Stmt->Lhs->getName() << " = ";
        j++;
      }
      graphPrintStmt(out, Context, Stmt->innerStmt, NewlineSymbol);
      if (isa<Expr>(Stmt->innerStmt)) 
        out << ";" << NewlineSymbol;
      I++;
    }
    if (Terminator != nullptr) {
      const Stmt *S = Terminator->innerStmt;
      if (auto *IS = dyn_cast<IfStmt>(S)) {
        out << "   T: if (";
        graphPrintStmt(out, Context, IS->getCond(), NewlineSymbol);
        out << ")" << NewlineSymbol;
      } else if (auto *FS = dyn_cast<ForStmt>(S)) {
        out << "   T : for (";
        graphPrintStmt(out, Context, FS->getInit(), "");
        out << "; ";
        graphPrintStmt(out, Context, FS->getCond(), "");
        out << "; ";
        graphPrintStmt(out, Context, FS->getInc(), "");
        out << ")" << NewlineSymbol;
      } else if (auto *RS = dyn_cast<ReturnStmt>(S)) {
        out << "   T: return ";
        graphPrintStmt(out, Context, RS->getRetValue(), NewlineSymbol);
        out << NewlineSymbol;
      } else if (isa<CilkSyncStmt>(S)){
        out << "   T: sync" << NewlineSymbol;
      }
    }
  }
  void dumpGraph(llvm::raw_ostream &out, clang::ASTContext &Context) {
    out << "\"{ [B" << getInd();
    out << "]\\l";
    print(out, Context, "\\l");
    out << "}\"";
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
  IRBasicBlock *Entry;

  IRFunction(IRProgram *Parent) : Parent(Parent) {}
  IRBasicBlock *createBlock() {
    IRBlockPtr B = std::make_unique<IRBasicBlock>(Blocks.size(), this);
    IRBasicBlock *Bp = B.get();
    Blocks.push_back(std::move(B));
    return Bp;
  }

  void print(llvm::raw_ostream &out, clang::ASTContext &Context) {
    int i = 0;
    for (auto &B: Blocks) {
      fprintf(stdout, BHGREEN "Block %d" COLOR_RESET "\n", i);
      out << "PREDS: ";
      B->iteratePreds([&] (IRBasicBlock *Pred) -> void {
        out << Pred->getInd() << " ";
      });
      out << "\n";
      B->print(out, Context, "\n");
      out << "SUCCS: ";
      for (auto *Succ: B->Succs) {
        out << Succ->getInd() << " ";
      }
      out << "\n\n\n";
      i++;
    }
  }

  void dumpGraph(llvm::raw_ostream &out, clang::ASTContext &Context) {
    out << "digraph unnamed {\n";
    for (auto &B: Blocks) {
      auto *BB = B.get();
      out << "    Node" << BB->getInd();
      out << " [shape=record,label=";
      BB->dumpGraph(out, Context);
      out << " ];\n";
    }
    
    for (auto &B: Blocks) {
      for (auto &Succ: B.get()->Succs) {
        out << "    Node" << B.get()->getInd();
        out << " -> Node" << Succ->getInd();
        out << ";\n";
      }
    }
    out << "}\n";
  }

  using IRBlockListTy = std::vector<IRBlockPtr>;
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

  void print(llvm::raw_ostream &out, clang::ASTContext &Context) {
    for (auto &F: Funcs) {
      F.get()->print(out, Context);
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