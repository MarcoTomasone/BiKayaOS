#include "../include/const.h"
#include "../include/listx.h"
#include "../include/types_bikaya.h"
#include "pcb.h"

HIDDEN pcb_t pcbTable[MAXPROC];
HIDDEN LIST_HEAD(pcbFree);


/*
    This function wipes the given structure/variable memory location. It's 
    inspired by memset function in the sandard library. Starting from the given
    location (the void pointer) it sets every byte to O/NULL till reaching the end
    of the given structure (the unsigned int "size").

    block: the memory address from where to start
    size: the number of bytes that the structure to be cleaned uses, recommended to use sizeof()
    
    return: void
*/
HIDDEN void *wipe_Pcb(void *block, unsigned int size) {
    if (size) {
        char *toWipe = block;
        
        while(size--)
            *toWipe++ = 0;
    }
    return (block);
}

/*
    This function uses the array pcbTmp_arr and for every and each PCB it adds it to
    the pcbFree_queue. It simply initializes the free queue of PCBs

    param: void
    return: void
*/
void initPcbs(void) {
    for (unsigned int i = 0; i < MAXPROC; i++)
        list_add_tail(&(pcbTable[i].p_next), &pcbFree);
}

/*
    This function removes the given PCB from all the eventual list/queue that 
    are related to him, but it gives error if the PCBs has some child.

    p: the PCB wich has to be returned to the pcbFree_queue
    return: void
*/
void freePcb(pcb_t *p) {
    list_del(&p->p_next);
    list_add_tail(&p->p_next, &pcbFree);
}

/*
    Function that removes a PCB from the pcbFree_queue if not already empty, 
    wipes the PCB, initialize some fields (p_child and p_sib)
    to empty list and returns it.

    return: the new allocated PCB or NULL if not avaiable
*/
pcb_t *allocPcb(void) {
    //Returns NULL if the pcbFree is empty (no free pcbs avaiable)
    struct list_head *tmp = list_next(&pcbFree); 

    //Error checking
    if (tmp == NULL)
        return (NULL);
    
    else {
        //Delete the pcb from the pcbFree_queue, obtain the pcb_t struct with "container_of" and return it
        pcb_t *newPcb = container_of(tmp, pcb_t, p_next);
        list_del(tmp);

        //Wipes the PCB and initialize his list to empty list
        newPcb = wipe_Pcb(newPcb, sizeof(pcb_t));
        INIT_LIST_HEAD(&newPcb->p_child);
        INIT_LIST_HEAD(&newPcb->p_sib);

        return(newPcb);
    }
}

/*

    This function simply initialize a new active PCB queue and setting the given pointer to be
    the dummy of such queue. Note that the given pointer must be only declared and not set
    to anything active (to avoid any type of bug), make sure that is always NULL.

    head: pointer to the "dummy" of pcbAtvive_queue
    return: void
*/
void mkEmptyProcQ(struct list_head *head) {
    INIT_LIST_HEAD(head);
}

/*
    Check if the given list_head pointer is an empty list/queue

    head: the dummy of the list/queue we want to check
    return: 1 if the list is empty, 0 else
*/
int emptyProcQ(struct list_head *head) {
    return(list_empty(head));
}

/*
    Insert the given PCB p to the pcbActive_queue, mainting the sorting by priority of the queue

    head: the pointer to the dummy of the queue
    p: the pointer to the pcb we want to add
*/
void insertProcQ(struct list_head *head, pcb_t *p) {
    struct list_head *tmp;
    pcb_t *last_examined_pcb;
    //Initial check that the arguments are correct
    if (head == NULL || p == NULL)
        return;

    //If the list is empty then it adds up directly
    else if (list_empty(head))
        list_add(&p->p_next, head);

    //Insert the element maintaining the sorting property of the queue
    else {
        list_for_each(tmp, head) {
            last_examined_pcb = container_of(tmp, pcb_t, p_next);

            //If the PCB has to stay in the middle of the queue, adds it in between and breaks
            if (p->priority > last_examined_pcb->priority) {
                list_add(&p->p_next, tmp->prev);
                return;
            }
        }

        //If the cicle loops til the end then the pcb has to be put in the queue tail (as last element)
        list_add_tail(&p->p_next, head);
    }
}

/*
    This function returns the first element of the pcbActive_queue, so the first PCB 
    in the priority queue (after checking for errors). Note that it doesn't remove the PCB
    from the queue (for that see removeProcQ() below).

    head: the pointer to the dummy of the queue
    return: the PCB pointer or NULL for errors
*/
pcb_t *headProcQ(struct list_head *head) {
    if (head == NULL || list_empty(head))
        return (NULL);

    return(container_of(head->next, pcb_t, p_next));
}

/*
    This function returns the first PCB in the queue as headProcQ but it removes it from the queue
    instead of only returning a reference to it.

    head: the pointer to the dummy of the queue
    return: the PCB pointer or NULL for errors
*/
pcb_t *removeProcQ(struct list_head *head) {
    pcb_t *toRemove = headProcQ(head);

    if (head != NULL)
        list_del(&toRemove->p_next);
    
    return(toRemove);
}

/*
    This function looks in the pcbActive_queue for the pcb given as argument, and returns
    it once and if found. But before it removes it from the queue mentioned above.

    head: the pointer to the dummy of the queue
    p: the process we want to remove from the queue
    return: NULL if error, the requested PCB on success
*/
pcb_t *outProcQ(struct list_head *head, pcb_t *p) {
    struct list_head *tmp;
    pcb_t *block;

    if (head == NULL || list_empty(head) || p == NULL)
        return (NULL);

    list_for_each(tmp, head) {
        block = container_of(tmp, pcb_t, p_next);
        
        if (p == block) { //If there's a match returns the found/given pcb
            list_del(tmp);
            return (block);
        }
    }
    //If there's no match then returns NULL
    return (NULL);
}

/*
    This function check that the given PCB has no childs. If the argument
    is NULL then returnes FALSE.

    this: a pointer to the PCB we want to check
    return: 1 if the PCB has no child, 0 else
*/
int emptyChild(pcb_t *this) {
    return (this != NULL && list_empty(&this->p_child));
}

void insertChild(pcb_t *prnt, pcb_t *p) {
    p->p_parent = prnt;
    list_add_tail(&p->p_sib, &prnt->p_child);
}
pcb_t *removeChild(pcb_t *p) {
    struct list_head *tmp = &p->p_child;
    if (p == NULL || list_empty(tmp))
        return (NULL);

    tmp = tmp->next;
    list_del(tmp);
    return(container_of(tmp, pcb_t, p_sib));
    
}
pcb_t *outChild(pcb_t *p) {
    if (p->p_parent == NULL)
        return(NULL);
    
    struct list_head *tmp, *siblingsList = &p->p_parent->p_child;

    list_for_each(tmp, siblingsList) {
        pcb_t *block = container_of(tmp, pcb_t, p_sib);
        
        if (p == block) { //If there's a match returns the found/given pcb
            list_del(tmp);
            return (block);
        }
    }
    //If there's no match then returns NULL
    return (NULL);
}