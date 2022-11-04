# fucked-up

A simple Brainfuck interpreter in C.


## Building and installing

Though it is trivial to build and install the program without it, a Makefile is included and you can use `make install` to install the program, `make` or `make fucked-up` to just build the program, and `make clean` to clean.


## Usage

If not supplied with any arguments the program will read from standard input and write to standard output.

`fucked-up [-c CODE | -f INPUT_FILE] [-g] [-o OUTPUT_FILE]`


`-c` - Read code from following argument

`-f` - Read code from specified file

`-g` - Make executable using GCC, using C as intermediate language

`-o` - Write to specified file

So an example of how to use the program would be:

`./fucked-up -f tests/helloworld.bf`

Or if you wish to create an executable:

`./fucked-up -f tests/helloworld.bf -g -o helloworld`

## Licensing

This project is licensed under the GNU General Public License, version 3. The exact text of this license can be found in the 'LICENSE' file.

An exception to this are some files in the 'tests' folder. The contents of 'tests/fizzbuzz.bf' and those of 'tests/helloworld.bf' come from the Esolang wiki, which provides all its code as part of the public domain.
I'm not sure what the license of 'tests/mandelbrot.bf' is, but it is merely included in this repository, as opposed to this being a derivative work, so I assume it's not a problem as it is being freely distributed anyway.
