#ifndef _CPU_H_
#define _CPU_H_
#include <stdbool.h>
#include <assert.h>

#define MAX_INSTRUCTION_LENGTH 500
#define MAX_INSTRUCTION_COUNT 1000

#define ROB_SIZE 8
#define RS_SIZE 4

typedef struct Register {
    int value;       // contains register value
    bool is_writing; // indicate that the register is current being written
                     // True: register is not ready
                     // False: register is ready
    int is_valid; 
    int tag;
} Register;


typedef struct BTB {
    int tag;
    int target;
} BTB;


typedef struct PredictionTable {
    int pattern;
} PredictionTable;

/* Model of CPU */
typedef struct CPU {
    /* Integer register file */
    Register *regs;
    BTB *btb;
    PredictionTable *predict_tb;
    int clock_cycle;
} CPU;

typedef struct decoded_instruction {
    char instruction[MAX_INSTRUCTION_LENGTH];
    int addr;
    char opcode[5];
    char register_addr[6];
    char operand_1[6];
    char operand_2[6];
    int num_var;
    int value_1;
    int value_2;
    int wb_value;
    bool dependency;

} decoded_instruction;


/* Reservation station queue structure declarations */

typedef struct rs_queue_struct {
    decoded_instruction instructions[RS_SIZE];
    int front;
    int rear;
} rs_queue_struct;

/* end of Reservation station queue structure declarations */


/* ROB structure declation */

typedef struct rob_struct {
    int dest;
    int result;
    int e;
    int completed;
} rob_struct;

typedef struct rob_queue_struct {
    struct rob_struct buffer[ROB_SIZE];
    int front;
    int rear;
} rob_queue_struct;

/* end of ROB structure declation */


CPU *CPU_init();

Register *
create_registers(int size);

BTB *create_btb(int size);

PredictionTable *create_predict_tb(int size);

int CPU_run(CPU *cpu, char *);

void CPU_stop(CPU *cpu);

int read_memory_map();

int read_instruction_file(char*);

int write_memory_map(char*);

int instruction_fetch(CPU *cpu);

int instruction_decode(CPU *cpu);

int instruction_analyze(CPU *cpu);

int register_read(CPU *cpu);

int instruction_issue(CPU *cpu);

int add_stage(CPU *cpu);

int multiplier_stage(CPU *cpu);

int divition_stage(CPU *cpu);

int branch(CPU *cpu);

int memory_1(CPU *cpu);

int memory_2(CPU *cpu);

int write_back(CPU *cpu);

int enqueue(char*);

int dequeue();

char* search_queue(char*);

#endif
