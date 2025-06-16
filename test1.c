#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <stdatomic.h>
#include <stdint.h>
#include "uthreads.h"

atomic_int done;

void userland_sleep(int usecs)
{
    struct timespec start, current;
    clock_gettime(CLOCK_MONOTONIC, &start);

    uint64_t nanos = usecs * 1e3;

    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &current);
        uint64_t elapsed = (current.tv_sec - start.tv_sec) * 1e9 +
                           (current.tv_nsec - start.tv_nsec);
        if (elapsed >= nanos)
            break;
    }
}

void f()
{
    int tid = uthread_get_tid();
    for(int i = 0; i < 100; i++)
    {
        printf("Thread %d: %d\n", tid, i);
        int x = 0;
        userland_sleep(200);
    }
    atomic_fetch_add(&done, 1);
    uthread_terminate(tid);
}

int main(void)
{
    atomic_store(&done, 0);
    uthread_init(1000);
    uthread_spawn(f);
    uthread_spawn(f);
    while(atomic_load(&done) < 2)
    {};
    printf("Done!\n");
    uthread_terminate(0);
    return 0;
}
