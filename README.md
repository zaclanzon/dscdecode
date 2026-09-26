# dscdecode — experimental DSC software decoder

Why this exists: while debugging NVIDIA display driver bugs on Linux,
each experiment cost a reboot and the only check was looking at the
monitor. This decoder is one piece of tooling for testing the display
path in software instead.

An open-source decoder for VESA DSC 1.1 and 1.2, written with no VESA model
code, and bit-exact with the VESA C model on 9,270 test streams. It was
written from the specification. Where the text is ambiguous or differs from
the reference model, which the specification says takes precedence (DSC 1.2b
§1.4.3), the decoder follows the model's observed behavior, and each such
point is a reading switch (RESEARCH.md).

C11, libc only, built as a library and a command-line tool.

BSD-2-Clause-Patent licensed; kernel DSC definitions retain their Intel
notice and MIT license.

## Status

dscdecode v0.2.0 decodes DSC 1.1 and DSC 1.2 streams at constant bit rate:
RGB and YCbCr 4:4:4 and simple 4:2:2 in both versions, native 4:2:2 and
native 4:2:0 in DSC 1.2, at 8 to 12 bits per component in DSC 1.1 and 8 to
16 in DSC 1.2. It has not been through VESA compliance testing.

Agreement with the VESA C model (version 1.67), used only as a black box.
For each test image, the model encodes the image, the model and dscdecode
both decode the model's bitstream, and the two outputs are compared bit
for bit. The hand-derived fixtures are bitstreams built by this
repository's generators. The model and dscdecode both decode them, and
both outputs match the hand-derived expected images. The discriminators are
bitstreams built so that the readings of an open question decode
differently; each was committed with its predicted outputs before the model
decoded it.

Where the specification text supports two readings, each reading is a
runtime switch (dscdecode --reading). RESEARCH.md lists every open
question, the evidence for each default and where each reading came from,
and compares the DSC 1.2 rate-control text with the model's behavior.

Every row below was run with one release binary (SHA-256 7e0eaebf…d786c)
for v0.2.0; PROGRESS.md, "v0.2.0 release gate", has the per-rate tables.

| Set | Streams | Bit-exact |
|---|---|---|
| DSC 1.1 RGB 4:4:4, 8 bpc: the 17 VESA 1080p evaluation images at 6, 7.5, 8, 10, 12 and 15 bpp, block prediction off and on, 1, 2 and 4 slices per line | 612 | 612 |
| DSC 1.1 RGB 4:4:4, 10 and 12 bpc: 8 synthetic pictures and the 17 images scaled to the depth with a few LSBs of noise, at every installed rate (6, 8, 10, 12, 15 bpp), block prediction off and on, 1, 2 and 4 slices | 1,500 | 1,500 |
| DSC 1.2 RGB 4:4:4, 8 to 16 bpc: synthetic pictures at 8, 10, 12, 14 and 16 bpc, the 17 images at 8 bpc and scaled to 10 and 12, same rates and settings | 2,730 | 2,730 |
| DSC 1.2 YCbCr 4:4:4, simple 4:2:2, native 4:2:2 and native 4:2:0, 8 to 16 bpc: the same pictures converted to YCbCr, three rates per format, block prediction off and on, 1 and 2 slices | 4,368 | 4,368 |
| Hand-derived fixtures (PPS and payload), decoded by the model and by dscdecode | 24 | 24 |
| Discriminators whose verdict is the default reading | 36 | 36 |
| Total | 9,270 | 9,270 |

The v0.1.0 row of 148 synthetic pictures, fixtures and fractional-rate
tests is not repeated, because its pictures no longer exist. Two
superseded discriminators are not counted: `oq2_threshold_equality` was
built under earlier readings of other questions, and `oq24_bitsave_flat` has
predictions only for two readings that the model contradicted; the model's
output matches neither prediction of either.

A successful decode does not show that an encoder's output conforms to
DSC. Like the model, dscdecode accepts some nonconforming streams, such
as nonzero padding in partial groups, and reports them with a warning
(`--reading partial_padding=reject` makes that an error).

The images, the model and the specification are not in this repository.
PROGRESS.md has the method and every result. THIRD_PARTY.md has the
provenance.

## Known issue in v0.1.x

v0.1.0 and v0.1.1 can reject or decode incorrectly some valid DSC 1.1
streams with narrow slices: a partial group at the end of a line inside the
initial transmission delay (OQ-19), or an initial_scale_value decremented
every group (OQ-42, OQ-43). v0.2.0 fixes both.

## Build and use

```sh
make                 # release build: build/release/dscdecode and libdsc.a
build/release/dscdecode picture.pps compressed.bin output.ppm
build/release/dscdecode picture.pps compressed.bin output.yuv   # YCbCr streams
build/release/dscdecode --slice picture.pps one-slice.bin slice.ppm
make test            # release build, then the test suites
make sanitize        # ASan/UBSan build in build/sanitize, then its suites
scripts/ci.sh        # everything CI runs: build, suites, sanitizers, fuzz smoke
```

Each build flavor has its own directory under `build/`: `release`
(`make`, `CFLAGS`, default `-O2 -g`), `sanitize`, `fuzz`, `fuzz-smoke` and
`afl`. A `.flags` file in each directory records the compiler and flags, so
changing either rebuilds that flavor, and one flavor never replaces another's
binaries. `make clean` removes `build/`.

Options: `--reading NAME=VALUE` selects one reading of a rate-control or
block-prediction question the text leaves open (see "Open questions" in
`RESEARCH.md`; `dscdecode` with no arguments lists them and the defaults),
`--stats` prints how often those questions arose, and `--trace FILE.csv`
writes per-group QP, bit counts and buffer fullness.

The PPS file must be exactly 128 bytes. `compressed.bin` contains raw picture
payload without a container header: for each vertical slice row, concatenate
one `slice_chunk_size`-byte chunk from each horizontal slice, for each of the
`slice_height` scanlines. Edge slices retain their padded dimensions in the
compressed input and are cropped in the output. `--slice` instead takes one
contiguous independently decodable slice and outputs its entire dimensions.

A VESA-model `.dsc` container, DisplayPort SDP packet, HDMI transport packet,
or captured link-symbol stream is **not** a raw input for this CLI. Framing
must be removed upstream. No hardware access occurs.

`include/dsc.h` exposes `dsc_parse_pps`; `dsc_decode_slice` and
`dsc_decode_frame`, which write RGB888 and serve 8 bpc RGB only; and
`dsc_decode_slice_planes` and `dsc_decode_frame_planes`, which write one plane
of 16-bit samples per component for every supported format
(`dsc_plane_size` gives the plane sizes). The parser uses the kernel's
`struct drm_dsc_config` and also parses 1.2 PPS fields. The decoder accepts
CBR streams of DSC 1.1 at 8, 10 and 12 bits per component and DSC 1.2 at 8,
10, 12, 14 and 16: RGB 4:4:4 and YCbCr 4:4:4 (convert_rgb 0) and simple
4:2:2 in both versions, and native 4:2:2 and native 4:2:0 in DSC 1.2. For
RGB the CLI writes a binary PPM with maxval 2^bpc − 1: one byte per sample
at 8 bpc, two bytes (most significant first) above. For YCbCr it writes raw
YCbCr to a file named `*.yuv`, in the layouts the VESA reference model
reads and writes: planar Y, Cb, Cr for native 4:2:0 (chroma at half width
and height) and for 4:4:4, interleaved UYVY for the 4:2:2 formats (simple
4:2:2 keeps the chroma of the even positions). Above 8 bpc each sample takes
two bytes, least significant first; planar samples hold the value as it is,
UYVY samples hold it in the most significant bits, as the model's files do.
The planes API returns Y, Cb and Cr with the chroma planes subsampled.
Thresholds retain PPS units and signed BPG offsets retain six-bit encoding.

Limits: 16,777,216 pixels for both a frame and an individual slice; at most 255
horizontal slices in the reused configuration representation; 256MiB CLI
input limit. Size mismatches, invalid syntax, invalid history references,
and detected RC violations return errors. A failure may leave library output
partially written; the CLI opens its output only after a successful decode.
The offline API returns errors rather than implementing unspecified sink
error-concealment pixels. Allocation failures never terminate the caller.

## Verification and fuzzing

`tests/VECTORS.md` gives the independent expected-pixel derivation and exact
bit construction for each fixture. `tests/make_vectors.py` is a narrowly
scoped fixture constructor, not a general DSC encoder. Tests are Python3;
the production library and CLI have no Python dependency.

Regenerate the original fixtures with `python3 tests/make_vectors.py`, the
transition/padding fixtures with `python3 tests/make_transition_vectors.py`,
the 10 and 12 bpc fixtures with `python3 tests/make_hbd_vectors.py`
(derivations in `research/hbd-worked-note.md`), and the lossless YCbCr
fixtures of the `.yuv` output with `python3 tests/make_ycbcr_vectors.py`.

```sh
python3 tests/make_corpus.py
make fuzz                        # requires Clang with libFuzzer
mkdir -p fuzz-corpus             # writable corpus; tests/corpus is read-only seeds
build/fuzz/fuzz_decode fuzz-corpus tests/corpus -max_len=65536 -timeout=2
make afl                         # requires afl-clang-fast / AFL++
afl-fuzz -i tests/corpus -o afl-results -- build/afl/fuzz_afl
make fuzz-smoke                  # deterministic mutation, GCC or Clang
```

The shared entry exercises both frame and single-slice APIs and caps per-input
pixels at 4096. It decodes each input with the default readings and again,
through the planes API for the frame and the single slice, under a
combination of every reading switch taken from the input's bytes. The seeds
(`tests/make_corpus.py`) are the fixtures and discriminators, so they cover
RGB and YCbCr 4:4:4, simple 4:2:2 and native 4:2:2 and 4:2:0 at 8 to 16
bpc. The deterministic sanitizer smoke campaign is **not** a
coverage-guided fuzz result. Sanitizer builds treat undefined behavior as
fatal and report leaks; on a host where LeakSanitizer cannot run, set
`ASAN_OPTIONS=detect_leaks=0`.

`tools/compare_model`, `tools/run_corpus` and the model step of
`scripts/ci.sh` run the release build, `build/release/dscdecode`, and print
the path of the binary they use. `--build sanitize` (for `scripts/ci.sh`,
`CI_MODEL_BUILD=sanitize`) selects `build/sanitize/dscdecode`;
`DSCDECODE_BIN` names any other binary.

`tests/make_discriminators.py` regenerates `tests/discriminators/`; the DSC
1.2 inputs come from `tests/make_v12_discriminators.py` and the native 4:2:2
and 4:2:0 ones from `tests/make_native_discriminators.py`, which build and
decode them with `tests/pydsc.py`, a small Python decoder model kept
separate from the C decoder (YCbCr, the native containers and block
prediction included). The native inputs' expected pictures are raw YCbCr.

`tools/make_pictures synthetic|derived --bpc N...` writes test pictures
outside the repository (default `~/dsc-runs/hbd-pictures`): DPX at 10, 12
and 16 bits in the layout the model writes, PPM at 8 and 14 bits, and a
master PPM of each. `tools/compare_model` and `tools/run_corpus` take
`--bpc` and `--dsc-version`; they check what the model read against the
master PPM, map the model's 16-bit output of 14-bit pictures back to 14
bits (checking the mapping), and default the line buffer to bpc + 1, at most
16. At 14 bpc the model's encoder dies from a signal after writing all its
output; the harness accepts such an encode only when the model's log
reports the last slice and the bitstream exists, and records the signal in
`result.json`.

YCbCr: `tools/make_pictures synthetic|derived|corpus --format FORMAT`
converts the pictures (and, for `corpus`, the 8-bit corpus images) to YCbCr
with the BT.709 weights in limited range, subsampling the chroma by
averaging, and writes them in forms the model reads: a 4:4:4 DPX
(descriptor 102) for `ycbcr_444`, and `NAME_WxH.yuv` (UYVY, or planar 4:2:0)
for `simple_422`, `native_422` and `native_420`; the module docstring gives
the formulas and layouts. `tools/compare_model image --format FORMAT` and
`tools/run_corpus --format FORMAT` code them as that format, with the
model's `_422` and `_420` rate files for the native modes (`--bpp` is the
picture's rate), check the model's copy of its input, and compare raw
YCbCr: the model's `.yuv` output for the 4:2:2 and 4:2:0 formats and its
4:4:4 DPX, read back, otherwise.

`tools/verify_refactor BASE HEAD` checks that a refactor between two commits,
such as reformatting or brace insertion, leaves the compiled code and the
Python code unchanged. It exports both commits with `git archive` into a scratch
directory and compares: the objects of every `.c` file under `src/`, `fuzz/`
and `tests/` (gcc and clang, `-O0` and `-O2`, no `-g`, relative paths, and an
`assert.h` shim first on the include path so that `__LINE__` does not reach the
objects), plus `src/` with the Makefile's flags; `dscdecode` and `libdsc.a`
from `make CFLAGS=-O2`; clang's raw tokens of every `.c` and `.h` file, where
only inserted `{ }` pairs are allowed and are counted; the comments, which may
differ only in whitespace; and `ast.dump` of every Python file. It exits 1 on
any difference and prints the first one, 0 when there is none. Files that are
neither C nor Python are listed but not checked.

## Remaining correctness work

* VBR, including its framing and buffer handling, is not implemented. Both
  decode APIs reject VBR streams as unsupported.
* DSC 1.1 YCbCr (4:4:4 and simple 4:2:2) has no comparison on
  model-encoded pictures: the model's encoder refuses DSC 1.1 YCbCr with
  its own rate files. One hand-derived DSC 1.1 YCbCr 4:4:4 fixture decodes
  bit-exact with the model; DSC 1.1 simple 4:2:2 is not tested against it.
* 14 bpc: the model's encoder dies from a signal after writing its
  bitstream and reference picture in most 14 bpc encodes (432 of 624 in the
  v0.2.0 gate: every RGB, YCbCr 4:4:4 and native 4:2:2 encode). Those
  results rely on such encodes; the harness accepts one only when the
  model's log reports the last slice and the bitstream exists, and the
  model's own decode, a separate process, runs normally.
* OQ-24: the model's window is fixed at its ends by the discriminators;
  its interior groups are inferred, not tested on their own.
* OQ-23: two of the text's readings (raw and adjusted) were never
  separated from each other; one input excludes both at once.
* Fractional bits_per_pixel has been compared at 7.5 bpp (the DSC 1.1
  corpus) and 9.3125 bpp (M2 synthetic pictures), and by one
  discriminator, not at other rates, depths or in DSC 1.2.
* The fuzzer rarely reaches valid decodes of the non-RGB formats: of the
  277 YCbCr and native inputs its 30-minute campaign kept, 14 decode
  without error.
* Open questions: OQ-9 (encoder only; decoding cannot observe it) is
  still open. Every other question in RESEARCH.md is resolved by
  comparison with the model. For 23 of them the default was fitted to the
  model's output rather than predicted from the text, so their
  discriminators confirm a fitted rule (the "Hypothesis source" column:
  22 marked "model output", and OQ-24).
* Model versions. The comparisons behind the defaults used model version
  1.67; DSC 1.2b cites 1.63 and DSC 1.2a cites 1.57. Versions 1.31a, 1.48,
  1.57 and 1.63 have since been compared on the discriminators, the
  fixtures and a subset of the model-encoded sets (RESEARCH.md, "Model
  versions"). 1.57 and 1.63 give the same verdict as 1.67 on every
  discriminator and are bit-exact on every set, so the defaults also
  reproduce the versions the standards cite; 1.31a does on every DSC 1.1
  test. The one output that changed, on the superseded `oq2` input, is
  from a stream that runs out of payload (1.31a to 1.57 differ there from
  1.63 and 1.67). Not covered: 1.48, a DSC 1.2 build, decodes native
  4:2:0 differently and no reading reproduces it (its successors' READMEs
  call its native 4:2:0 incorrect); its native results come from its own
  encodes, since it cannot decode a YCbCr stream on its own; 1.63's native
  4:2:0 was spot-checked only on pictures it converted from 4:4:4 itself;
  the older versions were not run on model-encoded 14 and 16 bpc, YCbCr
  4:4:4 or simple 4:2:2 pictures (only the corresponding discriminators and
  fixtures).
* The licensed VESA reference model is not included. `tools/compare_model`
  drives it as a black box when `DSCDECODE_MODEL_BIN` points at it, and
  `tests/discriminators/` holds inputs that separate the readings of the
  open questions. The Status section summarizes the model comparison.
  PROGRESS.md has every result.

See `RESEARCH.md` for pinned Linux/NVIDIA/specification sources, PPS field notes,
caller survey, RC-table adjudication, licensing evidence, and the
current verification report. No driver patches are part of this project.

## Patents

This project is licensed under BSD-2-Clause-Patent. That license includes
a limited patent grant from this project's contributors. It does not grant
rights to third-party patents that may cover DSC. In the patent
declarations in VESA's DSC release package, each member that filed an IPR
response form committed to license its declared patents on RAND terms,
with the right to charge royalties. Users should assess whether their
intended use requires additional licenses.

## Development

Built with Claude Code as a coding agent. Each commit carries a
Co-Authored-By trailer.
