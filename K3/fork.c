#include "types.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/string.h"

#include "proc/proc.h"
#include "proc/kthread.h"

#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/page.h"
#include "mm/pframe.h"
#include "mm/mmobj.h"
#include "mm/pagetable.h"
#include "mm/tlb.h"

#include "fs/file.h"
#include "fs/vnode.h"

#include "vm/shadow.h"
#include "vm/vmmap.h"

#include "api/exec.h"

#include "main/interrupt.h"

/* Pushes the appropriate things onto the kernel stack of a newly forked thread
 * so that it can begin execution in userland_entry.
 * regs: registers the new thread should have on execution
 * kstack: location of the new thread's kernel stack
 * Returns the new stack pointer on success. */
static uint32_t
fork_setup_stack(const regs_t *regs, void *kstack)
{
        /* Pointer argument and dummy return address, and userland dummy return
         * address */
        uint32_t esp = ((uint32_t) kstack) + DEFAULT_STACK_SIZE - (sizeof(regs_t) + 12);
        *(void **)(esp + 4) = (void *)(esp + 8); /* Set the argument to point to location of struct on stack */
        memcpy((void *)(esp + 8), regs, sizeof(regs_t)); /* Copy over struct */
        return esp;
}


/*
 * The implementation of fork(2). Once this works,
 * you're practically home free. This is what the
 * entirety of Weenix has been leading up to.
 * Go forth and conquer.
 */
int
do_fork(struct regs *regs)
{

        KASSERT(regs != NULL);
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        KASSERT(curproc != NULL);
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        KASSERT(curproc->p_state == PROC_RUNNING);
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");

        vmarea_t *vma, *clone_vma;
        pframe_t *pf;
        mmobj_t *to_delete, *new_shadowed;

        //NOT_YET_IMPLEMENTED("VM: do_fork");
        //return 0;

        proc_t *clone_proc = proc_create("child");

        vmmap_t *clone_map = vmmap_clone(curproc->p_vmmap);
        clone_map->vmm_proc = clone_proc;

        clone_proc->p_vmmap = clone_map;

        list_iterate_begin(&(clone_map->vmm_list), clone_vma, vmarea_t, vma_plink){
            vma = vmmap_lookup(curproc->p_vmmap, clone_vma->vma_start);
            if(clone_vma->vma_flags & MAP_PRIVATE){
                vma->vma_obj->mmo_ops->ref(vma->vma_obj);

                to_delete = shadow_create();
                to_delete->mmo_shadowed = vma->vma_obj;
                to_delete->mmo_un.mmo_bottom_obj = vma->vma_obj->mmo_un.mmo_bottom_obj;

                new_shadowed = shadow_create();
                new_shadowed->mmo_shadowed = vma->vma_obj;
                new_shadowed->mmo_un.mmo_bottom_obj = vma->vma_obj->mmo_un.mmo_bottom_obj;

                vma->vma_obj = to_delete;
                clone_vma->vma_obj = new_shadowed;
                dbg(DBG_PRINT, "(GRADING3A 7)\n");
            }else{
                clone_vma->vma_obj = vma->vma_obj;
                clone_vma->vma_obj->mmo_ops->ref(clone_vma->vma_obj);
                dbg(DBG_PRINT, "(GRADING3A 7)\n");
            }

            dbg(DBG_PRINT, "(GRADING3A 7)\n");

            list_insert_tail(mmobj_bottom_vmas(vma->vma_obj), &(clone_vma->vma_olink));
        }list_iterate_end();

        kthread_t *clone_thread = kthread_clone(curthr);
        clone_thread->kt_proc = clone_proc;
        list_insert_tail(&clone_proc->p_threads, &clone_thread->kt_plink);

        clone_thread->kt_ctx.c_eip = (uint32_t) userland_entry;
        clone_thread->kt_ctx.c_pdptr = clone_proc->p_pagedir;
        regs->r_eax = 0;
        
        clone_thread->kt_ctx.c_esp = fork_setup_stack(regs, clone_thread->kt_kstack);

        KASSERT(clone_proc->p_state == PROC_RUNNING);
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        KASSERT(clone_proc->p_pagedir != NULL);
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        KASSERT(clone_thread->kt_kstack != NULL);
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");

        for(int i = 0; i < NFILES; i++){
            clone_proc->p_files[i] = curproc->p_files[i];
            if(clone_proc->p_files[i]){
                fref(clone_proc->p_files[i]);
                dbg(DBG_PRINT, "(GRADING3A 7)\n");
            }
            dbg(DBG_PRINT, "(GRADING3A 7)\n");
        }

        pt_unmap_range(curproc->p_pagedir, USER_MEM_LOW, USER_MEM_HIGH);
        tlb_flush_all();

        clone_proc->p_start_brk = curproc->p_start_brk;
        clone_proc->p_brk = curproc->p_brk;

        sched_make_runnable(clone_thread);
        regs->r_eax = clone_proc->p_pid;

        dbg(DBG_PRINT, "(GRADING3A 7)\n");

        return clone_proc->p_pid;
}