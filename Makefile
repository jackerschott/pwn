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

APP_NAME = pwn
SRC_NAMES = audioh.c draw.c game.c gfxh.c main.c notation.c util.c
HDR_NAMES = audioh.h config.h draw.h game.h gfxh.h minimp3/minimp3.h minimp3/minimp3_ex.h notation.h pwn.h sounds.h util.h
TEST_NAMES = notationtest
TEST_SRC_NAMES = notation_test.c
TEST_HDR_NAMES = test.h
BUILD_DIR = build

CFLAGS = -g -O0 -DDATADIR='"/home/jona/it/dev/git/pwn/"'
LDFLAGS = 
INCS = 
LIBS = -lpthread -lcairo -lX11 -lpulse -lpulse-simple

BIN = $(BUILD_DIR)/$(APP_NAME)
SRC = $(addprefix src/,$(SRC_NAMES))
OBJ_DIR = $(BUILD_DIR)/obj
OBJ = $(addprefix $(OBJ_DIR)/,$(SRC_NAMES:c=o))

TEST_BIN = $(addprefix $(BUILD_DIR)/test/,$(TEST_NAMES))
TEST_OBJ_DIR = $(BUILD_DIR)/testobj
TEST_OBJ = $(addprefix $(TEST_OBJ_DIR)/,$(TEST_SRC_NAMES:c=o))

default: $(BIN)

# binary
$(BIN): $(BUILD_DIR) $(OBJ_DIR) $(OBJ) 
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

$(OBJ): $(OBJ_DIR)/%.o: src/%.c
	$(CC) -c $(CFLAGS) -o $@ $< $(INCS)

# tests
tests: $(BUILD_DIR) $(TEST_OBJ_DIR) $(TEST_NAMES) 

$(TEST_NAMES): %: $(BUILD_DIR)/test/%

$(TEST_BIN): $(BUILD_DIR)/test/%: $(TEST_OBJ_DIR)/%.o
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

$(TEST_OBJ): $(TEST_OBJ_DIR)/%.o: test/%.c
	$(CC) -c $(CFLAGS) -o $@ $< $(INCS)

$(BUILD_DIR) $(OBJ_DIR) $(TEST_OBJ_DIR):
	mkdir $@

clean:
	$(RM) $(OBJ)
	$(RM) $(TEST_OBJ)
	$(RM) $(BIN)
	-rmdir $(OBJ_DIR)
	-rmdir $(TEST_OBJ_DIR)

install: $(BIN)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(BIN) $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(APP_NAME)
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < pwn.1 > $(DESTDIR)$(MANPREFIX)/man1/pwn.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/pwn.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(APP_NAME)
	rm -f $(DESTDIR)$(MANPREFIX)/man1/pwn.1

.PHONY: default tests $(TEST_NAMES) clean
