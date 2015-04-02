SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

SUBDIRS		= src lib shrq_harness sharedq
TESTDIRS	= src

all:
	set -e
	for i in $(SUBDIRS); \
	do \
		cd $$i; \
		$(MAKE); \
		cd ..; \
	done

clean:
	set -e
	for i in $(SUBDIRS); \
	do \
		cd $$i; \
		$(MAKE) clean; \
		cd ..; \
	done

check:
	set -e
	for i in $(TESTDIRS); \
	do \
		cd $$i; \
		$(MAKE) check; \
		cd ..; \
	done
