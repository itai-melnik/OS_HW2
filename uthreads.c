#include "uthreads.h"
#include "thread_queue.h"



/* ------------  low–level, arch-specific helpers  ----------------- */

//define a type of address
typedef unsigned long address_t;


# define JB_SP 6
# define JB_PC 7
# define SECOND 1000000


static address_t translate_address(address_t addr)
{
    address_t ret;
    __asm__ volatile ("xor %%fs:0x30, %0\n"
                 "rol $0x11, %0\n"
                 : "=g"(ret)
                 : "0"(addr));
    return ret;
}







/* --------------------------------------------------------------- */
/* global variables                                                */
/* --------------------------------------------------------------- */


static thread_t threads[MAX_THREAD_NUM]; //TCB table
static char __attribute__((aligned(0x10))) thread_stacks[MAX_THREAD_NUM][STACK_SIZE];


unsigned long total_quantums = 0;
int quantum_usec = 0;

int current_tid = 0; //current thread running
//static int available_ids[MAX_THREAD_NUM];

int num_threads = 0;
int_queue_t ready_q;



/* --------------------------------------------------------------- */
/* arming timer                                                    */
/* --------------------------------------------------------------- */


static void install_timer_handler(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = timer_handler;      // Specify our signal handler
    sigemptyset(&sa.sa_mask);           // No signals blocked during the handler
    sa.sa_flags = 0;                   // No special flags

    if (sigaction(SIGVTALRM, &sa, NULL) == -1) {
        fprintf(stderr, "system error: sigaction failed\n");
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
        fprintf(stderr,"system error: setitimer failed");
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
    /* Input validation --------------------------------------------------- */
    if (quantum_usecs <= 0) {
        fprintf(stderr, "thread library error: quantum_usecs must be positive\n");
        return -1;
    }

    /* Prevent multiple initialisations ----------------------------------- */
    if (num_threads != 0) {
        fprintf(stderr, "thread library error: uthread_init may be called only once\n");
        return -1;
    }

    quantum_usec = quantum_usecs;
    total_quantums = 1;
    num_threads = 1;

    init_mask();
    install_timer_handler();


    // Configure virtual timer
    arm_virtual_timer();

    //initialize main thread
    threads[0].tid = 0;
    threads[0].state = THREAD_RUNNING;
    threads[0].quantums = 1;
    threads[0].sleep_until = 0;
    threads[0].entry = NULL; // Main thread has no entry point
    current_tid = 0;


    // Save main thread's context
    address_t sp = (address_t)(thread_stacks[0] + STACK_SIZE);
    sp = (sp & ~0xF); // Align to 16 bytes
    sp -= sizeof(address_t);          //space for return address


  

    sigsetjmp(threads[0].env, 1);
    threads[0].env->__jmpbuf[JB_SP] = translate_address(sp);
    threads[0].env->__jmpbuf[JB_PC] = translate_address((address_t)NULL); // Main thread doesn't need PC


    // Clear signal mask for main thread
    sigemptyset(&threads[0].env->__saved_mask);
   

    /* Clear per‑thread tables ------------------------------------------- */
    for (int i = 1; i < MAX_THREAD_NUM; ++i) {
        memset(thread_stacks[i], 0, STACK_SIZE);
        threads[i].state = THREAD_UNUSED;
    }

    
    /* Ready‑queue initialisation ---------------------------------------- */
    queue_init(&ready_q);

     
    return 0;
}

int uthread_spawn(thread_entry_point entry_point)
{

    sigset_t old;
    mask_sigvtalrm(&old);

    if (entry_point == NULL) {
        fprintf(stderr, "thread library error: entry_point is NULL\n");
        unmask_sigvtalrm(&old);
        return -1;

    }
    if (num_threads >= MAX_THREAD_NUM) {
        fprintf(stderr, "thread library error: too many threads\n");
        unmask_sigvtalrm(&old);
        return -1;
    }
        

    int availableId = -1;
    for (int i = 1; i < MAX_THREAD_NUM; i++) {
        if (threads[i].state == THREAD_UNUSED ||
            threads[i].state == THREAD_TERMINATED) {
        availableId = i;
        break;
        }

    }

    if (availableId == -1){
        unmask_sigvtalrm(&old);
        return -1;
    }
        
    threads[availableId].tid = availableId;
    threads[availableId].state = THREAD_READY;
    threads[availableId].quantums = 0;
    threads[availableId].sleep_until = 0;
    threads[availableId].entry = entry_point;

    setup_thread(availableId, thread_stacks[availableId], entry_point);

    
    ++num_threads;

    queue_enqueue(&ready_q, availableId);

    unmask_sigvtalrm(&old);

    return availableId;
}

int uthread_terminate(int tid)
{

    if (tid < 0 || tid >= MAX_THREAD_NUM ||
        threads[tid].state == THREAD_UNUSED ||
        threads[tid].state == THREAD_TERMINATED) {
        fprintf(stderr, "thread library error: invalid tid\n");
        return -1;
    }
   


    sigset_t old;
    mask_sigvtalrm(&old);

    // if tid is 0 we need to terminate everything

    if (tid == 0) {

        for (size_t i = 1; i < MAX_THREAD_NUM; i++)
        {
            if (threads[tid].state != THREAD_UNUSED) {
                //remove from queue if it is in ready state
                if(threads[i].state == THREAD_READY) queue_delete(&ready_q, i);
            
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

        threads[tid].state = THREAD_TERMINATED;
        num_threads--;
       
        threads[tid].entry = NULL;
       
        // get the corresponding stack in the stacks table and zero it
        memset(thread_stacks[tid], 0, sizeof thread_stacks[tid]);


    unmask_sigvtalrm(&old);

    return 0;
}




int uthread_block(int tid) {

    /* input validation */
    if (tid < 0 || tid >= MAX_THREAD_NUM) {
        fprintf(stderr, "thread library error: invalid tid\n");
        return -1;
    }
    if (tid == 0) {
        fprintf(stderr, "thread library error: cannot block main thread\n");
        return -1;
    }
    if (threads[tid].state == THREAD_UNUSED ||
        threads[tid].state == THREAD_TERMINATED) {
        fprintf(stderr, "thread library error: invalid tid\n");
        return -1;
    }
    

    sigset_t old;
    mask_sigvtalrm(&old);

    //currently running thread blocking itself
    if(current_tid == tid){
        threads[current_tid].state = THREAD_BLOCKED;
        schedule_next();

        
    }
    else{
    //running thread blocking another thread
    if(threads[tid].state == THREAD_READY)queue_delete(&ready_q, tid);

    threads[tid].state = THREAD_BLOCKED;
    }
  

    unmask_sigvtalrm(&old);
    return 0;
}


int uthread_resume(int tid){

    if (tid < 0 || tid >= MAX_THREAD_NUM) {
        fprintf(stderr, "thread library error: invalid tid\n");
        return -1;
    }

    sigset_t old;
    mask_sigvtalrm(&old);


    if (threads[tid].state == THREAD_UNUSED ||
        threads[tid].state == THREAD_TERMINATED) {
        fprintf(stderr, "thread library error: invalid tid\n");
        unmask_sigvtalrm(&old);
        return -1;
    }
    


    if(threads[tid].state == THREAD_READY ||threads[tid].state == THREAD_RUNNING){
        unmask_sigvtalrm(&old);
        return 0;
    }

    /*only resume blocked threads that have sleep_until = 0 
    we cant resume a sleeping thread as it is still blocked*/

    if (threads[tid].state == THREAD_BLOCKED && threads[tid].sleep_until == 0) {
        threads[tid].state = THREAD_READY;
        queue_enqueue(&ready_q, tid);
    }

    unmask_sigvtalrm(&old);
    return 0;

}


int uthread_sleep(int num_quantums){

    sigset_t old;
    mask_sigvtalrm(&old);


    /* main thread cannot sleep and num_quantums must be positive */
    if (current_tid == 0 || num_quantums <= 0) {
        fprintf(stderr, "thread library error: invalid sleep request\n");
        unmask_sigvtalrm(&old);
        return -1;
    }
   

    //make it sleep until total quantums reaches a certain number
    threads[current_tid].sleep_until = total_quantums + num_quantums;

    threads[current_tid].state = THREAD_BLOCKED;
    // /* context-switch to the next READY thread
    // The call never returns until this thread is awakened by timer_handler()*/
   
    unmask_sigvtalrm(&old);
    schedule_next();                    /* keeps SIGVTALRM blocked */
   
    // //resume here only after sleep expires
    
    return 0;
    
}




int uthread_get_tid(){

    return current_tid;
}



int uthread_get_total_quantums(){

    return total_quantums;

}


int uthread_get_quantums(int tid){

    if (tid < 0 || tid >= MAX_THREAD_NUM ||
    threads[tid].state == THREAD_UNUSED ||
    threads[tid].state == THREAD_TERMINATED) {
    fprintf(stderr, "thread library error: invalid tid\n");
    return -1;
    }
    return threads[tid].quantums;

}


/* --------------------------------------------------------------- */
/* Helper functions                                                */
/* --------------------------------------------------------------- */



void schedule_next(void){

    /* Called either from timer_handler (preemption) or when the *
     * currently running thread blocks/terminates.    
     
     *
     * Move current RUNNING thread to READY if it can run.     */

    sigset_t old;
    mask_sigvtalrm(&old);
    int prev = current_tid;


    /* Update quantum counters*/                       
    ++total_quantums;

    if (current_tid >= 0 && threads[current_tid].state != THREAD_TERMINATED) {
        ++threads[current_tid].quantums;
    }



    //handle sleeping threads
    for (int i = 0; i < MAX_THREAD_NUM; ++i) {
        if (threads[i].state == THREAD_BLOCKED &&
            threads[i].sleep_until > 0 &&
            threads[i].sleep_until <= total_quantums) {
            /* Sleep finished => READY*/                        
            threads[i].sleep_until = 0;
            threads[i].state = THREAD_READY;
            queue_enqueue(&ready_q, i);
        }
    }

    


    if (threads[prev].state == THREAD_RUNNING && 
        threads[prev].state != THREAD_TERMINATED)
    {
        threads[prev].state = THREAD_READY;
        queue_enqueue(&ready_q, prev);
    }


    if (queue_is_empty(&ready_q)) {
        //no other READY threads just keep running prev
        unmask_sigvtalrm(&old);
        return;
    }




    /* Pick next READY thread  */                           
    int next;
    queue_dequeue(&ready_q, &next);
    threads[next].state = THREAD_RUNNING;

    current_tid = next;

    /* Context-switch
    SIGVTALRM is blocked for the switch
    new thread resumes with same mask and timer re-enabled */ 


    
    context_switch(&threads[prev], &threads[next]);
    //get here only when the prev thread is rescheduled 
    unmask_sigvtalrm(&old);  //TODO: check order of this
    
   
    
    
    return;



}


void context_switch(thread_t *current, thread_t *next){

    /* Save current state; sigsetjmp() returns 0 the first time   */
    int retval = sigsetjmp(current->env, 1);

    if (retval != 0){
        return;
    }
        siglongjmp(next->env, 1);      /* jump to the next thread          */
    
    /* When we come back here (return value != 0) we are resuming */

}



void timer_handler(int signum){
    //schedule next 
    schedule_next();
}





void setup_thread(int tid, char *stack, thread_entry_point entry_point){
    //Save a clean context     
    if (sigsetjmp(threads[tid].env, 1) == 0){
        // Set up the stack pointer to point to the top of the stack
        address_t sp = (address_t) stack + STACK_SIZE;
        // Align the stack pointer to 16 bytes
        sp &= ~0xF;
        // Reserve space for the return address and align stack
        sp -= sizeof(address_t);
        

        // Program counter is the entry function address
        address_t pc = (address_t)(entry_point);


        // Set the stack pointer and program counter in the jump buffer
        threads[tid].env->__jmpbuf[JB_SP] = translate_address(sp);
        threads[tid].env->__jmpbuf[JB_PC] = translate_address(pc);

        // The signal mask inside env must start empty  
        sigemptyset(&threads[tid].env->__saved_mask);
    }
}







