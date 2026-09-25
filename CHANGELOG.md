# Changelog

## v0.2.0 (2026-09-25)

Adds DSC 1.2, higher bit depths and YCbCr. Bit-exact with the VESA C model
(version 1.67, used as a black box) on 9,270 test streams; README.md has the
table and PROGRESS.md every result.

Added:

* DSC 1.1 at 10 and 12 bits per component.
* DSC 1.2 at 8, 10, 12, 14 and 16 bits per component: RGB 4:4:4, YCbCr
  4:4:4, simple 4:2:2, native 4:2:2 and native 4:2:0. YCbCr 4:4:4 and simple
  4:2:2 are also accepted in DSC 1.1.
* Library: `dsc_decode_frame_planes`, `dsc_decode_slice_planes` and
  `dsc_plane_size`, which write 16-bit sample planes for every supported
  format. The RGB888 functions still serve 8 bpc RGB only.
* CLI: PPM with maxval 2^bpc − 1 for RGB (two bytes per sample above 8 bpc);
  raw YCbCr to `*.yuv` in the layouts the VESA model reads and writes.
* 26 new reading switches (OQ-7 and OQ-19 to OQ-43 in RESEARCH.md; 40 in
  all). Where the DSC 1.2 rate-control text and the reference model differ,
  the default follows the model and the text's reading stays available;
  RESEARCH.md, "DSC 1.2 text and the reference model", compares the two.
* Tests: fixtures at 10 to 16 bpc and in YCbCr; 33 new discriminators, for
  OQ-7 and OQ-19 to OQ-43 (38 inputs in all); a Python decoder model
  (`tests/pydsc.py`) that builds and checks the DSC 1.2 and native inputs.
* Tools: `tools/make_pictures` (high-bit-depth and YCbCr test pictures),
  `--bpc`, `--dsc-version` and `--format` for `tools/compare_model` and
  `tools/run_corpus`, `--jobs` for parallel runs, `tools/verify_refactor`.
* Build: one directory per make flavor under `build/` (`release`,
  `sanitize`, `fuzz`, `fuzz-smoke`, `afl`), rebuilt when the compiler or the
  flags change.

Fixed (DSC 1.1 streams with narrow slices, also in v0.1.0 and v0.1.1):

* OQ-19: a one- or two-pixel group at the end of a line inside the initial
  transmission delay lowered the rate-control offset by its real pixels
  only; the model counts it as a whole group. Such streams could be
  rejected or decoded incorrectly.
* OQ-42 and OQ-43: with a scale decrement interval of one group, the first
  group took a decrement, and where the scale had not reached unity by the
  end of a slice's first line, decrements continued after it. The model
  does neither.

Changed:

* bits_per_pixel is accepted up to 1023 (it was capped at 384).

## v0.1.1 (2026-09-24)

The v0.1.0 decoder with its C sources reformatted for readability. Object
files and the release binary built without debug information are
byte-identical to v0.1.0's (`tools/verify_refactor`), and the 612-stream
DSC 1.1 corpus comparison was repeated. Known issue: OQ-19, OQ-42 and
OQ-43 (fixed in v0.2.0).

## v0.1.0 (2026-09-24)

First release. DSC 1.1, 8 bits per component, RGB 4:4:4 at constant bit
rate, block prediction included, written from the specification with no
VESA model code. Bit-exact with the VESA C model on 760 test streams: the 17
VESA 1080p evaluation images at six rates (612) and 148 synthetic pictures,
hand-derived fixtures and fractional-rate tests. 14 reading switches for
the open questions OQ-1 to OQ-18, where the text supports more than one
reading. BSD-2-Clause-Patent.
