SHELL = /bin/bash
.SUFFIXES:
.SECONDARY:

CC = gcc
CFLAGS = -I../include -g3 -fPIC -std=gnu99 -pedantic -Wall
OBJECTS = shared.o shared_q.o shared_map.o shared_int.o
TESTDIRS = test
TESTINT = test_internal
TESTSHR = test_share
TESTQ = test_shrq
TESTMAP = test_shrmap
TESTS = $(TESTINT) $(TESTSHR) $(TESTQ) $(TESTMAP)
LIB = -lrt -lpthread -latomic

all: all64

rh7: CFLAGS = -I../include -g3 -fPIC -std=gnu99 -Wall
rh7: all64

all64: CFLAGS += -mcx16 -c -O3 -D__STDC_NO_ATOMICS__
all64: shared shared_int shared_q

all32: CFLAGS += -m32 -c -D__STDC_NO_ATOMICS__ -O1
all32: shared shared_int shared_q shared_map

debug: debug64

debug64: CFLAGS += -mcx16 -c -O0
debug64: shared shared_int shared_q shared_map

debug32: CFLAGS += -m32 -c -D__STDC_NO_ATOMICS__ -O0
debug32: shared shared_int shared_q shared_map

check: all64
	@set -e
	@for i in $(TESTDIRS); \
	do \
		cd $$i; \
		$(MAKE) check; \
		cd ..; \
	done

check64: CFLAGS += -mcx16 -DTESTMAIN -O0
check64: clean shared64 checkq64
#check64: clean shared64 checkq64 checkmap64

shared64: CFLAGS += -mcx16 -c -O0
shared64: shared shared_int

checkq64: CFLAGS += -mcx16 -c -O0
checkq64: shared64 shared_q
	@cd test && $(MAKE) checkq64 && cd ..

checkmap64: CFLAGS += -o $(TESTMAP)
checkmap64: shared64 shared_map
checkint64: shared64
	@cd test && $(MAKE) checkint64 && cd ..

checkshr64: shared64
	@cd test && $(MAKE) checkshr64 && cd ..

check32: CFLAGS += -m32 -D__STDC_NO_ATOMICS_ -DTESTMAIN -O0
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
