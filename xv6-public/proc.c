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

static struct proc *initproc;

int nextpid = 1;
int nexttid = 1;
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
  // struct thread *t;
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
  // p->tid = 0;
  // p->parentproc = p;
  // p->sidx = 0;
  // p->fidx = 0;
  // p->curthread = 0;
#ifdef MLFQ_SCHED
  p->tq = 0;
  p->qlvl = 0;
  p->priority = 0;
#endif
  strncpy(p->id, "root", 4);
  // t = p->threads;
  // t->state = EMBRYO;
  // t->tid = nexttid++;
  release(&ptable.lock);

  // for(int i = 0; i < NTHREAD; i++){
    // p->kstack[i] = 0;
  //   p->ustack[i] = 0;
  // }

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    // t->state = UNUSED;
    return 0;
  }
  // t->kstack = p->kstack[0];
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  // struct thread *t;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;

  // t = p->threads;
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
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  // t->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();
  struct proc *p;
  acquire(&ptable.lock);
  // p = (curproc->parentproc) ? curproc->parentproc : curproc;
  sz = curproc->sz;
  // if (curproc->fidx == curproc->sidx)
  // {
  //   // cprintf("p->sz : %d\n", p->sz);
  //   sz = curproc->sz;
  // }
  // else
  // {
  //   sz = curproc->ustack[curproc->fidx - 1];
  //   cprintf("sz : %d, fidx = %d\n", sz, curproc->fidx);
  // }
  // sz = curproc->parentproc->sz;
  
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == curproc->pid)
      p->sz = sz;
  }
  // curproc->ustack[curproc->fidx] = sz;
  // curproc->fidx = (curproc + 1 ) % NTHREAD;
  // if(curproc->fidx == curproc->sidx)
  //   curproc->sz = sz;
  // else 
  //   curproc->fidx = (curproc->fidx + 1) % (NTHREAD);
  // curproc->sp = sz - n;
  curproc->sz = sz;
  // curproc->parentproc->sz = sz;
  release(&ptable.lock);
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i = 0, pid;
  struct proc *np;
  // struct proc *p;
  struct proc *curproc = myproc();
  // cprintf("fork start\n");
  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  // cprintf("fork-1\n");
  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }

  // acquire(&ptable.lock);
  
  // cprintf("fork-2\n");
  // np->curthread = 0;
  // for(i = 0; i < NTHREAD; i++)
  //   np->ustack[i] = curproc->ustack[i];
  
  // ustack_ = np->ustack[0];
  // np->ustack[0] = np->ustack[curproc->curthread];
  // np->ustack[curproc->curthread] = ustack_;

  // acquire(&ptable.lock);
  // for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
  //   if(p->pid == curproc->pid && p->tid != curproc->tid){
  //     deallocuvm(np->pgdir, p->sp + 2*PGSIZE, p->sp);
  //   }
  // }
  // for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  // {
  //   if (p->pid == curproc->pid && p->tid != curproc->tid)
  //   {
  //     deallocuvm(np->pgdir, p->sp + PGSIZE, p->sp);
  //     np->ustack[np->sidx] = p->sp;
  //     np->sidx = np->sidx + 1;
  //   }
  // }
  // release(&ptable.lock);
  np->sz = curproc->sz;
  np->parent = curproc;
  // np->sp = curproc->sp;
  *np->tf = *curproc->tf;
  // np->sidx = curproc->sidx;
  // np->fidx = curproc->fidx;
  strncpy(np->id, curproc->id, sizeof(curproc->id));
  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  // np->threads->state = RUNNABLE;

  release(&ptable.lock);
  // cprintf("fork end\n");
  return pid;
}

int fork2(char *username)
{
  int i = 0, pid;
  struct proc *np;
  // struct proc *p;
  struct proc *curproc = myproc();
  // cprintf("fork start\n");
  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }
  // cprintf("fork-1\n");
  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }

  // acquire(&ptable.lock);

  // cprintf("fork-2\n");
  // np->curthread = 0;
  // for(i = 0; i < NTHREAD; i++)
  //   np->ustack[i] = curproc->ustack[i];

  // ustack_ = np->ustack[0];
  // np->ustack[0] = np->ustack[curproc->curthread];
  // np->ustack[curproc->curthread] = ustack_;

  // acquire(&ptable.lock);
  // for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
  //   if(p->pid == curproc->pid && p->tid != curproc->tid){
  //     deallocuvm(np->pgdir, p->sp + 2*PGSIZE, p->sp);
  //   }
  // }
  // for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  // {
  //   if (p->pid == curproc->pid && p->tid != curproc->tid)
  //   {
  //     deallocuvm(np->pgdir, p->sp + PGSIZE, p->sp);
  //     np->ustack[np->sidx] = p->sp;
  //     np->sidx = np->sidx + 1;
  //   }
  // }
  // release(&ptable.lock);
  np->sz = curproc->sz;
  np->parent = curproc;
  // np->sp = curproc->sp;
  *np->tf = *curproc->tf;
  // np->sidx = curproc->sidx;
  // np->fidx = curproc->fidx;
  strncpy(np->id, username, 16);
  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  // np->threads->state = RUNNABLE;

  release(&ptable.lock);
  // cprintf("fork end\n");
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
  // struct thread *t;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == curproc->pid && p != curproc){
      kfree(p->kstack);
      p->kstack = 0;
      p->pid = 0;
      p->parent = 0;
      p->name[0] = 0;
      p->killed = 0;
      p->state = UNUSED;
    }
  }
  release(&ptable.lock);

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

  // acquire(&ptable.lock);
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
  // for(t = curproc->threads; t < &curproc->threads[NTHREAD]; t++){
  //   if(t->state != UNUSED)
  //     t->state = ZOMBIE;
  // }
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  // struct thread *t;
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
        // for(t = p->threads; t < &p->threads[NTHREAD]; t++){
        //   if(p->kstack[t - p->threads] != 0){
        //     kfree(p->kstack[t - p->threads]);
        //     p->kstack[t - p->threads] = 0;
        //     p->ustack[t - p->threads] = 0;
        //   }
        //   t->kstack = 0;
        //   // t->ustack = 0;
        //   t->tid = 0;
        //   t->state = UNUSED;
        // }
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
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

void
priority_boost(void){
#ifdef MLFQ_SCHED
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    p->tq = 0;
    p->qlvl = 0;
  }
  release(&ptable.lock);
#endif
}

int
setpriority(int pid, int priority){
#ifdef MLFQ_SCHED
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid && p->parent->pid == myproc()->pid){
      p->priority = priority;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
#else
  return 0;
#endif
}

void
qlvl_down(void){
#ifdef MLFQ_SCHED
  acquire(&ptable.lock);
  myproc()->tq = 0;
  myproc()->qlvl++;
  release(&ptable.lock);
#endif
}

void
update_tq(void){
#ifdef MLFQ_SCHED
  acquire(&ptable.lock);
  myproc()->tq++;
  release(&ptable.lock);
#endif
}

int
getlev(void){
#ifdef MLFQ_SCHED
  return myproc()->qlvl;
#endif
  return 0;
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
  // struct thread *t;
  
#ifdef MULTILEVEL_SCHED
  for(;;){
    // Enable interrupts on this processor.
    sti();
    struct proc *proc_odd = 0;
    int evenproc_exist = 0;
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      if(p->pid % 2 == 0){
        evenproc_exist = 1;
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;
        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        continue;
      }

      if(p->pid % 2 == 1){
        if(!proc_odd){
          proc_odd = p;
        }
        else if(proc_odd->pid > p->pid){
          proc_odd = p;
        }
        continue;
      }
    }
    if(proc_odd && evenproc_exist == 0){
      c->proc = proc_odd;
      switchuvm(proc_odd);
      proc_odd->state = RUNNING;
      swtch(&(c->scheduler), proc_odd->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0; 
    }
    release(&ptable.lock);
  }

#elif MLFQ_SCHED
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    struct proc *p_select = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE || p->qlvl == MLFQ_K){
        continue;
      }

      if(!p_select){
        p_select = p;
      }

      else if(p_select->qlvl > p->qlvl){
        p_select = p;
        continue;
      }
      
      else if(p_select->qlvl == p->qlvl){
        if(p_select->tq == 0 && p_select->priority < p->priority){
          p_select = p;
          continue;
        }
        if(p_select->tq == 0 && p->tq != 0){
          p_select = p;
          continue;
        }
      }
    }
    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    if(p_select){
      c->proc = p_select;
      switchuvm(p_select);
      p_select->state = RUNNING;
      swtch(&(c->scheduler), p_select->context);
      switchkvm();
    }
 
    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;
    release(&ptable.lock);

  }

#else
  for(;;){
    // Enable interrupts on this processor.
    sti();
    
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // for(t = p->threads; t < &p->threads[NTHREAD]; t++){
      //   if(t->state == RUNNABLE){
      //     c->proc = p;
      //     p->curthread = t - p->threads;
      //     switchuvm(p);
      //     t->state = RUNNING;
      //     swtch(&(c->scheduler), t->context);
      //     switchkvm();

      //     c->proc = 0;
      //   }
      // }

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
#endif
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
  // struct thread *t;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  // t = &p->threads[p->curthread];
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

void
yield_(void)
{
#ifdef MLFQ_SCHED
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  myproc()->tq = 0;
  myproc()->qlvl = 0;
  sched();
  release(&ptable.lock);
#endif
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
  // struct thread *t;
  
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
  // t = &p->threads[p->curthread];
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

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep_(void *chan, struct spinlock *lk)
{
#ifdef MLFQ_SCHED
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  p->tq = 0;
  p->qlvl = 0;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
#endif
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  // struct thread *t;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    // if(p->state == RUNNABLE){
    //   for(t = p->threads; t < &p->threads[NTHREAD]; t++){
        if(p->state == SLEEPING && p->chan == chan)
          p->state = RUNNABLE;
      // }
    // }
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
  // struct thread *t;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      // for(t = p->threads; t < &p->threads[NTHREAD]; t++){
      //   if(t->state == SLEEPING)
      //     t->state = RUNNABLE;
      // }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
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
  // struct thread *t;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;

    // t = &p->threads[p->curthread];

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

int
thread_create(thread_t *thread, void*(*start_routine)(void*), void *arg)
{
//   struct proc *p, *curproc = myproc();
//   // struct thread *t;
//   uint sp;
//   int sz, i;
//   int ustack[2];
//   pde_t *pgdir;

//   // p = myproc();
// //   for (t = p->threads; t < &p->threads[NTHREAD]; t++)
// //     if (t->state == UNUSED)
// //       goto found;

// //   release(&ptable.lock);
// //   return -1;

// // found:
// //   p->curthread = t - p->threads;
// //   t->tid = nexttid++;

//   // if(pgdir == 0){
//   //   p->state = UNUSED;
//   //   return -1;
//   // }

//   // Allocate kernel stack.
//   // if (p->kstack[p->curthread] == 0 && (p->kstack[p->curthread] = kalloc()) == 0)
//   // {
//   //   t->tid = 0;
//   //   t->state = UNUSED;
//   //   release(&ptable.lock);
//   //   return -1;
//   // }
//   // t->kstack = p->kstack[p->curthread];
//   // sp = t->kstack + KSTACKSIZE;

//   // Leave room for trap frame.
//   // sp -= sizeof *t->tf;
//   // t->tf = (struct trapframe *)sp;
//   // *t->tf = *p->threads[p->curthread].tf;

//   // Set up new context to start executing at forkret,
//   // which returns to trapret.
//   // sp -= 4;
//   // *(uint *)sp = (uint)trapret;

//   // sp -= sizeof *t->context;
//   // t->context = (struct context *)sp;
//   // memset(t->context, 0, sizeof *t->context);
//   // t->context->eip = (uint)forkret;
//   // cprintf("create start\n");
//   if((p = allocproc()) == 0)
//     return -1;

//   if((pgdir = curproc->pgdir) == 0){
//     // kfree(p->kstack);
//     // p->kstack = 0;
//     p->state = UNUSED;
//     return -1;
//   }

//   p->sz = curproc->sz;
//   p->sidx = curproc->sidx;
//   p->fidx = curproc->fidx;
//   p->parent = curproc->parent;
//   *p->tf = *curproc->tf;

//   p->tf->eax = 0;
//   // cprintf("create-1\n");
//   for(i = 0; i < NOFILE; i++)
//     if(curproc->ofile[i])
//       p->ofile[i] = filedup(curproc->ofile[i]);
//   p->cwd = idup(curproc->cwd);

//   safestrcpy(p->name, curproc->name, sizeof(curproc->name));
//   acquire(&ptable.lock);
//   p->pid = curproc->pid;
//   p->parentproc = curproc;
//   p->tid = nexttid++;
//   // release(&ptable.lock);
//   release(&ptable.lock);
//   if(p->fidx == p->sidx){
//     // cprintf("p->sz : %d\n", p->sz);
//     sz = p->sz;
//   }
//   else{
//     sz = curproc->ustack[curproc->fidx];
//     // cprintf("sz : %d, fidx = %d\n", sz, curproc->fidx);
//   }
//   // else{
//   //   sz = PGROUNDUP(p->sz);
//   //   if((sz = allocuvm(p->pgdir, sz, sz + PGSIZE)) == 0){
//   //     t->kstack = 0;
//   //     t->tid = 0;
//   //     t->state = UNUSED;
//   //     release(&ptable.lock);
//   //     return -1;
//   //   }
//   //   p->sz = sz;
//   //   p->ustack[p->curthread] = sz;
//   //   // t->ustack = sz;
//   // }
//   // int oldsz = sz;
//   if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0){
//     p->kstack = 0;
//     p->tid = 0;
//     p->state = UNUSED;
//     release(&ptable.lock);
  //   return -1;
  // }

  // clearpteu(pgdir, (char*)(sz - 2*PGSIZE));

  // sp = sz;
  // sp -= 4;

  // ustack[0] = 0xffffffff;

  // sp -= 4;

  // ustack[1] = (uint)arg;
  // if(copyout(pgdir, sp, ustack, 8) < 0){
  //   p->kstack = 0;
  //   p->tid = 0;
  //   p->state = UNUSED;
  //   release(&ptable.lock);
  //   return -1;
  // }

  // p->sp = sz - 2*PGSIZE;
  // if(curproc->fidx == curproc->sidx)
  //   curproc->sz = sz;
  // else
  //   curproc->fidx = (curproc->fidx + 1);
  // p->sz = sz;
  // p->pgdir = pgdir;
  // curproc->sz = sz;

  // p->tf->esp = (uint)sp;
  // p->tf->eip = (uint)start_routine;
  // acquire(&ptable.lock);
  // *thread = p->tid;
  // // p->tid = *thread;

  // p->state = RUNNABLE;
  // for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  // {
  //   if (p->pid == curproc->pid)
  //     p->sz = sz;
  // }
  // release(&ptable.lock);
  // // cprintf("create end\n");
  return 0;
}

// void
// thread_terminate(void)
// {
//   struct proc *p = myproc();
//   // struct thread *t;
//   cprintf("thread_terminate start\n");

//   // acquire(&ptable.lock);
//   // t = &p->threads[p->curthread];
//   p->state = ZOMBIE;
//   cprintf("wakeup pid : %d, tid : %d\n", p->parentproc->pid, p->parentproc->tid);
//   wakeup1((void *)p->parentproc);
//   cprintf("thread_terminate end\n");
//   sched();
//   panic("panic: thread_terminate");
// }

void
thread_exit(void *retval)
{
  // struct proc *p = myproc();
  // int fd;
  // // it(p->tid == -1)
  // //   return;

  // // acquire(&ptable.lock);
  // p->retval = retval;

  // for(fd = 0; fd < NOFILE; fd++){
  //   if(p->ofile[fd]){
  //     fileclose(p->ofile[fd]);
  //     p->ofile[fd] = 0;
  //   }
  // }

  // begin_op();
  // iput(p->cwd);
  // end_op();
  // p->cwd = 0;
  // acquire(&ptable.lock);
  // wakeup1(p->parentproc);
  // p->state = ZOMBIE;

  // sched();
  // panic("zombie wakeup");
}

int
thread_join(thread_t thread, void **retval)
{
//   struct proc *p;
//   uint sp;
//   // struct thread *t;

//   acquire(&ptable.lock);
//   for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//     if(p->parentproc != myproc())
//       continue;
//     // if(p->state == RUNNABLE){
//       // for(t = p->threads; t < &p->threads[NTHREAD]; t++){
//         if(p->tid == thread)
//           goto found;
//       // }
//     // }
//   }

//   release(&ptable.lock);
//   return -1;

// found:
//   for(;;){
//     if(p->state == ZOMBIE)
//       break;
//     sleep((void*)myproc(), &ptable.lock);
//   }

//   *retval = p->retval;
//   kfree(p->kstack);
//   p->kstack = 0;
//   p->state = UNUSED;
//   p->name[0] = 0;
//   p->pid = 0;
//   p->parent = 0;
//   p->killed = 0;
//   p->tid = 0;
//   p->retval = 0;
//   p->parentproc = 0;
//   sp = p->sp;
//   p->sp = 0;
//   deallocuvm(p->pgdir, sp + 2*PGSIZE, sp);
//   myproc()->ustack[myproc()->sidx] = sp;
//   myproc()->sidx = (myproc()->sidx + 1);

//   release(&ptable.lock);
  return 0;
}

void
thread_terminate(int pid, int tid)
{
  // struct proc *p;
  // acquire(&ptable.lock);
  // for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  // {
  //   if(pid == 0)
  //     continue;
  //   if(p->pid == pid && p->tid != tid)
  //   {
  //     kfree(p->kstack);
  //     p->kstack = 0;
  //     p->pid = 0;
  //     p->parent = 0;
  //     p->name[0] = 0;
  //     p->killed = 0;
  //     p->state = UNUSED;
  //   }
  // }
  // release(&ptable.lock);
}