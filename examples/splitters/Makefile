SHELL = /bin/sh

SUBDIRS		= cpp d

all:
	@set -e
	@for i in $(SUBDIRS); \
	do \
		cd $$i; \
		$(MAKE); \
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


.PHONY: all clean
