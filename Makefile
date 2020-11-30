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

include config.mk

PROG = pwn
SRC = main.c gfxh.c game.c draw.c
HDR = pwn.h gfxh.h draw.h game.h config.h
OBJ = $(addprefix obj/,$(SRC:c=o))
OBJDIR = obj

INCS = 
LIBS = -lpthread -lcairo -lX11

CFLAGS = -g -O0 $(INCS)
LDFLAGS = $(LIBS)

$(PROG): $(OBJDIR) $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

$(OBJ): $(OBJDIR)/%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -f $(PROG)
	rm -f $(OBJ)
	rmdir obj

distclean: clean
	rm -r pwn-$(VERSION).tar.gz

dist:
	mkdir -p pwn-$(VERSION)
	cp -r $(SRC) $(HDR) pwn.1 Makefile config.mk LICENSE pwn-$(VERSION)
	tar -cf pwn-$(VERSION).tar pwn-$(VERSION)
	rm -rf pwn-$(VERSION)

install: $(PROG)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(PROG) $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PROG)
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < pwn.1 > $(DESTDIR)$(MANPREFIX)/man1/pwn.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/pwn.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)
	rm -f $(DESTDIR)$(MANPREFIX)/man1/pwn.1

.PHONY: clean install uninstall dist distclean
