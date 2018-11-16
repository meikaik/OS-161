#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>
#include <array.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include "opt-A2.h"





#if OPT_A2

int sys_fork(struct trapframe *tf, pid_t *retval) {

  // Create process structure for child process
  struct proc *child = proc_create_runprogram(curproc->p_name);

  // Create parent child relationship
  lock_acquire(process_arr_lock);
  struct proc_attr *childd = getproc(child->pid);
  lock_release(process_arr_lock);
  childd->ppid = curproc->pid;


  // Check for proc_create_runprogram failure
  if (child == NULL) {
    DEBUG(DB_SYSCALL, "ERR sys_fork - creating child process: proc_create_runprogram(curproc->p_name) \n");
    return ENPROC;
  }

  // Create and copy address space to child
  int error = as_copy(curproc_getas(), &(child->p_addrspace));

  // Check for as_copy failure and destroy during error
  if (error != 0) {
      DEBUG(DB_SYSCALL, "ERR sys_fork - copy child process address space: as_copy(curproc_getas(), &(child->p_addrspace)) \n");
      proc_destroy(child);
      return ENOMEM;
  }

  // Create trapframe
  struct trapframe *new_tf = kmalloc(sizeof(struct trapframe));

  // Check for trapframe create failure
  if (new_tf == NULL) {
    DEBUG(DB_SYSCALL, "ERR new trapframe for child \n");
    proc_destroy(child);
    return ENOMEM;
  }

  memcpy(new_tf, tf, sizeof(struct trapframe));

  // Create thread for child process
  error = thread_fork(curthread->t_name, child, &enter_forked_process, new_tf, 0);

  // Check for thread fork error
  if (error != 0){
    proc_destroy(child);
    kfree(tf);
    return ENOTSUP; // revisit this error code
  }

  *retval = child->pid;

  DEBUG(DB_SYSCALL, "SUCCESS sys_fork \n");
  return(0);
}

int sys_execv(char *progname, char **args) {
  if (progname == NULL) {
    return EFAULT;
  }

  (void) args; // prevent warning for now

  struct addrspace *old; // added this
  struct addrspace *as;
  struct vnode *v;
  vaddr_t entrypoint, stackptr;
  int result;

  /* Open the file. */
  result = vfs_open(progname, O_RDONLY, 0, &v);
  if (result) {
    return result;
  }

  /* Create a new address space. */
  as = as_create();
  if (as == NULL) {
    vfs_close(v);
    return ENOMEM;
  }

  /* Switch to it and activate it. */
  curproc_setas(as);
  as_activate();

  /* Load the executable. */
  result = load_elf(v, &entrypoint);
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    vfs_close(v);
    curproc_setas(old); // set old address space to curproc
    return result;
  }

  /* Done with the file now. */
  vfs_close(v);

  /* Define the user stack in the address space */
  result = as_define_stack(as, &stackptr);
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    curproc_setas(old); // set old address space to curproc
    return result;
  }

  /* Warp to user mode. */
  enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/, stackptr, entrypoint);

  /* enter_new_process does not return. */
  panic("enter_new_process returned\n");
  return EINVAL;

}

#endif

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

#if OPT_A2
  lock_acquire(process_arr_lock);

  struct proc_attr *child = getproc(curproc->pid);

  if (child->ppid != -1) {
    child->exitcode = _MKWAIT_EXIT(exitcode);
    child->state = ZOMBIE;
    cv_broadcast(wait_cv, process_arr_lock);
  } else {
    child->state = EXITED;
  }

  unsigned int len = array_num(process_arr);
  for(unsigned int i = 0; i < len; i++) {
	struct proc_attr *cur = array_get(process_arr, i);
	// if we found a process match in the arr && the process is a zombie
	if(child->pid == cur->ppid && ZOMBIE == cur->state ) {
	  cur->ppid = -1;
	  cur->state = EXITED;
	}
  }
  lock_release(process_arr_lock);
#endif



  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
    *retval = curproc->pid;
    return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result = 0;

  if (options != 0) {
    return EINVAL;
  }

#if OPT_A2

  lock_acquire(process_arr_lock);
  struct proc_attr *child = getproc(pid);

  if (child == NULL){
    lock_release(process_arr_lock);
    return ESRCH;
  }
  else if (curproc->pid != child->ppid) {
    lock_release(process_arr_lock);
    return ECHILD;
  }


  // Wait if child is alive
  while(child->state == RUNNING) {
    cv_wait(wait_cv, process_arr_lock);
  }

  exitstatus = child->exitcode;

  lock_release(process_arr_lock);
#endif
//  Previous stubbed exitstatus code:
//  for now, just pretend the exitstatus is 0
//  exitstatus = 0;

  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

