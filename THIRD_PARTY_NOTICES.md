# Third-party notices

voidImageViewer_PLUS is a fork of voidtools/voidImageViewer and vendors a
decode-only subset of Google's libwebp. The license texts live in this
repository; this file maps them to their sources.

## voidtools/voidImageViewer (the upstream project)

- Upstream source: https://github.com/voidtools/voidImageViewer
- Upstream version this fork carries its lineage from: 1.0.0.15
- License: MIT — Copyright 2025 voidtools / David Carpenter. The full
  text is the `voidImageViewer` section of [LICENSE](LICENSE).

## Google libwebp (the vendored WEBP decoder)

- Upstream source: https://github.com/webmproject/libwebp
- Vendored version: 1.6.0 (stable, announced 2025-06-30)
- Source tarball: `libwebp-1.6.0.tar.gz` (the GitHub `v1.6.0` tag
  archive), SHA-256
  `93a852c2b3efafee3723efd4636de855b46f9fe1efddd607e1f42f60fc8f2136`
- Imported: 2026-09-04
- What is vendored: the upstream **decode-only** file set
  (libwebpdecoder semantics) as classified by libwebp 1.6.0's own build
  files — `src/dec` (10 files), `src/demux` (2), the `src/dsp` decode
  set (44) and the `src/utils` decode set (10), 66 compiled entries in
  the shared project file. The WebP **encoder** is deliberately absent
  from both the tree and the shipped binary: the 1.6.0 import switched
  from a coarser prune that had kept encoder-side `.c` files in
  `src/dsp` and `src/utils` compiling into the exe, so the decode-only
  switch is the attack-surface reduction the pruning always intended.
- What is trimmed and why: everything the decode set does not need —
  the non-Windows build systems, fuzzers, examples, docs and language
  bindings (`webp_js/`, `examples/`, `imageio/`, `swig/`, `man/`,
  `gradle/`, `infra/`, `extras/`, `sharpyuv/`), the encoder trees
  (`src/enc/`, `src/mux/`) and the encoder-side `.c` files inside
  `src/dsp` and `src/utils`. The decode set was verified self-contained
  by an include-closure scan: no kept file references anything pruned.
- Kept beyond the compiled set: `tests/`, `doc/`, `cmake/`, the upstream
  headers, `COPYING`, `PATENTS`, `AUTHORS`, `ChangeLog`, `NEWS`,
  `README.md` and the top-level build files.
- License: BSD-3-Clause — see [libwebp/COPYING](libwebp/COPYING); the
  WebP patent grant is in [libwebp/PATENTS](libwebp/PATENTS). The BSD
  notice is also reproduced in the `webp` section of [LICENSE](LICENSE).

The exact vendored file list and the full provenance record — upstream
URL, version, tarball SHA-256, import history and the post-import sweep
— live in [libwebp/VERSION.imported](libwebp/VERSION.imported).
`tools/update_libwebp.py` verifies the tree against that record
(`--check`, offline) and performs version bumps (`--fetch`); refresh the
vendored copy through that tool, never by hand.
