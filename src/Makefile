SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

CC = gcc
OBJECTS = shared_q.o
TESTEXE = test_shrq
LIB = -lrt -lpthread

all: all64

all64: CFLAGS = -mcx16 -I../include -c -g3 -O0 -fPIC -std=gnu11 -pedantic -Wall -o "$@".o
all64: shared_q

all32: CFLAGS = -m32 -D__STDC_NO_ATOMICS_ -I../include -c -g3 -O0 -fPIC -std=gnu99 -Wall -o "$@".o
all32: shared_q

debug: debug64

debug64: CFLAGS = -mcx16 -I../include -c -g3 -O0 -fPIC -std=gnu11 -pedantic -Wall -o "$@".o
debug64: shared_q

debug32: CFLAGS = -m32 -D__STDC_NO_ATOMICS_ -I../include -c -g3 -O0 -fPIC -std=gnu11 -pedantic -Wall -o "$@".o
debug32: shared_q

check: check64

check32: CFLAGS = -m32 -D__STDC_NO_ATOMICS_ -I../include -DTESTMAIN -g3 -O0 -std=gnu99 -Wall -o $(TESTEXE)
check32: clean shared_q
	./$(TESTEXE)

check64: CFLAGS = -mcx16 -I../include -DTESTMAIN -g3 -O0 -std=gnu11 -pedantic -Wall -o $(TESTEXE)
check64: clean shared_q
	./$(TESTEXE)


%: %.c
	@$(CC) $< $(CFLAGS) $(LIB)

clean:
	@if test -f $(TESTEXE); then \
		rm $(TESTEXE); \
	fi
	@if test -f $(OBJECTS); then \
		rm $(OBJECTS); \
	fi
	@if test -f core*; then \
		rm core*; \
	fi

.PHONY: all all64 all32 debug debug64 debug32 check check64 check32 clean
