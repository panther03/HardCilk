#include <clang/AST/ASTConsumer.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>

#include <iostream>

#include "Cilk2IR.hpp"
#include "IR.hpp"
#include "CreateContinuationPaths.hpp"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;
using namespace clang::driver;

// static cl::OptionCategory MyToolCategory("cilk2vitis options");
// static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// so much for header only...
void IRBasicBlock::iteratePreds(std::function<void(IRBasicBlock* B)> CB) {
  for (auto &B : *Parent) {
    auto &BSuccs = (B.get())->Succs;
    if (BSuccs.find(this) != BSuccs.end()) {
      CB(B.get());
    }
  }
}


class Cilk2Vitis : public clang::ASTConsumer {
private:
  clang::ASTContext *Context;
  PreprocessingRecord *PPRec;
  SourceManager &SM;

  IRProgram P;
  Cilk2IRVisitor Visitor;

public:
  explicit Cilk2Vitis(clang::ASTContext *Context, PreprocessingRecord *PPRec,
                      SourceManager &SM)
      : Context(Context), PPRec(PPRec), SM(SM), Visitor(Context, P) {}

  void HandleTranslationUnit(clang::ASTContext &Context) {
    // Only visit declarations declared in the input TU
    auto Decls = Context.getTranslationUnitDecl()->decls();
    for (auto &Decl : Decls) {
      // Ignore declarations out of the main translation unit.
      //
      // SourceManager::isInMainFile method takes into account locations
      // expansion like macro expansion scenario and checks expansion
      // location instead if spelling location if required.
      if (!SM.isInMainFile(Decl->getLocation()))
        continue;
      Visitor.TraverseDecl(Decl);
    }

    P.print(Context);

    for (auto &F: P) {
      F.get()->dumpGraph(Context);
      CreateContinuationPaths(*F.get());
      F.get()->dumpGraph(Context);
      
      for (auto &B: *F.get()) {
        for (auto &S: *B.get()) {
          S->printAllIdentifiers();
        }
      }
    }
  }
};

// Frontened action to create the custom AST consumer
class Cilk2VitisAction : public clang::ASTFrontendAction {
public:
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &CI, StringRef file) override {
    clang::Preprocessor &PP = CI.getPreprocessor();
    PP.enableIncrementalProcessing();
    if (!PP.getPreprocessingRecord()) {
      PP.createPreprocessingRecord();
    }
    clang::PreprocessingRecord *PPRec = PP.getPreprocessingRecord();

    return std::make_unique<Cilk2Vitis>(&CI.getASTContext(), PPRec,
                                        CI.getSourceManager());
  }

  /*std::string extractFileName() {
    const clang::SourceManager &SM = getCompilerInstance().getSourceManager();

    const FileEntry *MainFileEntry = SM.getFileEntryForID(SM.getMainFileID());

    std::filesystem::path filePath(MainFileEntry->getName().str());
    std::string parentDir = filePath.parent_path().string();
    std::string name = filePath.stem().string();

    return (parentDir + "/" + name + "_cilk.cpp");
  }*/

  void EndSourceFileAction() override {
    std::cout << "end" << std::endl;
    /*
    clang::ASTContext &Context = getCompilerInstance().getASTContext();
    std::error_code EC;
    std::string outFilename = extractFileName();
    llvm::raw_fd_ostream outFile(outFilename, EC, llvm::sys::fs::OF_None);

    TheRewriter.getEditBuffer(Context.getSourceManager().getMainFileID())
        .write(outFile);
    outFile.close();
    */
  }
};

int main(int argc, const char **argv) {
  if (argc < 2) {
    std::cerr << "Expected path to input OpenCilk (C++) file." << std::endl;
    return 1;
  }

  // std::string inFilename = argv[1];
  // std::filesystem::path filePath(inFilename);
  // std::string parentDir = filePath.parent_path().string();
  // std::string name = filePath.stem().string();
  // std::string outFilename = parentDir + "/" + name + ".json";

  std::vector<std::string> compilationFlags = {
      "/opt/OpenCilk/bin/clang",
      "-c",
      "-w",
      "-fopencilk",
      "-O3",
      "-fsyntax-only",
      "-I/opt/OpenCilk/include",
      "-I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include"};

  compilationFlags.push_back(argv[1]);

  std::shared_ptr<clang::PCHContainerOperations> PCHContainerOps =
      std::make_shared<clang::PCHContainerOperations>();

  clang::FileSystemOptions FSOpts;
  llvm::IntrusiveRefCntPtr<clang::FileManager> Files(
      new clang::FileManager(FSOpts));

  clang::tooling::ToolInvocation invocation(
      compilationFlags, std::make_unique<Cilk2VitisAction>(), Files.get(),
      PCHContainerOps);

  return !invocation.run();
}