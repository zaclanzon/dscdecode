# Rate-control implementation from specification text

This implementation was written from DSC 1.1 §§6.8, 7.3, Figures 6-8,
6-12 and 6-13, and the prose portion of the DSC 1.1 fractional-bpp-underflow
SCR adopted September 3, 2015. No reference-model implementation was used
as the algorithm source. The text extracted from the erratum also contained
the code excerpt attached after its normative prose; that excerpt was not
used to resolve any implementation choice.

## Explicit choices and remaining uncertainty

* **Entropy QP latency.** The rendered Figure 6-8 connects group 0's coded
  size/activity to the QP used for entropy-decoding group 2. A pending-QP
  slot implements this two-group relationship. Each completed group runs
  both RC stages, making their result available two groups after the
  measured group. No reference-model timing was substituted. Both initial
  entropy QPs are zero, a startup interpretation that still needs an
  independent interoperability vector; the prose does not enumerate all
  startup registers.
  Rechecking DSC 1.2 Figure 6-13 yields the same diagram: the line labeled
  group 0's coded size and activity branches around the long-term block
  into the short-term block whose output is explicitly for group 2. A
  competing interpretation would delay only range selection and use newer
  group metrics for short-term RC, but the diagram does not draw that
  connection. The implementation retains the literal diagram interpretation;
  neither the errata prose nor the DSC 1.2 prose resolves the competing
  interpretation beyond that. No production code was changed on re-review.
* **Flatness.** §§6.6.3 and 7.3 describe advance signaling. The parser must
  schedule the indicated group's override before that group's entropy
  decode, using `dsc_rc_apply_flat`. The override uses §6.8.5.2's printed
  formulas and also supplies the starting QP for the next short-term RC
  calculation. How this restart interacts with an already pending QP is
  not explicitly specified by the prose. The current implementation
  updates the latest RC starting QP without changing the pending result;
  this remains an interoperability uncertainty, not a proved resolution.
* **QP increment direction.** Figure 6-13 prints `curQp < prev2Qp` for the
  branch containing increment limit 0. The implementation follows that
  inequality literally. Neither the fractional-bpp SCR nor the PPS-guidance
  SCR supplies a prose correction to it. Claims in earlier research that
  depend on inspecting the reference model are deliberately not applied.
* **Partial-group target.** §6.8.1 removes bits for actual pixels, whereas
  §6.8.4 explicitly defines the target using three times bits-per-pixel.
  The implementation preserves that distinction. Padded residuals do not
  consume image pixel times.
* **Fractional chunk accounting.** Each eligible post-delay pixel removes
  its integer-plus-carried-fraction allocation. A completed chunk removes
  the additional zero padding needed to reach `chunk_size * 8`, and resets
  the fractional accumulator. The delay test is the printed `>=` in
  §6.8.1. This represents the chunk boundary algorithm as a pixel counter
  rather than copying the prose's group-split pseudocode, whose arithmetic
  grouping and counter update are ambiguous. Fractional-bpp interoperability
  is not established by integer-bpp synthetic tests.
* **Scale and offset clock.** §6.8.2 says offset corrections superpose in
  Q11 and scale decrements every programmed interval until unity. It does
  not confine decrementing to the first line, so neither does this code.
  The end-of-slice increment trigger takes effect on the group immediately
  following the qualifying group, matching the explicit timing language.
  Initial-delay offset subtraction uses the number of actual pixels still
  within the delay, capped by the group's actual size.
* **Negative arithmetic.** Q11 and transformed fullness use mathematical
  floor, avoiding implementation-defined right shift of negative values.
  Threshold comparison is strict `>`; exact thresholds stay in the lower
  range. Figure 6-11 does not label equality explicitly; this convention
  needs a boundary vector.
* **Buffer errors.** Negative CBR model fullness and positive transformed
  model fullness are rejected. The maximum untransformed buffer is bounded
  by the rate times combined transmit/decode delay. VBR is rejected at
  initialization, since the CLI currently supplies only fixed-size CBR
  chunk framing.

The code is a bounded executable interpretation of the prose, not a claim
that these ambiguities have disappeared. In particular, flatness with RC
changes, nonzero RC startup transitions, exact range thresholds, fractional
bits-per-pixel, and short partial groups require independent coverage before
claiming general DSC interoperability.

`tests/test_rc.c` provides fixed, independently hand-calculated traces for
nonzero QP generation/delay, zero-residual decrements, one immediate flatness
adjustment, fractional-bit chunk accounting including partial groups, and
buffer/order errors. They test these interpretations, not VESA-model
equivalence. In particular, they do not establish the unresolved flatness
interaction with queued QPs.

## M2 note (2026-09-23)

Each uncertainty above is now a row of the "Open questions" table in
RESEARCH.md, with a runtime reading switch where the text supports more than
one reading: flatness restart (OQ-1), threshold equality (OQ-2), fractional
accounting (OQ-3), increment direction (OQ-5), RC latency (OQ-11), scale
decrement start (OQ-14) and partial-group target (OQ-15). Several defaults
differ from the choices described above, which record the M1 implementation.
The table gives the current default of each and the evidence for it.
