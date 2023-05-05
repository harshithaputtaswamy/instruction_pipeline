
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

bool stall = false;
bool break_loop = false;

struct rs_queue_struct rs_queue;
struct rob_queue_struct rob_queue;


decoded_instruction curr_id_struct_if, curr_id_struct_decode, curr_id_struct_ia, curr_id_struct_rr, curr_id_struct_is, curr_id_struct_add, curr_id_struct_mul, curr_id_struct_div, 
	curr_id_struct_br, curr_id_struct_mem1, curr_id_struct_mem2, curr_id_struct_wb;
decoded_instruction prev_id_struct_if, prev_id_struct_decode, prev_id_struct_ia, prev_id_struct_rr, prev_id_struct_is, prev_id_struct_add, prev_id_struct_mul, prev_id_struct_div, 
	prev_id_struct_br, prev_id_struct_mem1, prev_id_struct_mem2, prev_id_struct_wb;
decoded_instruction empty_instruction;


/* rob circular queue implementation */
void rob_queue_init() {
    rob_struct default_rob = {-1, -1, 0, 0};
    for (int i = 0; i < ROB_SIZE; i++) {
        rob_queue.buffer[i].dest =  -1;
        rob_queue.buffer[i].result = -1;
        rob_queue.buffer[i].e = 0;
        rob_queue.buffer[i].completed = 0;
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
        rob_queue.buffer[rob_queue.rear] = rob_data;
        rob_queue.rear = next_rear;    
    }
}

void rob_dequeue() {
    if (rob_queue_empty() == 0) { // check if queue is empty
        rob_queue.front = (rob_queue.front + 1) % ROB_SIZE; // calculate next front index
    }
}

void rob_display() {
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

void rs_dequeue() {
    if (rs_queue_empty() == 0) { // check if queue is empty
        rs_queue.front = (rs_queue.front + 1) % RS_SIZE; // calculate next front index
    }
}

void rs_display() {
  printf("RS Queue contents: ");
  if (rs_queue.front == rs_queue.rear) {
    printf("empty\n");
  } else {
    int i = rs_queue.front;
    while (i != rs_queue.rear) {
      printf("%d %s (%d) \n", rs_queue.instructions[i].addr, rs_queue.instructions[i].instruction, rs_queue.instructions[i].value_1);
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
	for (int reg=0; reg<REG_COUNT; reg++) {
        printf("------------ STATE OF ARCHITECTURAL REGISTER FILE ----------\n");
        printf("R# [(status 0=invalid, 1=valid), tag, value]\n");
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

		instruction_fetch(cpu);
		instruction_decode(cpu);
		instruction_analyze(cpu);
		register_read(cpu);
        instruction_issue(cpu);
		add_stage(cpu);
		multiplier_stage(cpu);
		divition_stage(cpu);
		branch(cpu);
		memory_1(cpu);
		memory_2(cpu);
		write_back(cpu);

		// print_btb_tb(cpu, cpu->clock_cycle);
		print_display(cpu, cpu->clock_cycle);
        // print_registers_test(cpu, cpu->clock_cycle);
        // printf("------------ Reserve Station ----------\n");
        // rs_display();
        // printf("------------ Reorder Buffer----------\n");
        // rob_display();

		if (break_loop) {
			break;
		}
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
	int values[2] = {0, 0};
	char operands[2][6];
	int addr;
	char *register_address;
	bool local_stall = false;

	prev_id_struct_rr = curr_id_struct_rr;

	if (squash_instrutions) {
		curr_id_struct_rr = empty_instruction;
		prev_id_struct_rr = empty_instruction;
	}

	if (strlen(prev_id_struct_ia.instruction) != 0) {

		if (!stall) {
			curr_id_struct_rr = prev_id_struct_ia;
			if (strcmp(prev_id_struct_rr.opcode, "ret") == 0) {
				return 0;
			}
		}
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
					values[i - 3] = cpu->regs[addr].value;
				}
			}
		}

		if (!local_stall) {
			curr_id_struct_rr.dependency = false;
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
    return 0;
}

int add_stage(CPU *cpu) {
	int addr;
	char *register_address;
	int value;
	bool local_stall = false;

	prev_id_struct_add = curr_id_struct_add;

	if (squash_instrutions) {
		prev_id_struct_add = empty_instruction;
		curr_id_struct_add = empty_instruction;
	}

	curr_id_struct_add = prev_id_struct_rr;
	if (strlen(prev_id_struct_rr.instruction) && strcmp(prev_id_struct_add.opcode, "ret")) {

		if (!prev_id_struct_rr.dependency) {

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
					value = addr;   //use the value given
				}
				else {
					register_address += 1;
					addr = atoi(register_address);
                    value = cpu->regs[addr].value;
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
				if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_add.register_addr) == 0) {
					curr_id_struct_rr.wb_value = curr_id_struct_add.wb_value;
				}
			}
			if (strcmp(curr_id_struct_add.opcode, "st") == 0) {
				if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_add.register_addr) == 0) {
					curr_id_struct_rr.wb_value = curr_id_struct_add.wb_value;
				}   
			}
		}
		else {
			strcpy(curr_id_struct_add.instruction, "");
		}

		if (!local_stall) {
			curr_id_struct_add.dependency = false;
		}
		else {
			curr_id_struct_add.dependency = true;
		}
	}
	printf("                                       ADD            : %s %d %d %d %d\n", curr_id_struct_add.instruction, curr_id_struct_add.dependency, curr_id_struct_add.value_1, curr_id_struct_add.value_2, curr_id_struct_add.wb_value);

	return 0;
}


int multiplier_stage(CPU *cpu) {
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

	if (strlen(prev_id_struct_add.instruction) && strcmp(prev_id_struct_mul.opcode, "ret")) {
		if (!prev_id_struct_add.dependency) {
			if (strcmp(curr_id_struct_mul.opcode, "mul") == 0) {
				curr_id_struct_mul.wb_value = curr_id_struct_mul.value_1 * curr_id_struct_mul.value_2;
			}
			
			// forwarding from mul to rr stage instruction
			if (strcmp(curr_id_struct_mul.opcode, "st")) {
				if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_mul.register_addr) == 0) {
					curr_id_struct_rr.value_1 = curr_id_struct_mul.wb_value;
				}
				if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_mul.register_addr) == 0) {
					curr_id_struct_rr.value_2 = curr_id_struct_mul.wb_value;
				}
				if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_mul.register_addr) == 0) {
					curr_id_struct_rr.wb_value = curr_id_struct_mul.wb_value;
				}

				// forwarding from mul to add stage instruction
				if (strcmp(curr_id_struct_add.operand_1, curr_id_struct_mul.register_addr) == 0) {
					curr_id_struct_add.value_1 = curr_id_struct_mul.wb_value;
				}
				if (curr_id_struct_add.num_var == 5 && strcmp(curr_id_struct_add.operand_2, curr_id_struct_mul.register_addr) == 0) {
					curr_id_struct_add.value_2 = curr_id_struct_mul.wb_value;
				}
				if (strcmp(curr_id_struct_add.register_addr, curr_id_struct_mul.register_addr) == 0 && (strcmp(curr_id_struct_add.opcode, "mul") == 0)) {
					curr_id_struct_add.wb_value = curr_id_struct_mul.wb_value;
				}
			}
			if (strcmp(curr_id_struct_mul.opcode, "st") == 0) {
				if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_mul.register_addr) == 0) {
					curr_id_struct_rr.wb_value = curr_id_struct_mul.wb_value;
				}   
			}
		}
		else {
			strcpy(curr_id_struct_mul.instruction, "");
		}

		if (!local_stall) {
			curr_id_struct_mul.dependency = false;
		}
		else {
			curr_id_struct_mul.dependency = true;
		}
	}
	printf("                                       MUL            : %s %d %d %d\n", curr_id_struct_mul.instruction, curr_id_struct_mul.value_1, curr_id_struct_mul.value_2, curr_id_struct_mul.wb_value);

	return 0;
}


int divition_stage(CPU *cpu) {
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


	if (strlen(prev_id_struct_mul.instruction)  && strcmp(prev_id_struct_rr.opcode, "ret")) {
		if (!prev_id_struct_mul.dependency) {
			if (strcmp(curr_id_struct_div.opcode, "div") == 0) {
				curr_id_struct_div.wb_value = curr_id_struct_div.value_1 / curr_id_struct_div.value_2;
			}

			// forwarding from div to rr
			if (strcmp(curr_id_struct_div.opcode, "st")) {
				if (strcmp(curr_id_struct_rr.operand_1, curr_id_struct_div.register_addr) == 0) {
					curr_id_struct_rr.value_1 = curr_id_struct_div.wb_value;
				}
				if (curr_id_struct_rr.num_var == 5 && strcmp(curr_id_struct_rr.operand_2, curr_id_struct_div.register_addr) == 0) {
					curr_id_struct_rr.value_2 = curr_id_struct_div.wb_value;
				}
				if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_div.register_addr) == 0) {
					curr_id_struct_rr.wb_value = curr_id_struct_div.wb_value;
				}
				
				// forwarding from div to add stage instruction
				if (strcmp(curr_id_struct_add.operand_1, curr_id_struct_div.register_addr) == 0) {
				    curr_id_struct_add.value_1 = curr_id_struct_div.wb_value;
				}
				if (curr_id_struct_add.num_var == 5 && strcmp(curr_id_struct_add.operand_2, curr_id_struct_div.register_addr) == 0) {
				    curr_id_struct_add.value_2 = curr_id_struct_div.wb_value;
				}
				if (strcmp(curr_id_struct_add.register_addr, curr_id_struct_div.register_addr) == 0 && (strcmp(curr_id_struct_add.opcode, "div") == 0)) {
				    curr_id_struct_add.wb_value = curr_id_struct_div.wb_value;
				}

				// forwarding from div to mul stage instruction
				if (strcmp(curr_id_struct_mul.operand_1, curr_id_struct_div.register_addr) == 0) {
				    curr_id_struct_mul.value_1 = curr_id_struct_div.wb_value;
				}
				if (curr_id_struct_mul.num_var == 5 && strcmp(curr_id_struct_mul.operand_2, curr_id_struct_div.register_addr) == 0) {
				    curr_id_struct_mul.value_2 = curr_id_struct_div.wb_value;
				}
				if (strcmp(curr_id_struct_mul.register_addr, curr_id_struct_div.register_addr) == 0 && (strcmp(curr_id_struct_add.opcode, "div") == 0)) {
				    curr_id_struct_mul.wb_value = curr_id_struct_div.wb_value;
				}
			}
			if (strcmp(curr_id_struct_div.opcode, "st") == 0) {
				if (strcmp(curr_id_struct_rr.register_addr, curr_id_struct_div.register_addr) == 0) {
					curr_id_struct_rr.wb_value = curr_id_struct_div.wb_value;
				}   
			}
		}
		else {
			strcpy(curr_id_struct_div.instruction, "");
		}

		if (!local_stall) {
			curr_id_struct_div.dependency = false;
		}
		else {
			curr_id_struct_div.dependency = true;
		}
	}
    printf("curr_id_struct_rr %s\n", curr_id_struct_rr.instruction);
	printf("                                       DIV            : %s %d %d %d %d\n", curr_id_struct_div.instruction, curr_id_struct_div.value_1, curr_id_struct_div.value_2, curr_id_struct_div.wb_value, curr_id_struct_div.dependency);

	return 0;
}


int branch(CPU *cpu) {
	int value;
	int branch_taken = 0;

	char *register_addr;
	char *next_instruction_addr;

	squash_instrutions = 0;
	prev_id_struct_br = curr_id_struct_br;

	if (strlen(prev_id_struct_div.instruction) != 0 && !prev_id_struct_div.dependency) {
		//TODO: check for bez, bgez, blez, bgtz, bltz instructions
		//      update prediction table and btb table
		//      squash all the previous fetch if branch is taken
		curr_id_struct_br = prev_id_struct_div;
		if (strcmp(prev_id_struct_br.opcode, "ret")) {

			if (strstr(curr_id_struct_br.opcode, "bez") || strstr(curr_id_struct_br.opcode, "blez") || strstr(curr_id_struct_br.opcode, "bltz") || 
				strstr(curr_id_struct_br.opcode, "bgez") || strstr(curr_id_struct_br.opcode, "bgtz") || strstr(curr_id_struct_br.opcode, "bez")) {

				register_addr = curr_id_struct_br.register_addr;
				register_addr += 1;
				value = curr_id_struct_br.wb_value;

				if (strcmp(curr_id_struct_br.opcode, "bez") == 0) {            
					if (value == 0) {
						branch_taken = 1;
					}
				}

				else if (strcmp(curr_id_struct_br.opcode, "blez") == 0) {
					if (value <= 0) {
						branch_taken = 1;
					}
				}

				else if (strcmp(curr_id_struct_br.opcode, "bltz") == 0) {
					if (value < 0) {
						branch_taken = 1;
					}
				}

				else if (strcmp(curr_id_struct_br.opcode, "bgez") == 0) {
					if (value >= 0) {
						branch_taken = 1;
						printf("here\n");
					}
				}

				else if (strcmp(curr_id_struct_br.opcode, "bgtz") == 0) {
					if (value > 0) {
						branch_taken = 1;
					}
				}


				if (strstr(curr_id_struct_br.operand_1, "#")) {
					next_instruction_addr = curr_id_struct_br.operand_1;
					next_instruction_addr += 1;
				}

				cpu->btb[curr_id_struct_br.addr % BTB_SIZE].target = atoi(next_instruction_addr)/4;   // update target value in btb
				cpu->btb[curr_id_struct_br.addr % BTB_SIZE].tag = 0;

				if (branch_taken) {

					if (cpu->predict_tb[curr_id_struct_br.addr % BTB_SIZE].pattern < 4) {
						program_counter = atoi(next_instruction_addr)/4;
						squash_instrutions = 1;     //TODO: make 1 only if prediction is false
						stall = 0;
					}

					cpu->predict_tb[curr_id_struct_br.addr % BTB_SIZE].pattern += 1;   // update pattern value in prediction table

				}
				else {      // after predicted that the branch will be taken, if branch is not taken then we have to squash all the instructions taken 
							//until now and update the pc to next instruction that is there after branch
					if (cpu->predict_tb[curr_id_struct_br.addr % BTB_SIZE].pattern >= 4) {
						squash_instrutions = 1;
						program_counter = curr_id_struct_br.addr + 1;

					}
					if (cpu->predict_tb[curr_id_struct_br.addr % BTB_SIZE].pattern > 0) {
						cpu->predict_tb[curr_id_struct_br.addr % BTB_SIZE].pattern -= 1;   // update pattern value in prediction table
					}
				}
			}
		}
	}
	else {
		strcpy(curr_id_struct_br.instruction, "");
	}
	printf("                                       BR             : %s %d %d %d\n", curr_id_struct_br.instruction, curr_id_struct_br.value_1, curr_id_struct_br.value_2, curr_id_struct_br.wb_value);

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
				int idx = (curr_id_struct_mem2.value_1)/4;
				value = memory_map[idx];
			}
			curr_id_struct_mem2.wb_value = value;
		}
		else if (strcmp(curr_id_struct_mem2.opcode, "st") == 0) {   //store instruction 
			register_number = curr_id_struct_mem2.register_addr;
			register_number += 1;

			addr = atoi(register_number);
		}
	}
	else {
		strcpy(curr_id_struct_mem2.instruction, "");
	}
	printf("                                       Mem2           : %s %d %d %d\n", curr_id_struct_mem2.instruction, curr_id_struct_mem2.value_1, curr_id_struct_mem2.value_2, curr_id_struct_mem2.wb_value);

	return 0;
}


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


	if (cpu->clock_cycle == 10)
		exit(0);
	return 0;
}
