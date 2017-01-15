PREFIX =
BINDIR = ${PREFIX}/bin
DESTDIR =

CC = gcc
CFLAGS = -g -std=gnu99 -Os -Wall -Wextra

BINARIES = inject list-containers
SUIDROOT = contain pseudo

all: ${BINARIES} ${SUIDROOT}

contain: contain.o console.o map.o mount.o util.o

inject: inject.o map.o util.o

pseudo: pseudo.o map.o util.o

list-containers: list-containers.o util.o

clean:
	rm -f -- ${BINARIES} ${SUIDROOT} tags *.o

install: ${BINARIES} ${SUIDROOT}
	mkdir -p ${DESTDIR}${BINDIR}
	install -s ${BINARIES} ${DESTDIR}${BINDIR}
	install -g root -m 4755 -o root -s ${SUIDROOT} ${DESTDIR}${BINDIR}

tags:
	ctags -R

.PHONY: all clean install tags
