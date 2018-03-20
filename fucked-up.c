/* Copyright (C) 2017-2018 Uwila

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
#include <errno.h>
#include <sysexits.h>
#include <unistd.h>
#include <ctype.h>

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
    GOAL_LLVM, // TODO
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
    int inssize = 2;
    int * instructions = calloc(inssize,sizeof(int));

    int i = 0; // Needed inssize
    int balance = 0; // Keeps track of BF_LOOP_START and BF_LOOP_END balance

    char c;
    int instruction;
    while ((c = fgetc(file)) != EOF){
        while (i >= inssize - 1){
            int *new_instructions = calloc(inssize*2,sizeof(int));
            int i;
            for (i=0; i < inssize; i++){
                new_instructions[i] = instructions[i];
            }
            free(instructions);
            instructions = new_instructions;
            inssize *= 2;
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

// Runs the program contained in a bf_data_t
int bf_data_run (bf_data_t *bf_data, FILE * output_file)
{
    // Current place in instruction space
    size_t insptr = 0;

    // Maximum and current index in memory space
    size_t memmax = 1;
    size_t memptr = 0;

    int *memory = calloc(memmax,sizeof(int));

    while(bf_data->instructions[insptr] != BF_UNDEFINED){

        while (memptr >= memmax){
            int *newmemory = calloc(memmax*2,sizeof(int));
            int i;
            for (i=0; i < memmax; i++){
                newmemory[i] = memory[i];
            }
            free(memory);
            memory = newmemory;
            memmax *= 2;
        }

        switch(bf_data->instructions[insptr]){
        case BF_INC:
            insptr++;
            memory[memptr] += bf_data->instructions[insptr];
            break;
        case BF_DEC:
            insptr++;
            memory[memptr] -= bf_data->instructions[insptr];
            break;
        case BF_NEXT:
            insptr++;
            memptr += bf_data->instructions[insptr];
            break;
        case BF_PREV:
            insptr++;
            memptr -= bf_data->instructions[insptr];
            break;
        case BF_LOOP_START:
            insptr++;
            if (memory[memptr] == 0){
                insptr = bf_data->instructions[insptr] + 1;
            }
            break;
        case BF_LOOP_END:
            insptr++;
            if (memory[memptr] != 0){
                insptr = bf_data->instructions[insptr] + 1;
            }
            break;
        case BF_PUT:
            fputc(memory[memptr],output_file);
            break;
        case BF_GET:
            memory[memptr] = getchar();
            break;
        }
        insptr++;
    }

    // Free the memory
    free(memory);

    return STATUS_OK;
}

// For passing code through other compilers
FILE ** p2open (const char *cmd, FILE **fpbuf)
{
    int ERROR = -1;
    FILE  ** result = NULL;
    int fd1[2], fd2[2];
    int ispipe1 = ERROR, ispipe2 = ERROR;
    pid_t pid;

    // Open fd1 for reading from and fd2 for writing to
    if((ispipe1 = pipe(fd1)) == -1 || (ispipe2 = pipe(fd2)) == -1) {
        return NULL;
    }

    if ((pid = fork())) {
        // Parent
        if((fpbuf[0] = fdopen(fd1[0],"r")) == NULL ||
           (fpbuf[1] = fdopen(fd2[1],"w")) == NULL) {
            result = NULL;
        } else {
            result = fpbuf;
        }
    } else {
        // Child
        dup2(fd2[0],0);
        dup2(fd1[1],1);
        close(fd1[0]);
        close(fd1[1]);
        close(fd2[0]);
        close(fd2[1]);
        execlp("/bin/sh","sh","-c",cmd,NULL);
    }

    // Check for problems
    if (ispipe1 != ERROR) {
        close(fd1[1]);
        if(!result) {
            close(fd1[0]);
        }
    }
    if (ispipe2 != ERROR) {
        close(fd2[0]);
        if(!result) {
            close(fd2[1]);
        }
    }
    return result;
}

int bf_data_through_gcc (bf_data_t *bf_data, FILE * output_file)
{
    FILE *gcc[2];

    int c;

    // Open pipes in and out of GCC
    if(p2open("gcc -O3 -xc -o /dev/stdout -",gcc) != NULL) {

        // Write to GCC

        int insptr;

        fprintf(gcc[1],
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
        for (insptr = 0;
             bf_data->instructions[insptr] != BF_UNDEFINED;
             insptr++) {
            switch (bf_data->instructions[insptr]) {
            case BF_INC:
                insptr++;
                fprintf(gcc[1],"memory[memptr] += %i;\n",
                        bf_data->instructions[insptr]);
                break;
            case BF_DEC:
                insptr++;
                fprintf(gcc[1],"memory[memptr] -= %i;\n",
                        bf_data->instructions[insptr]);
                break;
            case BF_GET:
                fprintf(gcc[1],"memory[memptr] = getchar();\n");
                break;
            case BF_PUT:
                fprintf(gcc[1],"putchar(memory[memptr]);\n");
                break;
            case BF_NEXT:
                insptr++;
                fprintf(gcc[1],"memptr += %i;\n",
                        bf_data->instructions[insptr]);
                // memfix is called to make sure the memory is still big enough
                fprintf(gcc[1],"memfix();");
                break;
            case BF_PREV:
                insptr++;
                fprintf(gcc[1],"memptr -= %i;\n",
                        bf_data->instructions[insptr]);
                break;
            case BF_LOOP_START:
                insptr++;
                fprintf(gcc[1],"while(memory[memptr]!=0){\n");
                break;
            case BF_LOOP_END:
                insptr++;
                fprintf(gcc[1],"}\n");
                break;
            }
        }

        // Now close main
        fprintf(gcc[1],"}\n");

        // Close input
        fclose(gcc[1]);

        // Read from GCC
        while((c = fgetc(gcc[0])) != EOF) {
            fputc(c, output_file);
        }

        // Close output
        fclose(gcc[0]);

        return STATUS_OK;
    } else {
        return STATUS_CANNOT_REACH_GCC;
    }
}

int main(int argc, char *argv[])
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
    default:
        fprintf(stderr, "Internal error, no `input_mode`");
        exit(EX_SOFTWARE);
    }

    switch(output_mode) {
    case WRITE_FILE:
        output_file = fopen(output_arg, "w");
        if (output_file == NULL) {
            perror("Could not read output file");
            exit(EX_CANTCREAT);
        }
        break;
    case WRITE_STDOUT:
        output_file = stdout;
        break;
    default:
        fprintf(stderr, "Internal error, no `output_mode`");
        exit(EX_SOFTWARE);
    }

    // Read data from file and close
    bf_data_from_file(&bf_data,input_file);
    fclose(input_file);

    // Error if something went wrong
    switch (status){
    case STATUS_UNBALANCED_LOOP:
        fputs("BF_LOOP_START and BF_LOOP_END were not balanced\n",stderr);
        exit(EX_DATAERR);
        break;
    case STATUS_LOOP_END_BEFORE_START:
        fputs("Encountered BF_LOOP_END before matching BF_LOOP_START\n",stderr);
        exit(EX_DATAERR);
        break;
    case STATUS_NO_INPUT:
        fputs("Could not read from input\n",stderr);
        exit(EX_NOINPUT);
        break;
    case STATUS_OK:
        // No problem
        break;
    default:
        fprintf(stderr, "Unknown error parsing input file %s\n", argv[1]);
        exit(EX_SOFTWARE);
    }

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
        status = bf_data_through_gcc (&bf_data, output_file);
        break;
    }

    // Close output
    fclose(output_file);

    // Error if something went wrong
    switch(status) {
    case STATUS_CANNOT_REACH_GCC:
        perror("Could not reach GCC");
        exit(EX_SOFTWARE);
        break;
    case STATUS_OK:
        // No problem
        break;
    }
}
