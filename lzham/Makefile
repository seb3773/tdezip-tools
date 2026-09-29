.PHONY: all clean test

all:
	@echo "=== Compiling LZHAM 1.0 (AMD64 / Linux) ==="
	$(MAKE) -C src all
	cp -f src/lzham .
	@echo "=== LZHAM built successfully: ./lzham ==="

test: all
	@echo "=== Running LZHAM test suite ==="
	@bash tests/run_tests.sh

clean:
	@echo "=== Cleaning LZHAM build artifacts ==="
	-$(MAKE) -C src clean
	rm -f lzham
	rm -rf pkg_root *.deb tests/tmp_test

distclean: clean
