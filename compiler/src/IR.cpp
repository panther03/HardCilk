#include "IR.hpp"
#include "util.hpp"
#include "llvm/Support/raw_ostream.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/StmtCilk.h>
#include <memory>
#include <regex>

/////////////
// IRStmt //
///////////
void IRStmt::printAllIdentifiers() {
    for (auto it = ExprIdentifierIterator(innerStmt); !it.done(); ++it) {
        llvm::outs() << (*it)->getNameInfo().getAsString() << "\n";
    }
}

///////////////////
// IRBasicBlock //
/////////////////
void IRBasicBlock::iteratePreds(std::function<void(IRBasicBlock* B)> CB) {
  for (auto &B : *Parent) {
    auto &BSuccs = (B.get())->Succs;
    if (BSuccs.find(this) != BSuccs.end()) {
      CB(B.get());
    }
  }
}

void IRBasicBlock::clone(IRBasicBlock *Dest) {
    for (auto &Stmt : Stmts) {
      Dest->pushStmt(new IRStmt(Stmt.get()->innerStmt, Stmt.get()->Lhs));
    }
    if (Terminator != nullptr) {
      Dest->Terminator = std::make_unique<IRStmt>(Terminator.get()->innerStmt);
    }
}

void IRBasicBlock::graphPrintStmt(llvm::raw_ostream &out, clang::ASTContext &Context, const Stmt* S, const char *NewlineSymbol) {
  llvm::SmallString<256> MsgBuffer;
  llvm::raw_svector_ostream Msg(MsgBuffer);

  S->printPretty(Msg, nullptr, Context.getPrintingPolicy(), 0U, NewlineSymbol);
  
  // uhhhhhh
  // yyea
  auto s = std::regex_replace(MsgBuffer.str().str(), std::regex("<"), "\\<");
  //s = std::regex_replace(s, std::regex(">"), "\\>");
  out << s;
}

void IRBasicBlock::print(llvm::raw_ostream &out, clang::ASTContext &Context, const char* NewlineSymbol) {
  int I = 1;
  int j = 0;
  for (auto &Stmt : Stmts) {
    out << "   " << I << ": ";
    if (Stmt->Lhs) {
      out << Stmt->Lhs->getName() << " = ";
      j++;
    }
    if (Stmt->Kind == IRStmt::ForInc) {
      out << "ForInc" << NewlineSymbol;
    } else if (Stmt->Kind == IRStmt::ForInit) {
      out << "ForInit" << NewlineSymbol;
    } else {
      graphPrintStmt(out, Context, Stmt->innerStmt, NewlineSymbol);
      if (isa<Expr>(Stmt->innerStmt)) 
        out << ";" << NewlineSymbol;
    }
    
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
      if (Terminator->Kind == IRStmt::SpawnNext) {
        assert(Terminator->SpawnNextDest);
        out << "   T: spawnNext fn" << Terminator->SpawnNextDest->Ind << NewlineSymbol;
      } else {
        out << "   T: sync" << NewlineSymbol;
      }
      
    }
  }
}

void IRBasicBlock::dumpGraph(llvm::raw_ostream &out, clang::ASTContext &Context) {
  out << "\"{ [B" << getInd();
  out << "]\\l";
  print(out, Context, "\\l");
  out << "}\"";
}

/////////////////
// IRFunction //
///////////////

IRBasicBlock* IRFunction::createBlock() {
  IRBlockPtr B = std::make_unique<IRBasicBlock>(Blocks.size(), this);
  IRBasicBlock *Bp = B.get();
  Blocks.push_back(std::move(B));
  return Bp;
}

void IRFunction::print(llvm::raw_ostream &out, clang::ASTContext &Context) {
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

void IRFunction::dumpGraph(llvm::raw_ostream &out, clang::ASTContext &Context) {
  out << "subgraph clusterfn" << Ind;
  if (RootFun) {
    out << "{\nlabel=\""  << RootFun->getName() << "\"\n";
  } else {
    out << "{\nlabel=\"fn" << Ind << "\"\n";
  }
  
  for (auto &B: Blocks) {
    auto *BB = B.get();
    out << "    Node" << Ind << "_" << BB->getInd();
    out << " [shape=record,";
    if (B.get() == Entry) {
      out << "fontcolor=\"blue\",color=\"blue\",";
    }
    out << "label=";
    BB->dumpGraph(out, Context);
    out << " ];\n";
  }
  
  for (auto &B: Blocks) {
    for (auto &Succ: B.get()->Succs) {
      out << "    Node" << Ind << "_" << B.get()->getInd();
      out << " -> Node" << Ind << "_" << Succ->getInd();
      out << ";\n";
    }
    const IRBasicBlock* HasSpawnToSpawnNext = nullptr;
    for (auto &S: *B) {
      if ((Spawn2SpawnNext.find(S.get()) != Spawn2SpawnNext.end())) {
        HasSpawnToSpawnNext = Spawn2SpawnNext[S.get()];
        break;
      }
    }
    if (HasSpawnToSpawnNext) {
      out << "    \"Node" << Ind << "_" << B->getInd();
      out << "\" -> \"Node" << Ind << "_" << HasSpawnToSpawnNext->getInd() << "\"";
      out << "  [style=\"dashed\" color=\"green\"];\n";
    }
  }
  out << "}\n";
}

void IRFunction::dumpArgs(llvm::raw_ostream &out) {
  out << "\tArgs: ";
  for (auto *v : Args) {
    out << v->getName() << ", ";
  }
  out << "\n\tLocals: ";
  for (auto *v : Locals) {
    out << v->getName() << ", ";
  }
  if (!Materialized.empty()) {
    out << "\n\tMaterialized: ";
    for (auto *v : Materialized) {
      out << v->getName() << ", ";
    }
  }
  out << "\n";
}

void IRFunction::moveBlock(IRBasicBlock *B, IRFunction *Dest) {
  IRFunction::iterator BlockIt = begin();
  std::advance(BlockIt, B->getInd());
  IRBlockPtr OwnedB = std::move(*BlockIt);
  Blocks.erase(BlockIt);
  int I = 0;
  for (auto &MyB : Blocks) {
    MyB->Ind = I; 
    I++;
  }
  B->Ind = Dest->Blocks.size();
  if (B->Ind == 0) {
    Dest->Entry = B;
  }
  Dest->Blocks.push_back(std::move(OwnedB));
  B->Parent = Dest;
}

////////////////
// IRPRogram //
//////////////

IRFunction* IRProgram::createFunc() {
  IRFuncPtr F = std::make_unique<IRFunction>(Funcs.size(), this);
  IRFunction *Fp = F.get();
  Funcs.push_back(std::move(F));
  return Fp;
}

void IRProgram::print(llvm::raw_ostream &out, clang::ASTContext &Context) {
  for (auto &F: Funcs) {
    F.get()->print(out, Context);
  }
}

void IRProgram::dumpGraph(llvm::raw_ostream &out, clang::ASTContext &Context) {
  out << "digraph unnamed {\ncompound=true;\n";
  for (auto &F: Funcs) {
    F->dumpGraph(out, Context);
  }
  for (auto &F: Funcs) {
    for (const auto & [B, SnD] : F->SpawnNext2Cont) { 
      out << "    \"Node" << F->Ind << "_" << B->getInd();
      out << "\" -> \"Node" << SnD->Ind << "_" << 0 << "\"";
      out << "  [style=\"dashed\" color=\"red\" lhead=clusterfn";
      out << SnD->Ind << "];\n";
    }
  }
  out << "}\n";
}