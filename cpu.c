
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cpu.h"

#define REG_COUNT 16


int memory_map[16384];
char instruction_set[MAX_INSTRUCTION_COUNT][MAX_INSTRUCTION_LENGTH];
int instruction_count = 0;
int program_counter = 0;
int stalled_cycles = 0;
bool stall = false;
bool break_loop = false;


decoded_instruction curr_id_struct_if, curr_id_struct_decode, curr_id_struct_ia, curr_id_struct_rr, curr_id_struct_add, curr_id_struct_mul, curr_id_struct_div, 
    curr_id_struct_br, curr_id_struct_mem1, curr_id_struct_mem2, curr_id_struct_wb;
decoded_instruction prev_id_struct_if, prev_id_struct_decode, prev_id_struct_ia, prev_id_struct_rr, prev_id_struct_add, prev_id_struct_mul, prev_id_struct_div, 
    prev_id_struct_br, prev_id_struct_mem1, prev_id_struct_mem2, prev_id_struct_wb;


CPU *CPU_init() {
    CPU *cpu = malloc(sizeof(*cpu));
    if (!cpu) {
        return NULL;
    }

    /* Create register files */
    cpu->regs = create_registers(REG_COUNT);

    return cpu;
}

/*
 * This function de-allocates CPU cpu.
 */
void CPU_stop(CPU *cpu) {
    free(cpu);
}

/*
 * This function prints the content of the registers.
 */
void
print_registers(CPU *cpu){
    
    
    printf("================================\n\n");

    printf("=============== STATE OF ARCHITECTURAL REGISTER FILE ==========\n\n");

    printf("--------------------------------\n");
    for (int reg=0; reg<REG_COUNT; reg++) {
        printf("REG[%2d]   |   Value=%d  \n",reg,cpu->regs[reg].value);
        printf("--------------------------------\n");
    }
    printf("================================\n\n");
}

void print_display(CPU *cpu, int cycle){
    printf("================================\n");
    printf("Clock Cycle #: %d\n", cycle);
    printf("--------------------------------\n");

   for (int reg=0; reg<REG_COUNT; reg++) {
       
        printf("REG[%2d]   |   Value=%d  \n",reg,cpu->regs[reg].value);
        printf("--------------------------------\n");
    }
    printf("================================\n");
    printf("\n");

}

/*
 *  CPU CPU simulation loop
 */
int CPU_run(CPU *cpu, char* filename) {
    int wb_value;

    cpu->clock_cycle = 0;
    read_memory_map();
    read_instruction_file(filename);

    for (;;) {
        cpu->clock_cycle++;

        instrcution_fetch();
        instrcution_decode();
        instrcution_analyze();
        register_read(cpu);
        add_stage(cpu);
        multiplier_stage();
        divition_stage();
        branch();
        memory_1();
        memory_2(cpu);
        write_back(cpu);

        if (break_loop) {
            break;
        }
    }
    print_registers(cpu);

    printf("Stalled cycles due to structural hazard: %d\n", stalled_cycles);
    printf("Total execution cycles: %d\n", cpu->clock_cycle);
    printf("Total instruction simulated: %d\n", instruction_count);
    printf("IPC: %.6f\n", (float)instruction_count/(float)(cpu->clock_cycle));
    return 0;
}

Register *
create_registers(int size) {
    Register *regs = malloc(sizeof(*regs) * size);
    if (!regs) {
        return NULL;
    }
    for (int i = 0; i < size; i++) {
        regs[i].value = 0;
        regs[i].is_writing = false;
    }
    return regs;
}

int read_memory_map() {
    FILE *memory_fd = fopen("./memory_map.txt", "r");
    char val[10];

    for (int i = 0; i < 16384; i++) {
        fscanf(memory_fd, "%s", val);
        memory_map[i] = atoi(val);
    };
    return 0;
}

int read_instruction_file(char* filename) {
    char base_dir[250] = "./programs/programs/";
    strcat(base_dir, filename);
    
    FILE *instruction_file = fopen(base_dir, "r");

    while (fgets(instruction_set[instruction_count], MAX_INSTRUCTION_LENGTH, instruction_file)) {
        instruction_set[instruction_count][strlen(instruction_set[instruction_count]) - 1] = '\0';
        instruction_count++;
    }
    
    return 0;
}

int instrcution_fetch() {

    stall = false;
    prev_id_struct_if = curr_id_struct_if;

    if (program_counter < instruction_count) {

        if (strlen(curr_id_struct_mem1.instruction) != 0) {
            if (strcmp(curr_id_struct_mem1.opcode, "ld") == 0) {
                stall = true;
                stalled_cycles++;
            }
        }

        if (!stall){
            strcpy(curr_id_struct_if.instruction, instruction_set[program_counter]);
            program_counter++;
            // printf("IF                       : %s\n", curr_id_struct_if.instruction);
        }
        else{
            strcpy(curr_id_struct_if.instruction, "");
        }
    }

    return 0;
}

int instrcution_decode() {
    char instruction[MAX_INSTRUCTION_LENGTH];
    char *decoded_instruction[5];
    char *tokenised = NULL;
    int counter = 0;

    prev_id_struct_decode = curr_id_struct_decode;
    
    if (strcmp(prev_id_struct_decode.opcode, "ret") == 0){
        return 0;
    }

    curr_id_struct_decode = prev_id_struct_if;

    // printf("%s\n", prev_id_struct_if.opcode);
    if (strlen(prev_id_struct_if.instruction) != 0) {
        strcpy(instruction, curr_id_struct_decode.instruction);
        tokenised = strtok(instruction, " ");

        while (tokenised != NULL) {
            decoded_instruction[counter++] = tokenised;
            tokenised = strtok(NULL, " ");
        }

        strcpy(curr_id_struct_decode.opcode, decoded_instruction[1]);

        strcpy(curr_id_struct_decode.register_addr, decoded_instruction[2]);

        strcpy(curr_id_struct_decode.operand_1, decoded_instruction[3]);

        if (counter == 5){
            strcpy(curr_id_struct_decode.operand_2, decoded_instruction[4]);
        }

        curr_id_struct_decode.num_var = counter;
        // printf("ID                       : %s %s %s %s\n", curr_id_struct_decode.instruction, curr_id_struct_decode.opcode, curr_id_struct_decode.operand_1, curr_id_struct_decode.operand_2);
    }
    else {
        strcpy(curr_id_struct_decode.instruction, "");
    }

    return 0;
}

int instrcution_analyze() {     // skipping implementation

    prev_id_struct_ia = curr_id_struct_ia;
        
    if (strcmp(prev_id_struct_ia.opcode, "ret") == 0){
        return 0;
    }
    
    curr_id_struct_ia = prev_id_struct_decode;

    if (strlen(prev_id_struct_decode.instruction) != 0) {
        // printf("IA                       : %s\n", curr_id_struct_ia.instruction);
    }
    else {
        strcpy(curr_id_struct_ia.instruction, "");
    }

    return 0;
}

int register_read(CPU *cpu) {
    int values[2] = {0, 0};
    char operands[2][6];
    int addr;
    char *register_address;

    prev_id_struct_rr = curr_id_struct_rr;    
    if (strcmp(prev_id_struct_rr.opcode, "ret") == 0){
        return 0;
    }

    curr_id_struct_rr = prev_id_struct_ia;

    if (strlen(prev_id_struct_ia.instruction) != 0) {
        if (strcmp(curr_id_struct_rr.opcode, "ret") != 0) {

            // could be register, address or number
            strcpy(operands[0], curr_id_struct_rr.operand_1);
            strcpy(operands[1], curr_id_struct_rr.operand_2);

            for (int i = 3; i < curr_id_struct_rr.num_var; i++) {
                register_address = operands[i - 3];
                addr = atoi(register_address);

                if (strstr(operands[i - 3], "#")) {
                    register_address += 1;
                    addr = atoi(register_address);
                    if (addr > 999) {
                        values[i - 3] = memory_map[(addr)/4];   // Read from memory map file
                    }
                    else {
                        values[i - 3] = addr;   //use the value given
                    }
                }
                else {
                    register_address += 1;
                    addr = atoi(register_address);
                    if (!((cpu->regs[addr]).is_writing)) {
                        values[i - 3] = cpu->regs[addr].value;
                    }
                }
            }
            curr_id_struct_rr.value_1 = values[0];
            curr_id_struct_rr.value_2 = values[1];
        }
        // printf("RR                       : %s %d %d %d\n", curr_id_struct_rr.instruction, addr, curr_id_struct_rr.value_1, curr_id_struct_rr.value_2);
    }
    else {
        strcpy(curr_id_struct_rr.instruction, "");
    }

    return 0;
}

int add_stage(CPU *cpu) {
    int addr;
    char *register_address;
    int value;

    prev_id_struct_add = curr_id_struct_add;
        
    if (strcmp(prev_id_struct_add.opcode, "ret") == 0){
        return 0;
    }

    curr_id_struct_add = prev_id_struct_rr;

    if (strlen(prev_id_struct_rr.instruction) != 0) {
        if (strcmp(curr_id_struct_add.opcode, "add") == 0) {
            curr_id_struct_add.wb_value = curr_id_struct_add.value_1 + curr_id_struct_add.value_2;
        }
        else if (strcmp(curr_id_struct_add.opcode, "sub") == 0) {
            curr_id_struct_add.wb_value = curr_id_struct_add.value_1 - curr_id_struct_add.value_2;
        }
        else if (strcmp(curr_id_struct_add.opcode, "set") == 0) {
            register_address = curr_id_struct_add.operand_1;

            if (strstr(curr_id_struct_add.operand_1, "#")) {
                register_address += 1;
                addr = atoi(register_address);
                if (addr > 999) {
                    value = memory_map[addr/4];   // Read from memory map file
                }
                else {
                    value = addr;   //use the value given
                }
            }
            else {
                    register_address += 1;
                    addr = atoi(register_address);
                if (!((cpu->regs[addr]).is_writing)) {
                    value = cpu->regs[addr].value;
                }
            }
            curr_id_struct_add.wb_value = value;
        }

        // printf("ADD                      : %s %d\n", curr_id_struct_add.instruction, curr_id_struct_add.wb_value);
    }
    else {
        strcpy(curr_id_struct_add.instruction, "");
    }
    return 0;
}

int multiplier_stage() {
    prev_id_struct_mul = curr_id_struct_mul;
        
    if (strcmp(prev_id_struct_mul.opcode, "ret") == 0){
        return 0;
    }

    curr_id_struct_mul = prev_id_struct_add;

    if (strlen(prev_id_struct_add.instruction) != 0) {
        if (strcmp(curr_id_struct_mul.opcode, "mul") == 0) {
            curr_id_struct_mul.wb_value = curr_id_struct_mul.value_1 * curr_id_struct_mul.value_2;
        }

        // printf("MUL                      : %s %d\n", curr_id_struct_mul.instruction, curr_id_struct_mul.wb_value);
    }
    else {
        strcpy(curr_id_struct_mul.instruction, "");
    }

    return 0;
}

int divition_stage() {
    prev_id_struct_div = curr_id_struct_div;
        
    if (strcmp(prev_id_struct_div.opcode, "ret") == 0){
        return 0;
    }

    curr_id_struct_div = prev_id_struct_mul;

    if (strlen(prev_id_struct_mul.instruction) != 0) {
        if (strcmp(curr_id_struct_div.opcode, "div") == 0) {
            curr_id_struct_div.wb_value = curr_id_struct_div.value_1 / curr_id_struct_div.value_2;
        }
        // printf("DIV                      : %s %d\n", curr_id_struct_div.instruction, curr_id_struct_add.wb_value);
    }
    else {
        strcpy(curr_id_struct_div.instruction, "");
    }

    return 0;
}

int branch() {
    prev_id_struct_br = curr_id_struct_br;

    if (strcmp(prev_id_struct_br.opcode, "ret") == 0){
        return 0;
    }

    curr_id_struct_br = prev_id_struct_div;

    if (strlen(prev_id_struct_div.instruction) != 0) {
        // printf("BR                       : %s\n", curr_id_struct_br.instruction);
    }
    else {
        strcpy(curr_id_struct_br.instruction, "");
    }

    return 0;
}

int memory_1() {
    prev_id_struct_mem1 = curr_id_struct_mem1;
        
    if (strcmp(prev_id_struct_mem1.opcode, "ret") == 0){
        return 0;
    }

    curr_id_struct_mem1 = prev_id_struct_br;

    if (strlen(prev_id_struct_br.instruction) != 0) {
        // printf("Mem1                     : %s\n", curr_id_struct_mem1.instruction);
    }
    else {
        strcpy(curr_id_struct_mem1.instruction, "");
    }

    return 0;
}

int memory_2(CPU *cpu) {
    int addr;
    char *register_address;
    int value;
    prev_id_struct_mem2 = curr_id_struct_mem2;
        
    if (strcmp(prev_id_struct_mem2.opcode, "ret") == 0){
        return 0;
    }

    curr_id_struct_mem2 = prev_id_struct_mem1;

    if (strlen(prev_id_struct_mem1.instruction) != 0) {
        // printf("Mem2                     : %s %d %d\n", curr_id_struct_mem2.instruction, curr_id_struct_mem2.wb_value, curr_id_struct_mem2.value_1);
        if (strcmp(curr_id_struct_mem2.opcode, "ld") == 0) {
            register_address = curr_id_struct_mem2.operand_1;

            if (strstr(curr_id_struct_mem2.operand_1, "#")) {
                register_address += 1;
                addr = atoi(register_address);
                if (addr > 999) {
                    value = memory_map[addr/4];   // Read from memory map file
                }
                else {
                    value = addr;   //use the value given
                }
            }
            else {
                    register_address += 1;
                    addr = atoi(register_address);
                if (!((cpu->regs[addr]).is_writing)) {
                    value = memory_map[cpu->regs[addr].value/4];
                }
            }
            curr_id_struct_mem2.wb_value = value;
        }
        // printf("Mem2                     : %s %d %d\n", curr_id_struct_mem2.instruction, curr_id_struct_mem2.wb_value, addr);
    }
    else {
        strcpy(curr_id_struct_mem2.instruction, "");
    }

    return 0;
}

int write_back(CPU *cpu) {
    char *register_addr;
    prev_id_struct_wb = curr_id_struct_wb;
    curr_id_struct_wb = prev_id_struct_mem2;

    if (strlen(prev_id_struct_mem2.instruction) != 0) {
        if (strcmp(curr_id_struct_wb.opcode, "ret") == 0) {
            break_loop = true;
        }
        else {
            register_addr = curr_id_struct_wb.register_addr;
            if (strchr(curr_id_struct_wb.register_addr, 'R') != NULL) {
                register_addr += 1;
                if (!cpu->regs[atoi(register_addr)].is_writing){
                    cpu->regs[atoi(register_addr)].is_writing = true;
                    cpu->regs[atoi(register_addr)].value = curr_id_struct_wb.wb_value;
                }
                cpu->regs[atoi(register_addr)].is_writing = false;
            }
        }
        // printf("WB                       : %s %d\n", curr_id_struct_wb.instruction, curr_id_struct_wb.wb_value);
    }
    else {
        strcpy(curr_id_struct_wb.instruction, "");
    }

    return 0;
}
