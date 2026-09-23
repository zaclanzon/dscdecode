# M2 progress log

This log records what each M2 phase did and the results it measured. All
model-comparison results are recorded here and nowhere else. The project
status is unchanged: a working, tested checkpoint, not conformance-validated.

Phase order for M2: R, 0, 1, 3, 4, 2, 5.

## Toolchain

| Tool | Version |
|---|---|
| GCC (`cc`, used by `make`, `make sanitize`) | 15.2.0 (Ubuntu 15.2.0-16ubuntu1) |
| Clang (libFuzzer, `make fuzz`) | 21.1.8 (Ubuntu 21.1.8-6ubuntu1) |
| Python | 3.14.4 |
| Host | Linux 7.0.0-34-generic, x86-64 |

## Session baseline (Phase R, 2026-09-23)

Measured on m2-work at 429c4e8 "M1 baseline (Sept 20 checkpoint)", before any
M2 change. It matches the M1 expected results.

| Suite | Plain build (`make test`) | ASan + UBSan (`make sanitize`) |
|---|---|---|
| Hand-derived image fixtures, bit-exact | 8 / 8 | 8 / 8 |
| CLI checks (`tests/test_cli.py`) | 18 / 18 | 18 / 18 |
| Rate-control trace suite (`test_rc`) | pass | pass |
| Prediction trace suite (`test_predict`) | pass | pass |
| Compiler warnings (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`) | 0 | 0 |

`make fuzz` links with clang 21.1.8. A 20,000-execution libFuzzer check ran
without error. LeakSanitizer works on this host, so M2 fuzz runs keep leak
detection on, unlike the M1 container, which needed `detect_leaks=0`.

Lines of code at baseline (`wc -l`, all lines):

| Area | Files | Lines |
|---|---|---|
| Library and CLI | `src/*.c`, `src/*.h`, `include/dsc.h` | 732 |
| Fuzz entry points | `fuzz/*.c` | 70 |
| Tests | `tests/*.c`, `tests/*.py` | 563 |
| Total | | 1,365 |

Phases complete at session start: none. Phase 0 had been started: the
baseline commit and the m2-work branch existed. RESEARCH.md, the provenance
paragraph, and this file did not.

Process note: an early libFuzzer check was pointed at `tests/corpus/` as its
writable corpus and added 71 generated inputs there. They were moved out of
the tree before any commit. Later runs write only to `~/dsc-runs/`.

## Phase 0: finish the baseline (2026-09-23)

* RESEARCH.md added, trimmed from the M1 archive copy (SHA-256
  678f594e…a0eae). Removed: the numerical audit appendix, the Annex E Table
  E-5 values (one sentence and the section reference remain), the PPS byte
  table (now a pointer to `struct drm_dsc_picture_parameter_set`), the model
  archive record with its file and function map and copyright quotation, the
  model-derived resolutions table, and every mirror link. The mirror line in
  "Sources" also lost its mirror revision and model archive name and hash. A
  new "Open questions" table lists OQ-1 to OQ-11. It includes the questions
  that the removed text had answered from model source.
* THIRD_PARTY.md gained the M1 provenance paragraph.
* The officially obtained `DSC_v1.1.pdf` has the same SHA-256 as the copy M1
  cited (9f5a1a54…4da65), so M1's DSC 1.1 section and page citations refer to
  the same document.
* Second commit: the VESA registration record (2026-09-23) went into the
  Track B section of RESEARCH.md and a new final paragraph of
  THIRD_PARTY.md. The provenance paragraph was not changed.
* Gate: `make test` and `make sanitize` green, same counts as the baseline.

## Phase 1: repo hygiene (2026-09-23)

* README: fixed "at most255" and "at4096", added the motivation paragraph at
  the top, pointed the libFuzzer example at a writable `fuzz-corpus/` instead
  of the tracked seed directory, and replaced the stale "complete PPS map"
  reference.
* `scripts/ci.sh` runs build (warnings are errors), the test suites, fixture
  reproducibility (generators must recreate every committed fixture byte for
  byte), the ASan/UBSan suites, and a fuzz smoke step (60 s libFuzzer, then
  the deterministic GCC smoke campaign). `.github/workflows/ci.yml` calls the
  same script step by step. It has not run, since the repository is not on
  GitHub.
* Sanitizer builds were made stricter. `make sanitize`, `make fuzz`, and
  `make fuzz-smoke` now pass `-fno-sanitize-recover=undefined`. Without it,
  UBSan prints a report and the process exits 0, so neither the test run nor
  libFuzzer would have failed on undefined behavior. Leak detection is now on
  by default.
* Gate: `scripts/ci.sh` green. Fuzz smoke: 1,760,114 libFuzzer executions in
  61 s (28,854 exec/s), then 400,000 deterministic executions, no findings.
