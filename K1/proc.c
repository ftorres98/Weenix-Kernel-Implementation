#include "kernel.h"
#include "config.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"
#include "proc/proc.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/mm.h"
#include "mm/mman.h"

#include "vm/vmmap.h"

#include "fs/vfs.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/file.h"

proc_t *curproc = NULL; /* global */
static slab_allocator_t *proc_allocator = NULL;

static list_t _proc_list;
static proc_t *proc_initproc = NULL; /* Pointer to the init process (PID 1) */

void
proc_init()
{
        list_init(&_proc_list);
        proc_allocator = slab_allocator_create("proc", sizeof(proc_t));
        KASSERT(proc_allocator != NULL);
}

proc_t *
proc_lookup(int pid)
{
        proc_t *p;
        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                if (p->p_pid == pid) {
                        return p;
                }
        } list_iterate_end();
        return NULL;
}

list_t *
proc_list()
{
        return &_proc_list;
}

size_t
proc_info(const void *arg, char *buf, size_t osize)
{
        const proc_t *p = (proc_t *) arg;
        size_t size = osize;
        proc_t *child;

        KASSERT(NULL != p);
        KASSERT(NULL != buf);

        iprintf(&buf, &size, "pid:          %i\n", p->p_pid);
        iprintf(&buf, &size, "name:         %s\n", p->p_comm);
        if (NULL != p->p_pproc) {
                iprintf(&buf, &size, "parent:       %i (%s)\n",
                        p->p_pproc->p_pid, p->p_pproc->p_comm);
        } else {
                iprintf(&buf, &size, "parent:       -\n");
        }

#ifdef __MTP__
        int count = 0;
        kthread_t *kthr;
        list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
                ++count;
        } list_iterate_end();
        iprintf(&buf, &size, "thread count: %i\n", count);
#endif

        if (list_empty(&p->p_children)) {
                iprintf(&buf, &size, "children:     -\n");
        } else {
                iprintf(&buf, &size, "children:\n");
        }
        list_iterate_begin(&p->p_children, child, proc_t, p_child_link) {
                iprintf(&buf, &size, "     %i (%s)\n", child->p_pid, child->p_comm);
        } list_iterate_end();

        iprintf(&buf, &size, "status:       %i\n", p->p_status);
        iprintf(&buf, &size, "state:        %i\n", p->p_state);

#ifdef __VFS__
#ifdef __GETCWD__
        if (NULL != p->p_cwd) {
                char cwd[256];
                lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                iprintf(&buf, &size, "cwd:          %-s\n", cwd);
        } else {
                iprintf(&buf, &size, "cwd:          -\n");
        }
#endif /* __GETCWD__ */
#endif

#ifdef __VM__
        iprintf(&buf, &size, "start brk:    0x%p\n", p->p_start_brk);
        iprintf(&buf, &size, "brk:          0x%p\n", p->p_brk);
#endif

        return size;
}

size_t
proc_list_info(const void *arg, char *buf, size_t osize)
{
        size_t size = osize;
        proc_t *p;

        KASSERT(NULL == arg);
        KASSERT(NULL != buf);

#if defined(__VFS__) && defined(__GETCWD__)
        iprintf(&buf, &size, "%5s %-13s %-18s %-s\n", "PID", "NAME", "PARENT", "CWD");
#else
        iprintf(&buf, &size, "%5s %-13s %-s\n", "PID", "NAME", "PARENT");
#endif

        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                char parent[64];
                if (NULL != p->p_pproc) {
                        snprintf(parent, sizeof(parent),
                                 "%3i (%s)", p->p_pproc->p_pid, p->p_pproc->p_comm);
                } else {
                        snprintf(parent, sizeof(parent), "  -");
                }

#if defined(__VFS__) && defined(__GETCWD__)
                if (NULL != p->p_cwd) {
                        char cwd[256];
                        lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                        iprintf(&buf, &size, " %3i  %-13s %-18s %-s\n",
                                p->p_pid, p->p_comm, parent, cwd);
                } else {
                        iprintf(&buf, &size, " %3i  %-13s %-18s -\n",
                                p->p_pid, p->p_comm, parent);
                }
#else
                iprintf(&buf, &size, " %3i  %-13s %-s\n",
                        p->p_pid, p->p_comm, parent);
#endif
        } list_iterate_end();
        return size;
}

static pid_t next_pid = 0;

/**
 * Returns the next available PID.
 *
 * Note: Where n is the number of running processes, this algorithm is
 * worst case O(n^2). As long as PIDs never wrap around it is O(n).
 *
 * @return the next available PID
 */
static int
_proc_getid()
{
        proc_t *p;
        pid_t pid = next_pid;
        while (1) {
failed:
                list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                        if (p->p_pid == pid) {
                                if ((pid = (pid + 1) % PROC_MAX_COUNT) == next_pid) {
                                        return -1;
                                } else {
                                        goto failed;
                                }
                        }
                } list_iterate_end();
                next_pid = (pid + 1) % PROC_MAX_COUNT;
                return pid;
        }
}

/*
 * The new process, although it isn't really running since it has no
 * threads, should be in the PROC_RUNNING state.
 *
 * Don't forget to set proc_initproc when you create the init
 * process. You will need to be able to reference the init process
 * when reparenting processes to the init process.
 */
proc_t *
proc_create(char *name)
{
        //NOT_YET_IMPLEMENTED("PROCS: proc_create");
        proc_t* p = (proc_t*)slab_obj_alloc(proc_allocator);

        p->p_pid = _proc_getid();
        KASSERT(PID_IDLE != p->p_pid || list_empty(&_proc_list));
        dbg(DBG_PRINT, "(GRADING1A 2.a)\n");
        KASSERT(PID_INIT != p->p_pid || PID_IDLE == curproc->p_pid);
        dbg(DBG_PRINT, "(GRADING1A 2.a)\n");

        strncpy(p->p_comm, name, PROC_NAME_LEN);
        p->p_comm[PROC_NAME_LEN-1] = '\0';

        list_init(&(p->p_threads));
        list_init(&(p->p_children));

        // set parent processes ------
        p->p_pproc = curproc;

        if (p->p_pid==PID_INIT) {
            proc_initproc=p;
            dbg(DBG_PRINT, "(GRADING1A 2)\n");
        }

        p->p_status = 0;

        p->p_state = PROC_RUNNING;

        sched_queue_init(&(p->p_wait));

        p->p_pagedir = pt_create_pagedir();

        list_link_init(&(p->p_list_link));
        list_link_init(&(p->p_child_link));

        // adds the children to current process's child list
        if (curproc!=NULL) {
            list_insert_tail(&(curproc->p_children), &(p->p_child_link));   // link on parent process' p_children list
            dbg(DBG_PRINT, "(GRADING1A 2)\n");
        }

        // modifying _proc_list
        list_insert_tail(&(_proc_list), &(p->p_list_link)); // list_link_t link gets added to list_t listttt

        dbg(DBG_PRINT, "(GRADING1A 2)\n");
        return p;
}

/**
 * Cleans up as much as the process as can be done from within the
 * process. This involves:
 *    - Closing all open files (VFS)
 *    - Cleaning up VM mappings (VM)
 *    - Waking up its parent if it is waiting
 *    - Reparenting any children to the init process
 *    - Setting its status and state appropriately
 *
 * The parent will finish destroying the process within do_waitpid (make
 * sure you understand why it cannot be done here). Until the parent
 * finishes destroying it, the process is informally called a 'zombie'
 * process.
 *
 * This is also where any children of the current process should be
 * reparented to the init process (unless, of course, the current
 * process is the init process. However, the init process should not
 * have any children at the time it exits).
 *
 * Note: You do _NOT_ have to special case the idle process. It should
 * never exit this way.
 *
 * @param status the status to exit the process with
 */
void
proc_cleanup(int status)
{
        //NOT_YET_IMPLEMENTED("PROCS: proc_cleanup");

        KASSERT(NULL != proc_initproc);
        dbg(DBG_PRINT, "(GRADING1A 2.b)\n");

        KASSERT(1 <= curproc->p_pid);
        dbg(DBG_PRINT, "(GRADING1A 2.b)\n");

        KASSERT(NULL != curproc->p_pproc);
        dbg(DBG_PRINT, "(GRADING1A 2.b)\n");

        sched_wakeup_on(&(curproc->p_pproc->p_wait));

        proc_t* p;
        list_iterate_begin(&(curproc->p_children), p, proc_t, p_child_link) {
                
                list_insert_tail(&(proc_initproc->p_children), &(p->p_child_link));
                p->p_pproc=proc_initproc;
                dbg(DBG_PRINT, "(GRADING1A 2)\n");

        } list_iterate_end();


        curproc->p_status = status;
        curproc->p_state = PROC_DEAD;

        KASSERT(NULL != curproc->p_pproc);
        dbg(DBG_PRINT, "(GRADING1A 2.b)\n");

        KASSERT(KT_EXITED == curthr->kt_state);
        dbg(DBG_PRINT, "(GRADING1A 2.b)\n");

}

/*
 * This has nothing to do with signals and kill(1).
 *
 * Calling this on the current process is equivalent to calling
 * do_exit().
 *
 * In Weenix, this is only called from proc_kill_all.
 */
void
proc_kill(proc_t *p, int status)
{
        //NOT_YET_IMPLEMENTED("PROCS: proc_kill");
        if(p == curproc){
                dbg(DBG_PRINT, "(GRADING1A)\n");
                do_exit(status);
        }
        else{
                kthread_t *kthr;
                list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
                        kthread_cancel(kthr,(void *)status);
                        dbg(DBG_PRINT, "(GRADING1A)\n");
                } list_iterate_end();
                dbg(DBG_PRINT, "(GRADING1A)\n");
        }
}

/*
 * Remember, proc_kill on the current process will _NOT_ return.
 * Don't kill direct children of the idle process.
 *
 * In Weenix, this is only called by sys_halt.
 */
void
proc_kill_all()
{
        //NOT_YET_IMPLEMENTED("PROCS: proc_kill_all");
        proc_t *p;
        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                if (p->p_pproc != NULL && p->p_pproc->p_pid != PID_IDLE && p->p_pid != PID_IDLE && p != curproc) { // NEW EDIT
                        proc_kill(p, 0);
                        dbg(DBG_PRINT, "(GRADING1A)\n");
                }
                dbg(DBG_PRINT, "(GRADING1A)\n");
        } list_iterate_end();
        dbg(DBG_PRINT, "(GRADING1A)\n");

        /*list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                
                if (p->p_pproc && p->p_pproc->p_pid != PID_IDLE && p != curproc)
                {
                        //dbg(DBG_PRINT, "(GRADING1C 9)\n");
                        proc_kill(p, 0);
                }
                //dbg(DBG_PRINT, "(GRADING1C 9)\n");
                
        } list_iterate_end();*/

        if (curproc->p_pproc != NULL && curproc->p_pproc->p_pid != PID_IDLE)
        {
                proc_kill(curproc, 0);
        }
}

/*
 * This function is only called from kthread_exit.
 *
 * Unless you are implementing MTP, this just means that the process
 * needs to be cleaned up and a new thread needs to be scheduled to
 * run. If you are implementing MTP, a single thread exiting does not
 * necessarily mean that the process should be exited.
 */
void
proc_thread_exited(void *retval)
{
        //NOT_YET_IMPLEMENTED("PROCS: proc_thread_exited");

        proc_cleanup((int)retval);
        dbg(DBG_PRINT, "(GRADING1A)\n");
        sched_switch();

}

/* If pid is -1 dispose of one of the exited children of the current
 * process and return its exit status in the status argument, or if
 * all children of this process are still running, then this function
 * blocks on its own p_wait queue until one exits.
 *
 * If pid is greater than 0 and the given pid is a child of the
 * current process then wait for the given pid to exit and dispose
 * of it.
 *
 * If the current process has no children, or the given pid is not
 * a child of the current process return -ECHILD.
 *
 * Pids other than -1 and positive numbers are not supported.
 * Options other than 0 are not supported.
 */
pid_t
do_waitpid(pid_t pid, int options, int *status)
{
        //NOT_YET_IMPLEMENTED("PROCS: do_waitpid");
        
        if (list_empty(&(curproc->p_children))) {
            dbg(DBG_PRINT, "(GRADING1A 2)\n");
            return -ECHILD;
        }

        if (pid==-1) {

            while (1) {
                proc_t* p;
                pid_t pidReturn;
                list_iterate_begin(&(curproc->p_children), p, proc_t, p_child_link) {

                        if (p->p_state==PROC_DEAD) {
                            KASSERT(NULL != p);
                            dbg(DBG_PRINT, "(GRADING1A 2.c)\n");
                            KASSERT(-1 == pid || p->p_pid == pid);
                            dbg(DBG_PRINT, "(GRADING1A 2.c)\n");
                            KASSERT(NULL != p->p_pagedir);
                            dbg(DBG_PRINT, "(GRADING1A 2.c)\n");

                            *status = p->p_status;
                            pidReturn = p->p_pid;

                            if (list_link_is_linked(&(p->p_list_link))) {
                                list_remove(&(p->p_list_link));
                                dbg(DBG_PRINT, "(GRADING1A 2)\n");
                            }
                            if (list_link_is_linked(&(p->p_child_link))) {
                                list_remove(&(p->p_child_link));
                                dbg(DBG_PRINT, "(GRADING1A 2)\n");
                            }

                            kthread_t *kthr = list_head(&(p->p_threads), kthread_t, kt_plink);
                            kthread_destroy(kthr);
                            pt_destroy_pagedir(p->p_pagedir);

                            slab_obj_free(proc_allocator, p);
                            dbg(DBG_PRINT, "(GRADING1A 2)\n");
                            return pidReturn;
                        }
                        dbg(DBG_PRINT, "(GRADING1A 2)\n");

                } list_iterate_end();

                sched_sleep_on(&(curproc->p_wait));
                dbg(DBG_PRINT, "(GRADING1A 2)\n");
            }

        }

        else if (pid>0) {
            proc_t* p;
            pid_t pidReturn;
            list_iterate_begin(&(curproc->p_children), p, proc_t, p_child_link) {

                    if (p->p_pid==pid) {
                        KASSERT(NULL != p);
                        dbg(DBG_PRINT, "(GRADING1A 2.c)\n");
                        KASSERT(-1 == pid || p->p_pid == pid);
                        dbg(DBG_PRINT, "(GRADING1A 2.c)\n");
                        KASSERT(NULL != p->p_pagedir);
                        dbg(DBG_PRINT, "(GRADING1A 2.c)\n");

                        *status = p->p_status;
                        pidReturn = p->p_pid;

                        while (p->p_state!=PROC_DEAD) {
                            sched_sleep_on(&(curproc->p_wait));
                            dbg(DBG_PRINT, "(GRADING1A 2)\n");
                        }

                        if (list_link_is_linked(&(p->p_list_link))) {
                                list_remove(&(p->p_list_link));
                                dbg(DBG_PRINT, "(GRADING1A 2)\n");
                        }
                        if (list_link_is_linked(&(p->p_child_link))) {
                            list_remove(&(p->p_child_link));
                            dbg(DBG_PRINT, "(GRADING1A 2)\n");
                        }

                        kthread_t *kthr = list_head(&(p->p_threads), kthread_t, kt_plink);
                        kthread_destroy(kthr);
                        pt_destroy_pagedir(p->p_pagedir);

                        slab_obj_free(proc_allocator, p);
                        dbg(DBG_PRINT, "(GRADING1A 2)\n");
                        return pidReturn;
                    }
                    dbg(DBG_PRINT, "(GRADING1A 2)\n");

            } list_iterate_end();
            dbg(DBG_PRINT, "(GRADING1A 2)\n");

        }
        
        dbg(DBG_PRINT, "(GRADING1A 2)\n");
        return -ECHILD;

}

/*
 * Cancel all threads and join with them (if supporting MTP), and exit from the current
 * thread.
 *
 * @param status the exit status of the process
 */
void
do_exit(int status)
{
        //NOT_YET_IMPLEMENTED("PROCS: do_exit");
        dbg(DBG_PRINT, "(GRADING1A 2)\n");
        kthread_exit((void *) status);
}
