
IDIR        := ../include
ODIR        := obj
CPPCHECK_DIR:= .cppcheck_cache

CC          := gcc
CFLAGS      := -O3 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -pthread -I$(IDIR)

CPPCHECK    := cppcheck
CLANG_TIDY  := clang-tidy

_DEPS       := rng.h config.h
DEPS        := $(patsubst %,$(IDIR)/%,$(_DEPS))


_OBJ        := main.o rng.o
OBJ         := $(patsubst %,$(ODIR)/%,$(_OBJ))

TARGET      := simulator.exe

.PHONY: clean all lint check check-clang check-cppcheck clean

all: $(TARGET)

$(ODIR)/%.o: %.c $(DEPS)
	@mkdir -p $(ODIR)
	$(CC) -c $< $(CFLAGS) -o $@

simulator.exe: $(OBJ)
	$(CC) $^ $(CFLAGS) -o $@

lint: check-cppcheck check-clang

check-cppcheck:
	@echo "Running Cppcheck (Multi-threading Cache-Enabled)"
	@mkdir -p $(CPPCHECK_DIR)
	$(CPPCHECK) -j 4 \
		--cppcheck-build-dir=$(CPPCHECK_DIR) \
		-I$(IDIR) \
		--enable=all \
		--inconclusive \
    --inline-suppr \
		.

check-clang:
	@echo "Running Clang-Tidy Engine"
	$(CLANG_TIDY) main.c rng.c \
		-checks='-*,bugprone-*,performance-*,readability-*,cert-*,clang-analyzer-*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling' \
		-- $(CFLAGS)

clean:
	@echo "Cleaning project..."
	rm -rf $(ODIR) $(TARGET) $(CPPCHECK_DIR)
