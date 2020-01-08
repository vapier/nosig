# Written by Mike Frysinger <vapier@gmail.com>
# Released into the public domain.

# Require C11 & POSIX-2008.  Seems reasonable.
STDC = -std=c11 -D_XOPEN_SOURCE=700

CFLAGS ?= -O2 -g
CFLAGS += $(STDC) -Wall -Wextra

VERSION := GIT
CPPFLAGS += -DVERSION='"$(VERSION)"'

WERROR := -Werror
ifeq ($(VERSION),GIT)
CFLAGS += $(WERROR)
endif

DESTDIR =
PREFIX = /usr
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man
MAN1DIR = $(MANDIR)/man1

all: nosig

check:
	./tests/runtests.sh

install:
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(MAN1DIR)
	install -m755 nosig $(DESTDIR)$(BINDIR)/nosig
	install -m644 nosig.1 $(DESTDIR)$(MAN1DIR)/nosig.1

clean:
	rm -f nosig

.PHONY: all check clean install
