SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

CC = gcc
EXE = sharedq
LIB = -L../lib -lshr

CFLAGS = -I../include -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE) -lrt -lpthread

all: main

%: %.c
	@$(CC) $< $(CFLAGS) $(LIB)

clean:
	@if test -f $(EXE); then \
		rm ./$(EXE); \
	fi

.PHONY: all clean
