# Third-party definitions

`include/drm/display/drm_dsc.h` is copied without changes from Linux master
9b87fdc9af2fbfcdb5c24a64139685ef80f6573f, path
`include/drm/display/drm_dsc.h`. It is MIT licensed, copyright 2018 Intel Corp.;
its original SPDX and attribution notices remain intact. The MIT terms are
reproduced in LICENSE. Its companion `drm_dp.h` is an original minimal
userspace compatibility shim, not a copied Linux header.

The VESA documents and reference-model archives are not included. No VESA
model implementation is incorporated. Specification-derived equations,
formats, and factual parameter comparisons are documented in RESEARCH.md.
Research included earlier model inspection; this is not a claim of formal
clean-room isolation. Track A implementation used specification prose and
errata, with model-only resolutions deliberately excluded.

Provenance of the M1 research. On September 16, 2026, the M1 research
downloaded the VESA DSC C reference model from an unofficial GitHub mirror and
read its source. No model code was copied into dscdecode. From M2 onward, any
comparison against the model uses the officially licensed model as a black
box only.
