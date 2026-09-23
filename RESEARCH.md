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

| ID | Question | Spec reference | Current implementation | Status |
|---|---|---|---|---|
| OQ-1 | Flatness restart ordering. When a flatness override changes masterQp, does the new value only seed the next short-term RC calculation, or does it also replace the QP already queued for a later group? | DSC 1.1 §6.8.5.2, Figure 6-8, §7.3 | Override seeds the next short-term calculation; the queued QP is untouched | Open |
| OQ-2 | Range threshold equality. When rcModelFullness equals a threshold exactly, is it in the lower or the upper range? | DSC 1.1 §6.8.3, Figure 6-11 | Strict comparison; equality stays in the lower range | Open |
| OQ-3 | Fractional bits_per_pixel. Where does the fractional-bit accumulator reset, and how are chunk padding bits counted against the buffer model? | DSC 1.1 §6.8.1, as amended by the fractional-bpp underflow SCR in DSC 1.1 E1 | Accumulator and chunk counters reset after every slice_width pixel times following the initial delay; padding tops each chunk up to chunk_size×8 bits | Open |
| OQ-4 | Block prediction references left of the slice edge during the BP search | DSC 1.1 §6.4.2 and §6.4.4 (DSC 1.2 numbering §6.4.4.1) | BP-enabled PPS rejected | Open; see research/prediction-ambiguities.md |
| OQ-5 | QP increment comparison direction. Figure 6-13 prints the rc_quant_incr_limit0 branch for curQp below prev2Qp; M1 recorded the opposite ordering from model source | DSC 1.1 §6.8.4, Figure 6-13 | Printed figure, literally | Open |
| OQ-6 | Decrement floor after a group with zero residuals | DSC 1.1 §6.8.4, Figure 6-12 | Floor is half of minQp; the 1.1 figure supports this reading | Open, awaiting black-box confirmation |
| OQ-7 | How first-line and second-line target offsets combine | DSC 1.2 §6.8.2 (second-line terms are 1.2 only) | 1.1 PPS carries no second-line fields | Open; outside the 1.1 decode scope |
| OQ-8 | Line storage saturation, present in §6.3 and absent from §7.4 | DSC 1.1 §6.3, §7.4, §7.5 | Saturate as in §6.3 | Open, awaiting black-box confirmation; prose argument in research/prediction-ambiguities.md |
| OQ-9 | forceMpp chunk-bit comparison scaling | DSC 1.1 §6.8.1 and E1 | Encoder-side decision; the decoder does not compute forceMpp | Open; no decoder effect expected |
| OQ-10 | BP SAD reduction (clip then sum, or sum then shift) | DSC 1.2 §6.4.4.1, DSC 1.2a E1, DSC 1.2b §6.4.4.1 | BP not implemented at M1 | Open; errata prose supports sum then shift |
| OQ-11 | RC latency and startup. Group N's coded size sets the QP used for group N+2, and the first two groups decode at QP 0 | DSC 1.1 Figure 6-8, §7.3 | As described | Open |

## Continuation — September 20, 2026

**M1 remains incomplete.** This continuation added two nonzero-QP/flatness
image fixtures, prediction sample traces, and a partial-group syntax-validation
fix. BP and VBR remain unsupported. The original research and historical
checkpoint below are preserved; this section records the current state.

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
were consulted for this continuation. No driver was modified. Track B's
official acquisition and license-applicability status is unchanged.

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
pending and produce QP 8 for the next calculation, so the unresolved restart
ordering cannot be distinguished by those fixtures. Exact threshold equality,
general fractional-rate interoperability, and reference/hardware agreement are
still open. BP's missing left-boundary sample convention remains documented in
`research/prediction-ambiguities.md`; no guessed convention was introduced.
VBR framing and buffer accounting are still unimplemented. These limitations
prevent an M1 completion or conformance claim.

The supplied checkpoint has no Git metadata or repository remote, and the local
workspace is not a Git repository. This continuation is delivered as source
files and an archive; no upstream repository or PR was created.

## Sources and immutable revisions

- Linux master: **9b87fdc9af2fbfcdb5c24a64139685ef80f6573f**, commit date 2026-09-15, fetched from torvalds/linux. [Helper](https://github.com/torvalds/linux/blob/9b87fdc9af2fbfcdb5c24a64139685ef80f6573f/drivers/gpu/drm/display/drm_dsc_helper.c), [definitions](https://github.com/torvalds/linux/blob/9b87fdc9af2fbfcdb5c24a64139685ef80f6573f/include/drm/display/drm_dsc.h). Both files read end to end (1566 and 602 lines).
- NVIDIA: **61dcc93722ecb418bb5f2e00923f05b4b8051dd1**, release 615.71.09, 2026-09-09. [nvt_dsc_pps.c](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/61dcc93722ecb418bb5f2e00923f05b4b8051dd1/src/common/modeset/timing/nvt_dsc_pps.c). PPS generation, packing, validation, slice selection and lookup matrices inspected.
- VESA DSC 1.2, 20 January 2016 (unofficial copy; link removed), 146 pages. SHA-256 `6574d6400416da2e714ecdf5de888cf3160a7f6d2340e1bd36ef91b4987ca828`. Sections 4, 6, 7 and Annex E supply syntax, reconstruction, rate control and parameter guidance. Figure 6-18 also inspected as a rendered page.
- DSC 1.1 PDF and DSC 1.1 E1 errata, together with a copy of the VESA C model, were obtained from an unofficial public mirror (link, mirror revision, and model archive name and hash removed). DSC 1.1 PDF SHA-256 `9f5a1a54601bc5ed9f58edd5f8878f1c14114a291ce96ea3629d63e7efb4da65`.
- [Official VESA distribution entry](https://vesa.org/vesa-display-compression-codecs/) links a registration form and explicitly lists normative C code. The downloaded copies above are mirrors, not authenticated downloads from VESA. No form was submitted.
- [NVIDIA issue 1039](https://github.com/NVIDIA/open-gpu-kernel-modules/issues/1039) is a report to investigate, not a normative source or proof of causality.

The specification access gate is satisfied. The normative C model includes a decoder (`FUNCTION=2`); therefore the claim that no publicly obtainable software decoder exists is too broad. A small, independently written, conventionally open-source, bounded-input decoder remains useful. No reference-model implementation text has been copied into a new library; no decoder implementation exists yet.

## Follow-up research: model availability, license and reuse decision

Updated 2026-09-16, before implementation or driver edits.

### Availability and exact terms

**The model is publicly obtainable, not VESA-member-only.** The [official DisplayPort FAQ](https://www.displayport.org/faq/) explicitly answers its membership question by saying that DSC 1.2a, including codec C sources, is freely available from VESA. The [VESA codec page](https://vesa.org/vesa-display-compression-codecs/) leads to [public registration](https://fs16.formsite.com/VESA/form714826558/secure_index.html). That page separates reference downloads from product development: the latter links an implementer agreement. Official availability involves registration; it is not an anonymous ZIP link. No registration or agreement has been submitted on the user's behalf.

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
  sanitizer campaign. No reference-model differential harness has run.
* `README.md`, `LICENSE`, `THIRD_PARTY.md`: build, scope, licenses and attribution.

### Specification ambiguities and disposition

The detailed logs are [RC ambiguities](research/rc-ambiguities.md) and
[prediction ambiguities](research/prediction-ambiguities.md). The following
are important release qualifications:

| Item | Prose/errata evidence and current disposition |
|---|---|
| Line storage rounding overflow | DSC1.1 §6.3 includes saturation omitted by §7.4. Prediction/reconstruction must agree with encoder (§7.5), so use the explicit saturating §6.3 rule. Resolved without model. |
| BP SAD final reduction | DSC1.2a E1, SCR adopted2017-08-03, proposed change §6.4.4.1 removes the erroneous combined clamp; DSC1.2b carries the corrected equation. This resolves the reduction contradiction from errata prose. |
| BP samples left of slice | Prose does not settle reference samples at negative x during search. The ambiguity can affect selected BP vectors; the boundary counterexample is in the prediction log. Speculative BP code was removed. Both decode APIs return UNSUPPORTED for BP-enabled PPS. |
| RC increment comparison | Follow DSC1.1 Fig6-13's printed `curQp < prev2Qp`; do not apply the Phase0 model-derived opposite branch. No checked prose erratum corrects this direction. |
| RC latency | Rendered DSC1.1 Fig6-8 and DSC1.2 Fig6-13 label group0 metrics → entropy QP for group2. The implementation queues the result accordingly. Stage-level wording permits another reading; independent dynamic-stream validation remains necessary. |
| Flatness scheduling | §6.6.3 explicitly signals a supergroup beginning two groups after its flag, with metadata in the intervening group. Parser follows this advance timing. Interaction between overridden QP and a pending RC result is unresolved. |
| Fractional drain, partial groups, thresholds | Follow printed actual-pixel drain, three-pixel target, mathematical floor and strict threshold comparison. Hand arithmetic tests cover selected cases, not all normative boundary behavior. |
| Narrow ICH neighbor window | Widths below7 cannot supply the specified seven previous-line pixels. Such neighbor references are rejected; valid P/history references remain usable. |
| Offline error behavior | Returns an error rather than manufacturing unspecified concealment pixels. A failed slice does not leak state into independently decoded slices. |

These limitations mean **M1 is not complete**. This checkpoint executes the
requested core pipeline and synthetic tests, but BP/VBR and general RC
interoperability are outstanding. It is not an offline conformance oracle yet.
Track B has not blocked writing, building, fuzzing or shipping this checkpoint;
the limitations arise from the permitted evidence and actual implemented scope.

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
## Track B — official VESA acquisition, 2026-09-16

### Registration record, 2026-09-23

* Registration: 2026-09-23. VESA Public Standards Download Registration,
  product-development path.
* Terms: Implementer's License Agreement (Exhibit D of VESA Policy 200D).
* Archive: `Display Stream Compression (DSC).zip`
* SHA-256: `4d8058e817bc71d41e5f87445979ca6b8f54ea83dd08870c42d158b617d02be2`
* The archive, the specification, and the reference model are not in this
  repository. The reference model is used only as a black box.

### Status as of 2026-09-16, superseded by the registration record above

**Status: pending official acquisition; redistribution coverage not established. Track A is not blocked.** No official codec archive was acquired in this attempt, no agreement submitted, and no reference model was run or consulted for implementation. This is an unresolved acquisition/licensing branch, not a finding that VESA prohibits redistribution.

### Official channel and actual stopping point

VESA's official [Display Compression Codecs page](https://vesa.org/vesa-display-compression-codecs/) advertises free standards and normative C code. Its Download Now link leads to [Public Standards Download Registration](https://fs16.formsite.com/VESA/form714826558/secure_index.html). The registration page displays standards selection (including DSC), first name, last name, company, country, and email. It distinguishes reference-only downloads from product development and links the latter to the [Standards Implementer License Agreement form](https://fs16.formsite.com/VESA/teamftyax1/index).

The public pages and agreement were successfully retrieved through web lookup. A subsequent read-only browser inspection of the implementer form stalled and was cancelled; no submission or download followed. There is no basis to claim that the final official package has the same notices as the previously examined mirror.

A truthful registration email and the user's intended registrant/company capacity have not been supplied for this acquisition. They must not be invented. The agreement says form submission creates the binding agreement and represents authority to bind an entity if one is named. The browser's control-browser skill additionally requires confirmation at action time before accepting a legally binding agreement, even where ordinary downloading is authorized. Consequently the acquisition is not complete; the form was not submitted. No email was sent to VESA.

### Published agreement: exact location and meaning

The complete, verbatim source is [Standards Implementor License Agreement, Exhibit D of VESA Policy 200D, §§1–2](https://vesa.org/wp-content/uploads/2024/05/Standards-Implementor-License-Agreement.pdf), pages 1–2. Refer to that original for exact language, including its placeholders and formatting defects; this report does not silently replace it with BSD-3-Clause text.

Section 1 makes (a) applicable to text, makes (b) applicable to Software included in the Specification as defined in Policy 200D, and gives (b) priority over inconsistent (a). Section 1(a) authorizes using the Specification to develop and distribute compliant products; it excludes sublicensing/modifying the Specification itself and distinguishes implementing products from derivatives of the Specification.

Section 1(b) includes this condition: **“Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.”** It also requires corresponding notices/conditions/disclaimer in materials accompanying binary redistribution and prohibits endorsement using copyright-holder/contributor names without written permission. It identifies its license as copyright-only and refers patent terms to Policy 200D. Section 2 disclaims warranties and liability. Section 4 provides breach notice and a 30-day cure mechanism (with an exception for incurable breaches); Section 6 warrants registration accuracy.

Crucially, the PDF has the placeholder `Copyright <year> All Rights Reserved` and does **not** contain the familiar explicit BSD grant permitting redistribution and use with or without modification. Its conditions contemplate redistribution, but it is not safe to label this document an exact BSD-3-Clause license or replace its language with that license. The text available here is not evidence identifying which files in an as-yet-unacquired package constitute the licensed Software, or reconciling third-party contribution notices inside those files.

### Decision for this checkpoint

Do not enable the model encoder as a vector generator or decoder as an oracle yet. The user's permitted-redistribution branch is not established. Keep the fresh implementation and synthetic/fuzz work independent; record no differential-test results. Once official acquisition is completed, record archive URL, version, hash, actual package notices and their scope, and the agreement accepted for that acquisition before deciding whether the model can be used under the user's strategy.

The user can complete the verified official registration route independently and supply the downloaded archive plus the terms accepted, or supply accurate registration details and approve agreement submission at the final action. This is a Track B continuation only and does not suspend Track A.
