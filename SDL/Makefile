# Makefile for platform detection for SDL

all:
ifeq ($(OS),Windows_NT)
	@echo "  Compiling SDL for Windows "
	@make -f Makefile-windows
else
ifeq ($(shell uname -s),Linux)
	@echo "  Compiling SDL for Linux "
	@make -f Makefile-linux
endif
ifeq ($(shell uname -s),Darwin)
	@echo "  Compiling SDL for OSX "
	@make -f Makefile-osx
endif
endif

clean:
ifeq ($(OS),Windows_NT)
	@make -f Makefile-windows clean
else
ifeq ($(shell uname -s),Linux)
	@make -f Makefile-linux clean
endif
ifeq ($(shell uname -s),Darwin)
	@make -f Makefile-osx clean
endif
endif