CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -O3 -DP1_MODE=2 -DP3_BATCH -DP2_FASTMATH -DLSTM_QUANT=8 -DNDEBUG

SRCS = src/coder/decoder.cpp \
       src/coder/encoder.cpp \
       src/context-manager.cpp \
       src/contexts/bit-context.cpp \
       src/contexts/bracket-context.cpp \
       src/contexts/combined-context.cpp \
       src/contexts/context-hash.cpp \
       src/contexts/indirect-hash.cpp \
       src/contexts/interval-hash.cpp \
       src/contexts/interval.cpp \
       src/contexts/sparse.cpp \
       src/mixer/byte-mixer.cpp \
       src/mixer/lstm-layer.cpp \
       src/mixer/lstm.cpp \
       src/mixer/mixer-input.cpp \
       src/mixer/mixer.cpp \
       src/mixer/sigmoid.cpp \
       src/mixer/sse.cpp \
       src/models/bracket.cpp \
       src/models/byte-model.cpp \
       src/models/direct-hash.cpp \
       src/models/direct.cpp \
       src/models/indirect.cpp \
       src/models/fxcmv1.cpp \
       src/models/match.cpp \
       src/models/paq8.cpp \
       src/models/ppmd.cpp \
       src/predictor.cpp \
       src/preprocess/dictionary.cpp \
       src/preprocess/preprocessor.cpp \
       src/runner.cpp \
       src/states/nonstationary.cpp \
       src/states/run-map.cpp

all: cmix

cmix: $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o cmix

test: cmix
	@bash tests/run_tests.sh

clean:
	rm -f cmix tests/test_*

.PHONY: all test clean
