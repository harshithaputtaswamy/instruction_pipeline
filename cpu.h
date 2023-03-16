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

/* Model of CPU */
typedef struct CPU {
    /* Integer register file */
    Register *regs;
    int clock_cycle;
} CPU;

typedef struct decoded_instruction {
    char instruction[MAX_INSTRUCTION_LENGTH];
    char opcode[4];
    char register_addr[6];
    char operand_1[6];
    char operand_2[6];
    int num_var;
    int value_1;
    int value_2;
    int wb_value;

} decoded_instruction;


CPU *CPU_init();

Register *
create_registers(int size);

int CPU_run(CPU *cpu, char *);

void CPU_stop(CPU *cpu);

int read_memory_map();

int read_instruction_file(char*);

int instrcution_fetch();

int instrcution_decode();

int instrcution_analyze();

int register_read(CPU *cpu);

int add_stage(CPU *cpu);

int multiplier_stage();

int divition_stage();

int branch();

int memory_1();

int memory_2(CPU *cpu);

int write_back(CPU *cpu);

#endif
