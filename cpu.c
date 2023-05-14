
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include "cpu.h"

#define REG_COUNT 16
#define BTB_SIZE 16
#define MAX_ADD 1
#define MAX_MUL 2
#define MAX_DIV 3
#define MAX_MEM 4

int memory_map[16384];
char instruction_set[MAX_INSTRUCTION_COUNT][MAX_INSTRUCTION_LENGTH];
int instruction_count = 0;
int fetched_instructions = 0;
int program_counter = 0;
int stalled_cycles = 0;
int squash_instrutions = 0;
int rob_stall = 0;
int rs_stall = 0;
int add_counter = 0;
int mul_counter = 0;
int div_counter = 0;
int mem_counter = 0;
bool stall = false;
bool break_loop = false;

struct rs_queue_struct rs_queue;
struct rob_queue_struct rob_queue;


decoded_instruction curr_id_struct_if, curr_id_struct_decode, curr_id_struct_ia, curr_id_struct_rr, curr_id_struct_add, curr_id_struct_mul, curr_id_struct_div, 
	curr_id_struct_br, curr_id_struct_mem1, curr_id_struct_mem2;
decoded_instruction prev_id_struct_if, prev_id_struct_decode, prev_id_struct_ia, prev_id_struct_rr, prev_id_struct_add, prev_id_struct_mul, prev_id_struct_div, 
	prev_id_struct_br, prev_id_struct_mem1, prev_id_struct_mem2;

decoded_instruction empty_instruction;

decoded_instruction curr_id_struct_is[RS_SIZE], prev_id_struct_is[RS_SIZE], curr_id_struct_arith[RS_SIZE], prev_id_struct_arith[RS_SIZE],
    curr_id_struct_wb[RS_SIZE], prev_id_struct_wb[RS_SIZE];

decoded_instruction curr_add_stage_1, prev_add_stage_1, curr_mul_stage_1, prev_mul_stage_1, curr_mul_stage_2, prev_mul_stage_2, 
    curr_div_stage_1, prev_div_stage_1, curr_div_stage_2, prev_div_stage_2, curr_div_stage_3, prev_div_stage_3, 
    curr_mem_stage_1, prev_mem_stage_1, curr_mem_stage_2, prev_mem_stage_2, curr_mem_stage_3, prev_mem_stage_3, curr_mem_stage_4, prev_mem_stage_4; 


/* rob circular queue implementation */
void rob_queue_init() {
    for (int i = 0; i < ROB_SIZE; i++) {
        rob_queue.buffer[i].dest =  -1;
        rob_queue.buffer[i].result = -1;
        rob_queue.buffer[i].e = 0;
        rob_queue.buffer[i].completed = 1;
    }
}

int rob_queue_full() {
    int next_rear = (rob_queue.rear + 1) % ROB_SIZE; // calculate next rear index
    if (next_rear == rob_queue.front) { // check if queue is full
        printf("Error: Queue is full\n");
        return 1;
    }
    return 0;
}

int rob_queue_empty() {
    if (rob_queue.front == rob_queue.rear) { // check if queue is empty
        printf("Error: Queue is empty\n");
        return 1;
    }
    return 0;
}

void rob_enqueue(rob_struct rob_data) {
    int next_rear = (rob_queue.rear + 1) % ROB_SIZE; // calculate next rear index
    if (rob_queue_full() == 0) { // check if queue is full
        rob_queue.buffer[rob_queue.rear].dest = rob_data.dest;
        rob_queue.buffer[rob_queue.rear].result = rob_data.result;
        rob_queue.buffer[rob_queue.rear].e = rob_data.e;
        rob_queue.buffer[rob_queue.rear].completed = rob_data.completed;
        rob_queue.rear = next_rear;    
    }
}

void rob_dequeue() {
    if (rob_queue_empty() == 0) { // check if queue is empty
        rob_queue.front = (rob_queue.front + 1) % ROB_SIZE; // calculate next front index
    }
}

void rob_display() {
    printf("rob_queue.front %d\n", rob_queue.front);
    printf("rob_queue.rear %d\n", rob_queue.rear);

    for (int i = 0; i < ROB_SIZE; i++) {
        printf("|ROB%d [dest: %d, result: %d, (e: %d, completed: %d)]|\n", i, rob_queue.buffer[i].dest, rob_queue.buffer[i].result, rob_queue.buffer[i].e, rob_queue.buffer[i].completed);
    }
    printf("\n");
}
/* end of ROB queue implementation */


/* reservation station queue implementation */

int rs_queue_full() {
    int next_rear = (rs_queue.rear + 1) % RS_SIZE; // calculate next rear index
    if (next_rear == rs_queue.front) { // check if queue is full
        printf("Error: Queue is full\n");
        return 1;
    }
    return 0;
}

int rs_queue_empty() {
    if (rs_queue.front == rs_queue.rear) { // check if queue is empty
        printf("Error: Queue is empty\n");
        return 1;
    }
    return 0;
}

void rs_enqueue(decoded_instruction instruction) {
    int next_rear = (rs_queue.rear + 1) % RS_SIZE; // calculate next rear index
    if (rs_queue_full() == 0) { // check if queue is full
        rs_queue.instructions[rs_queue.rear] = instruction;
        rs_queue.rear = next_rear;    
    }
}

void *rs_dequeue() {
    if (rs_queue_empty() == 0) { // check if queue is not empty
        rs_queue.front = (rs_queue.front + 1) % RS_SIZE; // calculate next front index
    }
}

decoded_instruction *rs_get_instruction() {
    if (rs_queue_empty() == 0) { // check if queue is not empty
        decoded_instruction *rs_instruction = malloc(sizeof(decoded_instruction));
        *rs_instruction = rs_queue.instructions[rs_queue.front];
        return rs_instruction;
    }
    return NULL;
}

void rs_display() {
    printf("RS Queue contents: \n");
    // printf("rs_queue.front %d\n", rs_queue.front);
    // printf("rs_queue.rear %d\n", rs_queue.rear);

    if (rs_queue.front == rs_queue.rear) {
        printf("empty\n");
    } else {
        int i = rs_queue.front;
        while (i != rs_queue.rear) {
        printf("00%d: %s %s %s %s \n", rs_queue.instructions[i].addr*4, rs_queue.instructions[i].opcode, rs_queue.instructions[i].register_addr, rs_queue.instructions[i].operand_1, rs_queue.instructions[i].operand_2);
        i = (i + 1) % (RS_SIZE + 1); // move to the next index, wrapping around
        }
        printf("\n");
    }
}
/* end of reservation station queue implementation */


CPU *CPU_init() {
	CPU *cpu = malloc(sizeof(*cpu));
	if (!cpu) {
		return NULL;
	}

	/* Create register files */
	cpu->regs = create_registers(REG_COUNT);

	/* Create btb */
	cpu->btb = create_btb(BTB_SIZE);

	/* Create prediction table */
	cpu->predict_tb = create_predict_tb(BTB_SIZE);

    rob_queue_init();
    
    for (int i = 0; i < RS_SIZE; i++) {
        curr_id_struct_is[i] = empty_instruction;
        curr_id_struct_arith[i] = empty_instruction;
        curr_id_struct_wb[i] = empty_instruction;
    }

    curr_add_stage_1 = empty_instruction;

    curr_mul_stage_1 = empty_instruction;
    curr_mul_stage_2 = empty_instruction;

    curr_div_stage_1 = empty_instruction;
    curr_div_stage_2 = empty_instruction;
    curr_div_stage_3 = empty_instruction;

    curr_mem_stage_1 = empty_instruction;
    curr_mem_stage_2 = empty_instruction;
    curr_mem_stage_3 = empty_instruction;
    curr_mem_stage_4 = empty_instruction;

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

void print_registers_test(CPU *cpu, int cycle) {
    printf("------------ STATE OF ARCHITECTURAL REGISTER FILE ----------\n");
    printf("R# [(status 0=invalid, 1=valid), tag, value, arith_done]\n");
	for (int reg=0; reg<REG_COUNT; reg++) {
        printf("R%d [(%d), %d, %d, %d]\n", reg, cpu->regs[reg].is_valid, cpu->regs[reg].tag, cpu->regs[reg].value, cpu->regs[reg].arith_done);
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


void print_btb_tb(CPU *cpu, int cycle){
	printf("============ BTB =================================\n");
	for (int count = 0; count < BTB_SIZE; count++) {
		printf("BTB[%2d]   |   Tag = %d   |   Target = %d   |\n", count % BTB_SIZE, cpu->btb[count % BTB_SIZE].tag, cpu->btb[count % BTB_SIZE].target);
		printf("--------------------------------\n");
	}
	printf("================================\n");
	printf("\n");
	printf("============ Prediction Table =================================\n");
	for (int count = 0; count < BTB_SIZE; count++) {
		printf("PT[%2d]   |   Pattern = %d   |\n", count % BTB_SIZE, cpu->predict_tb[count % BTB_SIZE].pattern);
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

        printf("squash_instrutions %d\n", squash_instrutions);
		instruction_fetch(cpu);
		instruction_decode(cpu);
		instruction_analyze(cpu);
		register_read(cpu);
        instruction_issue(cpu);
        arithmetic_operation(cpu);
		write_back(cpu);
        retire_stage(cpu);

		// print_btb_tb(cpu, cpu->clock_cycle);
		// print_display(cpu, cpu->clock_cycle);
        print_registers_test(cpu, cpu->clock_cycle);
        printf("\n\n------------ Reserve Station ----------\n");
        rs_display();
        printf("\n\n------------ Reorder Buffer----------\n");
        rob_display();

		if (break_loop) {
			break;
		}
        
        if (cpu->clock_cycle == 850)         // For DEBUG
            exit(0);
	}

	write_memory_map(filename);

	print_registers(cpu);
    printf("Number of IR stage stalls due to the full reservation station: %d\n", rs_stall);
    printf("Number of IR stage stalls due to the full reorder buffer: %d\n", rob_stall);
	printf("Stalled cycles due to data hazard: %d \n", stalled_cycles);
	printf("Total execution cycles: %d\n", cpu->clock_cycle);
	printf("Total instruction simulated: %d\n", fetched_instructions);
	printf("IPC: %.6f\n", (float)fetched_instructions/(float)(cpu->clock_cycle));

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
        regs[i].tag = 0;
		regs[i].is_valid = 0;
        regs[i].arith_done = 1;
	}
	return regs;
}

BTB *
create_btb(int size) {
	BTB *btb = malloc(sizeof(*btb) * size);
	if (!btb) {
		return NULL;
	}
	for (int i = 0; i < size; i++) {
		btb[i].tag = -1;
		btb[i].target = -1;
	}
	return btb;
}

PredictionTable *
create_predict_tb(int size) {
	PredictionTable *predict_tb = malloc(sizeof(*predict_tb) * size);
	if (!predict_tb) {
		return NULL;
	}
	for (int i = 0; i < size; i++) {
		predict_tb[i].pattern = 3;
	}
	return predict_tb;
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
	// char base_dir[250] = "./programs/";
	// strcat(base_dir, filename);
	
	FILE *instruction_file = fopen(filename, "r");

	while (fgets(instruction_set[instruction_count], MAX_INSTRUCTION_LENGTH, instruction_file)) {
		instruction_set[instruction_count][strcspn(instruction_set[instruction_count], "\r\n")] = '\0';
		instruction_count++;
	}

	// for (int i = 0; i < MAX_INSTRUCTION_COUNT; i++) {
	//     for (int j = 0; j < MAX_INSTRUCTION_LENGTH; j++) {
	//         printf("%c", instruction_set[i][j]);
	//     }
	// }
	
	return 0;
}


int write_memory_map(char* filename) {
	char *op_filename = "output_memory_map.txt";

	FILE *fp = fopen(op_filename, "w+");
	char c[100];
	for (int i = 0; i < 16384; i++) {
		sprintf(c, "%d", memory_map[i]);
		fprintf(fp, "%s ", c);
	}
	fclose(fp);

	return 0;
}


int instruction_fetch(CPU *cpu) {
	prev_id_struct_if = curr_id_struct_if;

	if (squash_instrutions) {
		prev_id_struct_if = empty_instruction;
		stall = false;
	}

    // structural hazard - if reservation station queue is full then stall if stage
	if (rs_queue_full() || rob_queue_full()) {
        stall = true;
	}
    printf("Stall %d\n", stall);
    printf("rs_queue_full %d\n", rs_queue_full());

    if (!stall) {
        strcpy(curr_id_struct_if.instruction, instruction_set[program_counter]);

        if (strstr(instruction_set[program_counter], "bez") || strstr(instruction_set[program_counter], "blez") || strstr(instruction_set[program_counter], "bltz") || 
        strstr(instruction_set[program_counter], "bgez") || strstr(instruction_set[program_counter], "bgtz") || strstr(instruction_set[program_counter], "bez")) {
            if (cpu->predict_tb[program_counter % BTB_SIZE].pattern >= 4) {
                program_counter = cpu->btb[program_counter % BTB_SIZE].target;
            }
            else {
                program_counter++;
            }
        }
        else {
            program_counter++;
        }
    }
    else {
        strcpy(curr_id_struct_if.instruction, prev_id_struct_if.instruction);
    }
	printf("                                       IF             : %s %s %s %s\n", curr_id_struct_if.instruction, curr_id_struct_if.opcode, curr_id_struct_if.operand_1, curr_id_struct_if.operand_2);

	return 0;
}


int instruction_decode(CPU *cpu) {
	char instruction[MAX_INSTRUCTION_LENGTH];
	char *decoded_instruction[5];
	char *tokenised = NULL;
	int counter = 0;

	prev_id_struct_decode = curr_id_struct_decode;

	if (squash_instrutions) {
        curr_id_struct_decode = empty_instruction;
		prev_id_struct_decode = empty_instruction;
	}

	if (strlen(prev_id_struct_if.instruction) != 0 && !stall) {
		curr_id_struct_decode = prev_id_struct_if;
		strcpy(instruction, curr_id_struct_decode.instruction);
		tokenised = strtok(instruction, " ");

		while (tokenised != NULL) {
			decoded_instruction[counter++] = tokenised;
			tokenised = strtok(NULL, " ");
		}

		curr_id_struct_decode.addr = atoi(decoded_instruction[0])/4;

		strcpy(curr_id_struct_decode.opcode, decoded_instruction[1]);

		if (strcmp(curr_id_struct_decode.opcode, "ret") != 0) {

			strcpy(curr_id_struct_decode.register_addr, decoded_instruction[2]);
			strcpy(curr_id_struct_decode.org_register_addr, decoded_instruction[2]);

			strcpy(curr_id_struct_decode.operand_1, decoded_instruction[3]);
			strcpy(curr_id_struct_decode.org_operand_1, decoded_instruction[3]);

			if (counter == 5) {
				strcpy(curr_id_struct_decode.operand_2, decoded_instruction[4]);
				strcpy(curr_id_struct_decode.org_operand_2, decoded_instruction[4]);
			}

		}
		curr_id_struct_decode.num_var = counter;

		curr_id_struct_decode.dependency = false;

	}

	printf("                                       ID             : %s %s %s %s\n", curr_id_struct_decode.instruction, curr_id_struct_decode.opcode, curr_id_struct_decode.operand_1, curr_id_struct_decode.operand_2);

	return 0;
}


int instruction_analyze(CPU *cpu) {
	bool local_stall = false;
	int addr;
	char *register_address;

	prev_id_struct_ia = curr_id_struct_ia;

	if (squash_instrutions) {
        curr_id_struct_ia = empty_instruction;
		prev_id_struct_ia = empty_instruction;
	}

	if (strlen(prev_id_struct_decode.instruction) != 0 && !stall) {
		curr_id_struct_ia = prev_id_struct_decode;
	}
	
	if (strlen(curr_id_struct_div.instruction) != 0) {
		if (strcmp(curr_id_struct_div.opcode, "st") != 0) {
			register_address = curr_id_struct_div.register_addr;
			register_address++;
			addr = atoi(register_address);
			cpu->regs[addr].is_writing = true;      // set register addr is_writing to true for instruction in br stage
		}
	}
	printf("                                       IA             : %s %d\n", curr_id_struct_ia.instruction, curr_id_struct_ia.dependency);

	return 0;
}


int register_read(CPU *cpu) {
    squash_instrutions = 0;
	int values[2] = {0, 0};
	char operands[2][6];
	int addr;
	char *register_address;
	bool local_stall = false;
    bool operand_1_inuse = false;
    bool operand_2_inuse = false;
    bool register_addr_inuse = false;

    char rename_address[5];
    char dest_num[2] = {};


	// if (squash_instrutions) {
	// curr_id_struct_rr = empty_instruction;
		prev_id_struct_rr = empty_instruction;
	// }


    printf("****stall IR %d %d\n", stall, curr_id_struct_rr.dependency);
    if (!stall) {
    	prev_id_struct_rr = curr_id_struct_rr;
        curr_id_struct_rr = prev_id_struct_ia;
        if (strcmp(curr_id_struct_rr.opcode, "ret") == 0) {
            squash_instrutions = 1;
        }
    }

	if (strlen(prev_id_struct_ia.instruction)) {

        /* Renaming registers */

        /*Rename the operands 1 and 2 values if they are present in the ROB buffer else don't rename*/
        register_address = curr_id_struct_rr.operand_1;
        if (strstr(register_address, "R") || strstr(register_address, "ROB")) {
            if (strstr(register_address, "ROB")) {
                register_address += 3;
                addr = atoi(register_address);
                for (int i = rob_queue.rear; i != rob_queue.front - 1 && i != ROB_SIZE - 1; i = (i - 1 + ROB_SIZE) % ROB_SIZE) {
                    if (i == addr) {         // comparing output register address
                        if (rob_queue.buffer[i].completed == 0) {
                            strcpy(rename_address, "ROB");
                            sprintf(dest_num, "%d", i);
                            strcat(rename_address, dest_num);
                            strcpy(curr_id_struct_rr.operand_1, rename_address);
                            break;
                        }
                    }
                }
            }
            else {
                register_address += 1;
                addr = atoi(register_address);      // get the register address in instruction
                for (int i = rob_queue.rear; i != rob_queue.front - 1 && i != ROB_SIZE - 1; i = (i - 1 + ROB_SIZE) % ROB_SIZE) {
                    if (rob_queue.buffer[i].dest == addr) {         // comparing output register address
                        if (rob_queue.buffer[i].completed == 0) {
                            strcpy(rename_address, "ROB");
                            sprintf(dest_num, "%d", i);
                            strcat(rename_address, dest_num);
                            strcpy(curr_id_struct_rr.operand_1, rename_address);
                            break;
                        }
                    }
                }
            }
        }

        if (curr_id_struct_rr.num_var == 5) {
            register_address = curr_id_struct_rr.operand_2;
            if (strstr(register_address, "R") || strstr(register_address, "ROB")) {
                if (strstr(register_address, "ROB")) {
                    register_address += 3;
                    addr = atoi(register_address);
                    for (int i = rob_queue.rear; i != rob_queue.front - 1 && i != ROB_SIZE - 1; i = (i - 1 + ROB_SIZE) % ROB_SIZE) {
                        if (i == addr) {         // comparing output register address
                            if (rob_queue.buffer[i].completed == 0) {
                                strcpy(rename_address, "ROB");
                                sprintf(dest_num, "%d", i);
                                strcat(rename_address, dest_num);
                                strcpy(curr_id_struct_rr.operand_2, rename_address);
                                break;
                            }
                        }
                    }
                }
                else {
                    register_address += 1;
                    addr = atoi(register_address);      // get the register address in instruction
                    for (int i = rob_queue.rear; i != rob_queue.front - 1 && i != ROB_SIZE - 1; i = (i - 1 + ROB_SIZE) % ROB_SIZE) {
                        if (rob_queue.buffer[i].dest == addr) {         // comparing output register address
                            if (rob_queue.buffer[i].completed == 0) {
                                strcpy(rename_address, "ROB");
                                sprintf(dest_num, "%d", i);
                                strcat(rename_address, dest_num);
                                strcpy(curr_id_struct_rr.operand_2, rename_address);
                                break;
                            }
                        }
                    }
                }
            }
        }

        /* checking if the current register is in use in ROB buffer */
        
        if (strstr(curr_id_struct_rr.opcode, "bez") || strstr(curr_id_struct_rr.opcode, "blez") || strstr(curr_id_struct_rr.opcode, "bltz") || 
            strstr(curr_id_struct_rr.opcode, "bgez") || strstr(curr_id_struct_rr.opcode, "bgtz") || strstr(curr_id_struct_rr.opcode, "bez")) {

            register_address = curr_id_struct_rr.register_addr;
            printf("curr_id_struct_rr %s %s\n", curr_id_struct_rr.instruction, register_address);

            if (strstr(register_address, "R") || strstr(register_address, "ROB")) {
                if (strstr(register_address, "ROB")) {
                    register_address += 3;
                    addr = atoi(register_address);
                    for (int i = rob_queue.front; i != rob_queue.rear; i = (i + 1) % ROB_SIZE) {
                        if (i == addr) {         // comparing output register address
                            if (rob_queue.buffer[i].completed == 0) {
                                strcpy(rename_address, "ROB");
                                sprintf(dest_num, "%d", i);
                                strcat(rename_address, dest_num);
                                strcpy(curr_id_struct_rr.register_addr, rename_address);
                                break;
                            }
                        }
                    }
                }
                else {
                    register_address += 1;
                    addr = atoi(register_address);      // get the register address in instruction
                    for (int i = rob_queue.front; i != rob_queue.rear; i = (i + 1) % ROB_SIZE) {
                        if (rob_queue.buffer[i].dest == addr) {         // comparing output register address
                            if (rob_queue.buffer[i].completed == 0) {
                                strcpy(rename_address, "ROB");
                                sprintf(dest_num, "%d", i);
                                strcat(rename_address, dest_num);
                                strcpy(curr_id_struct_rr.register_addr, rename_address);
                                break;
                            }
                            
                        }
                        if (((i + 1) % ROB_SIZE) == rob_queue.rear ) {
                            strcpy(rename_address, "ROB");
                            sprintf(dest_num, "%d", rob_queue.rear);
                            strcat(rename_address, dest_num);
                            strcpy(curr_id_struct_rr.register_addr, rename_address);
                        }
                    }
                }
            }
        }
        else {
            register_address = curr_id_struct_rr.register_addr;
            register_address += 1;
            addr = atoi(register_address);      // get the register address in instruction
            // for (int i = rob_queue.front; i != rob_queue.rear; i = (i + 1) % ROB_SIZE) {
            //     if (rob_queue.buffer[i].dest == addr) {         // comparing output register address
            //         if (rob_queue.buffer[i].completed == 0) {
            //             strcpy(rename_address, "ROB");
            //             sprintf(dest_num, "%d", i);
            //             strcat(rename_address, dest_num);
            //             strcpy(curr_id_struct_rr.register_addr, rename_address);
            //             break;
            //         }
                    
            //     }
                // if (((i + 1) % ROB_SIZE) == rob_queue.rear ) {
                    strcpy(rename_address, "ROB");
                    sprintf(dest_num, "%d", rob_queue.rear);
                    strcat(rename_address, dest_num);
                    strcpy(curr_id_struct_rr.register_addr, rename_address);
                // }
            // }
        }

        /* end of renaming */

        /* for branch instructions check if register address is same as in IS stage*/
        if (strstr(curr_id_struct_rr.opcode, "bez") || strstr(curr_id_struct_rr.opcode, "blez") || strstr(curr_id_struct_rr.opcode, "bltz") || 
            strstr(curr_id_struct_rr.opcode, "bgez") || strstr(curr_id_struct_rr.opcode, "bgtz") || strstr(curr_id_struct_rr.opcode, "bez"))
            {
            register_address = curr_id_struct_rr.org_register_addr;
            if (strstr(register_address, "R")){
                register_address += 1;
                addr = atoi(register_address);

                if (cpu->regs[addr].arith_done == 0) {
                    register_addr_inuse = true;
                    local_stall = true;
                }
            }

            if (strcmp(curr_id_struct_rr.org_register_addr, prev_id_struct_rr.org_register_addr) == 0) {
                local_stall = true;
                printf("***************\n\n\n");
                printf("prev_id_struct_rr. %s\n", prev_id_struct_rr.instruction);
            }
        }


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
				else if (strstr(register_address, "R")) {
                    printf("-------------register_address %s\n", register_address);
					if (strstr(register_address, "ROB")) {
                        register_address += 3;
    					addr = rob_queue.buffer[atoi(register_address)].dest;
                    }
                    else {
                        register_address += 1;
    					addr = atoi(register_address);
                    }
                    printf("-------------addr %d register_address %s\n", addr, register_address);
                    if (cpu->regs[addr].is_valid) {
    					values[i - 3] = cpu->regs[addr].value;
                        for (int rob_idx = rob_queue.front; rob_idx != rob_queue.rear; rob_idx = (rob_idx + 1) % (ROB_SIZE)) {
                            if (rob_queue.buffer[rob_idx].dest == addr && rob_queue.buffer[rob_idx].completed) {
                                if(rob_queue.buffer[rob_idx].result != values[i - 3]) {
                                    values[i - 3] = rob_queue.buffer[rob_idx].result;
                                }
                            }
                        }
                    }
                    else {
                        for (int rob_idx = rob_queue.front; rob_idx != rob_queue.rear; rob_idx = (rob_idx + 1) % (ROB_SIZE)) {
                            if (rob_queue.buffer[rob_idx].dest == addr) {
                                values[i - 3] = rob_queue.buffer[rob_idx].result;
                            }
                        }
                    }
                    printf("cpu->regs[addr].value %d\n", values[i - 3]);

				}
			}
		}

        printf("local_stall %d\n", local_stall);
		if (!local_stall) {
			curr_id_struct_rr.dependency = false;
            
		}
		else {
			curr_id_struct_rr.dependency = true;
		}     
        printf("curr_id_struct_rr.dependency %d\n", curr_id_struct_rr.dependency);

		if (curr_id_struct_rr.dependency) {
			stall = true;
			stalled_cycles++;
		}
		else {
            curr_id_struct_rr.value_1 = values[0];
            curr_id_struct_rr.value_2 = values[1];

            /* set instructions dependecy to true if the operands are in use in IS stage */
            if (strlen(prev_id_struct_rr.instruction) && strcmp(prev_id_struct_rr.opcode, "st") != 0) {
                if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.org_operand_2, prev_id_struct_rr.org_register_addr) == 0) {
                    curr_id_struct_rr.dependency = true;
                }
                printf("^^^^^^^^^^^^^^curr_id_struct_rr.dependency 997: %d\n", curr_id_struct_rr.dependency);
                if (strcmp(curr_id_struct_rr.operand_1, prev_id_struct_rr.org_register_addr) == 0) {
                    curr_id_struct_rr.dependency = true;
                }
                printf("^^^^^^^^^^^^^^curr_id_struct_rr.dependency 998: %d\n", curr_id_struct_rr.dependency);
                if (strcmp(curr_id_struct_rr.org_register_addr, prev_id_struct_rr.org_register_addr) == 0 && strcmp(curr_id_struct_rr.opcode, "st") == 0) {
                    curr_id_struct_rr.dependency = true;
                }
                printf("^^^^^^^^^^^^^^curr_id_struct_rr.dependency 999: %d\n", curr_id_struct_rr.dependency);
            }

            if (rs_queue_full()) {
                rs_stall++;
                stall = true;
            }
            else {
                curr_id_struct_rr.timestamp = cpu->clock_cycle;
                printf("^^^^^^^^^^\n^^^^^^^^^^^^\n");
                rs_enqueue(curr_id_struct_rr);
            }

            /* add data to ROB table */
            rob_struct rob_data;
            if (strcmp(curr_id_struct_rr.opcode, "bez") && strcmp(curr_id_struct_rr.opcode, "blez") && strcmp(curr_id_struct_rr.opcode, "bltz") && 
                strcmp(curr_id_struct_rr.opcode, "bgez") && strcmp(curr_id_struct_rr.opcode, "bgtz") && strcmp(curr_id_struct_rr.opcode, "bez")) {

                register_address = curr_id_struct_rr.org_register_addr;
                register_address += 1;
                addr = atoi(register_address);

                rob_data.dest = addr;
                rob_data.result = -1;
                rob_data.e = 0;
                rob_data.completed = 0;
            }
            else {
                rob_data.dest = -1;
                rob_data.result = -1;
                rob_data.e = 0;
                rob_data.completed = 0;
            }
            if (rob_queue_full()) {
                rob_stall++;
                stall = true;
                local_stall = true;
            }
            else {
                printf("enqueue 1\n");
                rob_enqueue(rob_data);
            }

			stall = false;
			

            /* for branch instruction get the write back value */
			if (strstr(curr_id_struct_rr.opcode, "bez") || strstr(curr_id_struct_rr.opcode, "blez") || strstr(curr_id_struct_rr.opcode, "bltz") || 
				strstr(curr_id_struct_rr.opcode, "bgez") || strstr(curr_id_struct_rr.opcode, "bgtz") || strstr(curr_id_struct_rr.opcode, "bez")) {

				register_address = curr_id_struct_rr.register_addr;
                printf("line 1089 register_address %s\n", register_address);
				if (strstr(register_address, "ROB")) {
                    register_address += 3;
                    addr = rob_queue.buffer[atoi(register_address)].dest;
                }
                else {
                    register_address += 1;
                    addr = atoi(register_address);
                }
				curr_id_struct_rr.wb_value = cpu->regs[addr].value;
                for (int rob_idx = rob_queue.front; rob_idx != rob_queue.rear; rob_idx = (rob_idx + 1) % (ROB_SIZE)) {
                    if (rob_queue.buffer[rob_idx].dest == addr && rob_queue.buffer[rob_idx].completed) {
                        if(rob_queue.buffer[rob_idx].result != curr_id_struct_rr.wb_value) {
                            curr_id_struct_rr.wb_value = rob_queue.buffer[rob_idx].result;
                        }
                    }
                }

                /* forwarding for branching */
                if (strcmp(curr_id_struct_rr.register_addr, prev_add_stage_1.register_addr) == 0) {
                    curr_id_struct_rr.wb_value = prev_add_stage_1.wb_value;
                }
                if (strcmp(curr_id_struct_rr.register_addr, prev_mul_stage_2.register_addr) == 0) {
                    curr_id_struct_rr.wb_value = prev_mul_stage_2.wb_value;
                }
                if (strcmp(curr_id_struct_rr.register_addr, prev_div_stage_3.register_addr) == 0) {
                    curr_id_struct_rr.wb_value = prev_div_stage_3.wb_value;
                }
                if (strcmp(curr_id_struct_rr.register_addr, prev_mem_stage_4.register_addr) == 0) {
                    curr_id_struct_rr.wb_value = prev_mem_stage_4.wb_value;
                }
                printf("curr_id_struct_rr.wb_value %d\n", curr_id_struct_rr.wb_value);
			}
		}

        /* Branching */
        if (!local_stall && !curr_id_struct_rr.dependency) {
            if (strstr(curr_id_struct_rr.opcode, "bez") || strstr(curr_id_struct_rr.opcode, "blez") || strstr(curr_id_struct_rr.opcode, "bltz") || 
                strstr(curr_id_struct_rr.opcode, "bgez") || strstr(curr_id_struct_rr.opcode, "bgtz") || strstr(curr_id_struct_rr.opcode, "bez")) {
                
                // stall the cycle if instruction in IR is branch and the register address is present in later stages
                if (register_addr_inuse) {
                    local_stall = true;
                    stall = true;
                }
                else {
                    branch_without_prediction(cpu, curr_id_struct_rr);
                }
            }
        }
	}


	printf("                                       RR             : %s %d %d %d\n", curr_id_struct_rr.instruction, curr_id_struct_rr.dependency, curr_id_struct_rr.value_1, curr_id_struct_rr.value_2);
	
	return 0;
}


int instruction_issue(CPU *cpu) {

    int addr;
    char *register_address;
    int values[2] = {0, 0};
	char operands[2][6];

    prev_add_stage_1 = curr_add_stage_1;
    curr_add_stage_1 = empty_instruction;

    prev_mul_stage_1 = curr_mul_stage_1;
    curr_mul_stage_1 = empty_instruction;

    prev_div_stage_1 = curr_div_stage_1;
    curr_div_stage_1 = empty_instruction;

    prev_mem_stage_1 = curr_mem_stage_1;
    curr_mem_stage_1 = empty_instruction;

    decoded_instruction *rs_instruction;

    for (int rs_idx = rs_queue.front; rs_idx != rs_queue.rear; rs_idx = (rs_idx + 1) % (RS_SIZE + 1)) {
        // for (int rs_idx_next = rs_queue.front + 1; rs_idx_next != rs_queue.rear; rs_idx_next = (rs_idx_next + 1) % (RS_SIZE + 1)) {
            printf("-------rs instruction %s\n", rs_queue.instructions[rs_idx].instruction);
            if (strlen(rs_queue.instructions[rs_idx].instruction) && rs_queue.instructions[rs_idx].timestamp < cpu->clock_cycle) {
                rs_queue.instructions[rs_idx].dependency = false;

                register_address = rs_queue.instructions[rs_idx].org_operand_1;
                printf("register addr %s\n", register_address);
                if (strstr(register_address, "R")) {
                    register_address += 1;
                    addr = atoi(register_address);
                    if (cpu->regs[addr].arith_done == 0) {
                        rs_queue.instructions[rs_idx].dependency = true;
                    }
                    printf("^^^^ arith done %d rs_queue.instructions[rs_idx].dependency 999: %d\n", cpu->regs[addr].arith_done, rs_queue.instructions[rs_idx].dependency);
                }

                register_address = rs_queue.instructions[rs_idx].org_operand_2;
                printf("register addr %s\n", register_address);
                if (strstr(register_address, "R")) {
                    register_address += 1;
                    addr = atoi(register_address);
                    if (cpu->regs[addr].arith_done == 0) {
                        rs_queue.instructions[rs_idx].dependency = true;
                    }
                    printf("^^^^ arith done %d rs_queue.instructions[rs_idx].dependency 999: %d\n", cpu->regs[addr].arith_done, rs_queue.instructions[rs_idx].dependency);
                }

                register_address = rs_queue.instructions[rs_idx].org_register_addr;
                printf("register addr %s\n", register_address);
                register_address += 1;
                addr = atoi(register_address);
                if (cpu->regs[addr].arith_done == 0) {
                    rs_queue.instructions[rs_idx].dependency = true;
                }
                printf("^^^^ arith done %d rs_queue.instructions[rs_idx].dependency 999: %d\n", cpu->regs[addr].arith_done, rs_queue.instructions[rs_idx].dependency);
            }
        // }
    }

    for (int i = 0; i < RS_SIZE; i++) {
        rs_instruction = rs_get_instruction();
        if (rs_instruction == NULL){
            break;
        }

        printf("rs_instruction->dependency %d\n", rs_instruction->dependency);
        if (strlen(rs_instruction->instruction) && rs_instruction->timestamp < cpu->clock_cycle && !rs_instruction->dependency) {     // check for instructions added before the current cycle

            if (strcmp(rs_instruction->opcode, "mul") == 0) {
                curr_mul_stage_1 = *rs_instruction;

                register_address = rs_instruction->org_register_addr;
				register_address++;
				addr = atoi(register_address);
                cpu->regs[addr].arith_done = 0;

                rs_dequeue();
            }
            else if (strcmp(rs_instruction->opcode, "div") == 0) {
                curr_div_stage_1 = *rs_instruction;
                
                register_address = rs_instruction->org_register_addr;
				register_address++;
				addr = atoi(register_address);
                cpu->regs[addr].arith_done = 0;

                rs_dequeue();
            }
            else if (strcmp(rs_instruction->opcode, "ld") == 0 || strcmp(rs_instruction->opcode, "st") == 0) {
                curr_mem_stage_1 = *rs_instruction;
                
                register_address = rs_instruction->org_register_addr;
				register_address++;
				addr = atoi(register_address);
                cpu->regs[addr].arith_done = 0;

                rs_dequeue();
            }
            else {
                curr_add_stage_1 = *rs_instruction;
                
                register_address = rs_instruction->org_register_addr;
				register_address++;
				addr = atoi(register_address);
                printf("addr %d\n", addr);
                cpu->regs[addr].arith_done = 0;

                rs_dequeue();
            }
            printf("                                       IS             : %s %d %d %d\n", rs_instruction->instruction, rs_instruction->dependency, rs_instruction->value_1, rs_instruction->value_2);
        }
    }

    return 0;
}


int arithmetic_operation(CPU *cpu) {

    for (int i = 0; i < RS_SIZE; i++) {
        prev_id_struct_arith[i] = curr_id_struct_arith[i];
        curr_id_struct_arith[i] = empty_instruction;
    }
    
    add_stage(cpu);
    multiplier_stage_1(cpu);
    multiplier_stage_2(cpu);
    division_stage_1(cpu);
    division_stage_2(cpu);
    division_stage_3(cpu);
    memory_1(cpu);
    memory_2(cpu);
    memory_3(cpu);
    memory_4(cpu);

    return 0;
}


decoded_instruction add_stage(CPU *cpu) {
	int addr;
	char *register_address;
	int value;
	bool local_stall = false;

	if (squash_instrutions) {
		prev_add_stage_1 = empty_instruction;
		curr_add_stage_1 = empty_instruction;
	}

    if (strlen(prev_add_stage_1.instruction)) {
        if (strcmp(prev_add_stage_1.opcode, "add") == 0 || strcmp(prev_add_stage_1.opcode, "sub") == 0 || strcmp(prev_add_stage_1.opcode, "set") == 0 || strcmp(prev_add_stage_1.opcode, "ret") == 0 ||
            strstr(prev_add_stage_1.opcode, "bez") || strstr(prev_add_stage_1.opcode, "blez") || strstr(prev_add_stage_1.opcode, "bltz") || 
            strstr(prev_add_stage_1.opcode, "bgez") || strstr(prev_add_stage_1.opcode, "bgtz") || strstr(prev_add_stage_1.opcode, "bez")) {

            if (strcmp(prev_add_stage_1.opcode, "add") == 0) {
                prev_add_stage_1.wb_value = prev_add_stage_1.value_1 + prev_add_stage_1.value_2;
            }
            else if (strcmp(prev_add_stage_1.opcode, "sub") == 0) {
                prev_add_stage_1.wb_value = prev_add_stage_1.value_1 - prev_add_stage_1.value_2;
            }
            else if (strcmp(prev_add_stage_1.opcode, "set") == 0) {
                register_address = prev_add_stage_1.operand_1;

                if (strstr(prev_add_stage_1.operand_1, "#")) {
                    register_address += 1;
                    addr = atoi(register_address);
                    value = addr;   //use the value given
                }
                else {
                    register_address += 1;
                    addr = atoi(register_address);
                    value = cpu->regs[addr].value;
                }
                prev_add_stage_1.wb_value = value;
            }

            register_address = prev_add_stage_1.org_operand_1;
            if (strstr(register_address, "R")){
                register_address += 1;
                addr = atoi(register_address);
                cpu->regs[addr].arith_done = 1;
            }
            if (prev_add_stage_1.num_var == 5) {
                register_address = prev_add_stage_1.org_operand_2;
                if (strstr(register_address, "R")){
                    register_address += 1;
                    addr = atoi(register_address);
                    cpu->regs[addr].arith_done = 1;
                }
            }
            register_address = prev_add_stage_1.org_register_addr;
            if (strstr(register_address, "R")){
                register_address += 1;
                addr = atoi(register_address);
                cpu->regs[addr].arith_done = 1;
                printf("cpu->regs[addr].arith_done %d, addr %d\n", cpu->regs[addr].arith_done, addr);
            }

			// forwarding from add to rs queue
            for (int rs_idx = rs_queue.front; rs_idx != rs_queue.rear; rs_idx = (rs_idx + 1) % (RS_SIZE + 1)) {
    			if (strcmp(prev_add_stage_1.opcode, "st")) {
                    if (strcmp(rs_queue.instructions[rs_idx].org_operand_1, prev_add_stage_1.org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].value_1 = prev_add_stage_1.wb_value;
                        
                    }
                    if (rs_queue.instructions[rs_idx].num_var == 5 && strcmp(rs_queue.instructions[rs_idx].org_operand_2, prev_add_stage_1.org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].value_2 = prev_add_stage_1.wb_value;

                    }
                    if (strcmp(rs_queue.instructions[rs_idx].org_register_addr, prev_add_stage_1.org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].wb_value = prev_add_stage_1.wb_value;
                    
                    }
                }
    			if (strcmp(prev_add_stage_1.opcode, "st") == 0) {
				    if (strcmp(rs_queue.instructions[rs_idx].org_register_addr, prev_add_stage_1.org_register_addr) == 0) {
					    rs_queue.instructions[rs_idx].wb_value = prev_add_stage_1.wb_value;
				    }
                }
			}
		// }
		// else {
		// 	strcpy(prev_add_stage_1.instruction, "");
		// }

            if (!local_stall) {
                prev_add_stage_1.dependency = false;
            }
            else {
                prev_add_stage_1.dependency = true;
            }
            curr_id_struct_arith[0] = prev_add_stage_1;

            printf("prev_add_stage_1.instruction %s\n", prev_add_stage_1.instruction);
            printf("                                       ADD            : %s %d %d %d %d\n", prev_add_stage_1.instruction, prev_add_stage_1.dependency, prev_add_stage_1.value_1, prev_add_stage_1.value_2, prev_add_stage_1.wb_value);
        }
    }

	return prev_add_stage_1;
}


int multiplier_stage_1(CPU *cpu) {

    prev_mul_stage_2 = curr_mul_stage_2;
	curr_mul_stage_2 = prev_mul_stage_1;

	printf("                                       Mul 1           : %s %d %d %d\n", prev_mul_stage_1.instruction, prev_mul_stage_1.value_1, prev_mul_stage_1.value_2, prev_mul_stage_1.wb_value);

	return 0;
}


decoded_instruction multiplier_stage_2(CPU *cpu) {
	int addr;
	char *register_address;
	int value;
	bool local_stall = false;

	// prev_mul_stage_2 = curr_mul_stage_2;
	// curr_mul_stage_2 = prev_mul_stage_1;

	if (squash_instrutions) {
		curr_id_struct_mul = empty_instruction;
		prev_id_struct_mul = empty_instruction;
	}

    decoded_instruction mul_instruction;
	// if (strlen(prev_id_struct_add.instruction) && strcmp(prev_id_struct_mul.opcode, "ret")) {
	// 	if (!prev_id_struct_add.dependency) {
    if (strlen(prev_mul_stage_2.instruction) && prev_mul_stage_2.timestamp < cpu->clock_cycle) {
        if (strcmp(prev_mul_stage_2.opcode, "mul") == 0) {
            prev_mul_stage_2.wb_value = prev_mul_stage_2.value_1 * prev_mul_stage_2.value_2;

			// forwarding from mul to rr stage instruction
            
            for (int rs_idx = rs_queue.front; rs_idx != rs_queue.rear; rs_idx = (rs_idx + 1) % (RS_SIZE + 1)) {

                if (strcmp(prev_mul_stage_2.opcode, "st")) {
                    if (strcmp(rs_queue.instructions[rs_idx].org_operand_1, prev_mul_stage_2.org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].value_1 = prev_mul_stage_2.wb_value;
                    }
                    if (rs_queue.instructions[rs_idx].num_var == 5 && strcmp(rs_queue.instructions[rs_idx].org_operand_2, prev_mul_stage_2.org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].value_2 = prev_mul_stage_2.wb_value;
                    }
                    if (strcmp(rs_queue.instructions[rs_idx].org_register_addr, prev_mul_stage_2.org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].wb_value = prev_mul_stage_2.wb_value;
                    }
                }
                if (strcmp(prev_mul_stage_2.opcode, "st") == 0) {
                    if (strcmp(rs_queue.instructions[rs_idx].org_register_addr, prev_mul_stage_2.org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].wb_value = prev_mul_stage_2.wb_value;
                    }   
                }
            }

            register_address = prev_mul_stage_2.org_operand_1;
            if (strstr(register_address, "R")){
                register_address += 1;
                addr = atoi(register_address);
                cpu->regs[addr].arith_done = 1;
            }
            if (prev_mul_stage_2.num_var == 5) {
                register_address = prev_mul_stage_2.org_operand_2;
                if (strstr(register_address, "R")){
                    register_address += 1;
                    addr = atoi(register_address);
                    cpu->regs[addr].arith_done = 1;
                }
            }
            register_address = prev_mul_stage_2.org_register_addr;
            if (strstr(register_address, "R")){
                register_address += 1;
                addr = atoi(register_address);
                cpu->regs[addr].arith_done = 1;
                printf("cpu->regs[addr].arith_done %d, addr %d\n", cpu->regs[addr].arith_done, addr);
            }

            curr_id_struct_arith[1] = prev_mul_stage_2;
            printf("                                       MUL 2            : %s %d %d %d\n", prev_mul_stage_2.instruction, prev_mul_stage_2.value_1, prev_mul_stage_2.value_2, prev_mul_stage_2.wb_value);
        }
    }

	return prev_mul_stage_2;
}


int division_stage_1(CPU *cpu) {

    prev_div_stage_2 = curr_div_stage_2;
	curr_div_stage_2 = prev_div_stage_1;

	printf("                                       Mul 1           : %s %d %d %d\n", curr_div_stage_2.instruction, curr_div_stage_2.value_1, curr_div_stage_2.value_2, curr_div_stage_2.wb_value);

	return 0;
}


int division_stage_2(CPU *cpu) {

    prev_div_stage_3 = curr_div_stage_3;
	curr_div_stage_3 = prev_div_stage_2;

	printf("                                       Mul 1           : %s %d %d %d\n", curr_div_stage_2.instruction, curr_div_stage_2.value_1, curr_div_stage_2.value_2, curr_div_stage_2.wb_value);

	return 0;
}


decoded_instruction division_stage_3(CPU *cpu) {
	int addr;
	char *register_address;
	int value;
	bool local_stall = false;

	if (squash_instrutions) {
		prev_id_struct_div = empty_instruction;
		curr_id_struct_div = empty_instruction;
	}

    decoded_instruction div_instruction;
	// if (strlen(prev_id_struct_mul.instruction)  && strcmp(prev_id_struct_rr.opcode, "ret")) {
	// 	if (!prev_id_struct_mul.dependency) {
    if (strlen(curr_div_stage_3.instruction) && curr_div_stage_3.timestamp < cpu->clock_cycle) {
        if (strcmp(curr_div_stage_3.opcode, "div") == 0) {
            curr_div_stage_3.wb_value = curr_div_stage_3.value_1 / curr_div_stage_3.value_2;

			// forwarding from div to rr
            for (int rs_idx = rs_queue.front; rs_idx != rs_queue.rear; rs_idx = (rs_idx + 1) % (RS_SIZE + 1)) {
            
                if (strcmp(curr_div_stage_3.opcode, "st")) {
                    if (strcmp(rs_queue.instructions[rs_idx].org_operand_1, curr_div_stage_3.org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].value_1 = curr_div_stage_3.wb_value;
                    }
                    if (rs_queue.instructions[rs_idx].num_var == 5 && strcmp(rs_queue.instructions[rs_idx].org_operand_2, curr_div_stage_3.org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].value_2 = curr_div_stage_3.wb_value;
                    }
                    if (strcmp(rs_queue.instructions[rs_idx].org_register_addr, curr_div_stage_3.org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].wb_value = curr_div_stage_3.wb_value;
                    }
                }
                if (strcmp(curr_div_stage_3.opcode, "st") == 0) {
                    if (strcmp(rs_queue.instructions[rs_idx].org_register_addr, curr_div_stage_3.org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].wb_value = curr_div_stage_3.wb_value;
                    }   
                }
            }

            register_address = curr_div_stage_3.org_operand_1;
            if (strstr(register_address, "R")){
                register_address += 1;
                addr = atoi(register_address);
                cpu->regs[addr].arith_done = 1;
            }
            if (curr_div_stage_3.num_var == 5) {
                register_address = curr_div_stage_3.org_operand_2;
                if (strstr(register_address, "R")){
                    register_address += 1;
                    addr = atoi(register_address);
                    cpu->regs[addr].arith_done = 1;
                }
            }
            register_address = curr_div_stage_3.org_register_addr;
            if (strstr(register_address, "R")){
                register_address += 1;
                addr = atoi(register_address);
                cpu->regs[addr].arith_done = 1;
                printf("cpu->regs[addr].arith_done %d, addr %d\n", cpu->regs[addr].arith_done, addr);
            }

            curr_id_struct_arith[2] = curr_div_stage_3;
            printf("curr_id_struct_rr %s\n", curr_id_struct_rr.instruction);
            printf("                                       DIV            : %s %d %d %d %d\n", curr_div_stage_3.instruction, curr_div_stage_3.value_1, curr_div_stage_3.value_2, curr_div_stage_3.wb_value, curr_div_stage_3.dependency);
        }
	}

	return curr_div_stage_3;
}


int branch_without_prediction(CPU *cpu, decoded_instruction branch_instruction) {
    int value;
	int branch_taken = 0;

	char *register_addr;
	char *next_instruction_addr;

    register_addr = branch_instruction.register_addr;
    register_addr += 1;
    value = branch_instruction.wb_value;

    printf("branch_instruction.wb_value %d\n", branch_instruction.wb_value);

    if (strcmp(branch_instruction.opcode, "bez") == 0) {            
        if (value == 0) {
            branch_taken = 1;
        }
    }

    else if (strcmp(branch_instruction.opcode, "blez") == 0) {
        if (value <= 0) {
            branch_taken = 1;
        }
    }

    else if (strcmp(branch_instruction.opcode, "bltz") == 0) {
        if (value < 0) {
            branch_taken = 1;
        }
    }

    else if (strcmp(branch_instruction.opcode, "bgez") == 0) {
        if (value >= 0) {
            branch_taken = 1;
            printf("here\n");
        }
    }

    else if (strcmp(branch_instruction.opcode, "bgtz") == 0) {
        if (value > 0) {
            branch_taken = 1;
        }
    }


    if (strstr(branch_instruction.operand_1, "#")) {
        next_instruction_addr = branch_instruction.operand_1;
        next_instruction_addr += 1;
    }

    if (branch_taken) {
        program_counter = atoi(next_instruction_addr)/4;
        squash_instrutions = 1;
        stall = 0;
    }
    
    printf("**************\n\n");
	printf("                                       BR             : %s %d %d %d\n", branch_instruction.instruction, branch_instruction.value_1, branch_instruction.value_2, branch_instruction.wb_value);

    return 0;
}


int branch_with_prediction(CPU *cpu, decoded_instruction branch_instruction) {
	int value;
	int branch_taken = 0;

	char *register_addr;
	char *next_instruction_addr;

	squash_instrutions = 0;

	// if (strlen(prev_id_struct_div.instruction) != 0 && !prev_id_struct_div.dependency) {
	// 	curr_id_struct_br = prev_id_struct_div;
		// if (strcmp(prev_id_struct_br.opcode, "ret")) {

			// if (strstr(branch_instruction.opcode, "bez") || strstr(branch_instruction.opcode, "blez") || strstr(branch_instruction.opcode, "bltz") || 
			// 	strstr(branch_instruction.opcode, "bgez") || strstr(branch_instruction.opcode, "bgtz") || strstr(branch_instruction.opcode, "bez")) {

				register_addr = branch_instruction.register_addr;
				register_addr += 1;
				value = branch_instruction.wb_value;

				if (strcmp(branch_instruction.opcode, "bez") == 0) {            
					if (value == 0) {
						branch_taken = 1;
					}
				}

				else if (strcmp(branch_instruction.opcode, "blez") == 0) {
					if (value <= 0) {
						branch_taken = 1;
					}
				}

				else if (strcmp(branch_instruction.opcode, "bltz") == 0) {
					if (value < 0) {
						branch_taken = 1;
					}
				}

				else if (strcmp(branch_instruction.opcode, "bgez") == 0) {
					if (value >= 0) {
						branch_taken = 1;
						printf("here\n");
					}
				}

				else if (strcmp(branch_instruction.opcode, "bgtz") == 0) {
					if (value > 0) {
						branch_taken = 1;
					}
				}


				if (strstr(branch_instruction.operand_1, "#")) {
					next_instruction_addr = branch_instruction.operand_1;
					next_instruction_addr += 1;
				}

				cpu->btb[branch_instruction.addr % BTB_SIZE].target = atoi(next_instruction_addr)/4;   // update target value in btb
				cpu->btb[branch_instruction.addr % BTB_SIZE].tag = 0;

				if (branch_taken) {

					if (cpu->predict_tb[branch_instruction.addr % BTB_SIZE].pattern < 4) {
						program_counter = atoi(next_instruction_addr)/4;
						squash_instrutions = 1;
						stall = 0;
					}

					cpu->predict_tb[branch_instruction.addr % BTB_SIZE].pattern += 1;   // update pattern value in prediction table

				}
				else {      // after predicted that the branch will be taken, if branch is not taken then we have to squash all the instructions taken 
							//until now and update the pc to next instruction that is there after branch
					if (cpu->predict_tb[branch_instruction.addr % BTB_SIZE].pattern >= 4) {
						squash_instrutions = 1;
						program_counter = branch_instruction.addr + 1;

					}
					if (cpu->predict_tb[branch_instruction.addr % BTB_SIZE].pattern > 0) {
						cpu->predict_tb[branch_instruction.addr % BTB_SIZE].pattern -= 1;   // update pattern value in prediction table
					}
				}
			// }
	// 	}
	// }
	// else {
	// 	strcpy(branch_instruction.instruction, "");
	// }
    printf("**************\n\n");
	printf("                                       BR             : %s %d %d %d\n", branch_instruction.instruction, branch_instruction.value_1, branch_instruction.value_2, branch_instruction.wb_value);

	return 0;
}


int memory_1(CPU *cpu) {
	
    prev_mem_stage_2 = curr_mem_stage_2;
	curr_mem_stage_2 = prev_mem_stage_1;

	printf("                                       Mem1           : %s %d %d %d\n", curr_id_struct_mem1.instruction, curr_id_struct_mem1.value_1, curr_id_struct_mem1.value_2, curr_id_struct_mem1.wb_value);

	return 0;
}

int memory_2(CPU *cpu) {
	prev_mem_stage_3 = curr_mem_stage_3;
	curr_mem_stage_3 = prev_mem_stage_2;

	printf("                                       Mem1           : %s %d %d %d\n", curr_id_struct_mem1.instruction, curr_id_struct_mem1.value_1, curr_id_struct_mem1.value_2, curr_id_struct_mem1.wb_value);

	return 0;
}

int memory_3(CPU *cpu) {
	prev_mem_stage_4 = curr_mem_stage_4;
	curr_mem_stage_4 = prev_mem_stage_3;

	printf("                                       Mem1           : %s %d %d %d\n", curr_id_struct_mem1.instruction, curr_id_struct_mem1.value_1, curr_id_struct_mem1.value_2, curr_id_struct_mem1.wb_value);

	return 0;
}


decoded_instruction memory_4(CPU *cpu) {
	int addr;
	char *register_address;
	char *register_number;
	int value;

	// prev_id_struct_mem2 = curr_id_struct_mem2;

	// if (strcmp(prev_id_struct_mem2.opcode, "ret") == 0) {
	// 	return 0;
	// }

	// if (strlen(prev_id_struct_mem1.instruction) != 0 && !prev_id_struct_mem1.dependency) {
		// curr_id_struct_mem2 = prev_id_struct_mem1;

    decoded_instruction mem_instruction;

    if (strlen(curr_mem_stage_4.instruction)) {
        if (strcmp(curr_mem_stage_4.opcode, "ld") == 0) {    //Load instruction
            register_address = curr_mem_stage_4.operand_1;

            if (strstr(curr_mem_stage_4.operand_1, "#")) {
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
                if (strstr(register_address, "ROB")) {
                    register_address += 3;
                    addr = rob_queue.buffer[atoi(register_address)].dest;
                }
                else {
                    register_address += 1;
                    addr = atoi(register_address);
                }
                int idx = (curr_mem_stage_4.value_1)/4;
                value = memory_map[idx];
            }
            curr_mem_stage_4.wb_value = value;
            curr_id_struct_arith[3] = curr_mem_stage_4;
        }
        else if (strcmp(curr_mem_stage_4.opcode, "st") == 0) {   //store instruction 
            register_number = curr_mem_stage_4.register_addr;
            if (strstr(register_number, "ROB")) {
                    register_number += 3;
                }
                else {
                    register_number += 1;
                }

            addr = atoi(register_number);
            curr_id_struct_arith[3] = curr_mem_stage_4;
        }

        /* forwarding from mem stage to reservation station */
        for (int rs_idx = rs_queue.front; rs_idx != rs_queue.rear; rs_idx = (rs_idx + 1) % (RS_SIZE + 1)) {
            if (strcmp(rs_queue.instructions[rs_idx].org_register_addr, curr_mem_stage_4.org_register_addr) == 0) {
                rs_queue.instructions[rs_idx].wb_value = curr_mem_stage_4.wb_value;
            }
        }

        register_address = curr_mem_stage_4.org_operand_1;
        if (strstr(register_address, "R")){
            register_address += 1;
            addr = atoi(register_address);
            cpu->regs[addr].arith_done = 1;
        }
        if (curr_mem_stage_4.num_var == 5) {
            register_address = curr_mem_stage_4.org_operand_2;
            if (strstr(register_address, "R")){
                register_address += 1;
                addr = atoi(register_address);
                cpu->regs[addr].arith_done = 1;
            }
        }
        register_address = curr_mem_stage_4.org_register_addr;
        if (strstr(register_address, "R")){
            register_address += 1;
            addr = atoi(register_address);
            cpu->regs[addr].arith_done = 1;
            printf("cpu->regs[addr].arith_done %d, addr %d\n", cpu->regs[addr].arith_done, addr);
        }

	// else {
	// 	strcpy(curr_mem_stage_4.instruction, "");
	// }
        printf("                                       Mem2           : %s %d %d %d\n", curr_mem_stage_4.instruction, curr_mem_stage_4.value_1, curr_mem_stage_4.value_2, curr_mem_stage_4.wb_value);
	}

	return curr_mem_stage_4;
}


int write_back(CPU *cpu) {
	char *register_number;
	int addr;

    for (int i = 0; i < RS_SIZE; i++) {
        prev_id_struct_wb[i] = curr_id_struct_wb[i];
        curr_id_struct_wb[i] = prev_id_struct_arith[i];
    }

    for (int j = 0; j < RS_SIZE; j++) {         // loop through the output array of arithmetic ops
        if (strlen(curr_id_struct_wb[j].instruction)) {
            register_number = curr_id_struct_wb[j].register_addr;
            printf("register_number %s\n", register_number);
            if (strstr(register_number, "ROB")) {
                register_number += 3;
            }
            else {
                register_number += 1;
            }

            addr = atoi(register_number);
            printf("wr addr %d\n", addr);
            for (int i = rob_queue.front; i != rob_queue.rear; i = (i + 1) % (ROB_SIZE)) {     // loop through rob buffer and update it

                if (i == addr) {
                    rob_queue.buffer[i].completed = 1;
                    rob_queue.buffer[i].result = curr_id_struct_wb[j].wb_value;     // update the register value in ROB buffer

                    // /* Loop through RS queue, check if any operand matches with the write back instruction, update it's value */
                    // for (int rs_idx = 0; rs_idx < RS_SIZE; rs_idx++) {
                    //     if (strlen(curr_id_struct_is[rs_idx].instruction)) {
                    //         printf("curr_id_struct_is[rs_idx].operand_1 %s\n", curr_id_struct_is[rs_idx].instruction);
                    //         register_number = curr_id_struct_is[rs_idx].operand_1;
                    //         if (strstr(register_number, "R")) {
                    //             register_number += 1;
                    //             addr = atoi(register_number);
                    //             if (rob_queue.buffer[i].dest == addr) {
                    //                 curr_id_struct_is[rs_idx].value_1 = rob_queue.buffer[i].result;
                    //             }
                    //         }
                    //         if (curr_id_struct_is[rs_idx].num_var == 5) {
                    //             register_number = curr_id_struct_is[rs_idx].operand_2;
                    //             if (strstr(register_number, "R")) {
                    //                 register_number += 1;
                    //                 addr = atoi(register_number);
                    //                 if (rob_queue.buffer[i].dest == addr) {
                    //                     curr_id_struct_is[rs_idx].value_2 = rob_queue.buffer[i].result;
                    //                 }
                    //             }
                    //         }
                    //     }
                    // }

                    /* 
                    if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_wb[j].register_addr) == 0) {
                        if (curr_id_struct_rr.dependency) {
                            curr_id_struct_rr.dependency = false;
                            rs_enqueue(curr_id_struct_rr);
                            printf("1 @@@@@@@@@@@@@@@ %s\n", curr_id_struct_rr.opcode);

                            // execute the branch instruction that is present in RR stage 
                            if (strstr(curr_id_struct_rr.opcode, "bez") || strstr(curr_id_struct_rr.opcode, "blez") || strstr(curr_id_struct_rr.opcode, "bltz") || 
                                strstr(curr_id_struct_rr.opcode, "bgez") || strstr(curr_id_struct_rr.opcode, "bgtz") || strstr(curr_id_struct_rr.opcode, "bez")) {
                                curr_id_struct_rr.wb_value = curr_id_struct_wb[j].wb_value;
                                printf("@@@@@@@@@@@@@@@ %s %d\n", curr_id_struct_rr.opcode, curr_id_struct_rr.wb_value);
                                branch_without_prediction(cpu, curr_id_struct_rr);

                                stall = false;

                                rob_struct rob_data;
                                rob_data.dest = -1;
                                rob_data.result = -1;
                                rob_data.e = 0;
                                rob_data.completed = 0;
                                rob_enqueue(rob_data);
                            }
                        }
                    }
                    */

                }

            }
            
            // forwarding WR to RS queue
            for (int rs_idx = rs_queue.front; rs_idx != rs_queue.rear; rs_idx = (rs_idx + 1) % (RS_SIZE + 1)) {
                if (strcmp(curr_id_struct_wb[j].opcode, "st")) {
                    if (strcmp(rs_queue.instructions[rs_idx].org_operand_1, curr_id_struct_wb[j].org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].value_1 = curr_id_struct_wb[j].wb_value;
                    }
                    if (rs_queue.instructions[rs_idx].num_var == 5 && strcmp(rs_queue.instructions[rs_idx].org_operand_2, curr_id_struct_wb[j].org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].value_2 = curr_id_struct_wb[j].wb_value;
                    }
                    if (strcmp(rs_queue.instructions[rs_idx].org_register_addr, curr_id_struct_wb[j].org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].wb_value = curr_id_struct_wb[j].wb_value;
                    }
                }
                
                if (strcmp(curr_id_struct_wb[j].opcode, "st") == 0) {
                    if (strcmp(rs_queue.instructions[rs_idx].org_register_addr, curr_id_struct_wb[j].org_register_addr) == 0) {
                        rs_queue.instructions[rs_idx].wb_value = curr_id_struct_wb[j].wb_value;
                    }   
                }
            }

            printf("                                       ");
            printf("WB             : %s %d           	| ", curr_id_struct_wb[j].instruction, curr_id_struct_wb[j].wb_value);
        }
        printf("\n");
    }
}


int retire_stage(CPU *cpu) {
	char *register_addr;
	int addr;
    int re_counter_flag = 0;
    int break_re = 0;
    int re_dequeue_counter = 0;

    for (int i = rob_queue.front; i != rob_queue.rear; i = (i + 1) % (ROB_SIZE)) {        // Loop over ROB buffer
        printf("i %d\n", i);
        if (break_re) {
            break;
        }
        for (int j = 0; j < RS_SIZE; j++) {     // Loop over write back stage output list
            if (strlen(prev_id_struct_wb[j].instruction)) {
                if (strcmp(prev_id_struct_wb[j].opcode, "ret") == 0) {
                    break_loop = true;
                }

                register_addr = prev_id_struct_wb[j].register_addr;
                printf("register_addr %s\n", register_addr);
                if (strstr(register_addr, "R")) {
                    if (strstr(register_addr, "ROB")) {
                        register_addr += 3;
                    }
                    else {                                
                        register_addr += 1;
                    }
                }

                addr = atoi(register_addr);

                printf("Completed %d dest address %d addr %d\n", rob_queue.buffer[i].completed, rob_queue.buffer[i].dest, addr);
                if (rob_queue.buffer[i].completed && i == addr) {
                    if (re_counter_flag == 2) {          // RE limit is 2
                        break_re = 1;               // Flag to break from outer loop
                        break;
                    }

                    /* Update the register values if the instruction is not branch*/
                    if (strcmp(prev_id_struct_wb[j].opcode, "bez") && strcmp(prev_id_struct_wb[j].opcode, "blez") && strcmp(prev_id_struct_wb[j].opcode, "bltz") && 
                        strcmp(prev_id_struct_wb[j].opcode, "bgez") && strcmp(prev_id_struct_wb[j].opcode, "bgtz") && strcmp(prev_id_struct_wb[j].opcode, "bez")) {
                        if (strcmp(prev_id_struct_wb[j].opcode, "ret") == 0) {
                            break_loop = true;
                        }
                        else if (strcmp(prev_id_struct_wb[j].opcode, "st") == 0) {
                            register_addr = prev_id_struct_wb[j].operand_1;
                            if (strstr(prev_id_struct_wb[j].operand_1, "R")) {
                                // if (strstr(register_addr, "ROB")) {
                                //     register_addr += 3;
                                //     // register_addr = rob_queue.buffer[atoi(register_addr)].dest;
                                //     addr = cpu->regs[rob_queue.buffer[atoi(register_addr)].dest].value;   // address found in the given register
                                // }
                                // else {
                                    register_addr += 1;
                                    // register_addr = atoi(register_addr);
                                    addr = cpu->regs[atoi(register_addr)].value;   // address found in the given register
                                // }
                                // cpu->regs[register_addr].is_writing = false;
                            }
                            else {
                                register_addr += 1;
                                addr = atoi(register_addr);
                            }
                            memory_map[addr/4] = rob_queue.buffer[i].result;
                        }
                        else {
                            register_addr = prev_id_struct_wb[j].register_addr;
                            printf("prev_id_struct_wb[j].register_addr %s\n", prev_id_struct_wb[j].register_addr);
                            if (strstr(prev_id_struct_wb[j].register_addr, "R")) {
                                if (strstr(register_addr, "ROB")) {
                                    register_addr += 3;
                                    // register_addr = rob_queue.buffer[atoi(register_addr)].dest;
                                    cpu->regs[rob_queue.buffer[atoi(register_addr)].dest].value = rob_queue.buffer[i].result;
                                    cpu->regs[rob_queue.buffer[atoi(register_addr)].dest].is_valid = 1;
                                }
                                else {
                                    register_addr += 1;
                                    cpu->regs[atoi(register_addr)].value = rob_queue.buffer[i].result;
                                    cpu->regs[atoi(register_addr)].is_valid = 1;
                                }
                                // cpu->regs[register_addr].is_writing = false;
                            }
                        }
                    }
                    printf("*************\n\n\nFirest increment\n");
                    re_dequeue_counter++;
                    fetched_instructions++;
                    re_counter_flag++;
                    printf("                                       ");
                    printf("RE             : %s %d           	| \n", prev_id_struct_wb[j].instruction, prev_id_struct_wb[j].wb_value);
                    break;
                }
                
                /* if it is a branch instruction then dequeue from ROB with default values */
                if (strstr(prev_id_struct_wb[j].opcode, "bez") || strstr(prev_id_struct_wb[j].opcode, "blez") || strstr(prev_id_struct_wb[j].opcode, "bltz") || 
                    strstr(prev_id_struct_wb[j].opcode, "bgez") || strstr(prev_id_struct_wb[j].opcode, "bgtz") || strstr(prev_id_struct_wb[j].opcode, "bez")) {
                    re_dequeue_counter++;
                    
                    printf("                                       ");
                    printf("RE             : %s %d           	| \n", prev_id_struct_wb[j].instruction, prev_id_struct_wb[j].wb_value);
                    
                }
                prev_id_struct_wb[j] = empty_instruction;
            }
        }
    }
    printf("\n");

    printf("re_dequeue_counter %d\n", re_dequeue_counter);
    int temp_counter = 0;
    for (int i = rob_queue.front; i != rob_queue.rear; i = (i + 1) % (ROB_SIZE)) {        // Loop over ROB buffer
        if (temp_counter == re_dequeue_counter) {
            break;
        }
        /* Update the ROB buffer values only when instruction is not branch */
        rob_queue.buffer[i].dest = -1;
        rob_queue.buffer[i].result = -1;
        rob_queue.buffer[i].e = 0;
        rob_queue.buffer[i].completed = 0;

        rob_dequeue();

        temp_counter++;
    }
    
	return 0;
}



// TODO: figure out how to add to queue in IR stage and read this instruction in IS stage, but still read upto 
//       4 instructions here - solved using timestamps

// TODO: while renaming check if the given register address has complete 1 in rob buffer, if yes then given the next name else it will
//       renamed with same value - solved

// TDOD: update completed to 1 and result in ROB table in WR stage
//       dequeu from rob in RE stage

// TODO: update regiters once the instruction is in RE stage



// TODO: check store instruction

// TODO: forwarding IDK what to do
