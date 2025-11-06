# ===== Makefile =====

# --- Compilers & flags ---
CC          := gcc
CFLAGS      := -O3 -std=c11 -Wall -Wextra -Wpedantic -fopenmp
INCLUDE     := -Iinclude

CILK_CC     := /opt/opencilk/bin/clang
CILK_FLAGS  := -fopencilk -O3 -std=c11 -Wall -Wextra -Wpedantic $(INCLUDE) 

LDFLAGS     := -lmatio -lz -lpthread
# --- Directories ---
OBJDIR := build
BINDIR := bin

# --- Common sources (used by all builds) ---
COMMON_SRC := src/graph.c src/mmio.c src/cc.c
COMMON_OBJ := $(addprefix $(OBJDIR)/, $(notdir $(COMMON_SRC:.c=.o)))

# --- Executables ---
READ_TARGET := $(BINDIR)/read_mtx
OMP_TARGET  := $(BINDIR)/cc_test
CILK_TARGET := $(BINDIR)/cc_test_cilk
PTHREADS_TARGET := $(BINDIR)/cc_test_pthreads

# --- Source files for each tool ---
READ_MAIN := src/main_read_test.c
OMP_MAIN  := src/main_cc_test.c
CILK_MAIN := src/main_cc_cilk_test.c
PTHREADS_MAIN := src/main_cc_pthreads_test.c

READ_OBJ := $(OBJDIR)/$(notdir $(READ_MAIN:.c=.o))
OMP_OBJ  := $(OBJDIR)/$(notdir $(OMP_MAIN:.c=.o))
CILK_OBJ := $(OBJDIR)/$(notdir $(CILK_MAIN:.c=.o))
PTHREADS_OBJ := $(OBJDIR)/$(notdir $(PTHREADS_MAIN:.c=.o))

# --- Build all ---
all: $(READ_TARGET) $(OMP_TARGET) $(CILK_TARGET) $(PTHREADS_TARGET)

# --- read_mtx tool ---
$(READ_TARGET): $(COMMON_OBJ) $(READ_OBJ) | $(BINDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built $@"

# --- cc_test (sequential + OpenMP) ---
OMP_SRC      := $(COMMON_SRC) src/cc_omp.c
OMP_OBJ_FULL := $(addprefix $(OBJDIR)/, $(notdir $(OMP_SRC:.c=.o))) $(OMP_OBJ)

$(OMP_TARGET): $(OMP_OBJ_FULL) | $(BINDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built $@"

# --- cc_test_pthreads (POSIX Threads) ---
PTHREADS_SRC_FULL := $(COMMON_SRC) src/cc_pthreads.c
PTHREADS_OBJ_FULL := $(addprefix $(OBJDIR)/, $(notdir $(PTHREADS_SRC_FULL:.c=.o))) $(PTHREADS_OBJ)

$(PTHREADS_TARGET): $(PTHREADS_OBJ_FULL) | $(BINDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) -lpthread
	@echo "Built $@"

# --- cc_test_cilk (OpenCilk) ---
CILK_SRC_FULL := $(COMMON_SRC) src/cc_cilk.c
CILK_OBJ_FULL := $(addprefix $(OBJDIR)/, $(notdir $(CILK_SRC_FULL:.c=.o))) $(CILK_OBJ)

$(CILK_TARGET): $(CILK_OBJ_FULL) | $(BINDIR)
	@if [ ! -x "$(CILK_CC)" ]; then \
		echo "Error: OpenCilk compiler not found at $(CILK_CC)"; \
		echo "Install OpenCilk 3.0 and/or update CILK_CC in the Makefile."; \
		exit 1; \
	fi
	$(CILK_CC) $(CILK_FLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built $@"

# --- Compile Cilk sources with the Cilk compiler ---
$(OBJDIR)/cc_cilk.o: src/cc_cilk.c | $(OBJDIR)
	$(CILK_CC) $(CILK_FLAGS) -c $< -o $@

$(OBJDIR)/main_cc_cilk_test.o: src/main_cc_cilk_test.c | $(OBJDIR)
	$(CILK_CC) $(CILK_FLAGS) -c $< -o $@

# --- Generic compile rule (GCC for non-Cilk .c) ---
$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

# --- Special mmio.c rule (suppress vendor warnings) ---
$(OBJDIR)/mmio.o: src/mmio.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDE) -w -c $< -o $@

# --- Directory creation ---
$(OBJDIR) $(BINDIR):
	@mkdir -p $@

# --- Utility targets ---
clean:
	rm -rf $(OBJDIR) $(BINDIR)
	@echo "Cleaned build artifacts."

# --- Convenience aliases ---
omp:   $(OMP_TARGET)
cilk:  $(CILK_TARGET)
read:  $(READ_TARGET)
pthreads: $(PTHREADS_TARGET)

.PHONY: all clean omp cilk read pthreads
