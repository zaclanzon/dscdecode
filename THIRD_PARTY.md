# Third-party definitions

`include/drm/display/drm_dsc.h` is copied without changes from Linux master
9b87fdc9af2fbfcdb5c24a64139685ef80f6573f, path
`include/drm/display/drm_dsc.h`. It is MIT licensed, copyright 2018 Intel Corp.;
its original SPDX and attribution notices remain intact. The MIT terms are
reproduced in LICENSES/MIT.txt. Its companion `drm_dp.h` is an original minimal
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

DSC 1.2 rate control. Where the DSC 1.2b text did not reproduce the model's
decodes (M3, September 24, 2026), the behavior was found by
black-box probing of the model's output. The model encoded test pictures,
some with rate-control parameters pinned or flatness signaling disabled
through the options its README documents; a debug build of this decoder,
kept outside the repository, forced chosen QPs and logged its rate-control
inputs to locate where its decode first departed from the model's;
candidate rules were scored against the model's decoded pictures; and each
rule kept was then checked on a new input whose predictions were committed
before the model decoded it. Only the model's README, its configuration
files, its command line and the files it writes were used. No model source
was used. The same method found OQ-19 and OQ-40 to OQ-43 (RESEARCH.md).

Registration record. Registration: 2026-09-23. VESA Public Standards Download
Registration, product-development path. Terms: Implementer's License Agreement
(Exhibit D of VESA Policy 200D). Archive: `Display Stream Compression (DSC).zip`,
SHA-256 `4d8058e817bc71d41e5f87445979ca6b8f54ea83dd08870c42d158b617d02be2`.
The archive, the specification, and the reference model are not in this
repository. The reference model is used only as a black box.
