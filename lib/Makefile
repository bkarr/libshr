SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

OBJECTS = ../src/shared_q.o
LIB = libshr.a
SHARED_LIB = libshr.so

all: $(OBJECTS)
	@ar -r $(LIB) $(OBJECTS)
	gcc -shared -o $(SHARED_LIB) $(OBJECTS)


clean:
	@if test -f $(LIB); then \
		rm ./$(LIB); \
		rm ./$(SHARED_LIB); \
	fi

install:
	@cp $(LIB) /usr/lib/.
	@cp $(SHARED_LIB) /usr/lib/.

.PHONY: all clean install
