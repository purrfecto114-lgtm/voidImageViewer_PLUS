"""LANE-B: swap ini.c's C-runtime memcpy/memcmp for this tree's own helpers."""

PATH = "src/ini.c"

with open(PATH, "rb") as fh:
    d = fh.read().decode("utf-8")

lines = d.replace("\r\n", "\n").split("\n")

assert "memcpy(needle + 1,ascii_section,needle_len - 2);" in lines[161], lines[161]
assert "if (memcmp(buf + i,needle,needle_len) == 0)" in lines[167], lines[167]

lines[161] = lines[161].replace("memcpy(", "os_copy_memory(")

lines[167:173] = [
    "\t\t\t\t\t// the byte walk: this tree's vocabulary has no memcmp.",
    "\t\t\t\t\tint same;",
    "\t\t\t\t\tDWORD j;",
    "",
    "\t\t\t\t\tsame = 1;",
    "",
    "\t\t\t\t\tfor(j=0;j<needle_len;j++)",
    "\t\t\t\t\t{",
    "\t\t\t\t\t\tif ((utf8_t)buf[i + j] != needle[j])",
    "\t\t\t\t\t\t{",
    "\t\t\t\t\t\t\tsame = 0;",
    "\t\t\t\t\t\t\tbreak;",
    "\t\t\t\t\t\t}",
    "\t\t\t\t\t}",
    "",
    "\t\t\t\t\tif (same)",
    "\t\t\t\t\t{",
    "\t\t\t\t\t\tfound = 1;",
    "\t\t\t\t\t\tbreak;",
    "\t\t\t\t\t}",
]

out = "\r\n".join(lines).encode("utf-8")
with open(PATH, "wb") as fh:
    fh.write(out)

print("ini.c: memcpy/memcmp -> os_copy_memory / byte walk (%d bytes)" % len(out))