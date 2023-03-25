
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cpu.h"

#define REG_COUNT 16
#define MAX_REGISTER_IN_USE 1000


char ip_filename[50];
int memory_map[16384];
char instruction_set[MAX_INSTRUCTION_COUNT][MAX_INSTRUCTION_LENGTH];
int instruction_count = 0;
int program_counter = 0;
int stalled_cycles = 0;
bool stall = false;
bool break_loop = false;
char register_in_use[MAX_REGISTER_IN_USE][3];       //initialize a queue to store registers in use
int front, rear;


decoded_instruction curr_id_struct_if, curr_id_struct_decode, curr_id_struct_ia, curr_id_struct_rr, curr_id_struct_add, curr_id_struct_mul, curr_id_struct_div, 
    curr_id_struct_br, curr_id_struct_mem1, curr_id_struct_mem2, curr_id_struct_wb;
decoded_instruction prev_id_struct_if, prev_id_struct_decode, prev_id_struct_ia, prev_id_struct_rr, prev_id_struct_add, prev_id_struct_mul, prev_id_struct_div, 
    prev_id_struct_br, prev_id_struct_mem1, prev_id_struct_mem2, prev_id_struct_wb;


int enqueue(char *reg) {
    if (rear == MAX_REGISTER_IN_USE - 1) {
        return -1;
    }
    else {
        rear = rear + 1;
        strcpy(register_in_use[rear], reg);
        return 1;
    }
}


int dequeue() {
    if (front == rear) {
        return -1;
    }
    else {
        front++;
        return 1;
    }
}


char* search_queue(char *reg) {
    for (int i = front; i <= rear; i++) {
        if (strcmp(register_in_use[i], reg) == 0) {
            return register_in_use[i];
        }
    }
    return NULL;
}


CPU *CPU_init() {
    CPU *cpu = malloc(sizeof(*cpu));
    if (!cpu) {
        return NULL;
    }

    /* Create register files */
    cpu->regs = create_registers(REG_COUNT);

    front = rear = -1;
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
    for (int i = 0; i < 16384; i++) {
        printf("%d ", memory_map[i]);
    }
}


void print_display(CPU *cpu, int cycle){
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
        printf("================================\n");
        printf("Clock Cycle #: %d\n", cpu->clock_cycle);
        printf("--------------------------------\n");
        printf("stall value %d\n", stall);

        instrcution_fetch();
        instrcution_decode();
        instrcution_analyze(cpu);
        register_read(cpu);
        add_stage(cpu);
        multiplier_stage(cpu);
        divition_stage(cpu);
        branch(cpu);
        memory_1(cpu);
        memory_2(cpu);
        write_back(cpu);

        print_display(cpu, cpu->clock_cycle);

        if (break_loop) {
            break;
        }
    }

    FILE *fp = fopen("output_memory_3.txt", "w");
    char c[100];
    for (int i = 0; i < 16384; i++) {
        sprintf(c, "%d", memory_map[i]);
        fprintf(fp, "%s ", c);
    }
    fclose(fp);

    print_registers(cpu);
    printf("Stalled cycles due to data hazard: %d\n", stalled_cycles);
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
    strcpy(ip_filename, filename);
    char base_dir[250] = "./programs/";
    strcat(base_dir, filename);
    
    FILE *instruction_file = fopen(base_dir, "r");

    while (fgets(instruction_set[instruction_count], MAX_INSTRUCTION_LENGTH, instruction_file)) {
        instruction_set[instruction_count][strlen(instruction_set[instruction_count]) - 1] = '\0';
        instruction_count++;
    }
    
    return 0;
}


int instrcution_fetch() {

    // stall = false;
    prev_id_struct_if = curr_id_struct_if;

    if (program_counter < instruction_count) {

        // if (strlen(curr_id_struct_mem1.instruction) != 0) {      // condition to induce structural hazard
        //     if (strcmp(curr_id_struct_mem1.opcode, "ld") == 0) {
        //         stall = true;
        //         stalled_cycles++;
        //     }
        // }
        printf("program_counter %d\n", program_counter);
        if (!stall) {       // TODO: remove program_counter condition
            strcpy(curr_id_struct_if.instruction, instruction_set[program_counter]);
            program_counter++;
        }
        else {
            strcpy(curr_id_struct_if.instruction, prev_id_struct_if.instruction);
        }
        printf("                                                IF             : %s\n", curr_id_struct_if.instruction);
    }

    return 0;
}


int instrcution_decode() {
    char instruction[MAX_INSTRUCTION_LENGTH];
    char *decoded_instruction[5];
    char *tokenised = NULL;
    int counter = 0;

    prev_id_struct_decode = curr_id_struct_decode;

    if (strcmp(prev_id_struct_decode.opcode, "ret") == 0) {
        return 0;
    }

    // printf("%s\n", prev_id_struct_if.opcode);
    if (strlen(prev_id_struct_if.instruction) != 0 && !stall) {
        curr_id_struct_decode = prev_id_struct_if;
        strcpy(instruction, curr_id_struct_decode.instruction);
        tokenised = strtok(instruction, " ");

        while (tokenised != NULL) {
            decoded_instruction[counter++] = tokenised;
            tokenised = strtok(NULL, " ");
        }

        strcpy(curr_id_struct_decode.opcode, decoded_instruction[1]);

        strcpy(curr_id_struct_decode.register_addr, decoded_instruction[2]);

        strcpy(curr_id_struct_decode.operand_1, decoded_instruction[3]);

        if (counter == 5) {
            strcpy(curr_id_struct_decode.operand_2, decoded_instruction[4]);
        }

        curr_id_struct_decode.num_var = counter;

        curr_id_struct_decode.dependency = false;

    }
    // else if (stall) {
    //     strcpy(curr_id_struct_decode.instruction, prev_id_struct_decode.instruction);
    // }
        printf("                                                ID             : %s %s %s %s\n", curr_id_struct_decode.instruction, curr_id_struct_decode.opcode, curr_id_struct_decode.operand_1, curr_id_struct_decode.operand_2);

    return 0;
}


int instrcution_analyze(CPU *cpu) {
    bool local_stall = false;
    int addr;
    char *register_address;

    prev_id_struct_ia = curr_id_struct_ia;

    if (strcmp(prev_id_struct_ia.opcode, "ret") == 0) {
        return 0;
    }

    if (strlen(prev_id_struct_decode.instruction) != 0 && !stall) {
        curr_id_struct_ia = prev_id_struct_decode;

        if (strcmp(curr_id_struct_ia.opcode, "set") == 0 || strcmp(curr_id_struct_ia.opcode, "ld") == 0 || strcmp(curr_id_struct_ia.opcode, "st") == 0) {
            register_address = curr_id_struct_ia.register_addr;
            register_address++;
            addr = atoi(register_address);
            if (cpu->regs[addr].is_writing) {
                local_stall = true;
            }
            // else {
            //     cpu->regs[addr].is_writing = true;
            // }
            // printf("is writing\n %d %d\n", addr, cpu->regs[addr].is_writing);
        }
        else {
            register_address = curr_id_struct_ia.operand_1;
            if (strstr(register_address, "R")){
                register_address++;
                addr = atoi(register_address);
                if (cpu->regs[addr].is_writing) {
                    local_stall = true;
                }
                // else {
                //     cpu->regs[addr].is_writing = true;
                // }
                // printf("is writing\n %d %d\n", addr, cpu->regs[addr].is_writing);
            }

            if (curr_id_struct_ia.num_var == 5 && strstr(curr_id_struct_ia.operand_2, "R")) {
                register_address = curr_id_struct_ia.operand_2;
                // if (strstr(register_address, "R")){
                    register_address++;
                    addr = atoi(register_address);
                    if (cpu->regs[addr].is_writing) {
                        local_stall = true;
                    }
                    // else {
                    //     cpu->regs[addr].is_writing = true;
                    // }
                    // printf("is writing\n %d %d\n", addr, cpu->regs[addr].is_writing);
                // }
            }
        }
        printf("local stall %d\n", local_stall);

        printf("Register %d\n", atoi(register_address));
        printf("Register %d\n", cpu->regs[atoi(register_address)].is_writing);
    }

    printf("$$$$$$$$$$$$$%s\n", curr_id_struct_div.instruction);
    
    if (strlen(curr_id_struct_div.instruction) != 0) {
        if (strcmp(curr_id_struct_div.opcode, "st") != 0) {
            register_address = curr_id_struct_div.register_addr;
            register_address++;
            addr = atoi(register_address);
            cpu->regs[addr].is_writing = true;      // set register addr is_writing to true for instruction in br stage
        }
    }
    printf("                                                IA             : %s %d\n", curr_id_struct_ia.instruction, curr_id_struct_ia.dependency);

    return 0;
}


int register_read(CPU *cpu) {
    int values[2] = {0, 0};
    char operands[2][6];
    int addr;
    char *register_address;
    bool local_stall = false;

    prev_id_struct_rr = curr_id_struct_rr;    
    if (strcmp(prev_id_struct_rr.opcode, "ret") == 0) {
        return 0;
    }

    if (strlen(prev_id_struct_ia.instruction) != 0) {

        if (!stall) {
            curr_id_struct_rr = prev_id_struct_ia;
        }

        printf("***%s %s\n", prev_id_struct_ia.operand_2, curr_id_struct_div.register_addr);
        printf("***%s\n", curr_id_struct_div.instruction);

        // stall if the register addr is same as operand 1 or 2 in add stage and the instruction is not add
        if (strcmp(curr_id_struct_rr.opcode, "st") != 0) {
            
            if (strlen(prev_id_struct_rr.instruction) && strcmp(prev_id_struct_rr.opcode, "st") != 0) {

                if (strcmp(prev_id_struct_rr.opcode, "add") != 0 && strcmp(prev_id_struct_rr.opcode, "sub") != 0 && strcmp(prev_id_struct_rr.opcode, "set") != 0) {

                    printf("curr_id_struct_rr %s\n", curr_id_struct_rr.instruction);
                    printf("prev_id_struct_rr %s\n", prev_id_struct_rr.instruction);
                    if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, prev_id_struct_rr.register_addr) == 0) {
                        local_stall = true;
                    }
                    printf("#### 00 local stall %d\n", local_stall);
                    if (strcmp(curr_id_struct_rr.operand_1, prev_id_struct_rr.register_addr) == 0) {
                        local_stall = true;
                    }
                    printf("strcmp(prev_id_struct_rr.opcode, add) != 0 | %d\n", (strcmp(prev_id_struct_rr.opcode, "set") != 0));
                    // if (local_stall && (strcmp(prev_id_struct_rr.opcode, "add") != 0 && strcmp(prev_id_struct_rr.opcode, "sub") != 0 && strcmp(prev_id_struct_rr.opcode, "set") != 0)) {
                    //     local_stall = true;
                    //     stall = true;
                    // }
                    // else {
                    //     local_stall = false;
                    // }
                }
                printf("#### 10 local stall %d\n", local_stall);
            }
            
            // stall if the register addr is same as operand 1 or 2 in mul stage
            printf("\nprev_id_struct_rr %s %d\n", prev_id_struct_rr.instruction, prev_id_struct_rr.dependency);
            printf("curr_id_struct_rr %s %d\n", curr_id_struct_rr.instruction, curr_id_struct_rr.dependency);
            printf("prev_id_struct_add %s %d\n\n", prev_id_struct_add.instruction, prev_id_struct_add.dependency);
            printf("curr_id_struct_add %s %d\n", curr_id_struct_add.instruction, curr_id_struct_add.dependency);
            printf("------------------ %d\n", (strlen(prev_id_struct_rr.instruction) == 0 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_rr.operand_1) == 0));
            printf("++++++++++++++++++ %d\n", (strlen(prev_id_struct_rr.instruction) == 0 && strcmp(curr_id_struct_add.opcode, "mul") != 0));

            if (strlen(curr_id_struct_add.instruction) && strcmp(curr_id_struct_add.opcode, "st") != 0) {

                if ((prev_id_struct_rr.dependency == 1 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_rr.operand_1) == 0) || (prev_id_struct_rr.dependency == 1 && strcmp(curr_id_struct_add.opcode, "mul") != 0)
                || (strcmp(curr_id_struct_rr.operand_2, curr_id_struct_rr.operand_1) && strcmp(curr_id_struct_add.opcode, "mul"))) { 

                    if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_add.register_addr) == 0) {
                        stall = true;
                        local_stall = true;
                    }
                    printf("#### 01 local stall %d\n", local_stall);
                    if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_add.register_addr) == 0) {
                        stall = true;
                        local_stall = true;
                    }

                    // if (local_stall && (strcmp(curr_id_struct_add.opcode, "mul") || strlen(prev_id_struct_rr.instruction) == 0)
                    // && (strcmp(curr_id_struct_rr.operand_2, curr_id_struct_rr.operand_1))) {      // RIGHT UNTIL HERE,    // Donot remove this line
                    //     local_stall = true;
                    //     stall = true;
                    // }
                    // else {
                    //     local_stall = false;
                    // }
                    printf("#### 11 local stall %d\n", local_stall);
                }
            }

            // stall if the register addr is same as operand 1 or 2 in div stage
            if (strlen(curr_id_struct_mul.instruction) && strcmp(curr_id_struct_mul.opcode, "st") != 0) {

                if ((prev_id_struct_rr.dependency == 1 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_rr.operand_1) == 0) || (prev_id_struct_rr.dependency == 1 && strcmp(curr_id_struct_mul.opcode, "div") != 0 )
                || (strcmp(curr_id_struct_rr.operand_2, curr_id_struct_rr.operand_1) && strcmp(curr_id_struct_mul.opcode, "div"))) {

                    if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_mul.register_addr) == 0) {
                        stall = true;
                        local_stall = true;
                    }
                    printf("#### 02 local stall %d\n", local_stall);
                    if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_mul.register_addr) == 0) {
                        stall = true;
                        local_stall = true;
                    }
                    
                    printf("+++++++++++++++prev_id_struct_rr %s\n", prev_id_struct_rr.instruction);

                    // if (local_stall && (strcmp(curr_id_struct_mul.opcode, "div") || strlen(prev_id_struct_rr.instruction) == 0) 
                    // && (strcmp(curr_id_struct_rr.operand_2, curr_id_struct_rr.operand_1))) {
                    //     local_stall = true;
                    //     stall = true;
                    // }
                    // else {
                    //     local_stall = false;
                    // }
                    printf("#### 12 local stall %d\n", local_stall);
                }
            }

            // stall if the register addr is same as operand 1 or 2 in br stage
            if (strlen(curr_id_struct_div.instruction) && strcmp(curr_id_struct_div.opcode, "st") != 0) {

                if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_div.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }
                printf("#### 03 local stall %d\n", local_stall);
                if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_div.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }
                printf("#### 13 local stall %d\n", local_stall);
            }

            if (strlen(curr_id_struct_br.instruction) && strcmp(curr_id_struct_br.opcode, "st") != 0) {
                // stall if the register addr is same as operand 1 or 2 in mem1 stage
                if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_br.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }
                printf("#### 04 local stall %d\n", local_stall);
                if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_br.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }
                printf("#### 14 local stall %d\n", local_stall);
            }

            if (strlen(curr_id_struct_mem1.instruction) && strcmp(curr_id_struct_mem1.opcode, "st") != 0) {
                // stall if the register addr is same as operand 1 or 2 in mem2 stage
                if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_mem1.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }
                printf("#### 05 local stall %d\n", local_stall);
                if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_mem1.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }
                printf("#### 15 local stall %d\n", local_stall);
            }

            if (strlen(curr_id_struct_mem2.instruction) && strcmp(curr_id_struct_mem2.opcode, "st") != 0) {
                // stall if the register addr is same as operand 1 or 2 in wb stage
                if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_mem2.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }

                printf("#### 06 local stall %d\n", local_stall);
                if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_mem2.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }
                printf("#### 16 local stall %d\n", local_stall);
            }
        }

        printf("***%s %d\n", prev_id_struct_rr.instruction, prev_id_struct_rr.dependency);

        printf("rr stage***%s %d\n", curr_id_struct_rr.instruction, curr_id_struct_rr.dependency);
        printf("rr prev stage***%s %d\n", prev_id_struct_rr.instruction, prev_id_struct_rr.dependency);

        // if (curr_id_struct_rr.num_var == 5 && strstr(curr_id_struct_rr.operand_2, "R")) {
        //     register_address = curr_id_struct_rr.operand_2;
        //     register_address++;
        //     addr = atoi(register_address);
        //     printf("Register %d\n", atoi(register_address));
        //     printf("Register %d\n", cpu->regs[atoi(register_address)].is_writing);
        //     if (cpu->regs[addr].is_writing) {
        //         local_stall = true;
        //     }
            
        // }
        // printf("#### 2 local stall %d\n", local_stall);

        if (strcmp(curr_id_struct_rr.opcode, "ret") != 0) {

            // could be register, address or number
            strcpy(operands[0], curr_id_struct_rr.operand_1);
            strcpy(operands[1], curr_id_struct_rr.operand_2);

            for (int i = 3; i < curr_id_struct_rr.num_var; i++) {
                register_address = operands[i - 3];

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
                    
                    printf("Register %d\n", atoi(register_address));
                    printf("Register %d\n", cpu->regs[atoi(register_address)].is_writing);
                    if (cpu->regs[addr].is_writing) {
                        local_stall = true;
                    }
                    else {
                        values[i - 3] = cpu->regs[addr].value;
                    }
                }
            }
            printf("#### 3 local stall %d\n", local_stall);

            // check if the register addr in rr stage is in use
            if (strcmp(curr_id_struct_rr.opcode, "st") == 0) {
                register_address = curr_id_struct_rr.register_addr;
                register_address++;
                addr = atoi(register_address);
                printf("Register %d\n", atoi(register_address));
                printf("Register %d\n", cpu->regs[atoi(register_address)].is_writing);
                if (cpu->regs[addr].is_writing) {
                    local_stall = true;
                }
            }

            if (strstr(curr_id_struct_rr.operand_1, "R")) {
                register_address = curr_id_struct_rr.operand_1;
                register_address++;
                addr = atoi(register_address);
                printf("Register %d\n", atoi(register_address));
                printf("Register %d\n", cpu->regs[atoi(register_address)].is_writing);
                if (cpu->regs[addr].is_writing) {
                    local_stall = true;
                }
            }

            if (curr_id_struct_rr.num_var == 5 && strstr(curr_id_struct_rr.operand_2, "R")) {
                register_address = curr_id_struct_rr.operand_2;
                register_address++;
                addr = atoi(register_address);
                printf("Register %d\n", atoi(register_address));
                printf("Register %d\n", cpu->regs[atoi(register_address)].is_writing);
                if (cpu->regs[addr].is_writing) {
                    local_stall = true;
                }
                
            }

                
            // if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_rr.operand_1) != 0) {
            //     if (strstr(curr_id_struct_rr.operand_1, "R")) {
            //         register_address = curr_id_struct_rr.operand_1;
            //         register_address++;
            //         addr = atoi(register_address);
            //         if (cpu->regs[addr].is_writing) {
            //             local_stall = true;
            //         }
            //     }
            // }
            // if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_rr.operand_2) != 0) {
            //     if (strstr(curr_id_struct_rr.operand_2, "R")) {
            //         register_address = curr_id_struct_rr.operand_2;
            //         register_address++;
            //         addr = atoi(register_address);
            //         if (cpu->regs[addr].is_writing) {
            //             local_stall = true;
            //         }
            //     }
            // }
        }

        if (!local_stall) {
            curr_id_struct_rr.dependency = false;
            // prev_id_struct_rr.instruction = "";
        }
        else {
            curr_id_struct_rr.dependency = true;
        }     

        if (curr_id_struct_rr.dependency) {
            stall = true;
            stalled_cycles++;
        }
        else {
            stall = false;
            curr_id_struct_rr.value_1 = values[0];
            curr_id_struct_rr.value_2 = values[1];
        }
    }

    // printf("stalled cycle %d %d %d\n", stalled_cycles, stall, local_stall);
    printf("                                                RR             : %s %d %d %d\n", curr_id_struct_rr.instruction, curr_id_struct_rr.dependency, curr_id_struct_rr.value_1, curr_id_struct_rr.value_2);
    
    return 0;
}


int add_stage(CPU *cpu) {
    int addr;
    char *register_address;
    int value;
    bool local_stall = false;

    prev_id_struct_add = curr_id_struct_add;

    if (strcmp(prev_id_struct_add.opcode, "ret") == 0) {
        return 0;
    }

    printf("prev rr %s %d %d\n", prev_id_struct_rr.instruction, prev_id_struct_rr.dependency, strlen(prev_id_struct_rr.instruction) != 0 && !prev_id_struct_rr.dependency);
    printf("value2 value2 addd stage %d %d\n", prev_id_struct_rr.value_1, prev_id_struct_rr.value_2);

    curr_id_struct_add = prev_id_struct_rr;
    if (strlen(prev_id_struct_rr.instruction) != 0) {

        if (!prev_id_struct_rr.dependency) {

            // forwarding implementation


            printf("add stage***%s\n", curr_id_struct_add.instruction);
            printf("prev add stage***%s\n", prev_id_struct_add.instruction);
            // forwarding from mul to add
            if (strlen(prev_id_struct_add.instruction) && strcmp(prev_id_struct_add.opcode, "st") && strcmp(prev_id_struct_add.register_addr, curr_id_struct_add.register_addr)) {
                if (strcmp(prev_id_struct_add.opcode, "mul") == 0) {
                    prev_id_struct_add.wb_value = prev_id_struct_add.value_1 * prev_id_struct_add.value_2;
                }

                if (strcmp(curr_id_struct_add.operand_1, prev_id_struct_add.register_addr) == 0) {
                    curr_id_struct_add.value_1 = prev_id_struct_add.wb_value;
                }
                if (curr_id_struct_add.num_var == 5 && strcmp(curr_id_struct_add.operand_2, prev_id_struct_add.register_addr) == 0) {
                    curr_id_struct_add.value_2 = prev_id_struct_add.wb_value;
                }
            }
                printf("$$$$$$$$$$$$$$$$$curr_id_struct_add.value_1 %d\n", curr_id_struct_add.value_1);
                printf("$$$$$$$$$$$$$$$$$curr_id_struct_add.value_2 %d\n", curr_id_struct_add.value_2);


                printf("curr_id_struct_mul.wb_value %d\n", curr_id_struct_mul.wb_value);
                printf("inst curr_id_struct_mul.instr %s\n", curr_id_struct_mul.instruction);
            // forwarding from div to add
            if (strlen(curr_id_struct_mul.instruction) && strcmp(curr_id_struct_mul.opcode, "st") && strcmp(curr_id_struct_mul.register_addr, prev_id_struct_add.register_addr)) {
                if (strcmp(curr_id_struct_mul.opcode, "div") == 0) {
                    curr_id_struct_mul.wb_value = curr_id_struct_mul.value_1 / curr_id_struct_mul.value_2;
                }
                if (strcmp(curr_id_struct_add.operand_1, curr_id_struct_mul.register_addr) == 0) {
                    curr_id_struct_add.value_1 = curr_id_struct_mul.wb_value;
                }
                if (curr_id_struct_add.num_var == 5 && strcmp(curr_id_struct_add.operand_2, curr_id_struct_mul.register_addr) == 0) {
                    curr_id_struct_add.value_2 = curr_id_struct_mul.wb_value;
                }

            }


            // if (strcmp(curr_id_struct_add.operand_2, prev_id_struct_add.operand_1)) {
            //     curr_id_struct_add.value_2 = prev_id_struct_add.value_1;
            // }
            // if (strcmp(curr_id_struct_add.operand_2, prev_id_struct_add.operand_2)) {
            //     curr_id_struct_add.value_2 = prev_id_struct_add.value_2;
            // }
            printf("ADD   : %d %d\n", curr_id_struct_add.value_1, curr_id_struct_add.value_2);

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
                    // if (addr > 999) {
                    //     value = memory_map[addr/4];   // Read from memory map file
                    // }
                    // else {
                        value = addr;   //use the value given
                    // }
                }
                else {
                    register_address += 1;
                    addr = atoi(register_address);
                    if (cpu->regs[addr].is_writing) {
                        // local_stall = true;
                    }
                    else {
                        value = cpu->regs[addr].value;
                    }
                }
                curr_id_struct_add.wb_value = value;
            }
                        
            // forwarding from add to add
            if (strlen(curr_id_struct_add.instruction) && strcmp(curr_id_struct_add.opcode, "st")) {
                if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_add.register_addr) == 0) {
                    curr_id_struct_rr.value_1 = curr_id_struct_add.wb_value;
                }
                if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_add.register_addr) == 0) {
                    curr_id_struct_rr.value_2 = curr_id_struct_add.wb_value;
                }
            }
        }
        else {
            strcpy(curr_id_struct_add.instruction, "");
        }

        // // forwarding from add to add
        // if (strlen(curr_id_struct_add.instruction) && strcmp(curr_id_struct_add.opcode, "st")) {
        //     if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_add.register_addr) == 0) {
        //         curr_id_struct_rr.value_1 = curr_id_struct_add.wb_value;
        //     }
        //     if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_add.register_addr) == 0) {
        //         curr_id_struct_rr.value_2 = curr_id_struct_add.wb_value;
        //     }
        // }

        //check dependency for registers in this step, do the same for mul, div stage
        
        if ((strcmp(curr_id_struct_add.opcode, "add") == 0 || strcmp(curr_id_struct_add.opcode, "sub") == 0 || strcmp(curr_id_struct_add.opcode, "set") == 0) && strcmp(curr_id_struct_add.register_addr, curr_id_struct_add.operand_1) != 0 && strcmp(curr_id_struct_add.register_addr, curr_id_struct_add.operand_2) != 0) {
            // if (strcmp(curr_id_struct_add.opcode, "st") != 0) {
            //     register_address = curr_id_struct_add.register_addr;
            //     register_address++;
            //     addr = atoi(register_address);
            //     printf("Register %d\n", atoi(register_address));
            //     printf("Register %d\n", cpu->regs[atoi(register_address)].is_writing);
            //     if (cpu->regs[addr].is_writing) {
            //         // local_stall = true;
            //     }
            // }

            if (strstr(curr_id_struct_add.operand_1, "R")) {
                register_address = curr_id_struct_add.operand_1;
                register_address++;
                addr = atoi(register_address);
                if (cpu->regs[addr].is_writing) {
                    // local_stall = true;
                }
            }

            if (curr_id_struct_add.num_var == 5 && strstr(curr_id_struct_add.operand_2, "R")) {
                register_address = curr_id_struct_add.operand_2;
                register_address++;
                addr = atoi(register_address);
                if (cpu->regs[addr].is_writing) {
                    // local_stall = true;
                }
            }
        }

        // if (strcmp(curr_id_struct_add.register_addr, curr_id_struct_add.operand_1) != 0) {
        //     if (strstr(curr_id_struct_add.operand_1, "R")) {
        //         register_address = curr_id_struct_add.operand_1;
        //         register_address++;
        //         addr = atoi(register_address);
        //         if (cpu->regs[addr].is_writing) {
        //             local_stall = true;
        //         }
        //     }
        // }
        // if (strcmp(curr_id_struct_add.register_addr, curr_id_struct_add.operand_2) != 0) {
        //     if (strstr(curr_id_struct_add.operand_2, "R")) {
        //         register_address = curr_id_struct_add.operand_2;
        //         register_address++;
        //         addr = atoi(register_address);
        //         if (cpu->regs[addr].is_writing) {
        //             local_stall = true;
        //         }
        //     }
        // }

        if (!local_stall) {
            curr_id_struct_add.dependency = false;
        }
        else {
            curr_id_struct_add.dependency = true;
        }
    }
    printf("                                                ADD            : %s %d %d %d %d\n", curr_id_struct_add.instruction, curr_id_struct_add.dependency, curr_id_struct_add.value_1, curr_id_struct_add.value_2, curr_id_struct_add.wb_value);
    return 0;
}


int multiplier_stage(CPU *cpu) {
    int addr;
    char *register_address;
    int value;
    bool local_stall = false;
    prev_id_struct_mul = curr_id_struct_mul;

    if (strcmp(prev_id_struct_mul.opcode, "ret") == 0) {
        return 0;
    }
    // printf("dependncy %s %d %d\n", prev_id_struct_add.instruction, prev_id_struct_add.dependency, strlen(prev_id_struct_add.instruction) != 0 && !prev_id_struct_add.dependency);

    curr_id_struct_mul = prev_id_struct_add;

    if (strlen(prev_id_struct_add.instruction) != 0) {
        if (!prev_id_struct_add.dependency) {
            if (strcmp(curr_id_struct_mul.opcode, "mul") == 0) {
                curr_id_struct_mul.wb_value = curr_id_struct_mul.value_1 * curr_id_struct_mul.value_2;
            }
        }
        else {
            strcpy(curr_id_struct_mul.instruction, "");
        }

        // // forwarding from mul to add
        // if (strcmp(curr_id_struct_mul.opcode, "st")) {
        //     if (strcmp(curr_id_struct_add.operand_1, curr_id_struct_mul.register_addr) == 0) {
        //         curr_id_struct_add.value_1 = curr_id_struct_mul.wb_value;
        //     }
        //     if (curr_id_struct_add.num_var == 5 && strcmp(curr_id_struct_add.operand_2, curr_id_struct_mul.register_addr) == 0) {
        //         curr_id_struct_add.value_2 = curr_id_struct_mul.wb_value;
        //     }
        // }


        if (strcmp(curr_id_struct_mul.opcode, "mul") == 0 && strcmp(curr_id_struct_mul.register_addr, curr_id_struct_mul.operand_1) != 0 && strcmp(curr_id_struct_mul.register_addr, curr_id_struct_mul.operand_2) != 0) {
            // if (strcmp(curr_id_struct_mul.opcode, "st") != 0) {
            //     register_address = curr_id_struct_mul.register_addr;
            //     register_address++;
            //     addr = atoi(register_address);
            //     printf("Register %d\n", atoi(register_address));
            //     printf("Register %d\n", cpu->regs[atoi(register_address)].is_writing);
            //     if (cpu->regs[addr].is_writing) {
            //         // local_stall = true;
            //     }
            // }

            if (strstr(curr_id_struct_mul.operand_1, "R")) {
                register_address = curr_id_struct_mul.operand_1;
                register_address++;
                addr = atoi(register_address);
                if (cpu->regs[addr].is_writing) {
                    // local_stall = true;
                }
            }

            if (curr_id_struct_mul.num_var == 5 && strstr(curr_id_struct_mul.operand_2, "R")) {
                register_address = curr_id_struct_mul.operand_2;
                register_address++;
                addr = atoi(register_address);
                if (cpu->regs[addr].is_writing) {
                    // local_stall = true;
                }
            }
        }
        if (!local_stall) {
            curr_id_struct_mul.dependency = false;
        }
        else {
            curr_id_struct_mul.dependency = true;
        }
    }
        printf("                                                MUL            : %s %d %d %d\n", curr_id_struct_mul.instruction, curr_id_struct_mul.value_1, curr_id_struct_mul.value_2, curr_id_struct_mul.wb_value);

    return 0;
}


int divition_stage(CPU *cpu) {
    int addr;
    char *register_address;
    int value;
    bool local_stall = false;
    prev_id_struct_div = curr_id_struct_div;

    if (strcmp(prev_id_struct_div.opcode, "ret") == 0) {
        return 0;
    }
    printf("DIV prev : %s %d %d %d\n", curr_id_struct_div.instruction, curr_id_struct_div.value_1, curr_id_struct_div.value_2, curr_id_struct_div.wb_value);
    printf("BR prev div stage : %s %d %d %d %d\n", prev_id_struct_div.instruction, prev_id_struct_div.value_1, prev_id_struct_div.value_2, prev_id_struct_div.wb_value, prev_id_struct_div.dependency);
    curr_id_struct_div = prev_id_struct_mul;

    if (strlen(prev_id_struct_mul.instruction) != 0) {
        if (!prev_id_struct_mul.dependency) {
            if (strcmp(curr_id_struct_div.opcode, "div") == 0) {
                // printf("DIV            : %s %d %d %d\n", curr_id_struct_div.instruction, curr_id_struct_div.value_1, curr_id_struct_div.value_2, curr_id_struct_add.wb_value);
                curr_id_struct_div.wb_value = curr_id_struct_div.value_1 / curr_id_struct_div.value_2;
            }
        }
        else {
            strcpy(curr_id_struct_div.instruction, "");
        }


        // // forwarding from div to add
        // if (strcmp(curr_id_struct_div.opcode, "st")) {
        //     if (strcmp(curr_id_struct_add.operand_1, curr_id_struct_div.register_addr) == 0) {
        //         curr_id_struct_add.value_1 = curr_id_struct_div.wb_value;
        //     }
        //     if (curr_id_struct_add.num_var == 5 && strcmp(curr_id_struct_add.operand_2, curr_id_struct_div.register_addr) == 0) {
        //         curr_id_struct_add.value_2 = curr_id_struct_div.wb_value;
        //     }
        // }

        if (strcmp(curr_id_struct_div.opcode, "div") == 0 && strcmp(curr_id_struct_div.register_addr, curr_id_struct_div.operand_1) != 0 && strcmp(curr_id_struct_div.register_addr, curr_id_struct_div.operand_2) != 0) {
            register_address = curr_id_struct_div.register_addr;
            register_address++;
            addr = atoi(register_address);
            printf("Register %d\n", atoi(register_address));
            printf("Register %d\n", cpu->regs[atoi(register_address)].is_writing);
            if (cpu->regs[addr].is_writing) {
                // local_stall = true;
            }
        // printf("#### 1 local stall %d\n", local_stall);

            if (strstr(curr_id_struct_div.operand_1, "R")) {
                register_address = curr_id_struct_div.operand_1;
                register_address++;
                addr = atoi(register_address);
                printf("Register %d\n", atoi(register_address));
                printf("Register %d\n", cpu->regs[atoi(register_address)].is_writing);
            
                if (cpu->regs[addr].is_writing) {
                    // local_stall = true;
                }
            }
        printf("#### 2 local stall %d\n", local_stall);

            if (curr_id_struct_div.num_var == 5 && strstr(curr_id_struct_div.operand_2, "R")) {
                register_address = curr_id_struct_div.operand_2;
                register_address++;
                addr = atoi(register_address);
                printf("Register %d\n", atoi(register_address));
                printf("Register %d\n", cpu->regs[atoi(register_address)].is_writing);
            
                if (cpu->regs[addr].is_writing) {
                    // local_stall = true;
                }
            }
        printf("#### 3 local stall %d\n", local_stall);
        }
        if (!local_stall) {
            curr_id_struct_div.dependency = false;
        }
        else {
            curr_id_struct_div.dependency = true;
        }
    }
    printf("                                                DIV            : %s %d %d %d %d\n", curr_id_struct_div.instruction, curr_id_struct_div.value_1, curr_id_struct_div.value_2, curr_id_struct_div.wb_value, curr_id_struct_div.dependency);

    return 0;
}


int branch(CPU *cpu) {
    int addr;
    char *register_address;
    prev_id_struct_br = curr_id_struct_br;

    if (strcmp(prev_id_struct_br.opcode, "ret") == 0) {
        return 0;
    }

    printf("BR prev : %s %d %d %d %d\n", prev_id_struct_div.instruction, prev_id_struct_div.value_1, prev_id_struct_div.value_2, prev_id_struct_div.wb_value, prev_id_struct_div.dependency);
    if (strlen(prev_id_struct_div.instruction) != 0 && !prev_id_struct_div.dependency) {
        curr_id_struct_br = prev_id_struct_div;

        register_address = curr_id_struct_br.register_addr;
        register_address++;
        addr = atoi(register_address);
        printf("Register %d\n", atoi(register_address));
        printf("Register %d\n", cpu->regs[atoi(register_address)].is_writing);
        // cpu->regs[addr].is_writing = true;
    }
    else {
        strcpy(curr_id_struct_br.instruction, "");
    }
        printf("                                                BR             : %s %d %d %d\n", curr_id_struct_br.instruction, curr_id_struct_div.value_1, curr_id_struct_div.value_2, curr_id_struct_div.wb_value);

    return 0;
}


int memory_1(CPU *cpu) {
    int addr;
    char *register_address;
    prev_id_struct_mem1 = curr_id_struct_mem1;
        
    if (strcmp(prev_id_struct_mem1.opcode, "ret") == 0) {
        return 0;
    }

    if (strlen(prev_id_struct_br.instruction) != 0 && !prev_id_struct_br.dependency) {
        curr_id_struct_mem1 = prev_id_struct_br;

        // register_address = curr_id_struct_mem1.register_addr;
        // register_address++;
        // addr = atoi(register_address);
        // cpu->regs[addr].is_writing = true;
    }
    else {
        strcpy(curr_id_struct_mem1.instruction, "");
    }
        printf("                                                Mem1           : %s %d %d %d\n", curr_id_struct_mem1.instruction, curr_id_struct_mem1.value_1, curr_id_struct_mem1.value_2, curr_id_struct_mem1.wb_value);

    return 0;
}


int memory_2(CPU *cpu) {
    int addr;
    char *register_address;
    char *register_number;
    int value;

    prev_id_struct_mem2 = curr_id_struct_mem2;

    if (strcmp(prev_id_struct_mem2.opcode, "ret") == 0) {
        return 0;
    }

    if (strlen(prev_id_struct_mem1.instruction) != 0 && !prev_id_struct_mem1.dependency) {
        curr_id_struct_mem2 = prev_id_struct_mem1;
        if (strcmp(curr_id_struct_mem2.opcode, "ld") == 0) {    //Load instruction
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
                // if (!((cpu->regs[addr]).is_writing)) {
                int idx = (curr_id_struct_mem2.value_1)/4;
                value = memory_map[idx];
                printf("\n^^^^^^^^^^^^^value %d %d %d %d\n", curr_id_struct_mem2.value_1, (curr_id_struct_mem2.value_1)/4, idx, value);
                // }
                printf("@@@@@@@@@@@@@@@@@@@@@@%d %d %d\n", memory_map[2191], memory_map[2192], memory_map[2193]);
            }
            curr_id_struct_mem2.wb_value = value;
        }
        else if (strcmp(curr_id_struct_mem2.opcode, "st") == 0) {   //store instruction 
            register_number = curr_id_struct_mem2.register_addr;
            register_number += 1;

            addr = atoi(register_number);
            if (!((cpu->regs[addr]).is_writing)) {
                value = curr_id_struct_mem2.value_1;    //get value from Register in operand1
            }
            curr_id_struct_mem2.wb_value = value;
        }

        // register_address = curr_id_struct_mem2.register_addr;
        // register_address++;
        // addr = atoi(register_address);
        // cpu->regs[addr].is_writing = true;

    }
    else {
        strcpy(curr_id_struct_mem2.instruction, "");
    }
        printf("                                                Mem2           : %s %d %d %d\n", curr_id_struct_mem2.instruction, curr_id_struct_mem2.value_1, curr_id_struct_mem2.value_2, curr_id_struct_mem2.wb_value);

    return 0;
}


int write_back(CPU *cpu) {
    char *register_addr;
    prev_id_struct_wb = curr_id_struct_wb;
    curr_id_struct_wb = prev_id_struct_mem2;
    int addr;
    char operands[2][6];

    if (strlen(prev_id_struct_mem2.instruction) != 0) {
        if (strcmp(curr_id_struct_wb.opcode, "ret") == 0) {
            break_loop = true;
        }
        else if (strcmp(curr_id_struct_wb.opcode, "st") == 0) {
            register_addr = curr_id_struct_wb.operand_1;
            if (strstr(curr_id_struct_wb.operand_1, "R")) {
                register_addr += 1;
                
                // printf("Register %d\n", atoi(register_addr));
                // printf("Register %d\n", cpu->regs[atoi(register_addr)].is_writing);
                addr = cpu->regs[atoi(register_addr)].value;   // address found in the given register
                cpu->regs[atoi(register_addr)].is_writing = false;
            }
            else {
                register_addr += 1;
                addr = atoi(register_addr);
            }
            printf("address %d\n", addr);
            memory_map[addr/4] = curr_id_struct_wb.wb_value;
        }
        else {
            register_addr = curr_id_struct_wb.register_addr;
            if (strstr(curr_id_struct_wb.register_addr, "R")) {
                register_addr += 1;
                // if (!cpu->regs[atoi(register_addr)].is_writing) {
                    // cpu->regs[atoi(register_addr)].is_writing = true;
                    cpu->regs[atoi(register_addr)].value = curr_id_struct_wb.wb_value;
                // }
                
                printf("Register %d\n", atoi(register_addr));
                printf("Register %d\n", cpu->regs[atoi(register_addr)].is_writing);
                cpu->regs[atoi(register_addr)].is_writing = false;

                printf("Register %d\n", atoi(register_addr));
                printf("Register %d\n", cpu->regs[atoi(register_addr)].is_writing);
            }
        }

        // strcpy(operands[0], curr_id_struct_wb.operand_1);
        // strcpy(operands[1], curr_id_struct_wb.operand_2);

        // for (int i = 3; i < curr_id_struct_wb.num_var; i++) {
        //     register_addr = operands[i - 3];

        //     if (strstr(operands[i - 3], "R")) {
        //         register_addr += 1;
                // cpu->regs[atoi(register_addr)].is_writing = false;
                // printf("Register %d\n", atoi(register_addr));
                // printf("Register %d\n", cpu->regs[atoi(register_addr)].is_writing);
            // }
    }
    else {
        strcpy(curr_id_struct_wb.instruction, "");
    }
        printf("                                                WB             : %s %d\n", curr_id_struct_wb.instruction, curr_id_struct_wb.wb_value);

    return 0;
}
