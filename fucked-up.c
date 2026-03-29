/* Copyright (C) 2017-2024 Lambdara

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <sys/stat.h>

// Error codes
enum {
    // General
    STATUS_OK = 0, // Default
    // Reading input
    STATUS_UNBALANCED_LOOP,
    STATUS_LOOP_END_BEFORE_START,
    STATUS_NO_INPUT,
    // Running
    STATUS_CANNOT_REACH_GCC,
    // Error in user code
    STATUS_MEMORY_POINTER_BELOW_ZERO,
    // Temporary file
    STATUS_CANNOT_CREATE_TEMP_FILE,
};

// Instructions
enum {
    BF_UNDEFINED = 0, // in `instructions`, not initialized is undefined
    BF_INC,        BF_DEC,
    BF_GET,        BF_PUT,
    BF_NEXT,       BF_PREV,
    BF_LOOP_START = -1, BF_LOOP_END = -2,
};

// Input Modes
enum {
    READ_FILE,
    READ_ARG,
    READ_STDIN,
};

// Output Modes
enum {
    WRITE_FILE,
    WRITE_STDOUT,
};

// Goals
enum {
    GOAL_EVAL,
    GOAL_GCC,
};

// Container for all the program data
typedef struct {
    int * instructions;
} bf_data_t;

// Maps brainfuck instruction to enum element
int bf_instruction(char c)
{
    switch(c){
    case '+':
        return BF_INC;
    case '-':
        return BF_DEC;
    case ',':
        return BF_GET;
    case '.':
        return BF_PUT;
    case '>':
        return BF_NEXT;
    case '<':
        return BF_PREV;
    case '[':
        return BF_LOOP_START;
    case ']':
        return BF_LOOP_END;
    default:
        return BF_UNDEFINED;
    }
}

// Reads instructions from brainfuck file
int bf_data_from_file (bf_data_t * bf_data, FILE *file)
{
    // Size of, and instruction space itself.
    int instructions_size = 2;
    int * instructions = calloc(instructions_size,sizeof(int));

    int i = 0; // Needed inssize
    int balance = 0; // Keeps track of BF_LOOP_START and BF_LOOP_END balance

    char c;
    int instruction;
    while ((c = fgetc(file)) != EOF){
        // We do not know how many instructions will be read
        // When instruction space too small, just double it
        // Amortized this is still O(n) time lost for n instructions
        while (i >= instructions_size - 1){
            int new_instructions_size = instructions_size * 2;
            instructions = realloc(instructions, new_instructions_size * sizeof(int));
            for (int i = instructions_size; i < new_instructions_size; i++)
                instructions[i] = BF_UNDEFINED;
            instructions_size = new_instructions_size;
        }

        if((instruction = bf_instruction(c)) != BF_UNDEFINED){
            instructions[i] = instruction;
            i++;
            if (instruction == BF_LOOP_START)
                balance++;
            else if (instruction == BF_LOOP_END)
                balance--;
            // Error if order of BF_LOOP_END and BF_LOOP_START tags is wrong
            if (balance < 0){
                return STATUS_LOOP_END_BEFORE_START;
            }
        }
    }

    // Error if BF_LOOP_START and BF_LOOP_END aren't balanced
    if (balance != 0){
        return STATUS_UNBALANCED_LOOP;
    }

    bf_data->instructions = instructions;

    return STATUS_OK;
}

void compress (bf_data_t *bf_data)
{
    // Get required size of instruction space
    int inssize = 1;
    int i;
    char prev = BF_UNDEFINED;
    char cur = BF_UNDEFINED;
    for (i = 0; bf_data->instructions[i] != 0; i++){
        /* All instructions except loops and IO take 2 spots for any number,
           loop constructs always take 2 spots per operator,
           IO (everything else) just takes one spot */
        cur = bf_data->instructions[i];
        switch(cur){
        case BF_INC:
        case BF_DEC:
        case BF_NEXT:
        case BF_PREV:
            if (cur != prev)
                inssize += 2;
            break;
        case BF_LOOP_START:
        case BF_LOOP_END:
            inssize += 2;
            break;
        default:
            inssize++;
        }
        prev = cur;
    }

    // Allocate memory for compressed version
    int *compressed = calloc(inssize,sizeof(int));

    // Pointer-offsets for instruction spaces
    int i_old = 0;
    int i_new = 0;

    // Tokens under compression
    int compressing = BF_UNDEFINED;

    /* Iterate through all of original intruction space, replacing
       BF_INCs, BF_DECs, BF_NEXTs, and BF_PREVs by: TOKEN AMOUNT;
       and replacing BF_LOOP_STARTs and BF_LOOP_ENDs by TOKEN DESTINATION; */
    while (bf_data->instructions[i_old] != 0) {
        switch(bf_data->instructions[i_old]){
        case BF_INC:
            if (compressing != BF_INC){
                compressing = BF_INC;
                compressed[i_new] = BF_INC;
                i_new++;
            } else {
                i_new--;
            }
            compressed[i_new]++;
            break;
        case BF_DEC:
            if (compressing != BF_DEC){
                compressing = BF_DEC;
                compressed[i_new] = BF_DEC;
                i_new++;
            } else {
                i_new--;
            }
            compressed[i_new]++;
            break;
        case BF_NEXT:
            if (compressing != BF_NEXT){
                compressing = BF_NEXT;
                compressed[i_new] = BF_NEXT;
                i_new++;
            } else {
                i_new--;
            }
            compressed[i_new]++;
            break;
        case BF_PREV:
            if (compressing != BF_PREV){
                compressing = BF_PREV;
                compressed[i_new] = BF_PREV;
                i_new++;
            } else {
                i_new--;
            }
            compressed[i_new]++;
            break;
        case BF_LOOP_START:
            compressing = BF_UNDEFINED;
            compressed[i_new] = BF_LOOP_START;
            i_new++; // Allow room to set jump by loop-end compression
            break;
        case BF_LOOP_END:
            compressing = BF_UNDEFINED;
            compressed[i_new] = BF_LOOP_END;

            // seek BF_LOOP_START matching this BF_LOOP_END
            int counter = 0;
            int loop_start = i_new;
            while(compressed[loop_start] != BF_LOOP_START || counter != 0){
                if (compressed[loop_start] == BF_LOOP_START)
                    counter--;
                loop_start--;
                if(compressed[loop_start] == BF_LOOP_END)
                    counter++;
            }
            // Set pointer from start to end, and from end to start
            compressed[loop_start + 1] = i_new;
            i_new++;
            compressed[i_new] = loop_start;
            break;
        default:
            compressing = BF_UNDEFINED;
            compressed[i_new] = bf_data->instructions[i_old];
        }
        i_old++;
        i_new++;
    }

    // Replace instruction space by compressed instruction space
    free(bf_data->instructions);
    bf_data->instructions = compressed;
}

void reallocate_runtime_memory(int **memory, size_t *memory_maximum, size_t memory_pointer) {
    int new_memory_maximum = *memory_maximum;
    while (memory_pointer >= new_memory_maximum)
        new_memory_maximum *=2;

    *memory = realloc(*memory, new_memory_maximum * sizeof(int)); // Preserves old content

    // Initialize the extra memory to 0
    for (int i = *memory_maximum; i < new_memory_maximum; i++)
        (*memory)[i] = 0;

    // Update current maximum
    *memory_maximum = new_memory_maximum;
}

// Runs the program contained in a bf_data_t
int bf_data_run (const bf_data_t *bf_data, FILE * output_file)
{
    // Status is OK until something goes wrong
    int status = STATUS_OK;

    // Current place in instruction space
    size_t instruction_pointer = 0;

    // Maximum and current index in memory space
    size_t memory_count = 1;
    size_t memory_pointer = 0;

    int *memory = calloc(memory_count,sizeof(int));

    while(bf_data->instructions[instruction_pointer] != BF_UNDEFINED){
        switch(bf_data->instructions[instruction_pointer]){
        case BF_INC:
            instruction_pointer++;
            memory[memory_pointer] += bf_data->instructions[instruction_pointer];
            break;
        case BF_DEC:
            instruction_pointer++;
            memory[memory_pointer] -= bf_data->instructions[instruction_pointer];
            break;
        case BF_NEXT:
            instruction_pointer++;
            memory_pointer += bf_data->instructions[instruction_pointer];
            if (memory_pointer >= memory_count)
                reallocate_runtime_memory(&memory, &memory_count, memory_pointer);
            break;
        case BF_PREV:
            instruction_pointer++;
            if (memory_pointer == 0) {
                status = STATUS_MEMORY_POINTER_BELOW_ZERO;
                goto free_memory_and_exit;
            }
            memory_pointer -= bf_data->instructions[instruction_pointer];
            break;
        case BF_LOOP_START:
            instruction_pointer++;
            if (memory[memory_pointer] == 0){
                instruction_pointer = bf_data->instructions[instruction_pointer] + 1;
            }
            break;
        case BF_LOOP_END:
            instruction_pointer++;
            if (memory[memory_pointer] != 0){
                instruction_pointer = bf_data->instructions[instruction_pointer] + 1;
            }
            break;
        case BF_PUT:
            fputc(memory[memory_pointer],output_file);
            break;
        case BF_GET:
            memory[memory_pointer] = getchar();
            break;
        }
        instruction_pointer++;
    }

    free_memory_and_exit:

    // Free the memory
    free(memory);

    return status;
}


int bf_data_through_gcc (bf_data_t *bf_data, char *output_filename)
{
    char intermediate_filename[] = "/tmp/XXXXXX.c";
    int mkstemps_error = mkstemps(intermediate_filename, 2);

    if (mkstemps_error == -1)
        return STATUS_CANNOT_CREATE_TEMP_FILE;
    
    FILE *intermediate = fopen(intermediate_filename, "w");

    // Open pipes in and out of GCC
    if(intermediate != NULL) {

        // Write to GCC

        fprintf(intermediate,
                // Includes
                "#include <stdlib.h>\n"
                "#include <stdio.h>\n"

                // Global variables for memory management
                "int* memory;"
                "int memsize=1, memptr=0;"

                // Function for dynamic memory allocation
                "void memfix(){"
                "    if(memptr < memsize) return;"
                "    int oldsize=memsize;"
                "    while(memptr >= memsize){"
                "        memsize *= 2;"
                "    };"
                "    int * newmem = calloc(memsize,sizeof(int));"
                "    int i;"
                "    for(i=0;i<oldsize;i++){"
                "        newmem[i]=memory[i];"
                "    }"
                "    free(memory);memory=newmem;"
                "}"

                // Open main
                "int main(void) {"
                "    memory = calloc(memsize,sizeof(int));");

        // Generate the actual instructions
        for (int instruction_pointer = 0;
             bf_data->instructions[instruction_pointer] != BF_UNDEFINED;
             instruction_pointer++) {
            switch (bf_data->instructions[instruction_pointer]) {
            case BF_INC:
                instruction_pointer++;
                fprintf(intermediate,"memory[memptr] += %i;\n",
                        bf_data->instructions[instruction_pointer]);
                break;
            case BF_DEC:
                instruction_pointer++;
                fprintf(intermediate,"memory[memptr] -= %i;\n",
                        bf_data->instructions[instruction_pointer]);
                break;
            case BF_GET:
                fprintf(intermediate,"memory[memptr] = getchar();\n");
                break;
            case BF_PUT:
                fprintf(intermediate,"putchar(memory[memptr]);\n");
                break;
            case BF_NEXT:
                instruction_pointer++;
                fprintf(intermediate,"memptr += %i;\n",
                        bf_data->instructions[instruction_pointer]);
                // memfix is called to make sure the memory is still big enough
                fprintf(intermediate,"memfix();");
                break;
            case BF_PREV:
                instruction_pointer++;
                fprintf(intermediate,"memptr -= %i;\n",
                        bf_data->instructions[instruction_pointer]);
                break;
            case BF_LOOP_START:
                instruction_pointer++;
                fprintf(intermediate,"while(memory[memptr]!=0){\n");
                break;
            case BF_LOOP_END:
                instruction_pointer++;
                fprintf(intermediate,"}\n");
                break;
            }
        }

        // Now close main
        fprintf(intermediate,"}\n");
        
        // Close intermediate file
        fclose(intermediate);

        // Run gcc on intermediate file
        execl("/usr/bin/gcc", "gcc", intermediate_filename, "-o", output_filename, NULL); // TODO error handling

        // Remove the intermediate file
        remove(intermediate_filename);

        return STATUS_OK;
    } else {
        return STATUS_CANNOT_REACH_GCC;
    }
}

int main(const int argc, char *argv[])
{
    /* How to read input, where to give output, and what to do */
    int input_mode = READ_STDIN;
    int output_mode = WRITE_STDOUT;
    int goal = GOAL_EVAL;

    // In- and output location
    char * input_arg = "";
    char * output_arg = "";

    // Argument parsing
    int c;
    while ((c = getopt (argc, argv, "c:f:gho:")) != -1) {
        switch (c) {
        case 'c':
            input_mode = READ_ARG;
            input_arg = optarg;
            break;
        case 'f':
            input_mode = READ_FILE;
            input_arg = optarg;
            break;
        case 'g':
            goal = GOAL_GCC;
            break;
        case 'h':
            fputs("Usage:\n\n",stderr);
            fputs("fucked-up [-c CODE | -f INPUT_FILE] [-g] [-o OUTPUT_FILE]\n\n",stderr);
            fputs("-c  Read code from following argument\n",stderr);
            fputs("-f  Read code from specified file\n",stderr);
            fputs("-g  Compile using GCC, using C as intermediate language\n",stderr);
            fputs("-o  Write to specified file\n",stderr);
            exit(EX_USAGE);
            break;
        case 'o':
            output_mode = WRITE_FILE;
            output_arg = optarg;
            break;
        default:
            return EX_DATAERR;
        }
    }

    // Create empty bf_data and initialize
    bf_data_t bf_data = {calloc(0,sizeof(int))};

    // Status so far
    int status = STATUS_OK;

    FILE * input_file;
    FILE * output_file;

    // Open input and output files depending on the input_mode and output_mode
    switch(input_mode) {
    case READ_ARG:
        input_file = fmemopen(input_arg,strlen(input_arg),"r");
        break;
    case READ_FILE:
        input_file = fopen(input_arg, "r");
        if (input_file == NULL){
            perror("Could not read input file");
            exit(EX_NOINPUT);
        }
        break;
    case READ_STDIN:
        input_file = stdin;
        break;
    }

    switch(output_mode) {
    case WRITE_FILE:
        if (goal != GOAL_GCC){
            output_file = fopen(output_arg, "w");
            if (output_file == NULL) {
                perror("Could not read output file");
                exit(EX_CANTCREAT);
            }
        } else {
            output_file = stdout;
        }
        break;
    case WRITE_STDOUT:
        output_file = stdout;
        break;
    }

    // Read data from file and close
    bf_data_from_file(&bf_data,input_file);
    fclose(input_file);

    // Compress the program
    compress(&bf_data);

    // Do specified job on the code, writing to specified output
    switch (goal) {
    case GOAL_EVAL:
        // Run the program
        status = bf_data_run (&bf_data, output_file);
        break;
    case GOAL_GCC:
        // Compile with GCC
        status = bf_data_through_gcc (&bf_data, output_arg);
        break;
    }

    // Close output
    fclose(output_file);

    // If the goal was to make an executable file, chmod it
    if (goal == GOAL_GCC && output_mode == WRITE_FILE)
        chmod (output_arg, 0775);

    // Error if something went wrong
    switch(status) {
    case STATUS_CANNOT_REACH_GCC:
        perror("Could not reach GCC");
        exit(EX_SOFTWARE);
        break;
    case STATUS_MEMORY_POINTER_BELOW_ZERO:
        perror("Memory pointer below zero");
    case STATUS_OK:
        // No problem
        break;
    }
}
