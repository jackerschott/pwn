#  pwn - simple multiplayer chess game
# 
#  Copyright (C) 2020 Jona Ackerschott
# 
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
# 
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
# 
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <https://www.gnu.org/licenses/>.

VERSION = 1.0

PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

export BINNAME = pwn
export BUILDDIR = build
TESTNAMES = notationtest

$(BINNAME): $(BUILDDIR)
	$(MAKE) -C src ROOTDIR=..

test: $(TESTNAMES)

$(TESTNAMES): $(BUILDDIR) $(BINNAME) 
	$(MAKE) -C test/$@ ROOTDIR=../..

$(BUILDDIR):
	mkdir -p $@

clean:
	rm -rf build/

install: $(BINNAME)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(BIN) $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(BINNAME)
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < $(BINNAME).1 > $(DESTDIR)$(MANPREFIX)/man1/$(BINNAME).1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/$(BINNAME).1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BINNAME)
	rm -f $(DESTDIR)$(MANPREFIX)/man1/$(BINNAME).1

.PHONY: $(SUBDIRS) clean install uninstall
