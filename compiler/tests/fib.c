#include <cilk/cilk.h>


long fib(long n) {
  long w;
  long *p;
  if (n < 2) {
    w = n;
    *p = (long)&w + w;
    p[3] = w;
    p = &w;
    long z = *p;
  } else {
    long x, y, z, a;

    for (int n = 0; n < 8; n++) {
      x = cilk_spawn fib(n-1);
      y = cilk_spawn fib(n-2);
      cilk_sync;
    }
    a = cilk_spawn fib(x-1);
    z = cilk_spawn fib(n-3);
    cilk_sync;
    
    w = x+y + a*z;
  }
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