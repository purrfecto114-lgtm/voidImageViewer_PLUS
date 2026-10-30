#!/usr/bin/env python3
"""Compile and execute the real qoi decoder with hostile input streams.

This is the behavioral gate the qoi decoder never had: the production
qoi_load body (plus _qoi_read_u32_be, the end marker and the real
safe_size_mul) is read from the working tree on every run and linked
against the win32 stubs in tests/native/qoi.c - an HGLOBAL-backed
stream, the interlocked stage marker, the debug allocator macros and
the two-gate pixel budget family. The cases cover every chunk tag
(rgb, rgba, index, diff, luma, run), the row advance, the reference's
trailing-chunk leniency, the run-overrun bound, and a battery of
hostile streams (bad magic, bad channels, bad colorspace, zero dims,
short stream, corrupted end marker, truncated payloads, exhausted
input, both budget gates and the cooperative cancel).

Run with --sanitize for undefined-behavior checking. Requires a host
C compiler (CC, default cc). Run from any directory.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)


def source(name):
    return (ROOT / "src" / name).read_text(encoding="utf-8")


def block(text, start):
    depth = 0
    for token in TOKENS.finditer(text, start):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[start:token.end()]
    raise ValueError("unterminated C block")


def function(filename, name):
    text = source(filename)
    match = re.search(r"(?m)^[\w *]+\b" + re.escape(name) + r"\([^;{}]*\)\s*\n\{", text)
    if not match:
        raise ValueError(f"definition missing: {filename}:{name}")
    start = text.index("{", match.start())
    return text[match.start():start] + block(text, start)


def production():
    qoi = source("qoi.c")
    magic = re.search(r"(?m)^#define\s+_QOI_MAGIC\s+\S+", qoi)
    if not magic:
        raise ValueError("qoi magic definition missing")
    marker = re.search(r"(?m)^static const BYTE _qoi_end_marker\[8\] = \{[^}]*\};", qoi)
    if not marker:
        raise ValueError("qoi end-marker definition missing")
    # the prefix rides above the template's includes, so it must stay
    # macro-only; the typed definitions go into the production block.
    prefix = magic.group() + "\n"
    bodies = "\n".join([
        marker.group(),
        function("qoi.c", "_qoi_read_u32_be"),
        function("safe_size.c", "safe_size_mul"),
        function("qoi.c", "qoi_load"),
    ])
    return prefix, bodies


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true",
                        help="enable undefined-behavior sanitizer")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="viv-qoi-") as directory:
        prefix, bodies = production()
        template = (ROOT / "tests" / "native" / "qoi.c").read_text()
        code = prefix + template.replace("/* PRODUCTION */", bodies)
        path = Path(directory) / "qoi.c"
        exe = path.with_suffix(".exe" if os.name == "nt" else "")
        path.write_text(code)
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", "-Wno-unused-function",
            "-Wno-unused-but-set-variable", "-Wno-unused-variable",
        ]
        if args.sanitize:
            command += ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
        build = subprocess.run(command + [str(path), "-o", str(exe)])
        if build.returncode != 0:
            print("qoi: FAIL (build)")
            return True
        run = subprocess.run([str(exe)])
        print(f"qoi: {'PASS' if run.returncode == 0 else 'FAIL'}", flush=True)
        return run.returncode != 0


if __name__ == "__main__":
    raise SystemExit(main())
