CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra -std=c99 -Isrc -flto

LIB_SRCS = src/globals.c \
           src/algorithms/algorithms.c \
           src/algorithms/dictionaries.c \
           src/algorithms/chameleon/core/chameleon_decode.c \
           src/algorithms/chameleon/core/chameleon_encode.c \
           src/algorithms/cheetah/core/cheetah_decode.c \
           src/algorithms/cheetah/core/cheetah_encode.c \
           src/algorithms/lion/core/lion_decode.c \
           src/algorithms/lion/core/lion_encode.c \
           src/algorithms/lion/forms/lion_form_model.c \
           src/buffers/buffer.c \
           src/structure/header.c

MAIN_SRC = src/density_main.c

all: density

density: $(MAIN_SRC) $(LIB_SRCS)
	$(CC) $(CFLAGS) -o $@ $(MAIN_SRC) $(LIB_SRCS)

test: density
	@bash tests/run_tests.sh

clean:
	rm -f density *.o tests/test_*

.PHONY: all test clean
