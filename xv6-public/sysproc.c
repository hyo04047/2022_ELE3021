#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_fork2(void)
{
  char *username;
  if(argstr(0, &username) < 0) return -1;
  return fork2(username);
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
  #ifdef MLFQ_SCHED
    sleep_(&ticks, &tickslock);
  #else
    sleep(&ticks, &tickslock);
  #endif
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_getppid(void)
{
  return myproc()->parent->pid;
}

int
sys_test(void){
  for(;;)
    cprintf("ticks = %d, pid = %d, name = %s\n", ticks, myproc()->pid, myproc()->name);
  return 0;
}

void
sys_yield(void){
#ifdef MLFQ_SCHED
  yield_();
#else
  yield();
#endif
}

int
sys_getlev(void)
{
#ifdef MLFQ_SCHED
  return getlev();
#endif
  return 0;
}

int 
sys_setpriority(void)
{
#ifdef MLFQ_SCHED
  int pid, priority;
  if(argint(0, &pid) < 0)
    return -1;
  if(argint(1, &priority) < 0)
    return -1;

  if(priority < 0 || priority > 10)
    return -2;
  else
    return setpriority(pid, priority);
#endif
  return 0;
}

int 
sys_thread_create(void)
{
  thread_t *thread;
  void *(*start_routine)(void*);
  void *arg;

  if (argptr(0, (char **)&thread, sizeof thread) < 0)
    return -1;
  if (argptr(1, (char **)&start_routine, sizeof start_routine) < 0)
    return -1;
  if (argptr(2, (char **)&arg, sizeof arg) < 0)
    return -1;

  return thread_create(thread, start_routine, arg);
}

int
sys_thread_exit(void)
{
  void *retval;

  if (argptr(0, (char **)&retval, sizeof retval) < 0)
    return -1;

  thread_exit(retval);

  return 0;
}

int
sys_thread_join(void)
{
  int thread;
  void **retval;

  if (argint(0, &thread) < 0)
    return -1;
  if (argptr(1, (char **)&retval, sizeof retval) < 0)
    return -1;

  return thread_join((thread_t)thread, retval);
}