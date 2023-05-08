
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

int memory_map[16384];
char instruction_set[MAX_INSTRUCTION_COUNT][MAX_INSTRUCTION_LENGTH];
int instruction_count = 0;
int fetched_instructions = 0;
int program_counter = 0;
int stalled_cycles = 0;
int squash_instrutions = 0;
int rob_stall = 0;
int rs_stall = 0;

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
        printf("00%d: %s %s %d %d \n", rs_queue.instructions[i].addr*4, rs_queue.instructions[i].opcode, rs_queue.instructions[i].register_addr, rs_queue.instructions[i].value_1, rs_queue.instructions[i].value_2);
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
    printf("R# [(status 0=invalid, 1=valid), tag, value]\n");
	for (int reg=0; reg<REG_COUNT; reg++) {
        printf("R%d [(%d), %d, %d]\n", reg, cpu->regs[reg].is_valid, cpu->regs[reg].tag, cpu->regs[reg].value);
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
		// add_stage(cpu);
		// multiplier_stage(cpu);
		// division_stage(cpu);
		// branch(cpu);
		// memory_1(cpu);
		// memory_2(cpu);
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
        
        if (cpu->clock_cycle == 40)         // For DEBUG
            exit(0);
	}

	write_memory_map(filename);

	print_registers(cpu);
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
	// char base_dir[250] = "./output_memory/";
	// DIR* output_memory_dir = opendir("./output_memory");

	// if (output_memory_dir) {
	//     closedir(output_memory_dir);
	// }
	// else if (ENOENT == errno) {
	//     mkdir("./output_memory", 0777);
	// }

	// if (strstr(filename, "1")) {
	//     strcpy(op_filename, "output_memory_1.txt");
	// }
	// else if (strstr(filename, "2")) {
	//     strcpy(op_filename, "output_memory_2.txt");
	// }
	// else if (strstr(filename, "3")) {
	//     strcpy(op_filename, "output_memory_3.txt");
	// }
	// else if (strstr(filename, "4")) {
	//     strcpy(op_filename, "output_memory_4.txt");
	// }

	// strcat(base_dir, op_filename);

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

			strcpy(curr_id_struct_decode.operand_1, decoded_instruction[3]);

			if (counter == 5) {
				strcpy(curr_id_struct_decode.operand_2, decoded_instruction[4]);
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

	prev_id_struct_rr = curr_id_struct_rr;

	// if (squash_instrutions) {
	// 	curr_id_struct_rr = empty_instruction;
	// 	prev_id_struct_rr = empty_instruction;
	// }


        printf("****stall IR %d\n", stall);
		if (!curr_id_struct_rr.dependency) {
			curr_id_struct_rr = prev_id_struct_ia;
			if (strcmp(prev_id_struct_rr.opcode, "ret") == 0) {
				return 0;
			}
		}
	if (strlen(prev_id_struct_ia.instruction) != 0) {

        /* Renaming registers */
        
        /* checking if the current register is in use in ROB buffer */
        register_address = curr_id_struct_rr.register_addr;
        printf("curr_id_struct_rr %s %s\n", curr_id_struct_rr.instruction, register_address);

        if (strstr(register_address, "R") || strstr(register_address, "ROB")) {
            if (strstr(register_address, "ROB")) {
                register_address += 3;
                addr = atoi(register_address);
                for (int i = rob_queue.front; i != rob_queue.rear; i = (i + 1) % ROB_SIZE) {
                    printf("addr %d i %d\n", addr, i);
                    if (i == addr) {         // comparing output register address
                        printf("rob_queue.buffer[i].completed %d\n", rob_queue.buffer[i].completed);
                        if (rob_queue.buffer[i].completed == 0) {
                            strcpy(rename_address, "ROB");
                            sprintf(dest_num, "%d", i);
                            strcat(rename_address, dest_num);
                            strcpy(curr_id_struct_rr.register_addr, rename_address);
                            printf("*****rename_address %s\n", rename_address);

                            register_addr_inuse = true;
                            local_stall = true;
                            break;
                        }
                    }
                }
                printf("local stall 1: %d\n", local_stall);

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
                            printf("**rename_address else case %s\n", rename_address);

                            register_addr_inuse = true;
                            local_stall = true;
                            break;
                        }
                        
                    }
                    if (((i + 1) % ROB_SIZE) == rob_queue.rear ) {
                            strcpy(rename_address, "ROB");
                            sprintf(dest_num, "%d", rob_queue.rear);
                            strcat(rename_address, dest_num);
                            strcpy(curr_id_struct_rr.register_addr, rename_address);
                            printf("**rename_address else if case**** %s\n", rename_address);
                        }
                }
                printf("local stall 2: %d\n", local_stall);

            }
            curr_id_struct_rr.timestamp = cpu->clock_cycle;


            if (!register_addr_inuse) {
                rob_struct rob_data;
                if (strcmp(curr_id_struct_rr.opcode, "bez") && strcmp(curr_id_struct_rr.opcode, "blez") && strcmp(curr_id_struct_rr.opcode, "bltz") && 
                        strcmp(curr_id_struct_rr.opcode, "bgez") && strcmp(curr_id_struct_rr.opcode, "bgtz") && strcmp(curr_id_struct_rr.opcode, "bez")) {
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
            }
        }

        /*We will see this later*/
        /*
        register_address = curr_id_struct_rr.operand_1;
        if (strstr(register_address, "R")) {
            register_address += 1;
            addr = atoi(register_address);

            for (int i = 0; i < ROB_SIZE; i++) {
                if (rob_queue.buffer[i].dest == addr) {         // comparing operand 1 register address
                    if (rob_queue.buffer[i].completed == 0) {
                        strcpy(rename_address, "ROB");
                        sprintf(dest_num, "%d", i);
                        strcat(rename_address, dest_num);
                        strcpy(curr_id_struct_rr.operand_1, rename_address);
                        operand_1_inuse = true;
                        break;
                    }
                }
                if (i == ROB_SIZE - 1) {
                    rob_struct rob_data;
                    rob_data.dest = addr;
                    rob_data.result = -1;
                    rob_data.e = 0;
                    rob_data.completed = 0;
                    if (rob_queue_full()) {
                        rob_stall++;
                        stall = true;
                    }
                    else {
                        printf("enqueue 2\n");
                        rob_enqueue(rob_data);
                    }
                }
            }
        }

        if (curr_id_struct_rr.num_var == 5) {
            register_address = curr_id_struct_rr.operand_2;
            if (strstr(register_address, "R")) {
                register_address += 1;
                addr = atoi(register_address);

                for (int i = 0; i < ROB_SIZE; i++) {
                    if (rob_queue.buffer[i].dest == addr) {             // comparing operand 2 register address
                        if (rob_queue.buffer[i].completed == 0) {
                            strcpy(rename_address, "ROB");
                            sprintf(dest_num, "%d", i);
                            strcat(rename_address, dest_num);
                            strcpy(curr_id_struct_rr.operand_2, rename_address);
                            operand_2_inuse = true;
                            break;
                        }
                    }
                    if (i == ROB_SIZE - 1) {
                        rob_struct rob_data;
                        rob_data.dest = addr;
                        rob_data.result = -1;
                        rob_data.e = 0;
                        rob_data.completed = 0;
                        if (rob_queue_full()) {
                            rob_stall++;
                            stall = true;
                        }
                        else {
                            printf("enqueue 3\n");
                            rob_enqueue(rob_data);
                        }
                    }
                }
            }
        }
        */
        /* end of renaming */


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
        // else {
        //     if (operand_1_inuse || operand_2_inuse) {
        //         local_stall = true;
        //     }
        // }

/*
        if (strlen(prev_id_struct_rr.instruction) && strcmp(prev_id_struct_rr.opcode, "st") != 0) {
            if (strcmp(prev_id_struct_rr.opcode, "bez") && strcmp(prev_id_struct_rr.opcode, "blez") && strcmp(prev_id_struct_rr.opcode, "bltz") && 
                strcmp(prev_id_struct_rr.opcode, "bgez") && strcmp(prev_id_struct_rr.opcode, "bgtz") && strcmp(prev_id_struct_rr.opcode, "bez")) {
            
                if (strcmp(prev_id_struct_rr.opcode, "add") != 0 && strcmp(prev_id_struct_rr.opcode, "sub") != 0 && strcmp(prev_id_struct_rr.opcode, "set") != 0) {

                    if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, prev_id_struct_rr.register_addr) == 0) {
                        local_stall = true;
                    }
                    if (strcmp(curr_id_struct_rr.operand_1, prev_id_struct_rr.register_addr) == 0) {
                        local_stall = true;
                    }
                    if (strcmp(curr_id_struct_rr.register_addr, prev_id_struct_rr.register_addr) == 0 && strcmp(curr_id_struct_rr.opcode, "st") == 0) {
                        local_stall = true;
                    }
                }
            }
        }
        printf("local stall 1: %d\n", local_stall);
        
        // stall if the register addr is same as operand 1 or 2 in mul stage and instruction is not mul
        if (strlen(curr_id_struct_add.instruction) && strcmp(curr_id_struct_add.opcode, "st") != 0) {
            if (strcmp(curr_id_struct_add.opcode, "bez") && strcmp(curr_id_struct_add.opcode, "blez") && strcmp(curr_id_struct_add.opcode, "bltz") && 
                strcmp(curr_id_struct_add.opcode, "bgez") && strcmp(curr_id_struct_add.opcode, "bgtz") && strcmp(curr_id_struct_add.opcode, "bez")) {
                if (strcmp(curr_id_struct_add.opcode, "add") != 0 && strcmp(curr_id_struct_add.opcode, "sub") != 0 && strcmp(curr_id_struct_add.opcode, "set") != 0) {

                    if ((prev_id_struct_rr.dependency == 1 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_rr.operand_1) == 0) || (prev_id_struct_rr.dependency == 1 && strcmp(curr_id_struct_add.opcode, "mul"))
                    || (strcmp(curr_id_struct_rr.operand_2, curr_id_struct_rr.operand_1) && strcmp(curr_id_struct_add.opcode, "mul"))) { 

                        if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_add.register_addr) == 0) {
                            stall = true;
                            local_stall = true;
                        }
                        if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_add.register_addr) == 0) {
                            stall = true;
                            local_stall = true;
                        }
                        if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_add.register_addr) == 0 && strcmp(curr_id_struct_rr.opcode, "st") == 0) {
                            stall = true;
                            local_stall = true;
                        }
                        printf("local stall 2: %d\n", local_stall);
                        
                    }
                }
            }
        }

        // stall if the register addr is same as operand 1 or 2 in div stage anf instruction is not div
        if (strlen(curr_id_struct_mul.instruction) && strcmp(curr_id_struct_mul.opcode, "st") != 0) {
            if (strcmp(curr_id_struct_mul.opcode, "bez") && strcmp(curr_id_struct_mul.opcode, "blez") && strcmp(curr_id_struct_mul.opcode, "bltz") && 
                strcmp(curr_id_struct_mul.opcode, "bgez") && strcmp(curr_id_struct_mul.opcode, "bgtz") && strcmp(curr_id_struct_mul.opcode, "bez")) {
                if (strcmp(curr_id_struct_mul.opcode, "add") != 0 && strcmp(curr_id_struct_mul.opcode, "sub") != 0 && strcmp(curr_id_struct_mul.opcode, "set") != 0 && strcmp(curr_id_struct_mul.opcode, "mul")) {
                    if ((prev_id_struct_rr.dependency == 1 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_rr.operand_1) == 0) || (prev_id_struct_rr.dependency == 1 && strcmp(curr_id_struct_mul.opcode, "div") != 0 )
                    || (strcmp(curr_id_struct_rr.operand_2, curr_id_struct_rr.operand_1) && strcmp(curr_id_struct_mul.opcode, "div"))) {

                        if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_mul.register_addr) == 0) {
                            stall = true;
                            local_stall = true;
                        }
                        if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_mul.register_addr) == 0) {
                            stall = true;
                            local_stall = true;
                        }
                        if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_mul.register_addr) == 0 && strcmp(curr_id_struct_rr.opcode, "st") == 0) {
                            stall = true;
                            local_stall = true;
                        }
                    }
                }
            }
        }
        printf("local stall 3: %d\n", local_stall);


        // stall if the register addr is same as operand 1 or 2 in br stage

        if (strlen(curr_id_struct_div.instruction) && strcmp(curr_id_struct_div.opcode, "st") != 0) {
            // if instruction in br is branch instruction don't stall
            if (strcmp(curr_id_struct_div.opcode, "bez") && strcmp(curr_id_struct_div.opcode, "blez") && strcmp(curr_id_struct_div.opcode, "bltz") && 
                strcmp(curr_id_struct_div.opcode, "bgez") && strcmp(curr_id_struct_div.opcode, "bgtz") && strcmp(curr_id_struct_div.opcode, "bez")) {

                if (curr_id_struct_rr.num_var == 5 && (strcmp(curr_id_struct_rr.operand_2, curr_id_struct_div.register_addr) == 0)) {
                    stall = true;
                    local_stall = true;
                }
                if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_div.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }
                if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_div.register_addr) == 0 && strcmp(curr_id_struct_rr.opcode, "st") == 0) {
                    stall = true;
                    local_stall = true;
                }
            }
        }
        printf("local stall 4: %d\n", local_stall);

        if (strlen(curr_id_struct_br.instruction) && strcmp(curr_id_struct_br.opcode, "st") != 0) {
            
            // if instruction in mem1 is branch instruction don't stall
            if (strcmp(curr_id_struct_br.opcode, "bez") && strcmp(curr_id_struct_br.opcode, "blez") && strcmp(curr_id_struct_br.opcode, "bltz") && 
                strcmp(curr_id_struct_br.opcode, "bgez") && strcmp(curr_id_struct_br.opcode, "bgtz") && strcmp(curr_id_struct_br.opcode, "bez")) {
                // stall if the register addr is same as operand 1 or 2 in mem1 stage
                if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_br.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }
                if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_br.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }
                if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_br.register_addr) == 0 && strcmp(curr_id_struct_rr.opcode, "st") == 0) {
                    stall = true;
                    local_stall = true;
                }
            }
        }
        printf("local stall 5: %d\n", local_stall);

        if (strlen(curr_id_struct_mem1.instruction) && strcmp(curr_id_struct_mem1.opcode, "st") != 0) {
            
            // if instruction in mem2 is branch instruction don't stall
            if (strcmp(curr_id_struct_mem1.opcode, "bez") && strcmp(curr_id_struct_mem1.opcode, "blez") && strcmp(curr_id_struct_mem1.opcode, "bltz") && 
                strcmp(curr_id_struct_mem1.opcode, "bgez") && strcmp(curr_id_struct_mem1.opcode, "bgtz") && strcmp(curr_id_struct_mem1.opcode, "bez")) {
                // stall if the register addr is same as operand 1 or 2 in mem2 stage
                if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_mem1.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }
                if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_mem1.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }
                if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_mem1.register_addr) == 0 && strcmp(curr_id_struct_rr.opcode, "st") == 0) {
                    stall = true;
                    local_stall = true;
                }
            }
        }
        printf("local stall 6: %d\n", local_stall);

        if (strlen(curr_id_struct_mem2.instruction) && strcmp(curr_id_struct_mem2.opcode, "st") != 0) {
            // if instruction in wb is branch instruction don't stall
            if (strcmp(curr_id_struct_mem2.opcode, "bez") && strcmp(curr_id_struct_mem2.opcode, "blez") && strcmp(curr_id_struct_mem2.opcode, "bltz") && 
                strcmp(curr_id_struct_mem2.opcode, "bgez") && strcmp(curr_id_struct_mem2.opcode, "bgtz") && strcmp(curr_id_struct_mem2.opcode, "bez")) {
            
                // stall if the register addr is same as operand 1 or 2 in wb stage
                if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_mem2.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }

                if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_mem2.register_addr) == 0) {
                    stall = true;
                    local_stall = true;
                }
                if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_mem2.register_addr) == 0 && strcmp(curr_id_struct_rr.opcode, "st") == 0) {
                    stall = true;
                    local_stall = true;
                }
            }
        }
        printf("local stall 7: %d\n", local_stall);
*/

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
					// if (strstr(register_address, "ROB")) {
                    //     register_address += 3;
    				// 	addr = rob_queue.buffer[atoi(register_address)].dest;
                    // }
                    // else {
                        register_address += 1;
    					addr = atoi(register_address);
                    // }
                    if (cpu->regs[addr].is_valid) {
    					values[i - 3] = cpu->regs[addr].value;
                    }
                    else {
                        for (int rob_idx = rob_queue.front; rob_idx != rob_queue.rear; rob_idx = (rob_idx + 1) % (ROB_SIZE)) {
                            if (rob_queue.buffer[rob_idx].dest == addr) {
                                values[i - 3] = rob_queue.buffer[rob_idx].result;
                            }
                        }
                    }
				}
			}
		}
		if (!local_stall) {
			curr_id_struct_rr.dependency = false;
            
            if (rs_queue_full()) {
                rs_stall++;
                stall = true;
            }
            else {
                rs_enqueue(curr_id_struct_rr);
            }
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
			
			if (strstr(curr_id_struct_rr.opcode, "bez") || strstr(curr_id_struct_rr.opcode, "blez") || strstr(curr_id_struct_rr.opcode, "bltz") || 
				strstr(curr_id_struct_rr.opcode, "bgez") || strstr(curr_id_struct_rr.opcode, "bgtz") || strstr(curr_id_struct_rr.opcode, "bez")) {

				register_address = curr_id_struct_rr.register_addr;
				register_address++;
				addr = atoi(register_address);
				curr_id_struct_rr.wb_value = cpu->regs[addr].value;
			}
		}
	}


	printf("                                       RR             : %s %d %d %d\n", curr_id_struct_rr.instruction, curr_id_struct_rr.dependency, curr_id_struct_rr.value_1, curr_id_struct_rr.value_2);
	
	return 0;
}


int instruction_issue(CPU *cpu) {

    for (int i = 0; i < RS_SIZE; i++) {
        prev_id_struct_is[i] = curr_id_struct_is[i];
        curr_id_struct_is[i] = empty_instruction;
    }

	// if (strlen(prev_id_struct_rr.instruction) != 0 && !prev_id_struct_rr.dependency) {
        decoded_instruction *rs_instruction;
        for (int i = 0; i < RS_SIZE; i++) {
            rs_instruction = rs_get_instruction();
            if (rs_instruction == NULL){
                break;
            }
            if (rs_instruction->timestamp < cpu->clock_cycle && !rs_instruction->dependency) {     // instructions added before the current cycle
                rs_dequeue();
                curr_id_struct_is[i] = *rs_instruction;
                printf("                                       IS             : %s %d %d %d\n", curr_id_struct_is[i].instruction, curr_id_struct_is[i].dependency, curr_id_struct_is[i].value_1, curr_id_struct_is[i].value_2);
            }
        }
    // }
    // else {
    //     for (int i = 0; i < RS_SIZE; i++) {

    //         curr_id_struct_is[i] = empty_instruction;
    //     };
    // }

    return 0;
}


int arithmetic_operation(CPU *cpu) {
    int counter = 0;
    int add_counter = 0;
    int mul_counter = 0;
    int div_counter = 0;
    int mem_counter = 0;

	int values[2] = {0, 0};
	char operands[2][6];
	int addr;
	char *register_address;

    decoded_instruction result;

    for (int i = 0; i < RS_SIZE; i++) {
        prev_id_struct_arith[i] = curr_id_struct_arith[i];
        curr_id_struct_arith[i] = empty_instruction;
    }

    for (int i = 0; i < RS_SIZE; i++) {
        if (strlen(prev_id_struct_is[i].instruction) != 0) {

            printf("********************\n");
            strcpy(operands[0], prev_id_struct_is[i].operand_1);
			strcpy(operands[1], prev_id_struct_is[i].operand_2);
            printf("%s %s %d %d\n", operands[0], operands[1], prev_id_struct_is[i].value_1, prev_id_struct_is[i].value_2);

			for (int j = 3; j < prev_id_struct_is[i].num_var; j++) {
                printf("\n^^^^^^^\n");
				register_address = operands[j - 3];
                printf("***********register_address %s\n", register_address);
				if (strstr(operands[j - 3], "#")) {
					register_address += 1;
					addr = atoi(register_address);
					if (addr > 999) {
						values[j - 3] = memory_map[(addr)/4];   // Read from memory map file
					}
					else {
						values[j - 3] = addr;   //use the value given
					}
				}
				else {
				// 	if (strstr(register_address, "ROB")) {
                //         register_address += 3;
    			// 		addr = rob_queue.buffer[atoi(register_address)].dest;
                //     }
                //     else {
                        register_address += 1;
    					addr = atoi(register_address);
                    // }
                    printf("addr add %d\n", addr);
					if (cpu->regs[addr].is_valid) {
    					values[j - 3] = cpu->regs[addr].value;
                        // for (int rob_idx = rob_queue.front; rob_idx != rob_queue.rear; rob_idx = (rob_idx + 1) % (ROB_SIZE)) {
                        //     if (rob_queue.buffer[rob_idx].dest == addr) {
                        //         if(rob_queue.buffer[rob_idx].result != values[j - 3]) {
                        //             values[j - 3] = rob_queue.buffer[rob_idx].dest;
                        //         }
                        //     }
                        // }
                        printf("********** should be here\n");
                    }
                    else {
                        for (int rob_idx = rob_queue.front; rob_idx != rob_queue.rear; rob_idx = (rob_idx + 1) % (ROB_SIZE)) {
                            if (rob_queue.buffer[rob_idx].dest == addr) {
                                values[j - 3] = rob_queue.buffer[rob_idx].result;
                            }
                        }
                    }
                    printf("values[j - 3] %d\n", values[j - 3]);
				}
			}
            
			prev_id_struct_is[i].value_1 = values[0];
			prev_id_struct_is[i].value_2 = values[1];

            if (strcmp(prev_id_struct_is[i].opcode, "mul") == 0) {
                if(mul_counter == 2) {
                    if (rs_queue_full()) {
                        rs_stall++;
                        stall = true;
                    }
                    else {
                        rs_enqueue(prev_id_struct_is[i]);
                    }
                    continue;
                }
                result = multiplier_stage(cpu, prev_id_struct_is[i]);
                result.timestamp = cpu->clock_cycle;
                curr_id_struct_arith[i] = result;

                mul_counter++;
            }

            else if (strcmp(prev_id_struct_is[i].opcode, "div") == 0) {
                if(div_counter == 3) {
                    if (rs_queue_full()) {
                        rs_stall++;
                        stall = true;
                    }
                    else {
                        rs_enqueue(prev_id_struct_is[i]);
                    }
                    continue;
                }
                result = division_stage(cpu, prev_id_struct_is[i]);
                result.timestamp = cpu->clock_cycle;
                curr_id_struct_arith[i] = result;

                div_counter++;
            }

            else if (strcmp(prev_id_struct_is[i].opcode, "ld") == 0 || strcmp(prev_id_struct_is[i].opcode, "st") == 0) {
                if(mem_counter == 4) {
                    if (rs_queue_full()) {
                        rs_stall++;
                        stall = true;
                    }
                    else {
                        rs_enqueue(prev_id_struct_is[i]);
                    }
                    continue;
                }
                result = memory_2(cpu, prev_id_struct_is[i]);
                result.timestamp = cpu->clock_cycle;
                curr_id_struct_arith[i] = result;

                mem_counter++;
            }
            
            else {
                if(add_counter == 1) {
                    if (rs_queue_full()) {
                        rs_stall++;
                        stall = true;
                    }
                    else {
                        rs_enqueue(prev_id_struct_is[i]);
                    }
                    continue;
                }
                printf("value1 %d value2 %d \n", prev_id_struct_is[i].value_1, prev_id_struct_is[i].value_2);
                result = add_stage(cpu, prev_id_struct_is[i]);
                result.timestamp = cpu->clock_cycle;
                curr_id_struct_arith[i] = result;

                add_counter++;
            }

            counter++;

        }
        else {
            break;
        }

    }

    return 0;
}


decoded_instruction add_stage(CPU *cpu, decoded_instruction add_instruction) {
	int addr;
	char *register_address;
	int value;
	bool local_stall = false;

	// prev_id_struct_add = curr_id_struct_add;

	if (squash_instrutions) {
		prev_id_struct_add = empty_instruction;
		curr_id_struct_add = empty_instruction;
	}

	// curr_id_struct_add = prev_id_struct_rr;
	// if (strlen(prev_id_struct_rr.instruction) && strcmp(prev_id_struct_add.opcode, "ret")) {

	// 	if (!prev_id_struct_rr.dependency) {
        
    if (strcmp(add_instruction.opcode, "add") == 0) {
        add_instruction.wb_value = add_instruction.value_1 + add_instruction.value_2;
    }
    else if (strcmp(add_instruction.opcode, "sub") == 0) {
        add_instruction.wb_value = add_instruction.value_1 - add_instruction.value_2;
    }
    else if (strcmp(add_instruction.opcode, "set") == 0) {
        register_address = add_instruction.operand_1;

        if (strstr(add_instruction.operand_1, "#")) {
            register_address += 1;
            addr = atoi(register_address);
            value = addr;   //use the value given
        }
        else {
            // if (strstr(register_address, "ROB")) {
            //     register_address += 3;
            //     addr = rob_queue.buffer[atoi(register_address)].dest;
            // }
            // else {
                register_address += 1;
                addr = atoi(register_address);
            // }
            value = cpu->regs[addr].value;
        }
        add_instruction.wb_value = value;
    }
    printf("*****************add value %d\n", value);
						
			// forwarding from add to add
            /*
			if (strlen(add_instruction.instruction) && strcmp(add_instruction.opcode, "st")) {
				
				if (strcmp(curr_id_struct_rr.operand_1, add_instruction.register_addr) == 0) {
					curr_id_struct_rr.value_1 = add_instruction.wb_value;
				}
				if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, add_instruction.register_addr) == 0) {
					curr_id_struct_rr.value_2 = add_instruction.wb_value;
				}
				if (strcmp(curr_id_struct_rr.register_addr, add_instruction.register_addr) == 0) {
					curr_id_struct_rr.wb_value = add_instruction.wb_value;
				}
			}
			if (strcmp(add_instruction.opcode, "st") == 0) {
				if (strcmp(curr_id_struct_rr.register_addr, add_instruction.register_addr) == 0) {
					curr_id_struct_rr.wb_value = add_instruction.wb_value;
				}   
			}
            */
		// }
		// else {
		// 	strcpy(add_instruction.instruction, "");
		// }

		if (!local_stall) {
			add_instruction.dependency = false;
		}
		else {
			add_instruction.dependency = true;
		}
	// }
    printf("add_instruction.instruction %s\n", add_instruction.instruction);
	printf("                                       ADD            : %s %d %d %d %d\n", add_instruction.instruction, add_instruction.dependency, add_instruction.value_1, add_instruction.value_2, add_instruction.wb_value);

	return add_instruction;
}


decoded_instruction multiplier_stage(CPU *cpu, decoded_instruction mul_instruction) {
	int addr;
	char *register_address;
	int value;
	bool local_stall = false;
	prev_id_struct_mul = curr_id_struct_mul;

	curr_id_struct_mul = prev_id_struct_add;

	if (squash_instrutions) {
		curr_id_struct_mul = empty_instruction;
		prev_id_struct_mul = empty_instruction;
	}

	// if (strlen(prev_id_struct_add.instruction) && strcmp(prev_id_struct_mul.opcode, "ret")) {
	// 	if (!prev_id_struct_add.dependency) {
			if (strcmp(mul_instruction.opcode, "mul") == 0) {
				mul_instruction.wb_value = mul_instruction.value_1 * mul_instruction.value_2;
			}
			
			// forwarding from mul to rr stage instruction
            /*
			if (strcmp(mul_instruction.opcode, "st")) {
				if (strcmp(curr_id_struct_rr.operand_1, mul_instruction.register_addr) == 0) {
					curr_id_struct_rr.value_1 = mul_instruction.wb_value;
				}
				if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, mul_instruction.register_addr) == 0) {
					curr_id_struct_rr.value_2 = mul_instruction.wb_value;
				}
				if (strcmp(curr_id_struct_rr.register_addr, mul_instruction.register_addr) == 0) {
					curr_id_struct_rr.wb_value = mul_instruction.wb_value;
				}

				// forwarding from mul to add stage instruction
				if (strcmp(curr_id_struct_add.operand_1, mul_instruction.register_addr) == 0) {
					curr_id_struct_add.value_1 = mul_instruction.wb_value;
				}
				if (curr_id_struct_add.num_var == 5 && strcmp(curr_id_struct_add.operand_2, mul_instruction.register_addr) == 0) {
					curr_id_struct_add.value_2 = mul_instruction.wb_value;
				}
				if (strcmp(curr_id_struct_add.register_addr, mul_instruction.register_addr) == 0 && (strcmp(curr_id_struct_add.opcode, "mul") == 0)) {
					curr_id_struct_add.wb_value = mul_instruction.wb_value;
				}
			}
			if (strcmp(mul_instruction.opcode, "st") == 0) {
				if (strcmp(curr_id_struct_rr.register_addr, mul_instruction.register_addr) == 0) {
					curr_id_struct_rr.wb_value = mul_instruction.wb_value;
				}   
			}
            */
		// }
		// else {
		// 	strcpy(mul_instruction.instruction, "");
		// }

		if (!local_stall) {
			mul_instruction.dependency = false;
		}
		else {
			mul_instruction.dependency = true;
		}
	// }
	printf("                                       MUL            : %s %d %d %d\n", mul_instruction.instruction, mul_instruction.value_1, mul_instruction.value_2, mul_instruction.wb_value);

	return mul_instruction;
}


decoded_instruction division_stage(CPU *cpu, decoded_instruction div_instruction) {
	int addr;
	char *register_address;
	int value;
	bool local_stall = false;
	prev_id_struct_div = curr_id_struct_div;

	curr_id_struct_div = prev_id_struct_mul;

	if (squash_instrutions) {
		prev_id_struct_div = empty_instruction;
		curr_id_struct_div = empty_instruction;
	}


	// if (strlen(prev_id_struct_mul.instruction)  && strcmp(prev_id_struct_rr.opcode, "ret")) {
	// 	if (!prev_id_struct_mul.dependency) {
			if (strcmp(div_instruction.opcode, "div") == 0) {
				div_instruction.wb_value = div_instruction.value_1 / div_instruction.value_2;
			}

			// forwarding from div to rr
            /*
			if (strcmp(div_instruction.opcode, "st")) {
				if (strcmp(curr_id_struct_rr.operand_1, div_instruction.register_addr) == 0) {
					curr_id_struct_rr.value_1 = div_instruction.wb_value;
				}
				if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, div_instruction.register_addr) == 0) {
					curr_id_struct_rr.value_2 = div_instruction.wb_value;
				}
				if (strcmp(curr_id_struct_rr.register_addr, div_instruction.register_addr) == 0) {
					curr_id_struct_rr.wb_value = div_instruction.wb_value;
				}
				
				// forwarding from div to add stage instruction
				if (strcmp(curr_id_struct_add.operand_1, div_instruction.register_addr) == 0) {
				    curr_id_struct_add.value_1 = div_instruction.wb_value;
				}
				if (curr_id_struct_add.num_var == 5 && strcmp(curr_id_struct_add.operand_2, div_instruction.register_addr) == 0) {
				    curr_id_struct_add.value_2 = div_instruction.wb_value;
				}
				if (strcmp(curr_id_struct_add.register_addr, div_instruction.register_addr) == 0 && (strcmp(curr_id_struct_add.opcode, "div") == 0)) {
				    curr_id_struct_add.wb_value = div_instruction.wb_value;
				}

				// forwarding from div to mul stage instruction
				if (strcmp(curr_id_struct_mul.operand_1, div_instruction.register_addr) == 0) {
				    curr_id_struct_mul.value_1 = div_instruction.wb_value;
				}
				if (curr_id_struct_mul.num_var == 5 && strcmp(curr_id_struct_mul.operand_2, div_instruction.register_addr) == 0) {
				    curr_id_struct_mul.value_2 = div_instruction.wb_value;
				}
				if (strcmp(curr_id_struct_mul.register_addr, div_instruction.register_addr) == 0 && (strcmp(curr_id_struct_add.opcode, "div") == 0)) {
				    curr_id_struct_mul.wb_value = div_instruction.wb_value;
				}
			}
			if (strcmp(div_instruction.opcode, "st") == 0) {
				if (strcmp(curr_id_struct_rr.register_addr, div_instruction.register_addr) == 0) {
					curr_id_struct_rr.wb_value = div_instruction.wb_value;
				}   
			}
            */
		// }
		// else {
		// 	strcpy(div_instruction.instruction, "");
		// }

		if (!local_stall) {
			div_instruction.dependency = false;
		}
		else {
			div_instruction.dependency = true;
		}
	// }
    printf("curr_id_struct_rr %s\n", curr_id_struct_rr.instruction);
	printf("                                       DIV            : %s %d %d %d %d\n", div_instruction.instruction, div_instruction.value_1, div_instruction.value_2, div_instruction.wb_value, div_instruction.dependency);

	return div_instruction;
}


int branch_without_prediction(CPU *cpu, decoded_instruction branch_instruction) {
    int value;
	int branch_taken = 0;

	char *register_addr;
	char *next_instruction_addr;

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

    if (branch_taken) {
        program_counter = atoi(next_instruction_addr)/4;
        squash_instrutions = 1;
        stall = 0;
    }
    return 0;
}


int branch_with_prediction(CPU *cpu, decoded_instruction branch_instruction) {
	int value;
	int branch_taken = 0;

	char *register_addr;
	char *next_instruction_addr;

	squash_instrutions = 0;
	prev_id_struct_br = curr_id_struct_br;

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
	printf("                                       BR             : %s %d %d %d\n", branch_instruction.instruction, branch_instruction.value_1, branch_instruction.value_2, branch_instruction.wb_value);

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
	}
	else {
		strcpy(curr_id_struct_mem1.instruction, "");
	}
	printf("                                       Mem1           : %s %d %d %d\n", curr_id_struct_mem1.instruction, curr_id_struct_mem1.value_1, curr_id_struct_mem1.value_2, curr_id_struct_mem1.wb_value);

	return 0;
}


decoded_instruction memory_2(CPU *cpu, decoded_instruction mem_instruction) {
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


    if (strcmp(mem_instruction.opcode, "ld") == 0) {    //Load instruction
        register_address = mem_instruction.operand_1;

        if (strstr(mem_instruction.operand_1, "#")) {
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
            int idx = (mem_instruction.value_1)/4;
            value = memory_map[idx];
        }
        mem_instruction.wb_value = value;
    }
    else if (strcmp(mem_instruction.opcode, "st") == 0) {   //store instruction 
        register_number = mem_instruction.register_addr;
        if (strstr(register_number, "ROB")) {
                register_number += 3;
            }
            else {
                register_number += 1;
            }

        addr = atoi(register_number);
    }
	// }
	// else {
	// 	strcpy(mem_instruction.instruction, "");
	// }
	printf("                                       Mem2           : %s %d %d %d\n", mem_instruction.instruction, mem_instruction.value_1, mem_instruction.value_2, mem_instruction.wb_value);

	return mem_instruction;
}


int write_back(CPU *cpu) {
	char *register_number;
	int addr;

    for (int i = 0; i < RS_SIZE; i++) {
        prev_id_struct_wb[i] = curr_id_struct_wb[i];
        curr_id_struct_wb[i] = prev_id_struct_arith[i];
    }

    for (int j = 0; j < RS_SIZE; j++) {         // loop through the output array of arithmetic ops
        printf("curr_id_struct_wb[j].instruction %s\n\n\n", curr_id_struct_wb[j].instruction);
        if (strlen(curr_id_struct_wb[j].instruction)) {
            register_number = curr_id_struct_wb[j].register_addr;

            if (strstr(register_number, "ROB")) {
                register_number += 3;
            }
            else {
                register_number += 1;
            }

            addr = atoi(register_number);
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

                    if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_wb[j].register_addr) == 0) {
                        if (curr_id_struct_rr.dependency) {
                            curr_id_struct_rr.dependency = false;
                            rs_enqueue(curr_id_struct_rr);

                            /* execute the branch instruction that is present in RR stage */
                            if (strstr(curr_id_struct_rr.opcode, "bez") || strstr(curr_id_struct_rr.opcode, "blez") || strstr(curr_id_struct_rr.opcode, "bltz") || 
                                strstr(curr_id_struct_rr.opcode, "bgez") || strstr(curr_id_struct_rr.opcode, "bgtz") || strstr(curr_id_struct_rr.opcode, "bez")) {
                                branch_without_prediction(cpu, curr_id_struct_rr);

                                rob_struct rob_data;
                                rob_data.dest = -1;
                                rob_data.result = -1;
                                rob_data.e = 0;
                                rob_data.completed = 0;
                                rob_enqueue(rob_data);
                            }
                        }
                    }

                }

            }
            printf("                                       ");
            printf("WB             : %s %d           	| ", curr_id_struct_wb[j].instruction, curr_id_struct_wb[j].wb_value);
            rob_display();
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

                register_addr = prev_id_struct_wb[j].register_addr;
                if (strstr(register_addr, "ROB")) {
                    register_addr += 3;
                }
                else {
                    register_addr += 1;
                }

                addr = atoi(register_addr);

                printf("prev_id_struct_wb[j].instruction %s j %d\n", prev_id_struct_wb[j].instruction, j);
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
                                if (strstr(register_addr, "ROB")) {
                                    register_addr += 3;
                                    // register_addr = rob_queue.buffer[atoi(register_addr)].dest;
                                    addr = cpu->regs[rob_queue.buffer[atoi(register_addr)].dest].value;   // address found in the given register
                                }
                                else {
                                    register_addr += 1;
                                    // register_addr = atoi(register_addr);
                                    addr = cpu->regs[atoi(register_addr)].value;   // address found in the given register
                                }
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
                    printf("Firest increment\n");
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

/*

int write_back(CPU *cpu) {
	char *register_addr;
	prev_id_struct_wb = curr_id_struct_wb;
	curr_id_struct_wb = prev_id_struct_mem2;
	int addr;
	char operands[2][6];
				
	if (strlen(prev_id_struct_mem2.instruction) != 0) {
		if (strcmp(curr_id_struct_wb.opcode, "bez") && strcmp(curr_id_struct_wb.opcode, "blez") && strcmp(curr_id_struct_wb.opcode, "bltz") && 
			strcmp(curr_id_struct_wb.opcode, "bgez") && strcmp(curr_id_struct_wb.opcode, "bgtz") && strcmp(curr_id_struct_wb.opcode, "bez")) {
			if (strcmp(curr_id_struct_wb.opcode, "ret") == 0) {
				break_loop = true;
			}
			else if (strcmp(curr_id_struct_wb.opcode, "st") == 0) {
				register_addr = curr_id_struct_wb.operand_1;
				if (strstr(curr_id_struct_wb.operand_1, "R")) {
					register_addr += 1;
					addr = cpu->regs[atoi(register_addr)].value;   // address found in the given register
					cpu->regs[atoi(register_addr)].is_writing = false;
				}
				else {
					register_addr += 1;
					addr = atoi(register_addr);
				}
				memory_map[addr/4] = curr_id_struct_wb.wb_value;
			}
			else {
				register_addr = curr_id_struct_wb.register_addr;
				if (strstr(curr_id_struct_wb.register_addr, "R")) {
					register_addr += 1;
					cpu->regs[atoi(register_addr)].value = curr_id_struct_wb.wb_value;
					cpu->regs[atoi(register_addr)].is_writing = false;
				}
			}
		}
		fetched_instructions++;

	}
	else {
		strcpy(curr_id_struct_wb.instruction, "");
	}
	printf("                                       WB             : %s %d\n", curr_id_struct_wb.instruction, curr_id_struct_wb.wb_value);

}
*/


// TODO: figure out how to add to queue in IR stage and read this instruction in IS stage, but still read upto 
//       4 instructions here - solved using timestamps

// TODO: while renaming check if the given register address has complete 1 in rob buffer, if yes then given the next name else it will
//       renamed with same value - solved

// TDOD: update completed to 1 and result in ROB table in WR stage
//       dequeu from rob in RE stage

// TODO: update regiters once the instruction is in RE stage



// TODO: check store instruction

// TODO: forwarding IDK what to do
