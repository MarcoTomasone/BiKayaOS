#include "../include/system_const.h"
#include "../process/scheduler.h"
#include "../generics/utils.h"
#include "../process/pcb.h"
#include "syscall_bp.h"
#include "interrupt.h"



// Old Area pointer, used to retrieve info about the exception
HIDDEN state_t *old_area = NULL;



/* ============= SUBHANDLER DEFINITION ============ */

HIDDEN void tmp(unsigned int line) {
   PANIC();
}

HIDDEN void intervalTimer_hadler(unsigned int line) {
   // Send an Ack to the timer, sets him up to a timeslice
   setIntervalTimer();
}

//Handler for Disks, Tapes, Networks and Printers devices 
HIDDEN void generic_device_handler(unsigned int line) {
   // Get the interrupt pending in terminal device
   unsigned int pending = *((memaddr*) CDEV_BITMAP_ADDR(line));
   
   for (unsigned int subdev = 0; subdev < DEV_PER_INT; subdev++) {
      // If a deviice has a pending interrupt, get a reference to it
      if ((pending & (1 << subdev)) == ON) {
         dtpreg_t *tmp_dev = (dtpreg_t*)DEV_REG_ADDR(line, subdev);
         
         if (tmp_dev->status == DTP_RDY) {
            pcb_t *unblocked = verhogen(&IO_blocked[EXT_IL_INDEX(line)][subdev]); 
            SYS_RETURN_VAL(((state_t*) &unblocked->p_s)) = DEV_STATUS_REG(tmp_dev);
            tmp_dev->command = CMD_ACK; //Fare busy waiting per l'esecuzione, non credo??
         }

         }
      }
   }

HIDDEN void terminal_handler(unsigned int line) {
   // Get the interrupt pending in terminal device
   unsigned int pending = *((memaddr*) CDEV_BITMAP_ADDR(line));
   
   for (unsigned int subdev = 0; subdev < DEV_PER_INT; subdev++) {
      // If a deviice has a pending interrupt, get a reference to it
      if ((pending & (1 << subdev)) == ON) {
         termreg_t *tmp_term = (termreg_t*)DEV_REG_ADDR(IL_TERMINAL, subdev);
         
         if (TRANSM_STATUS(tmp_term) == TERM_SUCCESS) {
            pcb_t *unblocked = verhogen(&IO_blocked[EXT_IL_INDEX(line)][subdev]);
            SYS_RETURN_VAL(((state_t*) &unblocked->p_s)) = tmp_term->transm_status;
            tmp_term->transm_command = CMD_ACK;
         }

         else if (RECV_STATUS(tmp_term) == TERM_SUCCESS) {
            pcb_t *unblocked = verhogen(&IO_blocked[EXT_IL_INDEX(line) + 1][subdev]);
            SYS_RETURN_VAL(((state_t*) &unblocked->p_s)) = tmp_term->recv_status;
            tmp_term->recv_command = CMD_ACK;
         }
      }
   }
   
}

/* ============= INTERRUPT HANDLER ============= */

/*
   Returns pending and non-pending interrupt line as a vector, with cross code
   for both architechtures

   interruptVector: the vector to populate (must be at least 8 cell long)
   return: void
*/
HIDDEN void getInterruptLines(unsigned int interruptVector[]) {
   #ifdef TARGET_UMPS
   unsigned int interruptLines = (((CAUSE_REG(old_area)) & LINE_MASK) >> LINE_OFFSET);
   for (unsigned int i = 0; i < MAX_LINE; i++)
      interruptVector[i] = interruptLines & (1 << i);
   #endif

   #ifdef TARGET_UARM
   unsigned int causeReg = CAUSE_REG(old_area);
   for (unsigned int i = 0; i < MAX_LINE; i++) {
      interruptVector[i] = CAUSE_IP_GET(causeReg, i);
   }
   #endif
}


// Vector of subhandler, there's one handler for each interrupt line
void (*subhandler[])(unsigned int) = { tmp, tmp, intervalTimer_hadler, generic_device_handler, generic_device_handler, generic_device_handler, generic_device_handler, terminal_handler };


/*
   The interrupt handler manages all the 8 line (each for one device class)
   It retrieves the cause of the interrupt from the old area and executes the subhandler
   on the line that presents an interrupt pending
*/
void interrupt_handler(void) {
   old_area = (state_t*) OLD_AREA_INTERRUPT;
   
   // In uARM an interrupt can block the current instructon so it has to be broght back the PC
   #ifdef TARGET_UARM
   PC_REG(old_area) -=  WORDSIZE;
   #endif
   
   // Check the exception code
   if (getExCode(old_area) != INTERRUPT_CODE)
      PANIC();
   
   // Retrieve a vectorized version of the pending interrupt 
   unsigned int interruptVector[MAX_LINE];
   getInterruptLines(interruptVector);

   for (unsigned int line = 0; line < MAX_LINE; line++) 
      if (interruptVector[line])
         subhandler[line](line);

   // Save the current old area state to the process that has executed
   pcb_t *currentProcess = getCurrentProc();
   currentProcess ? cloneState(&currentProcess->p_s, old_area, sizeof(state_t)) : 0;
   // The scheduler will chose a process and reset a timeslice, else it will loop
   scheduler();
}