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

## M2 baseline (Phase R, 2026-09-23)

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

Phases complete when M2 started: none. Phase 0 had been started: the
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

* Reading switches, runtime, in `struct dsc_options` and `dscdecode --reading`:
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
  (black-box use only). One synthetic 480×108 gradient was encoded under
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
* Reading switches:
  * `bp_left` (OQ-4): `replicate`, the default
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

## Phase 2, part 2: fuzzing the BP path (2026-09-23)

* libFuzzer on the BP-enabled decoder, same settings as Phase 4 except
  `-seed=20260924`. It started from Phase 4's final corpus plus the 16
  tracked seeds. Run 2026-09-23T18:55:21Z to 19:25:24Z.

  | Measure | Result |
  |---|---|
  | Wall time | 1,802 s (30 min), 6 worker processes |
  | Executions | 56,073,894 (fewer than Phase 4: the BP search and a third decode per input) |
  | Crashes / timeouts / OOMs / leaks | 0 / 0 / 0 / 0 (no artifacts written) |
  | libFuzzer coverage (fork mode) | cov 483, ft 2,879 (Phase 4: 428, 2,467) |
  | Source coverage of final corpus plus seeds (llvm-cov) | lines 97.3% (655/673), branches 82.9% (711/858); `predict.c` lines 100%, branches 87.9% |

* No crash, so no regression inputs were added. Fuzz executions so far in
  M2: 123,075,596 (Phase 4) + 56,073,894 (this run) + 958,981 (BP
  precheck) + the 60-second CI smoke runs listed in each phase.
* Gate: `scripts/ci.sh` green (model step SKIP). Fuzz smoke: 826,572
  libFuzzer executions in 61 s, then 640,000 deterministic executions.

## Phase 5, part 1: comparison with the VESA model (2026-09-23)

Setup. `DSCDECODE_MODEL_BIN=/usr/local/bin/dsc-ref`, driven only through
`tools/compare_model`. The model's interface came from its README.TXT, its
.cfg files and the files it writes. Encoder configurations start from
`test_dsc_1_1.cfg`: DSC 1.1, 8 bpc, RGB 4:4:4, CBR, with the matching
`rc_8bpc_<bpp>bpp.cfg`. The model's `.dsc` files follow DSC 1.1 Annex A
("DSCF", the 128-byte PPS, then the payload). Each check has the model
encode a picture, then the model and dscdecode both decode the model's
bitstream, and the two outputs are compared bit for bit. All model input,
output and logs are under `~/dsc-runs/` (phase5-*).

`~/vesa-corpus/` does not exist, so the corpus step (every corpus image at
8 bpp, BP on and off, 1/2/4 slices per line) was skipped. The same settings
were run on seven synthetic pictures made for this phase instead (below).

### How the model's behavior was found

The first comparisons diverged from the M1 readings within the first
groups. The per-group QP the model used was recovered from its decoded
pixels: a scratch build (outside the repository) decodes with a forced QP
per group and keeps the QP that reproduces the model's pixels. Candidate
readings were then fitted against the recovered QP sequences, and each fit
was checked by full bit-exact decodes. This gave reading switches for
questions that the M1 code had fixed silently, each with the model's
reading as the default and the M1 reading kept:

| Switch (question) | M1 behavior | Model behavior, now the default |
|---|---|---|
| `incr_order` (OQ-5) | `printed` | `swapped` |
| `rc_pipeline` (OQ-11) | `same-group` | `range-lag`: the short-term RC uses the previous step's range, and range 0 before the first group |
| `scale_dec` (OQ-14) | `from-group-1` | `from-group-0` |
| `partial_target` (OQ-15) | `three` | `pixels` |
| `very_flat` (OQ-16) | `group-qp` | `previous-qp` (a third reading, added this phase, see below) |
| `partial_padding` (OQ-17) | `reject` | `accept` |
| `flat_max_qp` (OQ-18) | `own` | `previous` |

Existing switches whose default changed: `flat_restart` to `in-flight`
(OQ-1) and `bp_left` to `midpoint` (OQ-4). New `--stats` counters:
`incr_order_differs`, `range_lag_differs`, `partial_groups`,
`very_flat_low_qp`, `padding_nonzero`, `flat_max_qp_differs`. Each new
switch has a hand-calculated case in `tests/test_rc.c`, and the fuzz target
varies all of them.

### Fixtures

The model decoded each fixture's bitstream. Its output equals both the
hand-derived expected image and dscdecode's output.

| Fixture | model vs hand-derived expected | model vs dscdecode (defaults) |
|---|---|---|
| flat | match (bit-exact) | match (bit-exact) |
| gradient | match (bit-exact) | match (bit-exact) |
| checker | match (bit-exact) | match (bit-exact) |
| mpp | match (bit-exact) | match (bit-exact) |
| ich | match (bit-exact) | match (bit-exact) |
| color_crop | match (bit-exact) | match (bit-exact) |
| qp_transition | match (bit-exact) | match (bit-exact) |
| qp_flatness | match (bit-exact) | match (bit-exact) |
| bp_left_edge | match (bit-exact) | match (bit-exact) |
| bp_slice_boundary | match (bit-exact) | match (bit-exact) |

The model also decoded `invalid_partial_residual` and `invalid_partial_ich`
(nonzero padding residuals; an unreplicated padding ICH index) without an
error. Its output matches dscdecode under `partial_padding=accept`;
`reject` refuses both. This is the OQ-17 evidence. The model's encoder wrote
canonical padding in all 38,034 partial groups of the stream set below, so
its streams alone cannot separate the two readings.

### Synthetic pictures, 8 bpp

Seven 8-bit pictures, 640×216 except s06 (637×125): a smooth gradient,
uniform noise, text-like strokes, flat blocks, sine waves, an odd-size
picture, and a mix. Slices are 108 lines high (s06: 108 lines, so the second
slice row is cropped).

First run, with the defaults at that point (`very_flat=as-signaled`,
`flat_max_qp=own`): 34 of 42 matched. The other 8 failed to decode
("invalid compressed slice"), all with 2 or 4 slices per line. The CLI writes
no output on a decode error. The first differing sample below comes from a
scratch build that keeps decoding after the error; values are model/dscdecode.

| Picture | BP | Slices/line | Decode error at | First differing sample | Cause |
|---|---|---|---|---|---|
| s02_noise | off | 2 | slice 3, group 1712 (an ICH index to an invalidated history entry) | (638,123) R 233/129 | OQ-18: the flatness override at slice 3 group 1711 was skipped because that group's own QP was 13, range 14's maximum. The model checks the previous group's QP (11) and applies it (QP 13 → 9). Forcing QP 9 for that one group reproduces the whole picture. |
| s05_waves | off | 2 | slice 2, group 214 | (24,109) R 226/227 | OQ-16: a very-flat signal at slice 2 group 115 (QP 4, previous group QP 5). The model demotes it to somewhat flat. |
| s05_waves | off | 4 | slice 4, group 270 | (344,110) G 44/43 (slice 6) | OQ-16, three events in slices 4 and 6 |
| s05_waves | on | 2 | slice 2, group 214 | (24,109) R 226/227 | OQ-16 |
| s05_waves | on | 4 | slice 4, group 270 | (344,110) G 44/43 (slice 6) | OQ-16 |
| s06_odd_size | off | 2 | slice 1, group 429 | (319,3) R 197/221 | OQ-16 |
| s06_odd_size | on | 2 | slice 1, group 429 | (319,3) R 197/221 | OQ-16 |
| s07_mixed | off | 2 | slice 3, group 1712 | (638,123) R 233/129 | OQ-18, the same event as s02 |

OQ-16. §6.8.5.2 demotes a very-flat signal when the current masterQp is
below 7 and does not say which group's QP that is. Neither existing reading
fit: `group-qp` fixed s05 and s06 with 2 slices per line but broke 7
streams that had matched (s05 with 1 slice, s06 with 4, s07 with 2 and 4).
Deciding each very-flat event with a flagged-group QP other than 5 (where
both results are QP 1) separately, by the choice that keeps the decode
matching the model longest, gave 14 decisive events in 9 streams. The model
demoted 6 of them; in all 6 the group before the flagged group was decoded
at QP 4 to 6. It kept the other 8; in all 8 that group was at QP 7. The
same below-7 test on the flagged group's own QP, on the QP queued for the
next group, or on the QP of the group that sent the flag does not separate
the two sets. This is a third reading, `very_flat=previous-qp`,
now the default. OQ-18 is the same unspecified "current masterQp" in the
same section, for the range-14 maximum. It got its own switch,
`flat_max_qp`, since the two uses could in principle differ. Both are in
RESEARCH.md.

Final run, current defaults (`~/dsc-runs/phase5-r3`):

| Picture | bpp | BP | Slices/line | Result | First differing sample | Diff count |
|---|---|---|---|---|---|---|
| s01_gradient | 8 | off | 1 | match | | 0 |
| s01_gradient | 8 | off | 2 | match | | 0 |
| s01_gradient | 8 | off | 4 | match | | 0 |
| s01_gradient | 8 | on | 1 | match | | 0 |
| s01_gradient | 8 | on | 2 | match | | 0 |
| s01_gradient | 8 | on | 4 | match | | 0 |
| s02_noise | 8 | off | 1 | match | | 0 |
| s02_noise | 8 | off | 2 | match | | 0 |
| s02_noise | 8 | off | 4 | match | | 0 |
| s02_noise | 8 | on | 1 | match | | 0 |
| s02_noise | 8 | on | 2 | match | | 0 |
| s02_noise | 8 | on | 4 | match | | 0 |
| s03_text | 8 | off | 1 | match | | 0 |
| s03_text | 8 | off | 2 | match | | 0 |
| s03_text | 8 | off | 4 | match | | 0 |
| s03_text | 8 | on | 1 | match | | 0 |
| s03_text | 8 | on | 2 | match | | 0 |
| s03_text | 8 | on | 4 | match | | 0 |
| s04_flat_blocks | 8 | off | 1 | match | | 0 |
| s04_flat_blocks | 8 | off | 2 | match | | 0 |
| s04_flat_blocks | 8 | off | 4 | match | | 0 |
| s04_flat_blocks | 8 | on | 1 | match | | 0 |
| s04_flat_blocks | 8 | on | 2 | match | | 0 |
| s04_flat_blocks | 8 | on | 4 | match | | 0 |
| s05_waves | 8 | off | 1 | match | | 0 |
| s05_waves | 8 | off | 2 | match | | 0 |
| s05_waves | 8 | off | 4 | match | | 0 |
| s05_waves | 8 | on | 1 | match | | 0 |
| s05_waves | 8 | on | 2 | match | | 0 |
| s05_waves | 8 | on | 4 | match | | 0 |
| s06_odd_size | 8 | off | 1 | match | | 0 |
| s06_odd_size | 8 | off | 2 | match | | 0 |
| s06_odd_size | 8 | off | 4 | match | | 0 |
| s06_odd_size | 8 | on | 1 | match | | 0 |
| s06_odd_size | 8 | on | 2 | match | | 0 |
| s06_odd_size | 8 | on | 4 | match | | 0 |
| s07_mixed | 8 | off | 1 | match | | 0 |
| s07_mixed | 8 | off | 2 | match | | 0 |
| s07_mixed | 8 | off | 4 | match | | 0 |
| s07_mixed | 8 | on | 1 | match | | 0 |
| s07_mixed | 8 | on | 2 | match | | 0 |
| s07_mixed | 8 | on | 4 | match | | 0 |

With BP on, only s02, s03, s06 and s07 contain BP groups.

### Flatness pictures (OQ-16, OQ-18)

Five pictures built to put flatness signals next to QP changes and the
range-14 maximum: noise at four amplitudes with flat patches or stripes,
640×216, and one 637×125. Each was encoded at 6 and 8 bpp with 1, 2 and 4
slices per line (30 streams), BP off, and decoded under each reading:

| Reading | Streams matching the model | Diverging |
|---|---|---|
| defaults (`very_flat=previous-qp`, `flat_max_qp=previous`) | 30 | 0 |
| `very_flat=group-qp` | 8 | 22 |
| `very_flat=as-signaled` | 9 | 21 |
| `flat_max_qp=own` | 2 | 28 |

### Every switch flipped, on every model stream

The stream set is the 92 streams the model decoded in this phase: the 42
matrix streams, the 30 flatness streams, 7 earlier validation encodes of the
synthetic pictures (8 bpp, one slice per line), 3 ramps 108 lines high and
190, 191 and 192 pixels wide (partial last groups), and the 10 fixtures. Each was decoded with the
defaults, then once per alternative reading with everything else default.
Defaults match the model on 92 of 92.

| Alternative reading | Streams still matching | Diverging (of which decode errors) | Events in the stream set (`--stats`, defaults) |
|---|---|---|---|
| `flat_restart=next-cycle` | 28 | 64 (52) | 57,464 flatness overrides whose queued QP differs, in 72 streams |
| `threshold_eq=upper` | 23 | 69 (58) | 16,777 exact threshold equalities, in 84 streams |
| `frac_reset=literal` | 92 | 0 (0) | none: every stream uses whole-number bpp |
| `delay_offset=exclusive` | 20 | 72 (56) | |
| `bp_left=replicate` | 83 | 9 (0) | 622 BP decisions that differ, in 12 streams |
| `bp_edge=before` | 82 | 10 (0) | BP used in 14 streams (145,125 groups) |
| `bp_sad=clip` | 83 | 9 (0) | |
| `incr_order=printed` | 12 | 80 (59) | 69,629 increments permitted differently, in 82 streams |
| `rc_pipeline=same-group` | 16 | 76 (59) | 179,017 groups whose ranges differ, in 92 streams |
| `scale_dec=from-group-1` | 20 | 72 (56) | |
| `partial_target=three` | 19 | 73 (56) | 38,034 partial groups, in 83 streams |
| `very_flat=group-qp` | 59 | 33 (28) | 257 very-flat signals where the readings can differ, in 40 streams |
| `very_flat=as-signaled` | 65 | 27 (26) | |
| `partial_padding=reject` | 92 | 0 (0) | none: no noncanonical padding |
| `flat_max_qp=own` | 62 | 30 (27) | 4,509 flatness signals treated differently, in 30 streams |

Questions without a switch, tested with a scratch build outside the
repository:

* OQ-6: flooring a zero-residual group's decrement at minQp instead of
  minQp/2 diverges on 59 of the 92 streams.
* OQ-8: the saturation matters only below a 9-bit line buffer. The 12
  pictures above (7 synthetic, 5 flatness), encoded at 8 bpp with
  `line_buf_depth` 8 and 1 and 2 slices per line (24 streams), all match
  with the defaults. Without the clamp, 12 of them diverge.

### Discriminators

| Discriminator | Question | Model output matches |
|---|---|---|
| `oq1_flat_restart` | OQ-1 | in-flight (next-cycle differs at x = 93) |
| `oq2_threshold_equality` | OQ-2 | neither; inconclusive (see below) |
| `oq3_fractional_bpp` | OQ-3 | chunk (literal differs at x = 93) |
| `oq4_bp_left` | OQ-4 | midpoint (replicate differs at x = 15, y = 1) |

`oq2_threshold_equality` was built under the M1 readings of OQ-5, OQ-11 and
OQ-14 to OQ-18. Under the model's range-lag pipeline its QP schedule
changes from group 2, so neither prediction applies; the model's output
differs from both from x = 6. dscdecode with the current defaults reports
"truncated input" on it at group 23, while the model outputs a picture
without reporting an error. `oq2b_threshold_equality` asks OQ-2 again under
the current readings. It is added in this part with its predictions in
`tests/discriminators/README.md`; the model has not decoded it yet. OQ-2 is
resolved by the stream set regardless (table above).

Harness change: `tools/compare_model discriminators` now reads the
decoder's defaults. An input that matches neither prediction fails the run
unless its manifest `assumes` differ from those defaults, in which case it
is reported as inconclusive. A model decode failure counts as matching
neither. `tests/test_compare_model.py` checks both outcomes with the fake
model.

### Tests and gate

* `test_rc`: new cases for the third OQ-16 reading and for OQ-18
  (`very_flat_type`, 6 cases; `flat_max_qp`, 4 cases).
* CLI checks: 20 → 22 (the padding fixtures under the default `accept`,
  in addition to the `reject` checks).
* Discriminator decodes: 56 → 72 (`oq2b`, 16 combinations).
* RESEARCH.md: open-questions table rewritten. OQ-1 to OQ-6, OQ-8 and
  OQ-10 to OQ-18 are marked resolved by black-box comparison with the VESA C
  model; OQ-7 (DSC 1.2 only) and OQ-9 (encoder only) stay open.
* The fuzz seed corpus gains the `oq2b` input (16 → 17 seeds).
* Gate: `scripts/ci.sh` green (model step SKIP). Fuzz smoke: 628,564
  libFuzzer executions in 61 s, then 680,000 deterministic executions.

## Phase 5, part 2: oq2b against the model (2026-09-23)

Part 1 (3e41c9e) committed `oq2b_threshold_equality` and its predictions
before the model decoded it. Result:

| Discriminator | Question | Model output matches |
|---|---|---|
| `oq2b_threshold_equality` | OQ-2 | lower (upper differs at x = 93: R 152 model, 132 upper prediction) |

This agrees with the stream set, where `threshold_eq=upper` diverges on 69
of 92 streams. RESEARCH.md now names `oq2b_threshold_equality` as the OQ-2
discriminator.

`scripts/ci.sh` with `DSCDECODE_MODEL_BIN=/usr/local/bin/dsc-ref` passes
every step, including the model step: the self-test image matches
bit-exactly; `oq1`, `oq2b`, `oq3` and `oq4` each match exactly one
prediction (in-flight, lower, chunk, midpoint); `oq2` is reported
inconclusive. Fuzz smoke in that run: 804,438 libFuzzer executions in 61 s,
then 680,000 deterministic executions.

Phase 5 summary: every open question the model can decide is resolved,
with the model's reading as the default and the other readings kept behind
their switches. OQ-7 (DSC 1.2 only) and OQ-9 (encoder only) remain open.
~/vesa-corpus/ is absent, so no corpus images were compared.

Lines of code after Phase 5 (`wc -l`, all lines; baseline in parentheses):

| Area | Lines |
|---|---|
| Library and CLI (`src/*.c`, `src/*.h`, `include/dsc.h`) | 1,265 (732) |
| Fuzz entry points (`fuzz/*.c`) | 85 (70) |
| Tests (`tests/*.c`, `tests/*.py`) | 2,198 (563) |
| Total of the baseline areas | 3,548 (1,365) |
| Tools and scripts, new in M2 (`tools/*`, `scripts/*.sh`) | 761 |

Fuzz executions in M2: 190,188,947 libFuzzer (Phase 4 run
123,075,596; Phase 2 run 56,073,894; BP precheck 958,981; Phase R check
20,000; nine 61-second CI smoke runs 10,060,476) plus 5,040,000
deterministic smoke executions. No crash, timeout, OOM or leak.

* Gate: `scripts/ci.sh` green (model step SKIP). Fuzz smoke: 684,656
  libFuzzer executions in 61 s, then 680,000 deterministic executions.

## Publication preparation (2026-09-23)

A second pass on m2-work, after the M2 phases above, to prepare the
repository for publication. Its four steps are numbered P1 to P4 here to
keep them apart from the M2 phases.

Baseline at 23a0131, before any change. `scripts/ci.sh` green with
`DSCDECODE_MODEL_BIN` unset (model step SKIP) and with it set to
`/usr/local/bin/dsc-ref` (self-test match; `oq1` in-flight, `oq2b` lower,
`oq3` chunk, `oq4` midpoint, `oq2` inconclusive).

| Suite | Result at baseline |
|---|---|
| Image fixtures, bit-exact (8 M1 plus 2 BP) | 10 / 10 |
| CLI checks (`tests/test_cli.py`) | 22 / 22 |
| Discriminator decodes (`tests/test_discriminators.py`) | 72 / 72 |
| `test_rc`, `test_predict` | pass, pass |
| Harness checks with the stand-in model (`tests/test_compare_model.py`) | 9 / 9 |
| ASan + UBSan suites (`make sanitize`) | pass |
| Fuzz smoke | 700,973 libFuzzer executions (61 s), then 680,000 deterministic |

### P1: stale M1 text (2026-09-23)

* RESEARCH.md. The September 20 continuation no longer says BP is
  unsupported, that the checkpoint has no Git metadata, or that Track B is
  unchanged; its remaining-work paragraph is now dated, followed by the M2
  status. "Sources" and the model-availability section now point to the
  2026-09-23 registration instead of saying no form was submitted. The
  sentence saying no decoder implementation exists is dated 2026-09-16 and
  points to Track A. The Track A disposition table marks the rows M2 changed
  (OQ-1, OQ-2, OQ-3, OQ-4, OQ-5, OQ-11, OQ-15), and its closing paragraph is
  in the past tense. Track B's sections "Status as of 2026-09-16", "Official
  channel and actual stopping point" and "Decision for this checkpoint" are
  replaced by a two-entry history: what M1 had on 2026-09-16, and the
  registration on 2026-09-23. The agreement section no longer calls the
  package unacquired. The registration record, the provenance paragraph in
  THIRD_PARTY.md, and the statements that M1 used unofficial copies of the
  specification and the model are unchanged.
* `research/prediction-ambiguities.md`: its M2 note said replication is the
  `bp_left` default. The default has been midpoint since Phase 5.
* `research/rc-ambiguities.md`: an M2 note maps each uncertainty to its
  open-question row.
* Gate: `scripts/ci.sh` green with the model unset and set.

### P2: small fixes and fractional bpp (2026-09-23)

* `oq2_threshold_equality` is marked `superseded_by: oq2b_threshold_equality`
  in `manifest.json` (written by `tests/make_discriminators.py`, so the
  fixture reproducibility check still holds) and in
  `tests/discriminators/README.md`. `tools/compare_model discriminators`
  still has the model decode it and prints the match for the record, but
  reports it as SUPERSEDED and leaves it out of the verdict and the exit
  status. `tests/test_discriminators.py` still decodes it under the 16
  combinations and the readings it assumes. `tests/test_compare_model.py`
  checks the SUPERSEDED report under both pipeline settings of the stand-in
  model, and gains a case (every stub output corrupted) that keeps the
  inconclusive path covered, since oq2 no longer reaches it.
* Padding warning. With `partial_padding=accept` (the default), a decode
  that accepted noncanonical padding prints one line to stderr:
  `warning: N partial group(s) with nonzero padding accepted (DSC 1.1 section
  6.6); --reading partial_padding=reject makes this an error`. The exit
  status does not change. The CLI now always collects the statistics, which
  do not affect decoding, and prints the `stats:` line only with `--stats`.
  CLI checks: 22 → 26 (the warning with count 1 on each padding fixture; a
  two-slice-row stream built from `invalid_partial_residual`, one warning with
  count 2; none for `color_crop`, whose partial groups carry canonical
  padding). The eight image-fixture checks and the two `reject` checks now
  also require that no warning is printed.
* Fractional bpp. The seven Phase 5 synthetic pictures (still present in
  `~/dsc-runs/synthetic/`) were encoded by the model at 7.5 and 9.3125 bpp,
  BP off and on, 1 and 2 slices per line, slice height 108: 56 streams. As
  in Phase 5, the model and dscdecode each decoded the model's bitstream and
  the outputs were compared bit for bit; each stream was also decoded with
  `frac_reset=literal`. Runs: `~/dsc-runs/pub/frac/`.
  * The model accepts both rates with the installed configs.
    `tools/compare_model` includes the nearest RC file (`rc_8bpc_8bpp.cfg`
    for 7.5, `rc_8bpc_10bpp.cfg` for 9.3125) and sets `BITS_PER_PIXEL`
    after it. The PPS carries bits_per_pixel 120 and 149 (sixteenths) and
    the RC file's initial_xmit_delay, 512 and 410. Chunk padding per chunk
    ranges from 0 bits (640-pixel slices) to 7.5 bits (7.5 bpp, 319-pixel
    slices), so both the no-padding and the padding cases occur.
  * `frac_reset=chunk` (default): 56 of 56 match the model bit-exactly.
  * `frac_reset=literal`: 16 match, 11 decode to different pixels, 29 fail
    to decode (27 "invalid compressed slice", 2 "rate-control buffer
    violation"). In every stream the two readings disagree on buffer
    fullness in at least 13,228 groups (`--stats` `frac_differs`); in the 16
    that match, the difference did not change any decoded pixel.
  * This supports OQ-3 = chunk, until now resting on `oq3_fractional_bpp`
    alone. RESEARCH.md cites these streams in the OQ-3 row; no default
    changed.

| Picture | bpp | BP | Slices/line | `chunk` (default) | `literal` | `literal`: first differing sample, model/dscdecode; samples differing |
|---|---|---|---|---|---|---|
| s01_gradient | 7.5 | off | 1 | match | mismatch | (3,97) G 113/112, 226,016 |
| s01_gradient | 7.5 | off | 2 | match | match |  |
| s01_gradient | 7.5 | on | 1 | match | mismatch | (3,97) G 113/112, 225,976 |
| s01_gradient | 7.5 | on | 2 | match | match |  |
| s01_gradient | 9.3125 | off | 1 | match | match |  |
| s01_gradient | 9.3125 | off | 2 | match | match |  |
| s01_gradient | 9.3125 | on | 1 | match | match |  |
| s01_gradient | 9.3125 | on | 2 | match | match |  |
| s02_noise | 7.5 | off | 1 | match | decode error (invalid compressed slice) |  |
| s02_noise | 7.5 | off | 2 | match | decode error (invalid compressed slice) |  |
| s02_noise | 7.5 | on | 1 | match | decode error (invalid compressed slice) |  |
| s02_noise | 7.5 | on | 2 | match | decode error (invalid compressed slice) |  |
| s02_noise | 9.3125 | off | 1 | match | decode error (invalid compressed slice) |  |
| s02_noise | 9.3125 | off | 2 | match | decode error (invalid compressed slice) |  |
| s02_noise | 9.3125 | on | 1 | match | decode error (invalid compressed slice) |  |
| s02_noise | 9.3125 | on | 2 | match | decode error (invalid compressed slice) |  |
| s03_text | 7.5 | off | 1 | match | mismatch | (6,17) R 235/193, 329,345 |
| s03_text | 7.5 | off | 2 | match | match |  |
| s03_text | 7.5 | on | 1 | match | mismatch | (6,17) R 235/193, 329,350 |
| s03_text | 7.5 | on | 2 | match | match |  |
| s03_text | 9.3125 | off | 1 | match | match |  |
| s03_text | 9.3125 | off | 2 | match | match |  |
| s03_text | 9.3125 | on | 1 | match | match |  |
| s03_text | 9.3125 | on | 2 | match | match |  |
| s04_flat_blocks | 7.5 | off | 1 | match | mismatch | (3,23) R 0/180, 161,419 |
| s04_flat_blocks | 7.5 | off | 2 | match | match |  |
| s04_flat_blocks | 7.5 | on | 1 | match | mismatch | (3,23) R 0/180, 160,815 |
| s04_flat_blocks | 7.5 | on | 2 | match | match |  |
| s04_flat_blocks | 9.3125 | off | 1 | match | decode error (rate-control buffer violation) |  |
| s04_flat_blocks | 9.3125 | off | 2 | match | match |  |
| s04_flat_blocks | 9.3125 | on | 1 | match | decode error (rate-control buffer violation) |  |
| s04_flat_blocks | 9.3125 | on | 2 | match | match |  |
| s05_waves | 7.5 | off | 1 | match | mismatch | (321,7) G 210/211, 361,591 |
| s05_waves | 7.5 | off | 2 | match | decode error (invalid compressed slice) |  |
| s05_waves | 7.5 | on | 1 | match | mismatch | (321,7) G 210/211, 361,756 |
| s05_waves | 7.5 | on | 2 | match | decode error (invalid compressed slice) |  |
| s05_waves | 9.3125 | off | 1 | match | mismatch | (327,30) R 164/163, 314,731 |
| s05_waves | 9.3125 | off | 2 | match | decode error (invalid compressed slice) |  |
| s05_waves | 9.3125 | on | 1 | match | mismatch | (327,30) R 164/163, 314,712 |
| s05_waves | 9.3125 | on | 2 | match | decode error (invalid compressed slice) |  |
| s06_odd_size | 7.5 | off | 1 | match | decode error (invalid compressed slice) |  |
| s06_odd_size | 7.5 | off | 2 | match | decode error (invalid compressed slice) |  |
| s06_odd_size | 7.5 | on | 1 | match | decode error (invalid compressed slice) |  |
| s06_odd_size | 7.5 | on | 2 | match | decode error (invalid compressed slice) |  |
| s06_odd_size | 9.3125 | off | 1 | match | decode error (invalid compressed slice) |  |
| s06_odd_size | 9.3125 | off | 2 | match | decode error (invalid compressed slice) |  |
| s06_odd_size | 9.3125 | on | 1 | match | mismatch | (367,3) R 19/18, 220,245 |
| s06_odd_size | 9.3125 | on | 2 | match | decode error (invalid compressed slice) |  |
| s07_mixed | 7.5 | off | 1 | match | decode error (invalid compressed slice) |  |
| s07_mixed | 7.5 | off | 2 | match | decode error (invalid compressed slice) |  |
| s07_mixed | 7.5 | on | 1 | match | decode error (invalid compressed slice) |  |
| s07_mixed | 7.5 | on | 2 | match | decode error (invalid compressed slice) |  |
| s07_mixed | 9.3125 | off | 1 | match | decode error (invalid compressed slice) |  |
| s07_mixed | 9.3125 | off | 2 | match | decode error (invalid compressed slice) |  |
| s07_mixed | 9.3125 | on | 1 | match | decode error (invalid compressed slice) |  |
| s07_mixed | 9.3125 | on | 2 | match | decode error (invalid compressed slice) |  |

* Gate: `scripts/ci.sh` green with the model unset and set.

### P3: corpus comparison, skipped (2026-09-23)

`~/vesa-corpus/` does not exist, so `tools/run_corpus` had no images to run
and no corpus table was made. The 7.5 bpp column that P2 made possible was
not run either. No code or test changed in this step.

* Gate: `scripts/ci.sh` green with the model unset and set.

### P4: final audit (2026-09-23)

Scope: every tracked file on main and m2-work, every text blob in the history
of both branches (13 commits, 222 distinct blobs, before this step's
commit), and the commit messages. Scripts and reports are under
`~/dsc-runs/pub/audit/`; reports that quote specification text are under
`~/vesa-spec/audit/`. The full finding list, including the history locations
that only a history rewrite could remove, went to the maintainer with this
step; history was not rewritten.

* Specification text. The three PDFs were already converted (page counts
  match); `-raw` conversions were added under `~/vesa-spec/`. Runs of 12 or
  more consecutive words shared with the specification text: two, both in
  the doc comments of `include/drm/display/drm_dsc.h`, the Linux header kept
  byte-identical to upstream (THIRD_PARTY.md); not changed. With punctuation
  ignored as well, the same header plus one SCR title quoted in
  `research/prediction-ambiguities.md`, now cited by date and subject
  instead. No blob or commit message shares even 8 consecutive words with
  the E1 errata, whose model source excerpts are therefore not reproduced;
  the "M2 source note" in RESEARCH.md describes the excerpt without quoting
  it.
* Model material. No model source file name, model copyright notice, mirror
  link or model archive hash in any blob. The hashes present identify the
  officially obtained archive, the DSC 1.1 and 1.2 PDFs M1 cited, and two
  copies of this project's research record. The provenance and M1
  disclosure statements are kept unchanged. Statements of model behavior
  come from the black-box runs in this file; the one M1 table entry that
  names a model value is verifiable from the installed `.cfg` files.
* Committed binary and image files. All 102 files in `tests/fixtures`,
  `tests/corpus` and `tests/discriminators` regenerate byte for byte from
  empty directories; none came from dsc-ref output (every expected image the
  model reproduces was committed by a generator before the model ran).
  `scripts/ci.sh fixtures` used to regenerate on top of a copy of `tests/`,
  so a file no generator writes would still have passed. It now empties the
  output directories first (keeping the hand-written discriminator README)
  and packages the seed corpus last; a stray seed now fails the step.
* Third-party code: only `include/drm/display/drm_dsc.h` (Linux, MIT,
  Intel), identical to the installed Linux 7.0.0-34 header. `drm_dp.h` is an
  original shim. No NVIDIA, i915 or other copied code or tables. `src/`
  holds no RC parameter set (the decoder reads every RC value from the PPS);
  the test generators and `tests/test_rc.c` define hand-chosen sets, some
  using the common 8 bpc buffer thresholds. Listed for the maintainer, not
  changed.
* Secrets and personal data. No keys, tokens or passwords. The only email
  in a file is the Intel author line in the vendored header. Five M1 build
  and test logs in `research/` recorded an absolute home path (with a
  username and a tool name) and a container scratch path; at the tip each is
  now the relative directory `dsc-decoder`.
* Git metadata: one author and committer identity on both branches, and a
  `Co-Authored-By` trailer on every commit. Reported only.
* Wording. Every status word the README may not claim is negated or
  technical at the tip; one summary of the agreement now says "products that
  implement it". References to prompt rules, to the person running the
  work, and to session, tool or sub-task process were rewritten in
  RESEARCH.md, PROGRESS.md and `research/rc-ambiguities.md` ("reading
  switch" for the rule-numbered name, "M2" for "session"). PROGRESS.md cited
  the Phase 5 part 1 commit by its pre-rewrite hash; it now cites 3e41c9e.
* Ignored files. Nothing that should be ignored was ever tracked.
  `.gitignore` now also covers model output (`*.dsc`, `*.dpx`, `*.out.ppm`,
  `*.ref.ppm`), other archive formats, coverage output, and editor and patch
  leftovers.
* Gate: `scripts/ci.sh` green with the model unset and set.
* Correction (follow-up commit): the P1 baseline table first gave the
  stand-in harness checks as 10; `tests/test_compare_model.py` has 9. The
  count is 9 before and after this pass (the discriminators check gained
  cases, not a new check line).

## Corpus comparison (2026-09-23)

Corpus: the 17 images in `Full_1080p_ref_images.zip` from the VESA DSC
test results, all 1920×1080 RGB. They were converted from BMP to PPM with
Pillow 12.1.1. The conversion is lossless. The images stay outside the
repository, in `~/vesa-corpus/`.

Run by `tools/run_corpus` with the Phase 5 configuration, block prediction
off and on, and 1, 2 and 4 slices per line. Model: `dsc-ref`, version
1.67. Results: `~/dsc-runs/corpus/20260923-201016` (8 bpp) and
`~/dsc-runs/corpus/20260923-202309` (the other rates).

| bpp | Runs | Bit-exact matches | Differing samples |
|---|---|---|---|
| 6 | 102 | 102 | 0 |
| 7.5 | 102 | 102 | 0 |
| 8 | 102 | 102 | 0 |
| 10 | 102 | 102 | 0 |
| 12 | 102 | 102 | 0 |
| 15 | 102 | 102 | 0 |
| Total | 612 | 612 | 0 |

Images: t_1024x1024Cr, t_1280x768_Noise_128, t_Barbara, t_Boats,
t_CircularPatterns26, t_Desktop10, t_FineTextRendering14, t_HSweepSplit,
t_Mandrill, t_Noise_gradient, t_TextOnTreeInField_crop, t_Tools,
t_hintergrund-musik, t_s1_peacock, t_s2_waterdrop_icons, t_s3_forestnymph,
t_sc_map.

## GitHub Actions (2026-09-23)

The first GitHub Actions run, on ubuntu-24.04, passed. The model step
reported SKIP, and the crash-upload step did not run. This supersedes the
Phase 1 note that the workflow has not run.

## Readability refactor verification (2026-09-24)

Checked: 62df40a, 70671c7 and b47e367 (branch
`claude/upbeat-maxwell-6b8daj`) on top of 732041a (v0.1.0). They change 14
files, all C: 11 `.c` and 3 `.h`. No Python, Makefile, `scripts/`,
`.github/` or Markdown file changed. Base and head were compared in two
worktrees. Toolchain on this VM: gcc 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1,
also `cc`), clang 18.1.3, Python 3.12.3. The v0.1.0 corpus run used gcc
15.2.0 and clang 21.1.8, so the matching corpus result below also shows that
it holds on a second toolchain.

* Object files. An `assert.h` shim defined `assert(e)` as `((e) ? (void)0 :
  __builtin_trap())`, and its `-I` came first on every command line, so the
  line numbers in `assert` do not reach the objects. `__LINE__` and `__FILE__` occur
  nowhere else in `src/`, `fuzz/` or `tests/`. Every `.c` file in those three
  directories (11) was compiled from the worktree root with relative paths
  and `-c -std=c11 -Iinclude -Isrc`, no `-g`, by gcc and clang at `-O0` and
  `-O2` (44 pairs). The 6 `src/` files were also compiled with the Makefile
  flags without `-g`: `cc -Iinclude -O2 -std=c11 -Wall -Wextra -Wpedantic
  -Wshadow -Wconversion` (6 pairs). All 50 pairs are byte-identical (`cmp`).
  Neither side printed a warning.
* Build outputs. After `make clean`, `make CFLAGS=-O2` gives byte-identical
  `dscdecode` and `libdsc.a`, build ID included.
* The `-g` build. A plain `make` (`-O2 -g`) gives different files. In
  `dscdecode` the differing sections are `.debug_aranges`, `.debug_info`,
  `.debug_abbrev`, `.debug_line`, `.debug_line_str`, `.debug_loclists` and
  `.note.gnu.build-id`. Every other section is identical, and so is
  `objdump -d`. The first differing byte is the section header offset in
  the ELF header, which moves because the debug sections change size. With
  `objcopy --strip-debug --remove-section=.note.gnu.build-id` the two files
  are identical. `ar` writes zero timestamps and owners, so `libdsc.a`
  differs only through its members. All five differ, only in `.debug_*` and
  `.rela.debug_*` sections, and are identical after `--strip-debug`. This
  is expected for a reformat built with `-g`. Debug information records
  source line numbers, which a reformat moves: the code at 0x3340 maps to
  `decode.c` lines 18 and 24 at base and 29 and 40 at head. It also records
  the build directory (`DW_AT_comp_dir`), which differs between the two
  worktrees. The build ID is a hash over the linked file, debug sections
  included.
* Tokens. Every `.c` and `.h` file (16) was lexed at both commits with
  `clang -Xclang -dump-raw-tokens`, with locations and flags removed. The
  tokens, whitespace included, rebuild each file exactly. The code tokens
  differ only by inserted `{ }` pairs: 203 in total, the bodies of 149
  `if`, 47 `for`, 5 `else` and 2 `while` statements. Each inserted `{` is
  matched by an inserted `}`, and removing the pairs from head gives the
  base tokens exactly. The 86 preprocessor directives are unchanged.
* Comments. The raw lexer emits comments. All 272 have the same text in the
  same order. 4 differ only in whitespace.
* Python. `ast.dump(ast.parse(source))` is equal at both commits for all 11
  Python files (`tests/*.py`, `tools/compare_model`, `tools/run_corpus`).
* Other files. The diff of `Makefile`, `scripts/` and `.github/` is empty.
  No Markdown file changed, so the README Status, Patents and summary text
  is unchanged.
* `DSCDECODE_MODEL_BIN=/usr/local/bin/dsc-ref scripts/ci.sh` at b47e367:
  every step passed (build, test, fixtures, sanitize, fuzz, model). The
  libFuzzer step ran 779,091 inputs in 61 s. The model step gave the v0.1.0
  verdicts: self-test match; `oq1` in-flight, `oq2b` lower, `oq3` chunk,
  `oq4` midpoint, `oq2` superseded. The model step, like the corpus run
  below, used the sanitizer build of `dscdecode`.
* `make fuzz-smoke` at b47e367 passed: 680,000 deterministic mutated-input
  executions.

Corpus at b47e367: the same 17 images in `~/vesa-corpus/`, run by
`DSCDECODE_MODEL_BIN=/usr/local/bin/dsc-ref tools/run_corpus --bpp 6 7.5 8
10 12 15 --min-images 17`, with block prediction off and on, and 1, 2 and 4
slices per line. Model: `dsc-ref`, version 1.67. Results:
`~/dsc-runs/corpus/20260924-013008`.

| bpp | Runs | Bit-exact matches | Differing samples |
|---|---|---|---|
| 6 | 102 | 102 | 0 |
| 7.5 | 102 | 102 | 0 |
| 8 | 102 | 102 | 0 |
| 10 | 102 | 102 | 0 |
| 12 | 102 | 102 | 0 |
| 15 | 102 | 102 | 0 |
| Total | 612 | 612 | 0 |

This VM reproduces the v0.1.0 corpus result.

The corpus run used the ASan/UBSan build of `dscdecode` left by the `ci.sh`
sanitize step (`-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
-fno-sanitize-recover=undefined`). The model step's `make all` found it up to
date, because make does not track CFLAGS, and `tools/compare_model` runs the
repository's `./dscdecode`. No `*SAN_OPTIONS` variable was set.

* Sanitizer reports in the run's logs: 0. None of the 44,678 text files in
  the results directory and nothing in the console output contains one. The
  logs keep `dscdecode`'s stderr only for a decode that exits non-zero, and
  none did: all 612 results are matches, none with an error. In this build
  every sanitizer finding exits non-zero: ASan aborts, UBSan is not
  recoverable, and a leak report at exit sets status 1.
* A sanitizer abort counts as a failure, not a match. `tools/compare_model`
  records any non-zero exit of `dscdecode` as an error with `match` false,
  without comparing the output image. `tools/run_corpus` prints such a run
  as "dscdecode error" and exits 1. Checked by running `tools/run_corpus`
  on one image with `DSCDECODE_BIN` set to a stand-in built with the same
  flags. An ASan heap-buffer-overflow, a UBSan signed overflow and a leak
  were each reported as "dscdecode error", with `match` false and exit
  status 1.

No corpus run used the -O2 build. Its objects are byte-identical to the base
build, and UBSan found no undefined behavior in the 612 sanitizer-build
decodes, so an -O2 difference would require undefined behavior that UBSan
does not detect.

# M3 progress log (branch m3-dsc12)

M3 extends the decoder toward DSC 1.2. Every M3 result is recorded here. The
README Status section and its numbers still describe v0.1.0 and are not
changed by M3. Model runs, pictures and scratch files are under
`~/dsc-runs/m3/`. For DSC 1.2 behavior the primary reference is DSC 1.2b;
DSC 1.2a with its E1 errata is secondary.

## Phase 0: baseline (2026-09-24)

Measured on m3-dsc12, created from main at 6382dae, before any M3 change. The
tree was clean.

Toolchain on this VM:

| Tool | Version |
|---|---|
| GCC (`cc`) | 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) |
| Clang (libFuzzer) | 18.1.3 (1ubuntu1) |
| llvm-cov, llvm-profdata | 18 |
| Python | 3.12.3, Pillow 10.2.0, no NumPy |
| GNU Make | 4.3 |
| AFL++ (`afl-clang-fast`) | not installed |
| Host | Linux 7.0.0-34-generic, x86-64, 8 CPUs |
| Model | `/usr/local/bin/dsc-ref`, version 1.67 |

Tests at baseline (both CI runs, identical counts):

| Suite | Result |
|---|---|
| Image fixtures, bit-exact (8 M1 plus 2 BP) | 10 / 10 |
| CLI checks (`tests/test_cli.py`) | 26 / 26 |
| Discriminator decodes (`tests/test_discriminators.py`) | 72 / 72 |
| `test_rc` (hand-calculated RC cases) | 29 cases, pass |
| `test_predict` (prediction cases) | 7 cases, pass |
| Harness checks with the stand-in model (`tests/test_compare_model.py`) | 9 / 9 |
| ASan + UBSan suites (`make sanitize`) | pass |
| Fixture reproducibility | pass |

Lines of code at baseline (`wc -l`, all lines):

| Area | Files | Lines |
|---|---|---|
| Library and CLI | `src/*.c`, `src/*.h`, `include/dsc.h` | 2,029 |
| Fuzz entry points | `fuzz/*.c` | 145 |
| Tests | `tests/*.c`, `tests/*.py` | 2,341 |
| Tools and scripts | `tools/compare_model`, `tools/run_corpus`, `scripts/ci.sh` | 778 |
| Total | | 5,293 |

The line counts are higher than the M2 figures because of the readability
refactor (62df40a to b47e367), which split dense lines.

CI:

* `scripts/ci.sh` with `DSCDECODE_MODEL_BIN` unset: every step passed (build,
  test, fixtures, sanitize, fuzz, model SKIP). libFuzzer: 743,097 executions in
  61 s; deterministic smoke: 680,000 executions.
* `DSCDECODE_MODEL_BIN=/usr/local/bin/dsc-ref scripts/ci.sh`: every step
  passed. libFuzzer 746,707 executions in 61 s; deterministic smoke 680,000.
  Model step: self-test match; `oq1` in-flight, `oq2b` lower, `oq3` chunk,
  `oq4` midpoint, `oq2` superseded. As at v0.1.0, the model step's
  `make all` found the sanitizer build left by the sanitize step and used it
  (`make` did not track flags). Phase 1 removes this.

Corpus at 8 bpp: the 17 images, block prediction off and on, 1, 2 and 4
slices per line, DSC 1.1 (`tools/run_corpus --bpp 8 --min-images 17`). To
make sure the release build was measured, `dscdecode` was built with a plain
`make` (`-O2 -g`) from `git archive 6382dae` under
`~/dsc-runs/m3/phase0/base-src/` and passed with `DSCDECODE_BIN`. Results:
`~/dsc-runs/corpus/20260924-023617`, 5 min 25 s.

| bpp | Runs | Bit-exact matches | Differing samples |
|---|---|---|---|
| 8 | 102 | 102 | 0 |

The 34-run 1.1 regression check used at the end of each M3 phase
(`--bpp 8 --bp 0 1 --slices 2`) is a subset of these runs: 34 of 34 match.

## Phase 1: separate build directories (2026-09-24)

* `Makefile`: each flavor builds in its own directory, `build/release`
  (`make`, `make test`; `CFLAGS`, default `-O2 -g`), `build/sanitize`
  (`make sanitize`), `build/fuzz` (`make fuzz`), `build/fuzz-smoke`
  (`make fuzz-smoke`) and `build/afl` (`make afl`). A stamp,
  `build/<flavor>/.flags`, holds the compiler's version line, the compiler and
  the flags. A forced rule rewrites it only when that text changes, and every
  object and program of the flavor depends on it, so a change of compiler or
  flags rebuilds the flavor and nothing else. Checked: `make`, then `make`
  (nothing rebuilt), `make CFLAGS=-O2` (all 8 compile and link steps rerun),
  `make CFLAGS=-O2` (nothing), `make` (all 8 again). `make clean` removes
  `build/` and the top-level outputs of the old layout. `make sanitize` no
  longer starts with `make clean`, because it no longer shares a directory
  with the release build.
* `tools/compare_model` and `tools/run_corpus` use
  `build/release/dscdecode`. `--build sanitize` (or `DSCDECODE_BUILD`)
  selects `build/sanitize/dscdecode`; `DSCDECODE_BIN` still names any other
  binary. Each prints `dscdecode: PATH` before its first decode (to stderr
  under `--json`), fails if the file does not exist, and records the path in
  `result.json` / `results.json`. Without `DSCDECODE_MODEL_BIN` they print
  SKIP first, as before.
* `scripts/ci.sh`: the build step builds `build/release/test_rc` and
  `build/release/test_predict`; the fuzz step runs
  `build/fuzz/fuzz_decode`; the model step compares `build/release/dscdecode`
  unless `CI_MODEL_BUILD=sanitize`, and names the build in its PASS line. The
  test scripts default to `build/release/dscdecode`. This removes the v0.1.0
  situation where the model step and corpus runs used whichever build was
  last made.
* README "Build and use" and "Verification and fuzzing" give the new paths
  and describe the flavors and `--build`. No other README section changed.
* `.gitignore` already listed `/build/`.
* `make afl` could not be run: AFL++ is not installed. The rule was checked
  with `make afl AFLCC=clang` (builds `build/afl/fuzz_afl`); that build was
  deleted.

No change to the program. At 069c9d7 (old Makefile) and in the working tree
(new Makefile), in two separate copies, `make CFLAGS=-O2` built the release
objects without `-g`, plus `test_rc` and `test_predict`. The compile commands
are identical except for the output paths. Compared with `cmp`:

| Output | Result |
|---|---|
| `src/{pps,decode,predict,rate_control,options,main}.o` | 6 of 6 byte-identical |
| `libdsc.a` | byte-identical (SHA-256 71e27d1d…686d44) |
| `dscdecode` | byte-identical (SHA-256 4f61aa2e…6ed3a) |
| `test_rc`, `test_predict` | byte-identical |

Scratch copies: `~/dsc-runs/m3/phase1/{before,after}`.

Gate:

* `scripts/ci.sh` with `DSCDECODE_MODEL_BIN` unset: all steps pass, model
  SKIP. libFuzzer 763,373 executions in 61 s; deterministic smoke 680,000.
* With `DSCDECODE_MODEL_BIN=/usr/local/bin/dsc-ref`: all steps pass. The
  model step printed `dscdecode: .../build/release/dscdecode`; self-test
  match; `oq1` in-flight, `oq2b` lower, `oq3` chunk, `oq4` midpoint, `oq2`
  superseded. libFuzzer 725,366 executions in 61 s; deterministic smoke 680,000.
  `tools/compare_model --build sanitize --self-test` printed
  `build/sanitize/dscdecode` and matched.
* 1.1 regression (`tools/run_corpus --bpp 8 --bp 0 1 --slices 2
  --min-images 17`, release build): 34 of 34 bit-exact
  (`~/dsc-runs/corpus/20260924-024859`).

## Phase 2: tools/verify_refactor (2026-09-24)

`tools/verify_refactor BASE HEAD` replaces the shim and scripts used for the
readability-refactor check (`~/verify/`: `shim/assert.h`, `compile.sh`,
`tokens.py`, `pyast.py`). It exports both commits with `git archive` into a
scratch directory (`$TMPDIR`) and runs:

* files: C and Python files present at only one commit are differences;
  other changed files are listed as not verified.
* objects: every tracked `.c` under `src/`, `fuzz/` and `tests/`, compiled
  from the tree root with relative paths by gcc and clang at `-O0` and `-O2`,
  `-c -std=c11 -Iinclude -Isrc`, no `-g`, with the `assert.h` shim first on
  the include path; plus the `src/` files with the Makefile release flags
  without `-g`. Every pair must be byte-identical. Occurrences of
  `__LINE__`, `__FILE__`, `__DATE__` or `__TIME__` are listed, since they
  can reach objects.
* build: `make CFLAGS=-O2 all` at both commits; `dscdecode` and `libdsc.a`
  (in `build/release/`, or the tree root for the older layout) must be
  byte-identical.
* tokens: clang raw tokens of every tracked `.c` and `.h`; the tokens must
  rebuild each file; code tokens may differ only by inserted `{ }` pairs,
  which are counted per file and by construct; preprocessor directives must
  be identical.
* comments: same text in the same order after removing whitespace; comments
  that differ only in whitespace are counted.
* python: `ast.dump(ast.parse(...))` of every tracked `*.py` file and every
  file with a python shebang.

It prints the first difference of each step and the first one overall, and
exits 1 on any difference, 0 on none, 2 on a setup error. README
"Verification and fuzzing" documents it.

Tests:

| Range | Result |
|---|---|
| 732041a..6382dae (the readability refactor) | PASS, exit 0. 50 of 50 object pairs identical; `dscdecode` and `libdsc.a` identical; 16 C files, 19,215 code tokens, 203 inserted brace pairs (if 149, for 47, else 5, while 2); 272 comments, 4 differing only in whitespace; 86 directives; 11 Python files equal. Same counts as the manual check recorded above. 3 s. |
| 6382dae..5fd54b3: scratch commit changing `-172` to `-173` in `src/rate_control.c` | FAIL, exit 1. objects (5 pairs of `rate_control.c`), build (`dscdecode`, `libdsc.a`) and tokens (`172` replaced by `173`) report it; first difference: `gcc-O0 src/rate_control.c: objects differ`. |
| 6382dae..181bee8: scratch commit changing one comment word in `src/decode.c` and the `--min-images` default in `tools/run_corpus` | FAIL, exit 1. objects, build and tokens same; comments and python report the change. |

Both scratch commits were made on a detached HEAD in a scratch worktree
(`~/dsc-runs/m3/phase2/scratch-wt`, since removed); no branch contains them.
Logs: `~/dsc-runs/m3/phase1/verify-pass.log`,
`~/dsc-runs/m3/phase2/verify-fail{,2}.log`.

Gate:

* `scripts/ci.sh` with the model unset: all steps pass, model SKIP.
  libFuzzer 800,862 executions in 61 s; deterministic smoke 680,000.
* With `DSCDECODE_MODEL_BIN=/usr/local/bin/dsc-ref`: all steps pass (release
  build; self-test match; `oq1` in-flight, `oq2b` lower, `oq3` chunk, `oq4`
  midpoint, `oq2` superseded). libFuzzer 785,152 executions in 61 s;
  deterministic smoke 680,000.
* 1.1 regression: 34 of 34 bit-exact (`~/dsc-runs/corpus/20260924-025529`).

## Phase 3, part 1: 10 and 12 bpc decoding, fixtures, OQ-19 discriminator (2026-09-24)

Phase 3 is not complete at this commit. This part is committed so that the
OQ-19 discriminator and its predictions are in the history before the
reference model decodes it (rule for open questions, RESEARCH.md).

* Decoder: DSC 1.1 RGB 4:4:4 CBR at 10 and 12 bpc. A new `src/format.c`
  derives per-substream sample depths (§6.1), mux word size (§4.4),
  maximum syntax element sizes, the QP scale and the flatness QPs
  (Table 6-2, §4.5, §6.8.5.2) from the PPS; the entropy decoder, the
  predictor (MPP midpoint, clamps, BP shift and edge threshold, line
  storage) and the rate control use them. The 8 bpc path is unchanged
  (all v0.1.0 suites pass).
* API: `dsc_decode_frame_planes`, `dsc_decode_slice_planes` and
  `dsc_plane_size` write 16-bit sample planes for every supported format;
  the RGB888 functions serve 8 bpc RGB only. The CLI writes PPM with maxval
  2^bpc − 1 (two bytes per sample above 8 bpc).
* Fixtures: six hand-derived fixtures at 10 and 12 bpc
  (`tests/make_hbd_vectors.py`, derivations in
  `research/hbd-worked-note.md`). CLI checks: 26 → 32.
* Pictures and harness: `tools/make_pictures` writes synthetic and derived
  10/12-bit pictures to `~/dsc-runs/hbd-pictures/`; `tools/compare_model`
  and `tools/run_corpus` take `--bpc`, `--dsc-version`, `--jobs`, check the
  model's `.ref.ppm` against a master PPM, and delete the images of matching
  runs unless `--keep`.
* New open question OQ-19 (initial-delay offset at a partial group), switch
  `delay_partial`, default `group-end`; unit test `delay_partial` (4
  cases); discriminator `oq19_delay_partial` with its predictions in
  `tests/discriminators/README.md` (discriminator decodes: 72 → 104). How
  it was found is recorded with the Phase 3 results.
* Gate for this part: `scripts/ci.sh` with the model unset: all steps pass.
