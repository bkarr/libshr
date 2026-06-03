SHELL = /bin/bash
.SUFFIXES:
.SECONDARY:

VPATH=..
CC = gcc
CFLAGS = -I../../include -I.. -g3 -fPIC -std=gnu11 -pedantic -Wall -O0

TESTINT = test_internal
TESTSHR = test_shared
TESTQ = test_shrq
TESTMAP = test_shrmap
TESTS = $(TESTINT) $(TESTSHR) $(TESTQ) $(TESTMAP)
LIB = -lrt -lpthread -latomic

# ARCH_FLAGS passed from parent Makefile, or detect if run standalone
UNAME_M := $(shell uname -m)
ARCH_FLAGS ?= $(if $(filter x86_64,$(UNAME_M)),-mcx16,$(if $(filter aarch64,$(UNAME_M)),-march=armv8-a -moutline-atomics,))

check: checkshr64 checkint64 checkq64

checkq64: CFLAGS += $(ARCH_FLAGS) ../shared_int.o ../shared.o ../shared_q.o -o $(TESTQ)
checkq64: clean test_shrq
	./$(TESTQ)
checkint64: CFLAGS += $(ARCH_FLAGS) ../shared_int.o -o $(TESTINT)
checkint64: clean test_internal
	./$(TESTINT)
checkshr64: CFLAGS += $(ARCH_FLAGS) ../shared.o -o $(TESTSHR)
checkshr64: clean test_shared
	./$(TESTSHR)


checkq32: CFLAGS += -m32 ../shared_int.o ../shared.o ../shared_q.o -o $(TESTQ)
checkq32: clean test_shrq
	./$(TESTQ)
checkint32: CFLAGS += -m32 ../shared_int.o -o $(TESTINT)
checkint32: clean test_internal
	./$(TESTINT)
checkshr32: CFLAGS += -m32 ../shared.o -o $(TESTSHR)
checkshr32: clean test_shared
	./$(TESTSHR)

%: %.c
	@$(CC) $< $(CFLAGS) $(LIB)

clean:
	@if test -f core*; then \
		@rm core*; \
	fi
	@rm -f -- $(TESTS)

.PHONY: clean checkint64 checkint32 checkshr64 checkshr32 checkq64 checkq32
