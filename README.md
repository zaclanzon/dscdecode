# dscdecode — experimental DSC software decoder

Why this exists: while debugging NVIDIA display driver bugs on Linux,
each experiment cost a reboot and the only check was looking at the
monitor. This decoder is one piece of tooling for testing the display
path in software instead.

An open-source DSC 1.1 decoder, written from the specification with no
VESA model code, and bit-exact with the VESA C model on 760 test streams.

C11, libc only, built as a library and a command-line tool.

BSD-2-Clause-Patent licensed; kernel DSC definitions retain their Intel
notice and MIT license.

## Status

dscdecode is a working, tested checkpoint for DSC 1.1. It has not been
through VESA compliance testing.

Agreement with the VESA C model (version 1.67), used only as a black box.
For each test image, the model encodes the image, the model and dscdecode
both decode the model's bitstream, and the two outputs are compared bit
for bit. The hand-derived fixtures are bitstreams built by this
repository's generators. The model and dscdecode both decode them, and
both outputs match the hand-derived expected images. Tested with 8 bpc
RGB 4:4:4 at constant bit rate.

Where the specification text supports two readings, each reading is a
runtime switch (dscdecode --reading). RESEARCH.md lists every open
question and the evidence for each default.

| Set | Streams | Bit-exact |
|---|---|---|
| The 17 VESA 1080p evaluation images: 6, 7.5, 8, 10, 12 and 15 bpp, block prediction off and on, 1, 2 and 4 slices per line | 612 | 612 |
| Synthetic pictures, hand-derived fixtures and fractional-rate tests | 148 | 148 |

A successful decode does not show that an encoder's output conforms to
DSC. Like the model, dscdecode accepts some nonconforming streams, such
as nonzero padding in partial groups, and reports them with a warning.

The images, the model and the specification are not in this repository.
PROGRESS.md has the method and every result. THIRD_PARTY.md has the
provenance.

M2 checkpoint (September 23): ten exact-image fixtures (including block
prediction), 26 CLI checks, RC and prediction traces, and 72 discriminator
decodes pass. Partial-group padding is parsed but not checked by default; the
CLI prints a warning with the number of partial groups whose nonzero padding
it accepted, and `--reading partial_padding=reject` makes that an error
(DSC 1.1 §6.6). Comparison runs against the VESA model are recorded in
`PROGRESS.md`.

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

* The status of every open rate-control and prediction question, and the
  evidence behind each default, is in the "Open questions" table in
  `RESEARCH.md`. Two remain open: OQ-7 (DSC 1.2 only) and OQ-9 (encoder only).
* Only DSC 1.1, 8 bits per component, RGB 4:4:4 at constant bit rate is
  decoded. The decoder rejects streams that use any of the following as
  unsupported; none is implemented:
  * DSC 1.2. The PPS parser reads the 1.2 fields, but the decoder accepts
    only version 1.1.
  * 10 and 12 bits per component (and the 14 and 16 of DSC 1.2).
  * VBR, including its framing and buffer handling.
  * Native 4:2:2 and native 4:2:0 (DSC 1.2).
  * Simple 4:2:2, and YCbCr input (`convert_rgb` 0).
* Fractional bits_per_pixel has been exercised by a discriminator, by
  model-encoded synthetic pictures at 7.5 and 9.3125, and by the 17-image
  corpus at 7.5 (`PROGRESS.md`), not across the range.
* The licensed VESA reference model is not included. `tools/compare_model`
  drives it as a black box when `DSCDECODE_MODEL_BIN` points at it, and
  `tests/discriminators/` holds inputs that separate the readings of the
  open rate-control questions. The Status section summarizes the model
  comparison. PROGRESS.md has every result.

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
