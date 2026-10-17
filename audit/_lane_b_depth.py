"""LANE-B: print the brace depth carried into each source line of one file."""

import sys

path = sys.argv[1]
lo = int(sys.argv[2])
hi = int(sys.argv[3])

src = open(path, "rb").read().decode("utf-8")

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

body = "".join(out)
depth = 0
line = 1
enter = {}
for ch in body:
    if ch == "{":
        enter[line] = depth
        depth += 1
    elif ch == "}":
        depth -= 1
        enter[line] = depth
    elif ch == "\n":
        line += 1

srclines = src.replace("\r\n", "\n").split("\n")
for ln in range(lo, min(hi, len(srclines)) + 1):
    d = enter.get(ln)
    if d is not None:
        print("%d depth-after %s :: %s" % (ln, d, srclines[ln - 1].strip()[:64]))
print("final depth", depth)