# Makefile to build the SDL library
# This file MUST be executed with working directory in SDL root

BIN_DIR = bin
CC = gcc

TARGET = glew.so
OBJECTS = $(BIN_DIR)/glew.o

CFLAGS = -g -O3
LFLAGS =

ifeq ($(OS),Windows_NT)
	LFLAGS := -lopengl32
else
ifeq ($(shell uname -s),Linux)
	LFLAGS := -lGL
endif
ifeq ($(shell uname -s),Darwin)
	LFLAGS := -framework OpenGL
endif
endif

all: make_build_dir ../$(BIN_DIR)/$(TARGET)

rebuild: clean all

clean:
	@rm -rf $(BIN_DIR)
	@echo "  GLEW binaries removed  "
	
make_build_dir:
	@mkdir -p $(BIN_DIR)
	@mkdir -p ../$(BIN_DIR)

../$(BIN_DIR)/$(TARGET): $(OBJECTS)
	@echo "  Linking GLEW  "; $(CC) -shared -o $@ $(OBJECTS) $(LFLAGS)
	@echo "  GLEW Compilation and Linking done  "

$(BIN_DIR)/glew.o: glew.c
	@echo "  Compiling GLEW  "; $(CC) -c $(CFLAGS) -o $@ $?
