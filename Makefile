# Project Name
TARGET = congame 

# Compiler
CC = gcc

# Compilation Flags
# -Wall: show all warnings
# -g: include debugging information
CFLAGS = -Wall -g -I/usr/include/libdrm

# Libraries to link
# lsystemd: for the logind handshake
# lm: for math functions
LIBS = -lsystemd -lm -ldrm

# Source files
SRC = main.c

all: $(TARGET) 

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)
	@echo "Build complete: ./$(TARGET)"

clean: 
	rm -f $(TARGET)
