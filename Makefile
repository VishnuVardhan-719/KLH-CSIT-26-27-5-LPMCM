# Makefile — Linux Process Monitoring and Control System
#
# Targets:
#   make            build build/lpmcs (zero warnings enforced)
#   make run        build and launch the interactive monitor
#   make test       build and run the unit/behaviour test suite
#   make sanitize   ASan+UBSan build, run tests under sanitizers
#   make clean      remove all build artifacts

CC      := gcc
CFLAGS  := -Wall -Wextra -std=c11 -O2 -D_POSIX_C_SOURCE=200809L \
           -Isrc -Itests
LDFLAGS :=

BUILD   := build
BIN     := $(BUILD)/lpmcs
TESTBIN := $(BUILD)/run_tests

SRC     := src/main.c src/utils.c src/system_info.c src/process_monitor.c \
           src/process_control.c src/process_tree.c src/process_demo.c \
           src/terminal_ui.c
HDR     := src/utils.h src/system_info.h src/process_monitor.h \
           src/process_control.h src/process_tree.h src/process_demo.h \
           src/terminal_ui.h

TESTSRC := tests/run_tests.c src/utils.c src/system_info.c \
           src/process_monitor.c src/process_control.c src/process_tree.c

.PHONY: all run test sanitize clean help

all: $(BIN)

$(BIN): $(SRC) $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

$(TESTBIN): tests/run_tests.c $(TESTSRC) $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(TESTSRC) $(LDFLAGS)

$(BUILD):
	mkdir -p $(BUILD)

run: $(BIN)
	./$(BIN)

test: $(BIN) $(TESTBIN)
	sh tests/test_suite_output.sh ./$(TESTBIN)
	sh tests/test_cli.sh ./$(BIN)

# Memory-safety evidence: the same suite under ASan + UBSan.
# (explicit recipe order: clean, rebuild with sanitizers, then run)
SANFLAGS := -g -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) $(SANFLAGS)" $(TESTBIN)
	ASAN_OPTIONS=detect_leaks=1 ./$(TESTBIN)

clean:
	rm -rf $(BUILD)

help:
	@echo "Targets: all (default), run, test, sanitize, clean"
