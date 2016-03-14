SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

CC = gcc
EXE = shrq_harness
LIB = -L../lib -lshr -lrt -lpthread

all: all64

#all64: CFLAGS = -I../include -DMTHRD -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)
all64: CFLAGS = -I../include -static -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)
all64: main

all32: CFLAGS = -m32 -I../include -static -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)
all32: main


%: %.c
	@$(CC) $< $(CFLAGS) $(LIB)

clean:
	@if test -f $(EXE); then \
		rm ./$(EXE); \
	fi

.PHONY: all all64 all32 clean
