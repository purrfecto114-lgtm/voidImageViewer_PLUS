"""LANE-B: re-brace the ini_precheck substring scan after the memcmp swap."""

PATH = "src/ini.c"

with open(PATH, "rb") as fh:
    d = fh.read().decode("utf-8")

lines = d.replace("\r\n", "\n").split("\n")

i = next(n for n, l in enumerate(lines) if "no memcmp." in l)
assert lines[i + 1] == "\t" * 5 + "int same;", repr(lines[i + 1])
lines.insert(i + 1, "\t" * 5 + "{")

j = next(n for n, l in enumerate(lines) if l == "\t" * 4 + "mem_free(needle);")
# the scan's for-body close goes between the if-close and the spacer line.
assert lines[j - 1] == "\t" * 4, repr(lines[j - 1])
assert lines[j - 2] == "\t" * 5 + "}", repr(lines[j - 2])
lines.insert(j - 1, "\t" * 5 + "}")

out = "\r\n".join(lines).encode("utf-8")
with open(PATH, "wb") as fh:
    fh.write(out)

print("ini.c: substring scan re-braced (%d bytes)" % len(out))