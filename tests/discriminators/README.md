# Discriminator inputs

Each input here is a DSC 1.1 PPS (`NAME.pps`) plus a one-slice payload
(`NAME.bin`), 8 bpc RGB: 96×1 for the rate-control questions, 30×2 for the
block-prediction one, 7×2 for OQ-19; the DSC 1.2 inputs below, and the
native 4:2:2 and 4:2:0 ones (YCbCr), are listed with their sizes. For one
open question in RESEARCH.md, the two readings decode it to different
pixels. Each file below states which reading predicts which output. The
predictions were written and committed before the VESA reference model
decoded any of these inputs (OQ-1 to OQ-3 in Phase 3, OQ-4 in Phase 2,
`oq2b` in Phase 5 of M2; `oq19` in Phase 3 of M3; the DSC 1.2 inputs in
Phase 4 of M3; OQ-37 to OQ-43 in Phase 5 of M3).

`python3 tests/make_discriminators.py` rebuilds every file here except this
README. The generator contains a rate-control model written separately from
the decoder. For every combination of the decoder's reading switches
(OQ-1, OQ-2, OQ-3, OQ-12: 16 combinations; for the BP input, OQ-4, OQ-10,
OQ-13: 8 combinations), it checks three things. The readings of the other
rate-control questions (OQ-5, OQ-11, OQ-14 to OQ-18) are fixed per input and
listed as `assumes` in `manifest.json`: the M1 readings for `oq1` to `oq3`,
the readings the decoder now defaults to for `oq2b`.

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

## oq2_threshold_equality — OQ-2, range threshold equality (superseded)

Switch: `--reading threshold_eq=lower|upper`.

**Superseded by `oq2b_threshold_equality`.** This input was built under the
M1 readings of the other rate-control questions (its `assumes` in
`manifest.json`), and its predictions hold only under them. The decoder
defaults changed in Phase 5, so it cannot decide OQ-2 against the model;
`oq2b` asks the same question under the current defaults. The manifest marks
it `superseded_by`, `tools/compare_model` reports it as SUPERSEDED and leaves
it out of the verdict, and `tests/test_discriminators.py` still decodes it
under the readings it assumes.

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

## oq2b_threshold_equality — OQ-2 under the current pipeline readings

Switch: `--reading threshold_eq=lower|upper`.

The same question as `oq2_threshold_equality`, which it supersedes, built
assuming the readings the decoder defaults to since Phase 5 instead of the M1
ones. Only two of
them matter to a one-line, full-group, scale-8 input without flatness
signals: OQ-11 range-lag and OQ-5 swapped. No group here takes the increment
branch, so only range-lag changes the design. Under range-lag the
short-term RC run after group N uses the range selected after group N − 1,
and range 0 before group 0.

* 8 bpp, `initial_xmit_delay` 128, the same thresholds as `oq2`. Range 0 and
  ranges 6–14 have minQp = maxQp = 0; ranges 1–5 have minQp = maxQp = 8.
  Pinning range 0 to QP 0 makes the lagged first step choose the same QP as
  the others. `initial_offset` is 5721.
* rcModelFullness starts at 5721 − 8192 = −2471 and changes by (coded
  bits − 24) per group. Group 0 codes 15 bits (−2480); groups 1–27 code 12
  bits each (−2804 after group 27, range 6). Group 28 codes 12 bits:
  **−2816, exactly threshold 5**, one group earlier than in `oq2`.
* Group 29 codes 12 bits (−2828, range 5 under both readings). It is below
  target, so the decrement branch gives MAX(prevQp − 1, minQp), with minQp
  from the range selected after group 28. That QP decodes group 31.
* **lower**: the range after group 28 is 5, minQp 8. Group 31 decodes at
  **QP 8**.
* **upper**: the range after group 28 is 6, minQp 0. Group 31 decodes at
  **QP 0**.

| Reading | Group 31 QP | RGB at x = 93, 94, 95 | SHA-256 of expected PPM |
|---|---|---|---|
| lower | 8 | (152,168,120) (128,112,160) (144,144,144) | `522adaa166d082b0…` |
| upper | 0 | (132,132,131) (129,128,130) (130,130,130) | `20657883ee83d170…` |

The expected pictures are identical to `oq2`'s; the PPS and payload are not.

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

## oq19_delay_partial — OQ-19, the initial-delay offset at a partial group

Switch: `--reading delay_partial=pixels|group-end`. Added in M3, Phase 3,
and committed with these predictions before the reference model decoded
it. Built by `oq19_delay_partial()` in `tests/make_discriminators.py`,
under the readings the decoder defaults to for the other questions
(`assumes` in `manifest.json`; the range lag of OQ-11 matters here).

* 7×2, one slice, 16 bpp, gray. Each line has groups at x = 0 and 3 and a
  one-pixel group at x = 6. initial_xmit_delay is 512, so the whole slice is
  inside the initial delay: no bits are removed, and rcXformOffset falls by
  16 bits per delayed pixel (§6.8.2). The scale is 1.0 and every BPG offset
  is 0.
* Line 0 codes zero residuals. Line 1: group 3 codes Y (16, 0, 0), group 4
  zeros, group 5 (the one-pixel group, the last of the slice) Y (3, 0, 0),
  whose third and second residuals are padding.
* Ranges 0 and 8 to 14 pin QP 0; ranges 1 to 7 pin QP 8. The initial offset
  (6519) places rcModelFullness after group 2 at −1776 under **pixels**
  (the one-pixel group counts one pixel), 16 bits above threshold 7
  (−1792): range 8. Under **group-end** (it counts three) it is −1808:
  range 7. Groups 0 and 1 end in range 8 under both.
* With the range lag, the step after group 3 uses the range selected after
  group 2. Group 3 is small against the target of 79 (range offset +31), so
  the decrement branch returns that range's minQp, which group 5 decodes
  at: **QP 0 (pixels)** or **QP 8 (group-end)**. Every other group decodes
  at QP 0 under both readings.
* Group 5's luma prefix parses the same at both QPs (predicted size 0), so
  the syntax is identical; its residual 3 adds 3 at QP 0 and 3 × 2³ = 24 at
  QP 8 (qLevelY 3) to the prediction 128.

| Reading | Group 5 QP | RGB at x = 6, y = 1 | SHA-256 of expected PPM |
|---|---|---|---|
| pixels | 0 | (131,131,131) | `7cca6d77f56acca3…` |
| group-end | 8 | (152,152,152) | `589aba68d29a189c…` |

Every other pixel is the same: gray 128, except (144,144,144) at x = 0 of
line 1.

## DSC 1.2 inputs — OQ-7, OQ-20 to OQ-25

Added in M3, Phase 4, and committed with these predictions before the
reference model decoded them. Each is a DSC 1.2 PPS (dsc_version_minor 2)
and a one-slice payload, built by `tests/make_v12_discriminators.py` with
the Python decoder model `tests/pydsc.py`, which is written separately from
the C decoder. The builder writes the syntax under one reading; the model
then decodes the same bits under every reading of the question, and each
parse must succeed. `tests/test_discriminators.py` checks that dscdecode
reproduces every prediction under every combination of the switches listed
as `vary` in `manifest.json`, with the other questions at the readings
listed as `assumes` (the decoder's defaults; OQ-23 and OQ-24 also assume
`bitsave_ich=not`, DSC 1.2b's reading of OQ-22). Every input ends with a
group whose QP the question decides; the group before it has zero residuals,
so its prefixes parse the same way at either QP. Full QP schedules are in
`manifest.json`.

| Input | Size, bpc | Question: reading → QP of the last group | Last-group pixels |
|---|---|---|---|
| `oq7_bpg_combine` | 30×1, 8 | OQ-7: add → 2, replace → 8 | x = 27–29: (165,196,148) add, (172,203,155) replace |
| `oq20_chroma_qlevel` | 24×1, 16 | OQ-20: QP 3 for both; qLevelC 1 (equal-depth) or 2 (table) | x = 21–23: (32768,32770,32772) equal-depth, (32766,32770,32774) table |
| `oq21_prefix16` | 24×1, 16 | OQ-21: QP 0; thirteen zeros are a size-13 prefix (15) or the MPP escape (13) | x = 21: (35768,…) under 15, (12003,…) under 13 |
| `oq22_bitsave_ich` | 15×2, 8 | OQ-22: not → 9, set → 8 | x = 12, 13 of line 1: (164,168,91), (171,175,82) not; (156,160,83), (163,167,74) set |
| `oq23_bitsave_pred_next` | 18×2, 8 | OQ-23: raw and adjusted → 9, next → 8 | x = 16, 17 of line 1: (171,191,50), (151,183,22) raw/adjusted; (123,143,2), (146,178,17) next |
| `oq24_bitsave_flat` | 18×2, 8 | OQ-24: group → 9, supergroup → 8 | x = 15, 16 of line 1: (164,168,91), (171,175,82) group; (156,160,83), (163,167,74) supergroup |
| `oq25_line_flat` | 3×2, 8 | OQ-25: very → 1, signaled → 0 (line 1's only group) | x = 0 of line 1: (127,128,129) very, (128,128,129) signaled |
| `oq24b_bitsave_flat` | 15×3, 8 | OQ-24: received and carrier → 9, supergroup and group → 8 | x = 12–14 of line 2: (163,182,72), (166,186,60), (142,178,36) received/carrier; (155,174,64), (158,178,52), (142,178,36) supergroup/group |
| `oq24c_bitsave_flat` | 9×3, 8 | OQ-24: received and supergroup → 8, carrier and group → 9 | x = 6–8 of line 2: (152,155,109), (156,135,145), (151,127,133) received/supergroup; (156,159,113), (160,139,149), (147,123,129) carrier/group |
| `oq24d_bitsave_flat` | 15×2, 8 | OQ-24: received, carrier, span → 8; supergroup, group, lagged → 9 | x = 12–14 of line 1: (156,160,83), (163,167,74), (151,167,54) at 8; (164,168,91), (171,175,82), (151,167,54) at 9 |
| `oq24e_bitsave_flat` | 24×2, 8 | OQ-24: supergroup, span → 8; group, received, carrier, lagged → 9 | x = 21–23 of line 1: (159,163,85), (163,167,73), (151,167,53) at 8; (183,187,109), (91,95,1), (147,163,49) at 9 |

How each is built:

* `oq7_bpg_combine`: first line only, 20 bpp, first_line_bpg_offset 15, so
  rcTgtBitsGroup is 75 (add) or 60 (replace). Ranges 6–14 pin QP 0 and
  ranges 0–5 allow 0–8; rcModelFullness falls from group to group, and the
  initial offset (solved by the generator) makes it cross threshold 5 after
  group 6. With the range lag only the step after group 7 uses an unpinned
  range. Group 7 codes 79 bits with more than 192 bits in the buffer: the
  increment branch, from curQp 0, by (79 − 75) >> 1 or MIN(8, (79 − 60) >> 1).
* `oq20_chroma_qlevel`: every range pins QP 3 (qLevelY 1; qLevelC 2 by
  Table 6-3, 1 after §6.8.6's adjustment). The last group codes Co
  residual −1: −4 or −2 after inverse quantization.
* `oq21_prefix16`: every range pins QP 0. The last group's luma prefix is
  thirteen zeros and a one, then three 13-bit residuals (3000, −2000, 1000),
  which is what the 15-bit reading parses; the 13-bit reading takes the
  zeros as MPP and reads three 16-bit residuals from the "1" on.
* `oq22_bitsave_ich`, `oq23_bitsave_pred_next`, `oq24_bitsave_flat`: every
  range, including range 14's maximum, pins QP 8, so DSC 1.2's line-start
  adjustment does not apply (the QP before the line start equals range 14's
  maximum). On line 1, two groups coded MPP in all three units set mppState
  2 and bitSaveMode 2 under DSC 1.2b; the step that sees it returns
  MIN(prevQp + 1, maxQp + 1) = 9. OQ-22's other reading never reaches
  bitSaveMode 2. OQ-23: a group with zero residuals follows; predActivity is
  9 + 5 + 4 = 18 from the MPP sizes as coded (raw), 9 + 4 + 3 = 16 after the
  clamp (adjusted), both at least bitSaveThresh 15, and 9 from its own zero
  residuals (next), which resets bitSaveMode; the zero-residual branch then
  gives CLAMP(9 − 1, 4, 8) = 8. OQ-24: group 7 sends flatness flag 1 and
  group 8 position 3, so the flat group lies beyond the slice; group 9, the
  first of that supergroup, is MPP. Under supergroup the step after it
  resets bitSaveMode; under group it keeps it.
* `oq24b_bitsave_flat`, `oq24c_bitsave_flat`: added after the model
  decoded `oq24_bitsave_flat` (PROGRESS.md, Phase 4) and committed with
  these predictions before the model decoded them. OQ-24 now has four
  readings, `supergroup`, `group`, `received` and `carrier` (RESEARCH.md).
  Same frame as above, with flatness_min_qp = flatness_max_qp = 8, so a
  flag is sendable at every group, and a signaled flat group changes no QP
  because the group before it decodes at QP 8, range 14's maximum (OQ-18).
  Supergroups are groups 1–4, 5–8, 9–12; the flag for one is sent in the
  group ≡ 3 (mod 4) before it and the type and position in the next group.
  Each input ends with two MPP groups, a zero group and the last group; the
  last decodes at 9 unless a flag of 1 covers one of the MPP groups under
  the reading, which resets bitSaveMode.
  `oq24b`: flag 1 in group 7 (supergroup 9–12, flat group 11), flag 0 in
  group 11; MPP groups 11 and 12. received and carrier see group 11's flag
  0 there; supergroup sees 9–12's flag 1; group sees flat group 11.
  `oq24c`: flag 1 in group 3 (supergroup 5–8, flat group 8); MPP groups 5
  and 6. received (groups 3–6 see group 3's flag) and supergroup (5–8)
  cover them; carrier covers only groups 3 and 4, group only group 8.
  Together the two inputs give each reading a different pair of outcomes.
  One more variant also fitted `oq24_bitsave_flat`: a group counts as
  flagged under received or supergroup. It is not a switch value; checked
  with a scratch build before the model decoded these inputs, it gives the
  supergroup prediction on `oq24b` and the received prediction on `oq24c`.
  `oq24_bitsave_flat` is marked superseded by `oq24b_bitsave_flat`: it has
  predictions only for the two readings it was built for.
* `oq24d_bitsave_flat`, `oq24e_bitsave_flat`: added after the model
  decoded `oq24b` and `oq24c` (PROGRESS.md, Phase 4) and committed with
  these predictions before the model decoded them. On the three OQ-24
  inputs before them the model's outputs fit two further readings, now
  switch values: `span` (from the group carrying the flag to the last group
  of its supergroup: groups 7–12 for the flag sent in group 7) and `lagged`
  (the flag as it was before the group: groups 8–11). Two other windows
  fit as well, 7–11 and 8–12; they are not switch values. The two inputs
  test the ends of the window, each with an MPP pair whose other group no
  reading covers, so the last group decodes at 8 if the tested group is
  covered and at 9 if not.
  `oq24d`: flag 0 in group 3, flag 1 in group 7 (flat group 12, beyond the
  slice); MPP groups 6 and 7. Is the flag's own group covered?
  `oq24e`: flag 1 in group 7 (flat group 9), flag 0 in group 11; MPP
  groups 12 and 13. Is the supergroup's last group covered?
  Outcomes (QP of the last group, `oq24d` / `oq24e`), for the windows that
  fit the earlier inputs: span 7–12 → 8 / 8; lagged 8–11 → 9 / 9; window
  7–11 → 8 / 9; window 8–12 → 9 / 8. The last two were computed with a
  scratch build before the model decoded these inputs.
* `oq25_line_flat`: one group per line. Line 1's group follows a group
  decoded at QP 0. DSC 1.2 adjusts the first group of every non-first line
  as very flat: veryFlatQp 1 under very; under signaled, the QP before
  (0) is below somewhatFlatQpThresh 7, which demotes it to somewhat flat,
  MAX(0 − 4, 0) = 0. Its Co residual −1 becomes −2 or −1.

## DSC 1.2 inputs — OQ-26 to OQ-36

Added in M3, Phase 4, part 4, and committed with these predictions before
the reference model decoded them. Each question is a place where the
model-encoded DSC 1.2 streams of Phase 4 departed from the printed text
(OQ-34: no evidence yet), recorded with the text as reading A (RESEARCH.md).
Built by `tests/make_v12_discriminators.py` with `tests/pydsc.py`, which now
implements every reading. Each input decides one QP (OQ-33, OQ-35 and
OQ-36: one parse) near the end of the slice; `manifest.json` lists the
switches each input is decoded under in all combinations (`vary`) and the
readings it relies on (`assumes`: the decoder's defaults, including the
model's readings of OQ-1, OQ-22 to OQ-25 and the observed readings of
OQ-27 and OQ-29 where the input needs them). The generator's docstrings
give the arithmetic of each input.

| Input | Size, bpc | Question: the decisive QP | Pixels |
|---|---|---|---|
| `oq26_low_min` | 45×1, 8 | low_min: last group QP max-qp → 6, min-qp → 5 | first difference x = 42, y = 0: (132, 213, 0) max-qp; (136, 213, 0) min-qp; 3 pixels differ |
| `oq27_decrement_test` | 48×1, 8 | decrement_test: last group QP both → 8, size → 7 | first difference x = 45, y = 0: (139, 166, 85) both; (147, 166, 77) size; 3 pixels differ |
| `oq28_activity_qp` | 24×2, 8 | activity_qp: last group QP prev → 12, prev2 → 9 | first difference x = 21, y = 1: (177, 197, 134) prev; (177, 181, 102) prev2; 3 pixels differ |
| `oq29_bitsave_step` | 15×2, 8 | bitsave_step: last group QP 1 → 5, 2 → 6 | first difference x = 12, y = 1: (137, 139, 118) 1; (141, 139, 114) 2; 3 pixels differ |
| `oq30_target_floor` | 54×1, 8 | target_floor: last group QP none → 15, zero → 12 | first difference x = 51, y = 0: (130, 130, 129) none; (161, 164, 154) zero; 3 pixels differ |
| `oq31_flat_rerun` | 48×1, 8 | flat_rerun: last group QP changed → 4, every → 0 | first difference x = 45, y = 0: (142, 148, 114) changed; (138, 147, 117) every; 3 pixels differ |
| `oq32_rerun_bitsave` | 9×4, 8 | rerun_bitsave: last group QP keep → 8, redo → 6 | first difference x = 6, y = 3: (185, 212, 60) keep; (177, 220, 52) redo; 3 pixels differ |
| `oq33_mux16` | 24×1, 16 | mux16: last group QP 68 → 2, 64 → 2 | first difference x = 21, y = 0: (0, 37934, 24106) 68; (0, 38768, 24768) 64; 3 pixels differ |
| `oq34_flat_top` | 33×2, 8 | flat_top: last group QP equal → 9, at-or-above → 13 | first difference x = 31, y = 1: (83, 85, 76) equal; (163, 165, 156) at-or-above; 2 pixels differ |
| `oq35_prefix16_scope` | 24×1, 16 | prefix16_scope: last group QP qp0 → 3, qlevel → 3 | first difference x = 21, y = 0: (58306, 58306, 58306) qp0; (12768, 12768, 12768) qlevel; 3 pixels differ |
| `oq36_prefix16_cut` | 27×1, 16 | prefix16_cut: last group QP always → 4, longer → 4 | first difference x = 21, y = 0: (33792, 33970, 33436) always; (32948, 33126, 32592) longer; 6 pixels differ |

In short:

* `oq26_low_min`: a zero-residual group at QP 6 with ranges 6–10:
  prevQp − 1 = 5 is kept above MAX(10 − 4, 0) = 6 only under max-qp.
* `oq27_decrement_test`: a group coded at the predicted size 4 with
  size-1 residuals: codedGroupSize 39, rcSizeGroup 12, tgtMinusOffset 33.
* `oq28_activity_qp`: in bitSaveMode 2 the QP climbs by 2 a step; a non-MPP
  group with predicted sizes 3 and 2 has predActivity 15 with prevQp 10
  (kept) and 13 with prev2Qp 8 (reset).
* `oq29_bitsave_step`: bitSaveMode 2 from QP 4 with adjustedMaxQp 13.
* `oq30_target_floor`: every range offset −32 at 8 bpp: target −8.
* `oq31_flat_rerun`: a signaled somewhat-flat group already at QP 0 after a
  group at QP 1: re-running the step with prev2Qp 0 turns an increment
  into no change.
* `oq32_rerun_bitsave`: a line start re-runs the previous step; recomputing
  bitSaveMode with prev2Qp 1 resets it, and the MPP line-start group then
  does or does not bring bitSaveMode 2 back.
* `oq33_mux16`: 16 bpc; the luma funnel holds 64 to 67 bits when chroma
  words are due, so the two thresholds order the mux words differently.
* `oq34_flat_top`: bitSaveMode raises the QP to 13, above range 14's
  maximum 12, before a signaled somewhat-flat group.
* `oq35_prefix16_scope`: 16 bpc at QP 3: fifteen zeros are MPP (qlevel) or
  the start of a sixteen-bit prefix (qp0).
* `oq36_prefix16_cut`: 16 bpc at QP 4 with predicted sizes of 2 or more: an
  ICH group and a continued ICH group, which the always reading reads as
  P-mode groups.

## DSC 1.2 native modes and the scale decrement — OQ-37 to OQ-43

Added in M3, Phase 5, part 1, and committed with these predictions before
the reference model decoded them. OQ-37 to OQ-39 are readings of the text;
OQ-40 to OQ-43 are places where model-encoded streams of Phase 5 decoded
bit-exact under one reading only (RESEARCH.md). The native inputs are
YCbCr: their pictures are raw YCbCr (`NAME.READING.expected.yuv`, the
layout dscdecode writes for a `.yuv` output and the model for its `.yuv`
files: planar 4:2:0 for native 4:2:0, UYVY for native 4:2:2), and
`manifest.json` gives their `output` and `format`. Native 4:2:0 and 4:2:2
inputs are built by `tests/make_native_discriminators.py`, the RGB ones by
`tests/make_v12_discriminators.py`, both with `tests/pydsc.py`, which now
models YCbCr, the native containers and block prediction (checked against
the first slices of the model-encoded native streams of Phase 5). OQ-42 and
OQ-43 have an input in each DSC version; the two inputs of a pair decode to
the same pictures. The generators' docstrings give the arithmetic.

| Input | Size, bpc, format | Question: the decisive QP or decision | First difference between the predictions |
|---|---|---|---|
| `oq37_activity420` | 48×2, 8, native 4:2:0 | activity420: last group QP luma → 12, sum → 9 | Y at x = 42, y = 1: 176 luma, 160 sum; 3 samples differ |
| `oq38_activity422` | 48×2, 8, native 4:2:2 | activity422: last group QP sizes → 12, total → 9 | Cb at x = 42, y = 1: 97 sizes, 129 total; 4 samples differ |
| `oq39_bp420_edge` | 36×4, 8, native 4:2:0, BP | bp420_edge: BP at line 2, hPos 15 (container) luma → no, all → yes | Y at x = 30, y = 2: 108 luma, 109 all; 10 samples differ |
| `oq40_offset_adj` | 12×2, 8, native 4:2:0 | offset_adj: last group QP subtract → 0, start → 4 | Y at x = 7, y = 1: 128 subtract, 127 start; 2 samples differ |
| `oq41_ich_window` | 12×2, 8, native 4:2:2 | ich_window: ICH entries 25, 27, 29 at the left edge start at luma sample 0 (pixels) or 1 (container) | Cb at x = 0, y = 1: 120 pixels, 126 container; 20 samples differ |
| `oq41b_ich_window` | 24×4, 8, native 4:2:0 | ich_window: ICH entries 29-31 at the right edge start at luma sample 20 (pixels) or 19 (container) | Y at x = 18, y = 2: 109 pixels, 152 container; 13 samples differ |
| `oq42_scale_first` | 30×1, 8, RGB, DSC 1.2 | scale_first: last group QP group → 4, not → 0 | R at x = 27, y = 0: 134 group, 130 not; 9 samples differ |
| `oq42b_scale_first` | 30×1, 8, RGB, DSC 1.1 | as `oq42_scale_first` | as `oq42_scale_first` |
| `oq43_scale_line` | 12×2, 8, RGB, DSC 1.2 | scale_line: last group QP until-unity → 4, first → 0 | R at x = 9, y = 1: 135 until-unity, 131 first; 5 samples differ |
| `oq43b_scale_line` | 12×2, 8, RGB, DSC 1.1 | as `oq43_scale_line` | as `oq43_scale_line` |

SHA-256 prefixes of the predictions (full values in `manifest.json`):
`oq37_activity420` luma `53858a3b9812be54`, sum `92d6b58eb2aef330`;
`oq38_activity422` sizes `632322550202dcd7`, total `2ad8ee71bc6e2891`;
`oq39_bp420_edge` luma `21e5ed0eebc6db70`, all `b7582bd3b8562506`;
`oq40_offset_adj` subtract `bc657342f915901d`, start `1ea4af8fca969eb4`;
`oq41_ich_window` pixels `56aca5b99f71628b`, container `3cba5798821a7ffe`;
`oq41b_ich_window` pixels `f70d9681bbb5dab5`, container `34da1d252030b430`;
`oq42_scale_first` and `oq42b_scale_first` group `1072edbb3b3fffd0`, not
`c12267f3d76c57f7`; `oq43_scale_line` and `oq43b_scale_line` until-unity
`aae11c1af4c7a591`, first `2ab2365669a9aaa6`.

In short:

* `oq37_activity420`, `oq38_activity422`: the frame of `oq28_activity_qp`
  in the native containers. MPP groups keep bitSaveMode 2 and raise the QP
  by 2 a step; a P-mode group at QP 8 has predicted sizes (3, 1, 3) in
  4:2:0 (8 + MAX(3, 1) + 3 = 14 against 8 + MAX(3, 1 + 3) = 12) and
  (3, 3, 3, 3) in 4:2:2 (8 + (12 >> 1) = 14 against (8 + 12) >> 1 = 10),
  with bitSaveThresh 14.
* `oq39_bp420_edge`: line 1's luma repeats every three container pixels
  without a step above 32, so bpVector -3 wins from hPos 9 and bpCount
  reaches 3 at hPos 15; the only edge is a Cb step of 60 two lines up.
* `oq40_offset_adj`: second_line_offset_adj 512 at rcXformScale 1 moves
  rcModelFullness after group 0 across threshold 0 only under start.
* `oq41_ich_window`, `oq41b_ich_window`: previous-line ICH entries at the
  first group of a line and at the last group of a line.
* `oq42_scale_first`, `oq43_scale_line`: initial_scale_value 16 decremented
  every group; the readings leave the scale one apart after group 6 (one
  line of ten groups) or after group 4 (the first group of the second line
  of a 12-pixel slice, OQ-42 read as not).

## Running against the reference model

`tools/compare_model bitstream tests/discriminators/NAME.pps
tests/discriminators/NAME.bin` has the model decode the input and compares
the result with each expected PPM (or raw YCbCr, for the native inputs).
`tools/compare_model discriminators` does
this for every input. It fails if the model matches both predictions, or
neither; an input whose `assumes` differ from the decoder's current defaults
that matches neither is reported as inconclusive instead, since its
predictions were conditional on those readings. An input marked
`superseded_by` in the manifest (`oq2_threshold_equality`) is decoded and
printed for the record, reported as SUPERSEDED, and left out of the verdict.
Results are recorded in PROGRESS.md only.
