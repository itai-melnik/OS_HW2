#include "uthreads.h"
#include "thread_queue.h"





/* --------------------------------------------------------------- */
/* global variables                                                */
/* --------------------------------------------------------------- */

thread_t threads[MAX_THREAD_NUM];
unsigned long total_quantums = 1;
int current_tid; //current thread running
int quantum_usec;
int available_ids[MAX_THREAD_NUM];
int num_threads = 0;
int_queue_t ready_q;
int thread_stacks[MAX_THREAD_NUM][STACK_SIZE]; // 2d array of stacks


/* --------------------------------------------------------------- */
/* arming timer                                                    */
/* --------------------------------------------------------------- */


static void install_timer_handler(void)
{
    struct sigaction sa;
    sa.sa_handler = timer_handler;      // Specify our signal handler
    sigemptyset(&sa.sa_mask);           // No signals blocked during the handler
    sa.sa_flags = 0;                   // No special flags

    if (sigaction(SIGVTALRM, &sa, NULL) == -1) {
        perror("system error: sigaction failed");
        exit(1);
    }
}


static void arm_virtual_timer(void)
{
    struct itimerval timer;

    /* first expiry */
    timer.it_value.tv_sec  =  quantum_usec / 1000000; // Initial expiration in seconds
    timer.it_value.tv_usec =  quantum_usec % 1000000; //Initial expiration in microseconds

    /* subsequent expiries (periodic) */
    timer.it_interval = timer.it_value;       /* same length as first quantum */

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL) == -1) {
        perror("system error: setitimer failed");
        exit(1);
    }
}




/* --------------------------------------------------------------- */
/* masking helpers                                                 */
/* --------------------------------------------------------------- */
static sigset_t vt_set;

static void init_mask(void)
{
    if (sigemptyset(&vt_set) == -1 || sigaddset(&vt_set, SIGVTALRM) == -1) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }
}

/* Block SIGVTALRM and remember the previous mask in *old. */
static inline void mask_sigvtalrm(sigset_t *old)
{
    if (sigprocmask(SIG_BLOCK, &vt_set, old) == -1) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }
}

/* Restore the mask saved in *old. */
static inline void unmask_sigvtalrm(const sigset_t *old)
{
    if (sigprocmask(SIG_SETMASK, old, NULL) == -1) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }
}






int uthread_init(int quantum_usecs)
{

    // make everything available
    int availableId = -1;
    for (int i = 0; i < MAX_THREAD_NUM; i++)
    {
        if (!available_ids[i])
        {
            availableId = i;
            available_ids[i] = 1;
            break;
        }
    }

    if (availableId == -1)
        return -1;

    // initialize q
    queue_init(&ready_q);

    threads[availableId].tid = availableId;
    threads[availableId].state = THREAD_RUNNING;
    threads[availableId].quantums = 1;
    threads[availableId].sleep_until = 0;
    current_tid = availableId;
    num_threads++;

    //initialize the mask
    init_mask();

    sigset_t old; //old set of signals to be blocked, unblocked or waited for

    mask_sigvtalrm(&old);

    install_timer_handler();         //block SIGVTALRM
    quantum_usec = quantum_usecs;    // stored globally   
    arm_virtual_timer();               

    unmask_sigvtalrm(&old); 

    return 0;
}

int uthread_spawn(thread_entry_point entry_point)
{


    //use thread_setup

    if (!entry_point || MAX_THREAD_NUM <= num_threads)
    {
        return -1;
    }


    sigset_t old;

    mask_sigvtalrm(&old);

    int availableId = -1;
    for (int i = 0; i < MAX_THREAD_NUM; i++)
    {
        if (!available_ids[i])
        {
            availableId = i;
            available_ids[i] = 1;
            break;
        }
    }

    if (availableId == -1){
        unmask_sigvtalrm(&old);
        return -1;
    }
        

    threads[availableId].tid = availableId;
    threads[availableId].state = THREAD_READY;
    threads[availableId].quantums = 0; //??? think its 0 because it is in ready not running
    threads[availableId].sleep_until = 0;
    threads[availableId].entry = entry_point;
    num_threads++;

    // get the corresponding stack in the stacks table
    int thread_stacks[MAX_THREAD_NUM] = 0;

    tq_enqueue(&ready_q, &threads[availableId]); // check if by reference

    unmask_sigvtalrm(&old);

    return 0;
}

int uthread_terminate(int tid)
{

    // tid does not exist
    if (!available_ids[tid])
    {
        return -1;
    }


    sigset_t old;
    mask_sigvtalrm(&old);


    if (tid == 0) {
        // if tid is 0 we need to terminate everything

        int ctid = 0; // the tid we are going to retrieve from the q
        while (!queue_is_empty(&ready_q))
        {
            queue_dequeue(&ready_q, &ctid);
            available_ids[ctid] = 0;
            threads[ctid].tid = 0;
            threads[ctid].state = THREAD_TERMINATED;
            threads[ctid].quantums = 0; //??? think its 0 because it is in ready not running
            threads[ctid].sleep_until = 0;
            threads[ctid].entry = NULL;
            num_threads--;
            memset(thread_stacks[ctid], 0, sizeof thread_stacks[ctid]);
        }
    } 
    else {

        available_ids[tid] = 0;
        threads[tid].tid = 0;
        threads[tid].state = THREAD_TERMINATED;
        threads[tid].quantums = 0; //??? think its 0 because it is in ready not running
        threads[tid].sleep_until = 0;
        threads[tid].entry = NULL;
        num_threads--;

        // get the corresponding stack in the stacks table
        memset(thread_stacks[tid], 0, sizeof thread_stacks[tid]);
   
    }

    // delete the process from the queue
    queue_delete(&ready_q, tid);

    unmask_sigvtalrm(&old);

    return 0;
}
int uthread_block(int tid) {
    if (!available_ids[tid]  || tid==0)
    {
        return -1;
    }

    sigset_t old;
    mask_sigvtalrm(&old);
    if(threads[tid].state == THREAD_READY){
        queue_delete(&ready_q, tid);
    }
    threads[tid].state = THREAD_BLOCKED;

    unmask_sigvtalrm(&old);

    return 0;
}


int uthread_resume(int tid){
    if (!available_ids[tid])
    {
        return -1;
    }


    sigset_t old;
    mask_sigvtalrm(&old);


    ///TODO: fix this
    if(threads[tid].state == THREAD_READY ||threads[tid].state == THREAD_RUNNING){
        unmask_sigvtalrm(&old);
        return 0;
    }
    threads[tid].state = THREAD_READY;

    unmask_sigvtalrm(&old);
    return 0;

}


int uthread_sleep(int num_quantums){

    int tid = uthread_get_tid();

    //main thread cant sleep
    if(tid == 0){
        return -1;
    }


    threads[tid].sleep_until += num_quantums; //check logic here 
    

    //while  num_quantums > 0 keep thread blocked if 0 add to ready queue
    

}




int uthread_get_tid(){

    for (size_t i = 0; i < MAX_THREAD_NUM; i++)
    {
        if(threads[i].state == THREAD_RUNNING){
            return i;
        }
    }

    // retrun current_tid

    return 0;
    
}



int uthread_get_total_quantums(){

    return total_quantums;

}


int uthread_get_quantums(int tid){


    ///




}


/* --------------------------------------------------------------- */
/* Helper functions                                                */
/* --------------------------------------------------------------- */



void schedule_next(void){
    int tid;
    queue_dequeue(&ready_q, &tid);



}


void context_switch(thread_t *current, thread_t *next){



}



void timer_handler(int signum){

    total_quantums += 1; 

    //scheduling decision

    //either move to end of READY queue

    //schedule next 



}



void setup_thread(int tid, char *stack, thread_entry_point entry_point){



}







