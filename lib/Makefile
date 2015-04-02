SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

OBJECTS = ../src/shared_q.o
LIB = libshr.a

all: $(OBJECTS)
	@ar -r $(LIB) $(OBJECTS)


clean:
	@if test -f $(LIB); then \
		rm ./$(LIB); \
	fi

.PHONY: all clean
