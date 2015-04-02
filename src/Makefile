CC = gcc
OBJECTS = shared_q.o
TESTEXE = test_shrq

all: CFLAGS = -I../include -c -g3 -O0 -std=gnu11 -pedantic -Wall -o "$@".o -lrt -lpthread
all: shared_q

test: CFLAGS = -I../include -DTESTMAIN -g3 -O0 -std=gnu11 -pedantic -Wall -o $(TESTEXE) -lrt -lpthread
test: clean shared_q
	./$(TESTEXE)

%: %.c
	@$(CC) $< $(CFLAGS) $(LIBS)

clean:
	@if test -f $(TESTEXE); then \
		rm $(TESTEXE); \
	fi
	@if test -f $(OBJECTS); then \
		rm $(OBJECTS); \
	fi

.PHONY: all clean test