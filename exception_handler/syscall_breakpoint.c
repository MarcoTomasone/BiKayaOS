#include "../include/system_const.h"
#include "../include/types_bikaya.h"
#include "../generics/utils.h"
#include "../process/scheduler.h"
#include "../process/asl.h"
#include "../process/pcb.h"
#include "syscall_breakpoint.h"


// A pointer to the old area, used to retrieve info about the exception
HIDDEN state_t *old_area = NULL;

/* ================ SYSCALL DEFINITION ================ */

/*
    This syscall return the current process (also the caller process) time
    statistics such as usermode and kernel mode clock cycle elapsed as
    well as the activation time of the process

    user: the usermode time elapsed (in clocks)
    kernel: the kernelmode time elapsed (in clocks)
    wallclock: the time of the first activation (in clock)
    return: void
*/
HIDDEN void getCPU_time(unsigned int *user, unsigned int *kernel, unsigned int *wallclock) {
    // Update all the data before returning
    update_time(KER_MD_TIME, TOD_LO);
    // Get the time_t struct
    time_t *data = &getCurrentProc()->p_time;

    *user = data->usermode_time;
    *kernel = data->kernelmode_time;
    *wallclock = TOD_LO - data->activation_time;
}


/*
    This syscall create a new process, allocating the PCB, saving the given
    state inside it and setting up the new process in the parent's child list.
    The function takes care also of adding the process to the scheduler, 
    and then calling it (round-robin scheduler).

    statep: the state of the newborn process
    priority: the newborn priority
    cpid: the newborn pid that is as well a pointer to the PCB itself
    return: 0 on success, -1 on failure
*/
HIDDEN void create_process(state_t* statep, int priority, void** cpid) {
    pcb_t *new_proc = allocPcb();
    pcb_t *parent = getCurrentProc();

    // Error during allocation, error code returned
    if (new_proc == NULL || statep == NULL || cpid != NULL || parent == NULL)
        SYS_RETURN_VAL(old_area) = FAILURE;

    // Set the given state to the new process
    cloneState(&new_proc->p_s, statep, sizeof(state_t));

    // Set priority and insert the PCB in the father's child node
    new_proc->priority = priority;
    insertChild(parent, new_proc);

    // Insert the new process in the ready queue and sets the pid
    scheduler_add(new_proc);
    *cpid = new_proc;
    
    SYS_RETURN_VAL(old_area) = SUCCESS;

    // The scheduler is Round Robin, so it saves the new state and calls for another process to execute
    cloneState(&parent->p_s, old_area, sizeof(state_t));
    scheduler();
}


/*
    This syscall terminates the process given as input (pid), removing recursively
    from the ASL, the ready queue and the father's child list.
    If pid is NULL then the current process/caller is killed.
    This is done for all the descendants of the given PCB (Sons, Grandsons, etc).

    pid: a pointer to the process to terminate
    return: 0 on success, -1 on failure
*/
void terminate_process(void* pid) {
    pcb_t* proc = pid ? pid : getCurrentProc(); 
    unsigned int sys_status = 0;
    
    // The function has no process to kill
    if (proc == NULL)
        SYS_RETURN_VAL(old_area) = FAILURE;

    // Removes the root from father's child list
    outChild(proc);
    // Removes it from the semd or ready_queue one of them must be true, else error
    sys_status |= (proc == outBlocked(proc));
    sys_status |= (proc == outProcQ(getReadyQ(), proc));

    struct list_head *tmp = NULL;

    list_for_each(tmp, &proc->p_child) {
        // Recursive call on the childs
        terminate_process(container_of(tmp, pcb_t, p_sib));
        // Check the return code of the recursive call
        if (SYS_RETURN_VAL(old_area) == FAILURE)
            return;
    }

    freePcb(proc);

    SYS_RETURN_VAL(old_area) = sys_status ? SUCCESS : FAILURE;

    // If I killed the current process
    if (pid == NULL) {
        // Fix the dangling reference in the scheduler and chose another process
        setCurrentProc(NULL);
        scheduler();
    }
}


/*
    This syscall releases the semaphore wich is identified with the semaddr arg.
    if other processes are waiting on the same semaphore then before leaving it
    awakes the first in the sem's queue. If a semaphore results with processes 
    blocked on him but removeBlocked returns NULL then kernel panic is issued

    semaddr: the memory location/ value of the semaphore that has to be released
    return: the unblocked process (for internal use only)
*/
pcb_t* verhogen(int *semaddr) {
    *semaddr += 1;
    if (*semaddr <= 0) {
        pcb_t *unblocked_proc = removeBlocked(semaddr);
        
        if (unblocked_proc) {
            scheduler_add(unblocked_proc);
            return(unblocked_proc);
        }

        else // Sem results not empty but in fact is 
            PANIC();
    }

    else return(NULL);
}


/*
    This syscall request a semaphore wich is identified with the semaddr arg.
    If the sem is already reserved then the state is saved and the process is blocked
    and another process choosed by the scheduler start executing.
    Else the process continue it's execution free

    semaddr: the memory location/ value of the semaphore that has to be requested
    return: void
*/
HIDDEN void passeren(int *semaddr) {
    *semaddr -= 1;
    if (*semaddr < 0) {
        // Get the current process PCB (with checks)
        pcb_t *tmp = getCurrentProc();
        (tmp == NULL) ? PANIC() : 0;

        // Saves the updated state adn time stats
        cloneState(&tmp->p_s, old_area, sizeof(state_t));
        update_time(KER_MD_TIME, TOD_LO);

        // Insert the PCB in the semaphor blocked queue
        insertBlocked(semaddr, tmp);

        // Set the scheduler properly
        setCurrentProc(NULL);
        scheduler();
    }   
}


/*

*/
#define DEV_REGISTER_SIZE 4
#define REGISTER_PER_DEV 4
#define DEV_PER_IL 8

HIDDEN void wait_IO(unsigned int command, unsigned int *dev_register, int subdevice) {
    // Retrieve the first address of the multiple_line_device
    memaddr dev_start = (memaddr)DEV_REG_ADDR(IL_DISK, 0);
    memaddr current_memaddr = (memaddr)dev_register;

    // Checks arguments and calculate the offset in words
    (current_memaddr > dev_start) ? 0 : PANIC();
    unsigned int offset = current_memaddr - dev_start;

    // From the word offset then is easy to obtain device class and subdevice
    unsigned int device_class = offset / (DEV_REGISTER_SIZE * REGISTER_PER_DEV * DEV_PER_IL);
    unsigned int device_no = offset % (DEV_REGISTER_SIZE * REGISTER_PER_DEV * DEV_PER_IL);

    // Issue the command
    *dev_register = command;

    // Block the process onto the queue
    int *matrix_cell = &IO_blocked[device_class + subdevice][device_no];
    pcb_t *caller = getCurrentProc();
    caller ? 0 : PANIC();
    passeren(matrix_cell);
}


/*
    This syscall give to the caller the ability to set a custom handler for exception 
    as Breakpoint, Syscall (with No. > 8), TLB and Trap. Each custom handler can be set once
    if the process tries to reset an handler then is killed.

    type: an int that represents which custom handler the process want to set
    old: a memory location in which the state as to be saved before executing the custom handler
    new: the memory location in which the custom handler can be found and loaded
    return: 0 on success and -1 on failure 
*/    
HIDDEN void spec_passup(int type, state_t *old, state_t *new) {
    pcb_t *caller = getCurrentProc();
    caller ? 0 : PANIC();

    // The custom handler could be only setted once for each exception
    if (! caller->custom_hndlr.has_custom_handler[type]) {
        caller->custom_hndlr.has_custom_handler[type] = ON;

        switch (type) {
            case SYS_BP_COSTUM:
                caller->custom_hndlr.syscall_bp_new = new;
                caller->custom_hndlr.syscall_bp_old = old;
                SYS_RETURN_VAL(old_area) = SUCCESS;
                break; 

            case TLB_CUSTOM:
                caller->custom_hndlr.tlb_new = new;
                caller->custom_hndlr.tlb_old = old;
                SYS_RETURN_VAL(old_area) = SUCCESS;
                break;

            case TRAP_CUSTOM:
                caller->custom_hndlr.trap_new = new;
                caller->custom_hndlr.trap_old = old;
                SYS_RETURN_VAL(old_area) = SUCCESS;
                break;
            
            // Option not recognised
            default:
                SYS_RETURN_VAL(old_area) = FAILURE;
        }
    }

    // If a process try to "reset" a custom handler is killed
    else terminate_process(caller);
}


/*
    This syscall assign the current process pid and his parent pid
    to the given parameter (after checking that both are not NULL)

    pid: the mem location where to save the pid
    ppid: the mem location where to save the curr. proc. parent pid
    retunr: void;
*/
HIDDEN void get_PID_PPID(void** pid, void** ppid){
    pcb_t *current = getCurrentProc();
    *pid = pid ? current : NULL;
    *ppid = ppid ? current->p_parent : NULL;
}



/* ========== SYSCALL & BREAKPOINT HANDLER ========== */

/* 
    This function takes the syscall number and call the appropriate system call,
    eventually initializing the args of each syscall. If a syscall is not recognized
    or implemented then issue a Kernel Panic

    sysNumber: the syscall number retrieved from the Old Area
    return: void
*/
void syscallDispatcher(unsigned int sysNumber) {
    switch (sysNumber) {
        case GETCPUTIME:
            getCPU_time((unsigned int*)SYS_ARG_1(old_area), (unsigned int*)SYS_ARG_2(old_area), (unsigned int*)SYS_ARG_3(old_area));
            break;

        case CREATEPROCESS:
            create_process((state_t*)SYS_ARG_1(old_area), (int)SYS_ARG_2(old_area), (void**)SYS_ARG_3(old_area));
            break;

        case TERMINATEPROCESS:
            terminate_process((void*)SYS_ARG_1(old_area));
            break; 

        case VERHOGEN:
            verhogen((int*)SYS_ARG_1(old_area));
            break;

        case PASSEREN:
            passeren((int*)SYS_ARG_1(old_area));
            break;

        case WAITIO:
            wait_IO((unsigned int)SYS_ARG_1(old_area), (unsigned int*)SYS_ARG_2(old_area), (int)SYS_ARG_3(old_area));
            break;

        case SPECPASSUP:
            spec_passup((int)SYS_ARG_1(old_area), (state_t*)SYS_ARG_2(old_area), (state_t*)SYS_ARG_3(old_area));
            break;

        case GETPID:
            get_PID_PPID((void**)SYS_ARG_1(old_area), (void**)SYS_ARG_2(old_area));
            break;

        default:
            PANIC();
    }
}

/*
    This is the handler of the syscall/breakpoint new area.
    It checks for the cause register exception code and eventually calls
    the subhandlers (one for syscall, one for breakpoiints).

    return: void
*/
void syscall_breakpoint_handler(void) {
    // At first update the user_time of execution
    update_time(USR_MD_TIME, TOD_LO);
    // Retrieve the old area, where the previous state is saved and extrapolate the exception code
    old_area = (state_t*) OLD_AREA_SYSCALL;
    unsigned int exCode = getExCode(old_area);

    // Sets the PC to the next instruction in uMPS
    #ifdef TARGET_UMPS
    PC_REG(old_area) += WORDSIZE;
    #endif

    // Checsks if the code is for a syscall and not a breakpoint
    if (exCode == SYSCALL_CODE) {  
        unsigned int numberOfSyscall = SYSCALL_NO(old_area);
        syscallDispatcher(numberOfSyscall);
    }

    // Else is a breakpoint 
    else if(exCode == BREAKPOINT_CODE)
        PANIC();

    // Unrecognized code for this handler
    else         
        PANIC();
    
    // At last update the kernel mode execution time
    update_time(KER_MD_TIME, TOD_LO);
    LDST(old_area);
}