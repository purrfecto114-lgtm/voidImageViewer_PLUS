"""LANE-B: locate the line where src/ini.c stops balancing."""

src = open("src/ini.c", "rb").read().decode("utf-8")

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

stripped = "".join(out)
print("src %d stripped %d" % (len(src), len(stripped)))

depth = 0
line = 1
prev_zero = 0
for ch in stripped:
    if ch == "\n":
        line += 1
    elif ch == "{":
        depth += 1
    elif ch == "}":
        depth -= 1
    if depth == 0 and ch in "{}":
        prev_zero = line
print("last depth-0 at line", prev_zero, "final depth", depth)

# print the depth at the top of each line between 100 and 150
depth = 0
line = 1
marks = []
for ch in stripped:
    if ch == "\n":
        line += 1
    elif ch == "{":
        depth += 1
    elif ch == "}":
        depth -= 1
    marks.append((line, depth))

at = {}
for ln, d in marks:
    at.setdefault(ln, d)

for ln in range(100, 150):
    print(ln, at.get(ln), repr(src.replace("\r\n", "\n").split("\n")[ln - 1][:70]))