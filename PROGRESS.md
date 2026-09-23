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

## Phase 3, part 1: reading switches and discriminators (2026-09-23)

This part is committed before the reference model has run anything, so the
discriminator predictions are fixed before any model output exists.

* Rule 7 switches, runtime, in `struct dsc_options` and `dscdecode --reading`:
  `flat_restart` (OQ-1), `threshold_eq` (OQ-2), `frac_reset` (OQ-3) and a new
  `delay_offset` (OQ-12, the initial-delay boundary, found while designing the
  OQ-3 input). Defaults keep M1 behavior. Both readings of each are recorded
  in RESEARCH.md.
* Correction under a clear text: the §6.8.5.2 restart now happens only when
  the flatness override changes masterQp.
* New `dscdecode --stats` (event counters) and `--trace FILE.csv` (per-group
  QP, bits, fullness, range).
* `tests/discriminators/`: `oq1_flat_restart`, `oq2_threshold_equality`,
  `oq3_fractional_bpp`. They are built by `tests/make_discriminators.py`, a
  rate-control model written separately from the decoder, and checked by
  `tests/test_discriminators.py`. The decoder reproduces every predicted
  output under all 16 switch combinations (48 decodes). The README states
  which reading predicts which output.
* `test_rc` gained hand-calculated cases for each switch and for the restart
  correction.
* Gate: `scripts/ci.sh` green. Fuzz smoke: 1,646,134 libFuzzer executions in
  61 s, then 400,000 deterministic executions, no findings.

## Phase 3, part 2: model-comparison harness (2026-09-23)

* `tools/compare_model` reads the model path from `DSCDECODE_MODEL_BIN`. It
  prints SKIP and exits 77 when that is unset, and `scripts/ci.sh` counts 77
  as a pass. Modes:
  * `image`: the model encodes, then the model and dscdecode both decode.
  * `bitstream`: a raw PPS and payload, decoded by both.
  * `discriminators`: which prediction the model's decode matches.
  * `--self-test`.

  It reports the first differing sample (x, y, component, model value,
  dscdecode value) and the number of differing samples and pixels.
  `--all-readings` decodes with all 16 switch combinations. Runs go to
  `~/dsc-runs/compare/`.
* `tools/run_corpus` runs the image comparison over every image in a
  directory outside the repo (default `~/vesa-corpus/`). It reads PPM, DPX,
  PNG and PGM, plus JPEG if Pillow is present, and writes a Markdown table.
  It refuses a directory inside the repository. `~/vesa-corpus/` does not
  exist at the moment.
* Model interface, from README.TXT, the .cfg files, and files the model wrote
  (rule 1). I encoded one synthetic 480×108 gradient under
  `~/dsc-runs/format-discovery/` to learn the formats. No pixel comparison was
  made and no discriminator was run.
  * The model reports "version 1.67".
  * `.dsc` layout: the bytes `DSCF`, the 128-byte PPS, then the payload. The
    chunk order was checked two ways, without decoding. A two-slice-per-line
    encode equals the line-interleaved chunks of single-slice encodes of the
    left and right halves. A two-slice-row encode equals the top-half payload
    followed by the bottom-half payload. That is the DSC 1.1 §4.2.2 order
    dscdecode already reads.
  * Decode mode (`FUNCTION 2`, `PPM_FILE_OUTPUT 1`) writes `NAME.out.ppm`.
  * README.TXT's first line refers to "test model notes at the bottom", but no
    such section exists in the installed file.
* `tests/fake_model.py` is a stand-in with the same command-line and file
  conventions. `tests/test_compare_model.py` uses it in `make test` to check
  the harness: SKIP, container handling, config generation, mismatch
  reporting, `--all-readings`, discriminator verdicts, the PNG reader, and
  `run_corpus`. Writing the image-mode check found and fixed a payload-size
  error for pictures whose height is not a multiple of the slice height.
* `scripts/ci.sh` has a new `model` step. The GitHub workflow has the same
  step, which reports SKIP there.
* Gate: `scripts/ci.sh` green with `DSCDECODE_MODEL_BIN` unset; the model
  step reported SKIP. Fuzz smoke: 1,654,396 libFuzzer executions in 61 s,
  then 400,000 deterministic executions, no findings.
