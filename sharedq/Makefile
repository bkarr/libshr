SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

CC = gcc
EXE = sharedq
LIB = -L../lib -static -lshr -lrt -lpthread

CFLAGS = -I../include -g3 -O2 -std=gnu11 -pedantic -Wall -o $(EXE)

all: main

%: %.c
	@$(CC) $< $(CFLAGS) $(LIB)

clean:
	@if test -f $(EXE); then \
		rm ./$(EXE); \
	fi

install:
	@cp $(EXE) /usr/bin/.

.PHONY: all clean install
