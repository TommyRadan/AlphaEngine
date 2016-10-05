MAKE = Makefile
TOPDIR = $(shell pwd)
BIN_DIR = Binaries

# ==================================================================================
all:
	@mkdir -p $(BIN_DIR)
	@echo "  Created common binaries ($(BIN_DIR)) directory "
	
	@cd Core && make -f $(MAKE) all

# ==================================================================================
clean:
	@rm -rf $(BIN_DIR)
	@echo "  Common binaries removed  "

	@cd Core && make -f $(MAKE) clean

# ==================================================================================
rebuild: clean all
