#include "./include/system_const.h"
#include "./generics/utils.h"
#include "include/listx.h"
#include "./process/pcb.h"

#define TOD_LO     *((unsigned int *)BUS_REG_TOD_LO)
#define TIME_SCALE *((unsigned int *)BUS_REG_TIME_SCALE)
//#define RAMBASE    *((unsigned int *)BUS_REG_RAM_BASE)
//#define RAMSIZE    *((unsigned int *)BUS_REG_RAM_SIZE)
//#define RAMTOP     (RAMBASE + RAMSIZE)

#define ST_READY         1
#define ST_BUSY          3
#define ST_TRANSMITTED   5
#define CMD_ACK          1
#define CMD_TRANSMIT     2
#define CHAR_OFFSET      8
#define TERM_STATUS_MASK 0xFF

#define SYS3       3
#define STEPS      6

volatile int test1_baton[STEPS + 1] = {0};
volatile int test2_baton[STEPS + 1] = {0};
volatile int test3_baton[STEPS + 1] = {0};

typedef unsigned int devreg;

static unsigned int get_microseconds() {
    //return TOD_LO / TIME_SCALE;
    return(0);
}

static void delay_ms(unsigned int ms) {
    unsigned int start = get_microseconds();

    while (get_microseconds() - start <= ms * 1000)
        ;
}

/******************************************************************************
 * I/O Routines to write on a terminal
 ******************************************************************************/

#ifdef TARGET_UMPS
static termreg_t *term0_reg = (termreg_t *)DEV_REG_ADDR(IL_TERMINAL, 0);

static unsigned int tx_status(termreg_t *tp) {
    return ((tp->transm_status) & TERM_STATUS_MASK);
}

void termprint(char *str) {
    while (*str) {
        unsigned int stat = tx_status(term0_reg);
        if (stat != ST_READY && stat != ST_TRANSMITTED)
            return;

        term0_reg->transm_command = (((*str) << CHAR_OFFSET) | CMD_TRANSMIT);

        while ((stat = tx_status(term0_reg)) == ST_BUSY)
            ;

        term0_reg->transm_command = CMD_ACK;

        if (stat != ST_TRANSMITTED)
            return;
        else
            str++;
    }
}
#endif
#ifdef TARGET_UARM
#define termprint(str) tprint(str);
#endif

char *toprint[] = {
    "1                        \n",  "2          _nnnn_        \n",  "3         dGGGGMMb       \n",
    "4        @p~qp~~qMb      \n",  "5        M|@||@) M|      \n",  "6        @,----.JM|      \n",
    "7       JS^\\__/  qKL     \n", "8      dZP        qKRb   \n",  "9     dZP          qKKb  \n",
    "10   fZP            SMMb \n",  "11   HZM            MMMM \n",  "12   FqM            MMMM \n",
    "13 __| '.        |\\dS'qML\n", "14 |    `.       | `' \\Zq\n", "15_)      \\.___.,|     .'\n",
    "16\\____   )MMMMMP|   .'  \n", "17     `-'       `--'    \n",  "18                       \n",
};

void test1() {
    int i = 0;
    termprint("Entering test1!\n");
    for (i = 0; i < STEPS; i++) {
        while (test3_baton[i] == 0)
            ;

        termprint(toprint[i * 3]);
        delay_ms(100);
        test1_baton[i] = 1;
    }
    while (test3_baton[STEPS] == 0)
        ;
    termprint("Good job from test1\n");
    test1_baton[STEPS] = 1;
    SYSCALL(SYS3, 0, 0, 0);
}

void test2() {
    int i = 0;
    termprint("Entering test2!\n");


    for (i = 0; i < STEPS; i++) {
        while (test1_baton[i] == 0)
            ;

        termprint(toprint[i * 3 + 1]);
        delay_ms(100);
        test2_baton[i] = 1;
    }
    while (test1_baton[STEPS] == 0)
        ;
    termprint("Good job from test2\n");
    test2_baton[STEPS] = 1;
    SYSCALL(SYS3, 0, 0, 0);
}

void test3() {
    int i = 0;
    termprint("Entering test3!\n");

    test3_baton[0] = 1;
    for (i = 0; i < STEPS; i++) {
        while (test2_baton[i] == 0)
            ;

        termprint(toprint[i * 3 + 2]);
        delay_ms(100);
        test3_baton[i + 1] = 1;
    }
    while (test2_baton[STEPS] == 0)
        ;

    termprint("Good job from test3\n");
    SYSCALL(SYS3, 0, 0, 0);
}

#define N_WRITE_PROC 3
struct list_head ready_queue;
pcb_t* writeProcess[N_WRITE_PROC];
memaddr writerFunc[N_WRITE_PROC] = { (memaddr)test1, (memaddr)test2, (memaddr)test3 };

// Questo va sistemato
process_option writer_opt = { ENABLE_INTERRUPT, KERNEL_MD_ON, 0, ALL_INTRRPT_ENABLED, VIRT_MEM_OFF, 0, TIMER_ENABLED };

//TODO REMOVE dovrebbero essere tutti del tipo void handler(void)
void tmpHander() {
    termprint("I catched an exception, I'm the handler btw\n");
    PANIC();
}

// BiKayaOS entry point
int main(void) {
    termprint("Welcome to phase 1.5 of BiKayaOS \n");
    
    // Populate the New Areas in the ROM reserved frame
    initNewArea((memaddr)tmpHander, (memaddr)NEW_AREA_INTERRUPT);
    initNewArea((memaddr)tmpHander, (memaddr)NEW_AREA_TLB);
    initNewArea((memaddr)tmpHander, (memaddr)NEW_AREA_TRAP);
    initNewArea((memaddr)tmpHander, (memaddr)NEW_AREA_SYSCALL);

    termprint("Initialized all the new areas for exception handling\n");

    // Initializes the PCB and the ready queue
    initPcbs();
    mkEmptyProcQ(&ready_queue);
    termprint("PCB and ready queue initialized!\n");

    // Alloc 3 PCB and set's their states
    for (int i = 0; i < N_WRITE_PROC; i++) {
        writeProcess[i] = allocPcb();
        
        if (writeProcess[i] == NULL) {
            termprint("Unexpected NULL in allocPCB()\n");
            PANIC();
        }

        // Set the status registrer with the requested option
        setStatusReg(&writeProcess[i]->p_s, &writer_opt);
        // Set Stack Pointer to a free memory location
        setStackP(&writeProcess[i]->p_s, (memaddr)(RAMTOP-(RAM_FRAMESIZE*(i+1))));
        // Give the process an arbitrary priority
        writeProcess[i]->priority = i+1;
        // Sets the Program Counter to the entry point of the function
        setPC(&writeProcess[i]->p_s, writerFunc[i]);

        insertProcQ(&ready_queue, writeProcess[i]);
    }

    termprint("Created 3 process, set status register and stack pointer,\n");
    termprint("give 'em priority and set their PC.\n");
    termprint("Also added to the ready queue\n");
}