---
name: Bug report
about: The viewer crashes, hangs or renders something wrong
title: ''
labels: bug
assignees: ''
---

**Windows version:**
[e.g. Windows 11 23H2, Windows 10 22H2, Windows 7 SP1 — the viewer requires Windows 7 or later; x86 or x64]

**App version:**
[The release tag, e.g. 1.1.15 or 1.1.16-rc.1 — or "portable zip" and which one (x64 / x86)]

**The image file:**
- Format (BMP, GIF, ICO, PNG, JPG, TIF, WEBP, JPEG-XR, HEIF, AVIF, DDS, QOI, EMF, WMF — or unknown):
- Dimensions (if known):
- Animated (GIF / WEBP): yes / no / unsure:
- Where it came from, if known (camera, editor, export tool, download, corrupted in transit):

**Repro steps:**
1.
2.

**Expected behavior:**

**Actual behavior:**

**Crash or wrong render?**
- [ ] It crashes or hangs (say which, and what appears — a dialog, a silent exit, a frozen window)
- [ ] It renders wrong (describe what is wrong: colors, size, position, animation frame, transparency)
- [ ] Something else (describe it)

**Notes:**
The viewer is built to tolerate hostile and corrupted image files — it enforces a pixel budget and its test suite opens a whole set of anomaly samples on every change. A file that crashes it, hangs it or makes it consume unreasonable resources is a bug this report should carry. Anomaly-tolerant sample files are very welcome as attachments to this issue (zip them if GitHub refuses the raw extension); your sample may join the automated set that keeps the fix from regressing.
