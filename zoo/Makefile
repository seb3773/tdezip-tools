CC ?= gcc
CFLAGS ?= -O2 -Wall -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2
LDFLAGS ?= -Wl,-z,relro -Wl,--as-needed

.PHONY: all clean distclean test

all:
	@echo "=== Compiling ZOO 2.10 (AMD64 / Linux) ==="
	$(MAKE) -C src linux64
	cp -f src/zoo .
	cp -f src/fiz .
	@echo "=== ZOO and FIZ built successfully ==="

test: all
	@echo "=== Running ZOO test suite ==="
	@bash tests/run_tests.sh

clean:
	@echo "=== Cleaning ZOO build artifacts ==="
	-$(MAKE) -C src clean
	rm -f src/*.o src/zoo src/fiz
	rm -f zoo fiz
	rm -rf pkg_root *.deb

distclean: clean
