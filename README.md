# dscdecode — experimental DSC software decoder

Why this exists: while debugging NVIDIA display driver bugs on Linux,
each experiment cost a reboot and the only check was looking at the
monitor. This decoder is one piece of tooling for testing the display
path in software instead.

Plain C11, libc-only library plus CLI for inspecting DSC 1.1, 8-bit RGB 4:4:4
CBR streams. MIT licensed; kernel DSC definitions retain their Intel notice.

**Status: a working, tested implementation checkpoint, not a completed or
conformance-validated M1.** Hand-derived vectors pass; general dynamic-QP/RC
interoperability remains unverified. VBR is explicitly unsupported. Block
prediction is implemented. Where the specification text reads two ways, each
reading is a runtime switch (see "Open questions" in `RESEARCH.md`). Do not
use a successful decode as proof that hardware encoder programming conforms to
DSC until those remaining validation gaps are closed.

M2 checkpoint (September 23): ten exact-image fixtures (including block
prediction), 22 CLI checks, RC and prediction traces, and 72 discriminator
decodes pass. Partial-group padding is parsed but not checked by default
(`--reading partial_padding=reject` enforces DSC 1.1 §6.6). Comparison runs
against the VESA model are recorded in `PROGRESS.md`.

## Build and use

```sh
make
./dscdecode picture.pps compressed.bin output.ppm
./dscdecode --slice picture.pps one-slice.bin slice.ppm
make test
make sanitize
scripts/ci.sh      # everything CI runs: build, suites, sanitizers, fuzz smoke
```

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

`include/dsc.h` exposes `dsc_parse_pps`, `dsc_decode_slice`, and
`dsc_decode_frame`. The parser uses the kernel's `struct drm_dsc_config` and
also parses 1.2 PPS fields, but the decoder accepts only the 1.1 profile above.
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

Regenerate the original fixtures with `python3 tests/make_vectors.py` and the
transition/padding fixtures with `python3 tests/make_transition_vectors.py`.

```sh
python3 tests/make_corpus.py
make fuzz                        # requires Clang with libFuzzer
mkdir -p fuzz-corpus               # writable corpus; tests/corpus is read-only seeds
./fuzz_decode fuzz-corpus tests/corpus -max_len=65536 -timeout=2
make afl                         # requires afl-clang-fast / AFL++
afl-fuzz -i tests/corpus -o afl-results -- ./fuzz_afl
make fuzz-smoke                   # deterministic mutation, GCC or Clang
```

The shared entry exercises both frame and single-slice APIs and caps per-input
pixels at 4096. The deterministic sanitizer smoke campaign is **not** a
coverage-guided fuzz result. Sanitizer builds treat undefined behavior as
fatal and report leaks; on a host where LeakSanitizer cannot run, set
`ASAN_OPTIONS=detect_leaks=0`.

## Remaining correctness work

* The status of every open rate-control and prediction question, and the
  evidence behind each default, is in the "Open questions" table in
  `RESEARCH.md`. Two remain open: OQ-7 (DSC 1.2 only) and OQ-9 (encoder only).
* Only DSC 1.1, 8 bits per component, RGB 4:4:4 is decoded. Fractional
  bits_per_pixel has been exercised only by a discriminator, not by
  model-encoded pictures.
* VBR framing and buffer handling are not implemented.
* The licensed VESA reference model is not included. `tools/compare_model`
  drives it as a black box when `DSCDECODE_MODEL_BIN` points at it, and
  `tests/discriminators/` holds inputs that separate the readings of the
  open rate-control questions. Comparison results are recorded only in
  `PROGRESS.md`.

See `RESEARCH.md` for pinned Linux/NVIDIA/specification sources, PPS field notes,
caller survey, RC-table adjudication, licensing evidence, and the
current verification report. No driver patches are part of this project.
