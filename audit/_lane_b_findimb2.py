"""LANE-B: find where the src/ini.c strip diverges from the source line map."""

src = open("src/ini.c", "rb").read().decode("utf-8")

out = []
i = 0
n = len(src)
state = None
src_line = 1
marks = []  # (src_line, char)
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
            marks.append((src_line, "\n"))
            src_line += 1
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
            marks.append((src_line, "\n"))
            src_line += 1
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
    if c == "\n":
        marks.append((src_line, "\n"))
        src_line += 1
    i += 1

print("state at end:", state)

depth = 0
for ln, ch in marks:
    if ch == "{":
        depth += 1
    elif ch == "}":
        depth -= 1
        if depth == 0:
            last0 = ln
    else:
        if depth != 1:
            print("depth %d at line %d" % (depth, ln))

print("final depth", depth, "last depth-0 line", locals().get("last0"))