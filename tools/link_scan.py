#!/usr/bin/env python3
"""Link-candidate symbol audit for the split tree.

The compile stage cannot see link errors. This tool audits both directions:

  forward:  every extern declared in viv_state.h / viv_*.h has exactly one
            non-static definition in some compile unit;
  reverse:  every file-scope static definition is referenced only inside
            its own unit (a cross-unit reference is an undeclared-identifier
            compile error waiting, or an unresolved external at link time).

Block comments are stripped before matching, so commented-out code does not
count as a reference. Exit status is nonzero when anything is off.

Usage: python3 tools/link_scan.py  (from the repository root)
"""
import re, glob, os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "src")
os.chdir(SRC)

KEYWORDS = {"return", "if", "else", "while", "for", "switch", "case",
            "break", "continue", "goto", "sizeof", "typedef", "extern",
            "static", "struct", "enum", "union"}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def extern_names(skipped):
    names = set()
    for h in ["viv_state.h", "os.h"] + sorted(glob.glob("viv_*.h")):
        text = strip_comments(open(h, encoding="utf-8", errors="replace").read())
        for m in re.finditer(r"^extern\s+(.+?);", text, re.M):
            decl = m.group(1)
            # round 135: the os domain's function-pointer externs
            # (extern RET (CALLCONV *name)(args)) are runtime-assigned
            # data - the assignment carries no type prefix, so the
            # definition scan can never see them. a pointer declarator
            # is a conservative skip: an unaudited name is safe, a
            # mis-parsed one (the return type as the name) is not.
            if re.search(r"\(\s*[A-Za-z_][A-Za-z0-9_]*\s*\*\s*[A-Za-z_][A-Za-z0-9_]*\s*\)", decl):
                fm0 = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(", decl)
                if fm0:
                    skipped.add(fm0.group(1))
                continue
            fm = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(", decl)
            if fm:
                names.add(fm.group(1))
            else:
                vm = re.search(r"([A-Za-z_][A-Za-z0-9_]*)((?:\[[^\]]*\])+)?\s*$", decl)
                if vm:
                    names.add(vm.group(1))
        # the remake domains declare their public api as plain prototypes
        # (no extern keyword): every single-line header prototype joins the
        # audit the same way (multi-line prototypes are conservative skips -
        # an unaudited name is safe, a mis-parsed one is not).
        if h != "viv_state.h":
            for line in text.split("\n"):
                s = line.strip()
                if (not s) or s.startswith(("#", "extern ", "typedef", "static", "struct", "enum", "union")):
                    continue
                if s.startswith("__declspec"):
                    s = re.sub(r"^__declspec\([^)]*\)\s*", "", s)
                if re.search(r"\(\s*[A-Za-z_][A-Za-z0-9_]*\s*\*\s*[A-Za-z_][A-Za-z0-9_]*\s*\)", s):
                    fm1 = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(", s)
                    if fm1:
                        skipped.add(fm1.group(1))
                    continue
                if not (s.endswith(");") and ("(" in s)):
                    continue
                fm = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(", s)
                if fm and fm.group(1) not in KEYWORDS:
                    names.add(fm.group(1))
    return names


def scan():
    units = ["viv.c", "os.c"] + sorted(glob.glob("viv_*.c"))
    unit_code = {u: strip_comments(open(u, encoding="utf-8", errors="replace").read()) for u in units}

    # forward: extern -> definition
    skipped = set()
    externs = extern_names(skipped)
    defs = {}
    for u, code in unit_code.items():
        for i, line in enumerate(code.split("\n"), 1):
            s = line.strip()
            if not s or s.startswith(("#", "extern ", "typedef")):
                continue
            if s.startswith("__declspec"):
                s = re.sub(r"^__declspec\([^)]*\)\s*", "", s)
            m = re.match(r"^((?:[A-Za-z_][A-Za-z0-9_]*[\s\*]+)+)([A-Za-z_][A-Za-z0-9_]*)\s*(\(|\[|=|;|,)", s)
            if not m:
                continue
            if any(tok in KEYWORDS for tok in m.group(1).split()):
                continue
            if m.group(2) in externs:
                # a line that ends in a semicolon and carries a parameter
                # list is a prototype, not a definition (the domains keep
                # local prototypes for their own public functions; the
                # variable externs keep their = / ; definitions). an
                # initializer kills the prototype reading - a cast on the
                # right of the = (the interlocked pointer forms landed the
                # first one) is not a parameter list, and c prototypes
                # carry no = at all.
                if s.endswith(";") and ("(" in s) and ("=" not in s):
                    continue
                defs.setdefault(m.group(2), []).append((u, i))

    # reverse: static definition -> cross-unit reference
    leaks = []
    static_skipped = set()
    for u, code in unit_code.items():
        statics = set()
        depth = 0
        for line in code.split("\n"):
            s = line.strip()
            if depth == 0:
                if s.startswith("static") and re.search(r"\(\s*[A-Za-z_][A-Za-z0-9_]*\s*\*\s*[A-Za-z_][A-Za-z0-9_]*\s*\)", s):
                    # a static pointer declarator: internal by name, skip -
                    # but the skip is counted, not silent (the audit's own
                    # blind spots are part of its report now).
                    fm2 = re.search(r"static\s+(?:[A-Za-z_][A-Za-z0-9_]*[\s\*]+)+([A-Za-z_][A-Za-z0-9_]*)\s*\(", s)
                    if fm2:
                        static_skipped.add(fm2.group(1))
                else:
                    m = re.match(r"^static\s+(?:[A-Za-z_][A-Za-z0-9_]*[\s\*]+)+([A-Za-z_][A-Za-z0-9_]*)\s*(\(|\[|=|;|,)", s)
                    if m:
                        statics.add(m.group(1))
            depth += s.count("{") - s.count("}")
            if depth < 0:
                depth = 0
        for sym in statics:
            for other, ocode in unit_code.items():
                if other == u:
                    continue
                if re.search(r"\b" + re.escape(sym) + r"\b", ocode):
                    leaks.append((sym, u, other))

    missing = sorted(n for n in externs if n not in defs)
    dupes = {n: v for n, v in defs.items() if len(v) > 1}
    print("extern declarations: %d, defined: %d" % (len(externs), len(defs)))
    print("MISSING DEFINITIONS (%d): %s" % (len(missing), missing))
    print("DUPLICATE DEFINITIONS (%d):" % len(dupes))
    for n, v in dupes.items():
        print("  %s %s" % (n, v))
    print("CROSS-UNIT STATIC LEAKS (%d):" % len(leaks))
    for sym, home, other in leaks:
        print("  %s (defined in %s) referenced by %s" % (sym, home, other))
    # the conservative skips, named and counted: an unaudited name is
    # safe, a mis-parsed one is not - but either way the audit's own
    # blind spots belong in the audit's report, not in its silence.
    print("SKIPPED BY THE CONSERVATIVE RULES (%d extern, %d static): %s / %s" %
          (len(skipped), len(static_skipped), sorted(skipped), sorted(static_skipped)))
    return 0 if (not missing and not dupes and not leaks) else 1


if __name__ == "__main__":
    sys.exit(scan())
