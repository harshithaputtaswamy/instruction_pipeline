#ifndef _CPU_H_
#define _CPU_H_
#include <stdbool.h>
#include <assert.h>

#define MAX_INSTRUCTION_LENGTH 500
#define MAX_INSTRUCTION_COUNT 1000

typedef struct Register {
    int value;       // contains register value
    bool is_writing; // indicate that the register is current being written
                     // True: register is not ready
                     // False: register is ready
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

int instrcution_fetch(CPU *cpu);

int instrcution_decode(CPU *cpu);

int instrcution_analyze(CPU *cpu);

int register_read(CPU *cpu);

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
