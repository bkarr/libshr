SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

CC = gcc
EXE = sharedq
LIB = -L../lib -static -lshr -lrt -lpthread

all: CFLAGS = -I../include -g3 -O1 -std=gnu11 -pedantic -Wall -o $(EXE)
all: main

debug: CFLAGS = -I../include -g3 -O0 -std=gnu11 -pedantic -Wall -o $(EXE)
debug: main

%: %.c
	@$(CC) $< $(CFLAGS) $(LIB)

clean:
	@if test -f $(EXE); then \
		rm ./$(EXE); \
	fi

install:
	@cp $(EXE) /usr/bin/.

.PHONY: all clean install
