# Security Policy

## Supported versions

The project ships a stable line and, while the next stable promotion is
being validated in the field, one release-candidate line at a time:

| Version | Line | Supported |
| --- | --- | --- |
| 1.1.14 | latest stable | yes |
| 1.1.15-rc.x (1.1.15-rc.12 at the time of writing) | current release candidate | yes, until the 1.1.15 stable promotion retires it |
| 1.1.13 and older | previous lines | no — please upgrade (stable or the current rc) and re-check before reporting |

The authoritative current values are `VERSION_STRING` in `src/version.h`
and the [releases page](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases);
when in doubt, check those first.

## Reporting a vulnerability

Please report privately through GitHub's built-in private vulnerability
reporting: open the repository's **Security** tab and click **Report a
vulnerability**, or go directly to
<https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/security/advisories/new>.
Private reporting keeps the details between you and the maintainer until a
fix and a coordinated advisory are ready.

An honest expectation: the project has one active maintainer, and the
response is best-effort - there is no dedicated security inbox or an SLA
behind the private channel, just the same GitHub account that ships the
releases. Reports are answered as fast as one person can.

Please do **not** open a public issue, discussion or pull request for
anything security-sensitive.

Where you can, include:

- the version you tested (the release tag, or "portable zip" plus which
  one, x64 or x86),
- the platform (Windows version; renderer, if you know it: GDI, OpenGL or
  Direct3D),
- the image file that triggers it, if any — attached or linked (anonymized
  if needed), and
- reproduction steps or a minimal proof of concept.

## What to expect

This is a single-maintainer project; response is best effort, and the
commitments are correspondingly modest:

- acknowledge privately reported vulnerabilities, target within 7 days,
- keep the reporter informed while the investigation and fix progress,
- publish the fix and the GitHub security advisory together (coordinated
  disclosure), after which the public issue tracker can be used freely,
- credit the reporter in the advisory, if they want the credit.

Security fixes land on `main` and ship with the next release; anything
severe enough to warrant an out-of-band patch release gets one.

## Scope

voidImageViewer is a desktop image viewer whose primary attack surface is
**untrusted image files**: files arrive from disk, shell associations and
drag-and-drop, and are decoded by the viewer's own code and by the system
codec stack. The viewer is built to tolerate hostile and corrupted input —
it enforces a pixel budget and the CI opens a whole set of anomaly samples
on every push — so any image file that crashes it, hangs it or blows past
that budget is a bug in scope.

In scope:

- everything under `src/`: the decoders (the built-in QOI and WEBP
  decoders, the WIC and GDI+ load paths), the renderers (GDI, OpenGL,
  Direct3D), the zoom/fit math, the animation player, the shell and
  recent-file handling, and the command line,
- the vendored libwebp decode-only subset under `libwebp/` — the encoder
  is deliberately absent from both the tree and the binary (see
  `THIRD_PARTY_NOTICES.md` and `libwebp/VERSION.imported`),
- the installer (`nsis/`), and
- the CI and release pipelines (`.github/workflows/`).

Out of scope:

- bugs in Windows itself or in GPU drivers (WIC, GDI+, Direct3D, OpenGL)
  — report those to Microsoft or the driver vendor. A reproduction here
  is still welcome, so a workaround can be looked for on the viewer side.
- vulnerabilities in upstream libwebp that are not reachable through this
  viewer — report those to the libwebp project, and mention them here too
  so the vendored copy can be refreshed (`tools/update_libwebp.py`).
- social engineering, phishing, or attacks on accounts or infrastructure
  rather than on the shipped code.

## Unsigned binaries and download verification

Release binaries are currently **unsigned** (an open-source code-signing
account is on the roadmap). Every release therefore publishes a
`sha256.txt` asset listing the SHA-256 checksum of each download, and the
README tells every user to verify against it before running. To verify:

```powershell
# PowerShell
Get-FileHash -Algorithm SHA256 .\voidImageViewer-1.1.14-x64-Setup.exe
```

```bat
:: or from cmd
certutil -hashfile voidImageViewer-1.1.14-x64-Setup.exe SHA256
```

Compare the output with the matching line in the release's `sha256.txt`
asset. The release pipeline hashes every asset at build time, carries the
hashes through the build job's outputs and re-verifies each file after
artifact transport before publishing, so a mismatch against the published
`sha256.txt` means the download was altered or corrupted in transit —
delete it and download again from the releases page.
