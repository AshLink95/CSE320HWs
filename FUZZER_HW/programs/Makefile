# === NOTE ===
# During grading, we will replace the entire programs directory with
# our own. Feel free to add additional programs to test your fuzzer
# against here using the build system. See 
# $PROJECT_DIR/programs/README.md for how to use the build system.
# === NOTE ===

# Add program subdirectories here to add to build script

PROGRAM-DIR := cascade hello-world

# ------------------------------------------------------------ #
# === DO NOT MODIFY BEYOND THIS POINT ===
# Modifying the file beyond this point may break the build
# system. 
# === DO NOT MODIFY BEYOND THIS POINT ===

MAKEFLAGS += --no-print-directory

DIR_NM := $(shell basename "$(CURDIR)")
BIN ?= bin/

.PHONY: all debug clean

all: $(BIN)
	@echo " DESCEND  $(DIR_NM)"
	@for DIR in $(PROGRAM-DIR); do 						\
		$(MAKE) -C "$$DIR" all BIN=../bin/;				\
	done
	@echo " ASCEND   $(DIR_NM)"

debug: $(BIN)
	@echo " DESCEND  $(DIR_NM)"
	@for DIR in $(PROGRAM-DIR); do 						\
		$(MAKE) -C "$$DIR" debug BIN=../bin/;			\
	done
	@echo " ASCEND   $(DIR_NM)"

clean:
	@for DIR in $(PROGRAM-DIR); do						\
		$(MAKE) -C "$$DIR" clean BIN=../bin/;			\
	done
	@rm -rf $(BIN)
	@echo " CLEAN    $(DIR_NM)"

$(BIN):
	@mkdir -p $(BIN)
