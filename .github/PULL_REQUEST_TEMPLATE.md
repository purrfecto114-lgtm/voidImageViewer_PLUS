# Summary

[What does this change do, and why? One short paragraph — the why matters more than the what.]

# Risk surface touched

- [ ] Decoders (GDI+ load paths, WIC, QOI, the vendored libwebp/WEBP)
- [ ] Renderers (GDI, OpenGL, Direct3D — texture handling, readback, context lifecycle)
- [ ] Zoom/fit math or the animation player
- [ ] Threads / the hidden render export pipeline
- [ ] Installer (NSIS), shell integration or file associations
- [ ] CI / workflows / test suites / golden manifest
- [ ] None of the above (docs, comments only)

# Testing evidence

Which of these ran, and on what platform (CI leg, local Windows, the zig build)?

- [ ] `python3 tests/zoom_math_test.py`
- [ ] `python3 tests/menu_structure_test.py`
- [ ] `python3 tests/simulation_test.py`
- [ ] `python3 tests/pixel_golden_test.py`
- [ ] `tests\smoke_test.ps1` — the real-machine anomaly sweep (Windows)
- [ ] `tests\render_golden.ps1` — the pixel golden comparison (Windows)

Platform(s):

# Pixel-golden impact

Does anything here change rendered pixels for any sample — decode, fit math, backdrop, alpha flattening, renderer sampling?

- [ ] No — rendered pixels stay byte-identical; the committed golden manifest still passes as is.
- [ ] Yes — `tests/golden/golden-manifest.json` must be regenerated through the `tests` workflow's `bootstrap-golden` dispatch input, and the regenerated manifest committed in this PR. The manifest is never hand-edited.

# Compatibility notes

- [ ] Windows 7 (the minimum supported system) builds and behaves
- [ ] x86 (Win32) configuration considered
- [ ] Touch / gesture paths considered
- [ ] N/A

# Rollback plan

[If this has to be reverted after a release, does the revert land cleanly? Is anything persisted (ini settings, registry, file associations, the golden manifest) that a revert would strand, and what happens to it?]
