SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

CC = gcc
EXE = shrq_harness
LIB = -L../lib -lshr -lrt -lpthread

#CFLAGS = -I../include -DMTHRD -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)
CFLAGS = -I../include -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)

all: main

%: %.c
	@$(CC) $< $(CFLAGS) $(LIB)

clean:
	@if test -f $(EXE); then \
		rm ./$(EXE); \
	fi

.PHONY: all clean
