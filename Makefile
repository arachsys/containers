BINDIR := $(PREFIX)/bin
CFLAGS := -Os -Wall -Wfatal-errors

BINARIES := inject
SUIDROOT := contain pseudo

%:: %.c Makefile
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

all: $(BINARIES) $(SUIDROOT)

contain: contain.[ch] console.c map.c mount.c util.c

inject: contain.h inject.c map.c util.c

pseudo: contain.h pseudo.c map.c util.c

clean:
	rm -f $(BINARIES) $(SUIDROOT)

install: $(BINARIES) $(SUIDROOT)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -s $(BINARIES) $(DESTDIR)$(BINDIR)
	install -o root -g root -m 4755 -s $(SUIDROOT) $(DESTDIR)$(BINDIR)

.PHONY: all clean install
