#!/usr/bin/env bash
# Local CI. Runs the same steps as .github/workflows/ci.yml.
#
#   scripts/ci.sh            all steps, in order
#   scripts/ci.sh STEP...    only the named steps
#
# Steps: build test fixtures sanitize fuzz model
#
# Environment:
#   CI_FUZZ_SECONDS      libFuzzer smoke duration (default 60)
#   CI_FUZZ_DIR          writable libFuzzer corpus (default: a fresh temp dir;
#                        never tests/corpus, which holds the tracked seeds)
#   DSCDECODE_MODEL_BIN  reference model for the model step; unset means SKIP
set -euo pipefail
cd "$(dirname "$0")/.."

FUZZ_SECONDS=${CI_FUZZ_SECONDS:-60}
# Same strictness as `make sanitize`: leaks reported, UB fatal.
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}

declare -a RESULTS=()
note() { printf '\n==> %s\n' "$*"; }

step_build() {
    make clean
    make CFLAGS='-O2 -g -Werror' all test_rc test_predict
}

step_test() {
    make CFLAGS='-O2 -g -Werror' test
}

# The generators must reproduce every committed fixture byte for byte.
step_fixtures() {
    local work
    work=$(mktemp -d)
    cp -r tests "$work/"
    (cd "$work/tests" && python3 make_vectors.py >/dev/null &&
        python3 make_transition_vectors.py && python3 make_corpus.py &&
        python3 make_bp_vectors.py >/dev/null && python3 make_discriminators.py >/dev/null)
    diff -r tests/fixtures "$work/tests/fixtures"
    diff -r tests/corpus "$work/tests/corpus"
    diff -r tests/discriminators "$work/tests/discriminators"
    rm -rf "$work"
}

step_sanitize() {
    make sanitize
}

# On failure the work directory is kept and holds libFuzzer's crash input.
step_fuzz() {
    local work=${CI_FUZZ_DIR:-} temporary=0
    if [ -z "$work" ]; then work=$(mktemp -d); temporary=1; fi
    mkdir -p "$work"
    make fuzz
    ./fuzz_decode -max_total_time="$FUZZ_SECONDS" -max_len=65536 -timeout=2 \
        -artifact_prefix="$work/" -print_final_stats=1 "$work" tests/corpus
    [ "$temporary" -eq 0 ] || rm -rf "$work"
    make fuzz-smoke
}

# Needs the licensed reference model (never on GitHub). SKIP counts as a pass.
# With the model: a small image comparison plus every discriminator, each of
# which must match exactly one of its two predictions.
step_model() {
    local status=0
    make CFLAGS='-O2 -g -Werror' all
    tools/compare_model --self-test || status=$?
    if [ "$status" -eq 77 ]; then
        RESULTS+=("model: SKIP (DSCDECODE_MODEL_BIN not set)")
        return 0
    fi
    [ "$status" -eq 0 ] || return "$status"
    tools/compare_model discriminators
}

ALL=(build test fixtures sanitize fuzz model)
[ "$#" -gt 0 ] || set -- "${ALL[@]}"
for step in "$@"; do
    case " ${ALL[*]} " in *" $step "*) ;; *) echo "unknown step: $step" >&2; exit 2;; esac
    note "$step"
    before=${#RESULTS[@]}
    "step_$step"
    [ "${#RESULTS[@]}" -gt "$before" ] || RESULTS+=("$step: PASS")
done
note "summary"
printf '%s\n' "${RESULTS[@]}"
