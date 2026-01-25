SHELL = /bin/bash
.SUFFIXES:
.SECONDARY:

CC = gcc
CFLAGS = -I../include -g3 -fPIC -std=gnu11 -pedantic -Wall
OBJECTS = shared.o shared_q.o shared_map.o shared_int.o
TESTDIRS = test
TESTINT = test_internal
TESTSHR = test_share
TESTQ = test_shrq
TESTMAP = test_shrmap
TESTS = $(TESTINT) $(TESTSHR) $(TESTQ) $(TESTMAP)
LIB = -lrt -lpthread -latomic

# Architecture detection
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_M),x86_64)
    # x86_64: -mcx16 enables CMPXCHG16B for 128-bit atomics
    ARCH_FLAGS = -mcx16
else ifeq ($(UNAME_M),aarch64)
    # ARM64: Select optimization level based on GRAVITON variable
    ifeq ($(GRAVITON),5)
        ARCH_FLAGS = -march=armv9.2-a+sve2+lse -mtune=neoverse-v2
    else ifeq ($(GRAVITON),4)
        ARCH_FLAGS = -mcpu=neoverse-v2
    else ifeq ($(GRAVITON),3)
        ARCH_FLAGS = -mcpu=neoverse-v1
    else ifeq ($(GRAVITON),2)
        ARCH_FLAGS = -mcpu=neoverse-n1
    else
        # Generic ARM64: -moutline-atomics provides runtime LSE detection
        ARCH_FLAGS = -march=armv8-a -moutline-atomics
    endif
else ifeq ($(UNAME_M),i686)
    ARCH_FLAGS = -m32
endif

all: all64

rh7: CFLAGS = -I../include -g3 -fPIC -std=gnu99 -Wall
rh7: all64

all64: CFLAGS += $(ARCH_FLAGS) -c -O3
all64: shared shared_int shared_q

all32: CFLAGS += -m32 -c -O1
all32: shared shared_int shared_q shared_map

debug: debug64

debug64: CFLAGS += $(ARCH_FLAGS) -c -O0
debug64: shared shared_int shared_q

debug32: CFLAGS += -m32 -c -O0
debug32: shared shared_int shared_q shared_map

check: all64
	@set -e
	@for i in $(TESTDIRS); \
	do \
		cd $$i; \
		$(MAKE) check; \
		cd ..; \
	done

check64: CFLAGS += $(ARCH_FLAGS) -DTESTMAIN -O0
check64: clean shared64 checkq64
#check64: clean shared64 checkq64 checkmap64

shared64: CFLAGS += $(ARCH_FLAGS) -c -O0
shared64: shared shared_int

checkq64: CFLAGS += $(ARCH_FLAGS) -c -O0
checkq64: shared64 shared_q
	@cd test && $(MAKE) ARCH_FLAGS="$(ARCH_FLAGS)" checkq64 && cd ..

checkmap64: CFLAGS += -o $(TESTMAP)
checkmap64: shared64 shared_map
checkint64: shared64
	@cd test && $(MAKE) ARCH_FLAGS="$(ARCH_FLAGS)" checkint64 && cd ..

checkshr64: shared64
	@cd test && $(MAKE) ARCH_FLAGS="$(ARCH_FLAGS)" checkshr64 && cd ..

check32: CFLAGS += -m32 -DTESTMAIN -O0
check32: clean shared32 checkq32
#	@for name in $(TESTS); do echo ./$$name; ./$$name; done

shared32: CFLAGS += -m32 -c -O0
shared32: shared shared_int

checkq32: CFLAGS += -m32 -c -O0
checkq32: shared32 shared_q
	@cd test && $(MAKE) checkq32 && cd ..

checkmap32: CFLAGS += -o $(TESTMAP)
checkmap32: shared_map
checkint32: shared32
	@set -e
	@for i in $(TESTDIRS); \
	do \
		cd $$i; \
		$(MAKE) checkint32; \
		cd ..; \
	done

checkshr32: shared32
	@set -e
	@for i in $(TESTDIRS); \
	do \
		cd $$i; \
		$(MAKE) checkshr32; \
		cd ..; \
	done


%: %.c
	@$(CC) $< $(CFLAGS) $(LIB)

clean:
	@if test -f core*; then \
		@rm core*; \
	fi
	@rm -f -- $(OBJECTS) $(TESTS)
	@for i in $(TESTDIRS); \
	do \
		cd $$i; \
		$(MAKE) clean; \
		cd ..; \
	done

.PHONY: all all64 all32 debug debug64 debug32 check check64 checkmap64 checkq64
.PHONY: check32 checkmap32 checkq32 clean shared64 shared32 checkint64 checkint32
.PHONY: checkshr64 checkshr32
