# 10 and 12 bits per component: worked derivations for the fixtures

M3, Phase 3. These derivations give the expected pixels of the six
high-bit-depth fixtures in `tests/fixtures/` (`hbd10_*`, `hbd12_*`) and the
syntax a decoder must parse to reproduce them. The arithmetic was done by
hand and checked afterwards against `tests/make_hbd_vectors.py`, which writes
the bitstreams and the expected images from the formulas below. Rules come
from DSC 1.1: §4.1 (PPS), §4.4 and §4.5 (substreams and syntax), §6.1
(colour conversion), §6.4.1 (MMAP), §6.4.3 (MPP), §6.4.6 (reconstruction),
§6.5 (ICH), §6.6 (entropy coding), Table 6-2 (QP to qLevel) and §7.7
(inverse conversion).

## What changes with the bit depth

For bits_per_component = bpc, RGB input:

* Sample widths (§6.1): Y has bpc bits; Co and Cg have bpc + 1 bits and are
  offset by 2^bpc. Midpoints are 2^(bpc−1) for Y and 2^bpc for Co and Cg.
* Mux words (§4.4): 48 bits at 8 and 10 bpc, 64 bits at 12 bpc.
* Maximum syntax element size, the refill threshold of each substream
  (§4.4, §7.1). The text states only the condition; the size follows from
  the syntax of §4.5. A luma unit holds up to three flatness bits (type and
  location), a prefix of up to bpc + 1 bits (after an ICH group, "0" and then
  bpc zeros for the largest P-mode size, whose "1" is inferred), and three
  bpc-bit residuals: 4·bpc + 4. A chroma unit holds up to cpntBitDepth zeros
  (the "1" inferred at the largest size) and three residuals of that size:
  4·(bpc + 1). An ICH unit is shorter. At 8 bpc both give 36, the value
  behind the 83-unit balance FIFO of §3.7.1 (48 + 36 − 1). At 10 bpc: 44 and
  44; at 12 bpc: 52 and 52.
* QP scale (Table 6-2, §4.5, §6.8.5.2): the largest QP is 15 + 2·(bpc − 8)
  (19 at 10 bpc, 23 at 12 bpc); the flatness type bit is sent from QP
  7 + 2·(bpc − 8); a very-flat override sets QP 1 + 2·(bpc − 8).
* Table 6-2 rows. Below the top of the scale, qLevelY is ⌊(QP − 1)/2⌋ and
  qLevelC is ⌊QP/2⌋ + 1 (both 0 at QP 0). At two below the largest QP, the
  row moves one step from luma to chroma. So the same QP maps differently at
  different depths: QP 13 is (5, 8) at 8 bpc but (6, 7) at 10 and 12 bpc; QP
  21 is (9, 12) at 12 bpc, where the plain pattern would give (10, 11).
* BP (§6.4.4.1): differences are shifted right by cpntBitDepth − 7, and an
  edge is a step above 32 << (bpc − 8). The fixtures below do not use BP; the
  model comparisons in PROGRESS.md do.

All six fixtures use DSC 1.1, 24 bpp, a line buffer of bpc + 1 bits, no BP,
initial_xmit_delay 512 (longer than any of these slices, so the rate buffer
never drains), zero BPG offsets and final_offset, initial_offset 2048, and
flatness_min_qp = flatness_max_qp = the largest QP, which the fixtures never
reach, so no flatness bits are sent. Every range has min QP = max QP.

## hbd10_color and hbd12_color: lossless colour, four slices

Picture 99×5, slices 50×3 (two per line, two slice rows). The last group of
each slice line has two pixels (50 = 16·3 + 2). The right slices repeat
column 98 in their last column; the bottom slices repeat row 4 in their
last row. Every range pins QP 0, so qLevel is 0 for every unit.

Source, with s = 2^(bpc−10) (1 at 10 bpc, 4 at 12 bpc) and % the remainder:

* columns 30 to 32: (2^bpc − 1, 0, 2^bpc − 1) on even rows and
  (0, 2^bpc − 1, 3s) on odd rows;
* elsewhere R = s·(64 + 9x + 5y) + (7x + y) % s,
  G = s·(900 − 4x + (x + y) % 5) + (x·y) % s,
  B = s·(300 + 3x + 2y + 37x % 5) + (x + 2y) % s.

At 12 bpc the "% s" terms fill the two bits below the 10-bit scale.

Why the expected image is the source. At qLevel 0 the quantization divisor
is 1, so the MMAP blend (§6.4.1) clamps filtB − b to ±0 and each blended
value equals the previous-line sample; reconstruction is prediction plus
the residual, clamped (§6.4.6). The constructor chooses every residual as
source minus prediction, and MPP (prediction = midpoint + (previous sample
& 0) = midpoint) where a residual would need the full sample width
(§6.4.4.2). So every reconstructed sample equals the source sample, and
§7.7, the exact inverse of §6.1, returns the source RGB. The expected PPM is
the formula above evaluated over the 99×5 picture.

Worked first group of slice 0 at 10 bpc (pixels 0 to 2 of row 0):

| x | RGB | Co = R − B | t = B + ⌊Co/2⌋ | Cg = G − t | Y = t + ⌊Cg/2⌋ | (Y, Co + 1024, Cg + 1024) |
|---|---|---|---|---|---|---|
| 0 | (64, 900, 300) | −236 | 182 | 718 | 541 | (541, 788, 1742) |
| 1 | (73, 897, 305) | −232 | 189 | 708 | 543 | (543, 792, 1732) |
| 2 | (82, 894, 310) | −228 | 196 | 698 | 545 | (545, 796, 1722) |

First line of a slice, first group: a = midpoint, P0 = a, P1 = a + R0,
P2 = a + R0 + R1 (§6.4.1).

* Y (10 bits, midpoint 512): residuals 29, 2, 2; sizes 6, 3, 3. Predicted
  size 0 (first group), so the prefix is six zeros and a one, followed by
  011101 000010 000010. Next predicted size ⌊(6 + 3 + 2·3 + 2)/4⌋ = 4.
* Co (11 bits, midpoint 1024): residuals −236, 4, 4; sizes 9, 4, 4. Prefix
  nine zeros and a one; residuals 100010100 000000100 000000100.
* Cg: 1742 − 1024 = 718 needs 11 bits, the full width, so the unit is MPP:
  each residual is sample − 1024, i.e. 718, 708, 698, in 11 bits, after
  eleven zeros (the "1" is inferred at the largest size).

The first group needs a mux word from each substream (all three funnel
shifters start empty, below 44): Y, Co, Cg in that order.

The same pixel at 12 bpc: RGB (256, 3600, 1200) gives Co = −944, t = 728,
Cg = 2872, Y = 2164, so (Y, Co + 4096, Cg + 4096) = (2164, 3152, 6968). The Y
residual 2164 − 2048 = 116 needs 8 bits; Cg 6968 − 4096 = 2872 needs 13, the
full width, so the Cg unit is again MPP.

The spike columns 30 to 32 form group 10 of slice 0 on each row. Its
samples are far from their predictions, so Y, Co and Cg are MPP on several
rows: 7 MPP units in slice 0 and 5 in slice 2 at both depths
(`tests/fixtures/hbd_manifest.json`).

## hbd10_ich and hbd12_ich: ICH escape and continuation

Picture 48×3, one slice. Every pixel has one colour: (603, 521, 402) at
10 bpc and (2403, 2081, 1602) at 12 bpc.

At 10 bpc: Co = 201, t = 402 + 100 = 502, Cg = 521 − 502 = 19,
Y = 502 + 9 = 511, so (Y, Co + 1024, Cg + 1024) = (511, 1225, 1043).
Inverse check (§7.7): t = 511 − ⌊19/2⌋ = 502, G = 19 + 502 = 521,
B = 502 − ⌊201/2⌋ = 402, R = 201 + 402 = 603.

At 12 bpc: Co = 801, t = 1602 + 400 = 2002, Cg = 79, Y = 2002 + 39 = 2041;
(2041, 4897, 4175).

* Group 0, P-mode at QP 0 on the first line: residuals (v − midpoint, 0, 0)
  give v at all three pixels, since P1 = a + R0 = v and P2 = a + R0 + R1 = v.
  At 10 bpc the Y residual is −1 (size 1): prefix "01", then 1, 0, 0. The
  group enters the ICH as three entries of the colour (§6.5.2).
* Group 1 escapes to ICH. After a P group the escape is a luma prefix of
  bpc + 1 − adjPredictedSize zeros with no "1" (Table 6-1). The predicted
  size after group 0 is ⌊(1 + 0 + 0 + 2)/4⌋ = 0 at 10 bpc, so eleven zeros;
  at 12 bpc the Y residual is −7 (size 4), ⌊(4 + 2)/4⌋ = 1, so twelve zeros.
  Then index 0 in each of the three substreams (5 bits each).
* Groups 2 to 47: a luma prefix of "1" (ICH continues) and index 0 three
  times. Index 0 is the most recently used entry, the colour. Selecting it
  three times keeps it at index 0 (the repeated index is ignored for the
  update, §6.5.2). After the first line the history keeps 25 entries, and
  index 0 is still the colour.

Every decoded pixel is the colour.

## hbd10_qp and hbd12_qp: a pinned QP where Table 6-2 differs from 8 bpc

Picture 48×1, one slice, 16 groups. Every range has min QP = max QP = Q:
Q = 13 at 10 bpc, Q = 21 at 12 bpc. Every group is MPP in all three units
with residuals Y +1, Co −1, Cg 0.

QP schedule. Groups 0 and 1 decode at QP 0 (§6.8, the first RC result
applies two groups later). Group 0 is large (10 bpc: 11 + 30 + 11 + 33 +
11 + 33 = 129 bits), above the target, with a buffer fullness of at least
64, so the short-term RC takes the increment branch: MAX(minQp, prevQp) =
Q, then MIN(maxQp, …) = Q. Every other branch also returns Q, because each
range has min = max = Q. Groups 2 to 15 decode at Q.

qLevels (Table 6-2): at 10 bpc, QP 13 gives qLevelY = 6, qLevelC = 7 (8 bpc
would give 5 and 8). At 12 bpc, QP 21 = 23 − 2 gives qLevelY = 9,
qLevelC = 12 (not 10 and 11).

MPP widths are cpntBitDepth − qLevel (§6.4.3): at QP 0, 10/11/11 and
12/13/13; at Q, 4/4/4 at 10 bpc and 3/1/1 at 12 bpc. Prefixes (§6.6.1):
after an MPP unit the predicted size is the full width, adjusted by the
qLevel change and clamped to width − 1, so each later unit codes one zero
(plus "1" for Y, whose largest P-mode size is not the escape). Example,
10 bpc group 2 Y: predicted 10, qLevel change 6, clamp(4, 0, 3) = 3, size
4: "01" then 0001 three times.

Samples (§6.4.3: prediction = midpoint + (previous group's last sample &
(2^qLevel − 1)); reconstruction adds residual·2^qLevel, §6.4.6):

| | 10 bpc | 12 bpc |
|---|---|---|
| Y, groups 0–1 | 512 + 0 + 1 = 513 | 2048 + 0 + 1 = 2049 |
| Y, group 2 | 512 + (513 & 63) + 64 = 577 | 2048 + (2049 & 511) + 512 = 2561 |
| Y, groups 3–15 | 512 + (577 & 63) + 64 = 577 | 2048 + (2561 & 511) + 512 = 2561 |
| Co, groups 0–1 | 1024 + 0 − 1 = 1023 | 4096 + 0 − 1 = 4095 |
| Co, groups 2–15 | 1024 + (1023 & 127) − 128 = 1023 | 4096 + (4095 & 4095) − 4096 = 4095 |
| Cg, all | 1024 | 4096 |

RGB (§7.7): cscCo = −1, cscCg = 0, so t = Y, G = Y, B = Y − ⌊−1/2⌋ = Y + 1,
R = −1 + B = Y. Expected pixels: (513, 513, 514) for groups 0–1, then
(577, 577, 578) at 10 bpc; (2049, 2049, 2050), then (2561, 2561, 2562) at
12 bpc.

A decoder that used the 8 bpc row at QP 13 (widths 5, 3, 3), or the plain
pattern at QP 21 (widths 2, 2, 2), would parse different sizes from group 2
on and could not reproduce these pixels.

## Checks

`python3 tests/make_hbd_vectors.py` recreates every file. dscdecode decodes
each fixture to its expected PPM (`tests/test_cli.py`), and the VESA model,
run as a black box on the six bitstreams, produced the same six images
(PROGRESS.md, Phase 3). That agreement also supports the maximum syntax
element sizes above: a different refill threshold changes the order of the
mux words and the parse.
