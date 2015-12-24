CC=gcc
CFLAGS=-Wall -O2

ARCH:=$(shell uname -s)-$(shell uname -m)

SRC=$(wildcard *.c)
OBJS=$(SRC:%.c=%.o)

ifndef DESTDIR
DESTDIR=/usr/local
endif

all: ac_dimmer-admin ac_button-admin
.PHONY: all

clean :
	rm -rf *.o ac_dimmer-admin ac_button-admin
.PHONY: clean

ac_dimmer-admin: $(OBJS)

ac_button-admin: $(OBJS)

%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $< -c -o $@

install: install-files
	mkdir -p $(DESTDIR)/bin/
	install $(OUTDIR)/ac_dimmer-admin $(DESTDIR)/bin/
	chown root:root $(DESTDIR)/bin/ac_dimmer-admin
	chmod +s $(DESTDIR)/bin/ac_dimmer-admin
	install $(OUTDIR)/ac_button-admin $(DESTDIR)/bin/
	chown root:root $(DESTDIR)/bin/ac_button-admin
	chmod +s $(DESTDIR)/bin/ac_button-admin
.PHONY: install

uninstall:
	rm -f $(DESTDIR)/bin/ac_dimmer-admin
	rm -f $(DESTDIR)/bin/ac_button-admin
.PHONY: uninstall
