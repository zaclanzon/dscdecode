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

Phase 5 (2026-09-23) ran the VESA C model as a black box (rule: command line
and output files only; see `PROGRESS.md`). A question marked resolved had one
reading that reproduced the model's own decode bit-exactly on every model
stream and discriminator that exercises it, while the other reading diverged.
The default switch value is that reading, and the other reading stays
available behind the same switch. "Stream set" means the 92 streams the
model decoded in Phase 5 (`PROGRESS.md`): 82 pictures the model itself
encoded (the 42-case matrix, 30 flatness pictures, 7 validation pictures, 3
partial-group ramps) and the 10 exact-image fixtures. Counts per question are
in `PROGRESS.md`.

| ID | Question | Spec reference | Current implementation | Status |
|---|---|---|---|---|
| OQ-1 | Flatness restart ordering. When a flatness override changes masterQp for group k, §6.8.5.2 says the new value seeds the short-term RC of the next RC cycle, without saying which cycle of the Figure 6-8 pipeline is next. Reading A (next-cycle): the cycle run after group k is decoded (it sets group k+2's QP) starts from the override; the QP already queued for group k+1 stands. Reading B (in-flight): with the Figure 6-8 pipeline, the cycle that sets group k+1's QP runs while group k decodes, so it is re-run from the override. | DSC 1.1 §6.8.5.2, Figure 6-8, §7.3 | `flat_restart=in-flight` (default) or `next-cycle` (M1 behavior) | Resolved by black-box comparison with the VESA C model: in-flight. Discriminator `oq1_flat_restart`; also the Phase 5 stream set. |
| OQ-2 | Range threshold equality. When rcModelFullness equals a threshold exactly, is it in the lower or the upper range? Figure 6-11 draws the boundaries without saying which side owns them. | DSC 1.1 §6.8.3, Figure 6-11 | `threshold_eq=lower` (default, M1 behavior) or `upper` | Resolved by black-box comparison with the VESA C model: lower. Discriminator `oq2b_threshold_equality`, built under the current readings of the other rate-control questions; also the Phase 5 stream set. The earlier discriminator `oq2_threshold_equality` is superseded by `oq2b_threshold_equality`: it was built with the M1 readings of OQ-5, OQ-11 and OQ-14 to OQ-18, which the model does not follow, so it cannot decide the question. It remains a decoder-side test under the readings it assumes. |
| OQ-3 | Fractional bits_per_pixel. The printed §6.8.1 pseudocode resets the fractional accumulator when (pixelCount − initial_xmit_delay) is a multiple of slice_width, which includes the first pixel after the delay. Reading A (chunk): reset when a chunk of slice_width pixel times completes, as the framer's chunk accounting and §6.8.1's remark that the accumulator restarts for every slice line suggest. Reading B (literal): reset exactly where the pseudocode prints it. They differ by at most one bit of buffer fullness. The E1 fractional-bpp SCR changes only the encoder's forceMpp condition. | DSC 1.1 §6.8.1; DSC 1.1 E1, fractional-bpp underflow SCR | `frac_reset=chunk` (default, M1 behavior) or `literal` | Resolved by black-box comparison with the VESA C model: chunk. Discriminator `oq3_fractional_bpp`, and the fractional-rate model streams (7.5 and 9.3125 bpp) of publication step P2 in `PROGRESS.md`. The Phase 5 stream set uses whole-number bpp and cannot separate the readings. |
| OQ-4 | Block prediction references left of the slice. The §6.4.4.1 search compares previous-line samples out to hPos−16, and the searches at hPos 0–15 reach positions left of the slice, which the text does not define. They matter: bpCount increments from hPos 9, so those positions can decide the first BP groups (hPos 15–21). Reading A (replicate): such samples repeat the slice's first previous-line sample, as §6.4.1 prescribes for the MMAP filter on the previous line. Reading B (midpoint): they take the component midpoint, as MMAP and MPP do for the first group of a line. The same convention supplies the left neighbor of sample 0 in the edge test. | DSC 1.1 §6.4.2, §6.4.4.1, §7.5.2.1; §6.4.1 for the replication precedent | `bp_left=midpoint` (default) or `replicate`. Worked examples in research/bp-worked-note.md | Resolved by black-box comparison with the VESA C model: midpoint. Discriminator `oq4_bp_left`; also the Phase 5 stream set. |
| OQ-5 | QP increment comparison direction. Figure 6-13 prints the rc_quant_incr_limit0 branch for curQp below prev2Qp. Reading A (printed): as printed. Reading B (swapped): the limit0 branch applies when curQp is above prev2Qp, the ordering M1 recorded from model source. | DSC 1.1 §6.8.4, Figure 6-13 | `incr_order=swapped` (default) or `printed` | Resolved by black-box comparison with the VESA C model: swapped. No discriminator; evidence: the Phase 5 stream set. Unit test `increment_order`. |
| OQ-6 | Decrement floor after a group with zero residuals | DSC 1.1 §6.8.4, Figure 6-12 | Floor is half of minQp; the 1.1 figure supports this reading. No switch | Resolved by black-box comparison with the VESA C model: half of minQp. No discriminator; evidence: the Phase 5 stream set, decoded by a scratch build (outside the repository) that floors at minQp. |
| OQ-7 | How first-line and second-line target offsets combine | DSC 1.2 §6.8.2 (second-line terms are 1.2 only) | 1.1 PPS carries no second-line fields | Open; outside the 1.1 decode scope |
| OQ-8 | Line storage saturation, present in §6.3 and absent from §7.4 | DSC 1.1 §6.3, §7.4, §7.5 | Saturate as in §6.3. No switch; prose argument in research/prediction-ambiguities.md | Resolved by black-box comparison with the VESA C model: saturate. It matters only when line_buf_depth is below 9. Evidence: Phase 5 pictures encoded with line_buf_depth 8, decoded by a scratch build (outside the repository) without the clamp. |
| OQ-9 | forceMpp chunk-bit comparison scaling | DSC 1.1 §6.8.1 and E1 | Encoder-side decision; the decoder does not compute forceMpp | Open; no decoder effect, so decoding cannot observe it |
| OQ-10 | BP SAD reduction. The 1.1 prose says three 9-bit partial SADs are summed and the three LSBs dropped; 1.1's printed formula instead clips the sum to 511 and drops nothing. DSC 1.2b §6.4.4.1 prints the corrected equation (sum, then shift by 3), and its revision history lists it as a correction to match the C model. | DSC 1.1 §6.4.4.1; DSC 1.2b §6.4.4.1 and revision history | `bp_sad=shift` (default, 1.1 prose and 1.2b) or `clip` (1.1 formula) | Resolved by black-box comparison with the VESA C model: shift. No discriminator; evidence: the block-prediction streams of the Phase 5 stream set. |
| OQ-11 | RC pipeline. Group N's coded size sets the QP used for group N+2, and the first two groups decode at QP 0. Reading A (same-group): the short-term RC for that cycle uses the range (minQp, maxQp, bpg offset) selected from the fullness after group N. Reading B (range-lag): the range is the one selected in the previous cycle, and the first cycle uses range 0. The figure shows the latency but not when range selection happens within it. | DSC 1.1 Figure 6-8, §6.8.3, §7.3 | `rc_pipeline=range-lag` (default) or `same-group` (M1 behavior) | Resolved by black-box comparison with the VESA C model: range-lag. No discriminator; evidence: the Phase 5 stream set. Unit test `range_pipeline`. |
| OQ-12 | Initial-delay boundary. §6.8.1 starts removing bits at the pixel where pixelCount equals initial_xmit_delay. §6.8.2 lowers rcXformOffset while the initial delay lasts, without a pixel-exact end. Reading A (inclusive): the offset falls for initial_xmit_delay pixels, pixelCount 1 to initial_xmit_delay, so one pixel both removes bits and lowers the offset. Reading B (exclusive): only the initial_xmit_delay − 1 pixels that remove no bits count. Annex E's `initial_xmit_delay × bits_per_pixel` budgets fit reading A but are not exact enough to decide. Found in M2 while designing the OQ-3 discriminator. | DSC 1.1 §6.8.1, §6.8.2, Annex E | `delay_offset=inclusive` (default, M1 behavior) or `exclusive` | Resolved by black-box comparison with the VESA C model: inclusive. No discriminator; evidence: the Phase 5 stream set. |
| OQ-13 | BP edge counter. BP needs lastEdgeCount below 3, the pixels passed since a step above 32 in any component. The decision uses only previous-line information, but the text does not say at which previous-line sample the count is read. Reading A (window): at hPos+2, the last sample of the search window and the current block in Annex D.2's description. Reading B (before): at hPos−1, the sample left of the group. Also implemented as M2 readings: bpCount starts each line at 0, since its hPos-below-9 rule is only meaningful per line; the search runs for every group, including ICH groups. | DSC 1.1 §6.4.4.1, Annex D.2 | `bp_edge=window` (default) or `before` | Resolved by black-box comparison with the VESA C model: window. No discriminator; evidence: the block-prediction streams of the Phase 5 stream set. The other M2 readings in this row have no switch; the defaults match those streams. |
| OQ-14 | Scale decrement start. When initial_scale_value is above 8, §6.8.2 lowers the scale once every scale_decrement_interval groups. Reading A (from-group-1): the interval is counted from the second group of the slice. Reading B (from-group-0): it is counted from the first group. | DSC 1.1 §6.8.2 | `scale_dec=from-group-0` (default) or `from-group-1` (M1 behavior) | Resolved by black-box comparison with the VESA C model: from-group-0. No discriminator; evidence: the Phase 5 stream set. Unit test `scale_decrement`. |
| OQ-15 | RC target for a partial group. The last group of a slice line can hold one or two pixels. Reading A (three): rcTgtBitsGroup is computed from 3 × bits_per_pixel for every group. Reading B (pixels): from the pixels actually in the group, matching the bits §6.8.1 removes for them. | DSC 1.1 §6.8.1, §6.8.4 | `partial_target=pixels` (default) or `three` (M1 behavior) | Resolved by black-box comparison with the VESA C model: pixels. No discriminator; evidence: the Phase 5 stream set. Unit test `partial_target`. |
| OQ-16 | Very-flat demotion. §6.8.5.2 notes that a flatness signal counts as somewhat flat when the current masterQp is below 7 (8 bpc), without saying which group's QP is current. The type bit is already omitted when the signaling group's QP is below 7. Reading A (group-qp): demote when the flagged group's own short-term QP is below 7. Reading B (as-signaled): the note restates the omitted bit, and the decoded type applies unchanged. Reading C (previous-qp): demote when the QP that decoded the group before the flagged one is below 7. Found in Phase 5, when multi-slice model streams failed to decode. | DSC 1.1 §6.8.5.2; §4.5 (Table 4-9) and §6.6.3 for the type bit | `very_flat=previous-qp` (default), `group-qp` (M1 behavior) or `as-signaled` | Resolved by black-box comparison with the VESA C model: previous-qp. No discriminator; evidence: the Phase 5 stream set, including pictures built for this question (flat patches in noise). Unit test `very_flat_type`. |
| OQ-17 | Partial-group padding. §6.6 has encoders clear a partial group's padding residuals and repeat its rightmost real ICH index, but does not say what a decoder does with other padding. Reading A (reject): other padding is a bitstream error. Reading B (accept): the padding is parsed, feeds size prediction, and is otherwise ignored. | DSC 1.1 §6.6, §7.8 | `partial_padding=accept` (default) or `reject` (M1 behavior). Under `accept`, the CLI prints one warning with the number of partial groups whose padding it accepted | Resolved by black-box comparison with the VESA C model: accept. Evidence: the model decodes fixtures `invalid_partial_residual` and `invalid_partial_ich` without error, and its output matches the accept reading. No discriminator. |
| OQ-18 | Flatness at the top QP. §6.8.5.2 makes no flatness adjustment when the current masterQp equals range 14's range_max_qp, with the same unspecified current QP as OQ-16. Reading A (own): the flagged group's short-term QP. Reading B (previous): the QP that decoded the group before the flagged one. Found in Phase 5, when two multi-slice model streams failed to decode. | DSC 1.1 §6.8.5.2 | `flat_max_qp=previous` (default) or `own` (M1 behavior) | Resolved by black-box comparison with the VESA C model: previous. No discriminator; evidence: the Phase 5 stream set. Unit test `flat_max_qp`. |

## Continuation — September 20, 2026

**M1 remained incomplete at this date.** This continuation added two
nonzero-QP/flatness image fixtures, prediction sample traces, and a
partial-group syntax-validation fix. BP and VBR were unsupported at this
point; M2 implemented BP, and VBR is still unsupported. The original research
and historical checkpoint below are preserved; this section records the state
on September 20.

### Recovered inputs and source discipline

The supplied `dsc-decoder-checkpoint.zip` was extracted into a local working
directory. Its `RESEARCH.md` is byte-identical to supplied `RESEARCH (2).md`
(SHA-256 `95eb8ebe83afb0e4803e5b77e857fe405782f58d4418f1f94876ca5d66d454bc`).
Supplied revisions (1) and (3) are identical older research snapshots.
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
written and the user explicitly authorized independent Track A. No driver was
edited. No reference-model source was consulted to implement or resolve
ambiguities in this track. The earlier researcher's exposure remains disclosed;
this is an independently expressed implementation, not a formal clean-room claim.
One erratum extraction in the RC subtask included attached source text after its
prose; the subtask explicitly did not use that excerpt. The shipped project
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

Section 1 makes (a) applicable to text, makes (b) applicable to Software included in the Specification as defined in Policy 200D, and gives (b) priority over inconsistent (a). Section 1(a) authorizes using the Specification to develop and distribute compliant products; it excludes sublicensing/modifying the Specification itself and distinguishes implementing products from derivatives of the Specification.

Section 1(b) includes this condition: **“Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.”** It also requires corresponding notices/conditions/disclaimer in materials accompanying binary redistribution and prohibits endorsement using copyright-holder/contributor names without written permission. It identifies its license as copyright-only and refers patent terms to Policy 200D. Section 2 disclaims warranties and liability. Section 4 provides breach notice and a 30-day cure mechanism (with an exception for incurable breaches); Section 6 warrants registration accuracy.

Crucially, the PDF has the placeholder `Copyright <year> All Rights Reserved` and does **not** contain the familiar explicit BSD grant permitting redistribution and use with or without modification. Its conditions contemplate redistribution, but it is not safe to label this document an exact BSD-3-Clause license or replace its language with that license. The agreement text alone does not identify which files in the package constitute the licensed Software, or reconcile third-party contribution notices inside those files.
