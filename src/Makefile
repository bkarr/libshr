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

all: all64

rh7: CFLAGS = -I../include -g3 -fPIC -std=gnu99 -Wall
rh7: all64

all64: CFLAGS += -mcx16 -c -O3 -D__STDC_NO_ATOMICS__
all64: shared shared_int shared_q shared_map

debug: debug64

debug64: CFLAGS += -mcx16 -c -O0 -D__STDC_NO_ATOMICS__
debug64: shared shared_int shared_q shared_map 

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

.PHONY: all all64 debug debug64 check check64 checkmap64 checkq64
.PHONY: clean shared64 checkint64 checkshr64 
