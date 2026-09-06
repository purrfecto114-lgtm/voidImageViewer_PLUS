#!/usr/bin/env python3
"""R20 GREEN: unify every user-visible version display on VERSION_STRING.

Fixes:
  viv.c 5788  debug banner        "1.0.2.25" -> "1.0.03 (build 26)"
  viv.c 12598 about dialog        "1.0.2.25 (x64)" -> "1.0.03 (x64)"
  viv.c 16549 ARP DisplayVersion  "1.0.2" -> "1.0.03"
  installer.nsi VIAddVersionKey   "1.0.02.x64" -> "1.0.03"
  version.h                       1.0.02/25 -> 1.0.03/26
All replacements are byte-level without touching line endings.
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent

def repl(path, old, new, count=1):
    p = ROOT / path
    data = p.read_bytes()
    assert data.count(old.encode()) == count, f"{path}: expected {count} of {old[:60]!r}, got {data.count(old.encode())}"
    p.write_bytes(data.replace(old.encode(), new.encode()))
    print(f"  {path}: {count} replacement(s)")

# 1. viv.c - debug banner (line ~5788)
repl("src/viv.c",
     'debug_printf("viv %d.%d.%d.%d%s %s\\n",VERSION_MAJOR,VERSION_MINOR,VERSION_REVISION,VERSION_BUILD,VERSION_TYPE,VERSION_TARGET_MACHINE);',
     'debug_printf("viv %s%s (build %d) %s\\n",VERSION_STRING,VERSION_TYPE,VERSION_BUILD,VERSION_TARGET_MACHINE);')

# 2. viv.c - about dialog (line ~12598)
repl("src/viv.c",
     'string_printf(version_wbuf,"%d.%d.%d.%d%s %s",VERSION_MAJOR,VERSION_MINOR,VERSION_REVISION,VERSION_BUILD,VERSION_TYPE,VERSION_TARGET_MACHINE);',
     'string_printf(version_wbuf,"%s%s %s",VERSION_STRING,VERSION_TYPE,VERSION_TARGET_MACHINE);')

# 3. viv.c - add/remove programs DisplayVersion (line ~16549) + stale comment
repl("src/viv.c",
     '// 1.1.0-rc.2 (from version.h)\r\n\t\tstring_printf(version_wbuf,"%d.%d.%d%s",VERSION_MAJOR,VERSION_MINOR,VERSION_REVISION,VERSION_TYPE);',
     '// the release identity straight from version.h - must match the tag\r\n\t\tstring_printf(version_wbuf,"%s%s",VERSION_STRING,VERSION_TYPE);')

# 4. installer.nsi - version keys carry the identity, machine stays in the
#    file name and the branding text (it never belonged in a version field)
repl("nsis/installer.nsi",
     'VIAddVersionKey "FileVersion" "${DISPLAYVERSION}.${TARGETMACHINE}"',
     'VIAddVersionKey "FileVersion" "${DISPLAYVERSION}"')
repl("nsis/installer.nsi",
     'VIAddVersionKey "ProductVersion" "${DISPLAYVERSION}.${TARGETMACHINE}"',
     'VIAddVersionKey "ProductVersion" "${DISPLAYVERSION}"')

# 5. version.h - 1.0.02/25 -> 1.0.03/26 (CRLF file, single-line replacements)
repl("src/version.h", '#define VERSION_REVISION 2', '#define VERSION_REVISION 3')
repl("src/version.h", '#define VERSION_BUILD 25', '#define VERSION_BUILD 26')
repl("src/version.h", '#define VERSION_STRING "1.0.02"', '#define VERSION_STRING "1.0.03"')

# 6. Changes.txt - new 1.0.03 section (CRLF)
changes = ROOT / "Changes.txt"
data = changes.read_bytes()
section = (
    "Stable: Version 1.0.03 (version display unification)\r\n"
    "\tversion identity: every user visible version now shows the release\r\n"
    "\tidentity - the tag string. the about dialog showed 1.0.2.25, the\r\n"
    "\tadd/remove programs entry showed 1.0.2 and the installer version\r\n"
    "\tkeys showed 1.0.02.x64 while the tag said 1.0.02 - the about line,\r\n"
    "\tthe uninstall DisplayVersion, the debug banner and the two nsis\r\n"
    "\tversion keys now derive straight from VERSION_STRING, and the test\r\n"
    "\tsuite guards all five display points against regression.\r\n"
    "\r\n"
)
assert data.startswith(b"Stable: Version 1.0.02"), "Changes.txt head unexpected"
changes.write_bytes(section.encode() + data)
print("  Changes.txt: 1.0.03 section prepended (CRLF)")

# 7. README.md - What's new block (LF)
readme = ROOT / "README.md"
data = readme.read_bytes()
block = (
    "**1.0.03 — version display unification:**\n"
    "\n"
    "- **One identity everywhere** — the About dialog (was `1.0.2.25 (x64)`), the Settings→Apps uninstall entry (was `1.0.2`) and the installer's version keys (was `1.0.02.x64`) now all show the release identity that matches the git tag — e.g. `1.0.03 (x64)` in About. The debug banner prints the identity with the build counter (`viv 1.0.03 (build 26) (x64)`), and five new test guards keep every display point anchored to `VERSION_STRING` so the formats can never drift apart again.\n"
    "\n"
)
anchor = b"**1.0.02 \xe2\x80\x94 third audit round: decode budget + real machine smoke test:**"
assert data.count(anchor) == 1, "README anchor not found"
readme.write_bytes(block.encode() + data)
print("  README.md: 1.0.03 block prepended (LF)")
print("GREEN: all display points now anchor to VERSION_STRING")
