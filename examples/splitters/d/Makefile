SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .d .o

DC = dmd
EXE = dsplitter
OBJECTS = dsplitter.o
LIB = -L-lshr
DFLAGS = -g -gc -d

all: dsplitter

dsplitter: dsplitter.d shrq.d

%: %.d
	@$(DC) $< $(DFLAGS) $(LIB)

clean:
	@if test -f $(EXE); then \
		rm ./$(OBJECTS); \
		rm ./$(EXE); \
	fi


.PHONY: all clean
