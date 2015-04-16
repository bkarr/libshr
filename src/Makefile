SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

CC = gcc
OBJECTS = shared_q.o
TESTEXE = test_shrq
LIB = -lrt -lpthread

all: CFLAGS = -I../include -c -g3 -O1 -std=gnu11 -pedantic -Wall -o "$@".o
all: shared_q

check: CFLAGS = -I../include -DTESTMAIN -g3 -O1 -std=gnu11 -pedantic -Wall -o $(TESTEXE)
check: clean shared_q
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

.PHONY: all clean check
