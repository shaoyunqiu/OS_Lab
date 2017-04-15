#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE);
    cprintf("wakeup proc %d in sched.c/wakeup_proc\n", proc->pid) ;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        if (proc->state != PROC_RUNNABLE) {
            proc->state = PROC_RUNNABLE;
            proc->wait_state = 0;
        }
        else {
            warn("wakeup runnable process.\n");
        }
    }
    local_intr_restore(intr_flag);
}

void
schedule(void) {
    bool intr_flag;
    list_entry_t *le, *last;
    struct proc_struct *next = NULL;
    cprintf("in schedule, the current proc %d: %s, and it's state is %d\n", current->pid, current->name, current->state) ;s
    local_intr_save(intr_flag);
    {
        current->need_resched = 0;
        last = (current == idleproc) ? &proc_list : &(current->list_link);
        le = last;
        do {
            if ((le = list_next(le)) != &proc_list) {
                next = le2proc(le, list_link);
                if (next->state == PROC_RUNNABLE) {
                    break;
                }
            }
        } while (le != last);
        if (next == NULL || next->state != PROC_RUNNABLE) {
            next = idleproc;
        }
        next->runs ++;
        cprintf("the runtimes of next proc is %d in schedule\n", next->runs) ;
        if (next != current) {
            cprintf("next proc %d going to run in schedule\n", next->pid) ;
            proc_run(next);
        }
        else{
            cprintf("the former proc %d still going to run in schedule\n", next->pid) ;
        }
    }
    local_intr_restore(intr_flag);
}

