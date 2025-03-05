#include "cilk_explicit.hh"

THREAD(fib);
THREAD(sum);
THREAD(main_cont);


struct fib_closure : closure { 
    int n;

    task_fn_t getTask() override {
        return &fib;
    }
    fib_closure (cont k1, int n): n(n) {k = k1;}
};

struct sum_closure : closure { 
    int arg1;
    int arg2;

    task_fn_t getTask() override {
        return &sum;
    }

    sum_closure (cont k1, int arg1, int arg2): arg1(arg1), arg2(arg2) {k = k1;}
};

struct main_cont_closure: closure {
    int fibResult;

    task_fn_t getTask() override {
        return &main_cont;
    }

    main_cont_closure (cont k1, int fibResult): fibResult(fibResult) {k = k1;}
};

THREAD(sum) {
    sum_closure *sum_args = (sum_closure*)(args.get());
    SEND_ARGUMENT(sum_args->k, sum_args->arg1 + sum_args->arg2);
}

THREAD(fib) {
    fib_closure *fib_args = (fib_closure*)(args.get());
    if (fib_args->n < 2) {
        SEND_ARGUMENT(fib_args->k, fib_args->n);
    } else {
        cont x, y;
        spawn_next<sum_closure> s1 (sum_closure(fib_args->k, 0, 0));
        SN_BIND(s1, &x, arg1);
        SN_BIND(s1, &y, arg2);
        spawn<fib_closure> f1 ( fib_closure(x, fib_args->n - 1));
        spawn<fib_closure> f2 ( fib_closure(y, fib_args->n - 2));
    }
}

THREAD(main_cont) {
    main_cont_closure *main_cont_args = (main_cont_closure*)(args.get());
    printf("Fib result %d\n", main_cont_args->fibResult);
    return;
}

int main() {
    cont k;
    spawn_next<main_cont_closure> s1 (main_cont_closure(k, 0));
        // this doesn't make any sense but im too lazy ^^^^
        // to put a null continuation. 
        // doesnt matter tho since it doesnt call k.
    SN_BIND(s1, &k, fibResult);
    spawn<fib_closure> f ( fib_closure(k, 30) );
    return 0;
}