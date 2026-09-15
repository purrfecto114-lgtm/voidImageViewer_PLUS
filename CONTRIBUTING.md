# Contributing to voidImageViewer_PLUS

voidImageViewer_PLUS is a lightweight Windows image viewer written in
plain C89 against the Win32 API — a fork of
[voidtools/voidImageViewer](https://github.com/voidtools/voidImageViewer)
focused on touch and gesture support, rendering (the GDI/OpenGL/Direct3D
pipeline and the dark UI) and bilingual localization (English /
简体中文). Bug reports, anomaly sample files, features and pull requests
are all welcome. Pull requests target `main`; a PR that changes `src/`
should fill in the pull request template's testing section.

## Building

**Visual Studio (the shipping configuration).** VS2022 or newer with the
v143 toolset, through the `vs2019` project (the directory name is
historical — the toolset is what matters):

```
msbuild vs2019\voidImageViewer.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 "/p:OutDir=build\x64\\" "/p:IntDir=build\obj\x64\\" /m /nologo /v:m
msbuild vs2019\voidImageViewer.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v143 "/p:OutDir=build\x86\\" "/p:IntDir=build\obj\x86\\" /m /nologo /v:m
```

Or open `vs2019\voidImageViewer.sln` in the IDE and build the
`voidImageViewer` project (x64 or Win32). The `vs2026` project
(`vs2026\voidImageViewer.vcxproj`, v145 toolset) is the
forward-compatibility build; both projects share one file list
(`voidImageViewer.files.props`). These are the same command lines the CI
runs — see `.github/workflows/tests.yml`.

**No Visual Studio (the zig cross build).** Contributors on non-Windows
machines can compile the same sources with zig cc:

```
pip install ziglang
sh build-zig/build.sh
```

The script compiles every translation unit in `build-zig/files.txt` and
links a 64-bit `build-zig/viv.exe`, writing `build-zig/viv.exe.manifest`
next to it (the zig build embeds no manifest). Keep the two files
together, or build through Visual Studio when you need a single-file
binary.

**Optional installer.** NSIS 3 via `nsis\build_installer.ps1` (the script
auto-detects the Visual Studio version; the sources compile with
`/utf-8`).

## The test matrix

Four Python suites — all stdlib-only, no pip installs, runnable directly
on any platform:

```
python3 tests/zoom_math_test.py       # the zoom/fit math
python3 tests/menu_structure_test.py  # menu table, dark-mode wiring, localization, version consistency
python3 tests/simulation_test.py      # the second-rework simulation (the 1.1.07 check)
python3 tests/pixel_golden_test.py    # the pixel-golden harness structure
```

On Windows (PowerShell 5.1+), two real-machine scripts:

```
powershell -ExecutionPolicy Bypass -File tests\smoke_test.ps1 -ExePath build\x64\voidImageViewer.exe -SamplesDir tests\samples
powershell -ExecutionPolicy Bypass -File tests\render_golden.ps1 -ExePath build\x64\voidImageViewer.exe -SamplesDir tests\samples
```

`smoke_test.ps1` opens the whole anomaly sample set through the exe and
watches for crashes (the samples generate inline with pure-stdlib Python
when the directory is missing); `render_golden.ps1` runs the pixel
oracle described below. CI (`.github/workflows/tests.yml`) runs the four
Python suites on every push, pull request and tag, and compiles both
platforms on the pinned `windows-2022` (v143) and `windows-2025` (v145)
images, with the smoke and golden legs riding the windows-2022 one.

## Version rules

- `src/version.h` is the single source of truth. `VERSION_STRING` (for
  example `1.1.14-rc.5`) is what the release tag, the installer names and
  the version resource display; the NSIS installer derives everything
  from it at compile time (`nsis/version.nsh`). Bumping the version means
  editing that one file, plus the `Changes.txt` entry.
- A release tag must match `VERSION_STRING` exactly (an optional `v`
  prefix aside) — the release pipeline refuses to run otherwise. Tag
  shape: `1.1.14`, `1.1.14-rc.5`, or the legacy `v1.1.0-rc.7` style.
- rc → stable promotion: drop the `-rc.N` suffix from `VERSION_STRING`,
  set `VERSION_TYPE` to the empty string, bump `VERSION_BUILD`, add the
  `Changes.txt` section. The release notes banner switches from
  pre-release to stable wording on its own.
- The release pipeline is `.github/workflows/release.yml`, gated
  validate → tests → build → publish: the tag is validated against
  `VERSION_STRING` and the tag whitelist, all four suites run on the
  tagged commit, the shipping binaries pass the smoke test and the
  strict pixel-golden comparison before anything is packaged, and publish
  re-verifies every asset SHA-256 after artifact transport and refuses to
  overwrite an existing release. `-rc`/`-beta` tags publish as
  pre-releases.

## The vendored libwebp

`libwebp/` is a vendored, decode-only subset of Google's libwebp (see
`THIRD_PARTY_NOTICES.md`). `libwebp/VERSION.imported` is its provenance
record: the upstream URL, the exact version, the tarball SHA-256 and the
precise file set. Work with it only through `tools/update_libwebp.py`:

- `python3 tools/update_libwebp.py --check` verifies the tree offline
  against `VERSION.imported` — run it whenever anything under `libwebp/`
  was touched, even by accident.
- `python3 tools/update_libwebp.py --fetch` performs a version bump: it
  fetches the upstream release, verifies it against the recorded tarball
  hash, re-lands the decode-only subset and rewrites `VERSION.imported`.

Never hand-edit files under `libwebp/`.

## The pixel-golden manifest

`tests/golden/golden-manifest.json` pins the SHA-256 of the rendered
bitmaps the hidden render export (`-render-export`) produces for the
golden sample set, per renderer. It is regenerated in exactly one way:

1. Push the commit whose pixels you intend to pin.
2. Run the `tests` workflow by dispatch (Actions → tests → Run workflow)
   with the `bootstrap-golden` input checked.
3. Download the `golden-manifest` artifact and commit it to the tree.

Never hand-edit the manifest. The bootstrap goes through the dispatch on
purpose: the hashes must come from the same environment CI verifies in.
The runner's 96 DPI desktop gives the export window its exact canvas,
while a developer display above 100% scaling makes the window manager
refuse the exact client rect and the harness exits before hashing
anything. Once the manifest is tracked, every push compares strictly and
every release compares the shipping binary strictly — so a deliberate
pixel change rides the bootstrap flow, not the editor.

## Code style

- C89: plain C with the Win32 API, nothing newer relied on.
- Comments are lowercase prose that explain **why** — the code already
  says what it does. The whole tree reads this way; keep it that way.
- Indentation in `src/*.c` and `src/*.h` is tabs: match the surrounding
  file; a space-indented new line reads as a diff artifact here.
- Line endings are per-file and must stay consistent within each file:
  `src/` and `Changes.txt` are CRLF; this readme and the `.github/`
  files are LF. Never mix endings inside one file.
- Sources compile with `/utf-8`; non-ASCII literals (the Simplified
  Chinese strings) are UTF-8 by contract.
