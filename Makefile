# Makefile for EV_program

# Variables
CC = mpicc                  # Compiler
CFLAGS = -Wall              # Compiler flags
OBJ = A2.o                  # Object files

# Target 'all' for building the executable
all: EV_program

# Rule to build the executable
EV_program: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Rule to compile source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
clean:
	rm -f $(OBJ) EV_program

# Phony targets
.PHONY: all clean