SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

OBJECTS = ../src/shared_q.o ../src/shared.o ../src/shared_int.o
LIB = libshr.a
SHARED_LIB = libshr.so

# Architecture detection for conditional 32-bit flag
UNAME_M := $(shell uname -m)

all: all64

rh7: all64

all64: $(OBJECTS)
	@ar -r $(LIB) $(OBJECTS)
	@gcc -shared -o $(SHARED_LIB) $(OBJECTS)
	@chmod 0755 $(SHARED_LIB)

all32: $(OBJECTS)
	@ar -r $(LIB) $(OBJECTS)
ifeq ($(UNAME_M),x86_64)
	@gcc -m32 -shared -o $(SHARED_LIB) $(OBJECTS)
else
	@gcc -shared -o $(SHARED_LIB) $(OBJECTS)
endif
	@chmod 0755 $(SHARED_LIB)


clean:
	@if test -f $(LIB); then \
		rm ./$(LIB); \
		rm ./$(SHARED_LIB); \
	fi

install:
	@cp $(LIB) /usr/lib/.
	@cp $(SHARED_LIB) /usr/lib/.
	@ldconfig

.PHONY: all all64 all32 clean install
