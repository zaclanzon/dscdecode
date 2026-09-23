# Specification-derived synthetic vectors

`python3 tests/make_vectors.py` recreates the checked-in fixture bytes. This is a
restricted syntax constructor for three fixed mathematical patterns, not an
encoder implementation. Its only algorithmic sources are DSC 1.1 prose,
Tables 4-6 and 6-1, §§6.4.1, 6.4.4.2, 6.6.1, 7.7, and Annex E. No VESA model
source or executable was consulted or used in constructing these fixtures.

All fixtures are RGB 8bpc, DSC 1.1, 8.0 compressed bits per pixel,
9-bit line buffer, BP disabled, and constant QP zero. The five initial fixtures
use96×3 slices and full three-pixel groups. `color_crop` uses95×3 slices and
two-sample final groups.
Every range has min-QP=max-QP=0. Flatness min/max are15, so no flatness syntax is
present at QP zero. Except for `color_crop`, Co and Cg remain256, making
RGB=(Y,Y,Y) directly under §7.7.

| Fixture | Picture | Independent pixel formula |
|---|---|---|
| flat | 96×3, one slice | R=G=B=128 |
| gradient | 96×3, one slice | R=G=B=80+x+y |
| checker | 192×3, two slices | R=G=B=120+16×((floor(x/5)+y) mod2) |
| mpp | 96×3, one slice | First three pixels0,255,0; remainder128 |
| ich | 96×3, one slice | R=G=B=128, with explicit ICH syntax |
| color_crop | 187×5, four95×3 slices | R=80+floor(x/8), G=100+y, B=150−floor(x/16) |

`color_crop` exercises independently varying chroma components, signed shifts
in color conversion, partial groups, horizontal and vertical slice order, and
right/bottom cropping. All four95×3 slices are fully transmitted, extending
beyond the picture's right and bottom edges with replicated edge pixels. Its
expected PPM uses only the visible187×5 RGB formula. The independent forward
transform is Co=R−B, t=B+floor(Co/2), Cg=G−t, Y=t+floor(Cg/2), with256 added
to each chroma component. Thus its first pixel RGB(80,100,150) gives
Co=−70, t=115, Cg=−15, and transmitted(Y,Co,Cg)=(107,186,241).
The inverse arithmetic recovers t=107−floor(−15/2)=115, G=100, B=150, R=80.
For each95-pixel row, the last group's third residual is explicitly zero and
participates in size prediction despite producing no pixel. Its PPS uses
chunk_size95, extra mux bits216, final_offset4312, and slice_bpg_offset48299.

The checker boundary is x=96, inside a five-pixel checker cell. Thus the same
cell continues across independently initialized slices. Picture payloads use
the CBR chunk order from §4.2.2: line0/slice0, line0/slice1, line1/slice0, etc.
`*.sliceN.bin` contain independently decodable, contiguous slice payloads.
`*.syntax.txt` show every group as three bit strings (Y, Co, Cg).

## Arithmetic checks independent of decoding

At QP0 the quantizer divisor is1; the MMAP low-pass blending bound truncates to0,
so previous-line samples remain unchanged. The first prediction is128.

* Flat: every residual is0, all size predictors stay0, and every component unit
  is the one-bit prefix `1`. There are96 groups, giving96 syntax bits per SSP.
  Each SSP requests three48-bit words, producing432 bits before CBR padding.
* Gradient first group: values80,81,82 give residuals−48,+1,+1; required sizes
  are7,2,2. The luma prefix is seven zeroes and a one, followed by three seven-bit
  two's-complement values. Its next predicted size is(7+2+2×2+2)//4=3.
* Checker first group: values120,120,120 give residuals−8,0,0. Required sizes
  are4,0,0; the luma prefix is `00001`, then `1000 0000 0000`.

The expected PPM is written directly from the pixel formula, without parsing,
inverse quantization, prediction, rate control, or a decoder call. Exact hashes
and syntax/mux lengths are in `fixtures/manifest.json`.

## PPS arithmetic

groupsPerLine=32, groupsTotal=96, chunk_size=ceil(96×8/8)=96 bytes,
sliceBits=2304. With maxSeSize36 and muxWordSize48, Annex E's conservative
numExtraMuxBits is246, reduced to240 to make sliceBits−numExtraMuxBits divisible
by48. rc_model_size=8192 and initial_offset=6144 give initial_scale_value32.
scale_decrement_interval=floor(32/(32−8))=1. With initial_xmit_delay512:

* final_offset=8192−512×8+240=4336;
* slice_bpg_offset=ceil((8192−6144+240)×2048/96)=48811;
* first_line_bpg_offset=nfl_bpg_offset=0;
* initial_dec_delay=ceil((8192−6144+512×8)/8)−512=256.

Scale increment is disabled with interval0. The initial transmission delay
exceeds the288 pixels in a slice, avoiding buffer-drain transitions in these
targeted fixtures. These deliberately unusual parameters are syntax tests,
not recommended display modes or evidence of broad HRD conformance.

## Coverage limits

The three required fixtures cover size-prediction updates, signed residuals,
lossless inverse quantization, MMAP on the first and later rows, all three
substreams, mux refills/padding, RGB conversion, and horizontal slice isolation.
Two additional targeted fixtures exercise MPP and ICH. The MPP fixture's
first group exceeds the MMAP residual-size limit, selecting the midpoint128
with signed residuals−128,+127,−128. The ICH fixture starts with a normal flat
P group to initialize history, uses a nine-zero luma escape, and references
history index0 for all three pixels of every subsequent group. Those subsequent
groups use a one-bit continuation prefix and three five-bit indices. All values
remain128, including when the history capacity changes after the first line.

These fixtures do not establish correctness of adaptive/nonzero QP, flatness
signaling, BP, nontrivial ICH reordering, reduced line-buffer precision, fractional bpp,
or general HRD behavior. Fuzz robustness is a separate claim from conformance.
# September 20 additions: nonzero QP and syntax rejection

`make_transition_vectors.py` constructs two additional 96-by-1 RGB fixtures.
Its QP schedules and expected grayscale samples are fixed constants derived
below, not obtained by executing a decoder or simulating its RC implementation.
It reuses only the existing bitstream multiplexer. These tests therefore remain
conditional on the checkpoint's documented startup/two-group timing model.
Sharing a mux constructor also means they are not an independent check of mux
framing itself.

Both use 24 bpp, a 288-byte slice budget, initial transmit/decode delays of 512,
scale 8, initial offset 2048, model size 8192, zero BPG offsets, and min/max QP 8
in ranges 0--13. Range 14 has maximum 15 to avoid suppressing flatness overrides.
The encountered ranges are below 14. The target is 72 bits/group with thresholds
69 and 75. The initial MPP groups consume 105 and 82 bits; increment logic selects
QP 8. Later QP-8 MPP groups consume 43 bits (plus applicable metadata), so the
decrement branch's minimum of 8 keeps them at 8. The pending-QP slot yields the
fixed visible schedule 0,0,8,8,... used to construct the entropy syntax.

Each group is MPP-coded with luma residual +1 and chroma residuals zero. At QP
0, the three component widths are 8,9,9; at QP 8, they are 5,4,4 (Table 6-3).
For `qp_transition`, the first two groups are gray 129. The remaining groups
are gray 137: section 6.4.3 gives 128+(129 mod 8)+8, then the same value on each
following group. Each gray value repeats for three pixels.

`qp_flatness` signals somewhat-flat at group 3, with type/location 0/0 at group
4, applying to group 5; it signals very-flat at group 7, with type/location 1/0
at group 8, applying to group 9 (section 6.6.3). The fixed QP schedule is
0,0,8,8,8,4,8,8,8,1,8,... . The gray schedule is
129,129,137,137,137,131,139,139,139,129,137,... . At QP 4, luma qLevel is 1:
128+(137 mod 2)+2=131. Returning to QP 8 gives 128+(131 mod 8)+8=139.
At QP 1, luma qLevel is zero, giving 129. Both override groups generate QP 8
again. Their already-pending QP is also 8, so this vector deliberately does
**not** distinguish alternative pending-QP/flatness restart interpretations.

Two width-95 malformed fixtures isolate section 6.6's partial-group requirements:
`invalid_partial_residual` has a nonzero third luma residual in the final two-pixel
group; `invalid_partial_ich` has a third ICH index different from the second.
The original checkpoint silently discarded those samples and accepted the
noncanonical syntax. The decoder now returns a bitstream error before prediction.
CLI tests also check that rejection leaves an existing output file intact.

`test_predict.c` independently tests section 6.3's line-storage rounding and
saturation: at depth 8, internal chroma 511 becomes 510 and 257 becomes 258;
at depth 9 both are preserved. Shift-register ICH keeps original sample precision.
Separate tests check that a full final group does not populate history and that
multiple horizontal slices clear history between lines.
