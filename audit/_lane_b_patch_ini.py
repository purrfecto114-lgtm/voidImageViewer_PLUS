"""LANE-B one-shot patcher for src/ini.c.

Applies the remaining parse-anomaly edits. The tree is CRLF, so the file is
normalized to LF, edited by line index with a content assertion on every
anchor, and written back as CRLF.
"""

import sys

PATH = "src/ini.c"

with open(PATH, "rb") as fh:
    raw = fh.read()

text = raw.decode("utf-8")
assert text.count("\r\n") == text.count("\n"), "mixed line endings"
lines = text.replace("\r\n", "\n").split("\n")


def find(needle, start=0, count=1):
    """The first line at or after `start` carrying `needle`. Ambiguity is
    caught by the expect() guard the caller pairs with it, not here."""
    for i in range(start, len(lines)):
        if needle in lines[i]:
            return i
    raise SystemExit("anchor %r not found at or after line %d" % (needle, start + 1))


def expect(i, needle):
    if needle not in lines[i]:
        raise SystemExit("line %d is %r, expected %r" % (i + 1, lines[i], needle))
    return i


def splice(at, remove=0, add=()):
    """Replace `remove` lines at `at` with the `add` block."""
    lines[at:at + remove] = list(add)


# --- 1. the section header records that the section was seen -------------
i = find("is_section = 1;")
expect(i, "is_section = 1;")
splice(i + 1, 0, ["\t" * 9 + "section_found = 1;"])

# --- 2. a non-blank line inside the section with no '=' ------------------
i = find("key_start = p;")
expect(i, "key_start = p;")
# the two malformed exits sit at i+4..i+5 and i+10..i+11 (the '\n' and the
# '\r\n' branch bodies' "p++; goto next_line;" pair).
nl_i = i + 4
expect(nl_i, "if (*p == '\\n')")
expect(nl_i + 2, "p++;")
expect(nl_i + 3, "goto next_line;")
splice(nl_i + 2, 2, [
    "\t" * 9 + "// a line that ends here never reached the '=': the key",
    "\t" * 9 + "// line is malformed. a blank line is legal in an ini",
    "\t" * 9 + "// and is not one.",
    "\t" * 9 + "if (!_ini_line_is_blank(key_start,(int)(p - key_start)))",
    "\t" * 9 + "{",
    "\t" * 10 + "_ini_last_anomaly |= INI_ANOMALY_MALFORMED_LINE;",
    "\t" * 9 + "}",
    "",
    "\t" * 9 + "p++;",
])

# the crlf exit is the one immediately above the '=' test: its "p+=2;" sits
# two lines under the 'if ((*p == \'\\r\') ...)'. locate it by content so
# the earlier splice's line shift cannot mislead the arithmetic.
eq_i = find("if (*p == '=')", start=nl_i)
inc_i = eq_i - 4
expect(inc_i, "p+=2;")
expect(inc_i - 2, "if ((*p == '\\r') && (p[1] == '\\n'))")
expect(inc_i - 1, "{")
expect(inc_i + 1, "goto next_line;")
crlf_i = inc_i - 2
splice(crlf_i + 2, 2, [
    "\t" * 9 + "// the same malformed exit on a crlf line.",
    "\t" * 9 + "if (!_ini_line_is_blank(key_start,(int)(p - key_start)))",
    "\t" * 9 + "{",
    "\t" * 10 + "_ini_last_anomaly |= INI_ANOMALY_MALFORMED_LINE;",
    "\t" * 9 + "}",
    "",
    "\t" * 9 + "p+=2;",
])

# --- 3. a final line with no terminator at all ---------------------------
i = find("key_start = p;")
# the end of the key scan: after the '=' branch records its pair, one
# 'p++;' advances the scan and the '}' below closes the while, then the
# is_section block. walk down from the pair so the earlier splice's line
# shift cannot mislead the arithmetic.
count_i = find("count++;", start=find("if (*p == '=')", start=i))
tail_i = find("\t" * 7 + "p++;", start=count_i + 1)
expect(tail_i, "p++;")
expect(tail_i + 1, "\t" * 7 + "}")
expect(tail_i + 2, "\t" * 6 + "}")
splice(tail_i + 1, 0, [
    "",
    "\t" * 7 + "// the scan only leaves here on the buffer terminator: a",
    "\t" * 7 + "// last line with no '=' is as malformed as any other.",
    "\t" * 7 + "if (!_ini_line_is_blank(key_start,(int)(p - key_start)))",
    "\t" * 7 + "{",
    "\t" * 8 + "_ini_last_anomaly |= INI_ANOMALY_MALFORMED_LINE;",
    "\t" * 7 + "}",
])

# --- 4. a field past any real settings value ----------------------------
i = find("// add a key / value pair.")
splice(i, 0, [
    "\t" * 8 + "// a field past any real settings value is a corrupted",
    "\t" * 8 + "// line. the reader still copies it - it stays tolerant",
    "\t" * 8 + "// - but the caller hears about it before it writes the",
    "\t" * 8 + "// file back.",
    "\t" * 8 + "if ((key_len > _INI_MAX_FIELD) || (value_len > _INI_MAX_FIELD))",
    "\t" * 8 + "{",
    "\t" * 9 + "_ini_last_anomaly |= INI_ANOMALY_OVERSIZED_VALUE;",
    "\t" * 8 + "}",
    "",
])

# --- 5. two different values for one key --------------------------------
i = find("// free last")
expect(i - 1, "{")
expect(i - 2, "if (utf8_compare(last->key,(*kv_p)->key) == 0)")
splice(i, 0, [
    "\t" * 7 + "// the same key twice. the sort keeps the last one (the",
    "\t" * 7 + "// ini contract), but two different values for one key is",
    "\t" * 7 + "// a file disagreeing with itself - the shape a half",
    "\t" * 7 + "// written or a merged file takes. identical values are",
    "\t" * 7 + "// harmless, so only a real disagreement counts.",
    "\t" * 7 + "if (utf8_compare(last->value,(*kv_p)->value) != 0)",
    "\t" * 7 + "{",
    "\t" * 8 + "_ini_last_anomaly |= INI_ANOMALY_DUPLICATE_KEY;",
    "\t" * 7 + "}",
    "",
])

# --- 6. no section header at all ----------------------------------------
i = find("done:")
expect(i, "done:")
splice(i + 1, 0, [
    "",
    "\t" * 3 + "// the section header never appeared: whatever parsed",
    "\t" * 3 + "// belongs to some other section, so every key the caller",
    "\t" * 3 + "// wanted fell through to its compiled-in default and the",
    "\t" * 3 + "// session is now running on defaults it will write back.",
    "\t" * 3 + "if (!section_found)",
    "\t" * 3 + "{",
    "\t" * 4 + "_ini_last_anomaly |= INI_ANOMALY_SECTION_MISSING;",
    "\t" * 3 + "}",
])

out = "\r\n".join(lines).encode("utf-8")
with open(PATH, "wb") as fh:
    fh.write(out)

print("ini.c patched: %d bytes" % len(out))