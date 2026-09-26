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
* Wording. Every status word such as "conformant" or "validated" is
  negated or technical at the tip; one summary of the agreement now says
  "products that implement it". Wording about how the work was organized,
  rather than about the work, was rewritten in RESEARCH.md, PROGRESS.md and
  `research/rc-ambiguities.md` ("reading switch" for a numbered internal
  name, "M2" for a reference to the working period). PROGRESS.md cited
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

Checked: 62df40a, 70671c7 and b47e367 (made on a side branch) on top of
732041a (v0.1.0). They change 14
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
reference model decodes it, so that the predictions are fixed before the
model's output is seen.

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

## Phase 3, part 2: model comparison at 10 and 12 bpc, OQ-19 resolved (2026-09-24)

Phase 3 is complete with this part. The decoder code is the code committed in
part 1 (4f164ba): the release binary used for every run below
(`~/dsc-runs/m3/phase3/bin/dscdecode`, SHA-256 d736d40b…4033) is
byte-identical to `build/release/dscdecode` built from 4f164ba. This part
adds only records.

### How the model reads and writes more than 8 bits

Learned from `/usr/local/share/dsc-ref/README.TXT` and from the files the
model writes; the scratch files are in `~/dsc-runs/m3/phase3/discovery/`.

* The model reads PPM and DPX. For the comparison the input is DPX, in the
  layout the model writes itself when it converts an input: DPX 2.0, one RGB
  image element, packing method A, big-endian. 10-bit: one pixel per 32-bit
  word, B in bits 31–22, G in 21–12, R in 11–2. 12-bit: one 16-bit word per
  sample, value in the top 12 bits, in the order B, G, R, with no padding
  at line ends and the image data padded to a whole number of 32-bit words.
  With `SWAP_R_AND_B 0`, `DPXR_DATUM_ORDER 1` and `DPXR_PAD_ENDS 1` the model
  reads this layout back unchanged at every size tried, including the odd
  width 637.
* Layouts that did not work: a 10-bit V1.0 header was read with another
  packing; 12-bit samples in R, G, B order came back with R and B swapped;
  per-line padding of odd-width 12-bit rows shifted every row after the
  first; 16-bit DPX samples were rescaled on input.
* Every run checks what the model read: the model's `input.ref.ppm` must equal
  the master PPM that `tools/make_pictures` writes beside each DPX. The first
  12-bit matrix found the odd-width misread this way (30 runs refused before
  any comparison), which led to the end-of-image padding above.
* The model's decoded output is compared as a PPM with maxval 2^bpc − 1.

### Pictures

`tools/make_pictures`, output in `~/dsc-runs/hbd-pictures/` (outside the
repository):

* synthetic-10, synthetic-12: eight pictures per bit depth, 640×216 except
  s08 (637×125): s01 gradient, s02 full-range noise, s03 fine text strokes,
  s04 flat blocks whose neighbours differ by one LSB, s05 sine waves, s06
  low-bit texture around mid-grey, s07 a mix of these, s08 the mix at an odd
  size. The 640-pixel width gives a one-pixel partial group at the end of
  each line with 4 slices (160 pixels per slice); 637 gives partial groups
  with 1, 2 and 4 slices.
* derived-10, derived-12: the 17 corpus images, each sample scaled as
  round(v × (2^bpc − 1) / 255) plus noise uniform in
  [−(2^(bpc−8) − 1), 2^(bpc−8) − 1] (±3 at 10 bpc, ±15 at 12 bpc), clamped;
  the noise generator is seeded from SHA-256 of image name, bit depth and
  component, so the pictures are reproducible.

### Model comparison, DSC 1.1 RGB 4:4:4 CBR

Every run: the model encodes the picture with the `rc_<bpc>bpc_<bpp>bpp.cfg`
of the rate (these are all the 4:4:4 rates installed: 6, 8, 10, 12, 15 bpp),
DSC 1.1, line buffer bpc + 1, block prediction off and on, 1, 2 and 4
slices per line; the model decodes its stream, and dscdecode (release build,
default readings) decodes the same PPS and payload. "Match" is bit-exact
equality of all samples. Command: `tools/run_corpus --dir <pictures> --bpc
<bpc> --bpp 6 8 10 12 15 --bp 0 1 --slices 1 2 4 --jobs 8`
(`~/dsc-runs/m3/phase3/matrix.sh`).

10 bpc:

| Pictures | bpp | Runs | Bit-exact | Differing samples | Model input check |
|---|---|---|---|---|---|
| synthetic (8) | 6 | 48 | 48 | 0 | 48 / 48 |
| synthetic (8) | 8 | 48 | 48 | 0 | 48 / 48 |
| synthetic (8) | 10 | 48 | 48 | 0 | 48 / 48 |
| synthetic (8) | 12 | 48 | 48 | 0 | 48 / 48 |
| synthetic (8) | 15 | 48 | 48 | 0 | 48 / 48 |
| derived (17) | 6 | 102 | 102 | 0 | 102 / 102 |
| derived (17) | 8 | 102 | 102 | 0 | 102 / 102 |
| derived (17) | 10 | 102 | 102 | 0 | 102 / 102 |
| derived (17) | 12 | 102 | 102 | 0 | 102 / 102 |
| derived (17) | 15 | 102 | 102 | 0 | 102 / 102 |
| total | | 750 | 750 | 0 | |

12 bpc:

| Pictures | bpp | Runs | Bit-exact | Differing samples | Model input check |
|---|---|---|---|---|---|
| synthetic (8) | 6 | 48 | 48 | 0 | 48 / 48 |
| synthetic (8) | 8 | 48 | 48 | 0 | 48 / 48 |
| synthetic (8) | 10 | 48 | 48 | 0 | 48 / 48 |
| synthetic (8) | 12 | 48 | 48 | 0 | 48 / 48 |
| synthetic (8) | 15 | 48 | 48 | 0 | 48 / 48 |
| derived (17) | 6 | 102 | 102 | 0 | 102 / 102 |
| derived (17) | 8 | 102 | 102 | 0 | 102 / 102 |
| derived (17) | 10 | 102 | 102 | 0 | 102 / 102 |
| derived (17) | 12 | 102 | 102 | 0 | 102 / 102 |
| derived (17) | 15 | 102 | 102 | 0 | 102 / 102 |
| total | | 750 | 750 | 0 | |

Results: `~/dsc-runs/m3/phase3/runs/corpus/20260924-074757` (synthetic-10),
`20260924-074804` (synthetic-12), `20260924-074813` (derived-10),
`20260924-075231` (derived-12). Each `results.json` records the dscdecode
path.

The six hand-derived fixtures of part 1 (`hbd10_*`, `hbd12_*`): the model's
decode of each fixture PPS and payload is identical to the committed
`.expected.ppm`.

### OQ-19: found, discriminated, resolved

The first run of the matrix (before `delay_partial` existed, so with the
pixels reading) failed 8 of 240 synthetic-10 runs and 4 of 240 synthetic-12
runs, all with 4 slices, all with dscdecode rejecting the stream as invalid
(the decode had gone wrong earlier in the slice); the derived pictures (480-pixel slices, no partial groups) did not fail. In each
failing slice the first difference was a QP two groups after a one-pixel
partial group at the end of a line inside the initial transmission delay.
Following the M2 method, a scratch build (outside the repository) forced
single-group QPs and added offsets to rcXformOffset to find what the model
used. A change of −24 at the partial group (bits_per_pixel = 8, so three
pixels' worth instead of one) and +24 at the first group of the next line
reproduced the model's pixels through the end of the delay. Hypotheses that
did not fit: counting three pixels for every group (fails where the delay
ends), and changes to the range thresholds. The rule that
fits every failing case is OQ-19 reading B (group-end), RESEARCH.md.

The discriminator `oq19_delay_partial` and both predictions were committed in
4f164ba before the model saw it. The model then decoded it
(`~/dsc-runs/m3/phase3/oq19-model.log`, run
`~/dsc-runs/m3/phase3/runs/compare/20260924-074743-094127-oq19_delay_partial`):
its output matches the group-end prediction bit-exact; the pixels
prediction differs in 3 samples of 1 pixel (x = 6, y = 1). The default
`delay_partial=group-end` stays, and every run of the matrix above used it.

The case is not specific to high bit depths. At 8 bpc, three synthetic-10
pictures reduced to 8 bits (s01, s02 and s07, each sample shifted right by
2; `~/dsc-runs/m3/debug/pics8/`) were run at 6, 8, 10, 12 and 15 bpp, block
prediction off and on, 1, 2 and 4 slices: 90 of 90 bit-exact with this
branch (`~/dsc-runs/m3/phase3/runs/corpus/20260924-081509`). The v0.1.0
decoder (6382dae, pixels reading) on the 30 four-slice runs of that set:
27 bit-exact, 3 rejected (`20260924-081925`). v0.1.0's comparisons had used
slice widths without one-pixel partial groups inside the initial delay. M3
leaves the README Status section at v0.1.0; this note is the record.

### Harness changes made during the matrix

* `tools/run_corpus --jobs N` runs comparisons in parallel. A matching run
  deletes its pictures, streams and model outputs unless `--keep`, and
  `tools/compare_model` copies only the rate-control file it uses into the
  run directory. The first matrix filled the disk during the derived-10
  set; the matrix was restarted from the start after these changes.
* `tools/compare_model` checks the model's input against the master PPM
  (above) and reads PNM with any maxval.

### Tests and gate

| Suite | Phase 0 | Now |
|---|---|---|
| Image fixtures, bit-exact | 10 | 16 (adds the six `hbd*` fixtures) |
| CLI checks | 26 | 32 |
| Discriminator decodes | 72 | 104 |
| `test_rc` cases | 29 | 33 (`delay_partial`, 4 cases) |
| `test_predict` cases | 7 | 7 |

Gate:

* `scripts/ci.sh` with `DSCDECODE_MODEL_BIN` unset: all steps pass, model
  SKIP. libFuzzer 489,022 executions in 61 s (fewer than in Phase 2 because
  each input now also runs the planes API); deterministic smoke 960,000.
* With `DSCDECODE_MODEL_BIN=/usr/local/bin/dsc-ref`: all steps pass (release
  build; self-test match; `oq1` in-flight, `oq2b` lower, `oq3` chunk, `oq4`
  midpoint, `oq19` group-end, `oq2` superseded). libFuzzer 417,024 executions
  in 61 s; deterministic smoke 960,000.
* 1.1 regression: 34 of 34 bit-exact (`~/dsc-runs/corpus/20260924-082559`).

Logs: `~/dsc-runs/m3/phase3/gate-*.log`.

## Phase 4, part 1: DSC 1.2 RGB 4:4:4 decoding, OQ-7 and OQ-20 to OQ-25 discriminators (2026-09-24)

Phase 4 is not complete at this commit. This part is committed so that the
seven DSC 1.2 discriminators and their predictions are in the history before
the reference model decodes them. Section numbers are DSC 1.2b unless marked.

* Decoder: accepts dsc_version_minor 2 for RGB 4:4:4 CBR at 8, 10, 12, 14
  and 16 bpc (14 and 16 only with DSC 1.2, Table 4-1). 16 bpc: chroma is
  16 bits (§6.1), the inverse colour transform scales chroma back to 17 bits
  (§7.7), the luma prefix at QP 0 is limited (Table 4-10, OQ-21), and mux
  words are 64 bits (§4.4). The QP-to-qLevel arithmetic covers Table 6-3
  (every column checked); chroma qLevel drops by one where luma and chroma
  have the same depth (§6.8.6, OQ-20).
* Rate control, DSC 1.2 only (§6.8.4, Figures 6-17 and 6-18): the buffer
  overflow and underflow branches with maxQp and minQp from the range and
  the top QP 2·bpc − 1, the bit-saving modes (mppState, bitSaveMode,
  predActivity against bitSaveThresh), the zero-residual branch, the
  second-line target terms (second_line_bpg_offset, nsl_bpg_offset,
  second_line_offset_adj) combined with the first-line terms per OQ-7, and
  the very-flat adjustment of the first group of each non-first line
  (§6.8.5.2, OQ-25). DSC 1.1 streams take the DSC 1.1 paths unchanged.
* Seven new reading switches (`include/dsc.h`, RESEARCH.md):
  `bpg_combine` (OQ-7), `chroma_qlevel` (OQ-20), `prefix16` (OQ-21),
  `bitsave_ich` (OQ-22), `bitsave_pred` (OQ-23, three readings),
  `bitsave_flat` (OQ-24), `line_flat` (OQ-25). `dscdecode --stats` counts
  bit-saving steps and line-start adjustments.
* Discriminators: one per question, built by `tests/make_v12_discriminators.py`
  with a separate Python decoder model (`tests/pydsc.py`); predictions in
  `tests/discriminators/README.md` and `manifest.json`. Discriminator
  decodes: 104 → 2,728 (every combination of the switches each input
  varies). Unit cases for the DSC 1.2 short-term RC and for OQ-7 in
  `tests/test_rc.c`.
* The fuzz target sets the new switches from input bits.
* Gate for this part: `scripts/ci.sh` with the model unset: all steps pass.

## Phase 4, part 2: the model decides six DSC 1.2 questions; OQ-24 gets two more inputs (2026-09-24)

Phase 4 is not complete at this commit.

### The first seven DSC 1.2 discriminators against the model

After 60ae6cc, `DSCDECODE_MODEL_BIN=/usr/local/bin/dsc-ref tools/compare_model
discriminators` (`~/dsc-runs/m3/phase4/disc-model.log`, runs under
`~/dsc-runs/m3/phase4/runs/compare/20260924-083242-*`). The M2 and Phase 3
inputs gave their earlier verdicts again. The new inputs:

| Input | Question | Model output matches | Other predictions | Default before → after |
|---|---|---|---|---|
| `oq7_bpg_combine` | OQ-7 | add | replace: 9 samples in 3 pixels differ | add → add |
| `oq20_chroma_qlevel` | OQ-20 | equal-depth | table: 6 samples in 3 pixels | equal-depth → equal-depth |
| `oq21_prefix16` | OQ-21 | 13 | 15: 9 samples in 3 pixels | 15 → 13 |
| `oq22_bitsave_ich` | OQ-22 | not | set: 6 samples in 2 pixels | not → not |
| `oq23_bitsave_pred_next` | OQ-23 | next | raw and adjusted (one shared prediction): 6 samples in 2 pixels | raw → next |
| `oq24_bitsave_flat` | OQ-24 | neither | supergroup and group: 12 samples in 4 pixels each | see below |
| `oq25_line_flat` | OQ-25 | signaled | very: 1 sample | very → signaled |

Three defaults changed: `prefix16=13` (the model follows DSC 1.2b §3.10.2
and DSC 1.2a Table 4-10, not DSC 1.2b Table 4-10), `bitsave_pred=next`,
`line_flat=signaled`. OQ-23: raw and adjusted were never separated from
each other: after MPP units the two sums differ by exactly two, and in the
one input tried for it the adjusted sum came out exactly at bitSaveThresh,
where both readings keep bitSaveMode, so that input was dropped. Both
predict the same output on `oq23_bitsave_pred_next`, and the model's output
differs from it, so both are excluded.

### OQ-24: both readings contradicted, two more readings, two more inputs

The model's output of `oq24_bitsave_flat` matches neither prediction: it
first differs at x = 14 of line 1, in group 10, which has zero residuals,
so its pixels are the MMAP prediction, whose clamp depends on QP. Both
predictions have group 10 at QP 9 (bitSaveMode 2 after the MPP groups 7 and
8); the model decoded it at another QP. No combination of the other DSC 1.2
switches reproduced the model's output. A scratch build outside the
repository (`~/dsc-runs/m3/phase4/scratch`) tried other definitions of the
flag the bitSaveMode update tests; three reproduced the model's output
bit-exact: the flag received last (groups 7 to 10 for the flag sent in
group 7), only the groups that carry the flag and the type and position
(groups 7 and 8), and either the flag received last or the supergroup's
flag. Groups 7 and 8 carry the flatness bits in this input, which is why all
three fit.

As for every open question, `received` and `carrier` were added to the
`bitsave_flat` switch (RESEARCH.md, OQ-24), the default became `received`
(judged more likely), and two inputs were built and committed with
predictions for all four readings before the model decoded them:
`oq24b_bitsave_flat` (received and carrier → QP 9, supergroup and group →
8) and `oq24c_bitsave_flat` (received and supergroup → 8, carrier and
group → 9). Each reading has a different pair of outcomes. The third
variant (received or supergroup) is not a switch value; the scratch build
gives it the supergroup prediction on `oq24b`, so `oq24b` excludes it if
the model matches received there. `oq24_bitsave_flat` is marked superseded:
it has predictions only for its two readings. Details in
`tests/discriminators/README.md`.

Tooling: each input's manifest entry lists the readings it has predictions
for, and `tests/test_discriminators.py` and `tools/compare_model` use that
list, so a reading added later does not need predictions for inputs the
model has already decoded. `tests/make_v12_discriminators.py` rebuilds the
first seven inputs under the defaults they were built with; they
regenerate byte-for-byte. Discriminator decodes: 2,728 → 6,312.

## Phase 4, part 3: OQ-24, third round (2026-09-24)

Phase 4 is not complete at this commit.

After 352b429 the model decoded `oq24b_bitsave_flat` and `oq24c_bitsave_flat`
(`~/dsc-runs/m3/phase4/disc-model2.log`, runs
`~/dsc-runs/m3/phase4/runs/compare/20260924-122116-*`). Every other input
gave its earlier verdict.

| Input | Model output matches | Does not match |
|---|---|---|
| `oq24b_bitsave_flat` | supergroup, group (one shared prediction) | received, carrier: 6 samples in 2 pixels |
| `oq24c_bitsave_flat` | supergroup, received (one shared prediction) | group, carrier: 9 samples in 3 pixels |

With `oq24_bitsave_flat`, where the model matched neither supergroup nor
group, no one of the four readings fits all three inputs. The variant
recorded in the discriminator README before these decodes (a group is
covered under received or supergroup) fits all three: its recorded
outcomes were the supergroup prediction on `oq24b` and the received
prediction on `oq24c`. It is the window from the group that carries the
flag to the last group of its supergroup (groups 7–12 for the flag sent in
group 7). The scratch build found three more windows that fit the three
outputs: the flag as it was before the group (8–11), 7–11 and 8–12; all the
three inputs show is that a flag of 1 covers at least one group of each MPP
pair tested (7–8, 11–12, and 5–6 for the flag sent in group 3).

So `span` (7–12) and `lagged` (8–11) were added to the `bitsave_flat`
switch, the default became `span`, and two inputs that test the two ends of
the window were built and committed with predictions for all six readings
and for the other two windows: `oq24d_bitsave_flat` (is the flag's own group
covered?) and `oq24e_bitsave_flat` (is the supergroup's last group
covered?). The four windows give four different pairs of outcomes
(`tests/discriminators/README.md`). `oq24b` and `oq24c` keep predictions for
the four readings they were built with. Discriminator decodes: 6,312 →
10,664.

OQ-24 result. After ae904ee the model decoded `oq24d_bitsave_flat` and
`oq24e_bitsave_flat` (`~/dsc-runs/m3/phase4/disc-model3.log`, runs
`~/dsc-runs/m3/phase4/runs/compare/20260924-123040-*`):

| Input | Model output matches | Does not match |
|---|---|---|
| `oq24d_bitsave_flat` | supergroup, group, lagged | received, carrier, span: 6 samples in 2 pixels |
| `oq24e_bitsave_flat` | group, received, carrier, lagged | supergroup, span: 9 samples in 3 pixels |

The flag's own group is not covered and the supergroup's last group is not
covered: of the windows that fitted the first three inputs, only lagged
(8–11) gives these outcomes, and it fits all five inputs. `bitsave_flat`
now defaults to `lagged`; RESEARCH.md marks OQ-24 resolved, noting that the
interior groups 9 and 10 are inferred, not tested on their own. Every other
discriminator gave its earlier verdict; `tools/compare_model
discriminators` exits 0.

## Phase 4, part 4: DSC 1.2 short-term RC and 16 bpc prefix, OQ-26 to OQ-36 (2026-09-24)

Phase 4 is not complete at this commit. The model has not decoded the eleven
inputs committed here.

### How the model's DSC 1.2 behavior was found

A first DSC 1.2 comparison at 8 bpc (s01, s02, s07 of the synthetic set
reduced to 8 bits; 6 to 15 bpp, BP off/on, 1, 2 and 4 slices; 90 runs,
`~/dsc-runs/m3/phase4/runs/corpus/20260924-123129`) matched in 14 runs. The
method was the one used for OQ-19, all outside the repository
(`~/dsc-runs/m3/phase4/dbg`, `dbg2`, `exp`):

* A debug build forces chosen QPs (optionally carrying them into the RC
  state), keeps decoding after errors, and logs the RC inputs of each step
  and each flatness or line-start re-run. Searches over forced QPs give the
  QPs the model used at the first divergence; the RC inputs at the steps
  that produced them show which rule differs.
* Controlled encodes: the model's configuration files accept explicit rate
  control parameters (README.TXT), so the same picture was encoded with
  every range pinned to one QP (the decodes then matched exactly: entropy
  coding, prediction, bit-saving at a pinned QP and flatness agree), with
  flatness signaling disabled (flatness QP window 15..15), with one range
  for all levels and zero bpg offsets, and with pinned ranges at higher QPs.
  Each removes some of the RC's paths from the comparison.
* Each candidate rule was switched on in the debug build and scored on all
  retained runs; a rule was kept only if it made runs match without
  breaking others. Near alternatives were scored too and rejected
  (lowMinQp 0 instead of MAX(minQp − 4, 0): 62 of 88; "+2 only when
  bitSaveMode 2 begins": 33 of 88; flatness restart, increment order and
  range-lag switches: no gain).

What the model does, against the DSC 1.2b text (details in RESEARCH.md):

| Question | Text | Model streams |
|---|---|---|
| OQ-26 lowMinQp | MAX(maxQp − 4, 0) | MAX(minQp − 4, 0) |
| OQ-27 decrement | codedGroupSize and rcSizeGroup below tgtMinusOffset | rcSizeGroup below it |
| OQ-28 predActivity | prevQp | prev2Qp |
| OQ-29 bitSaveMode 2 | prevQp + 1 | prevQp + 2 |
| OQ-30 negative target | used as computed | raised to 0 |
| OQ-31 flatness re-run | when the QP changes | at every adjusted flat group and line start |
| OQ-32 bitSaveMode in the re-run | kept | computed again |
| OQ-33 16 bpc luma refill threshold | 68 by the formula | 64 |
| OQ-35 16 bpc cut prefix | QP 0 | luma qLevel 0 (13 bits) and 1 (15 bits) |
| OQ-36 cut prefix rules | in every such group | only where the uncut prefix is longer |

Two corrections follow the text and are not questions: a DSC 1.2 line
start is adjusted only while the previous group's QP is below range 14's
maximum (the decoder had used the DSC 1.1 "equal" test, which misses QPs
that bitSaveMode raises above it), and the re-run after a flatness
adjustment uses prev2Qp flatness-adjusted (§6.8.4). Also fixed: the 16 bpc
luma prefix at QP 0 replaced the prefix instead of limiting it, which made
a large predicted size uncodable. OQ-34 (signaled flatness above range
14's maximum) came from reading the text, not from a stream; its default is
the text.

With all of these, the real decoder matches the model on every retained
run: 332 of 332 (the 76 failing 8 bpc runs above, 14 controlled encodes,
the 240 16 bpc runs of the synthetic-16 probe, and two single runs). Probes
of the synthetic 10-bit and 12-bit sets (240 runs each) and three corpus
images at 8 bpc (54 runs) matched with a debug wrapper that had most of the
rules; the full Phase 4 matrix will be run with the final decoder.

### 14 and 16 bpc input

* 16 bpc: 16-bit DPX in the layout of `tools/make_pictures` is read
  exactly. Line buffer 17 is refused by the model (8 to 16), so the harness
  default is now bpc + 1, at most 16.
* 14 bpc: the model reads no 14-bit DPX, and did not read a 16-bit DPX
  holding 14-bit values exactly (its source check failed), so 14-bit
  pictures are written as PPM with maxval 16383, which it reads exactly.
  The model writes 14-bit pictures as 16-bit PPM, each sample v as
  floor(v × 65535 / 16383); the harness maps them back and checks that each
  maps forward again.
* The model's encoder dies from a signal (SIGSEGV or SIGABRT) at every 14
  bpc encode tried, in each output mode, after its log shows the last slice
  processed and after it has written the bitstream and the reference
  picture; the files are complete (the bitstream is the same size on every
  repetition, and the model's own decode of it, a separate process, runs
  normally). The harness accepts such an encode only under those three
  conditions, records the signal in `result.json`
  (`model_encode_signal`) and prints a note. Cores went to the system
  handler; none was opened.
* `tools/run_corpus` accepts PPM at any maxval and makes run directory
  names unique within a second.

### Implementation and tests

* Eleven switches, `low_min`, `decrement_test`, `activity_qp`,
  `bitsave_step`, `target_floor`, `flat_rerun`, `rerun_bitsave`, `mux16`,
  `flat_top`, `prefix16_scope` and `prefix16_cut`; defaults are the model
  streams' readings (OQ-34: the text). `tests/pydsc.py` implements all of
  them; the inputs built before these switches existed are rebuilt under the
  text's readings and regenerate byte-for-byte.
* Discriminators `oq26_low_min` to `oq36_prefix16_cut`, one per question,
  predictions in
  `tests/discriminators/README.md`. Discriminator decodes: 10,664 → 11,816.
* `tests/test_rc.c`: the DSC 1.2 cases keep their printed-rule expectations
  under the text readings, and new cases check OQ-26, OQ-27, OQ-29 and
  OQ-30 under the model readings.
* `tests/test_compare_model.py`: the stand-in model decodes with
  `flat_restart=in-flight` (OQ-1's model reading), which the OQ-31 and
  OQ-32 inputs assume.

Results. After f589964 the model decoded every discriminator
(`~/dsc-runs/m3/phase4/disc-model4.log`, runs
`~/dsc-runs/m3/phase4/runs/compare/20260924-181844-*`). The earlier inputs
gave their earlier verdicts; the new ones:

| Input | Model output matches | Other prediction |
|---|---|---|
| `oq26_low_min` | min-qp | max-qp: 3 samples in 3 pixels differ |
| `oq27_decrement_test` | size | both: 6 samples in 3 pixels |
| `oq28_activity_qp` | prev2 | prev: 7 samples in 3 pixels |
| `oq29_bitsave_step` | 2 | 1: 4 samples in 3 pixels |
| `oq30_target_floor` | zero | none: 9 samples in 3 pixels |
| `oq31_flat_rerun` | every | changed: 9 samples in 3 pixels |
| `oq32_rerun_bitsave` | redo | keep: 9 samples in 3 pixels |
| `oq33_mux16` | 64 | 68: 8 samples in 3 pixels |
| `oq34_flat_top` | at-or-above | equal: 6 samples in 2 pixels |
| `oq35_prefix16_scope` | qlevel | qp0: 9 samples in 3 pixels |
| `oq36_prefix16_cut` | longer | always: 18 samples in 6 pixels |

Ten confirm the readings the model streams showed. OQ-34, whose default
was the text, went the other way: `flat_top` now defaults to
`at-or-above`. RESEARCH.md marks OQ-26 to OQ-36 resolved. The committed
inputs regenerate unchanged under the new default, and all 11,816
discriminator decodes pass.

## Phase 4, part 5: model comparison, DSC 1.2 RGB 4:4:4 (2026-09-24)

Phase 4 is complete with this part. The decoder is the tree of this commit
(release build, SHA-256 7a6afb3c…c11e, copied to
`~/dsc-runs/m3/phase4/bin/dscdecode` for the runs); the only code change
since f589964 is the OQ-34 default.

Every run: the model encodes the picture in DSC 1.2 mode
(`DSC_VERSION_MINOR 2`) with the `rc_<bpc>bpc_<bpp>bpp.cfg` of the rate
(6, 8, 10, 12 and 15 bpp: every installed 4:4:4 rate at every depth), line
buffer bpc + 1 (16 at 16 bpc), block prediction off and on, 1, 2 and 4
slices per line; the model decodes its stream and dscdecode decodes the same
PPS and payload with its default readings. "Match" is bit-exact equality
of all samples. `~/dsc-runs/m3/phase4/matrix.sh`; synthetic pictures from
`tools/make_pictures` (8 bits added in this part), the corpus from
`~/vesa-corpus`, the derived corpus as in Phase 3.

| Pictures | bpc | Runs per rate (6 / 8 / 10 / 12 / 15 bpp) | Bit-exact | Differing samples | Model input check |
|---|---|---|---|---|---|
| synthetic (8) | 8 | 48 each | 240 / 240 | 0 | 240 / 240 |
| synthetic (8) | 10 | 48 each | 240 / 240 | 0 | 240 / 240 |
| synthetic (8) | 12 | 48 each | 240 / 240 | 0 | 240 / 240 |
| synthetic (8) | 14 | 48 each | 240 / 240 | 0 | 240 / 240 |
| synthetic (8) | 16 | 48 each | 240 / 240 | 0 | 240 / 240 |
| corpus (17) | 8 | 102 each | 510 / 510 | 0 | (8-bit PPM, read directly) |
| derived (17) | 10 | 102 each | 510 / 510 | 0 | 510 / 510 |
| derived (17) | 12 | 102 each | 510 / 510 | 0 | 510 / 510 |
| total | | | 2,730 / 2,730 | 0 | |

Every rate row of every set matched in full (per-rate counts in each
`results.json`). At 14 bpc all 240 encodes ended with the model's encoder
dying from a signal after writing its output (part 4); every one of them
decoded bit-exact. Results: `~/dsc-runs/m3/phase4/runs/corpus/`
`20260924-182001` (synthetic-8), `-182009` (10), `-182018` (12), `-182028`
(14), `-182432` (16), `-182442` (corpus-8), `-182919` (derived-10),
`-183548` (derived-12).

README: "Build and use" now lists the DSC 1.2 RGB 4:4:4 profiles the
decoder accepts, and "Verification and fuzzing" describes
`tools/make_pictures`, the high-bit-depth handling of the harness and the
DSC 1.2 discriminator generator. "Remaining correctness work" still
describes v0.1.0 (M3 changes only those two sections); this log is the
record.

Tests at this commit:

| Suite | Phase 0 | Now |
|---|---|---|
| Image fixtures, bit-exact | 10 | 16 |
| CLI checks | 26 | 32 |
| Discriminator decodes | 72 | 11,816 (37 inputs) |
| `test_rc` cases | 29 | 33, plus the DSC 1.2 short-term and OQ-7 cases |
| `test_predict` cases | 7 | 7 |

Gate:

* `scripts/ci.sh` with `DSCDECODE_MODEL_BIN` unset: all steps pass, model
  SKIP. libFuzzer 305,755 executions in 61 s (the model matrix was running
  on the same CPUs); deterministic smoke 1,840,000.
* With `DSCDECODE_MODEL_BIN=/usr/local/bin/dsc-ref`: all steps pass; every
  discriminator's verdict equals the decoder's default reading (OQ-1, 2b,
  3, 4, 7, 19 to 36; oq2 and oq24 superseded). libFuzzer 368,900
  executions in 61 s; deterministic smoke 1,840,000.
* 1.1 regression: 34 of 34 bit-exact (`~/dsc-runs/corpus/20260924-184142`).

Logs: `~/dsc-runs/m3/phase4/gate-*.log`, `matrix-*.log`.

## Phase 5, part 1: YCbCr and native 4:2:2 and 4:2:0 decoding, OQ-37 to OQ-43 discriminators (2026-09-24)

Phase 5 is not complete at this commit. This part is committed so that the
ten new discriminators and their predictions are in the history before the
reference model decodes them; the model has decoded none of them. Section
numbers are DSC 1.2b unless marked.

### Decoder

* YCbCr (convert_rgb 0) in DSC 1.1 and 1.2: 4:4:4, simple 4:2:2 (Annex B:
  4:4:4 coding, the even-position chroma kept on output), and in DSC 1.2
  native 4:2:2 and native 4:2:0. Chroma has the luma depth (§7.7); in DSC
  1.2 qLevelC is one below Table 6-3 (§6.8.6).
* Native modes code a half-width container (§3.10.1, §4.3, §6.1): four
  units in 4:2:2 (Y even, Cb, Cr, Y odd; substreams Y, Co, Cg, Y2), three in
  4:2:0 (Y even, Y odd in Co, Cb or Cr by line in Cg). The rate control
  counts container pixels (§6.8.1); chunk_size follows the container width;
  native 4:2:2 has its overflow threshold of −224 (§6.8.4); predActivity
  has the native forms (OQ-37, OQ-38); the second-line offsets of native
  4:2:0 apply (§6.8.2, OQ-40). Prediction: MMAP per container unit, native
  4:2:0 chroma from the second line above and first-line prediction for
  its lines 0 and 1, BP on luma only in 4:2:0 (§6.4), ICH entries are
  container pixels, previous-line entries are luma pairs at any position
  (§6.5.1, OQ-41), 32 history entries on the first two lines of 4:2:0. ICH
  indices of native 4:2:2 are in Y2, Co and Cg (§6.6.2). RESEARCH.md lists
  the points taken without a switch.
* Output: `dsc_decode_*_planes` returns Y, Cb, Cr planes for YCbCr, chroma
  at half width for 4:2:2 and also half height for 4:2:0
  (`dsc_plane_size`). `dscdecode` writes YCbCr to `NAME.yuv` in the
  layouts of the model's .yuv files: planar 4:2:0, UYVY 4:2:2, and (the
  model writes none) planar 4:4:4; above 8 bits two bytes per sample, least
  significant first, the value in the top bits, as in the model's files.
  RGB streams still go to PPM.
* bits_per_pixel is accepted up to 1023 (it was capped at 384; in native
  modes it is twice the picture's rate, and 16 bpc RGB can exceed 24 bpp);
  a group may have up to 512 bits (four units at 16 bpc).

### Found on model-encoded streams

The model's I/O, from its README.TXT and the files it writes: `.yuv` input
and output (YUV_FILE_FORMAT 0, planar 4:2:0; 1, UYVY), 16-bit little-endian
samples above 8 bpc with the value in the top bits (a 12-bit 194 is read as
12, and written back as 192); in decode mode the `.yuv` format must match
the coding format (a 4:2:0 stream written as UYVY crashes it); 4:4:4 YCbCr
only as DPX (descriptor 102: Cb, Y, Cr per pixel; 8 bits: bytes reversed in
each 32-bit big-endian word; 10 bits: one word per pixel, Cb in bits 2–11,
Y 12–21, Cr 22–31; 12, 14, 16 bits: 16-bit samples, two per word from the
low half, holding v·16, floor(v·65535/16383) and v).

First streams (256x64 and 128x32 synthetic YCbCr, `~/dsc-runs/m3/phase5`):
native 4:2:2, simple 4:2:2 and 8-bit YCbCr 4:4:4 decoded bit-exact at once.
Native 4:2:0 diverged from group 16; it decodes bit-exact once
second_line_offset_adj is also added at the slice start, as Table E-2 says
(OQ-40). Textured pictures with block prediction and ICH decoded bit-exact
only with previous-line ICH windows clamped in container pixels (OQ-41).
YCbCr 4:4:4 at 10 to 16 bpc on small slices, and then an 8-bit RGB DSC 1.2
picture of 128x32, diverged: with an initial_scale_value of 30 or 32
decremented every group, the model skips a decrement due at the first group
(OQ-42) and makes none after the first line (OQ-43), in DSC 1.1 as in 1.2.
The Phase 4 comparisons (slices 108 lines high and more, longer decrement
intervals) did not reach either case; with both new defaults the
DSC 1.1 regression check still gives 34 of 34 matches
(`~/dsc-runs/corpus/20260924-193723`).

### Switches, discriminators, tests

* Seven switches: `activity420` (OQ-37), `activity422` (OQ-38),
  `bp420_edge` (OQ-39), `offset_adj` (OQ-40), `ich_window` (OQ-41),
  `scale_first` (OQ-42), `scale_line` (OQ-43). OQ-37 to OQ-39 default to
  the reading judged more likely from the text; OQ-40 to OQ-43 to the
  reading the model streams showed. `--stats` counts `activity_differs`,
  `bp420_edge_differs` and `ich_window_differs`.
* Ten discriminators (`oq37_activity420` to `oq43b_scale_line`),
  predictions in `tests/discriminators/README.md` and `manifest.json`;
  native inputs by the new `tests/make_native_discriminators.py`. Their
  expected pictures are raw YCbCr. `tests/pydsc.py` now models YCbCr, the
  native containers and block prediction; on the first slice of eight
  model-encoded native and 4:4:4 streams its output equals the model's.
  Discriminator decodes: 11,816 → 13,896.
* `tools/compare_model`: YCbCr streams (the model's output format chosen per
  stream, DPX 102 read back to planar), raw YCbCr comparison, the new
  switches, `_422`/`_420` rate files. The stand-in model writes YCbCr too.

## Phase 5, part 2: the model decides OQ-37 to OQ-43; YCbCr model comparison (2026-09-25)

Phase 5 is complete with this part.

### Discriminators

After 81f0e3b the model decoded every discriminator
(`~/dsc-runs/m3/phase5/disc-model5.log`, runs under
`~/dsc-runs/m3/phase5/runs/compare/20260924-195014-*`). The earlier inputs
gave their earlier verdicts; the new ones:

| Input | Model output matches | Other prediction |
|---|---|---|
| `oq37_activity420` | luma | sum: 3 samples differ |
| `oq38_activity422` | sizes | total: 4 samples differ |
| `oq39_bp420_edge` | luma | all: 10 samples differ |
| `oq40_offset_adj` | start | subtract: 2 samples differ |
| `oq41_ich_window` | container | pixels: 20 samples differ |
| `oq41b_ich_window` | container | pixels: 13 samples differ |
| `oq42_scale_first` | not | group: 9 samples in 3 pixels |
| `oq42b_scale_first` | not | group: 9 samples in 3 pixels |
| `oq43_scale_line` | first | until-unity: 5 samples in 2 pixels |
| `oq43b_scale_line` | first | until-unity: 5 samples in 2 pixels |

Every default was already the reading the model supports; RESEARCH.md marks
OQ-37 to OQ-43 resolved.

### Pictures and conversion

`tools/make_pictures --format` converts RGB pictures to YCbCr. The
conversion (also in its docstring): ITU-R BT.709 weights, limited range.
With R, G, B scaled to [0, 1] by 2^bpc − 1, Y' = 0.2126 R + 0.7152 G +
0.0722 B, Pb = (B − Y') / 1.8556, Pr = (R − Y') / 1.5748, and with
s = 2^(bpc − 8): Y = round(s·(16 + 219·Y')), Cb = round(s·(128 + 224·Pb)),
Cr = round(s·(128 + 224·Pr)), each clamped to the sample range. 4:2:2
chroma is the mean of two horizontally adjacent 4:4:4 samples, 4:2:0 chroma
the mean of a 2x2 block, both rounded half up; odd widths (and for 4:2:0 odd
heights) lose their last column (row) first, so s08_odd_size is 636x125 in
the 4:2:2 formats and 636x124 in 4:2:0. The sources: the eight synthetic
RGB pictures of Phase 3 (640x216, one 637x125) at 8, 10, 12, 14 and 16 bpc;
the 17 corpus images (1920x1080, 8 bpc) as they are; and the Phase 3
derived corpus at 10 and 12 bpc (each image scaled to the depth, plus a few
LSBs of deterministic noise). Outputs under
`~/dsc-runs/hbd-pictures/{synthetic,corpus,derived}-<bpc>-<format>`.

The model reads them as follows (found from its README.TXT and the files
it writes; the harness sets these options): 4:4:4 as DPX descriptor 102 in
its own default write layout, read with DPXR_DATUM_ORDER 0 and
SWAP_R_AND_B 0 (the harness's template sets both the other way, which
reverses the bytes of each word and was the first cause of mismatching
input checks); 14-bit samples as 16-bit words holding ceil(v·65535/16383),
which it scales back with a floor (v << 2 reads back as v − 1); 4:2:2 as
UYVY `.yuv` whose 16-bit samples hold the value in the top bits; 4:2:0 as
planar `.yuv` whose 16-bit samples hold the value as it is (it refuses
larger values: "sample value did not match expected bit depth"). dscdecode's
`.yuv` output follows the same two conventions. Each run checks the model's
copy of its input (`.ref.yuv`, or `.ref.dpx` read back) against the source.

### Model comparison, DSC 1.2 YCbCr CBR

The decoder is the tree of this commit (release build, SHA-256
7e0eaebf…d786c, copied to `~/dsc-runs/m3/phase5/bin/dscdecode` for the
runs). Script `~/dsc-runs/m3/phase5/matrix5.sh` (the run was interrupted
once; `matrix5b.sh` resumed from the set it stopped in); results under
`~/dsc-runs/m3/phase5/runs/corpus/` and in
`~/dsc-runs/m3/phase5/aggregate_v12.txt`. Every picture of each set was
coded at three picture rates (4:4:4 and simple 4:2:2: 6, 8 and 12 bpp;
native 4:2:2: 6, 8 and 10; native 4:2:0: 4, 6 and 8, with the model's
`_422` and `_420` rate files), BP off and on, 1 and 2 slices per line
(slices 108 lines high, the model's default); the model decoded its own
bitstream and dscdecode decoded the same bitstream.

| Format | Synthetic, 8–16 bpc (8 pictures) | Corpus, 8 bpc (17) | Derived, 10 and 12 bpc (17) | Runs | Bit-exact |
|---|---|---|---|---|---|
| YCbCr 4:4:4 | 480 | 204 | 408 | 1,092 | 1,092 |
| Simple 4:2:2 | 480 | 204 | 408 | 1,092 | 1,092 |
| Native 4:2:2 | 480 | 204 | 408 | 1,092 | 1,092 |
| Native 4:2:0 | 480 | 204 | 408 | 1,092 | 1,092 |
| All | 1,920 | 816 | 1,632 | 4,368 | 4,368 |

All 4,368 decodes are bit-exact, with the defaults, and the model's copy of
its input matched the source on every run. At 14 bpc the model's encoder
died from a signal after writing its output in 192 runs (YCbCr 4:4:4 and
native 4:2:2), accepted as in Phase 4. Where the Phase 5 questions arose,
counted by `--stats` with the defaults: in native 4:2:2 the two predActivity
readings disagreed on the keep-or-reset test 11,137,939 times and the ICH
window readings placed 436,694 previous-line lookups differently; in native
4:2:0, 2,929,780 predActivity disagreements, 306,222 BP decisions that
depend on the chroma edge test, and 478,884 window disagreements. Block
prediction was used in 68,840,236 groups in all.

DSC 1.1 YCbCr (4:4:4 and simple 4:2:2) was not compared. The model's
encoder refuses it with its own rate files at every depth ("In DSC 1.1 mode
with YCbCr, the max QP for range 14 must be less than" 12, 16 or 20, even
where the files' range 14 maximum is lower); the 480 runs attempted
stopped at the encoder. The decoder's DSC 1.1 YCbCr path is tested by the
fixture `ycc444_v11_8` only. `tools/run_corpus` now labels such rows
"harness error" instead of "dscdecode error".

### Tests

* `tests/make_ycbcr_vectors.py`: six lossless YCbCr fixtures (4:4:4 at DSC
  1.1 8 bpc and DSC 1.2 10 bpc, simple 4:2:2 at 12, native 4:2:2 at 10,
  native 4:2:0 at 14 and 16), whose expected `.yuv` is written from the
  chosen samples and which pydsc decodes to the same bytes.
  `tests/test_cli.py` decodes them and checks that YCbCr output to `.ppm`
  and RGB output to `.yuv` are refused: CLI checks 32 → 40.
* `tests/test_rc.c`: OQ-42 and OQ-43 under all four combinations.
* Discriminator decodes: 13,896.

### Gate

`scripts/ci.sh` with `DSCDECODE_MODEL_BIN` unset: every step passes, model
SKIP. With `DSCDECODE_MODEL_BIN=/usr/local/bin/dsc-ref`: every step
passes, including the model step (self-test and every discriminator; the
superseded inputs are reported for the record). DSC 1.1 regression check,
`tools/run_corpus --bpp 8 --bp 0 1 --slices 2 --min-images 17`: 34 of 34
match (`~/dsc-runs/corpus/20260925-005420`).

## Phase 6: fuzzing the new formats and switches, DSC 1.1 corpus rerun (2026-09-25)

Phase 6 is complete with this commit.

### Fuzz target

`fuzz/fuzz_decode.c` also sets the seven Phase 5 switches (`activity420`,
`activity422`, `bp420_edge`, `offset_adj`, `ich_window`, `scale_first`,
`scale_line`) from the input's bytes, and decodes the single slice through
the planes API under those readings as well. The formats come from the
PPS bytes; the seed corpus (`tests/make_corpus.py`) now includes the YCbCr
fixtures and the native discriminators, so its seeds cover RGB and YCbCr
4:4:4, simple 4:2:2 and native 4:2:2 and 4:2:0 at 8 to 16 bpc.

### libFuzzer campaign

`build/fuzz/fuzz_decode` (`make fuzz`: Ubuntu clang 18.1.3, `-O1
-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined`),
`ASAN_OPTIONS=detect_leaks=1`, `UBSAN_OPTIONS=halt_on_error=1`, six fork
workers (`-fork=6`, crashes, timeouts and out-of-memory reports collected
without stopping), `-max_total_time=1800 -max_len=65536 -timeout=10
-rss_limit_mb=2048`, seeded from `tests/corpus`
(log `~/dsc-runs/m3/phase6/fuzz30.log`, corpus
`~/dsc-runs/m3/phase6/fuzz-corpus`).

* 1,806 seconds, 60,167,835 executions (about 33,000 a second over the six
  workers).
* 0 crashes, 0 timeouts, 0 out-of-memory reports; no artifacts.
* Coverage at the end: 5,371 edges, 5,524 features, 1,132 corpus entries
  (1,247 files kept).
* The kept corpus, classified by its PPS: 173 native 4:2:0, 58 native 4:2:2,
  24 YCbCr 4:4:4, 22 simple 4:2:2, 969 RGB inputs and one input shorter
  than a PPS; 9 of the native ones and 5 of the other YCbCr ones decode
  without error, the others stop at an error inside or before the slice.

No crash was found, so there was nothing to fix and no regression input to
add.

### DSC 1.1 corpus, full rerun

`tools/run_corpus --bpp 6 7.5 8 10 12 15` (defaults: BP off and on; 1, 2
and 4 slices per line) with the release build (SHA-256 7e0eaebf…d786c,
unchanged by this commit): 17 images, **612 of 612 runs bit-exact**
(`~/dsc-runs/corpus/20260925-012804`).

### Gate

`scripts/ci.sh` without `DSCDECODE_MODEL_BIN`: every step passes, model
SKIP. With `DSCDECODE_MODEL_BIN=/usr/local/bin/dsc-ref`: every step passes.
DSC 1.1 regression check: 34 of 34 match
(`~/dsc-runs/corpus/20260925-014631`).

# v0.2.0 release preparation (branch m3-dsc12)

This part prepares release v0.2.0 from the M3 branch. The decoder code is
not changed: only documentation, test tooling and records. Runs and scratch
files are under `~/dsc-runs/v0.2.0/`. Each phase ends with `scripts/ci.sh`
without and with `DSCDECODE_MODEL_BIN`, then a rebuild of the release binary,
whose SHA-256 is compared with Phase 0's.

## Phase 0: frozen binary (2026-09-25)

At 91cfa42, `make clean && make` (release flavor, `CFLAGS` default `-O2 -g`)
built `build/release/dscdecode` with SHA-256
`7e0eaebf78bc295db741ddfd4691cfde90b3cda8a376451d884865140e7d786c`, the
binary of M3 Phases 5 and 6. A second clean build gave the same bytes. The
copy `~/dsc-runs/v0.2.0/bin/dscdecode` is the binary of every model run in
this part (`DSCDECODE_BIN`).

| Tool | Version |
|---|---|
| GCC (`cc`) | 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) |
| Clang (libFuzzer) | 18.1.3 (1ubuntu1) |
| Python | 3.12.3, Pillow 10.2.0 |
| GNU Make | 4.3 |
| Host | Linux 7.0.0-34-generic, x86-64, 8 CPUs |
| Model | `/usr/local/bin/dsc-ref`, version 1.67 |

Gate (script `~/dsc-runs/v0.2.0/gate.sh`, logs in `~/dsc-runs/v0.2.0/phase0/`):

* `scripts/ci.sh` without `DSCDECODE_MODEL_BIN`: every step passes, model
  SKIP. libFuzzer 509,411 executions in 61 s; deterministic smoke 2,480,000.
* With `DSCDECODE_MODEL_BIN=/usr/local/bin/dsc-ref` and `DSCDECODE_BIN` set
  to the frozen copy: every step passes. The model step used the frozen
  copy; self-test match; every discriminator's verdict is the decoder's
  default reading (`oq2` and `oq24` superseded). libFuzzer 441,177
  executions in 61 s; deterministic smoke 2,480,000.
* Rebuild after the gate: byte-identical to the frozen copy.

## Phase 1: what the standards say about the model (2026-09-25)

RESEARCH.md has a new section, "DSC 1.2 text and the reference model". It
records what DSC 1.1, DSC 1.2a (with its E1 errata) and DSC 1.2b say about
the C model's status and precedence. For each question whose default
follows the model where the DSC 1.2b text reads otherwise or is silent
(OQ-19, OQ-21, OQ-23 to OQ-36, OQ-40 to OQ-43) it gives the section, the
text's reading, the model's, the errata and what precedence implies.

* All three standards give the model precedence over the text: DSC 1.1 and
  DSC 1.2a in the introductions of §6 and §7 (and list it as a normative
  reference), DSC 1.2b in §1.4.3 for the whole standard. Two DSC 1.2a E1
  SCRs call the C code normative and correct and change the text to match
  it. DSC 1.2b names model version 1.63; the model here is 1.67.
* No errata in hand covers any of the questions listed.
* Summary: OQ-21, OQ-26, OQ-27, OQ-29, OQ-30, OQ-34, OQ-35, OQ-36 and OQ-40
  look like errors in the text; OQ-19, OQ-23, OQ-25, OQ-31, OQ-32, OQ-33,
  OQ-41 and OQ-42 look like ambiguity; OQ-24, OQ-28 and OQ-43 remain
  unclear.
* The DSC 1.2a E1 errata PDF embeds model source excerpts. One was displayed
  while the errata was searched; it was not used (source note in the
  section).
* A 12-word overlap check of RESEARCH.md against the text of the five spec
  PDFs finds no shared run (also none at 9 words).

Gate (`~/dsc-runs/v0.2.0/phase1/`): `scripts/ci.sh` without the model:
every step passes, model SKIP (libFuzzer 452,873 executions in 61 s;
deterministic smoke 2,480,000). With the model and the frozen binary: every
step passes, every discriminator verdict is the default reading (libFuzzer
483,741 in 61 s; smoke 2,480,000). Rebuild: byte-identical to the frozen
binary.

## Phase 2: where each answer came from (2026-09-25)

* RESEARCH.md, open-questions table: a column "Hypothesis source" for OQ-1
  to OQ-43. Text: 20 questions (OQ-1 to OQ-4, OQ-6 to OQ-10, OQ-12, OQ-13,
  OQ-20 to OQ-23, OQ-25, OQ-34, OQ-37 to OQ-39). Model output: 22 (OQ-5,
  OQ-11, OQ-14 to OQ-19, OQ-26 to OQ-33, OQ-35, OQ-36, OQ-40 to OQ-43).
  Both: OQ-24 (two text readings first, four fitted; the default is fitted).
  Each model-output cell says which reading came first. OQ-5's cell also
  records that its model reading was first noted in M1 from model source and
  applied only after M2 fitted it to model output.
* A paragraph before the table explains the difference: for a model-output
  question a discriminator confirms a fitted rule on a new input, and is not
  an independent prediction.
* THIRD_PARTY.md: a paragraph on how the DSC 1.2 rate-control behavior was
  found (black-box probing of the model's output with a debug build of this
  decoder and controlled encodes; predictions committed before the model
  decoded each confirming input; no model source used).

Gate (`~/dsc-runs/v0.2.0/phase2/`): `scripts/ci.sh` without the model:
every step passes, model SKIP (libFuzzer 497,841 executions in 61 s;
smoke 2,480,000). With the model and the frozen binary: every step passes,
every discriminator verdict is the default reading (libFuzzer 509,233 in
61 s; smoke 2,480,000). Rebuild: byte-identical to the frozen binary.

## Phase 3: hygiene audit of everything since main (2026-09-25)

Scope: the 302 files that differ between 6382dae (main) and the tip, and,
for what a history rewrite would be needed to remove, the commits
6382dae..HEAD. Scripts and outputs in `~/dsc-runs/v0.2.0/audit/`.

| Check | Method | Result |
|---|---|---|
| Spec text | Every run of 12 or more consecutive words shared with the text of the five spec PDFs (DSC 1.1, DSC 1.1 E1, DSC 1.2a, DSC 1.2a E1, DSC 1.2b; both text extractions of each), case-insensitive, whitespace-normalized; also with punctuation removed. Run on the files at the tip and on every line added in 6382dae..HEAD | None at the tip, none in the history |
| Model material | Model source file names (the ten names of the audit list), copyright notices, mirror links, statements of model behavior | No file name. The copyright lines are the vendored Intel header and the quoted VESA license agreement in RESEARCH.md (not model text). The only links are to Linux, NVIDIA and vesa.org pages; the M1 mirror is named without a link. Every M3 statement of model behavior cites its README, its configuration files, its command line or its output files; OQ-5 names M1's model-source note as the origin of a reading, now in the "Hypothesis source" column |
| Committed binaries | Every file that is not text | 223, all under `tests/fixtures`, `tests/corpus` and `tests/discriminators`; the fixture step of `scripts/ci.sh` rebuilds those directories from empty with the generators and diffs them (passes in every gate). No other binary is tracked. No model output, archive, PDF or configuration file was ever committed in 6382dae..HEAD, and no file was added and later deleted |
| Personal data and paths | Absolute home paths, host and user names, e-mail addresses, keys and tokens | None in the files. Paths are written `~/dsc-runs/...`. Commit metadata carries the author's name and e-mail (reported only) |
| Wording | "conformant", "validated", "certified", "compliant", "clean-room" not negated; references to instructions, to people, or to the working sessions | Every status word is negated or technical. Fixed at the tip in PROGRESS.md: four references to working rules (Phase 3 part 1, Phase 3 part 2, Phase 4 part 2, Phase 4 part 5), one to a working session (Phase 5 part 2), the M2 audit's description of its own wording fixes, and a tool-generated branch name (refactor verification) |

Would need a history rewrite (not done): the wording fixed above remains in
the commits that introduced it (4f164ba, 1287598, 352b429, 2622753 and
785522e on this branch; the M2 audit description in 3868701 and the branch
name in 6382dae, both on main), and every commit records the author's
e-mail address.

Gate (`~/dsc-runs/v0.2.0/phase3/`): `scripts/ci.sh` without the model:
every step passes, model SKIP (libFuzzer 498,318 executions in 61 s;
smoke 2,480,000). With the model and the frozen binary: every step passes,
every discriminator verdict is the default reading (libFuzzer 429,151 in
61 s; smoke 2,480,000). Rebuild: byte-identical to the frozen binary.

## Phase 4: v0.2.0 release gate (2026-09-25)

Every comparison below used the frozen binary
`~/dsc-runs/v0.2.0/bin/dscdecode`, SHA-256
`7e0eaebf78bc295db741ddfd4691cfde90b3cda8a376451d884865140e7d786c` (GCC
13.3.0, `make`, `-O2 -g`; toolchain in Phase 0), with its default readings,
and the VESA C model version 1.67 as a black box. Every `results.json`
records that binary. Scripts: `~/dsc-runs/v0.2.0/make_pictures.sh`,
`matrix.sh`, `aggregate.py`; logs in `~/dsc-runs/v0.2.0/logs/`; results
under `~/dsc-runs/v0.2.0/runs/corpus/` (one directory per set, listed in
`~/dsc-runs/v0.2.0/resultdirs.txt`) and `runs/compare/`.

Pictures. `tools/make_pictures` regenerated every set into
`~/dsc-runs/v0.2.0/pictures/` (16 invocations: synthetic RGB at 8 to 16 bpc,
derived RGB at 10 and 12, and synthetic, derived and corpus in each of the
four YCbCr formats). All 876 files, in 39 sets, are byte-identical to
`~/dsc-runs/hbd-pictures/` (`diff -r` and SHA-256 lists). The runs used the
regenerated sets; the 8-bit RGB corpus is `~/vesa-corpus` as before.

"Bit-exact" is equality of every sample of the model's decode and
dscdecode's decode of the model's bitstream. "Input check" is the harness's
check that the model read the picture exactly (not made for the 8-bit corpus
PPMs, which the model reads directly).

DSC 1.1 RGB 4:4:4, 8 bpc: the 17 corpus images, BP off and on, 1, 2 and 4
slices per line (`20260925-151453`).

| bpp | 6 | 7.5 | 8 | 10 | 12 | 15 | Total |
|---|---|---|---|---|---|---|---|
| Bit-exact / runs | 102 / 102 | 102 / 102 | 102 / 102 | 102 / 102 | 102 / 102 | 102 / 102 | 612 / 612 |

DSC 1.1 RGB 4:4:4, 10 and 12 bpc: every installed rate, BP off and on, 1, 2
and 4 slices.

| Pictures | bpc | 6 / 8 / 10 / 12 / 15 bpp | Bit-exact / runs | Input check | Results |
|---|---|---|---|---|---|
| synthetic (8) | 10 | 48 each | 240 / 240 | 240 / 240 | `20260925-152027` |
| synthetic (8) | 12 | 48 each | 240 / 240 | 240 / 240 | `20260925-152037` |
| derived (17) | 10 | 102 each | 510 / 510 | 510 / 510 | `20260925-152047` |
| derived (17) | 12 | 102 each | 510 / 510 | 510 / 510 | `20260925-152601` |
| total | | | 1,500 / 1,500 | | |

DSC 1.2 RGB 4:4:4, 8 to 16 bpc (the M3 Phase 4 matrix): same rates and
settings, line buffer bpc + 1 (16 at 16 bpc).

| Pictures | bpc | 6 / 8 / 10 / 12 / 15 bpp | Bit-exact / runs | Input check | Encoder signal | Results |
|---|---|---|---|---|---|---|
| synthetic (8) | 8 | 48 each | 240 / 240 | 240 / 240 | 0 | `20260925-153147` |
| synthetic (8) | 10 | 48 each | 240 / 240 | 240 / 240 | 0 | `20260925-153155` |
| synthetic (8) | 12 | 48 each | 240 / 240 | 240 / 240 | 0 | `20260925-153205` |
| synthetic (8) | 14 | 48 each | 240 / 240 | 240 / 240 | 240 | `20260925-153215` |
| synthetic (8) | 16 | 48 each | 240 / 240 | 240 / 240 | 0 | `20260925-153619` |
| corpus (17) | 8 | 102 each | 510 / 510 | (not made) | 0 | `20260925-153630` |
| derived (17) | 10 | 102 each | 510 / 510 | 510 / 510 | 0 | `20260925-154106` |
| derived (17) | 12 | 102 each | 510 / 510 | 510 / 510 | 0 | `20260925-174745` |
| total | | | 2,730 / 2,730 | | 240 | |

DSC 1.2 YCbCr (the M3 Phase 5 matrix): three rates per format (4:4:4 and
simple 4:2:2: 6, 8, 12 bpp; native 4:2:2: 6, 8, 10; native 4:2:0: 4, 6, 8),
BP off and on, 1 and 2 slices per line. Each cell is bit-exact / runs; every
rate of every set matched in full, and the input check matched on all 4,368.

| Format | Synthetic 8 / 10 / 12 / 14 / 16 bpc | Corpus 8 bpc | Derived 10 / 12 bpc | Total | Encoder signal (14 bpc) |
|---|---|---|---|---|---|
| YCbCr 4:4:4 | 96 / 96 each | 204 / 204 | 204 / 204 each | 1,092 / 1,092 | 96 of 96 |
| Simple 4:2:2 | 96 / 96 each | 204 / 204 | 204 / 204 each | 1,092 / 1,092 | 0 of 96 |
| Native 4:2:2 | 96 / 96 each | 204 / 204 | 204 / 204 each | 1,092 / 1,092 | 96 of 96 |
| Native 4:2:0 | 96 / 96 each | 204 / 204 | 204 / 204 each | 1,092 / 1,092 | 0 of 96 |
| total | | | | 4,368 / 4,368 | 192 |

Discriminators, `tools/compare_model discriminators`
(`logs/discriminators.log`): exit 0. For every input with a verdict, the
model's output matches the prediction of the decoder's default reading.
`oq24b_bitsave_flat` and `oq24c_bitsave_flat` were built before `lagged`
existed and have no prediction for it; decoded under the defaults, each is
bit-exact with the model (`logs/discriminators-default.log`). The
superseded `oq2_threshold_equality` and `oq24_bitsave_flat` are reported,
not judged, as before; under the defaults `oq24_bitsave_flat` is bit-exact
with the model and `oq2_threshold_equality` is rejected (it was built under
the M1 readings of other questions).

| Inputs | With a verdict | Verdict = default reading | Superseded |
|---|---|---|---|
| 38 | 36 | 36 (34 by name, `oq24b` and `oq24c` by a default decode) | 2 |

Fixtures (`logs/fixtures.log`): each of the 24 frame fixtures (PPS and
payload) decoded by the model and by dscdecode, `tools/compare_model
bitstream`: 24 of 24 bit-exact, including `ycc444_v11_8` (DSC 1.1 YCbCr
4:4:4) and the two invalid-padding fixtures. dscdecode's output equals the
committed expected picture for all 22 that have one.

Totals:

| Set | Streams | Bit-exact |
|---|---|---|
| DSC 1.1 RGB 4:4:4, 8 bpc | 612 | 612 |
| DSC 1.1 RGB 4:4:4, 10 and 12 bpc | 1,500 | 1,500 |
| DSC 1.2 RGB 4:4:4, 8 to 16 bpc | 2,730 | 2,730 |
| DSC 1.2 YCbCr 4:4:4, simple 4:2:2, native 4:2:2, native 4:2:0 | 4,368 | 4,368 |
| Hand-derived fixtures | 24 | 24 |
| Discriminators (verdict = default) | 36 | 36 |
| Total | 9,270 | 9,270 |

14 bpc encoder signal: the model's encoder died from a signal after writing
its output in 432 of the 624 encodes at 14 bpc (all 240 RGB, all 96 YCbCr
4:4:4, all 96 native 4:2:2; none of the simple 4:2:2 or native 4:2:0
encodes), the same pattern as in M3. Each was accepted under the harness's
conditions and decoded bit-exact.

Gate (`~/dsc-runs/v0.2.0/phase4/`): `scripts/ci.sh` without the model:
every step passes, model SKIP (libFuzzer 576,443 executions in 61 s;
smoke 2,480,000). With the model and the frozen binary: every step passes
(libFuzzer 529,381 in 61 s; smoke 2,480,000). Rebuild: byte-identical to
the frozen binary.

## Phase 5: README, CHANGELOG, release notes (2026-09-25)

* README: the summary sentence now describes DSC 1.1 and 1.2 and the 9,270
  streams of the release gate. Status is rewritten for v0.2.0: the formats,
  the method paragraph (with the discriminators added), a table of the six
  sets of Phase 4 with their totals, one sentence on why the v0.1.0 row of
  148 streams is gone (its pictures no longer exist), the two superseded
  discriminators, the caveat on nonconforming streams, and the pointers to
  PROGRESS.md and THIRD_PARTY.md. The M2 checkpoint paragraph is removed.
  New section "Known issue in v0.1.x" (OQ-19, OQ-42, OQ-43). "Remaining
  correctness work" lists what is not done or not verified; each item was
  checked against the code and this log: VBR (rejected in `src/decode.c`
  and `src/rate_control.c`), DSC 1.1 YCbCr (encoder refusal, Phase 5 part
  2; the one fixture `ycc444_v11_8` decodes bit-exact with the model, Phase
  4 above), 14 bpc encoder signals (432 of 624, Phase 4 above), OQ-24's
  inferred interior groups, OQ-23's unseparated readings, fractional
  bits_per_pixel, the fuzzer's reach into non-RGB formats (14 of 277 kept
  inputs decode, Phase 6 of M3), OQ-9 still open, and the model not being
  included. "Patents", "Development" and the license line are unchanged.
* CHANGELOG.md: entries for v0.1.0, v0.1.1 and v0.2.0.
* Release notes: `~/dsc-runs/v0.2.0/release-notes.md` (outside the
  repository), one line per paragraph.

Gate (`~/dsc-runs/v0.2.0/phase5/`): `scripts/ci.sh` without the model:
every step passes, model SKIP (libFuzzer 432,709 executions in 61 s;
smoke 2,480,000). With the model and the frozen binary: every step passes
(libFuzzer 432,443 in 61 s; smoke 2,480,000). Rebuild: byte-identical to
the frozen binary.

## Wording fix before release (2026-09-25)

The README summary said DSC 1.1 was written from the specification alone,
but several DSC 1.1 defaults follow the model's observed behavior (the M2
fitted readings, OQ-19, OQ-42, OQ-43; "Hypothesis source" in RESEARCH.md).
The summary now says the decoder was written from the specification and
follows the model's observed behavior where the text is ambiguous or differs
from it, as DSC 1.2b §1.4.3 gives the model precedence. CHANGELOG.md
(v0.1.0 and v0.2.0) and the release notes say the same; the release notes
also state that the comparisons used model version 1.67 while DSC 1.2b
cites 1.63. No code change.
