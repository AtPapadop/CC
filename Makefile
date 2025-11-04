# ===== Makefile =====
CC       := gcc
CFLAGS   := -O3 -std=c11 -Wall -Wextra -Wpedantic -fopenmp
INCLUDE  := -Iinclude

OBJDIR := build
BIN    := bin

COMMON_SRC  := src/graph.c src/mmio.c src/cc.c src/cc_omp.c
COMMON_OBJ  := $(addprefix $(OBJDIR)/, $(notdir $(COMMON_SRC:.c=.o)))

READ_MAIN   := src/main_read_test.c
READ_OBJ    := $(OBJDIR)/$(notdir $(READ_MAIN:.c=.o))
READ_TARGET := $(BIN)/read_mtx

CC_MAIN     := src/main_cc_test.c
CC_OBJ      := $(OBJDIR)/$(notdir $(CC_MAIN:.c=.o))
CC_TARGET   := $(BIN)/cc_test

# Default target builds both executables
all: $(READ_TARGET) $(CC_TARGET)

# Link the read_mtx tool
$(READ_TARGET): $(COMMON_OBJ) $(READ_OBJ) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@
	@echo "Built $@"

# Link the cc_test tool
$(CC_TARGET): $(COMMON_OBJ) $(CC_OBJ) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@
	@echo "Built $@"

# Compile our own source files
$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

# Special rule for mmio.c (suppress noisy vendor warnings)
$(OBJDIR)/mmio.o: src/mmio.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDE) -w -c $< -o $@

# Ensure output directories exist before compiling or linking
$(OBJDIR) $(BIN):
	@mkdir -p $@

clean:
	rm -rf $(OBJDIR) $(BIN)
	@echo "Cleaned build artifacts."

.PHONY: all clean
