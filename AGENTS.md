# Instructions for coding agents

These rules apply to every AI coding agent working in this repository,
including Claude Code and Codex.

## Provenance
- Earlier research for this project inspected parts of the VESA DSC C
  model source (see THIRD_PARTY.md). The decoder contains no model code,
  and all future work must keep it that way.
- Never read, request, search for, or download the VESA C model source or
  any copy of it, including mirrors. Never use notes, chats or files
  derived from it. If such material appears in your context, stop and
  tell the maintainer.
- Use the model only as a black box: its binary, its README and config
  files, and the files it writes. Do not disassemble or decompile it, or
  run strings on it.

## Specification
- Do not copy prose, tables or figures from the DSC specification into
  the repository. Cite section numbers and describe behavior in your own
  words.
- Never commit VESA documents, model binaries or model output, test
  pictures, or archives.

## Ambiguities
- When the specification text supports more than one reading, or differs
  from the model, record each reading in RESEARCH.md with its section,
  implement it behind a named reading switch, and add it to the
  open-questions table with its hypothesis source.
- Commit each discriminator, with its prediction for every reading, before
  the model decodes it.

## Changes and verification
- Work on a branch. Never commit to main.
- Do not describe the decoder as conformant, validated, certified,
  clean-room or independent.
- Run scripts/ci.sh before each commit. Do not weaken or delete a test to
  make it pass.
- Release claims come only from a model comparison run with that
  release's binary. Do not change the README Status numbers outside a
  release.

## Git
- Never push, tag, create releases, or change remotes. The maintainer
  does these.
