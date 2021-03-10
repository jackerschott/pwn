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

export PREFIX = /usr/local
export MANPREFIX = $(PREFIX)/share/man

export BINNAME = pwn
export BUILDDIR = build
TESTNAMES = notationtest gametest

SOUNDNAMES = move.mp3 capture.mp3 lowtime.mp3 gamedecided.mp3

$(BINNAME): $(BUILDDIR)
	@printf '\e[1m%s\e[m\n' 'cd src'
	@$(MAKE) -C src --no-print-directory ROOTDIR=..
	@printf '\e[1m%s\e[m\n' 'cd ..'

test: $(TESTNAMES)

$(TESTNAMES): $(BUILDDIR) $(BINNAME) 
	@printf '\e[1m%s\e[m\n' "cd test/$@"
	@$(MAKE) -C test/$@ --no-print-directory ROOTDIR=../..
	@printf '\e[1m%s\e[m\n' 'cd ../..'

$(BUILDDIR):
	mkdir -p $@

clean:
	rm -rf $(BUILDDIR)

install: $(BINNAME)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(BUILDDIR)/$(BINNAME) $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(BINNAME)
	mkdir -p $(DESTDIR)$(PREFIX)/share/sounds
	cp -f $(addprefix sounds/,$(SOUNDNAMES)) $(DESTDIR)$(PREFIX)/share/sounds
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < $(BINNAME).1 > $(DESTDIR)$(MANPREFIX)/man1/$(BINNAME).1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/$(BINNAME).1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BINNAME)
	rm -f $(DESTDIR)$(MANPREFIX)/man1/$(BINNAME).1
	rm -f $(addprefix $(DESTDIR)$(PREFIX)/share/sounds/,$(SOUNDNAMES))

dist:
	mkdir -p pwn-$(VERSION)
	cp -R LICENSE Makefile pwn.1 sounds src test pwn-$(VERSION)
	tar -czf pwn-$(VERSION).tar.gz pwn-$(VERSION)
	rm -rf pwn-$(VERSION)


.PHONY: $(SUBDIRS) clean install uninstall
