CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -D_GNU_SOURCE
LDFLAGS ?=

.PHONY: all clean test

all: pack unpack pcat

pack: src/pack.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

unpack: src/unpack.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

pcat: unpack
	ln -sf unpack $@

test: all
	./tests/run_tests.sh

clean:
	rm -f pack unpack pcat
