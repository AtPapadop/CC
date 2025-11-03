# ===== Makefile =====
CC      := gcc
CFLAGS  := -O3 -std=c11 -Wall -Wextra -Wpedantic
INCLUDE := -Iinclude

SRC     := src/graph.c src/mmio.c src/main_read_test.c
OBJDIR  := build
OBJ     := $(addprefix $(OBJDIR)/, $(notdir $(SRC:.c=.o)))

BIN     := bin
TARGET  := $(BIN)/read_mtx

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJ)
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $(OBJ) -o $@
	@echo "✅ Built $@"

# Compile our own source files
$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

# Special rule for mmio.c (suppress noisy vendor warnings)
$(OBJDIR)/mmio.o: src/mmio.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDE) -w -c $< -o $@

# Ensure build dir exists
$(OBJDIR):
	@mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(BIN)
	@echo "🧹 Cleaned build artifacts."

.PHONY: all clean
