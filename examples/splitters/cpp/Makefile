SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .cpp .o .cc

CC = g++
EXE = cppsplitter
LIB = -L../../../lib -static -lshr -lrt -lpthread

CFLAGS = -I../../../include -g3 -O1 -pedantic -Wall -o $(EXE)

all: main

%: %.cpp
	@$(CC) $< $(CFLAGS) $(LIB)

clean:
	@if test -f $(EXE); then \
		rm ./$(EXE); \
	fi


.PHONY: all clean
