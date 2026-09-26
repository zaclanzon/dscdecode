# DSC software decoder research

Research date: 2026-09-16 (UTC). Phase 0 written before implementation.

> **M2 trim, 2026-09-23.** This is the M1 research record with these parts
> removed: the numerical audit appendix, the Annex E Table E-5 values, the PPS
> byte table, the model archive record with its file and function map and
> copyright quotation, the table of model-derived resolutions, and every link
> to an unofficial mirror. Questions that the removed parts answered from model
> source are listed as open below. The original is kept outside the repository.

## Open questions

Maintained from M2 onward. "Open" means the specification text supports more
than one reading, or the only M1 answer came from model source that this
repository no longer relies on. Where the decoder implements one reading of a
question with two readings, the switch that selects it is named in the table.

Switches are runtime options: `struct dsc_options` in `include/dsc.h`, or
`dscdecode --reading NAME=VALUE`. `dsc_options_init()` sets the defaults.
`dscdecode --stats` counts the groups where a question's readings could
disagree.

M2 correction, not an open question: §6.8.5.2 restarts the short-term RC
only when the flatness override changes masterQp. M1 also restarted
it when a flagged group was already at QP 0, which a somewhat-flat signal
leaves at 0. The decoder now restarts only on a change (`tests/test_rc.c`,
`flat_unmodified`).

M2 source note: the DSC 1.1 E1 errata PDF contains model source excerpts
after the prose of the fractional-bpp SCR. During M2 some of that excerpt was
displayed while reading the SCR's prose. It was not used for any decision
here. Only the SCR's prose change to §6.8.1 was used.

Phase 5 (2026-09-23) ran the VESA C model as a black box (its command line
and output files only; see `PROGRESS.md`). A question marked resolved had one
reading that reproduced the model's own decode bit-exactly on every model
stream and discriminator that exercises it, while the other reading diverged.
The default switch value is that reading, and the other reading stays
available behind the same switch. "Stream set" means the 92 streams the
model decoded in Phase 5 (`PROGRESS.md`): 82 pictures the model itself
encoded (the 42-case matrix, 30 flatness pictures, 7 validation pictures, 3
partial-group ramps) and the 10 exact-image fixtures. Counts per question are
in `PROGRESS.md`.

The column "Hypothesis source" says where the readings came from, and in
particular the one that became the default. "Text": the default, or both
readings, were derived from the specification text before any model output
was seen, so the model's output tested a hypothesis fixed in advance; where
a discriminator was committed with its predictions first, its verdict is an
independent prediction. "Model output": the default was found by fitting
the model's output after the earlier reading failed on model streams,
either by M2's per-group QP recovery (a scratch build forces each group's QP
and keeps the one that reproduces the model's pixels) or by M3's debug
builds and pinned-RC encodes (PROGRESS.md, M3 Phase 4 part 4). For those
questions a discriminator confirms a fitted rule on an input built after
the fit. It shows that the rule holds beyond the streams it was fitted to
and that the other reading fails there, but it is not an independent
prediction: the rule was chosen because it reproduced the model. The M2
questions of this kind have no discriminator; the later model comparisons
(PROGRESS.md) test their rules on streams they were not fitted to, in the
same limited sense. Where both apply, the cell says which came first. The DSC 1.2 questions are compared
with the text in "DSC 1.2 text and the reference model" below.

| ID | Question | Spec reference | Current implementation | Status | Hypothesis source |
|---|---|---|---|---|---|
| OQ-1 | Flatness restart ordering. When a flatness override changes masterQp for group k, §6.8.5.2 says the new value seeds the short-term RC of the next RC cycle, without saying which cycle of the Figure 6-8 pipeline is next. Reading A (next-cycle): the cycle run after group k is decoded (it sets group k+2's QP) starts from the override; the QP already queued for group k+1 stands. Reading B (in-flight): with the Figure 6-8 pipeline, the cycle that sets group k+1's QP runs while group k decodes, so it is re-run from the override. | DSC 1.1 §6.8.5.2, Figure 6-8, §7.3 | `flat_restart=in-flight` (default) or `next-cycle` (M1 behavior) | Resolved by black-box comparison with the VESA C model: in-flight. Discriminator `oq1_flat_restart`; also the Phase 5 stream set. | Text: both readings, and the discriminator with its predictions, before any model output (M2). |
| OQ-2 | Range threshold equality. When rcModelFullness equals a threshold exactly, is it in the lower or the upper range? Figure 6-11 draws the boundaries without saying which side owns them. | DSC 1.1 §6.8.3, Figure 6-11 | `threshold_eq=lower` (default, M1 behavior) or `upper` | Resolved by black-box comparison with the VESA C model: lower. Discriminator `oq2b_threshold_equality`, built under the current readings of the other rate-control questions; also the Phase 5 stream set. The earlier discriminator `oq2_threshold_equality` is superseded by `oq2b_threshold_equality`: it was built with the M1 readings of OQ-5, OQ-11 and OQ-14 to OQ-18, which the model does not follow, so it cannot decide the question. It remains a decoder-side test under the readings it assumes. | Text: both readings before any model output (M2). |
| OQ-3 | Fractional bits_per_pixel. The printed §6.8.1 pseudocode resets the fractional accumulator when (pixelCount − initial_xmit_delay) is a multiple of slice_width, which includes the first pixel after the delay. Reading A (chunk): reset when a chunk of slice_width pixel times completes, as the framer's chunk accounting and §6.8.1's remark that the accumulator restarts for every slice line suggest. Reading B (literal): reset exactly where the pseudocode prints it. They differ by at most one bit of buffer fullness. The E1 fractional-bpp SCR changes only the encoder's forceMpp condition. | DSC 1.1 §6.8.1; DSC 1.1 E1, fractional-bpp underflow SCR | `frac_reset=chunk` (default, M1 behavior) or `literal` | Resolved by black-box comparison with the VESA C model: chunk. Discriminator `oq3_fractional_bpp`, and the fractional-rate model streams (7.5 and 9.3125 bpp) of publication step P2 in `PROGRESS.md`. The Phase 5 stream set uses whole-number bpp and cannot separate the readings. | Text: both readings before any model output (M2). |
| OQ-4 | Block prediction references left of the slice. The §6.4.4.1 search compares previous-line samples out to hPos−16, and the searches at hPos 0–15 reach positions left of the slice, which the text does not define. They matter: bpCount increments from hPos 9, so those positions can decide the first BP groups (hPos 15–21). Reading A (replicate): such samples repeat the slice's first previous-line sample, as §6.4.1 prescribes for the MMAP filter on the previous line. Reading B (midpoint): they take the component midpoint, as MMAP and MPP do for the first group of a line. The same convention supplies the left neighbor of sample 0 in the edge test. | DSC 1.1 §6.4.2, §6.4.4.1, §7.5.2.1; §6.4.1 for the replication precedent | `bp_left=midpoint` (default) or `replicate`. Worked examples in research/bp-worked-note.md | Resolved by black-box comparison with the VESA C model: midpoint. Discriminator `oq4_bp_left`; also the Phase 5 stream set. | Text: both readings before any model output (M1, M2). |
| OQ-5 | QP increment comparison direction. Figure 6-13 prints the rc_quant_incr_limit0 branch for curQp below prev2Qp. Reading A (printed): as printed. Reading B (swapped): the limit0 branch applies when curQp is above prev2Qp, the ordering M1 recorded from model source. | DSC 1.1 §6.8.4, Figure 6-13 | `incr_order=swapped` (default) or `printed` | Resolved by black-box comparison with the VESA C model: swapped. No discriminator; evidence: the Phase 5 stream set. Unit test `increment_order`. | Model output (M2 per-group QP recovery). The printed reading is the text and was M1's default; reading B was first recorded in M1 from model source (THIRD_PARTY.md) and was not applied until M2 fitted it to model output. |
| OQ-6 | Decrement floor after a group with zero residuals | DSC 1.1 §6.8.4, Figure 6-12 | Floor is half of minQp; the 1.1 figure supports this reading. No switch | Resolved by black-box comparison with the VESA C model: half of minQp. No discriminator; evidence: the Phase 5 stream set, decoded by a scratch build (outside the repository) that floors at minQp. | Text: the default is the DSC 1.1 figure; a scratch build tested the alternative on model output (M2). |
| OQ-7 | How the first-line and second-line terms of rcXformBpgOffset combine. In DSC 1.2 and DSC 1.2a §6.8.4 the pseudocode first sets the first-line or non-first-line term and then plainly assigns the second-line term (second_line_bpg_offset, or minus floor(nsl_bpg_offset)) to rcXformBpgOffset, which discards the first term; DSC 1.2b adds it instead (the DSC 1.2a E1 SCR on rcXformBpgOffset). Reading A (replace): the second-line terms replace the first-line ones, so outside native 4:2:0 (both second-line terms zero) first_line_bpg_offset and nfl_bpg_offset leave the target. Reading B (add): they are added. | DSC 1.2b §6.8.4; DSC 1.2a §6.8.4 and its E1 errata | `bpg_combine=add` (default, DSC 1.2b) or `replace` | Resolved by black-box comparison with the VESA C model: add. Discriminator `oq7_bpg_combine`, committed with its predictions (60ae6cc) before the model decoded it; the model's output matches the add prediction and not the replace one. | Text: DSC 1.2a against DSC 1.2b, before any model output (M3). |
| OQ-8 | Line storage saturation, present in §6.3 and absent from §7.4 | DSC 1.1 §6.3, §7.4, §7.5 | Saturate as in §6.3. No switch; prose argument in research/prediction-ambiguities.md | Resolved by black-box comparison with the VESA C model: saturate. It matters only when line_buf_depth is below 9. Evidence: Phase 5 pictures encoded with line_buf_depth 8, decoded by a scratch build (outside the repository) without the clamp. | Text: §6.3 against §7.4, before any model output (M1). |
| OQ-9 | forceMpp chunk-bit comparison scaling | DSC 1.1 §6.8.1 and E1 | Encoder-side decision; the decoder does not compute forceMpp | Open; no decoder effect, so decoding cannot observe it | Text (encoder only; never tested). |
| OQ-10 | BP SAD reduction. The 1.1 prose says three 9-bit partial SADs are summed and the three LSBs dropped; 1.1's printed formula instead clips the sum to 511 and drops nothing. DSC 1.2b §6.4.4.1 prints the corrected equation (sum, then shift by 3), and its revision history lists it as a correction to match the C model. | DSC 1.1 §6.4.4.1; DSC 1.2b §6.4.4.1 and revision history | `bp_sad=shift` (default, 1.1 prose and 1.2b) or `clip` (1.1 formula) | Resolved by black-box comparison with the VESA C model: shift. No discriminator; evidence: the block-prediction streams of the Phase 5 stream set. | Text: DSC 1.1 prose and formula, DSC 1.2b and its revision history (M1). |
| OQ-11 | RC pipeline. Group N's coded size sets the QP used for group N+2, and the first two groups decode at QP 0. Reading A (same-group): the short-term RC for that cycle uses the range (minQp, maxQp, bpg offset) selected from the fullness after group N. Reading B (range-lag): the range is the one selected in the previous cycle, and the first cycle uses range 0. The figure shows the latency but not when range selection happens within it. | DSC 1.1 Figure 6-8, §6.8.3, §7.3 | `rc_pipeline=range-lag` (default) or `same-group` (M1 behavior) | Resolved by black-box comparison with the VESA C model: range-lag. No discriminator; evidence: the Phase 5 stream set. Unit test `range_pipeline`. | Model output (M2 per-group QP recovery). The same-group reading is the text as M1 implemented it and came first. |
| OQ-12 | Initial-delay boundary. §6.8.1 starts removing bits at the pixel where pixelCount equals initial_xmit_delay. §6.8.2 lowers rcXformOffset while the initial delay lasts, without a pixel-exact end. Reading A (inclusive): the offset falls for initial_xmit_delay pixels, pixelCount 1 to initial_xmit_delay, so one pixel both removes bits and lowers the offset. Reading B (exclusive): only the initial_xmit_delay − 1 pixels that remove no bits count. Annex E's `initial_xmit_delay × bits_per_pixel` budgets fit reading A but are not exact enough to decide. Found in M2 while designing the OQ-3 discriminator. | DSC 1.1 §6.8.1, §6.8.2, Annex E | `delay_offset=inclusive` (default, M1 behavior) or `exclusive` | Resolved by black-box comparison with the VESA C model: inclusive. No discriminator; evidence: the Phase 5 stream set. | Text: both readings before any model output (M2, while designing the OQ-3 input). |
| OQ-13 | BP edge counter. BP needs lastEdgeCount below 3, the pixels passed since a step above 32 in any component. The decision uses only previous-line information, but the text does not say at which previous-line sample the count is read. Reading A (window): at hPos+2, the last sample of the search window and the current block in Annex D.2's description. Reading B (before): at hPos−1, the sample left of the group. Also implemented as M2 readings: bpCount starts each line at 0, since its hPos-below-9 rule is only meaningful per line; the search runs for every group, including ICH groups. | DSC 1.1 §6.4.4.1, Annex D.2 | `bp_edge=window` (default) or `before` | Resolved by black-box comparison with the VESA C model: window. No discriminator; evidence: the block-prediction streams of the Phase 5 stream set. The other M2 readings in this row have no switch; the defaults match those streams. | Text: both readings before any model output (M2 block prediction). |
| OQ-14 | Scale decrement start. When initial_scale_value is above 8, §6.8.2 lowers the scale once every scale_decrement_interval groups. Reading A (from-group-1): the interval is counted from the second group of the slice. Reading B (from-group-0): it is counted from the first group. | DSC 1.1 §6.8.2 | `scale_dec=from-group-0` (default) or `from-group-1` (M1 behavior) | Resolved by black-box comparison with the VESA C model: from-group-0. No discriminator; evidence: the Phase 5 stream set. Unit test `scale_decrement`. | Model output (M2 per-group QP recovery). The from-group-1 reading (M1) came first. |
| OQ-15 | RC target for a partial group. The last group of a slice line can hold one or two pixels. Reading A (three): rcTgtBitsGroup is computed from 3 × bits_per_pixel for every group. Reading B (pixels): from the pixels actually in the group, matching the bits §6.8.1 removes for them. | DSC 1.1 §6.8.1, §6.8.4 | `partial_target=pixels` (default) or `three` (M1 behavior) | Resolved by black-box comparison with the VESA C model: pixels. No discriminator; evidence: the Phase 5 stream set. Unit test `partial_target`. | Model output (M2 per-group QP recovery). The three reading (M1, §6.8.4 as printed) came first. |
| OQ-16 | Very-flat demotion. §6.8.5.2 notes that a flatness signal counts as somewhat flat when the current masterQp is below 7 (8 bpc), without saying which group's QP is current. The type bit is already omitted when the signaling group's QP is below 7. Reading A (group-qp): demote when the flagged group's own short-term QP is below 7. Reading B (as-signaled): the note restates the omitted bit, and the decoded type applies unchanged. Reading C (previous-qp): demote when the QP that decoded the group before the flagged one is below 7. Found in Phase 5, when multi-slice model streams failed to decode. | DSC 1.1 §6.8.5.2; §4.5 (Table 4-9) and §6.6.3 for the type bit | `very_flat=previous-qp` (default), `group-qp` (M1 behavior) or `as-signaled` | Resolved by black-box comparison with the VESA C model: previous-qp. No discriminator; evidence: the Phase 5 stream set, including pictures built for this question (flat patches in noise). Unit test `very_flat_type`. | Model output (M2 per-group QP recovery on failing multi-slice streams). group-qp (M1) came first; as-signaled and previous-qp were added in M2, previous-qp to fit the model. |
| OQ-17 | Partial-group padding. §6.6 has encoders clear a partial group's padding residuals and repeat its rightmost real ICH index, but does not say what a decoder does with other padding. Reading A (reject): other padding is a bitstream error. Reading B (accept): the padding is parsed, feeds size prediction, and is otherwise ignored. | DSC 1.1 §6.6, §7.8 | `partial_padding=accept` (default) or `reject` (M1 behavior). Under `accept`, the CLI prints one warning with the number of partial groups whose padding it accepted | Resolved by black-box comparison with the VESA C model: accept. Evidence: the model decodes fixtures `invalid_partial_residual` and `invalid_partial_ich` without error, and its output matches the accept reading. No discriminator. | Model output (M2): reject is the text (§6.6) and came first; accept was added when the model decoded the invalid-padding fixtures without error. |
| OQ-18 | Flatness at the top QP. §6.8.5.2 makes no flatness adjustment when the current masterQp equals range 14's range_max_qp, with the same unspecified current QP as OQ-16. Reading A (own): the flagged group's short-term QP. Reading B (previous): the QP that decoded the group before the flagged one. Found in Phase 5, when two multi-slice model streams failed to decode. | DSC 1.1 §6.8.5.2 | `flat_max_qp=previous` (default) or `own` (M1 behavior) | Resolved by black-box comparison with the VESA C model: previous. No discriminator; evidence: the Phase 5 stream set. Unit test `flat_max_qp`. | Model output (M2 per-group QP recovery on failing multi-slice streams). own (M1) came first. |
| OQ-19 | Initial-delay offset at a partial group. §6.8.2 lowers rcXformOffset by three times bits_per_pixel for each group while the initial delay lasts; §6.8.1 counts the pixels actually in each group, and the last group of a line can hold one or two pixels. Reading A (pixels): a group lowers the offset by bits_per_pixel for each of its real pixels inside the delay. Reading B (group-end): each group counts from the end of the previous group to its own end as if it had three pixels, starting from its real position; a one-pixel group at the end of a line counts three pixels and the first group of the next line only one, so the total over the delay is unchanged but the offset between the two groups is two pixels' worth lower. Found in M3 Phase 3, when model-encoded streams with one-pixel partial groups inside the initial delay failed to decode at 8, 10 and 12 bpc (4 slices of a 640-pixel line, 6 to 12 bpp); the v0.1.0 comparisons had not met the case. | DSC 1.1 §6.8.1, §6.8.2 | `delay_partial=group-end` (default) or `pixels` (M1 and M2 behavior). Unit test `delay_partial` | Resolved by black-box comparison with the VESA C model: group-end. Discriminator `oq19_delay_partial` was committed with both predictions (4f164ba) before the model decoded it; the model's output matches the group-end prediction and not the pixels one. The M3 Phase 3 stream set (PROGRESS.md) also decodes bit-exact only under group-end. | Model output (M3 Phase 3 debug build: forced QPs and offset changes on failing streams). pixels (M1, M2) came first; group-end was fitted. |
| OQ-20 | qLevelC at 16 bpc RGB. DSC 1.2b §6.8.6 gives two chroma rules: Table 6-3 applies to RGB input (and to every DSC 1.1 stream), and for DSC 1.2 streams whose luma and chroma have the same bit depth qLevelC is one less than the table value. At 16 bpc RGB both conditions hold, because §6.1 limits YCoCg-R chroma to 16 bits. Reading A (table): Table 6-3 as printed. Reading B (equal-depth): qLevelC − 1. Under A, QP 30 and 31 give qLevelC 16, a zero-bit MPP residual for 16-bit chroma, so B is judged more likely. | DSC 1.2b §6.1, §6.8.6, Table 6-3 | `chroma_qlevel=equal-depth` (default) or `table` | Resolved by black-box comparison with the VESA C model: equal-depth. Discriminator `oq20_chroma_qlevel`, committed with its predictions (60ae6cc) before the model decoded it; the model's output matches the equal-depth prediction and not the table one. | Text: both readings before any model output (M3). |
| OQ-21 | The 16 bpc luma prefix limit at QP 0. DSC 1.2b Table 4-10 limits prefix_Y at 16 bpc and QP 0 to 15 bits, all zeros meaning MPP; DSC 1.2b §3.10.2 and DSC 1.2a Table 4-10 say 13. Reading A (15). Reading B (13). | DSC 1.2b Table 4-10, §3.10.2; DSC 1.2a Table 4-10 | `prefix16=13` (default, since M3 Phase 4 part 2) or `15` (DSC 1.2b Table 4-10) | Resolved by black-box comparison with the VESA C model: 13. Discriminator `oq21_prefix16`, committed with its predictions (60ae6cc) before the model decoded it; the model's output matches the 13 prediction and not the 15 one. The model follows DSC 1.2b §3.10.2 and DSC 1.2a Table 4-10 here, not DSC 1.2b Table 4-10. Tested with a predicted size of 0 at QP 0; which groups have a cut prefix, and when its rules apply, are OQ-35 and OQ-36. | Text: two passages of DSC 1.2b and DSC 1.2a, before any model output (M3). |
| OQ-22 | bitSaveMode and ichSelected. DSC 1.2b §6.8.4 takes the MPP branch (mppState, bitSaveMode 2) and the keep branch only when the group is not coded in ICH mode; DSC 1.2a tests the opposite condition, which makes bitSaveMode 2 unreachable (an ICH group has no MPP units). The DSC 1.2a E1 SCR on bitSaveMode changes it to match the C model. Reading A (not, DSC 1.2b). Reading B (set, DSC 1.2a as printed). | DSC 1.2b §6.8.4; DSC 1.2a §6.8.4 and its E1 errata | `bitsave_ich=not` (default) or `set` | Resolved by black-box comparison with the VESA C model: not. Discriminator `oq22_bitsave_ich`, committed with its predictions (60ae6cc) before the model decoded it; the model's output matches the not prediction (DSC 1.2b) and not the set one. | Text: DSC 1.2a against DSC 1.2b, before any model output (M3). |
| OQ-23 | Which predictedSize values predActivity uses. DSC 1.2b §6.8.4 computes predActivity from predictedSize[0..3], which Tables 6-2 and 7-1 list among what the entropy decoder hands on after each group. §6.6.1 defines predictedSize (from the previous unit's residual sizes) and adjPredictedSize (after the qLevel change and a clamp to maxSize − 1). Reading A (raw): the predictedSize the group was coded with. Reading B (adjusted): its adjPredictedSize. Reading C (next): the predictedSize computed from the group's own residuals, for the next group. | DSC 1.2b §6.8.4, §6.6.1, Tables 6-2 and 7-1 | `bitsave_pred=next` (default, since M3 Phase 4 part 2) or `raw` or `adjusted` | Resolved by black-box comparison with the VESA C model: next. Discriminator `oq23_bitsave_pred_next`, committed with its predictions (60ae6cc) before the model decoded it; the model's output matches the next prediction; raw and adjusted shared one prediction there, and the model's output differs from it, so both are excluded without an input that separates them from each other (PROGRESS.md, Phase 4). | Text: all three readings before any model output (M3). |
| OQ-24 | Which groups the bitSaveMode update of DSC 1.2b §6.8.4 treats as having flatness signaled for their supergroup. §6.6.3 sends the flag for a supergroup (groups 1–4, 5–8, … of the slice) in the group two before it starts, and the type and position in the next group; below, the flag sent in group 7 is for supergroup 9–12. Readings, as the groups a flag of 1 covers: A (supergroup) 9–12; B (group) only the flat group the position points at; C (received) the flag received last, 7–10; D (carrier) the groups carrying the flag and the type and position, 7–8; E (span) from the flag's group to the end of its supergroup, 7–12; F (lagged) the flag as it was before the group, 8–11. A and B were the first two readings; C and D fitted the model's output of `oq24_bitsave_flat`; E and F fitted its outputs of all three inputs then decoded, as do the windows 7–11 and 8–12. | DSC 1.2b §6.8.4, §6.6.3 | `bitsave_flat=lagged` (default) or `supergroup`, `group`, `received`, `carrier`, `span` | Resolved by black-box comparison with the VESA C model: F (lagged). Inputs, each committed with its predictions before the model decoded it: `oq24_bitsave_flat` (60ae6cc; A and B) — the model matched neither; `oq24b_bitsave_flat` and `oq24c_bitsave_flat` (352b429; A to D, and the E outcome in the discriminator README) — A and B on `oq24b`, A and C on `oq24c`; `oq24d_bitsave_flat` and `oq24e_bitsave_flat` (ae904ee; A to F, and the windows 7–11 and 8–12) — A, B and F on `oq24d`, B, C, D and F on `oq24e`. F is the only reading or window that fits all five. The inputs fix the ends of the window (7 not covered, 8 and 11 covered, 12 not covered); groups 9 and 10 are taken to be covered as well, which no input tests on its own. In other words, the model treats the flag as covering the four groups after the one that carries it, not the supergroup of §6.8.5.1. | Both. Text first: supergroup and group. received and carrier were fitted to the model's output of the first input, span and lagged to its outputs of the next two (M3 debug builds). The default, lagged, is a fitted reading; the last two inputs tested it with predictions committed before the model decoded them. |
| OQ-25 | The first group of a non-first line in DSC 1.2. §6.8.5.2 gives it the very-flat QP adjustment when its QP is below range 14's maximum; the rule that a very-flat signal below somewhatFlatQpThresh counts as somewhat flat is stated for signaled flatness. Reading A (very): always veryFlatQp. Reading B (signaled): as a signaled very-flat group, including the OQ-16 demotion. | DSC 1.2b §6.8.5.2 | `line_flat=signaled` (default, since M3 Phase 4 part 2) or `very` | Resolved by black-box comparison with the VESA C model: signaled. Discriminator `oq25_line_flat`, committed with its predictions (60ae6cc) before the model decoded it; the model's output matches the signaled prediction and not the very one. | Text: both readings before any model output (M3). |
| OQ-26 | lowMinQp in the zero-residual branch of the DSC 1.2 short-term RC. §6.8.4 defines it from maxQp (MAX(maxQp − 4, 0)). Reading A (max-qp): as printed. Reading B (min-qp): MAX(minQp − 4, 0). The text supports only A; the model-encoded streams of Phase 4 let the QP fall below the range minimum after zero-residual groups, which A forbids and B allows, so the question is kept open for a discriminator. | DSC 1.2b §6.8.4, Figure 6-17 | `low_min=min-qp` (default) or `max-qp` | Resolved by black-box comparison with the VESA C model: min-qp. Discriminator `oq26_low_min`, committed with both predictions (f589964) before the model decoded it; the model's output matches the min-qp prediction and not the max-qp one. | Model output (M3 debug builds and pinned-RC encodes). The text reading was implemented first. |
| OQ-27 | The decrement branch of Figure 6-17. The box tests codedGroupSize and rcSizeGroup both against tgtMinusOffset (reading A, both). Reading B (size): rcSizeGroup alone, which is the same as either of the two, since rcSizeGroup never exceeds codedGroupSize. The model-encoded streams decrement where only rcSizeGroup is below the offset. | DSC 1.2b §6.8.4, Figure 6-17 | `decrement_test=size` (default) or `both` | Resolved by black-box comparison with the VESA C model: size. Discriminator `oq27_decrement_test`, committed with both predictions (f589964) before the model decoded it; the model's output matches the size prediction and not the both one. | Model output (M3 debug builds and pinned-RC encodes). The text reading was implemented first. |
| OQ-28 | The QP in predActivity (bitSaveMode update). The pseudocode adds prevQp, the QP generated last (reading A, prev). Reading B (prev2): prev2Qp, the QP the group whose predicted sizes are used was decoded with; in the re-run after a flatness adjustment, that QP flatness-adjusted as §6.8.4 describes. The model-encoded streams follow B. | DSC 1.2b §6.8.4 | `activity_qp=prev2` (default) or `prev` | Resolved by black-box comparison with the VESA C model: prev2. Discriminator `oq28_activity_qp`, committed with both predictions (f589964) before the model decoded it; the model's output matches the prev2 prediction and not the prev one. | Model output (M3 debug builds and pinned-RC encodes). The text reading was implemented first. |
| OQ-29 | stQp in bitSaveMode 2. Figure 6-17 sets prevQp + 1 (reading A). Reading B: prevQp + 2. Both are clamped to adjustedMaxQp, which hid the difference in `oq22` to `oq24e`; the model-encoded streams follow B. | DSC 1.2b §6.8.4, Figure 6-17 | `bitsave_step=2` (default) or `1` | Resolved by black-box comparison with the VESA C model: 2. Discriminator `oq29_bitsave_step`, committed with both predictions (f589964) before the model decoded it; the model's output matches the 2 prediction and not the 1 one. | Model output (M3 debug builds and pinned-RC encodes). The text reading was implemented first. |
| OQ-30 | A negative rcTgtBitsGroup. With a short group at a line end or a large negative range offset the §6.8.4 formula is below 0. Reading A (none): used as computed, which enlarges incrAmount. Reading B (zero): raised to 0. The text gives no floor; the model-encoded streams follow B. | DSC 1.2b §6.8.4 | `target_floor=zero` (default) or `none` | Resolved by black-box comparison with the VESA C model: zero. Discriminator `oq30_target_floor`, committed with both predictions (f589964) before the model decoded it; the model's output matches the zero prediction and not the none one. | Model output (M3 debug builds and pinned-RC encodes). The text reading was implemented first. |
| OQ-31 | When a DSC 1.2 flatness adjustment re-runs the short-term RC. §6.8.5.2 (as in DSC 1.1, OQ-1) starts the next cycle from the adjusted QP only when the adjustment changes it (reading A, changed). §6.8.4 says that in DSC 1.2 prev2Qp is adjusted for flatness whenever the current group is flat, and §3.10.3 that flatness corrections are now part of the short-term RC. Reading B (every): the step is re-run at every adjusted flat group and line start, changed or not. In both readings the re-run uses the flatness-adjusted prev2Qp (§6.8.4). The model-encoded streams follow B. | DSC 1.2b §6.8.4, §6.8.5.2, §3.10.3 | `flat_rerun=every` (default) or `changed` | Resolved by black-box comparison with the VESA C model: every. Discriminator `oq31_flat_rerun`, committed with both predictions (f589964) before the model decoded it; the model's output matches the every prediction and not the changed one. | Model output (M3 debug builds and pinned-RC encodes). The text reading was implemented first. |
| OQ-32 | bitSaveMode in that re-run. Reading A (keep): the step keeps the bitSaveMode it computed. Reading B (redo): the bitSaveMode update, part of §6.8.4, is computed again with the re-run's prevQp and prev2Qp. The model-encoded streams follow B. | DSC 1.2b §6.8.4 | `rerun_bitsave=redo` (default) or `keep` | Resolved by black-box comparison with the VESA C model: redo. Discriminator `oq32_rerun_bitsave`, committed with both predictions (f589964) before the model decoded it; the model's output matches the redo prediction and not the keep one. | Model output (M3 debug builds and pinned-RC encodes). The text reading was implemented first. |
| OQ-33 | The luma refill threshold at 16 bpc. The longest luma unit by the formula used at other depths is 4 × bpc + 4 = 68 bits (reading A); §3.10.2 limits the prefix so that elements stay within the 64-bit mux word (reading B, 64). The text does not restate the threshold (§4.4). The model-encoded 16 bpc streams follow B. | DSC 1.2b §4.4, §3.10.2 | `mux16=64` (default) or `68` | Resolved by black-box comparison with the VESA C model: 64. Discriminator `oq33_mux16`, committed with both predictions (f589964) before the model decoded it; the model's output matches the 64 prediction and not the 68 one. | Model output (M3 debug builds and pinned-RC encodes). The text reading was implemented first. |
| OQ-34 | The flatness adjustment when the previous QP is above range 14's maximum. §6.8.5.2 skips signaled flatness when the QP equals range_max_qp[14] (reading A, equal), written when no QP could exceed it; DSC 1.2's bitSaveMode can (adjustedMaxQp). Reading B (at-or-above): also skipped above it, as the DSC 1.2 line-start rule does (it applies only below range 14's maximum). No model stream had shown the case; the question came from the text, and the default was the text until the discriminator decided. | DSC 1.2b §6.8.5.2 | `flat_top=at-or-above` (default) or `equal` | Resolved by black-box comparison with the VESA C model: at-or-above. Discriminator `oq34_flat_top`, committed with both predictions (f589964) before the model decoded it; the model's output matches the at-or-above prediction and not the equal one. | Text: both readings before any model stream showed the case (M3); the default was the text reading until the discriminator went the other way. |
| OQ-35 | Which 16 bpc groups have a cut luma prefix. Table 4-10 cuts it at primaryQp 0 (reading A, qp0). Reading B (qlevel): at every QP with luma qLevel 0 (QP 0 to 2, cut as OQ-21) and with luma qLevel 1 (QP 3 and 4, cut at 15 bits, the field length Table 4-10 prints). The model-encoded 16 bpc streams follow B. | DSC 1.2b Table 4-10, §3.10.2, Table 6-3 | `prefix16_scope=qlevel` (default) or `qp0` | Resolved by black-box comparison with the VESA C model: qlevel. Discriminator `oq35_prefix16_scope`, committed with both predictions (f589964) before the model decoded it; the model's output matches the qlevel prediction and not the qp0 one. | Model output (M3 debug builds and pinned-RC encodes). The text reading was implemented first. |
| OQ-36 | When the cut and its rules apply. Table 4-10: in such a group the prefix has at most N bits, ICH is disallowed, the prefix is not adjusted after an ICH group, and all zeros mean MPP regardless of the size prediction (reading A, always). Reading B (longer): only where the uncut prefix could be longer than N bits (a small predicted size); elsewhere the ordinary prefix, ICH escape and ICH adjustment apply. The model-encoded 16 bpc streams follow B; A makes a large predicted size impossible to code. | DSC 1.2b Table 4-10, §3.10.2 | `prefix16_cut=longer` (default) or `always` | Resolved by black-box comparison with the VESA C model: longer. Discriminator `oq36_prefix16_cut`, committed with both predictions (f589964) before the model decoded it; the model's output matches the longer prediction and not the always one. | Model output (M3 debug builds and pinned-RC encodes). The text reading was implemented first. |
| OQ-37 | predActivity in native 4:2:0. The DSC 1.2b §6.8.4 pseudocode (as DSC 1.2a's) opens a MAX( for native 4:2:0 and never closes it. Reading A (luma): the larger of the two luma units' predicted sizes plus the chroma unit's, which parallels the 4:4:4 form (the luma size plus the larger chroma size). Reading B (sum): the parenthesis closes at the end, the larger of the even-luma size and the sum of the other two. A is judged more likely. Found in the text. The first model-encoded 4:2:0 stream decodes bit-exact under both. | DSC 1.2b §6.8.4; DSC 1.2a §6.8.4 | `activity420=luma` (default) or `sum` | Resolved by black-box comparison with the VESA C model: luma. Discriminator `oq37_activity420`, committed with both predictions (81f0e3b) before the model decoded it; the model's output matches the luma prediction and not the sum one. | Text: both readings before the model decoded a stream that separates them (M3). |
| OQ-38 | predActivity in native 4:2:2. The pseudocode adds prevQp and the four predicted sizes followed by `>> 1`. Reading A (sizes): only the sum of the sizes is halved, as the written formula reads. Reading B (total): by C precedence the whole sum, prevQp included, is halved. A is judged more likely: B would halve the QP term that the other two forms add in full. Found in the text. The first model-encoded 4:2:2 stream decodes bit-exact under both. | DSC 1.2b §6.8.4; DSC 1.2a §6.8.4 | `activity422=sizes` (default) or `total` | Resolved by black-box comparison with the VESA C model: sizes. Discriminator `oq38_activity422`, committed with both predictions (81f0e3b) before the model decoded it; the model's output matches the sizes prediction and not the total one. | Text: both readings before the model decoded a stream that separates them (M3). |
| OQ-39 | The BP edge test in native 4:2:0. §6.4.4.1 counts an edge where a sample differs from its left neighbor by more than 32 << (bpc − 8) in any component; §3.10.1 and §6.4.2 restrict BP and the BP search in native 4:2:0 to the luma units, and §6.4.4.1 excludes chroma from the SAD, but the edge test's wording is not restricted. Reading A (luma): only the two luma units of the container. Reading B (all): chroma too, from the line chroma is predicted from (two lines up), where the line has one. A is judged more likely. Found in the text. | DSC 1.2b §6.4.4.1, §6.4.2, §3.10.1 | `bp420_edge=luma` (default) or `all` | Resolved by black-box comparison with the VESA C model: luma. Discriminator `oq39_bp420_edge`, committed with both predictions (81f0e3b) before the model decoded it; the model's output matches the luma prediction and not the all one. | Text: both readings before any model output (M3). |
| OQ-40 | second_line_offset_adj (native 4:2:0). The normative §6.8.2 says only that it is subtracted from rcXformOffset once the first line of the slice is done (reading A, subtract). The informative Table E-2 says it is added to the offset at the start of the slice and subtracted at the first group of the second line (reading B, start), so that it lowers the offset only for the first line. B is judged more likely: under A the adjustment would lower the offset for the whole rest of the slice. Found when the first model-encoded native 4:2:0 stream decoded bit-exact under B and not under A. | DSC 1.2b §6.8.2, Annex E Table E-2 | `offset_adj=start` (default) or `subtract` | Resolved by black-box comparison with the VESA C model: start. Discriminator `oq40_offset_adj`, committed with both predictions (81f0e3b) before the model decoded it; the model's output matches the start prediction and not the subtract one. | Model output (M3 Phase 5: a model-encoded native 4:2:0 stream). subtract (§6.8.2) was implemented first; start, which the informative Table E-2 also describes, is the change that made the stream decode, and the record does not say whether the table or the fit suggested it. |
| OQ-41 | Previous-line ICH entries at the slice edges in native modes. Entries 25 to 31 are pairs of adjacent luma samples of the line above, starting one sample left of the group's first pixel, so they cover eight samples; DSC 1.2b §6.5.1 shifts "the window of referenced pixel values" at the slice edges so that they come from the active raster. Reading A (pixels): the eight luma samples are shifted into the slice, starting at 0 on the left and at slice_width − 8 on the right. Reading B (container): the container pixels those samples lie in, x − 1 to x + 3, are shifted into the slice, so the pairs start at luma sample 1 on the left and at slice_width − 9 on the right (the last luma sample is never referenced). Found when model-encoded native 4:2:0 and 4:2:2 streams decoded bit-exact under B and not under A, at both edges. | DSC 1.2b §6.5.1, Figures 6-7 and 6-8 | `ich_window=container` (default) or `pixels` | Resolved by black-box comparison with the VESA C model: container. Discriminators `oq41_ich_window` (left edge) and `oq41b_ich_window` (right edge), committed with both predictions (81f0e3b) before the model decoded them; the model's output matches the container prediction and not the pixels one on each. | Model output (M3 Phase 5: model-encoded native streams). pixels is the reading taken from the text; container was fitted to the streams. |
| OQ-42 | A scale decrement due at the first group. DSC 1.1 and DSC 1.2b §6.8.2 start rcXformScale at initial_scale_value at the start of a slice and lower it by one every scale_decrement_interval groups. With the OQ-14 reading from-group-0 the interval is counted from the first group, so with an interval of 1 the first decrement falls on group 0 itself (reading A, group). Reading B (not): the first group keeps initial_scale_value, and the count is otherwise unchanged, so with longer intervals A and B agree. Found in Phase 5 on model-encoded streams with small slices (128x32, 64x16; initial_scale_value 30 or 32 and an interval of 1): DSC 1.1 and 1.2, RGB and YCbCr, decode bit-exact only under B. The larger streams of M2 and Phase 4 have longer intervals. | DSC 1.1 §6.8.2; DSC 1.2b §6.8.2 | `scale_first=not` (default) or `group` | Resolved by black-box comparison with the VESA C model: not. Discriminators `oq42_scale_first` (DSC 1.2) and `oq42b_scale_first` (DSC 1.1), committed with both predictions (81f0e3b) before the model decoded them; the model's output matches the not prediction and not the group one on each. | Model output (M3 Phase 5: model-encoded small-slice streams). group (the OQ-14 default applied literally) came first; not was fitted. |
| OQ-43 | Scale decrements after the first line. §6.8.2 lowers the scale "until the factor reaches unity scaling" (reading A, until-unity: for as long as that takes); it describes the decrement as the start-of-slice adjustment, and Annex E sizes scale_decrement_interval so that unity is reached by the end of the first line. Reading B (first): decrements happen only during the first line of the slice, so a scale still above unity then stays there. Found with OQ-42 on the same streams: at 64x16 the scale is still 9 when the first line ends, and the model decodes those streams bit-exact only if it stays 9. The larger streams reach unity within the first line. | DSC 1.1 §6.8.2; DSC 1.2b §6.8.2, Annex E | `scale_line=first` (default) or `until-unity` | Resolved by black-box comparison with the VESA C model: first. Discriminators `oq43_scale_line` (DSC 1.2) and `oq43b_scale_line` (DSC 1.1), committed with both predictions (81f0e3b) before the model decoded them; the model's output matches the first prediction and not the until-unity one on each. | Model output (M3 Phase 5: model-encoded small-slice streams). until-unity (the text) came first; first was fitted. |

YCbCr and the native modes (M3 Phase 5), points taken as the text reads,
without a switch; the model-encoded streams of Phase 5 decode bit-exact
under each:

* YCbCr chroma has the luma bit depth (DSC 1.2b §7.7), and in DSC 1.2
  qLevelC is one below Table 6-3 (§6.8.6: equal luma and chroma depths).
* The odd-position luma samples (Y2 in native 4:2:2, the Co substream in
  native 4:2:0) are luma units: qLevelY (§6.1, §6.4.5) and the Table 4-13
  codebook, with no ICH escape. Table 4-16 defines prefix_Co with qLevelC,
  but for the substream's usual content; the Co substream carries luma in
  native 4:2:0 (§4.4).
* In native 4:2:2 the ICH indices are in Y2, Co and Cg; the Y unit holds only
  the escape (§4.5, §6.6.2).
* The refill threshold of Y2, of the odd luma in native 4:2:0 and of YCbCr
  chroma is 4 × cpntBitDepth, as for other non-first units; Table E-1's
  estimate of numExtraMuxBits counts those substreams with maxSeSize_C.
* BP's search and edge test run on the container (§6.4.4.1): the left
  neighbor of a container sample is the previous container sample of the
  same unit.
* A previous-line ICH pair takes the chroma of its even-position sample
  (Figure 6-8's example, I25 = Y−1, Y0 and Cb0); in native 4:2:0 that chroma
  comes from the second line above (§6.5.1).
* Native 4:2:0 lines 0 and 1 have no chroma above and use all 32 history
  entries (§6.4.1, §6.5.1); a picture with one slice per line keeps the
  history from line 0 into line 1.
* Simple 4:2:2 output keeps the chroma of the even positions (Annex B,
  Figure B-3).
* bits_per_pixel may be up to its field maximum, 1023: in the native modes
  it is twice the picture's rate (Table 4-1).

## DSC 1.2 text and the reference model

Added for v0.2.0 (2026-09-25). In M3, several defaults follow the VESA C
model's behavior (observed only as a black box) where it differs from the
DSC 1.2b text. This section records what the three standards say about the
model's status, and for each such question, what the text says, what the
model does, whether an erratum covers it and what the precedence statements
imply. Section numbers are DSC 1.2b unless marked. The wording is a summary;
the standards are the authority.

### What the standards say about the model

* DSC 1.1. The table of normative reference documents (§1.7) lists the
  VESA DSC C model, version 1.31. The introductions of §6 (encoding) and §7
  (decoding) each say that where the standard and the C model disagree, the
  model's implementation prevails; for decoders the sentence uses "shall".
  The fractional-bpp SCR in the DSC 1.1 E1 errata calls the C model
  normative in its background section.
* DSC 1.2a. The same two statements open §6 and §7, and the normative
  reference table (§1.8) lists the C model, version 1.57. Two SCRs of the
  DSC 1.2a E1 errata (the rcXformBpgOffset and bpSad SCR, and the
  bitSaveMode SCR) change the text to agree with the model, say that the
  model itself does not change, and state that the C code is normative and
  correct. Their reason is that implementers may follow the text rather
  than the code.
* DSC 1.2b. §1.4.3 now carries the precedence rules for the whole
  standard: among text, figures and tables, tables prevail, then figures,
  then text; and where the standard and the C model disagree, the model's
  implementation prevails. The §6 and §7 introductions no longer repeat it.
  The reference table (§1.5, Table 1-7) lists the C model, version 1.63
  (June 2021), in a single table that does not mark entries normative or
  informative. §1.3 describes DSC 1.2b as functionally identical to DSC
  1.2a, a maintenance release that folds in the DSC 1.2a SCRs. Its
  revision history lists those SCRs and one further correction to the
  16 bpc luma prefix limit (§3.10.2 and Table 4-10; see OQ-21).

Three limits on using those statements here:

* Version. The model used for the comparisons reports version 1.67. DSC
  1.2b names 1.63 and DSC 1.2a names 1.57. When this section was written,
  nothing showed that 1.63 behaves like 1.67 on these questions; "Across
  model versions" below now shows that 1.57 and 1.63 do, on every point
  tested.
* Observation. The model was used only as a black box, through its
  command line, its configuration files and the files it writes. A reading
  that reproduces its output is a description of its behavior on the
  streams tried, not a statement of what its source does.
* Scope. The precedence clause settles a disagreement in favor of the
  model; it does not say which differences are errors in the text. For
  implementers who do not have the model, the text is what they read, and
  the errata above show that VESA has corrected the text to match the model
  before.

v0.2.0 source note: the DSC 1.2a E1 errata PDF contains excerpts of model
source in the background parts of the rcXformBpgOffset and bpSad SCR and of
the bitSaveMode SCR, and model source line references in the rate-control
clarification SCR. While the errata was searched for this section, the
first SCR's excerpt and a few of those identifier lines were displayed. They
were not used: this section relies only on the SCRs' proposed text changes
and their stated reasons, and the decoder is unchanged.

### Question by question

Only the questions whose default was set to the model's behavior where the
DSC 1.2b text reads otherwise, or where the text is silent and the default
came from model output. "Errata" means the DSC 1.2a E1 errata (the only
errata to DSC 1.2 in hand); no errata to DSC 1.2b was available.

| Question | DSC 1.2b section | The text, read as written | The model | Errata | What precedence implies |
|---|---|---|---|---|---|
| OQ-19 `delay_partial` (also DSC 1.1) | §6.8.1, §6.8.2 | During the initial delay the offset falls by three pixels' worth of bits per group, while §6.8.1 drains the real pixels of each group; a group of one or two pixels at a line end inside the delay is not addressed. | Counts each group as three pixels from where the previous group ended, so a short group at a line end counts more and the next line's first group less. | None. The DSC 1.1 E1 fractional-bpp SCR changes only the encoder's forceMpp condition. | The text is silent on the case, so there is no conflict to resolve; the model's behavior fills the gap. |
| OQ-21 `prefix16` | Table 4-10, §3.10.2 | Table 4-10 allows a 15-bit luma prefix at 16 bpc and QP 0; §3.10.2 says 13. DSC 1.2a's Table 4-10 says 13. | 13. | None in DSC 1.2a E1. The DSC 1.2b revision history lists a correction of this limit in both places, which left them disagreeing. | §1.4.3 would favor the table over the text (15); the model clause favors 13, and so do §3.10.2 and DSC 1.2a. The model at qLevel 1 cuts the prefix at 15 bits (OQ-35), which suggests that the 1.2b table may describe that case. |
| OQ-23 `bitsave_pred` | §6.8.4, §6.6.1, Tables 6-2 and 7-1 | predActivity uses predictedSize[0..3], which the entropy coder hands to the rate control; which of the sizes computed around a group is meant is not said. | The size predicted from the group's own residuals, for the next group. | The rate-control clarification SCR added predictedSize[0..3] to the outputs listed in Tables 6-2 and 7-1 without defining it further. | The text admits more than one reading, and the model selects one. Two of the three text readings (raw and adjusted) were never separated from each other. |
| OQ-24 `bitsave_flat` | §6.8.4, §6.6.3, §6.8.5.1 | The bitSaveMode update runs only when no flatness is signaled "for the supergroup", which the text does not tie to the current group. The natural reading is the supergroup of §6.8.5.1 that contains the group. | The flag covers the four groups after the group that carries it. | The bitSaveMode SCR reprints the condition without change. | The text is open to several readings, none of which is the model's; the model's is a window that no passage describes. Groups 9 and 10 of the window are inferred. |
| OQ-25 `line_flat` | §6.8.5.2, §3.10.3 | The first group of a non-first line is handled as a very flat group. | As a signaled very flat group, including its demotion to somewhat flat at low QP (OQ-16). | None. | "Handled as very flat" covers both readings; the model selects one. |
| OQ-26 `low_min` | §6.8.4, Figure 6-17 | lowMinQp is derived from maxQp (four less, at least 0). | Derived from minQp in the same way. | None. | A plain conflict; the model prevails. |
| OQ-27 `decrement_test` | §6.8.4, Figure 6-17 | The decrement box requires both codedGroupSize and rcSizeGroup to be under tgtMinusOffset; the prose says both are compared without saying how they combine. | Only rcSizeGroup is tested (equivalent to either being under it). | None. | A conflict with the figure; the model prevails. |
| OQ-28 `activity_qp` | §6.8.4 | predActivity adds prevQp, the QP generated most recently. | Adds prev2Qp, the QP of the group whose predicted sizes are used (flatness-adjusted in a re-run). | None. | A conflict with the pseudocode; the model prevails. Whether "prevQp" is a naming slip or a consequence of the unstated pipeline timing (OQ-11) cannot be told from outputs. |
| OQ-29 `bitsave_step` | §6.8.4, Figure 6-17 | bitSaveMode 2 sets stQp to prevQp plus one. | Plus two. | None. | A conflict with the figure; the model prevails. |
| OQ-30 `target_floor` | §6.8.4 | rcTgtBitsGroup is used as computed, even when negative. | A negative target is raised to 0. | None. | A conflict (the text has no floor); the model prevails. |
| OQ-31 `flat_rerun` | §6.8.4, §6.8.5.2, §3.10.3 | §6.8.5.2, carried over from DSC 1.1, re-runs the step only when the flatness adjustment changes the QP; §6.8.4 and §3.10.3 make flatness part of the DSC 1.2 short-term RC. | Re-runs at every adjusted flat group and line start. | None. | The passages pull in different directions; the model selects one. |
| OQ-32 `rerun_bitsave` | §6.8.4 | Silent on whether a re-run recomputes bitSaveMode. | Recomputes it. | None. | A gap; the model fills it. |
| OQ-33 `mux16` | §4.4, §3.10.2 | The refill threshold for 16 bpc luma is not restated; the formula used at other depths gives 68 bits, while §3.10.2 says the changes keep the mux word at 64 bits. | 64. | None. | A gap; the model fills it, as §3.10.2's stated aim suggests. |
| OQ-34 `flat_top` | §6.8.5.2 | Signaled flatness is skipped when the QP equals range 14's maximum, written when no QP could exceed it. | Also skipped above it. | None. | A conflict for QPs that bitSaveMode raises above range 14's maximum; the model prevails. This default was the text's until the discriminator was decoded. |
| OQ-35 `prefix16_scope` | Table 4-10, §3.10.2, Table 6-3 | The 16 bpc prefix cut applies at QP 0. | At every QP with luma qLevel 0 (13 bits) or 1 (15 bits). | None. | A conflict with the table; the model prevails. |
| OQ-36 `prefix16_cut` | Table 4-10, §3.10.2 | In such a group the cut and its rules (no ICH, no adjustment after ICH, all zeros mean MPP) always apply. | Only where the uncut prefix could be longer than the cut; the literal reading makes a large predicted size impossible to code. | None. | A conflict with the table; the model prevails. |
| OQ-40 `offset_adj` | §6.8.2, Annex E Table E-2 | Normative §6.8.2: second_line_offset_adj is subtracted after the first line. The informative Table E-2 entry adds it at the slice start and subtracts it at the first group of the second line. | As Table E-2. | None (the nsl_bpg_offset SCR concerns another field). | A conflict with the normative text; the model agrees with the informative annex. |
| OQ-41 `ich_window` | §6.5.1, Figures 6-7 and 6-8 | At the slice edges the window of referenced values is shifted into the slice; in the native modes the text does not say whether it is shifted in luma samples or in container pixels. | Container pixels. | None. | A gap; the model fills it. |
| OQ-42 `scale_first` (also DSC 1.1) | §6.8.2 | The scale starts at initial_scale_value and falls by one every scale_decrement_interval groups; with an interval of 1 it is not said whether the first group already counts. | The first group keeps initial_scale_value. | None. | A gap; the model fills it. |
| OQ-43 `scale_line` (also DSC 1.1) | §6.8.2, Annex E | Decrements continue until the scale reaches unity; the passage calls it a start-of-slice adjustment, and Annex E sizes the interval so that unity is reached within the first line. | No decrement after the first line of the slice. | None. | A conflict only for parameters outside Annex E's guidance; the model prevails. |

### Across model versions

Added 2026-09-26 (see "Model versions" below). For each point: the model
versions that decoded a test of it, and whether they behave as 1.67 does.
"Long-standing": in every version tested that supports the format.

| Question | Tested on | Behavior as in 1.67 | Standing |
|---|---|---|---|
| OQ-19 `delay_partial` | `oq19` (DSC 1.1): 1.31a to 1.67 | all five | long-standing, from 1.31a |
| OQ-21 `prefix16` | `oq21`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 (1.48 on) |
| OQ-23 `bitsave_pred` | `oq23`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-24 `bitsave_flat` | `oq24b` to `oq24e`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-25 `line_flat` | `oq25`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-26 `low_min` | `oq26`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-27 `decrement_test` | `oq27`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-28 `activity_qp` | `oq28`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-29 `bitsave_step` | `oq29`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-30 `target_floor` | `oq30`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-31 `flat_rerun` | `oq31`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-32 `rerun_bitsave` | `oq32`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-33 `mux16` | `oq33`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-34 `flat_top` | `oq34`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-35 `prefix16_scope` | `oq35`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-36 `prefix16_cut` | `oq36`: 1.48 to 1.67 | all four | long-standing in DSC 1.2 |
| OQ-40 `offset_adj` | `oq40`: 1.57 to 1.67; 1.48 dies on it | 1.57, 1.63, 1.67 | from 1.57; 1.48 could not be tested (its native 4:2:0 decoding differs throughout, and its successors' READMEs call it incorrect) |
| OQ-41 `ich_window` | `oq41` (4:2:2) and `oq41b` (4:2:0): 1.57 to 1.67; 1.48 on its own native 4:2:2 encodes | 1.48 (4:2:2), 1.57, 1.63, 1.67 | long-standing in native 4:2:2 (1.48 on); in native 4:2:0 from 1.57, 1.48 not testable |
| OQ-42 `scale_first` | `oq42b` (DSC 1.1): 1.31a to 1.67; `oq42` (DSC 1.2): 1.48 to 1.67 | all five | long-standing, from 1.31a |
| OQ-43 `scale_line` | `oq43b` (DSC 1.1): 1.31a to 1.67; `oq43` (DSC 1.2): 1.48 to 1.67 | all five | long-standing, from 1.31a |

The versions the standards cite behave as 1.67 on every point tested:
1.57 (DSC 1.2a) and 1.63 (DSC 1.2b) on all twenty. On the three that
apply to DSC 1.1, so does 1.31a, the oldest build here (DSC 1.1 names
version 1.31).

### Summary

Reassessed on 2026-09-26 with the five model versions ("Across model
versions" above). No point changes category. What the versions add:

* Look like errors in the text: OQ-26, OQ-27 and OQ-29 (explicit values or
  conditions in §6.8.4 and Figure 6-17 that the model contradicts); OQ-30
  (a missing floor); OQ-34 (a DSC 1.1 condition not updated for QPs that
  DSC 1.2 can reach); OQ-21, OQ-35 and OQ-36 (Table 4-10's 16 bpc cut,
  which disagrees with §3.10.2 and with the model on its length, its scope
  and when it applies); OQ-40 (normative §6.8.2 omits the addition that
  the informative Table E-2 describes). The versions make this category
  firmer. Each of these behaviors is present in 1.57 and 1.63, the versions
  DSC 1.2a and DSC 1.2b cite, and all but OQ-40 already in 1.48; none comes
  from a model change after the text was written. For OQ-21 the DSC 1.2b
  correction of Table 4-10 moved the table away from the behavior of 1.63,
  the version DSC 1.2b itself cites. OQ-40's behavior dates from the DSC
  1.2a model (1.57): 1.48 decodes native 4:2:0 differently throughout,
  which its successors' READMEs describe as the DSC 1.2 defect corrected in
  DSC 1.2a; §6.8.2 disagrees with both cited versions.
* Look like ambiguity: OQ-19, OQ-23, OQ-25, OQ-31, OQ-32, OQ-33, OQ-41 and
  OQ-42. The text is silent, or two passages allow both readings, and the
  model picks one. Its pick has not changed between versions: from 1.31a
  for OQ-19 and OQ-42 (DSC 1.1), from 1.48 for the others, except OQ-41 in
  native 4:2:0, known only from 1.57 on.
* Remain unclear: OQ-24 (the text's condition is open, and the model's
  window matches none of the readings the text suggests); OQ-28 (a naming
  slip, or the unstated pipeline timing); OQ-43 (the text's stop condition
  and the model's agree for Annex E parameters, so the model may reflect a
  design assumption rather than show a text error). The versions do not
  decide these. They show that none is a late model change: OQ-24 and
  OQ-28 behave the same from 1.48 on (no version tested ever used prevQp for
  OQ-28), and OQ-43 from 1.31a, the oldest build tested (DSC 1.1 names
  1.31), so the first-line-only decrement goes back to the DSC 1.1 model.

None of these points is covered by an erratum in hand. Under DSC 1.2b §1.4.3
the model's behavior is the standard's wherever the two differ, so the
defaults follow it. The earlier limit, that 1.67 had not been shown to
behave like the 1.63 that DSC 1.2b cites, no longer applies to these
points: 1.63 behaves like 1.67 on all twenty. Each text reading stays
available behind its switch.

## Model versions

Added 2026-09-26. Five builds of the VESA C model were compared, each only
as a black box (its README.TXT, configuration files, command line and output
files). Details and every run are in PROGRESS.md, "Model versions". The
decoder was not changed; every comparison used the v0.2.0 binary with its
default readings unless a cell says otherwise.

| Reported version | Copyright banner | Cited by | DSC versions (README) | Formats (README) |
|---|---|---|---|---|
| 1.31a | 2013-2014 | DSC 1.1 names 1.31 | 1.1 | RGB, YCbCr 4:4:4 and 4:2:2, 8 to 12 bpc |
| 1.48 | 2013-2015 | | 1.1, 1.2 | RGB, YCbCr 4:4:4, simple 4:2:2, native 4:2:2 and 4:2:0, 8 to 16 bpc |
| 1.57 | 2013-2016 | DSC 1.2a | 1.1, 1.2 (with a "DSC 1.2a" section) | as 1.48 |
| 1.63 | 2013-2021 | DSC 1.2b | 1.1, 1.2/1.2a/1.2b | as 1.48 |
| 1.67 | 2013-2021 | | 1.1, 1.2/1.2a/1.2b | as 1.48 |

The 1.57, 1.63 and 1.67 READMEs say that native 4:2:0 was not correctly
supported in DSC 1.2 and was deprecated, and that the DSC 1.2a model
corrects it. The older versions differ in their interfaces (parameters they
reject, output formats, a decode that needs the stream's bit depth, 1.48's
standalone YCbCr decode dying from SIGSEGV, 1.63 not reading 8-bit 4:2:0
.yuv files); `tools/compare_model` handles each (PROGRESS.md, Phase 1).

### Discriminators by version

The readings each version's output matches, over the discriminators of each
question (a reading counts when every input with a prediction for it
matched it). "n/a": the version does not document the input's DSC version
or format. "crash": 1.48 documents native 4:2:2 and 4:2:0 but dies decoding
a native bitstream on its own. "no input": no discriminator.

| Question | Discriminators | 1.31a | 1.48 | 1.57 | 1.63 | 1.67 |
|---|---|---|---|---|---|---|
| OQ-1 `flat_restart` | 1 | in-flight | in-flight | in-flight | in-flight | in-flight |
| OQ-2 `threshold_eq` | 1 | lower | lower | lower | lower | lower |
| OQ-3 `frac_reset` | 1 | chunk | chunk | chunk | chunk | chunk |
| OQ-4 `bp_left` | 1 | midpoint | midpoint | midpoint | midpoint | midpoint |
| OQ-5 `incr_order` | none | no input | no input | no input | no input | no input |
| OQ-6  | none | no input | no input | no input | no input | no input |
| OQ-7 `bpg_combine` | 1 | n/a | add | add | add | add |
| OQ-8  | none | no input | no input | no input | no input | no input |
| OQ-9  | none | no input | no input | no input | no input | no input |
| OQ-10 `bp_sad` | none | no input | no input | no input | no input | no input |
| OQ-11 `rc_pipeline` | none | no input | no input | no input | no input | no input |
| OQ-12 `delay_offset` | none | no input | no input | no input | no input | no input |
| OQ-13 `bp_edge` | none | no input | no input | no input | no input | no input |
| OQ-14 `scale_dec` | none | no input | no input | no input | no input | no input |
| OQ-15 `partial_target` | none | no input | no input | no input | no input | no input |
| OQ-16 `very_flat` | none | no input | no input | no input | no input | no input |
| OQ-17 `partial_padding` | none | no input | no input | no input | no input | no input |
| OQ-18 `flat_max_qp` | none | no input | no input | no input | no input | no input |
| OQ-19 `delay_partial` | 1 | group-end | group-end | group-end | group-end | group-end |
| OQ-20 `chroma_qlevel` | 1 | n/a | equal-depth | equal-depth | equal-depth | equal-depth |
| OQ-21 `prefix16` | 1 | n/a | 13 | 13 | 13 | 13 |
| OQ-22 `bitsave_ich` | 1 | n/a | not | not | not | not |
| OQ-23 `bitsave_pred` | 1 | n/a | next | next | next | next |
| OQ-24 `bitsave_flat` | 4 | n/a | lagged | lagged | lagged | lagged |
| OQ-25 `line_flat` | 1 | n/a | signaled | signaled | signaled | signaled |
| OQ-26 `low_min` | 1 | n/a | min-qp | min-qp | min-qp | min-qp |
| OQ-27 `decrement_test` | 1 | n/a | size | size | size | size |
| OQ-28 `activity_qp` | 1 | n/a | prev2 | prev2 | prev2 | prev2 |
| OQ-29 `bitsave_step` | 1 | n/a | 2 | 2 | 2 | 2 |
| OQ-30 `target_floor` | 1 | n/a | zero | zero | zero | zero |
| OQ-31 `flat_rerun` | 1 | n/a | every | every | every | every |
| OQ-32 `rerun_bitsave` | 1 | n/a | redo | redo | redo | redo |
| OQ-33 `mux16` | 1 | n/a | 64 | 64 | 64 | 64 |
| OQ-34 `flat_top` | 1 | n/a | at-or-above | at-or-above | at-or-above | at-or-above |
| OQ-35 `prefix16_scope` | 1 | n/a | qlevel | qlevel | qlevel | qlevel |
| OQ-36 `prefix16_cut` | 1 | n/a | longer | longer | longer | longer |
| OQ-37 `activity420` | 1 | n/a | crash | luma | luma | luma |
| OQ-38 `activity422` | 1 | n/a | crash | sizes | sizes | sizes |
| OQ-39 `bp420_edge` | 1 | n/a | crash | luma | luma | luma |
| OQ-40 `offset_adj` | 1 | n/a | crash | start | start | start |
| OQ-41 `ich_window` | 2 | n/a | crash | container | container | container |
| OQ-42 `scale_first` | 2 | not (DSC 1.1 input) | not | not | not | not |
| OQ-43 `scale_line` | 2 | first (DSC 1.1 input) | first | first | first | first |

Every input that a version decoded matched the same prediction as with
1.67, and gave the same output as the decoder's default decode. No question
changed its reading between versions; for each, the first version that
behaves like 1.67 is the oldest one that decodes its discriminators.
Beyond the discriminators:

* 1.48, native 4:2:2 (OQ-38, OQ-41): on its own encodes (32 streams,
  decoded by the model in one FUNCTION 0 run), the model follows `sizes` and
  `container`, as 1.67 does; 16 and 22 of those streams separate the two
  readings.
* OQ-17: the two invalid-padding fixtures decode bit-exact, under `accept`,
  in every version.
* The superseded `oq2_threshold_equality`, which needs more bits than its
  payload holds under the readings that reproduce the model, gives one
  output in 1.31a, 1.48 and 1.57 and another in 1.63 and 1.67; neither reads
  past the payload. It is the only discriminator whose output changed.

### Model-encoded streams by version

Bit-exact / runs; each version encoded the pictures and decoded its own
bitstreams (Phase 3).

| Set | 1.31a | 1.48 | 1.57 | 1.63 | 1.67 |
|---|---|---|---|---|---|
| DSC 1.1 corpus, 8 bpc, 8 bpp | 102 / 102 | 102 / 102 | 102 / 102 | 102 / 102 | 102 / 102 |
| DSC 1.1 synthetic, 10 and 12 bpc, 8 bpp | 96 / 96 | | | | |
| DSC 1.2 RGB synthetic, 8, 10 and 12 bpc, 6 to 15 bpp | n/a | 720 / 720 | 720 / 720 | 720 / 720 | 720 / 720 |
| DSC 1.2 native 4:2:2 synthetic, 8 bpc, 8 bpp | n/a | 32 / 32 | 32 / 32 | 32 / 32 | 32 / 32 |
| DSC 1.2 native 4:2:0 synthetic, 8 bpc, 6 bpp | n/a | 0 / 32 | 32 / 32 | 32 / 32 (from 4:4:4 DPX) | 32 / 32 |

1.48's native 4:2:0 decode differs from dscdecode within the first groups of
line 0, and no switch setting, text reading or combination of the text
readings reproduces it. 1.57 decodes 1.48's own native 4:2:0 bitstreams
differently from 1.48 (on the 8 that 1.57 and dscdecode decode alike, 1.48
differs from both), in line with the READMEs' note on DSC 1.2 native 4:2:0.

Behavior changes between versions:

| Change | Older behavior | From | Setting that reproduces the older version |
|---|---|---|---|
| Native 4:2:0 decoding | 1.48 | 1.57 | none found |
| Decoding after the payload is exhausted (`oq2`) | 1.31a to 1.57 | 1.63 | none possible without changing the decoder |

## Continuation — September 20, 2026

**M1 remained incomplete at this date.** This continuation added two
nonzero-QP/flatness image fixtures, prediction sample traces, and a
partial-group syntax-validation fix. BP and VBR were unsupported at this
point; M2 implemented BP, and VBR is still unsupported. The original research
and historical checkpoint below are preserved; this section records the state
on September 20.

### Recovered inputs and source discipline

The checkpoint archive `dsc-decoder-checkpoint.zip` was extracted into a
local working directory. Its `RESEARCH.md` is byte-identical to the latest
separately kept copy of the research record (SHA-256
`95eb8ebe83afb0e4803e5b77e857fe405782f58d4418f1f94876ca5d66d454bc`); two
other kept copies are identical older snapshots.
The imported six image fixtures, RC traces and 14 CLI checks passed before edits.

The pinned DSC 1.1 PDF was downloaded again from the source listed below;
its SHA-256 matched `9f5a1a54601bc5ed9f58edd5f8878f1c14114a291ce96ea3629d63e7efb4da65`.
Sections 6.3--6.8 and 7.1--7.4 supplied the relevant prose checks; Figure 6-8
(page 81) was visually inspected. No reference-model sources or executables
were consulted for this continuation. No driver was modified. Registration
through the official VESA route followed on 2026-09-23 (see Track B).

### Implementation and validation changes

* `src/decode.c` now rejects a partial group's nonzero padded residuals and
  ICH indices that do not repeat the rightmost real index, as specified by
  DSC 1.1 section 6.6 (page 75). Previously these invalid samples were ignored
  during reconstruction. A new rejection test failed against the original
  implementation before the fix. Successful existing images are unchanged.
* `tests/make_transition_vectors.py` constructs two additional image fixtures
  with fixed, independently calculated QP and pixel sequences, plus two invalid
  partial-group fixtures. `tests/VECTORS.md` records their derivation and limits.
  The two successful fixtures exercise QP 0/8 transitions, MPP quantization,
  size prediction after QP changes, and both flatness types (QP 4 and QP 1).
* `tests/test_predict.c` checks 8-bit versus 9-bit line-storage precision,
  chroma rounding/saturation, history precision, final-group exclusion, and
  multiple-slice line reset. The Makefile runs these in normal and sanitizer tests.
* All **eight image fixtures match exactly**, and **18 CLI checks** pass.
  Both the RC trace suite and new prediction trace suite pass. GCC builds with
  the existing warning flags are clean. AddressSanitizer/UndefinedBehaviorSanitizer
  runs of the full test suite pass.
* A final coverage-guided libFuzzer run completed **1,210,135 executions in
  51 seconds**, with 1,281 coverage counters and 6,380 features, without a
  reported address/undefined-behavior error or timeout. It used the prior
  exploratory corpus plus the new synthetic seeds, seed 20260920, a 65,536-byte
  input limit, and a two-second per-input timeout. This is a finite robustness
  test, not proof of crash freedom. `ASAN_OPTIONS=detect_leaks=0` was required
  because LeakSanitizer cannot operate under this environment's tracing.
  An earlier exploratory campaign completed 1,043,587 executions but exited
  with a LeakSanitizer environment error; it is not counted as a clean pass.
  The final successful run and sanitizer results are preserved in
  `research/2026-09-20-fuzz.txt` and `research/2026-09-20-sanitizers.txt`.

### Freshness and remaining work

Linux master was refreshed to **518e5b794c06c0f0eb40df3e202274a66202c137**
(committer date September 19, 2026). Its
[`drm_dsc.h`](https://github.com/torvalds/linux/blob/518e5b794c06c0f0eb40df3e202274a66202c137/include/drm/display/drm_dsc.h)
is byte-identical to the checkpoint's reused header, and its
[`drm_dsc_helper.c`](https://github.com/torvalds/linux/blob/518e5b794c06c0f0eb40df3e202274a66202c137/drivers/gpu/drm/display/drm_dsc_helper.c)
is byte-identical to the September 15 revision. No reconciliation or numerical
table change was needed. The earlier RC-table conclusions are unchanged.

The new fixtures validate the stated two-group/startup interpretation; they do
not independently settle it. Both flatness overrides occur with QP 8 already
pending and produce QP 8 for the next calculation, so the restart ordering
cannot be distinguished by those fixtures. On September 20, exact threshold
equality, general fractional-rate interoperability, the BP left-boundary
sample convention (`research/prediction-ambiguities.md`), agreement with the
reference model, and VBR were all open, and they prevented an M1 completion
claim.

M2 status (2026-09-23): block prediction is implemented, and the decoder has
been compared with the VESA C model, used as a black box. The current status
of each question is in the "Open questions" table above; the comparison
results are in `PROGRESS.md`. VBR framing and buffer accounting are still
unimplemented, fractional bits_per_pixel has been compared at two rates
only, and no hardware agreement has been tested.

The September 20 checkpoint was delivered as source files and an archive,
without Git metadata. In M2 it became the first commit ("M1 baseline") of this
Git repository, and all later work is recorded as commits on top of it.

## Sources and immutable revisions

- Linux master: **9b87fdc9af2fbfcdb5c24a64139685ef80f6573f**, commit date 2026-09-15, fetched from torvalds/linux. [Helper](https://github.com/torvalds/linux/blob/9b87fdc9af2fbfcdb5c24a64139685ef80f6573f/drivers/gpu/drm/display/drm_dsc_helper.c), [definitions](https://github.com/torvalds/linux/blob/9b87fdc9af2fbfcdb5c24a64139685ef80f6573f/include/drm/display/drm_dsc.h). Both files read end to end (1566 and 602 lines).
- NVIDIA: **61dcc93722ecb418bb5f2e00923f05b4b8051dd1**, release 615.71.09, 2026-09-09. [nvt_dsc_pps.c](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/61dcc93722ecb418bb5f2e00923f05b4b8051dd1/src/common/modeset/timing/nvt_dsc_pps.c). PPS generation, packing, validation, slice selection and lookup matrices inspected.
- VESA DSC 1.2, 20 January 2016 (unofficial copy; link removed), 146 pages. SHA-256 `6574d6400416da2e714ecdf5de888cf3160a7f6d2340e1bd36ef91b4987ca828`. Sections 4, 6, 7 and Annex E supply syntax, reconstruction, rate control and parameter guidance. Figure 6-18 also inspected as a rendered page.
- DSC 1.1 PDF and DSC 1.1 E1 errata, together with a copy of the VESA C model, were obtained from an unofficial public mirror (link, mirror revision, and model archive name and hash removed). DSC 1.1 PDF SHA-256 `9f5a1a54601bc5ed9f58edd5f8878f1c14114a291ce96ea3629d63e7efb4da65`.
- [Official VESA distribution entry](https://vesa.org/vesa-display-compression-codecs/) links a registration form and explicitly lists normative C code. The downloaded copies above are mirrors, not authenticated downloads from VESA. Registration through this route was completed on 2026-09-23; the record is in Track B.
- [NVIDIA issue 1039](https://github.com/NVIDIA/open-gpu-kernel-modules/issues/1039) is a report to investigate, not a normative source or proof of causality.

The specification access gate is satisfied. The normative C model includes a decoder (`FUNCTION=2`); therefore the claim that no publicly obtainable software decoder exists is too broad. A small, independently written, conventionally open-source, bounded-input decoder remains useful. No reference-model implementation text has been copied into this library. This section was written on 2026-09-16, before the implementation; the implementation is described in "Track A implementation checkpoint" below.

## Follow-up research: model availability, license and reuse decision

Updated 2026-09-16, before implementation or driver edits.

### Availability and exact terms

**The model is publicly obtainable, not VESA-member-only.** The [official DisplayPort FAQ](https://www.displayport.org/faq/) explicitly answers its membership question by saying that DSC 1.2a, including codec C sources, is freely available from VESA. The [VESA codec page](https://vesa.org/vesa-display-compression-codecs/) leads to [public registration](https://fs16.formsite.com/VESA/form714826558/secure_index.html). That page separates reference downloads from product development: the latter links an implementer agreement. Official availability involves registration; it is not an anonymous ZIP link. Registration on the product-development path was completed on 2026-09-23 (see Track B).

## RC parameter guidance

The 2015 PPS-guidance SCR in the DSC 1.1 E1 errata corrected the recommended values in DSC 1.1 Annex E, Table E-5; the values are not reproduced here.

## What an offline decoder can establish

PPS plus compressed bytes can establish syntax validity, reconstructed pixels, and agreement between transmitted parameters and the encoded payload. PPS alone cannot establish what hardware actually programmed. A successful decode cannot establish perceptual fidelity without the source image, or prove DP/HDMI timing and transport correctness. Different recommended RC presets are not automatically nonconforming: the decoder uses the transmitted range parameters. Diagnose a preset as causal only with a stream, reconstructed output and, where relevant, buffer-model evidence.

## Kernel representation and PPS field map

Reuse the MIT-licensed `drm_dsc.h` definitions, retaining field order and integer widths, with a minimal userspace type/DP-header compatibility include. The 128-byte packed wire struct and the native `drm_dsc_config` are different structures. Never memcpy the PPS into the config or dereference unaligned multibyte fields. All two-byte wire values are big endian. `range_bpg_offset` remains a masked 6-bit quantity in the config's u8; sign-extend only for arithmetic. `rc_buf_thresh` stores the encoded byte in each u16, not an expanded bit count.

The byte layout is not reproduced here. See `struct drm_dsc_picture_parameter_set` in `include/drm/display/drm_dsc.h` for the packed 128-byte wire format, and DSC 1.1 §4.1.1 for the normative syntax.

Other unlisted bits are reserved. `slice_count`, `mux_word_size`, and `rc_bits` are not transmitted fields. Derive the first from geometry (checking the config's u8 capacity), the second from bpc, and the last from HRD timing. Do not overwrite PPS RC values with `drm_dsc_setup_rc_params()` results.

## Helper mathematics and behavior

For RGB, groups per line are ceil(slice_width/3); groups per slice multiply this by slice_height. Chunk bytes are ceil(slice_width * bits_per_pixel / 128), with bpp stored in sixteenths. A CBR slice occupies chunk bytes times slice height. The helper computes mux-tail allowance using three substreams, rounds it down until the remaining slice budget is divisible by mux-word size, and includes it in final and slice offsets. Its final offset is model size minus rounded transmission-delay bit budget plus mux allowance. Non-first-line and slice offsets use 11 fractional bits and ceiling division. Scale is stored in eighths. HRD delay is ceiling(minimum buffer bits / bpp); decoder delay subtracts transmission delay.

The helper has four preset families: pre-SCR 1.1, newer 4:4:4, native 4:2:2 and native 4:2:0. Lookup requires an exact bpp/bpc match; it does not interpolate or validate arbitrary untrusted PPS. `drm_dsc_set_rc_buf_thresh()` stores thresholds divided by 64 and special-cases 6 bpp. Parameter calculation assumes valid nonzero geometry and suitable denominators. These are kernel-internal trusted configuration helpers, not a safe parser contract.

## In-tree caller survey

The direct-call inventory below was obtained by searching the checked-out complete `drivers/gpu/drm` subtree for every exported helper. Include-only/structure users were also searched. This is a DRM driver survey, not a claim about out-of-tree vendors.

| Family | PPS and slice programming | Divergence |
|---|---|---|
| i915 | `intel_vdsc.c` packs DP SDP and DSI PPS with common helpers; writes DSC PPS/RC hardware registers separately. `intel_dp.c` and `icl_dsi.c` invoke common RC math. Picture width per hardware instance differs from full transmitted picture width. | Older paths choose pre-SCR at selected 8/12-bpp modes. Display version >=13 generally uses formula/QP-table calculation, except DSC 1.1 DSI, whose hardcoded panel/VBT expectations require presets. Slice topology includes pipes, streams and slices per stream. |
| xe | `xe/Makefile` builds shared i915 display code including `intel_vdsc.o`. | Not an independent PPS algorithm. |
| amdgpu | `dcn20_dsc.c` creates register values and packs the same config with the common PPS helper; `rc_calc_dpi.c` invokes common RC math after AMD RC selection. Newer DCN blocks inherit/extend this path. | Separate `rc_calc.c`/DML calculation; horizontal slice width rounds up including padding; vertical division must be exact. Native modes double the PPS bpp representation. |
| MSM DSI / DPU | Host fills params with const/threshold/pre-SCR helpers and common math; panels send packed PPS over DSI. `dpu_hw_dsc.c` and `_1_2.c` write slice dimensions, chunk sizes and RC thresholds/ranges to registers. | Host rejects fractional bpp, forces RGB/CBR, selects pre-SCR and line buffer bpc+1. Host validates exact divisibility. Bonded interfaces and encoder blocks partition slices; wire slice count is not the hardware engine count. |
| nouveau | No `drm_dsc` references or helper calls in this pinned subtree. | Do not invent a nouveau encoder path. DSC-capability constants elsewhere do not establish implementation. |
| panel drivers | Eleven direct users listed below call common packer and `mipi_dsi_picture_parameter_set_multi`. | Panel-specific geometry/version/depth and sometimes full presets, rather than a universal default. |
| DRM DP / MIPI infrastructure | Header/structure/capability and packet transport users. | Transport support does not itself provide a pixel decoder or configure an encoder. |

Panel configurations (width x height per slice; count per configured interface):

| Driver suffix (`panel-*.c`) | Version | bpc/bpp | Slice / count |
|---|---|---|---|
| chipone-icna35xx | 1.1 | 8/8 | 540x20 or 540x12 / 2 |
| himax-hx83121a | 1.1 | 8/8 | 800x20 / 1 |
| ilitek-ili9882t | 1.2 | 8/8 | 800x8 / 2 |
| lg-sw43408 | 1.1 | 8/8 | 540x16 / 2 |
| novatek-nt36536 | 1.2 | 10/8 | 476x20 / 2 |
| novatek-nt37801 | 1.1 | 8/8 | 720x40 / 2 |
| raydium-rm692e5 | 1.1 | 8/8 | 1224x60 / 1 |
| samsung-s6e3ha8 | 1.1 | 8/8 | 720x40 / 2 |
| visionox-r66451 | 1.2 | 8/8 | 540x20 / 2 |
| visionox-rm692e5 | 1.1 | 10/8 | 540x20 / 2 |
| visionox-vtdr6130 | 1.2 | 8/8 | 540x40 / 2 |

## NVIDIA differences and contradictions

NVIDIA independently generates PPS, retaining threshold values in bits until packing. It builds bytes explicitly and places successive bytes in increasing byte positions of 32-bit hardware words. This is not a reversal of big-endian PPS multibyte fields. Geometry selection intersects source/sink capability masks, checks throughput and width, and uses ceiling horizontal division. Normal automatic height selection tries picture-height divisors; Blackwell eDP instead seeks small heights for selective-update power savings. These policies differ from a generic decoder's accepted syntax.

| Finding | Kernel/helper | NVIDIA | Specification / consequence |
|---|---|---|---|
| RGB QP matrices | Intel's separate `intel_qp_tables.c` | Six 444 matrices for 8/10/12bpc | **All 4,410 entries match exactly**. Not an NVIDIA-only table scheme. |
| 8bpc/8bpp range 9 | pre-SCR and newer min=3 | min=4 | Preset difference in both spec table comparisons. |
| Same, range 10 max | pre-SCR 11; newer 10 | 10 | Old recommendation vs revised recommendation, not evidence of invalid syntax. |
| Same, range 11 | pre-SCR (5,12,-12); newer (5,11,-12) | (5,11,-10) | Offset differs from both; maximum agrees with newer table. |
| Same, range 12 max | pre-SCR 13; newer 11 | 11 | Agrees with newer recommendation. |
| Same, range 13 | pre-SCR (7,13,-12); newer (9,12,-12) | (8,12,-12) | Neither fixed preset; matches Intel formula matrix. |
| Same, range 14 | pre-SCR (13,15,-12); newer (12,13,-12) | (12,13,-12) | Agrees with newer recommendation. |
| 8bpp/10bpc range 0 max | pre-SCR 4; newer 8 | 8 | Old spec table vs corrected/model recommendation; helper comments explicitly document it. |
| First-line budget | fixed tables choose 12 or 15 | height-dependent formula | Annex E recommends height adaptation; mismatch alone is not a conformance defect. |
| PPS edge / target offsets | packer ignores config members, writes 6 and 3/3 | packs supplied values | PPS permits fields to vary. An inverse of this packer must not discard transmitted values. |
| Threshold units | encoded units in u16 array | full bit count until packing | Wire bytes agree; multiplying twice would be a decoder bug. |
| Header docs | `0xF` described as 14-bit line storage; convert_rgb=false described as YCoCg | n/a | Spec says 15-bit for 0xF (1.2) and YCbCr without RGB conversion. Documentation defects, not packer behavior. |
| Native scale increment | common math omits nsl offset in denominator | includes nsl | Outside milestone; requires native-mode review, not an RGB fix. |
| Native minimum-buffer estimate | uses general first-line upper bound | samples offset inflection points | Spec describes tighter native bound; estimates need not coincide. |
| Geometry validation | trusted inputs; multiple unchecked divisors | PPS validator divides by slice_width before checking it | Do not reuse either as an arbitrary-input validator. NVIDIA zero-width divide identified by inspection, not exercised against hardware. |
| Slice count policy | caller-specific | forced validation omits 6 although enumeration includes 6 | Internal policy inconsistency; not DSC bitstream syntax. |
| Native 420 | dedicated helper presets | dedicated 420 RC path is commented out; 444 fallback remains | Outside milestone; report as implementation divergence, not demonstrated corruption. |

The numerical appendix exhaustively lists differences at **33 overlapping, reachable fixed operating points**, 161 differing RC triples. It covers RGB pre-SCR/newer 8/10/12/15 bpp at 8/10/12 bpc and native 422 7/8/10 bpp. NVIDIA rejects 444 rates below 8 and native 422 below 7 through the inspected entry path; 14/16 bpc have no NVIDIA table here. Fractional columns have no corresponding fixed helper row: the full RGB matrices were instead compared to Intel. The 1.2 Table E-5 spec column is available only at 8/12bpp, and matches the newer helper RC triples there. A dash is absence of a published row in that table, not approval of NVIDIA's value. Native-mode spec-table expansion is outside this initial RGB audit.

## Full decode pipeline and ordering

1. Validate the 128-byte PPS into the reused config layout. Accept only the supported version/format; distinguish unsupported modes from malformed data. Bound sizes before allocation and multiplication. Treat all slice state as local to a slice.
2. Separate picture-level chunks into slices if the CLI receives picture data. CBR chunks arrive by slice-line across horizontal slices; concatenated whole slices are a different input convention and must be explicit. Decode padded slices fully and crop at picture edges.
3. Initialize three component reservoirs and per-slice RC, residual-size predictors, reconstructed line storage and invalid ICH entries. RGB 8bpc uses component widths 8,9,9 and 48-bit mux words. Refill reservoirs in Y/Co/Cg order when fewer than the maximum syntax-element bits remain (36 each here).
4. At each three-pixel group parse conditional flatness syntax, modified unary prefixes, and signed residuals or three ICH indices. Prefix interpretation depends on previous ICH use and predicted residual size. A maximum-width residual selects MPP. Parse all three residual slots even in a partial final group.
5. Map the current QP through the specified luma/chroma tables. Reconstruct predictive groups from inverse-quantized residuals and MMAP, optional block prediction, or per-component MPP; clamp in the component domain. MMAP uses filtered prior-line neighbors and within-group residuals. BP selection must be computed identically from the previous line, not guessed from pixels in the current group.
6. For ICH groups, resolve all indices against the same history snapshot, including the seven previous-line neighbors. Update MRU order only after the group, handling repeated indices by last occurrence. P-mode inserts reconstructed pixels. Keep full reconstruction separate from rounded line-buffer values.
7. Count actual consumed syntax bits and the ideal residual coding size. Evolve buffer fullness, fractional drain, chunk alignment, RC offset/scale and delayed range selection. Apply DSC 1.1 short-term rules and signaled flatness at their exact group boundaries. This state controls later entropy sizes; it is part of decoding, not merely an encoder-quality heuristic.
8. Convert reconstructed YCoCg-R to clipped 8-bit RGB, write rows to the output image, update line storage and repeat. Consume and validate slice-tail padding according to the selected transport convention. Errors return status; never exit the embedding process, conceal an error as a successful image, or read invented zero bytes beyond input.

## Implementation and validation contract

Plain C library and CLI, libc-only runtime. Original implementation under MIT; preserve kernel MIT notices. Use byte reads and checked size arithmetic. Reject unsupported 10/12bpc/chroma/version combinations explicitly. Include a libFuzzer entry with the first parser/decoder implementation, cap output and work, and run ASan/UBSan on malformed and valid streams. Finite fuzzing cannot prove no input ever crashes.

Single-slice is the first correctness gate. At least flat, gradient and cross-slice checkerboard vectors must have documented bit construction and independently derived reconstruction, not decoder-generated expected output. DSC is lossy: expected reconstruction is not automatically the original source pixels. Test partial groups, line-buffer precision, ICH transitions, RC evolution and truncated reservoirs. The reference model may serve as an additional oracle but does not replace the three hand-derived vectors.

Implementation status and final freshness/test results will be appended after Phase 0. No implementation had been written when this document was created.

## Track A implementation checkpoint — 2026-09-16

Implementation began only after Phase 0 and the licensing/erratum follow-up were
written, as the independent Track A. No driver was
edited. No reference-model source was consulted to implement or resolve
ambiguities in this track. The earlier researcher's exposure remains disclosed;
this is an independently expressed implementation, not a formal clean-room claim.
One text extraction of an erratum, made for the rate-control work, included the
model source excerpt attached after its prose; that excerpt was not used. The shipped project
contains no reference-model code or binary.

### Delivered functionality and files

* `include/drm/display/drm_dsc.h`: byte-identical kernel definition file from the
  pinned commit; `drm_dp.h` provides minimal userspace types.
* `include/dsc.h`, `src/pps.c`: bounded PPS parser, kernel configuration layout,
  statuses, original public library API. Parsing handles version1.1/1.2 fields;
  decoding supports only1.1.
* `src/decode.c`: component substream reservoirs, modified unary entropy parsing,
  inverse-quantized reconstruction integration, color conversion, independent
  slices, chunk-interleaved frame assembly and edge cropping.
* `src/predict.c/.h`: MMAP/MPP, ICH state, history MRU updates, line storage.
* `src/rate_control.c/.h`: prose-based RC buffer, offset/scale, QP selection and
  flatness interpretation. Several interoperability uncertainties remain.
* `src/main.c`, `Makefile`: `libdsc.a` and libc-only `dscdecode` CLI. PPS+raw
  bitstream to binary RGB PPM; `--slice` supports independent slice debugging.
* `tests/`: six hand-constructed fixtures, independent expected PPMs, readable
  syntax transcripts, provenance/hashes, CLI verification and arithmetic RC tests.
* `fuzz/fuzz_decode.c`: libFuzzer entry added with the initial decoder;
  `afl_main.c` adapts the same entry for AFL; `smoke.c` supplies a deterministic
  sanitizer campaign. No reference-model differential harness had run at
  this checkpoint; M2 added `tools/compare_model`.
* `README.md`, `LICENSE`, `THIRD_PARTY.md`: build, scope, licenses and attribution.

### Specification ambiguities and disposition

The detailed logs are [RC ambiguities](research/rc-ambiguities.md) and
[prediction ambiguities](research/prediction-ambiguities.md). The following
were the M1 release qualifications. Rows marked "M2" were changed later; the
current disposition of every question is in the "Open questions" table at
the top.

| Item | Prose/errata evidence and current disposition |
|---|---|
| Line storage rounding overflow | DSC1.1 §6.3 includes saturation omitted by §7.4. Prediction/reconstruction must agree with encoder (§7.5), so use the explicit saturating §6.3 rule. Resolved without model. |
| BP SAD final reduction | DSC1.2a E1, SCR adopted2017-08-03, proposed change §6.4.4.1 removes the erroneous combined clamp; DSC1.2b carries the corrected equation. This resolves the reduction contradiction from errata prose. |
| BP samples left of slice | Prose does not settle reference samples at negative x during search. The ambiguity can affect selected BP vectors; the boundary counterexample is in the prediction log. Speculative BP code was removed. Both decode APIs return UNSUPPORTED for BP-enabled PPS. M2: BP is implemented, with this question as OQ-4 (`bp_left`). |
| RC increment comparison | Follow DSC1.1 Fig6-13's printed `curQp < prev2Qp`; do not apply the Phase0 model-derived opposite branch. No checked prose erratum corrects this direction. M2: OQ-5 (`incr_order`); the default changed after black-box comparison. |
| RC latency | Rendered DSC1.1 Fig6-8 and DSC1.2 Fig6-13 label group0 metrics → entropy QP for group2. The implementation queues the result accordingly. Stage-level wording permits another reading; independent dynamic-stream validation remains necessary. M2: OQ-11 (`rc_pipeline`); the default changed after black-box comparison. |
| Flatness scheduling | §6.6.3 explicitly signals a supergroup beginning two groups after its flag, with metadata in the intervening group. Parser follows this advance timing. Interaction between overridden QP and a pending RC result is unresolved. M2: OQ-1 (`flat_restart`); the default changed after black-box comparison. |
| Fractional drain, partial groups, thresholds | Follow printed actual-pixel drain, three-pixel target, mathematical floor and strict threshold comparison. Hand arithmetic tests cover selected cases, not all normative boundary behavior. M2: OQ-2, OQ-3 and OQ-15; the partial-group target default changed after black-box comparison. |
| Narrow ICH neighbor window | Widths below7 cannot supply the specified seven previous-line pixels. Such neighbor references are rejected; valid P/history references remain usable. |
| Offline error behavior | Returns an error rather than manufacturing unspecified concealment pixels. A failed slice does not leak state into independently decoded slices. |

These limitations meant **M1 was not complete**. The checkpoint executed the
requested core pipeline and synthetic tests; BP, VBR and general RC
interoperability were outstanding at that date. M2 implemented BP and compared
the decoder with the reference model (see "Continuation" above and
`PROGRESS.md`); VBR is still outstanding. The decoder is not an offline
conformance oracle. Track B did not block writing, building, fuzzing or
shipping this checkpoint; the limitations arose from the permitted evidence
and the implemented scope.

### Validation results

| Vector | Geometry | Independent expectation | Result |
|---|---|---|---|
| flat |96×3, one slice | RGB(128,128,128) | Exact PPM match |
| gradient |96×3, one slice | Gray80+x+y | Exact PPM match |
| checker |192×3, two96×3 slices | Gray120/136,5-pixel cells crossing slice boundary | Exact PPM match |
| mpp |96×3, one slice | Explicit0,255,0 first pixels, then128 | Exact PPM match |
| ich |96×3, one slice | Gray128 with explicit P→ICH and continued ICH coding | Exact PPM match |
| color_crop |187×5, four95×3 slices | Independently specified RGB ramps; cropped right/bottom padding | Exact PPM match |

All six vectors deliberately pin QP0. Their expected output is computed directly
from image formulas, not by this decoder. See `tests/VECTORS.md` for exact
construction and limitations. The colored case also exercises partial groups.

*14 CLI checks passed*: six exact frames, an independent single slice, six
truncated-input rejections, and short-PPS rejection. The same checks passed with
GCC AddressSanitizer and UndefinedBehaviorSanitizer.

`tests/test_rc.c` additionally passed under ASan/UBSan: hand-calculated nonzero
QP transitions, flatness adjustment, fractional8+1/16-bpp chunk padding and RC
error rejection. It tests the documented prose interpretation and does not
establish agreement with the normative C model.

The final deterministic sanitizer campaign completed **240,000 mutated-input
executions** across the six PPS+bitstream seeds, with no detected address or
undefined-behavior violation. This is finite evidence, not a proof of safety or
an execution of libFuzzer/AFL coverage guidance. Clang/AFL were unavailable in
the environment, so their shipped entry points were not exercised by those
engines. GCC sanitizer instrumentation was used instead. LeakSanitizer was
disabled because this execution environment does not support its tracing.

Bounds are checked before allocations and indexing, entropy loops are capped by
component widths, and pixel loops are capped by16Mi pixels. Production code uses
libc only. Final strict C11 build uses `-Wall -Wextra -Wpedantic -Wshadow
-Wconversion -Werror`. No driver or hardware validation is claimed.

Upstream freshness: the pre-validation remote-master recheck still returned
`9b87fdc9af2fbfcdb5c24a64139685ef80f6573f`; neither requested DSC source file
changed. The vendored header was byte-compared with that checkout.
## Track B — official VESA acquisition

### Registration record, 2026-09-23

* Registration: 2026-09-23. VESA Public Standards Download Registration,
  product-development path.
* Terms: Implementer's License Agreement (Exhibit D of VESA Policy 200D).
* Archive: `Display Stream Compression (DSC).zip`
* SHA-256: `4d8058e817bc71d41e5f87445979ca6b8f54ea83dd08870c42d158b617d02be2`
* The archive, the specification, and the reference model are not in this
  repository. The reference model is used only as a black box.

### History

* 2026-09-16 (M1). The official route was known: VESA's
  [Display Compression Codecs page](https://vesa.org/vesa-display-compression-codecs/)
  leads to a public registration form, which separates reference-only
  downloads from product development and links an implementer license
  agreement for the latter. The published agreement had been read (next
  section). M1 had no officially obtained archive. It worked from unofficial
  copies of the DSC 1.1 specification, the E1 errata and the C model (see
  "Sources" and `THIRD_PARTY.md`), and it recorded no comparison against the
  model.
* 2026-09-23 (M2). Registration completed on the product-development path,
  under the Implementer's License Agreement (record above). From M2 onward the
  officially obtained model is used only as a black box; comparison results
  are in `PROGRESS.md`.

### Published agreement: exact location and meaning

The complete, verbatim source is [Standards Implementor License Agreement, Exhibit D of VESA Policy 200D, §§1–2](https://vesa.org/wp-content/uploads/2024/05/Standards-Implementor-License-Agreement.pdf), pages 1–2. Refer to that original for exact language, including its placeholders and formatting defects; this report does not silently replace it with BSD-3-Clause text.

Section 1 makes (a) applicable to text, makes (b) applicable to Software included in the Specification as defined in Policy 200D, and gives (b) priority over inconsistent (a). Section 1(a) authorizes using the Specification to develop and distribute products that implement it; it excludes sublicensing/modifying the Specification itself and distinguishes implementing products from derivatives of the Specification.

Section 1(b) includes this condition: **“Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.”** It also requires corresponding notices/conditions/disclaimer in materials accompanying binary redistribution and prohibits endorsement using copyright-holder/contributor names without written permission. It identifies its license as copyright-only and refers patent terms to Policy 200D. Section 2 disclaims warranties and liability. Section 4 provides breach notice and a 30-day cure mechanism (with an exception for incurable breaches); Section 6 warrants registration accuracy.

Crucially, the PDF has the placeholder `Copyright <year> All Rights Reserved` and does **not** contain the familiar explicit BSD grant permitting redistribution and use with or without modification. Its conditions contemplate redistribution, but it is not safe to label this document an exact BSD-3-Clause license or replace its language with that license. The agreement text alone does not identify which files in the package constitute the licensed Software, or reconcile third-party contribution notices inside those files.
