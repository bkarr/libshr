SHELL = /bin/sh
.SUFFIXES:
.SUFFIXES: .c .o

SUBDIRS		= src lib shrq_harness sharedq examples
MKDIRS		= src lib shrq_harness sharedq
TESTDIRS	= src
INSTDIRS	= include lib sharedq
EXDIRS		= examples

all:
	@set -e
	@for i in $(MKDIRS); \
	do \
		cd $$i; \
		$(MAKE); \
		cd ..; \
	done

rh7:
	@set -e
	@for i in $(MKDIRS); \
	do \
		cd $$i; \
		$(MAKE) rh7; \
		cd ..; \
	done

all64:
	@set -e
	@for i in $(MKDIRS); \
	do \
		cd $$i; \
		$(MAKE) all64; \
		cd ..; \
	done

all32:
	@set -e
	@for i in $(MKDIRS); \
	do \
		cd $$i; \
		$(MAKE) all32; \
		cd ..; \
	done

clean:
	@set -e
	@for i in $(SUBDIRS); \
	do \
		cd $$i; \
		$(MAKE) clean; \
		cd ..; \
	done

check:
	@set -e
	@for i in $(TESTDIRS); \
	do \
		cd $$i; \
		$(MAKE) check; \
		cd ..; \
	done

install:
		@set -e
		@for i in $(INSTDIRS); \
		do \
			cd $$i; \
			$(MAKE) install; \
			cd ..; \
		done

examples:
	@set -e
	@for i in $(EXDIRS); \
	do \
		cd $$i; \
		$(MAKE); \
		cd ..; \
	done

.PHONY: all all64 all32 clean check install examples
