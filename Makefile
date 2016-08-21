MAKE = Makefile
TOPDIR = $(shell pwd)
BIN_DIR = bin

# ==================================================================================
all:
	@mkdir -p $(BIN_DIR)
	@echo "  Created common binaries ($(BIN_DIR)) directory "
	
	@cd SDL && make -f $(MAKE) all
	@cd GLEW && make -f $(MAKE) all
	@cd AlphaEngine && make -f $(MAKE) all

# ==================================================================================
clean:
	@rm -rf $(BIN_DIR)
	@echo "  Common binaries removed  "

	@cd SDL && make -f $(MAKE) clean
	@cd GLEW && make -f $(MAKE) clean
	@cd AlphaEngine && make -f $(MAKE) clean

# ==================================================================================
rebuild: clean all
