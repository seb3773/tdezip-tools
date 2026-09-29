CXX ?= g++
CXXFLAGS ?= -O3 -Wall -Wextra -fno-strict-aliasing -D_FILE_OFFSET_BITS=64 -DNDEBUG
LDFLAGS ?= -Wl,-z,relro -Wl,--as-needed -lpthread

INCLUDES = -Iinclude -Ilzhamcomp -Ilzhamdecomp

DECOMP_SRCS = $(wildcard lzhamdecomp/*.cpp)
COMP_SRCS = lzhamcomp/lzham_lzbase.cpp \
            lzhamcomp/lzham_lzcomp_state.cpp \
            lzhamcomp/lzham_lzcomp_internal.cpp \
            lzhamcomp/lzham_lzcomp.cpp \
            lzhamcomp/lzham_match_accel.cpp \
            lzhamcomp/lzham_pthreads_threading.cpp
LIB_SRCS = lzhamlib/lzham_lib.cpp
MAIN_SRCS = lzham_main.cpp

ALL_SRCS = $(DECOMP_SRCS) $(COMP_SRCS) $(LIB_SRCS) $(MAIN_SRCS)
ALL_OBJS = $(ALL_SRCS:.cpp=.o)

.PHONY: all clean

all: lzham

lzham: $(ALL_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(ALL_OBJS) lzham
