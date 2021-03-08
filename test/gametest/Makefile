BUILDDIR := $(ROOTDIR)/$(BUILDDIR)
BINNAME = gametest
BINDIR = $(BUILDDIR)/test
OBJDIR = $(BUILDDIR)/obj

BIN = $(BINDIR)/$(BINNAME)
SRC = gametest.c
OBJ = $(addprefix $(OBJDIR)/,$(SRC:.c=.o))
TARGETSRC = game.c notation.c
TARGETOBJ = $(addprefix $(OBJDIR)/,$(TARGETSRC:.c=.o))

CFLAGS = -g
LDFLAGS = 
INCS = -I$(ROOTDIR)/test/ -I$(ROOTDIR)/src
LIBS =

# binary
$(BIN): $(BINDIR) $(OBJDIR) $(OBJ) $(TARGETOBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(TARGETOBJ) $(LIBS)

$(OBJ): $(OBJDIR)/%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $< $(INCS)

$(BINDIR): $(BUILDDIR)
	mkdir -p $@
