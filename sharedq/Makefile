SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

CC = gcc
EXE = sharedq
LIB = -L../lib -l:libshr.a -lrt -lpthread

all: all64

all64: CFLAGS = -I../include -g3 -O1 -std=gnu11 -pedantic -Wall -o $(EXE)
all64: main

all32: CFLAGS = -m32 -I../include -g3 -O1 -std=gnu11 -pedantic -Wall -o $(EXE)
all32: main

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

.PHONY: all all64 all32 debug clean install
