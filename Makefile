include config.mk

PROG = chess
SRC = main.c gfxh.c comh.c handler.c game.c draw.c
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

install: $(PROG)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(PROG) $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PROG)
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)

.PHONY: clean install uninstall
