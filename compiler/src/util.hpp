#pragma once
#include <clang/AST/Stmt.h>
#include <clang/AST/Expr.h>

#include <stdio.h>
#include <deque>

#define BRED "\e[1;31m"
#define BHGREEN "\e[1;92m"
#define BHBLK "\e[1;90m"
#define COLOR_RESET "\e[0m"
#define PANIC(...)                                                             \
  fprintf(stderr,                                                              \
          "[" BRED "panic" COLOR_RESET "@" BHBLK "%s:%d" COLOR_RESET "(%s)] ", \
          __FILE__, __LINE__, __func__);                                       \
  fprintf(stderr, __VA_ARGS__);                                                \
  fprintf(stderr, "\n");                                                       \
  exit(EXIT_FAILURE);

using namespace clang;

struct ExprIdentifierIterator {
private:
  std::vector<const Expr *> WorkList;
  std::deque<const DeclRefExpr *> ReturnQueue;

public:
  ExprIdentifierIterator(const clang::Stmt *Stmt) {
    if (const Expr *E = dyn_cast<Expr>(Stmt)) {
      WorkList.push_back(E);
      ++(*this);
    }
  }

  const DeclRefExpr *operator*() const { return ReturnQueue.back(); }

  ExprIdentifierIterator &operator++() {
    if (!ReturnQueue.empty()) {
      ReturnQueue.pop_back();
    }
    while (ReturnQueue.empty() && !WorkList.empty()) {
      const Expr *E = WorkList.back();
      WorkList.pop_back();
      for (const auto &C : E->children()) {
        if (const auto *ID = dyn_cast<DeclRefExpr>(C)) {
          ReturnQueue.push_front(ID);
        } else if (const Expr *CE = dyn_cast<Expr>(C)) {
          WorkList.push_back(CE);
        }
      }
    }
    return *this;
  }

  bool done() { return ReturnQueue.empty(); }
};