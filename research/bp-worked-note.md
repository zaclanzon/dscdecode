# Block prediction: worked derivations for the BP fixtures

M2, Phase 2. These derivations give the expected pixels of
`tests/fixtures/bp_left_edge` and `tests/fixtures/bp_slice_boundary`, and the
BP decisions a decoder must make to reproduce them. Arithmetic was done by
hand and checked afterwards with `tests/make_bp_vectors.py`. The rules come
from DSC 1.1 §6.4.2, §6.4.4.1 and §7.5.2.1, with the bpSad reduction as
corrected in DSC 1.2b §6.4.4.1 (OQ-10).

## Rules used

For a group whose first pixel is at hPos on a line other than the first:

1. The nine reference samples are the previous line at hPos−6 … hPos+2. For
   each candidate vector v in the order −1, −3, −4, −5, −6, −7, −8, −9, −10,
   each reference sample p is compared with the previous-line sample at p+v.
2. Per component, |difference| is shifted right by (bit depth − 7): by 1 for
   8-bit Y and by 2 for 9-bit Co and Cg. It is then capped at 63.
3. The capped values of three adjacent reference samples, all components
   together, form a 3×1 partial SAD capped at 511. The three partials are
   summed; bpSad drops the three low bits.
4. The lowest bpSad wins. A tie keeps the earlier candidate, which is the
   smaller magnitude. If −1 wins, bpCount becomes 0. Otherwise bpCount goes
   up by one, but only when hPos ≥ 9. bpCount starts every line at 0.
5. BP is used when bpCount ≥ 3, the group has three pixels, and an edge (a
   step above 32 in any component between neighboring previous-line samples)
   lies within the last three samples (OQ-13).
6. The BP prediction for pixel hPos+j is the reconstructed pixel
   hPos+j+v of the current line. Reconstruction is prediction plus residual
   × 2^qLevel, clamped (§6.4.6).

Consequences: BP is never used on a slice's first line. bpCount can reach 3
at hPos 15 at the earliest, after increments at hPos 9, 12 and 15.

## Colour patterns (§6.1 forward transform, chroma offset by 256)

Co = R − B, t = B + ⌊Co/2⌋, Cg = G − t, Y = t + ⌊Cg/2⌋.

| Name | RGB | Co | t | Cg | Y | (Y, Co+256, Cg+256) |
|---|---|---|---|---|---|---|
| A | (200, 40, 40) | 160 | 120 | −80 | 80 | (80, 416, 176) |
| B | (40, 200, 40) | 0 | 40 | 160 | 120 | (120, 256, 416) |
| C | (40, 40, 200) | −160 | 120 | −80 | 80 | (80, 96, 176) |
| D | (220, 220, 60) | 160 | 140 | 80 | 180 | (180, 416, 336) |
| E | (60, 220, 220) | −160 | 140 | 80 | 180 | (180, 96, 336) |
| F | (220, 60, 220) | 0 | 220 | −160 | 140 | (140, 256, 96) |
| G | (250, 130, 10) | 240 | 130 | 0 | 130 | (130, 496, 256) |
| H | (10, 250, 130) | −120 | 70 | 180 | 160 | (160, 136, 436) |
| I | (130, 10, 250) | −120 | 190 | −180 | 100 | (100, 136, 76) |

Row 0 of each pattern is A B C A B C …, row 1 is D E F …, row 2 is G H I ….

## bp_left_edge (36×3, one slice)

Source: pixel (x, y) is pattern row y at x mod 3. Every group is coded
losslessly at QP 0, so the expected output is the source itself.

### Search on line 1 (previous line = A B C …)

Per-sample capped differences for vector −1:

* at x ≡ 0 (A against C): Y 0, Co 320 >> 2 = 80 → 63, Cg 0; sum 63.
* at x ≡ 1 (B against A): Y 40 >> 1 = 20, Co 160 >> 2 = 40, Cg 240 >> 2 = 60; sum 120.
* at x ≡ 2 (C against B): Y 20, Co 40, Cg 60; sum 120.

Every 3×1 block holds one sample of each kind: 63 + 120 + 120 = 303. Three
blocks give 909, so bpSad(−1) = 909 >> 3 = 113. Under the 1.1 formula
reading (OQ-10) it is MIN(511, 909) = 511.

For v = −3, −6 or −9, a sample and its reference have the same x mod 3, so
the difference is 0 wherever both lie inside the slice. Vector −3's
references are hPos−9 … hPos−1, all inside from hPos 9 on. There, bpSad(−3)
is 0, the lowest possible, and −3 comes before −6 and −9. The winner is −3
whatever the other candidates score, including those that reach left of the
slice (OQ-4).

| hPos (line 1) | best vector | bpCount after | edge within last three | BP |
|---|---|---|---|---|
| 0, 3, 6 | any; no effect | 0 (no increment below hPos 9) | yes | no |
| 9 | −3 (bpSad 0) | 1 | yes | no |
| 12 | −3 | 2 | yes | no |
| 15 | −3 | 3 | yes | **yes** |
| 18 … 33 | −3 | 3 | yes | **yes** |

Every neighboring pair of pattern samples differs by more than 32 in Co or
Cg, so an edge lies within any three samples, under both OQ-13 readings.

Line 2 (previous line D E F …) behaves the same way. Vector −1 compares E
with D at x ≡ 1 (Co 320 → 63), F with E at x ≡ 2 (Y 40 → 20, Co 160 → 40,
Cg 240 → 60), and D with F at x ≡ 0 (Y 20, Co 40, Cg 60). −3 again has SAD 0.

### Group at hPos 15, line 1

BP with vector −3 predicts pixels 15, 16, 17 from reconstructed pixels 12,
13, 14 of line 1, which are D, E, F. The targets are D, E, F, so every
residual is 0. The unit is `1` in each component once the predicted size has
decayed to 0.

For contrast, MMAP would predict pixel 15's Y as CLAMP(a + b − c, MIN(a, b),
MAX(a, b)), with a = F.Y = 140 (left), b = A.Y = 80 (above) and c = C.Y = 80
(above-left). That is 140, not D.Y = 180. A decoder that does not select BP
here therefore outputs Y 140 plus the zero residual and fails the fixture.

Lines 1 and 2 use BP in groups 15, 18, …, 33: 14 groups. Groups 0–12 of each
line are MMAP, or MPP where a residual needs 8 or 9 bits. Their residuals
are whatever makes them lossless.

## bp_slice_boundary (70×3, two 35×3 slices)

Slice 0 holds the same patterns as bp_left_edge. Slice 1 (picture x 35–69)
holds, on line y, pattern row (y + 1) mod 3 shifted by one:
pixel (x, y) = pattern[(y + 1) mod 3][(x − 35 + 1) mod 3].

* Each slice runs its own search on its own previous line, from its own
  hPos 0. For slice 1, picture x 35 is hPos 0, and positions left of it are
  outside that slice. The fixture's decisions do not depend on how those
  positions read (OQ-4), for the same reason as above.
* Slice width 35 = 11 × 3 + 2. The last group (hPos 33) has two pixels. It
  is a partial group, so it never uses BP, although bpCount is 3 there.
* In each slice, lines 1 and 2 use BP in groups 15 … 30: 6 groups per line,
  24 groups over both slices.
* Line 1 of slice 1: previous line is D E F shifted (E F D E F …), current
  line G H I shifted (H I G …). At hPos 15 the prediction copies picture
  x 47, 48, 49 (slice hPos 12, 13, 14), which are H, I, G, the targets for
  hPos 15, 16, 17 (x mod 3 after the shift: 16 mod 3 = 1 → H). All zero
  residuals.

## oq4_bp_left discriminator

See `tests/discriminators/README.md`. Its first line comes from a seeded
search in `tests/make_bp_vectors.py`. The generator requires that the two
left-boundary readings make different BP decisions, and that each reading's
decisions agree under both OQ-10 and both OQ-13 readings.
