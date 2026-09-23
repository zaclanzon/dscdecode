# Discriminator inputs

Each input here is a DSC 1.1 PPS (`NAME.pps`) plus a one-slice payload
(`NAME.bin`), 8 bpc RGB: 96×1 for the rate-control questions, 30×2 for the
block-prediction one. For one open question in
RESEARCH.md, the two readings decode it to different pixels. Each file below
states which reading predicts which output. The predictions were written and
committed before the VESA reference model decoded any of these inputs
(OQ-1 to OQ-3 in Phase 3, OQ-4 in Phase 2).

`python3 tests/make_discriminators.py` rebuilds every file here except this
README. The generator contains a rate-control model written separately from
the decoder. For every combination of the decoder's reading switches
(OQ-1, OQ-2, OQ-3, OQ-12: 16 combinations; for the BP input, OQ-4, OQ-10,
OQ-13: 8 combinations), it checks three things:

1. The entropy parse, meaning every bit of every group, is identical.
2. The output depends only on the reading of the discriminator's own question.
3. The two readings of that question give different outputs.

`tests/test_discriminators.py` then checks that dscdecode reproduces each
predicted output under all of those combinations. It also checks that the
decoder's `--stats` counters show the question's condition occurred.

## Common design

All three put the disagreement in the last group of the slice (group 31,
pixels 93–95). Groups 0–30 decode identically under both readings. The
question decides only which QP group 31 decodes at. Group 31 carries the same
residuals in every file: Y (3, −3, 2), Co (1, −2, 1), Cg (1, −2, 1). A
higher QP multiplies them by a larger power of two (DSC 1.1 §6.4.6, Table
6-2). Group 30 has all-zero residuals, which makes group 31's predicted size
0 under both readings, so group 31's prefix bits parse the same way (§6.6.1).

Every file relies on the two-group RC latency (OQ-11): group N's coded size
sets the QP of group N+2, and groups 0 and 1 decode at QP 0. The M1 fixtures
`qp_transition` and `qp_flatness` depend on it too. If the model's output
matches **neither** prediction of a discriminator, the result says nothing
about that discriminator's question. Look at OQ-11 first.

The designs also avoid the other known questions. None depends on the
comparison direction in Figure 6-13 (OQ-5). The only all-zero group, group 30,
would set the QP of group 32, which does not exist (OQ-6). The delay-boundary
reading (OQ-12) is one of the 16 combinations checked above.

## oq1_flat_restart — OQ-1, flatness restart ordering

Switch: `--reading flat_restart=next-cycle|in-flight`.

* 8 bpp, `initial_xmit_delay` 128. That is longer than the slice, so no bits
  leave the buffer. Ranges 0–13 have minQp = maxQp = 8. Range 14 has maxQp 15,
  which keeps §6.8.5.2 from suppressing overrides. Flatness is signaled at QP
  3–12.
* Group 0 is small (15 bits, below tgtMinusOffset 21). The decrement branch
  gives MAX(0, minQp 8) = 8, so groups 2–30 are QP 8 before any override.
* Group 27 sends next_flatness_flag = 1. Group 28 sends type 0 (somewhat flat)
  and group 1, so the override applies to group 30 (§4.5, §6.6.3).
* Group 29 codes in 24 bits with rcSizeGroup 21, inside [21, 27]. Its
  short-term cycle keeps prevQp.
* Group 30: stQp 8 becomes masterQp MAX(8 − 4, 0) = 4.
* **next-cycle**: the cycle after group 29 already ran from prevQp 8 and
  queued 8. Group 31 decodes at **QP 8** (qLevel Y 3, C 5).
* **in-flight**: that cycle is re-run from prevQp 4, stays in target, and
  queues 4. Group 31 decodes at **QP 4** (qLevel Y 1, C 3).

Group 31 starts from Y 192, Co 256, Cg 256. At QP 8, Y is 216, 192, 208 and
Co and Cg are 288, 224, 256. Pixel 93 is then t = 216 − 16 = 200,
B = 200 − 16 = 184, R = 32 + 184 = 216, G = 32 + 200 = 232 (§7.7).

| Reading | Group 31 QP | RGB at x = 93, 94, 95 | SHA-256 of expected PPM |
|---|---|---|---|
| next-cycle | 8 | (216,232,184) (192,176,224) (208,208,208) | `11b3d988499d241d…` |
| in-flight | 4 | (198,202,190) (192,188,200) (196,196,196) | `40c26acc5e87658f…` |

A third behavior, where the override value simply replaces the queued QP,
would also give QP 4 here. This file does not separate it from in-flight.

## oq2_threshold_equality — OQ-2, range threshold equality

Switch: `--reading threshold_eq=lower|upper`.

* 8 bpp, `initial_xmit_delay` 128 (no bits removed). The thresholds are the
  common 8 bpc values, the same as `rc_buf_thresh` in Linux
  `drm_dsc_helper.c`. Threshold 5 is
  84 × 64 − 8192 = −2816. Ranges 0–5 have minQp = maxQp = 8; ranges 6–14
  have minQp = maxQp = 0. `initial_offset` is 5733.
* Inside the initial delay, rcXformOffset falls 24 bits per group
  (§6.8.2), so rcModelFullness changes by (coded bits − 24) per group. It
  starts at 5733 − 8192 = −2459. Group 0 codes 15 bits (−2468); groups 1–28
  code 12 bits each (−2804 after group 28, range 6). Group 29 codes 12 bits:
  **−2816, exactly threshold 5**.
* Group 29 is below target, so the decrement branch gives
  MAX(prevQp − 1, minQp) of its range, and that QP decodes group 31.
* **lower**: the value belongs to range 5, minQp 8. Group 31 decodes at
  **QP 8**.
* **upper**: the value belongs to range 6, minQp 0. Group 31 decodes at
  **QP 0**.

| Reading | Group 31 QP | RGB at x = 93, 94, 95 | SHA-256 of expected PPM |
|---|---|---|---|
| lower | 8 | (152,168,120) (128,112,160) (144,144,144) | `522adaa166d082b0…` |
| upper | 0 | (132,132,131) (129,128,130) (130,130,130) | `20657883ee83d170…` |

## oq3_fractional_bpp — OQ-3, fractional bits per pixel

Switch: `--reading frac_reset=chunk|literal`.

* 8.5 bpp (`bits_per_pixel` 136), chunk 102 bytes, `initial_xmit_delay` 33.
  Every range is minQp 0, maxQp 8, and `rc_edge_factor` is 15. The range and
  threshold do not matter; only the buffer fullness does.
* Bits leave the buffer from pixel 33 on (§6.8.1). A chunk would take 96
  pixel times to complete, and only 64 occur, so no padding bits enter.
* Group 29 ends at pixel 90, the 58th pixel from pixel 33.
  **chunk**: floor(8.5 × 58) = 493 bits removed.
  **literal**: the reset after pixel 33 discards its half bit, so
  8 + floor(8.5 × 57) = 492 bits removed.
* Group 29 codes 44 bits, above tgtPlusOffset 29. Buffer fullness after it
  is **63 (chunk)** or **64 (literal)**. The increment branch of Figure 6-12
  requires fullness ≥ 64.
  * chunk: the branch is not taken and prevQp 0 stands. Group 31 decodes at
    **QP 0**.
  * literal: curQp = MAX(0, 0) = 0 equals prev2Qp. The edge test
    36 × 2 < 12 × 15 passes, so the result is MIN(8, 0 + (44 − 26) >> 1) = 8.
    Group 31 decodes at **QP 8**.

| Reading | Group 31 QP | RGB at x = 93, 94, 95 | SHA-256 of expected PPM |
|---|---|---|---|
| chunk | 0 | (183,183,182) (180,179,181) (181,181,181) | `2a396faec9e2ddf3…` |
| literal | 8 | (203,219,171) (179,163,211) (195,195,195) | `b01d1ab1910bc984…` |

## oq4_bp_left — OQ-4, BP samples left of the slice

Switch: `--reading bp_left=replicate|midpoint`.

Built by `tests/make_bp_vectors.py`, not by the rate-control generator. BP
is enabled and every RC range pins QP 0, so no rate-control reading can
matter. The block-prediction search (§6.4.4.1) compares previous-line
samples out to 16 positions left of the group. For the groups at hPos 0–15
some of those positions lie left of the slice, and the text does not say
what they hold.

* 30×2, one slice, gray (Co = Cg = 256). Line 0 is the fixed sequence in the
  generator. It was found by a seeded search that requires the property
  below.
* Line 1 is coded losslessly under the replicate reading. The same bits are
  then decoded under the other reading. The entropy parse does not depend on
  the predictor (§7.5.2.2: only DSU-VLC sizes select MPP), so the syntax is
  identical and only the prediction changes.
* **replicate** (samples left of the slice repeat sample 0): the search at
  hPos 9 picks vector −1, which resets bpCount. bpCount is 1 at hPos 12 and
  2 at 15, and first reaches 3 at hPos 18. The group at hPos 15 uses MMAP.
* **midpoint** (those samples are 128): the search at hPos 9 picks −7, which
  reaches four positions left of the slice. bpCount is 3 at hPos 15, so that
  group uses **BP with vector −6**.
* Both readings give the same decisions under both edge-counter readings
  (OQ-13) and both SAD readings (OQ-10). The generator checks this.

| Reading | Group at hPos 15 | SHA-256 of expected PPM |
|---|---|---|
| replicate | MMAP | `4dd233c60e813001…` |
| midpoint | BP, vector −6 | `cfee1190a838072f…` |

Full hashes, QP schedules, BP decisions and per-group buffer values for
the decisive groups are in `manifest.json`.

## Running against the reference model

`tools/compare_model bitstream tests/discriminators/NAME.pps
tests/discriminators/NAME.bin` has the model decode the input and compares
the result with each expected PPM. Results are recorded in PROGRESS.md only.
