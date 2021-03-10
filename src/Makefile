BUILDDIR := $(ROOTDIR)/$(BUILDDIR)
OBJDIR = $(BUILDDIR)/obj

BIN = $(BUILDDIR)/$(BINNAME)
SRC = audioh.c draw.c game.c gfxh.c main.c notation.c util.c
OBJ = $(addprefix $(OBJDIR)/,$(SRC:.c=.o))

CFLAGS = -Wall -O2 -DDATADIR="\"$(DESTDIR)$(PREFIX)/share/sounds/\""
#CFLAGS = -g -O0 -DDATADIR="\"/home/jona/it/dev/git/pwn/sounds/\""
LDFLAGS = 
INCS = 
LIBS = -lpthread -lcairo -lX11 -lpulse -lpulse-simple

$(BIN): $(OBJDIR) $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

$(OBJ): $(OBJDIR)/%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $< $(INCS)

$(OBJDIR): $(BUILDDIR)
	mkdir -p $@
