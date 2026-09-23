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
	python3 tests/test_discriminators.py ./dscdecode
fuzz:
	clang $(CPPFLAGS) -Isrc -std=c11 -g -O1 -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined $(LIBSRC) fuzz/fuzz_decode.c -o fuzz_decode
# UBSan must abort, not print and continue, or a finding would not fail the
# run. Leak detection is on by default; hosts where LeakSanitizer cannot run
# (see RESEARCH.md, M1 container) can set ASAN_OPTIONS=detect_leaks=0.
SANFLAGS = -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined
ASAN_OPTIONS ?= detect_leaks=1
UBSAN_OPTIONS ?= halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS UBSAN_OPTIONS
sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='$(SANFLAGS)' all
	python3 tests/test_cli.py ./dscdecode
	python3 tests/test_discriminators.py ./dscdecode
	$(MAKE) CFLAGS='$(SANFLAGS)' test_rc
	./test_rc
	$(MAKE) CFLAGS='$(SANFLAGS)' test_predict
	./test_predict
clean:
	rm -f $(OBJ) src/main.o libdsc.a dscdecode fuzz_decode fuzz_smoke fuzz_afl test_rc test_predict
.PHONY: fuzz-smoke afl
fuzz-smoke:
	$(CC) $(CPPFLAGS) -Isrc $(SANFLAGS) -std=c11 $(LIBSRC) fuzz/fuzz_decode.c fuzz/smoke.c -o fuzz_smoke
	python3 tests/make_corpus.py
	./fuzz_smoke tests/corpus/*
afl:
	afl-clang-fast $(CPPFLAGS) -Isrc -std=c11 -g -O1 $(LIBSRC) fuzz/fuzz_decode.c fuzz/afl_main.c -o fuzz_afl
