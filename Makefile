CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -g
CPPFLAGS += -Iinclude
WARN = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion
LIBSRC = src/pps.c src/decode.c src/predict.c src/rate_control.c
OBJ = $(LIBSRC:.c=.o)
.PHONY: all clean test fuzz sanitize
all: libdsc.a dscdecode
libdsc.a: $(OBJ)
	$(AR) rcs $@ $^
dscdecode: src/main.o libdsc.a
	$(CC) $(CFLAGS) $^ -o $@
%.o: %.c include/dsc.h include/drm/display/drm_dsc.h src/predict.h src/rate_control.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@
test_rc: src/rate_control.c tests/test_rc.c
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) $(WARN) $^ -o $@
test_predict: src/predict.c tests/test_predict.c
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) $(WARN) $^ -o $@
test: all test_rc test_predict
	./test_rc
	./test_predict
	python3 tests/test_cli.py ./dscdecode
fuzz:
	clang $(CPPFLAGS) -Isrc -std=c11 -g -O1 -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined $(LIBSRC) fuzz/fuzz_decode.c -o fuzz_decode
sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined' all
	ASAN_OPTIONS=detect_leaks=0 python3 tests/test_cli.py ./dscdecode
	$(MAKE) CFLAGS='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined' test_rc
	ASAN_OPTIONS=detect_leaks=0 ./test_rc
	$(MAKE) CFLAGS='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined' test_predict
	ASAN_OPTIONS=detect_leaks=0 ./test_predict
clean:
	rm -f $(OBJ) src/main.o libdsc.a dscdecode fuzz_decode fuzz_smoke fuzz_afl test_rc test_predict
.PHONY: fuzz-smoke afl
fuzz-smoke:
	$(CC) $(CPPFLAGS) -Isrc -std=c11 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined $(LIBSRC) fuzz/fuzz_decode.c fuzz/smoke.c -o fuzz_smoke
	python3 tests/make_corpus.py
	ASAN_OPTIONS=detect_leaks=0 ./fuzz_smoke tests/corpus/*
afl:
	afl-clang-fast $(CPPFLAGS) -Isrc -std=c11 -g -O1 $(LIBSRC) fuzz/fuzz_decode.c fuzz/afl_main.c -o fuzz_afl
