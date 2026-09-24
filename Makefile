CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -g
CPPFLAGS += -Iinclude
WARN = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion
LIBSRC = src/pps.c src/format.c src/decode.c src/predict.c src/rate_control.c src/options.c
LIBOBJ = $(LIBSRC:.c=.o)
HEADERS = include/dsc.h include/drm/display/drm_dsc.h src/format.h src/predict.h src/rate_control.h
FUZZSRC = $(LIBSRC) fuzz/fuzz_decode.c

# Each build flavor has its own directory under build/. The file .flags in
# each directory records the compiler and the flags; it is rewritten only when
# that text changes, so a change of compiler or flags rebuilds the flavor.
BUILD = build
REL = $(BUILD)/release
SAN = $(BUILD)/sanitize
FUZ = $(BUILD)/fuzz
SMK = $(BUILD)/fuzz-smoke
AFL = $(BUILD)/afl

# UBSan must abort, not print and continue, or a finding would not fail the
# run. Leak detection is on by default; hosts where LeakSanitizer cannot run
# (see RESEARCH.md, M1 container) can set ASAN_OPTIONS=detect_leaks=0.
SANFLAGS = -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined
FUZZCC = clang
FUZZFLAGS = -std=c11 -g -O1 -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined
AFLCC = afl-clang-fast
AFLFLAGS = -std=c11 -g -O1
ASAN_OPTIONS ?= detect_leaks=1
UBSAN_OPTIONS ?= halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS UBSAN_OPTIONS

version = $(shell $(1) --version 2>/dev/null | head -n 1)
$(REL)/.flags: STAMP = $(call version,$(CC)) | $(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) | $(AR)
$(SAN)/.flags: STAMP = $(call version,$(CC)) | $(CC) $(CPPFLAGS) $(SANFLAGS) $(WARN)
$(FUZ)/.flags: STAMP = $(call version,$(FUZZCC)) | $(FUZZCC) $(CPPFLAGS) -Isrc $(FUZZFLAGS)
$(SMK)/.flags: STAMP = $(call version,$(CC)) | $(CC) $(CPPFLAGS) -Isrc $(SANFLAGS) -std=c11
$(AFL)/.flags: STAMP = $(call version,$(AFLCC)) | $(AFLCC) $(CPPFLAGS) -Isrc $(AFLFLAGS)

$(BUILD)/%/.flags: FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(STAMP)' | cmp -s - $@ || printf '%s\n' '$(STAMP)' > $@

.PHONY: all release clean test fuzz sanitize fuzz-smoke afl FORCE
# Keep objects: pattern-rule chains would otherwise delete them as intermediates.
.SECONDARY:
all release: $(REL)/libdsc.a $(REL)/dscdecode

# Release flavor: $(CFLAGS). Sanitizer flavor: $(SANFLAGS). Same sources.
$(REL)/%: FLAGS = $(CFLAGS)
$(SAN)/%: FLAGS = $(SANFLAGS)

$(REL)/src/%.o: src/%.c $(HEADERS) $(REL)/.flags
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) $(WARN) -c $< -o $@
$(SAN)/src/%.o: src/%.c $(HEADERS) $(SAN)/.flags
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(FLAGS) $(WARN) -c $< -o $@
$(BUILD)/%/libdsc.a: $(addprefix $(BUILD)/%/,$(LIBOBJ))
	$(AR) rcs $@ $^
$(BUILD)/%/dscdecode: $(BUILD)/%/src/main.o $(BUILD)/%/libdsc.a
	$(CC) $(FLAGS) $^ -o $@
$(BUILD)/%/test_rc: src/rate_control.c src/format.c src/options.c tests/test_rc.c $(HEADERS) $(BUILD)/%/.flags
	$(CC) $(CPPFLAGS) -Isrc $(FLAGS) $(WARN) $(filter %.c,$^) -o $@
$(BUILD)/%/test_predict: src/predict.c src/format.c src/options.c tests/test_predict.c $(HEADERS) $(BUILD)/%/.flags
	$(CC) $(CPPFLAGS) -Isrc $(FLAGS) $(WARN) $(filter %.c,$^) -o $@

test: all $(REL)/test_rc $(REL)/test_predict
	$(REL)/test_rc
	$(REL)/test_predict
	python3 tests/test_cli.py $(REL)/dscdecode
	python3 tests/test_discriminators.py $(REL)/dscdecode
	python3 tests/test_compare_model.py $(REL)/dscdecode

sanitize: $(SAN)/dscdecode $(SAN)/test_rc $(SAN)/test_predict
	python3 tests/test_cli.py $(SAN)/dscdecode
	python3 tests/test_discriminators.py $(SAN)/dscdecode
	$(SAN)/test_rc
	$(SAN)/test_predict

$(FUZ)/fuzz_decode: $(FUZZSRC) $(HEADERS) $(FUZ)/.flags
	$(FUZZCC) $(CPPFLAGS) -Isrc $(FUZZFLAGS) $(FUZZSRC) -o $@
fuzz: $(FUZ)/fuzz_decode

$(SMK)/fuzz_smoke: $(FUZZSRC) fuzz/smoke.c $(HEADERS) $(SMK)/.flags
	$(CC) $(CPPFLAGS) -Isrc $(SANFLAGS) -std=c11 $(FUZZSRC) fuzz/smoke.c -o $@
fuzz-smoke: $(SMK)/fuzz_smoke
	python3 tests/make_corpus.py
	$(SMK)/fuzz_smoke tests/corpus/*

$(AFL)/fuzz_afl: $(FUZZSRC) fuzz/afl_main.c $(HEADERS) $(AFL)/.flags
	$(AFLCC) $(CPPFLAGS) -Isrc $(AFLFLAGS) $(FUZZSRC) fuzz/afl_main.c -o $@
afl: $(AFL)/fuzz_afl

# Also removes the outputs of the single-directory layout used before build/.
clean:
	rm -rf $(BUILD)
	rm -f $(LIBOBJ) src/main.o libdsc.a dscdecode fuzz_decode fuzz_smoke fuzz_afl test_rc test_predict
