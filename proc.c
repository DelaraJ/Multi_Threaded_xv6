#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"


struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

typedef struct Node {
    int vertex;
    enum nodetype type;
    struct Node* next;
} Node;
typedef struct Graph{
  struct spinlock lock;
  Node* adjList[MAXTHREAD+NRESOURCE];
  int visited[MAXTHREAD+NRESOURCE];
  int recStack[MAXTHREAD+NRESOURCE];
} Graph;
//################ADD Your Implementation Here######################

  Graph gr;
  void init_graph(Node* graph_page){
    initlock(&gr.lock, "grLock");
    for (int i = 0; i < MAXTHREAD+NRESOURCE; i++)
    {
      if(i < NRESOURCE) {
        gr.adjList[i] = &graph_page[i];
        gr.adjList[i]->next = 0;
        gr.adjList[i]->type = RESOURCE;
        gr.adjList[i]->vertex = i;
      }
      else {
        gr.adjList[i] = &graph_page[i];
        gr.adjList[i]->next = 0;
        gr.adjList[i]->type = PROCESS;
        gr.adjList[i]->vertex = -1;
        
      }
      gr.visited[i] = 0;
      gr.recStack[i] = 0;
    }
    
  }


//Graph creation and functions
//##################################################################


struct spinlock resource_locks[NRESOURCE];
static struct proc *initproc;
Resource* metadata_half;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }

  sp = p->kstack + KSTACKSIZE;
  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

//This is for test

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
  p->Is_Thread=0;
  p->Thread_Num=0;
  p->tstack=0;
  p->tid=0;
  p->resource = metadata_half;
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
//################ADD Your Implementation Here######################
  char* page_start = kalloc();
  if(!page_start){
    panic("userinit: out of memory?");
  }
  cprintf("\n\nAllocated Page for MultiThreading: %p\n\n", page_start);
  metadata_half = (Resource*)page_start;
  char* data_half = page_start + 1024 * 2;
  for (int i = 0; i < 128; i++)
  {
    metadata_half[i].acquired = 0;
    metadata_half[i].resourceid = i;
    metadata_half[i].startaddr = data_half + 16 * i;
  }
  
  // int* arr = (int*)malloc(10 * sizeof(int));
  // arr[0] = 0;

      //Resource page handling and creation



//##################################################################
//initialize resource locks
for (int i = 0; i < NRESOURCE; i++) {
  initlock(&resource_locks[i], "resource_lock");
}

//###############################################
  //graph page
  Node* graph_page = (Node*)kalloc();
  init_graph(graph_page);
  // for (int i = 0; i < NRESOURCE + MAXTHREAD; i++)
  // {
  //   cprintf("\ngraph: %p\n", gr.adjList[i]);
  // }
  
//###############################################
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
  //np->tid=-1;
  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;
  if(curproc->tid == 0 && curproc->Thread_Num!=0) {
    panic("Parent cannot exit before its children");
  }
  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        // if(p->Is_Thread){
        //   gr.adjList[p->tid]->next = 0;
        //   gr.adjList[p->tid]->vertex = 0;
        //   cprintf("Here!\n");
        // }
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}
int clone(void (*worker)(void*,void*),void* arg1,void* arg2,void* stack)
{
  //int i, pid;
  struct proc *New_Thread;
  struct proc *curproc = myproc();
  uint sp,HandCrafted_Stack[3];
  // Allocate process.
  if((New_Thread = allocproc()) == 0){
    return -1;
  }
  if(curproc->tid!=0){
      kfree(New_Thread->kstack);
      New_Thread->kstack = 0;
      New_Thread->state = UNUSED;
      cprintf("Clone called by a thread\n");
      return -1;
  }
  //The new thread parent would be curproc
  New_Thread->pid=curproc->pid;
  New_Thread->sz=curproc->sz;

  //The tid of the thread will be determined by Number of current threads 
  //of a process
  curproc->Thread_Num++;
  //HERE WILL BE CHANGE TO GET TID = FIRST AVALAIBLE NUMBER < MAXTHREADCOUNT
  for(int i = NRESOURCE; i < NRESOURCE + MAXTHREAD; i++) {
    // SURELY A NUMBER WILL BE FIND HERE
    if(gr.adjList[i]->vertex == -1) {
      New_Thread->tid = i - NRESOURCE + 1; // 1..MAXTHREAD
      acquire(&gr.lock);
      gr.adjList[i]->vertex = i;
      release(&gr.lock);
      break;
    }
  }
  // New_Thread->tid=curproc->Thread_Num;
  New_Thread->Is_Thread=1;
  //The parent of thread will be the process calling clone
  New_Thread->parent=curproc;

  //Sharing the same virtual address space
  New_Thread->pgdir=curproc->pgdir;
  if(!stack){
      kfree(New_Thread->kstack);
      New_Thread->kstack = 0;
      New_Thread->state = UNUSED;
      curproc->Thread_Num--;
      New_Thread->tid=0;
      New_Thread->Is_Thread=0;
      cprintf("Child process wasn't allocated a stack\n");    
  }
  //Assuming that child_stack has been allocated by malloc
  New_Thread->tstack=(char*)stack;
  //Thread has the same trapframe as its parent
  *New_Thread->tf=*curproc->tf;

  HandCrafted_Stack[0]=(uint)0xfffeefff;
  HandCrafted_Stack[1]=(uint)arg1;
  HandCrafted_Stack[2]=(uint)arg2;
  
  sp=(uint)New_Thread->tstack;
  sp-=3*4;
  if(copyout(New_Thread->pgdir, sp,HandCrafted_Stack, 3 * sizeof(uint)) == -1){
      kfree(New_Thread->kstack);
      New_Thread->kstack = 0;
      New_Thread->state = UNUSED;
      curproc->Thread_Num--;
      New_Thread->tid=0;
      New_Thread->Is_Thread=0;      
      return -1;
  }
  New_Thread->tf->esp=sp;
  New_Thread->tf->eip=(uint)worker;
  //Duplicate all the file descriptors for the new thread
  for(uint i = 0; i < NOFILE; i++){
    if(curproc->ofile[i])
      New_Thread->ofile[i] = filedup(curproc->ofile[i]);
  }
  New_Thread->cwd = idup(curproc->cwd);
  safestrcpy(New_Thread->name, curproc->name, sizeof(curproc->name));
  acquire(&ptable.lock);
  New_Thread->state=RUNNABLE;
  release(&ptable.lock);
  //cprintf("process running Clone has  %d threads\n",curproc->Thread_Num);  
  return New_Thread->tid;
}
int join(int Thread_id)
{
  struct proc  *p,*curproc=myproc();
  int Join_Thread_Exit=0,jtid;
  if(Thread_id==0)
     return -1;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->tid == Thread_id && p->parent == curproc) {
      Join_Thread_Exit=1; 
      break;
    }
  }
  if(!Join_Thread_Exit || curproc->killed){
    //cprintf("Herere");
    return -1;
  }  
  acquire(&ptable.lock);
  for(;;){
    // thread is killed by some other thread in group
    //cprintf("I am waiting\n");
    if(curproc->killed){
      release(&ptable.lock);
      return -1;
    }
    if(p->state == ZOMBIE){
      // Found the thread 
      curproc->Thread_Num--;
      jtid = p->tid;
      kfree(p->kstack);
      //set the Node back to initial
      if(p->Is_Thread){
          acquire(&gr.lock);
          // cprintf("%d\n", p->tid);
          Node* curr_thread = gr.adjList[p->tid - 1 + NRESOURCE];
          for (int i = 0; i < NRESOURCE; i++)
          {
            Node* n = gr.adjList[i];
            if(n->next == curr_thread){
              n->next = 0;
              releaseresource(n->vertex);
              
            }
            // cprintf("On Res : %d\n", n->vertex);
          }
          
          // for (Node* n = gr.adjList[0]; n->vertex < NRESOURCE; n += sizeof(Node))
          // {
          //   cprintf("On Res : %d\n", n->vertex);
          //   if(n->next == curr_thread){
          //     n->next = 0;
          //     releaseresource(n->vertex);
          //   }
          // }
          curr_thread->next = 0;
          
          
          curr_thread->vertex = -1;
          release(&gr.lock);
          // cprintf("Is_Thread\n");
      }
      p->kstack = 0;
      p->pgdir = 0;
      p->pid = 0;
      p->tid = 0;
      p->tstack = 0;
      p->parent = 0;
      p->name[0] = 0;
      p->killed = 0;
      p->state = UNUSED;
      release(&ptable.lock);
      //cprintf("Parent has  %d threads\n",curproc->Thread_Num);
      return jtid;
    } 

    sleep(curproc, &ptable.lock);  
  }     
  //curproc->Thread_Num--;
  return 0;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int requestresource(int Resource_ID)
{
  struct proc *curproc = myproc();
  int stat = 0;
  if(Resource_ID < 0){
    return -1;
  }
  int thread_index_in_graph = curproc->tid - 1 + NRESOURCE;
  if(curproc->resource[Resource_ID].acquired != 0){
    // ADD AN EDGE TO BE WAITING FOR THAT RESOURCE
    acquire(&gr.lock);
    gr.adjList[thread_index_in_graph]->next = gr.adjList[Resource_ID];
    release(&gr.lock);
    stat = -1;
  }else{
    curproc->resource[Resource_ID].acquired = curproc->tid;
    acquire(&gr.lock);
    gr.adjList[Resource_ID]->next = gr.adjList[thread_index_in_graph];
    release(&gr.lock);
    // cprintf("requested {%d}\n", Resource_ID);
  }
  // //PRINT GRAPH
  // cprintf("printing graph...\n");
  // for (int i = 0; i < NRESOURCE + MAXTHREAD; i++)
  // {
  //   int tmp = 0;
  //   Node* n = gr.adjList[i];
  //   while(n->next != 0 && tmp < NRESOURCE + MAXTHREAD){
  //     cprintf("%d -> %d\n", n->vertex, n->next->vertex);
  //     n = n->next;
  //     tmp = tmp +1;
  //   }
  //   if(tmp == NRESOURCE + MAXTHREAD) {
  //     cprintf("CYCLE\n");
      
  //   }
  //   else {
  //     cprintf("%d -> NULL\n", n->vertex);
  //   }
  //     cprintf("---------------\n");

  // }
  
  //find cycle
  for (int i = 0; i < NRESOURCE + MAXTHREAD; i++)
  {
    Node* n = gr.adjList[i];
    while(n->next != 0){
      // cprintf("Here!\n");
      if(gr.visited[n->vertex] == 1){
        cprintf("----DEADLOCK!----\n");
        return -1;
        // panic("----DEADLOCK!----");
      }
      gr.visited[n->vertex] = 1;
      n = n->next;
    }
    memset(gr.visited, 0, sizeof(gr.visited));
    // for (int i = 0; i < NRESOURCE + MAXTHREAD; i++)
    // {
    //   gr.visited[i] = 0;
    // }
    
  }
  return stat;
}
int releaseresource(int Resource_ID)
{
  struct proc *curproc = myproc();
  
  if(Resource_ID < 0){
    return -1;
  }
  if(curproc->resource[Resource_ID].acquired == 0){
    return -1;
  }
  
  // for (int i = 0; i < NRESOURCE; i++)
  // {
  //   cprintf("before : res %d : %d\n", i, curproc->resource[i].acquired);
  //   // cprintf("-------------\n");
  // }
  
  curproc->resource[Resource_ID].acquired = 0;
  
  // if(curproc->Is_Thread){
  //   cprintf("Thread %d released : %d\n", curproc->tid, Resource_ID);
  // }
  // else{
  //   cprintf("Not a Thread!\n");
  // }
  // for (int i = 0; i < NRESOURCE; i++)
  // {
  //   cprintf("after : res %d : %d\n", i, curproc->resource[i].acquired);
  // }
  
  
  return 0;
}

int writeresource(int Resource_ID, void *buffer, int offset, int size) {
    if (Resource_ID < 0 || Resource_ID >= NRESOURCE) {
        return -1; 
    }
    Resource *res = &metadata_half[Resource_ID];

    acquire(&resource_locks[Resource_ID]);

    if (offset < 0 || size < 0 || offset + size > 16) {
      release(&resource_locks[Resource_ID]);
      return -1; 
    }
    if (res->acquired != myproc()->tid) {
      release(&resource_locks[Resource_ID]);
      return -1; 
    }

    memmove((char *)res->startaddr + offset, buffer, size);

    release(&resource_locks[Resource_ID]);

    return 0; 
}
int readresource(int Resource_ID, int offset, int size, void *buffer) {
    if (Resource_ID < 0 || Resource_ID >= NRESOURCE) {
        return -1; 
    }
    Resource *res = &metadata_half[Resource_ID];

    acquire(&resource_locks[Resource_ID]);

    if (offset < 0 || size < 0 || offset + size > 16) {
      release(&resource_locks[Resource_ID]);
      return -1; 
    }

    memmove(buffer, (char *)res->startaddr + offset, size);
    
    release(&resource_locks[Resource_ID]);
    
    return 0; 
}