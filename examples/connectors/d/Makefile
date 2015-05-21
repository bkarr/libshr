SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .d .o

DC = dmd
EXE = dconnect
OBJECTS = dconnect.o
LIB = -L-lshr
DFLAGS = -g -gc -d

all: dconnect

dconnect: dconnect.d shrq.d

%: %.d
	@$(DC) $< $(DFLAGS) $(LIB)

clean:
	@if test -f $(EXE); then \
		rm ./$(OBJECTS); \
		rm ./$(EXE); \
	fi


.PHONY: all clean
