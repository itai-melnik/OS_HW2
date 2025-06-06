#include "uthreads.h"
#include "thread_queue.h"


/* ------------  low–level, arch-specific helpers  ----------------- */

//define a type of address
typedef unsigned long address_t;


# define JB_SP 6
# define JB_PC 7


static address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor %%fs:0x30, %0\n"
                 "rol $0x11, %0\n"
                 : "=g"(ret)
                 : "0"(addr));
    return ret;
}









/* --------------------------------------------------------------- */
/* global variables                                                */
/* --------------------------------------------------------------- */

thread_t threads[MAX_THREAD_NUM]; //TCB table
static char thread_stacks[MAX_THREAD_NUM][STACK_SIZE]; // 2d array of stacks


unsigned long total_quantums = 1;
int quantum_usec;

int current_tid; //current thread running
static int available_ids[MAX_THREAD_NUM] = {0};

int num_threads = 0;
int_queue_t ready_q;



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
    timer.it_value.tv_sec  =  quantum_usec / SECOND; // Initial expiration in seconds
    timer.it_value.tv_usec =  quantum_usec % SECOND; //Initial expiration in microseconds

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

    memset(threads, 0, sizeof threads);

    if (availableId == -1)
        return -1;



    memset(threads, 0, sizeof threads);
    memset(thread_stacks, 0, sizeof thread_stacks);
    // initialize q
    queue_init(&ready_q);

    num_threads = 0;
    total_quantums = 1;

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
        
    setup_thread(availableId, thread_stacks[availableId], entry_point);

    threads[availableId].tid = availableId;
    threads[availableId].state = THREAD_READY;
    threads[availableId].quantums = 0;
    threads[availableId].sleep_until = 0;
    threads[availableId].entry = entry_point;
    num_threads++;


    queue_enqueue(&ready_q, availableId);

    unmask_sigvtalrm(&old);

    return 0;
}

int uthread_terminate(int tid)
{

    //input validation
    if (tid < 0 || tid >= MAX_THREAD_NUM || !available_ids[tid])
    return -1;
   


    sigset_t old;
    mask_sigvtalrm(&old);

    // if tid is 0 we need to terminate everything

    if (tid == 0) {

        for (size_t i = 1; i < MAX_THREAD_NUM; i++)
        {
            if (available_ids[i]) {
                //remove from queue if it is in ready state
                if(threads[i].state == THREAD_READY) queue_delete(&ready_q, i);
            
                available_ids[i] = 0;
                threads[i].state = THREAD_TERMINATED;
                num_threads--;
            
                threads[i].entry = NULL;
            
                // get the corresponding stack in the stacks table and zero it
                memset(thread_stacks[i], 0, sizeof thread_stacks[i]);
            }
            
        }


        // Clear the ready queue
        while (!queue_is_empty(&ready_q)) {
            int tmp;
            queue_dequeue(&ready_q, &tmp);
        }

        
        unmask_sigvtalrm(&old);
        exit(0); 
    } 


    //the running thread terminates itself
    if(tid == current_tid){

        available_ids[tid] = 0;
        threads[tid].state = THREAD_TERMINATED;
        num_threads--;
        unmask_sigvtalrm(&old);
        
        //if no more running threads
        if (num_threads == 0) { exit(0); }
        
        schedule_next();   //never returns

    }



    
    /*if thread getting terminated is not the calling thread or the main thread
    it is a thread in READY / BLOCKED / SLEEPING state */

    //remove from queue if it is in ready state
    if(threads[tid].state == THREAD_READY) queue_delete(&ready_q, tid);

        available_ids[tid] = 0;
        threads[tid].state = THREAD_TERMINATED;
        num_threads--;
       
        threads[tid].entry = NULL;
       
        // get the corresponding stack in the stacks table and zero it
        memset(thread_stacks[tid], 0, sizeof thread_stacks[tid]);


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

    if (threads[tid].state == THREAD_BLOCKED && threads[tid].sleep_until == 0) {
        threads[tid].state = THREAD_READY;
        queue_enqueue(&ready_q, tid);
    }

    unmask_sigvtalrm(&old);
    return 0;

}


int uthread_sleep(int num_quantums){

    int tid = uthread_get_tid();

    //main thread cant sleep
    if(tid == 0){
        return -1;
    }

    queue_delete(&ready_q, tid);

    //block the thread
    threads[tid].state = THREAD_BLOCKED;

    //make it sleep until total quantums reaches a certain number
    threads[tid].sleep_until = total_quantums + num_quantums;

    return 0;
    
}




int uthread_get_tid(){

    if (threads[current_tid].tid == current_tid) return current_tid;

    
    for (size_t i = 0; i < MAX_THREAD_NUM; i++)
    {
        if(threads[i].state == THREAD_RUNNING){
            return i;
        }
    }

    return 0;
    
}



int uthread_get_total_quantums(){

    return total_quantums;

}


int uthread_get_quantums(int tid){

    return threads[tid].quantums;
    


}


/* --------------------------------------------------------------- */
/* Helper functions                                                */
/* --------------------------------------------------------------- */



void schedule_next(void){

    /* Called either from timer_handler (preemption) or when the *
     * currently running thread blocks/terminates.    
     
     *
     * 1) Move current RUNNING thread to READY if it can run.     */

    int prev = current_tid;
    if (threads[prev].state== THREAD_RUNNING)
    {
        threads[prev].state = THREAD_READY;
        queue_enqueue(&ready_q, prev);
    }


    /* 2) Pick next READY thread                                  */
    int next;
    queue_dequeue(&ready_q, &next);
    threads[next].state = THREAD_RUNNING;


    /* 3) Context-switch                                          */
    context_switch(&threads[prev], &threads[next]);



}


void context_switch(thread_t *current, thread_t *next){

    /* Save current state; sigsetjmp() returns 0 the first time   */
    if (sigsetjmp(current->env, 1) == 0)
    {
        current_tid = next->tid;          /* globally record the new RUNNING */
        siglongjmp(next->env, 1);      /* jump to the next thread          */
    }
    /* When we come back here (return value != 0) we are resuming */
    return;


}



void timer_handler(int signum){

     /* Update quantum counters                                     */
    ++total_quantums;
    ++threads[current_tid].quantums;


    //handle sleeping threads

    for (int i = 0; i < MAX_THREAD_NUM; ++i)
    {
        if (threads[i].state == THREAD_BLOCKED &&
            threads[i].sleep_until != 0 &&
            threads[i].sleep_until <= total_quantums)
        {
            /* Sleep finished -> READY                              */
            threads[i].sleep_until = 0;
            threads[i].state = THREAD_READY;
            queue_enqueue(&ready_q, i);
        }
    }


    //scheduling decision

    //either move to end of READY queue

    //schedule next 
    schedule_next();
    return;



}



void setup_thread(int tid, char *stack, thread_entry_point entry_point){


    //Save a clean context                                 
    sigsetjmp(threads[tid].env, 1);

    //Wire the new stack & PC into that context 
    address_t sp = (address_t)stack + STACK_SIZE - sizeof(address_t);
    address_t pc = (address_t)entry_point;

    threads[tid].env->__jmpbuf[JB_SP] = translate_address(sp);
    threads[tid].env->__jmpbuf[JB_PC] = translate_address(pc);

    //The signal mask inside env must start empty  
    sigemptyset(&threads[tid].env->__saved_mask);

}







