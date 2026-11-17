#!/usr/bin/env python3
"""Byte-invariant guards: line endings, BOMs and final newlines.

The corrections review's must-fix finding: an editor tool has silently
rewritten bytes in this tree four recorded times (tabs widened to
spaces, line endings converted mid-file), and nothing in the repo
could see the class of change - the guard suites pin text shapes, the
pixel oracle pins decoded output, but nobody pinned the file bytes
themselves. Two of the encounters shipped partway before a suite
caught them by accident; the stragglers this suite was born from were
found while writing it (an entire C source still on LF endings, nine
CRLF lines pasted into the middle of this suite's own file).

The invariants pin what the tree actually carries, per class:

- src/*.c and src/*.h: CRLF line endings only, no BOM - the windows
  native form the C89 sources have always carried;
- Changes.txt: CRLF line endings and the UTF-8 BOM the release
  pipeline's derivation scripts read;
- the python, powershell, markdown, yaml, shell and plain-text layers
  (.editorconfig's [*.{py,ps1,md,yml,yaml,sh,txt}] glob): LF line
  endings only, no BOM - the unix native form. the nsis corner
  (the installer license texts) mixes encodings by upstream design
  and stays out, exactly as the .editorconfig's own header
  describes that corner; the guarded txt class is the tree's own
  plain-text list file.

A file that mixes endings inside one class is the exact corruption
signature this suite exists to catch, so the check is per file, not
per tree: every line must end the way its class spells it.

This suite runs anywhere python runs (the ubuntu leg) and reads the
working tree - it needs no git history and no build.
"""

import glob
import os
import sys

failures = []


def check(name, ok, detail=""):
    tag = "ok  " if ok else "FAIL"
    print("%s %s %s" % (tag, name, detail))
    if not ok:
        failures.append(name)


def endings(path):
    """Returns (crlf, bare_lf, bare_cr, has_bom, ends_with_newline)."""
    b = open(path, "rb").read()
    crlf = b.count(b"\r\n")
    bare_lf = b.count(b"\n") - crlf
    bare_cr = b.count(b"\r") - crlf
    has_bom = b[:3] == b"\xef\xbb\xbf"
    ends_nl = len(b) > 0 and b[-1:] in (b"\n", b"\r")
    return crlf, bare_lf, bare_cr, has_bom, ends_nl


def audit_class(label, paths, want_crlf, want_bom=False):
    """Checks one convention class; collects per-file verdicts."""
    files = []
    for pattern in paths:
        # recursive=True or the ** in "docs/**/*.md" matches exactly one
        # directory and everything deeper silently falls outside the
        # class (the audit's finding - a guard whose population is
        # smaller than its own pattern).
        files.extend(glob.glob(pattern, recursive=True))
    files = sorted(set(files))
    check("the %s class has files to guard" % label, len(files) > 0,
          "(%d files)" % len(files))

    mixed = []
    wrong_bom = []
    no_final_nl = []
    for f in files:
        crlf, bare_lf, bare_cr, has_bom, ends_nl = endings(f)
        if want_crlf:
            bad = bare_lf != 0 or bare_cr != 0
        else:
            bad = crlf != 0 or bare_cr != 0
        if bad:
            mixed.append("%s (crlf=%d bare_lf=%d bare_cr=%d)"
                         % (f, crlf, bare_lf, bare_cr))
        if has_bom != want_bom:
            wrong_bom.append("%s (bom=%s)" % (f, has_bom))
        if not ends_nl:
            no_final_nl.append(f)

    check("every %s file carries one line-ending spelling end to end" % label,
          not mixed, "; ".join(mixed))
    check("the %s class BOM discipline holds (%s)" %
          (label, "BOM required" if want_bom else "no BOM"),
          not wrong_bom, "; ".join(wrong_bom))
    check("every %s file ends with a newline" % label,
          not no_final_nl, "; ".join(no_final_nl))
    return files


def main():
    root = os.getcwd()

    # 1. the C89 sources: CRLF, no BOM.
    c_files = audit_class("C89 source", [os.path.join(root, "src", "*.c"),
                                         os.path.join(root, "src", "*.h")],
                          want_crlf=True, want_bom=False)

    # 2. the changelog: CRLF and the UTF-8 BOM.
    changes = os.path.join(root, "Changes.txt")
    check("Changes.txt exists", os.path.exists(changes))
    if os.path.exists(changes):
        crlf, bare_lf, bare_cr, has_bom, ends_nl = endings(changes)
        check("Changes.txt is CRLF end to end",
              bare_lf == 0 and bare_cr == 0,
              "(crlf=%d bare_lf=%d bare_cr=%d)" % (crlf, bare_lf, bare_cr))
        check("Changes.txt carries its UTF-8 BOM", has_bom)
        check("Changes.txt ends with a newline", ends_nl)

    # 3. the unix-native layers: LF, no BOM.
    py_files = audit_class("python", [os.path.join(root, "tests", "*.py"),
                                      os.path.join(root, "tools", "*.py")],
                           want_crlf=False, want_bom=False)
    ps_files = audit_class("powershell", [os.path.join(root, "tests", "*.ps1"),
                                          os.path.join(root, "tools", "*.ps1")],
                           want_crlf=False, want_bom=False)
    # the markdown class covers the github tree too - the issue and
    # pr templates are markdown the editorconfig's glob declares,
    # and the class used to stop at the root and docs (the same
    # coverage hole the shell and yaml legs close this round).
    md_files = audit_class("markdown", [os.path.join(root, "*.md"),
                                        os.path.join(root, "docs", "**", "*.md"),
                                        os.path.join(root, ".github", "**", "*.md")],
                           want_crlf=False, want_bom=False)
    # the github yaml class covers the whole .github tree - the
    # workflows, the dependabot roster and the codeql configs (the
    # workflows-only pattern certified a narrower class than the
    # editorconfig's glob declares).
    yml_files = audit_class("github yaml",
                            [os.path.join(root, ".github", "**", "*.yml")],
                            want_crlf=False, want_bom=False)
    # the shell and plain-text legs the editorconfig declares and the
    # suite never watched: the zig build scripts (the arm64 leg of
    # these very scripts runs in ci) and the zig file list. the nsis
    # license texts are the mixed corner the editorconfig's own
    # header names and stay out of every class here.
    sh_files = audit_class("shell", [os.path.join(root, "build-zig", "*.sh")],
                           want_crlf=False, want_bom=False)
    txt_files = audit_class("plain text", [os.path.join(root, "build-zig",
                                                          "*.txt")],
                            want_crlf=False, want_bom=False)

    # 4. the editorconfig must not lie about the classes it names: the
    # spelling it records for the C sources and the unix layers is the
    # spelling this suite just verified.
    ec = os.path.join(root, ".editorconfig")
    check("the editorconfig exists beside the invariants it describes",
          os.path.exists(ec))
    if os.path.exists(ec):
        text = open(ec, "rb").read().decode("utf-8", errors="replace")
        check("the editorconfig states the C layer as crlf + tab",
              "end_of_line = crlf" in text and "indent_style = tab" in text)
        check("the editorconfig states the unix layers as lf + space",
              "end_of_line = lf" in text and "indent_style = space" in text)
        check("the editorconfig states the changelog BOM",
              "charset = utf-8-bom" in text)
        check("the editorconfig keeps its [Changes.txt] section after the"
              " general txt glob (later sections win - the wrong order"
              " would flip the changelog to lf)",
              text.find("\n[Changes.txt]") >
              text.find("\n[*.{py,ps1,md,yml,yaml,sh,txt}]"))

    # 5. the guarded population itself: a class that silently empties
    # (a rename, a move) would otherwise pass by vacuity.
    check("the guarded population is the full tree this suite was born"
          " from (src %d, py %d, ps1 %d, md %d, yml %d, sh %d, txt %d)" %
          (len(c_files), len(py_files), len(ps_files), len(md_files),
           len(yml_files), len(sh_files), len(txt_files)),
          len(c_files) > 80 and len(py_files) >= 6 and len(ps_files) >= 3
          and len(md_files) >= 6 and len(yml_files) == 6
          and len(sh_files) == 3 and len(txt_files) == 1)

    print()
    if failures:
        print("%d FAILURE(S)" % len(failures))
        sys.exit(1)
    print("ALL BYTE INVARIANT TESTS PASS")


if __name__ == "__main__":
    main()
