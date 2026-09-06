#!/usr/bin/env python3
"""R20 round 20: version display unification.

Phase RED: update t_version guards to the new expectations + add the
display-anchoring guards, run the suite, and assert the exact expected
failures (the guards must be proven real before the fix lands).
"""
import re
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
TEST = ROOT / "tests" / "menu_structure_test.py"

src = TEST.read_bytes().decode()

# 1. expected version identity 1.0.02 -> 1.0.03, build 25 -> 26
old_expect = '''    check("version.h = 1.0.2.25 stable (the fork line continues from the restart)",
          (major, minor, rev, build) == ("1", "0", "2", "25") and vtype == "")
    check("VERSION_STRING is the release identity (the stable tag)",
          vstr == "1.0.02")'''
new_expect = '''    check("version.h = 1.0.3.26 stable (the fork line continues from the restart)",
          (major, minor, rev, build) == ("1", "0", "3", "26") and vtype == "")
    check("VERSION_STRING is the release identity (the stable tag)",
          vstr == "1.0.03")'''
assert src.count(old_expect) == 1, "t_version expectation block not found"
src = src.replace(old_expect, new_expect)

# 2. new display-anchoring guards appended to t_version
old_tail = '''    check("nsh has no hardcoded version left",
          '"1.1.0.' not in nsh and '"-rc.' not in nsh and '"1.1.01"' not in nsh
          and '"1.0.01"' not in nsh)'''
new_tail = old_tail + '''

    # r20: every user-visible version display must anchor to VERSION_STRING,
    # not to a separately formatted %d sequence (the about dialog showed
    # 1.0.2.25, the uninstall entry showed 1.0.2, while the tag said 1.0.02).
    viv = read("src/viv.c").decode()
    check("about dialog shows the release identity string",
          'string_printf(version_wbuf,"%s%s %s",VERSION_STRING,VERSION_TYPE,VERSION_TARGET_MACHINE);' in viv)
    check("uninstall DisplayVersion shows the release identity string",
          'string_printf(version_wbuf,"%s%s",VERSION_STRING,VERSION_TYPE);' in viv)
    check("debug banner shows the identity string with the build counter",
          'debug_printf("viv %s%s (build %d) %s\\n",VERSION_STRING,VERSION_TYPE,VERSION_BUILD,VERSION_TARGET_MACHINE);' in viv)
    check("no %d.%d.%d version formatting survives in viv.c",
          re.findall(r"%d\.%d\.%d", viv) == [])
    nsi = read("nsis/installer.nsi").decode()
    check("installer version keys carry the identity, not the machine suffix",
          'VIAddVersionKey "FileVersion" "${DISPLAYVERSION}"' in nsi and
          'VIAddVersionKey "ProductVersion" "${DISPLAYVERSION}"' in nsi and
          'VIAddVersionKey "FileVersion" "${DISPLAYVERSION}.${TARGETMACHINE}"' not in nsi and
          'VIAddVersionKey "ProductVersion" "${DISPLAYVERSION}.${TARGETMACHINE}"' not in nsi)'''
assert src.count(old_tail) == 1, "t_version tail block not found"
src = src.replace(old_tail, new_tail)

TEST.write_bytes(src.encode())
print("RED: guards updated (version 1.0.03/26 + 5 display anchoring checks)")
