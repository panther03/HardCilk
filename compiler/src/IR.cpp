#include "IR.hpp"
#include "util.hpp"
#include "clang/AST/Expr.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/StmtCilk.h>
#include <memory>
#include <regex>


void IRExprVisitor::Visit(IRExpr *E) {
  Depth++;
  switch (E->getKind()) {
    case IRExpr::EXK_BINOP: VisitBinop(llvm::dyn_cast<BinopIRExpr>(E)); return;
    case IRExpr::EXK_UNOP: VisitUnop(llvm::dyn_cast<UnopIRExpr>(E)); return;
    case IRExpr::EXK_CALL: VisitCall(llvm::dyn_cast<CallIRExpr>(E)); return;
    case IRExpr::EXK_ISPAWN: VisitISpawn(llvm::dyn_cast<ISpawnIRExpr>(E)); return;
    case IRExpr::EXK_FIDENT: VisitFIdent(llvm::dyn_cast<FIdentIRExpr>(E)); return;
    case IRExpr::EXK_REF: VisitRef(llvm::dyn_cast<RefIRExpr>(E)); return;
    case IRExpr::EXK_LITERAL: VisitLiteral(llvm::dyn_cast<LiteralIRExpr>(E)); return;
    case IRExpr::EXK_LVAL_IDENT: VisitIdent(llvm::dyn_cast<IdentIRExpr>(E)); return;
    case IRExpr::EXK_LVAL_ACCESS: VisitAccess(llvm::dyn_cast<AccessIRExpr>(E)); return;
    case IRExpr::EXK_LVAL_DREF: VisitDRef(llvm::dyn_cast<DRefIRExpr>(E)); return;
    case IRExpr::EXK_LVAL_INDEX: VisitIndex(llvm::dyn_cast<IndexIRExpr>(E)); return;    
    default: PANIC("impossible expr");
  }
}

/////////////
// IRExpr //
///////////

void IndexIRExpr::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(Ind);
  Out << "&(" << Arr->Name << "[";
  Ind->print(Out, Ctx);
  Out << "])";
}

void RefIRExpr::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(E);
  Out << "&(";
  E->print(Out, Ctx);
  Out << ")";
}

void DRefIRExpr::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(Expr);
  Out << "*(";
  Expr->print(Out, Ctx);
  Out << ")";
}

void AccessIRExpr::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(Struct);
  Out << Struct->Name;
  if (Arrow) {
    Out << "->";
  } else {
    Out << ".";
  }
  Out << Field;
}

void IdentIRExpr::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(Ident);
  Out << Ident->Name;
}

void FIdentIRExpr::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  if (auto *F = std::get_if<IRFunction *>(&FR)) {
    Out << (*F)->getName();
  } else {
    Out << std::get<ASTVarRef>(FR)->getName();
  }
}

void LiteralIRExpr::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(Lit);
  Lit->printPretty(Out, nullptr, Ctx.ASTCtx.getPrintingPolicy());
}

void BinopIRExpr::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(Left && Right);
  Out << "(";
  Left->print(Out, Ctx);
  Out << " ";
  printBinop(Out);
  Out << " ";
  Right->print(Out, Ctx);
  Out << ")";
}

void UnopIRExpr::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(Expr);
  Out << "(";
  const char *Op;
  if (printUnop(Op)) {
    Expr->print(Out, Ctx);
    Out << Op;
  } else { 
    Out << Op;
    Expr->print(Out, Ctx);
  }
  Out << ")";
}

void ISpawnIRExpr::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  Out << "spawn ";
  if (auto *F = std::get_if<IRFunction *>(&Fn)) {
    Out << (*F)->getName();
  } else {
    Out << std::get<ASTVarRef>(Fn)->getName();
  }
  Out << "(";
  bool first = true;
  for (auto &Arg : Args) {
    if (first) {
      first = false;
    } else {
      Out << ",";
    }
    Arg->print(Out, Ctx);
  }
  Out << ")";
}

void CallIRExpr::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  if (auto *F = std::get_if<IRFunction *>(&Fn)) {
    Out << (*F)->getName();
  } else {
    Out << std::get<ASTVarRef>(Fn)->getName();
  }
  Out << "(";
  bool first = true;
  for (auto &Arg : Args) {
    if (first) {
      first = false;
    } else {
      Out << ",";
    }
    Arg->print(Out, Ctx);
  }
  Out << ")";
}

/////////////
// IRStmt //
///////////
void LoopIRStmt::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  if (Inc || Init) {
    Out << "for (";
  } else {
    Out << "while (";
  }

  if (Init) {
    Init->print(Out, Ctx);
  }
  Out << ";";
  Cond->print(Out, Ctx);
  Out << ";";
  if (Inc) {
    Inc->print(Out, Ctx);
  }
}

void IfIRStmt::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(Cond);
  Out << "if (";
    Cond->print(Out, Ctx);
  Out << ")";
}

void SpawnNextIRStmt::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(Fn);
  Out << "spawnNext ";
  Out << Fn->getName();
}

void ESpawnIRStmt::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(Dest);
  Out << "spawn ";
  Dest->print(Out, Ctx);
  Out << " ";
  if (auto *F = std::get_if<IRFunction *>(&Fn)) {
    Out << (*F)->getName();
  } else {
    Out << std::get<ASTVarRef>(Fn)->getName();
  }
  bool first = true;
  for (auto &Arg : Args) {
    if (first) {
      first = false;
    } else {
      Out << ",";
    }
    Arg->print(Out, Ctx);
  }
  Out << ";";
}

void ExprWrapIRStmt ::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(Expr);
  Expr->print(Out, Ctx);
  Out << ";";
}

void StoreIRStmt::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(Dest);
  Dest->print(Out, Ctx);
  Out << " = ";
  Src->print(Out, Ctx);
  Out << ";";
}

void CopyIRStmt::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  assert(Dest);
  Out << Dest->Name << " = ";
  Src->print(Out, Ctx);
  Out << ";";
}

void SyncIRStmt::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  Out << "sync;";
}

void ReturnIRStmt::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {
  Out << "return ";
  if (RetVal) {
    RetVal->print(Out, Ctx);
  }
  Out << ";";
}

void ScopeAnnotIRStmt::print(llvm::raw_ostream &Out, IRPrintContext &Ctx) {}

//void IRStmt::printAllIdentifiers() {
//  for (auto it = ExprIdentifierIterator(innerStmt); !it.done(); ++it) {
//    llvm::outs() << (*it)->getNameInfo().getAsString() << "\n";
//  }
//}

///////////////////
// IRBasicBlock //
/////////////////
/*
void IRBasicBlock::iteratePreds(std::function<void(IRBasicBlock *B)> CB) {
  for (auto &B : *Parent) {
    auto &BSuccs = (B.get())->Succs;
    if (BSuccs.contains(this)) {
      CB(B.get());
    }
  }
}

void IRBasicBlock::clone(IRBasicBlock *Dest) {
  for (auto &Stmt : Stmts) {
    Dest->pushStmtBack(new IRStmt(Stmt.get()->innerStmt, Stmt.get()->Lhs));
  }
  if (Terminator != nullptr) {
    Dest->Terminator = std::make_unique<IRStmt>(Terminator.get()->innerStmt);
  }
}

void IRBasicBlock::graphPrintStmt(llvm::raw_ostream &out,
                                  clang::ASTContext &Context, const Stmt *S,
                                  const char *NewlineSymbol) {
  llvm::SmallString<256> MsgBuffer;
  llvm::raw_svector_ostream Msg(MsgBuffer);

  S->printPretty(Msg, nullptr, Context.getPrintingPolicy(), 0U, NewlineSymbol);

  // uhhhhhh
  // yyea
  auto s = std::regex_replace(MsgBuffer.str().str(), std::regex("<"), "\\<");
  // s = std::regex_replace(s, std::regex(">"), "\\>");
  out << s;
}

void IRBasicBlock::print(llvm::raw_ostream &out, clang::ASTContext &Context,
                         const char *NewlineSymbol) {
  int I = 1;
  int j = 0;
  for (auto &Stmt : Stmts) {
    out << "   " << I << ": ";
    if (Stmt->Lhs) {
      out << Stmt->Lhs->getName() << " = ";
      j++;
      I++;
      continue;
    }
    if (Stmt->Kind == IRStmt::ForInc) {
      out << "ForInc" << NewlineSymbol;
    } else if (Stmt->Kind == IRStmt::ForInit) {
      out << "ForInit" << NewlineSymbol;
    } else if (Stmt->Kind == IRStmt::SpawnNextDecl) {
      assert(Stmt->SpawnNextDest);
      out << "spawnNextDecl fn" << Stmt->SpawnNextDest->Ind
          << NewlineSymbol;
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
    } else if (isa<CilkSyncStmt>(S)) {
      if (Terminator->Kind == IRStmt::SpawnNext) {
        assert(Terminator->SpawnNextDest);
        out << "   T: spawnNext fn" << Terminator->SpawnNextDest->Ind
            << NewlineSymbol;
      } else {
        out << "   T: sync" << NewlineSymbol;
      }
    }
  }
}

void IRBasicBlock::dumpGraph(llvm::raw_ostream &out,
                             clang::ASTContext &Context) {
  out << "\"{ [B" << getInd();
  out << "]\\l";
  print(out, Context, "\\l");
  out << "}\"";
}

/////////////////
// IRFunction //
///////////////
*/

IRBasicBlock *IRFunction::createBlock() {
  IRBlockPtr B = std::make_unique<IRBasicBlock>(Blocks.size(), this);
  IRBasicBlock *Bp = B.get();
  Blocks.push_back(std::move(B));
  return Bp;
}

/*
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
} */

/*
void IRFunction::print(llvm::raw_ostream &out, clang::ASTContext &Context) {
  int i = 0;
  for (auto &B : Blocks) {
    fprintf(stdout, BHGREEN "Block %d" COLOR_RESET "\n", i);
    out << "PREDS: ";
    B->iteratePreds(
        [&](IRBasicBlock *Pred) -> void { out << Pred->getInd() << " "; });
    out << "\n";
    B->print(out, Context, "\n");
    out << "SUCCS: ";
    for (auto *Succ : B->Succs) {
      out << Succ->getInd() << " ";
    }
    out << "\n\n\n";
    i++;
  }
}

void IRFunction::dumpGraph(llvm::raw_ostream &out, clang::ASTContext &Context) {
  out << "subgraph clusterfn" << Ind;
  if (RootFun) {
    out << "{\nlabel=\"" << RootFun->getName() << "\"\n";
  } else {
    out << "{\nlabel=\"fn" << Ind << "\"\n";
  }

  for (auto &B : Blocks) {
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

  for (auto &B : Blocks) {
    for (auto &Succ : B.get()->Succs) {
      out << "    Node" << Ind << "_" << B.get()->getInd();
      out << " -> Node" << Ind << "_" << Succ->getInd();
      out << ";\n";
    }
    const IRBasicBlock *HasSpawnToSpawnNext = nullptr;
    for (auto &S : *B) {
      if ((Spawn2SpawnNext.find(S.get()) != Spawn2SpawnNext.end())) {
        HasSpawnToSpawnNext = Spawn2SpawnNext[S.get()];
        break;
      }
    }
    if (HasSpawnToSpawnNext) {
      out << "    \"Node" << Ind << "_" << B->getInd();
      out << "\" -> \"Node" << Ind << "_" << HasSpawnToSpawnNext->getInd()
          << "\"";
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
*/

////////////////
// IRPRogram //
//////////////

IRFunction *IRProgram::createFunc() {
  IRFuncPtr F = std::make_unique<IRFunction>(Funcs.size(), this);
  IRFunction *Fp = F.get();
  Funcs.push_back(std::move(F));
  return Fp;
}
/*

void IRProgram::print(llvm::raw_ostream &out, clang::ASTContext &Context) {
  for (auto &F : Funcs) {
    F.get()->print(out, Context);
  }
}

void IRProgram::dumpGraph(llvm::raw_ostream &out, clang::ASTContext &Context) {
  out << "digraph unnamed {\ncompound=true;\n";
  for (auto &F : Funcs) {
    F->dumpGraph(out, Context);
  }
  for (auto &F : Funcs) {
    for (const auto &[B, SnD] : F->SpawnNext2Cont) {
      out << "    \"Node" << F->Ind << "_" << B->getInd();
      out << "\" -> \"Node" << SnD->Ind << "_" << 0 << "\"";
      out << "  [style=\"dashed\" color=\"red\" lhead=clusterfn";
      out << SnD->Ind << "];\n";
    }
  }
  out << "}\n";
}

////////////////////////
// ScopedIRTraverser //
//////////////////////

IRBasicBlock* FindJoin(IRBasicBlock* Left, IRBasicBlock *Right) {
  std::vector<IRBasicBlock*> WorkList;
  std::unordered_map<IRBasicBlock*, bool> Seen;

  WorkList.push_back(Left);
  while (!WorkList.empty()) {
    auto *B = WorkList.back();
    WorkList.pop_back();
    
    if (Seen.find(B) != Seen.end()) continue;
    Seen[B] = true;
    for (auto *Succ: B->Succs) {
      WorkList.push_back(Succ);
    }
  }

  WorkList.push_back(Right);
  while (!WorkList.empty()) {
    auto *B = WorkList.back();
    WorkList.pop_back();

    if (Seen.find(B) != Seen.end()) {
      if (Seen[B]) {
        return B;
      } else {
        continue;
      }
    } 
    Seen[B] = false;
    for (auto *Succ: B->Succs) {
      WorkList.push_back(Succ);
    }
  }
  return nullptr;
}


void ScopedIRTraverser::traverse(IRFunction &F) {
  WorkList.push_back(WorkItem(F.entry()));

  while (!WorkList.empty()) {
    auto W = WorkList.back();
    WorkList.pop_back();
    
    if (W.B) {
      assert(W.SE == None);
      auto B = W.B;

      int JC = 1;
      if (JoinCounts.find(B) != JoinCounts.end()) {
        JC = JoinCounts[B];
      }
      JC--;
      JoinCounts[B] = JC;
  
      if (JC != 0) {
        continue;
      } 

      visitBlock(B);

      auto *Trm = B->Terminator ? B->Terminator->innerStmt : nullptr;
      if (Trm && isa<IfStmt>(Trm)) {
        auto *ThenB = B->Succs[0];
        auto *ElseB = B->Succs[1];
        auto *JoinB = FindJoin(ThenB, ElseB);
        assert(JoinB != ThenB);
        if (JoinB) {
          WorkList.push_back(WorkItem(JoinB));
          JoinCounts[JoinB] = 2;
        }
        WorkList.push_back(WorkItem(Close));
        if (ElseB && ElseB != JoinB) {
          WorkList.push_back(WorkItem(ElseB));
          WorkList.push_back(WorkItem(Else));
          JoinCounts[JoinB] += 1;
        }
        WorkList.push_back(WorkItem(ThenB));
        WorkList.push_back(WorkItem(Open));
      } else if (Trm && isa<ForStmt>(Trm) ) {
        // && B->Terminator->Kind == IRStmt::Default
        auto *BodyB = B->Succs[0];
        auto *AfterB = B->Succs[1];
        // The common successor of the body and the loop itself should be the loop.
        assert(FindJoin(BodyB, B) == B);
  
        // we don't need a join count for AfterB because 
        // it will be looped back already
        WorkList.push_back(WorkItem(AfterB));
        WorkList.push_back(WorkItem(Close));
        WorkList.push_back(WorkItem(BodyB));
        WorkList.push_back(WorkItem(Open));
      } else {
        assert(B->Succs.size() <= 1);
        for (auto *Succ: B->Succs) {
          WorkList.push_back(WorkItem(Succ));
        }
      }
    } else {
      assert(W.SE != None);
      handleScope(W.SE);
    }
  }
}*/