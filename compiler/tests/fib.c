#include <stdio.h>
#include <stdlib.h>

#include <cilk/cilk.h>


long fib(long n) {
  if (n < 2)
    return n;

  long x, y, z, w, a;

  x = cilk_spawn fib(n-1);
  y = cilk_spawn fib(n-2);
  cilk_sync;
  a = cilk_spawn fib(x-1);
  z = cilk_spawn fib(n-3);
  cilk_sync;
  
  w = x+y + a*z;
  return w;
}

/*

void fib(cont int k, long n) {
  if (n < 2) {
    send_argument(k, n);
  } else {
    cont int x,y;
    spawn_next(sum(k, ?x, ?y));
    spawn(fib(x, n-2));
    spawn(fib(y, n-1));
  }
}

void sum(cont int k, long x, long y) {
  send_argument(k, x+y);
}
*/