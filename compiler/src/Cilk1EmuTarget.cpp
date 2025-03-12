#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <llvm/Support/raw_ostream.h>

#include "Cilk1EmuTarget.hpp"
#include "IR.hpp"
#include "util.hpp"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCilk.h"
#include "clang/AST/Stmt.h"

#define TAB "    "

////////////////////////////////////////
// 1. Forward declarations, closures //
//////////////////////////////////////

void printFunDecl(IRFunction *F, llvm::raw_ostream &out, clang::ASTContext &C) {
  if (F->RootFun) {
    if (F->NeedsCont) {
      out << "THREAD(" << F->RootFun->getName() << ")";
    } else {
      out << F->RootFun->getReturnType().getAsString();
      out << " " << F->RootFun->getName() << "(";
      bool first = true;
      for (auto &ArgDecl : F->RootFun->parameters()) {
        if (!first) {
          out << ", ";
        } else {
          first = false;
        }
        ArgDecl->print(out, 0);
      }
      out << ")";
    }
  } else {
    out << "THREAD(sn_" << F->getInd() << ")";
  }
}

void printClosureDecl(IRFunction *F, llvm::raw_ostream &out,
                      clang::ASTContext &C) {
  out << "CLOSURE_DEF(";
  F->printName(out);
  out << ",\n";
  for (auto *Arg : F->Args) {
    out << TAB;
    Arg->print(out, C.getPrintingPolicy());
    out << ";\n";
  }
  for (auto *Mat : F->Materialized) {
    if (auto *MatV = dyn_cast<VarDecl>(Mat)) {
      out << TAB;
      MatV->getType().print(out, C.getPrintingPolicy());
      out << " " << MatV->getName() << ";\n";
    }
  }
  out << ");\n";
}

/////////////////////////////////////////////////////
// 2. Rewrite source to remove modified functions //
///////////////////////////////////////////////////

void printOriginalSource(IRProgram &P, llvm::raw_ostream &out,
                         clang::ASTContext &C, clang::CompilerInstance &CI) {
  Rewriter R;
  SourceManager &SM = CI.getSourceManager();
  R.setSourceMgr(SM, CI.getLangOpts());
  for (auto &F: P) {
    if (F->RootFun) {
      R.RemoveText(F->RootFun->getSourceRange());
    }
  }
  R.getEditBuffer(SM.getMainFileID()).write(out);
}

////////////////////////////
// 3. Print IR Functions //
//////////////////////////

struct DeclInfo {
  int ReuseCnt;
  enum { Arg, Local } Type;
};

// https://stackoverflow.com/questions/32685540/why-cant-i-compile-an-unordered-map-with-a-pair-as-key
struct pair_hash {
  template <class T1, class T2>
  std::size_t operator () (const std::pair<T1,T2> &p) const {
      auto h1 = std::hash<T1>{}(p.first);
      auto h2 = std::hash<T2>{}(p.second);

      // Mainly for demonstration purposes, i.e. works but is overly simple
      // In the real world, use sth. like boost.hash_combine
      return h1 ^ h2;  
  }
};

typedef std::unordered_map<std::pair<IRFunction*, IRVarRef>, DeclInfo, pair_hash> DeclMap;
#define DM_ENT(F,VR) (std::make_pair(F,VR))

void DeclMapInit(DeclMap &DM, IRFunction *F) {
  std::unordered_map<std::string, int> ReuseCnts;
  for (auto Arg : F->Args) {
    DM[DM_ENT(F, Arg)] = DeclInfo{.Type = DeclInfo::Arg};
    ReuseCnts[Arg->getDeclName().getAsString()] = 0;
  }
  for (auto Arg : F->Materialized) {
    DM[DM_ENT(F, Arg)] = DeclInfo{.Type = DeclInfo::Arg};
    ReuseCnts[Arg->getDeclName().getAsString()] = 0;
  }
  for (auto Local : F->Locals) {
    std::string LocalName = Local->getNameAsString();
    if (ReuseCnts.find(LocalName) == ReuseCnts.end()) {
      ReuseCnts[LocalName] = 0;
    } else {
      ReuseCnts[LocalName] += 1;
    }
    DM[DM_ENT(F, Local)] =
        DeclInfo{.ReuseCnt = ReuseCnts[LocalName], .Type = DeclInfo::Local};
  }
}

bool DeclMapLookup(DeclMap &DM, IRFunction *F, IRVarRef VR, std::string &replaced) {
  if (DM.find(DM_ENT(F,VR)) == DM.end()) {
    return false;
  }
  auto &DI = DM[DM_ENT(F,VR)];

  switch (DI.Type) {
    case DeclInfo::Arg: {
      replaced = "largs->" + VR->getName().str();
      return true;
    }
    case DeclInfo::Local: {
      if (DI.ReuseCnt > 0) {
        replaced = VR->getName().str() + std::to_string(DI.ReuseCnt);
        return true;
      } else {
        return false;
      }
    }
  }
  return false;
}

void DeclMapPrint(DeclMap &DM) {
  for (auto const& [P, DI] : DM)  {
    auto const& [F, VR] = P;
    F->printName(llvm::outs());
    llvm::outs() << "," << VR->getName() << " -> ";
    if (DI.Type == DeclInfo::Arg) {
      llvm::outs() << "Arg\n";
    } else {
      llvm::outs() << "Local (RC " << DI.ReuseCnt << ")\n";
    }
  }
}

void printLocals(DeclMap &DM, IRFunction *F, clang::ASTContext &C, llvm::raw_ostream &Out) {
  
  for (auto *Local: F->Locals) {
    if (const auto *VL = dyn_cast<VarDecl>(Local)) {
      DeclInfo DI = DM[DM_ENT(F, Local)];
      Out << TAB;
      VL->getType().print(Out, C.getPrintingPolicy());
      Out << " ";
      Out << VL->getName();
      if (DI.ReuseCnt > 0) {
        Out << DI.ReuseCnt;
      }
      Out << ";\n";
    } else {
      PANIC("Unsupported local type, tried to cast local to VarDecl\n");
    }
  }
}

class StmtNameRemapper : public clang::RecursiveASTVisitor<StmtNameRemapper> {
private:
  DeclMap &DM;
  Rewriter &R;
public: 
  IRFunction *F;
  explicit StmtNameRemapper(DeclMap &DM, Rewriter &R): DM(DM), R(R) {}

  bool VisitDeclRefExpr(clang::DeclRefExpr *Dre) {
    if (DM.find(DM_ENT(F, Dre->getDecl())) != DM.end()) {  
      std::string replaced;
      if (DeclMapLookup(DM, F, Dre->getDecl(), replaced)) {
        R.ReplaceText(Dre->getSourceRange(), replaced);
      }
    }
    return true;
  }
};

class Cilk1EmuPrinter : public ScopedIRTraverser {
private:
  DeclMap &DM;
  Rewriter &R;
  llvm::raw_ostream &Out;
  clang::ASTContext &C;
  int SpawnCtr = 0;
  int Indent = 1;

  void printIndentation() {
    for (int i = 0; i < Indent; i++) Out << TAB;
  }

  void handleScope(ScopeEvent SE) override {
    switch (SE) { 
      case ScopeEvent::Close: {
        assert(Indent > 0);
        Indent--;
        printIndentation();
        Out << "}\n";
        break;
      }
      case ScopeEvent::Open: {
        Out << " {\n";
        Indent++;
        break;
      }
      case ScopeEvent::Else: {
        assert(Indent > 0);
        Indent--;
        printIndentation();
        Out << "} else {\n";
        Indent++;
        break;
      }
      default: {}
    }
  }

  void handleSpawnNextDecl(IRStmt *S, IRFunction *F) {
    assert(S->Kind == IRStmt::SpawnNextDecl);
    std::string SpawnNextFnName = "sn_" + std::to_string(S->SpawnNextDest->getInd());
    printIndentation();
    Out << SpawnNextFnName << "_closure " << "SN_" << SpawnNextFnName << "c";
    if (F->NeedsCont) {
      Out << "(largs->k);\n";
    } else {
      Out << "(CONT_DUMMY);\n";
    }
    printIndentation();
    Out << "spawn_next<" << SpawnNextFnName << "_closure> " << "SN_" << SpawnNextFnName << "(SN_" << SpawnNextFnName << "c);\n";
    
  }

  void handleSpawnNext(IRStmt *S, IRFunction *F) {
    assert(S->Kind == IRStmt::SpawnNext);
    std::string SpawnNextFnName = "sn_" + std::to_string(S->SpawnNextDest->getInd());
    for (auto *Arg : S->SpawnNextDest->Args) {
      printIndentation();
      Out << "((" << SpawnNextFnName << "_closure*)SN_" << SpawnNextFnName << ".cls.get())->" << Arg->getName() << " = ";
      std::string Name = Arg->getName().str();
      DeclMapLookup(DM, F, Arg, Name);
      Out << Name << ";\n";
    }
    printIndentation();
    Out << "// Original sync was here\n";
  }

  void handleSpawn(IRStmt *S, IRFunction *F) {
    auto *CSE = dyn_cast<CilkSpawnExpr>(S->innerStmt);
    assert(CSE);
    auto *CallE = dyn_cast<CallExpr>(CSE->getSpawnedExpr());
    assert(CallE);
    auto *CalleeND = dyn_cast<FunctionDecl>(CallE->getCalleeDecl());
    assert(CalleeND);
    std::string SpawnFnName = CalleeND->getName().str();
    printIndentation();
    Out << "cont sp" << SpawnCtr << "k;\n";
    assert(S->Lhs);
    auto *ContF = F->SpawnNext2Cont[F->Spawn2SpawnNext[S]];
    assert(ContF);
    printIndentation();
    Out << "SN_BIND(SN_sn_" << ContF->getInd() << ", &sp" << SpawnCtr << "k, " << S->Lhs->getName() << ");\n";
    printIndentation();
    Out << SpawnFnName << "_closure sp" << SpawnCtr << "c(sp" << SpawnCtr << "k);\n";

    int Ind = 0;

    for (auto *Arg: CallE->arguments()) {
      printIndentation();
      Out << "sp" << SpawnCtr << "c." << CalleeND->getParamDecl(Ind)->getName();
      Out << " = " << R.getRewrittenText(Arg->getSourceRange()) << ";\n";
      Ind++;
    }

    printIndentation();
    Out << "spawn<" << SpawnFnName << "_closure> sp"  << SpawnCtr << "(sp" << SpawnCtr << "c);\n\n";
  }

  void visitStmt(IRStmt *S, IRBasicBlock *B) {
    auto *F = B->getParent();
    switch (S->Kind) {
      case IRStmt::ForInc: return;
      case IRStmt::ForInit: return;
      case IRStmt::SpawnNextDecl: handleSpawnNextDecl(S, F); break;
      case IRStmt::SpawnNext: handleSpawnNext(S, F); break;
      default: {
        if (isa<NullStmt>(S->innerStmt)) {
          return;
        }

        if (isa<CilkSpawnExpr>(S->innerStmt)) {
          handleSpawn(S, F);
          SpawnCtr++;
          return;
        }

        printIndentation();

        if (S->Lhs) {
          std::string LhsName = S->Lhs->getName().str();
          DeclMapLookup(DM, F, S->Lhs, LhsName);
          Out << LhsName << " = ";
        }
//

        if (auto *RS = dyn_cast<ReturnStmt>(S->innerStmt)) {
          if (F->NeedsCont) {
            Out << "SEND_ARGUMENT(largs->k, " << R.getRewrittenText(RS->getRetValue()->getSourceRange()) << ");\n"; 
          } else {
            Out << R.getRewrittenText(RS->getSourceRange()) << ";\n";
          }
        } else if (auto *FS = dyn_cast<ForStmt>(S->innerStmt)) {
          Out << "for (";
          if (auto *ID = dyn_cast<DeclStmt>(FS->getInit())) {
            if (ID->child_begin() != ID->child_end()) {
              auto *LoopInitDecl = dyn_cast<NamedDecl>(ID->getSingleDecl());
              assert(LoopInitDecl);
              std::string InitVarName = LoopInitDecl->getName().str();
              DeclMapLookup(DM, F, LoopInitDecl, InitVarName);
              Out << InitVarName << " = ";
              Out << R.getRewrittenText((*ID->child_begin())->getSourceRange());
            }
          } else {
            Out << R.getRewrittenText(FS->getInit()->getSourceRange());
          }
          Out << "; ";
          Out << R.getRewrittenText(FS->getCond()->getSourceRange()) << "; ";
          Out << R.getRewrittenText(FS->getInc()->getSourceRange()) << ")";
        } else if (auto *IS = dyn_cast<IfStmt>(S->innerStmt)) {
          Out << "if (" << R.getRewrittenText(IS->getCond()->getSourceRange()) << ")";
        } else {
          Out << R.getRewrittenText(S->innerStmt->getSourceRange());
          if (isa<Expr>(S->innerStmt)) {
            Out << ";\n";
          }
        }
        break;
      }
    }
  }

  void visitBlock(IRBasicBlock *B) override {

    for (auto &S: *B) {
      visitStmt(S.get(), B);
    }
    if (B->Terminator) visitStmt(B->Terminator.get(), B);
  }

public:
  Cilk1EmuPrinter(DeclMap &DM, llvm::raw_ostream &Out, clang::ASTContext &C, Rewriter &R) : DM(DM), Out(Out), C(C), R(R) {}
};

void PrintCilk1Emu(IRProgram &P, llvm::raw_ostream &out, clang::ASTContext &C,
                   clang::CompilerInstance &CI) {
  // 1. Print forward declarations of each function, include Cilk1 emulation
  // file.
  out << "#include \"cilk_explicit.hh\"\n";
  for (auto &F : P) {
    printFunDecl(F.get(), out, C);
    out << ";\n";
  }
  out << "\n";
  for (auto &F : P) {
    if (F->NeedsCont) {
      printClosureDecl(F.get(), out, C);
    }
  }
  // 2. Print the original source file with the original root functions removed.
  printOriginalSource(P, out, C, CI);

  // 3. Print the implementation of each function.
  DeclMap DM;
  for (auto &F: P) {
    DeclMapInit(DM, F.get());
  }

  Rewriter R;
  R.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
  StmtNameRemapper SMR(DM, R);
  // whole thing will implode if statements are visited by twice
  // very hacked together
  for (auto &F: P) {
    SMR.F = F.get();
    for (auto &B: *F) {
      for (auto &S: *B) {
        if (S->Kind != IRStmt::ForInc && S->Kind != IRStmt::ForInit) {
          SMR.TraverseStmt(const_cast<Stmt*>(S->innerStmt));
        }
      }
      if (B->Terminator) {
        auto *T = B->Terminator.get();
        if (auto *FS = dyn_cast<ForStmt>(T->innerStmt)) {
          SMR.TraverseStmt(const_cast<Expr*>(FS->getCond()));
          SMR.TraverseStmt(const_cast<Stmt*>(FS->getInit()));
          SMR.TraverseStmt(const_cast<Expr*>(FS->getInc()));
        } else if (auto *IS = dyn_cast<IfStmt>(T->innerStmt)) {
          SMR.TraverseStmt(const_cast<Expr*>(IS->getCond()));
        } else {
          SMR.TraverseStmt(const_cast<Stmt*>(T->innerStmt));
          
        }
      }
    }
  }
  //R.getEditBuffer(CI.getSourceManager().getMainFileID()).write(out);
  
  for (auto &F: P) {
    
    printFunDecl(F.get(), out, C);
    out << " {\n";

    printLocals(DM, F.get(), C, out);

    if (F->NeedsCont) {
      out << TAB;
      F->printName(out);
      out << "_closure *largs = (";
      F->printName(out);
      out << "_closure*)(args.get());\n";
    }

    
    Cilk1EmuPrinter Printer(DM, out, C, R);
    Printer.traverse(*F);
    if (F->NeedsCont) {
      out << "    return;\n";
    }
    // great hack
    if (F->RootFun && F->RootFun->getName() == "main") {
      out << "    return 0;\n";
    }
    out << "}\n";
  }
}