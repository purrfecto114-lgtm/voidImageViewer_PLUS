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

## QOI (the decode semantics ported into src/qoi.c)

- Upstream source: https://github.com/phoboslab/qoi — Dominic Szablewski,
  https://phoboslab.org
- License: MIT — Copyright (c) 2021, Dominic Szablewski. Full text below.
- What is ported: the opcode semantics of the reference `qoi.h` decoder —
  the index hash, the diff and luma deltas, the run bias of -1, and rgb
  chunks preserving the running alpha (the reference's exact behavior,
  not the intuitive one), as documented in the `src/qoi.c` file header.
  No QOI code is vendored wholesale; the hostile-input discipline around
  it (every read bounds-checked against the chunk end, the end-marker
  verification, the pixel budget) is this fork's own.
- The viewer ships this port in its binary; the MIT text below travels
  with it as part of this notices file.

```text
MIT License

Copyright (c) 2021, Dominic Szablewski, https://phoboslab.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
