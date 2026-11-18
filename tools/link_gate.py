#!/usr/bin/env python3
"""Static link-failure gate for the C sources.

The rotation round shipped a declared-but-never-defined helper
(_viv_rotation_cancelled had a prototype in viv_render.h, five call
sites and no body anywhere) - a defect the python suites cannot see
because none of them compile the app. This gate answers, for every
prototype that lives in a header and every call site that rides it,
that some .c carries a definition head; GetProcAddress function
pointers and plain file-scope variables are out of scope on purpose.

Validation fixture: a tree with the rotation helper's definition body
removed fails with exactly viv_rotation_cancelled; the healthy tree passes.
"""
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "src"
PREFIXES = ("viv_", "_viv", "os_", "string_", "mem_", "safe_", "localization_", "zoomui_", "config_")

CONTROL_TAILS = {"return", "if", "while", "for", "switch", "case", "else", "do", "goto", "sizeof"}
CONTINUATION_TAIL = ("|", "&", ",", "(", "?", "+", "-", "*", "/", "!")
call_re = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
prototype_re = re.compile(r"(?m)^\s*(?:extern\s+)?[A-Za-z_]\w*[\s\*]+(_?[a-z]\w*)\s*\([^;{]*\)\s*;")
declspec_re = re.compile(r"__declspec\s*\([^)]*\)")
type_only_re = re.compile(r"^\s*[A-Za-z_][\w\s\*]*$")

declarations = set()
for path in sorted(ROOT.rglob("*.h")):
    for match in prototype_re.finditer(path.read_text(errors="replace")):
        declarations.add(match.group(1))

definition_re = re.compile(r"^\s*(.*?)\s*\**\b([A-Za-z_]\w*)\s*\(")
definitions = set()
for path in sorted(ROOT.rglob("*.c")):
    lines = path.read_text(errors="replace").splitlines()
    for index, line in enumerate(lines):
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or stripped.endswith(";"):
            continue
        if index and lines[index - 1].rstrip().endswith(CONTINUATION_TAIL):
            continue
        line = declspec_re.sub("", line)
        match = definition_re.match(line)
        if not match:
            continue
        head, name = match.group(1), match.group(2)
        head = head.strip()
        if head:
            if any(c in head for c in "(){}=;|&?:"):
                continue
            if head.split()[-1] in CONTROL_TAILS:
                continue
        else:
            # a name at line start only counts when the previous line is a
            # bare return type (the multi-line definition shape os.c uses)
            if not (index and type_only_re.match(lines[index - 1])
                    and not lines[index - 1].rstrip().endswith((";" + "".join(CONTINUATION_TAIL)))):
                continue
        definitions.add(name)

dynamic_loads = set()
for path in sorted(ROOT.rglob("*.c")):
    text = path.read_text(errors="replace")
    for match in re.finditer(r"(\w+)\s*=\s*\(?\s*[\w\s\*]*_t\s*\)?\s*GetProcAddress", text):
        dynamic_loads.add(match.group(1))

problems = []
checked = 0
for path in sorted(ROOT.rglob("*.c")):
    text = path.read_text(errors="replace")
    for match in call_re.finditer(text):
        name = match.group(1)
        if not name.startswith(PREFIXES) or name in dynamic_loads:
            continue
        if name not in declarations:
            continue  # undeclared names are a C-dialect question, not a link failure
        checked += 1
        if name not in definitions:
            problems.append(f"{path.name}: {name} is prototyped in a header and called but never defined")

print(f"link gate: {checked} header-prototyped call sites, {len(definitions)} definition heads")
if problems:
    for problem in sorted(set(problems)):
        print("FAIL", problem)
    sys.exit(1)
print("link gate PASS")
