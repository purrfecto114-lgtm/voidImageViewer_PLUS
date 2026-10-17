"""LANE-B helper: a crude C brace balance check.

Not a parser - it strips comments and string/char literals (keeping the
line map intact) and then counts the braces. Enough to catch a botched
splice in a tree whose compiler gate cannot run here (this box has the MSVC
build tools but no Windows SDK).
"""

import sys


def scan(path):
    src = open(path, "rb").read().decode("utf-8", errors="replace")
    out = []
    i = 0
    n = len(src)
    state = None
    while i < n:
        c = src[i]
        if state == "c":
            if c == "*" and i + 1 < n and src[i + 1] == "/":
                state = None
                out.append("  ")
                i += 2
                continue
            if c == "\n":
                out.append(c)
            i += 1
            continue
        if state is not None:
            if c == "\\":
                out.append("  ")
                i += 2
                continue
            if c == state:
                state = None
                out.append(" ")
                i += 1
                continue
            if c == "\n":
                out.append(c)
            i += 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            state = "c"
            i += 2
            continue
        if c in "\"'":
            state = c
            out.append(" ")
            i += 1
            continue
        out.append(c)
        i += 1
    return src, "".join(out)


def check(path):
    src, body = scan(path)
    depth = 0
    line = 1
    bad = []
    if len(src.split("\n")) != len(body.split("\n")):
        bad.append("the strip lost a line (%d vs %d)"
                   % (len(src.split("\n")), len(body.split("\n"))))
    last0 = 0
    for ch in body:
        if ch == "\n":
            line += 1
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                last0 = line
            if depth < 0:
                bad.append("stray '}' at line %d" % line)
                break
    if depth != 0:
        bad.append("final depth %d (last balanced at line %d)" % (depth, last0))
    return bad


rc = 0
for path in sys.argv[1:]:
    bad = check(path)
    print(("ok   " if not bad else "FAIL ") + path + (" :: " + "; ".join(bad) if bad else ""))
    if bad:
        rc = 1
sys.exit(rc)