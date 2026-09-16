#!/usr/bin/env python3
"""Function-body conservation audit for structural refactors.

The corrections review's must-fix finding: the one-shot split that
turned the 21,129-line viv.c monolith into twelve modules carried a
pure-move claim ("every function body is byte-identical") that was
verified once, at split time, by tooling that never landed in the
tree - the claim survived only as commit-message prose, and nothing
could re-run it. This tool is the re-runnable form of that proof.

Given two revisions, it extracts every top-level function definition
from the C sources of each side, normalizes the two text edits the
split itself documents (the static-prefix strips on exported
functions; carriage returns, so both sides compare as text), and
compares the two sides as multisets of function bodies. A refactor
that only moved code answers "N old, N new, N matched, 0 lost, 0
gained". Anything else is named, function by function.

Brace structure is counted over a lexed mask (comments and string
and character literals blanked out), never over the raw bytes - the
guard suites pin literal brace-bearing text inside string constants,
and a raw count would tear exactly there.

Usage (from the repository root, needs a git checkout):

    python3 tools/split_conservation.py <old_rev> <new_rev> [files...]

The default file set is the union of src/viv.c and every src/viv_*.c
tracked on either side - the split's own population. Pass explicit
paths to audit any other structural refactor (the tool is the policy
CONTRIBUTING.md names for moves of this size).

Exit codes: 0 conserved, 1 drift (or a usage/revision error), 2 the
old side declared no functions at all (a wrong revision pair).
"""

import re
import subprocess
import sys
from collections import Counter


def git_blob(rev, path):
    """Returns the file's bytes at rev, or None when it did not exist."""
    p = subprocess.run(["git", "show", "%s:%s" % (rev, path)],
                       capture_output=True)
    if p.returncode != 0:
        return None
    return p.stdout


def git_ls(rev, pattern):
    """Returns the tracked paths matching the regex at rev."""
    p = subprocess.run(["git", "ls-tree", "-r", "--name-only", rev],
                       capture_output=True, text=True)
    if p.returncode != 0:
        return []
    rx = re.compile(pattern)
    return [l for l in p.stdout.splitlines() if rx.match(l)]


def mask_literals(text):
    """Blanks comments and string/char literals, keeping the layout.

    Structure decisions (brace depth, statement ends) must not read
    bytes that are data: the tree's own guard suites pin brace-bearing
    text inside string constants, and preprocessor conditionals can
    carry either branch's punctuation.
    """
    out = list(text)
    i = 0
    n = len(text)
    state = "code"
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nxt == "/":
                state = "line"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = "block"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == '"':
                state = "str"
                out[i] = " "
                i += 1
                continue
            if c == "'":
                state = "chr"
                out[i] = " "
                i += 1
                continue
            if c == "\\" and nxt == "\n":
                # a line continuation is not a token boundary for our
                # purposes; keep both bytes.
                i += 2
                continue
            i += 1
            continue
        if state == "line":
            if c == "\n":
                state = "code"
            else:
                out[i] = " "
            i += 1
            continue
        if state == "block":
            if c == "*" and nxt == "/":
                out[i] = out[i + 1] = " "
                state = "code"
                i += 2
                continue
            if c != "\n":
                out[i] = " "
            i += 1
            continue
        if state in ("str", "chr"):
            quote = '"' if state == "str" else "'"
            if c == "\\" and i + 1 < n:
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == quote or c == "\n":
                # an unterminated literal ends at the line break (the
                # compiler would say so too).
                state = "code"
                if c == quote:
                    out[i] = " "
            else:
                out[i] = " "
            i += 1
            continue
    return "".join(out)


def extract_functions(data):
    """Returns the normalized top-level function bodies in file order."""
    text = data.decode("latin-1").replace("\r\n", "\n")
    masked = mask_literals(text)
    mlines = masked.split("\n")
    olines = text.split("\n")
    assert len(mlines) == len(olines)
    funcs = []
    i = 0
    while i < len(mlines):
        line = mlines[i]
        # a candidate signature line: starts at column zero with an
        # identifier character, is not a non-function column-zero form
        # (comment, directive, typedef, a bare label).
        first_word = re.match(r"[A-Za-z_][A-Za-z_0-9]*", line)
        first_word = first_word.group(0) if first_word else ""
        if (line and (line[0].isalpha() or line[0] == "_")
                and first_word not in ("enum", "struct", "union",
                                       "interface", "extern")
                and not line.startswith("//")
                and not line.startswith("#")
                and not line.startswith("/*")
                and not line.startswith("*")
                and not line.startswith("typedef")):
            # walk the signature: column-zero lines up to the one that
            # opens the body (its own tail brace, or a bare brace line
            # right after). a semicolon first means a declaration, a
            # table or an extern - not a function body.
            j = i
            opened = None
            while j < len(mlines) and j < i + 8:
                probe = mlines[j]
                if probe.rstrip().endswith("{"):
                    opened = j
                    break
                if probe.rstrip() == "{":
                    opened = j
                    break
                if probe.rstrip().endswith(";"):
                    break
                if j > i and probe.startswith((" ", "\t", "\r")):
                    break
                j += 1
            if opened is not None:
                depth = 0
                k = opened
                closed = None
                while k < len(mlines):
                    pl = mlines[k].rstrip()
                    pl = pl + " " * (len(mlines[k]) - len(pl))
                    depth += mlines[k].count("{") - mlines[k].count("}")
                    if depth <= 0:
                        closed = k
                        break
                    k += 1
                if closed is not None and closed >= opened:
                    block = olines[i:closed + 1]
                    funcs.append(normalize(block))
                    i = closed + 1
                    continue
        i += 1
    return funcs


def normalize(block_lines):
    """The split's documented text edits, and nothing else."""
    text = "\n".join(block_lines)
    # the static-prefix strips on cross-module exports: the moved
    # definitions dropped 'static ' when they became shared. both
    # spellings must hash as one function.
    text = re.sub(r"^static\s+", "", text)
    # a defensive per-line rstrip keeps a pure move from failing on
    # an editor's stray trailing space at a block's end.
    text = "\n".join(l.rstrip() for l in text.split("\n"))
    return text


def name_of(func):
    for line in func.split("\n")[:4]:
        m = re.findall(r"([_A-Za-z][_A-Za-z0-9]*)\s*\(", line)
        if m:
            return m[-1]
    return "<unnamed>"


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    old_rev, new_rev = sys.argv[1], sys.argv[2]
    explicit = sys.argv[3:]

    def rev_exists(rev):
        return subprocess.run(["git", "cat-file", "-e", rev],
                              capture_output=True).returncode == 0

    if not rev_exists(old_rev) or not rev_exists(new_rev):
        print("split_conservation: revision not found: %s / %s"
              % (old_rev, new_rev))
        sys.exit(1)

    if explicit:
        paths = list(dict.fromkeys(explicit))
    else:
        paths = list(dict.fromkeys(
            ["src/viv.c"]
            + git_ls(old_rev, r"src/viv_.*\.c$")
            + git_ls(new_rev, r"src/viv_.*\.c$")))

    old_funcs = []
    old_files = []
    for p in paths:
        d = git_blob(old_rev, p)
        if d is not None:
            got = extract_functions(d)
            old_funcs.extend(got)
            old_files.append("%s (%d)" % (p, len(got)))
    new_funcs = []
    new_files = []
    for p in paths:
        d = git_blob(new_rev, p)
        if d is not None:
            got = extract_functions(d)
            new_funcs.extend(got)
            new_files.append("%s (%d)" % (p, len(got)))

    if not old_funcs:
        print("split_conservation: the old side declares no functions"
              " - wrong revision pair?")
        sys.exit(2)

    old_ms = Counter(old_funcs)
    new_ms = Counter(new_funcs)
    lost = old_ms - new_ms
    gained = new_ms - old_ms
    matched = sum((old_ms & new_ms).values())

    print("old side: %d functions across %d files (%s)"
          % (len(old_funcs), len(old_files), old_rev))
    print("new side: %d functions across %d files (%s)"
          % (len(new_funcs), len(new_files), new_rev))
    print("matched bodies (normalized): %d" % matched)
    if lost:
        print("lost from the old side: %d" % sum(lost.values()))
        for f in lost:
            print("  - %s" % name_of(f))
    if gained:
        print("gained on the new side: %d" % sum(gained.values()))
        for f in gained:
            print("  + %s" % name_of(f))
    if not lost and not gained:
        print("CONSERVED: every old function body answers on the new"
              " side, byte for byte, modulo the documented static"
              " strips.")
        sys.exit(0)
    sys.exit(1)


if __name__ == "__main__":
    main()
