CC      ?= gcc
CFLAGS  ?= -O3 -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -DNDEBUG -D_POSIX_C_SOURCE=200112L -std=c99 -Isrc
LDFLAGS ?= -Wl,-z,relro -Wl,--as-needed

SRCS = src/lzfse_encode.c \
       src/lzfse_decode.c \
       src/lzfse_encode_base.c \
       src/lzfse_decode_base.c \
       src/lzvn_encode_base.c \
       src/lzvn_decode_base.c \
       src/lzfse_fse.c

OBJS = $(SRCS:.c=.o)
MAIN_OBJ = src/lzfse_main.o

BIN = lzfse

.PHONY: all clean test

all: $(BIN)

$(BIN): $(OBJS) $(MAIN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: all
	./tests/run_tests.sh

clean:
	rm -f src/*.o $(BIN)
