SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

CC = gcc
OBJECTS = shared_q.o
TESTEXE = test_shrq
LIB = -lrt -lpthread

all: CFLAGS = -mcx16 -I../include -c -g3 -O1 -fPIC -std=gnu11 -pedantic -Wall -o "$@".o
all: shared_q

all32: CFLAGS = -m32 -D__STDC_NO_ATOMICS_ -I../include -c -g3 -O1 -fPIC -std=gnu99 -Wall -o "$@".o
all32: shared_q

debug: CFLAGS = -mcx16 -I../include -c -g3 -O0 -fPIC -std=gnu11 -pedantic -Wall -o "$@".o
debug: shared_q

check32: CFLAGS = -m32 -D__STDC_NO_ATOMICS_ -I../include -DTESTMAIN -g3 -O0 -std=gnu99 -Wall -o $(TESTEXE)
check32: clean shared_q
	./$(TESTEXE)

check: CFLAGS = -mcx16 -I../include -DTESTMAIN -g3 -O0 -std=gnu11 -pedantic -Wall -o $(TESTEXE)
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

.PHONY: all all32 clean check check32
