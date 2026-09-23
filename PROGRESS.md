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

## Phase 4: robustness (2026-09-23)

* The fuzz target now also decodes each input under one combination of the
  reading switches, taken from the payload bytes so the choice is
  reproducible. It collects statistics and a per-group trace, so those paths
  are fuzzed too. The discriminator inputs joined the seed corpus (13 seeds).
* libFuzzer run with clang 21.1.8, ASan and UBSan (UB fatal), and
  LeakSanitizer on:
  `-fork=6 -max_total_time=1800 -max_len=65536 -timeout=2 -seed=20260923`,
  started 2026-09-23T18:19:43Z, finished 18:49:46Z.

  | Measure | Result |
  |---|---|
  | Wall time | 1,802 s (30 min), 6 worker processes |
  | Executions | 123,075,596 |
  | Crashes / timeouts / OOMs / leaks | 0 / 0 / 0 / 0 (no artifacts written) |
  | libFuzzer coverage (fork mode) | cov 428, ft 2,467, corpus 625 units |
  | Source coverage of final corpus plus seeds (llvm-cov) | lines 97.0% (588/606), branches 81.4% (633/778), regions 93.7% |
  | Same, seeds only, for comparison | lines 91.9%, branches 65.2% |

  The 18 lines not reached are `dsc_strerror` (called only by the CLI), an
  unused accessor, and three defensive returns the decoder cannot reach
  through its own call pattern. Those are the syntax-error return after a
  successful reservoir refill, `signed_size` beyond 10 bits, and the RC
  step's argument check.
* No crash, so no regression inputs were added. This is a finite run, not a
  proof of crash freedom.
* The M1 run reported "cov 1,281" from a single-process run; fork mode counts
  differently, so the two libFuzzer coverage numbers are not comparable. The
  llvm-cov figures above are the comparable measure.
* Gate: `scripts/ci.sh` green (model step SKIP). Fuzz smoke: 1,276,188
  libFuzzer executions in 61 s, then 520,000 deterministic executions.

## Phase 2, part 1: block prediction (2026-09-23)

* BP implemented in `src/predict.c` from DSC 1.1 §6.4.2, §6.4.4.1 and
  §7.5.2.1, with the bpSad equation as corrected in DSC 1.2b §6.4.4.1.
  BP-enabled PPS are now decoded rather than rejected. `dsc_options_init`
  moved to `src/options.c` so the predictor and RC tests can both link it.
* Rule 7 switches:
  * `bp_left` (OQ-4, the question the brief names): `replicate`, the default
    by analogy with §6.4.1's rule for previous-line samples outside the
    slice, or `midpoint`.
  * `bp_edge` (OQ-13, new): which previous-line sample `lastEdgeCount` is
    read at, `window` (hPos+2, default) or `before` (hPos−1).
  * `bp_sad` (OQ-10): `shift` (1.1 prose and 1.2b, default) or `clip` (1.1's
    printed formula).

  Recorded in RESEARCH.md. `--stats` gains `bp_groups` and
  `bp_left_differs`.
* Fixtures `bp_left_edge` (BP from hPos 15, the earliest possible group) and
  `bp_slice_boundary` (two slices; BP just right of the slice boundary; a
  partial last group that must not use BP). Expected pixels are derived by
  hand in `research/bp-worked-note.md`. `tests/make_bp_vectors.py` builds the
  bitstreams: a lossless QP-0 constructor with its own BP search. Both
  fixtures decode bit-exact under all 8 BP reading combinations (CLI checks:
  18 → 20).
* Prediction trace suite (`test_predict`): hand-derived BP cases for the
  hPos 9 counting rule, the copy from |vector| pixels left, BP disabled, the
  partial-group rule, the edge gate, and both left-boundary readings,
  including the statistics.
* Discriminator `tests/discriminators/oq4_bp_left`: a 30×2 input where the
  first BP group is hPos 15 under `midpoint` and hPos 18 under `replicate`.
  Its predictions are in the README, committed before any model run on it.
  Discriminator decodes: 48 → 56.
* The fuzz target also varies the BP switches. BP fixtures and the OQ-4 input
  are seeds (16 total). A 2-minute libFuzzer precheck of the BP path in a
  scratch copy ran 958,981 executions with no finding. The 30-minute run
  follows in part 2.
* Gate: `scripts/ci.sh` green (model step SKIP). Fuzz smoke: 779,414
  libFuzzer executions in 61 s, then 640,000 deterministic executions.
