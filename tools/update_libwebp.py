#!/usr/bin/env python3
"""libwebp vendored-tree refresh and verification tool.

libwebp/ is a pruned decode-only vendored tree and libwebp/VERSION.imported
is its provenance record: upstream url, version, tarball sha256 and the
file-set rules (per-directory compiled counts, pruned upstream
directories, the encoder-side name patterns). this tool turns that
record into an executable contract - the audit's P1 ask ("automate
libwebp refresh: single command download, verify, prune, closure check;
record version and hash; ci detects a dirty tree") maps onto two modes:

  --check                      (offline, no network)
      verify the current tree against the record:
        (i)   every recorded file-set member is on disk - the kept src
              directories, the per-directory .c counts, the total
              ClCompile count and the files the record names (the
              record lists the compiled set by count, so count
              equality is the faithful presence test);
        (ii)  nothing unexpected lives under libwebp/ - the root
              allowlist plus the src subdirectory and per-directory
              classification rules, with zero encoder files anywhere;
        (iii) the quoted-include closure of every vendored .c/.h
              resolves to a file inside libwebp/ (angle-bracket system
              includes are out of scope; the one deliberate exception
              is the generated config.h include, see below);
        (iv)  the recorded version and tarball sha256 are reprinted -
              the hash pins the tarball at fetch time; offline there
              is no tarball to hash, so nothing is recomputed.
      exit 0 clean, exit 1 with a precise drift report. this is the
      "ci detects a dirty tree" hook.

  --fetch --version X.Y.Z [--sha256 HEX]    (the refresh)
      refuses to run over a tree that fails --check (no silent
      layering over drift - afterwards nobody could tell upstream
      churn from local damage), downloads the upstream tag archive
      from the url pattern the record documents (https only), verifies
      the sha256 - required on the command line unless the requested
      version is the recorded one, in which case the recorded hash is
      the authority; upstream publishes no checksum of its own for
      the auto-generated tag archives - extracts to a temp dir behind
      a path guard, copies the decode-only allowlist over libwebp/,
      regenerates libwebp/VERSION.imported in the same wrapped-prose
      format, re-runs the full --check and prints a git
      status/diffstat summary when git is available.

record quirks the tool adapts to (never the reverse):
  the record is hand-wrapped prose at ~70 columns, not a machine
  manifest, so every parser regex tolerates a line break wherever a
  space can appear. its "kept:" line describes the import-time root
  (tests/ doc/ cmake/ ChangeLog NEWS and the build systems); the R70
  housekeeping round later pruned all of those, keeping configure.ac
  and VERSION.imported as the version and provenance pins - the root
  rule below encodes that surviving policy and verifies the pruned
  names stay absent instead of reporting them missing.

usage:  python3 tools/update_libwebp.py --check
        python3 tools/update_libwebp.py --fetch --version X.Y.Z \
            --sha256 <64 hex chars>
"""
import argparse
import datetime
import fnmatch
import hashlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEBP = ROOT / "libwebp"
RECORD = WEBP / "VERSION.imported"
WEBP_RESOLVED = WEBP.resolve()

# the root policy the record's kept line has evolved into: the license
# texts and readme it names, plus configure.ac (the one top-level build
# file the R70 housekeeping kept, as the version pin this tool and the
# guard suite read) and the record itself. everything else the record
# listed at import time is in ROOT_SINCE_PRUNED and must stay gone.
ROOT_KEEPERS = ("AUTHORS", "COPYING", "PATENTS", "README.md",
                "VERSION.imported", "configure.ac")
ROOT_SINCE_PRUNED = ("Android.mk", "CMakeLists.txt", "ChangeLog", "NEWS",
                     "Makefile.am", "Makefile.vc", "cmake", "doc", "tests")

PATH_TOKEN = r"src/[\w./-]+\.[ch]"


def section(text, start, *ends):
    """slice one marker-delimited prose section. the wrapping makes
    line positions unreliable but the section markers are stable, in
    the current record and in what this tool regenerates alike."""
    i = text.find(start)
    if i < 0:
        return ""
    for e in ends:
        j = text.find(e, i + len(start))
        if j >= 0:
            return text[i:j]
    return text[i:]


def parse_record(text):
    """parse the wrapped-prose provenance record into its fields.

    each field is matched independently, so the ~70-column hand
    wrapping never matters: a regex only breaks when a single token is
    split across lines, and the record never does that.
    """
    rec = {}

    def field(pattern):
        m = re.search(pattern, text, re.M)
        return m.group(1) if m else None

    rec["upstream"] = field(r"^upstream:\s+(\S+)")
    rec["version"] = field(r"^version:\s+(\d+(?:\.\d+)+)")
    rec["tarball"] = field(r"^source tarball:\s+(.+?)\s*$")
    m = re.search(r"^tarball sha256:\s+([0-9a-fA-F]{64})", text, re.M)
    rec["sha256"] = m.group(1).lower() if m else None
    rec["imported"] = field(r"^imported:\s+(\d{4}-\d{2}-\d{2})")

    compiled = section(text, "what is compiled", "pruned at import")
    rec["counts"] = {}
    rec["mixed"] = set()
    for d, tag, n in re.findall(
            r"src/(dec|demux|dsp|utils)(\s+decode\s+set)?\s*\((\d+)\)",
            compiled):
        rec["counts"][d] = int(n)
        if tag:
            # "decode set" marks the dirs that mix decode and encoder
            # files upstream and therefore need name classification
            rec["mixed"].add(d)
    m = re.search(r"(\d+)\s+ClCompile entries", compiled)
    rec["total"] = int(m.group(1)) if m else None

    # the upgrade-audit clause names files that must exist (new) or
    # must not (gone); singular and comma-list forms are both emitted
    # by this tool's own refresh, so the round trip is exact
    rec["new_files"] = re.findall(
        r"the only\s+new\s+file\s+is\s+(%s)" % PATH_TOKEN, compiled)
    m = re.search(r"the new files\s+are\s+((?:%s[,\s]+)+)" % PATH_TOKEN,
                  compiled)
    if m:
        rec["new_files"] += re.findall(PATH_TOKEN, m.group(1))
    rec["gone_files"] = re.findall(
        r"the only\s+file\s+gone\s+is\s+(%s)" % PATH_TOKEN, compiled)
    m = re.search(r"the gone files\s+are\s+((?:%s[,\s]+)+)" % PATH_TOKEN,
                  compiled)
    if m:
        rec["gone_files"] += re.findall(PATH_TOKEN, m.group(1))

    # kept src directories, from the brace form in the kept line
    m = re.search(r"src/\{([a-z,]+)\}", text)
    rec["src_dirs"] = m.group(1).split(",") if m else []

    # pruned upstream dirs: every whitespace-delimited token in the
    # pruned section that is a plain path ending in a slash (the
    # "+ gradle top-level files" aside never qualifies, and neither do
    # the mid-sentence "src/dsp" / "src/utils" mentions - a slash must
    # terminate the token, backtracking regexes need not apply)
    pruned = section(text, "pruned at import", "post-import sweep",
                     "standing tree policy", "kept:")
    dirs = set()
    for tok in pruned.split():
        tok = tok.strip(".,;()")
        if tok.endswith("/") and re.fullmatch(r"[\w./]+", tok):
            dirs.add(tok.rstrip("/"))
    rec["pruned_dirs"] = sorted(dirs)

    # encoder-side names: wildcard patterns and plain names, collected
    # from every narrative section that carries them (the correction
    # narrative lists the .c patterns, the sweep/standing sections the
    # plain names, a regenerated record repeats the patterns in its
    # pruned section), so the current record and a regenerated one
    # parse the same
    narrative = (pruned +
                 section(text, "IMPORTANT correction", "pruned at import") +
                 section(text, "post-import sweep", "kept:") +
                 section(text, "standing tree policy", "kept:"))
    toks = re.findall(r"[\w*]+\.[ch]", narrative)
    rec["enc_patterns"] = sorted({t for t in toks if "*" in t})
    rec["enc_names"] = sorted({t for t in toks if "*" not in t})
    return rec


def is_encoder_name(name, rec):
    """the record's own classification of encoder-side files: the
    wildcard patterns plus the plain names. applied to every file
    under src/, this is the "zero encoder files anywhere" guarantee
    (src/webp/encode.h is the public api header the record keeps; it
    matches none of the patterns, which are .c-shaped)."""
    return (name in rec["enc_names"] or
            any(fnmatch.fnmatch(name, p) for p in rec["enc_patterns"]))


def quoted_includes(path):
    """yield (include string, config gated) for every quoted include.

    upstream guards the generated src/webp/config.h include behind
    #ifdef HAVE_CONFIG_H and the vcxproj build never defines that
    macro, so the file is legitimately absent from the vendored tree.
    the tracker below walks the preprocessor conditionals just far
    enough to recognize that one pattern: a stack of
    (config region, active side) entries where #else/#elif flip the
    active side and #endif pops. it is a heuristic, not a
    preprocessor - but the only thing it ever excuses is an include
    that names config.h inside a HAVE_CONFIG_H region.
    """
    stack = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        s = line.strip()
        m = re.match(r"#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)$", s)
        if m:
            kw, rest = m.group(1), m.group(2).strip()
            if kw == "endif":
                if stack:
                    stack.pop()
            elif kw in ("else", "elif"):
                if stack:
                    stack[-1] = (stack[-1][0], not stack[-1][1])
            elif kw == "ifdef":
                stack.append((rest == "HAVE_CONFIG_H", True))
            elif kw == "ifndef":
                stack.append((False, True))
            else:
                stack.append(("HAVE_CONFIG_H" in rest, True))
            continue
        im = re.match(r'#\s*include\s+"([^"]+)"', s)
        if im:
            gated = any(cfg and act for cfg, act in stack)
            yield im.group(1), gated


def resolve_include(inc, from_file):
    """c quoted-include semantics as this build sees them: the
    directory of the including file first, then the vcxproj's single
    additional include path (the libwebp root, ..\\libwebp in both
    project files)."""
    for base in (from_file.parent, WEBP):
        try:
            cand = (base / inc).resolve()
        except OSError:
            continue
        if cand.is_file():
            return cand
    return None


def is_inside(path):
    try:
        path.relative_to(WEBP_RESOLVED)
        return True
    except ValueError:
        return False


def check_tree(rec):
    """run the four check families against the on-disk tree, print a
    full report and return the problem list (empty = clean)."""
    problems = []

    def bad(msg):
        problems.append(msg)

    if not WEBP.is_dir():
        print("libwebp/ is missing entirely")
        return ["libwebp/ is missing entirely"]

    # (iv) the provenance the record carries - reprinted, not
    # recomputed: offline there is no tarball to hash
    print("recorded version:        %s" % rec["version"])
    print("recorded tarball:        %s" % rec["tarball"])
    print("recorded tarball sha256: %s" % rec["sha256"])
    print("recorded import date:    %s" % rec["imported"])
    print("  (the sha256 pins the tarball at fetch time; offline it is reprinted only)")

    # (i) presence: root keepers, kept src dirs, counts, named files
    missing_root = [f for f in ROOT_KEEPERS if not (WEBP / f).is_file()]
    for f in missing_root:
        bad("root keeper %s is missing (the license/provenance pins)" % f)
    back_root = [f for f in ROOT_SINCE_PRUNED if (WEBP / f).exists()]
    for f in back_root:
        bad("%s is present at the libwebp root; the R70 housekeeping pruned it"
            " and the record's import-time kept line no longer applies" % f)
    for d in rec["src_dirs"]:
        if not (WEBP / "src" / d).is_dir():
            bad("recorded src directory src/%s is missing" % d)
    for d in sorted(rec["counts"]):
        have = len([p for p in (WEBP / "src" / d).glob("*.c") if p.is_file()])
        if have != rec["counts"][d]:
            bad("src/%s carries %d .c files, the record pins %d"
                % (d, have, rec["counts"][d]))
    total = len([p for p in (WEBP / "src").rglob("*.c") if p.is_file()])
    if rec["total"] is not None and total != rec["total"]:
        bad("src/ carries %d .c files in total, the record pins %d"
            " ClCompile entries" % (total, rec["total"]))
    for f in rec["new_files"]:
        if not (WEBP / f).is_file():
            bad("the record names %s as newly present; it is missing" % f)
    for f in rec["gone_files"]:
        if (WEBP / f).exists():
            bad("the record names %s as gone from the previous import;"
                " it is back" % f)
    for d in rec["pruned_dirs"]:
        if (WEBP / d).exists():
            bad("the record prunes %s; it is present" % d)
    enc_hits = [str(p.relative_to(WEBP)).replace("\\", "/")
                for p in (WEBP / "src").rglob("*")
                if p.is_file() and is_encoder_name(p.name, rec)]
    for p in enc_hits:
        bad("encoder-side file %s under src/ (the record guarantees"
            " zero encoder files anywhere)" % p)

    # (ii) unexpected entries: the root allowlist, the src subdir
    # allowlist and the per-directory classification
    allowed_root = set(ROOT_KEEPERS) | {"src"}
    stray_root = [p.name for p in WEBP.iterdir()
                  if p.name not in allowed_root]
    for n in stray_root:
        bad("unexpected %s at the libwebp root (allowed: %s)"
            % (n, " ".join(sorted(allowed_root))))
    stray_src = []
    if (WEBP / "src").is_dir():
        stray_src = [p.name for p in (WEBP / "src").iterdir()
                     if p.name not in set(rec["src_dirs"])]
    for n in stray_src:
        bad("unexpected %s directly under libwebp/src (allowed: %s)"
            % (n, " ".join(sorted(rec["src_dirs"]))))
    for d in rec["src_dirs"]:
        base = WEBP / "src" / d
        if not base.is_dir():
            continue
        for p in sorted(base.iterdir()):
            if p.is_dir():
                bad("unexpected subdirectory %s under src/%s" % (p.name, d))
            elif p.is_file() and d in rec["mixed"] and p.suffix not in (".c", ".h"):
                bad("unexpected non-source file %s under src/%s"
                    " (the decode/encoder mix dirs keep .c/.h only)"
                    % (p.name, d))

    # (iii) include closure of every vendored .c/.h
    scanned = inc_total = inc_ok = inc_gated = inc_bad = 0
    for f in sorted(p for p in WEBP.rglob("*")
                    if p.is_file() and p.suffix in (".c", ".h")):
        scanned += 1
        rel = str(f.relative_to(ROOT)).replace("\\", "/")
        for inc, gated in quoted_includes(f):
            inc_total += 1
            if gated and inc.endswith("config.h"):
                inc_gated += 1
                continue
            rp = resolve_include(inc, f)
            if rp is None:
                inc_bad += 1
                bad('%s includes "%s" which resolves to no file inside'
                    " the vendored tree" % (rel, inc))
            elif not is_inside(rp):
                inc_bad += 1
                bad('%s includes "%s" which resolves to %s, outside the'
                    " vendored tree" % (rel, inc, rp))
            else:
                inc_ok += 1

    # the report: every family prints its own summary line so a clean
    # run doubles as a tree report
    print("root: %d/%d keepers present, %d/%d R70-pruned items absent"
          % (len(ROOT_KEEPERS) - len(missing_root), len(ROOT_KEEPERS),
             len(ROOT_SINCE_PRUNED) - len(back_root), len(ROOT_SINCE_PRUNED)))
    for d in sorted(rec["src_dirs"]):
        base = WEBP / "src" / d
        c = len([p for p in base.glob("*.c")]) if base.is_dir() else 0
        h = len([p for p in base.glob("*.h")]) if base.is_dir() else 0
        note = (" (record %d)" % rec["counts"][d] if d in rec["counts"]
                else " (headers-only dir)")
        print("src/%-6s %2d .c%s, %2d .h" % (d + ":", c, note, h))
    print("compiled set: %d .c on disk, the record pins %s"
          " ClCompile entries" % (total, rec["total"]))
    print("pruned upstream dirs: %d recorded, %d present (drift above"
          " if any)" % (len(rec["pruned_dirs"]),
                        sum(1 for d in rec["pruned_dirs"] if (WEBP / d).exists())))
    print("encoder files anywhere under src/: %d" % len(enc_hits))
    print("unexpected entries: %d at the root or under src/"
          % (len(stray_root) + len(stray_src)))
    print("include closure: %d files scanned, %d quoted includes,"
          " %d resolved in-tree, %d config.h gated (#ifdef HAVE_CONFIG_H,"
          " absent by design), %d unresolved or outside"
          % (scanned, inc_total, inc_ok, inc_gated, inc_bad))
    if problems:
        print("DRIFT (%d):" % len(problems))
        for p in problems:
            print("  - " + p)
    else:
        print("no drift: the tree matches the record")
    return problems


def cmd_check():
    if not RECORD.is_file():
        print("libwebp/VERSION.imported is missing; the vendored tree"
              " has no record to check against")
        return 1
    rec = parse_record(RECORD.read_text(encoding="utf-8", errors="replace"))
    absent = [k for k in ("upstream", "version", "tarball", "sha256",
                          "imported") if rec[k] is None]
    if rec["total"] is None or not rec["counts"] or not rec["src_dirs"]:
        absent.append("what is compiled")
    if absent:
        print("the record does not parse: missing or unparsable fields:"
              " %s" % ", ".join(absent))
        return 1
    print("libwebp vendored tree vs libwebp/VERSION.imported (offline check)")
    print("")
    problems = check_tree(rec)
    if problems:
        print("RESULT: dirty tree (exit 1)")
        return 1
    print("RESULT: clean (exit 0)")
    return 0


def safe_extract(tar, dest):
    """extract behind a path guard. python's filter="data" already
    rejects absolute paths, .. traversal, links escaping the tree and
    device nodes; the manual branch reproduces the same guarantees
    for the older interpreters this tool may meet on windows."""
    dest = dest.resolve()
    try:
        tar.extractall(dest, filter="data")
        return
    except TypeError:
        pass
    for m in tar.getmembers():
        name = m.name.replace("\\", "/")
        parts = name.split("/")
        if (name.startswith("/") or ".." in parts or
                (len(parts[0]) > 1 and parts[0][1] == ":")):
            raise RuntimeError("archive member escapes the extraction"
                               " dir: %s" % m.name)
        if m.issym() or m.islnk() or not (m.isreg() or m.isdir()):
            raise RuntimeError("archive carries a link or special"
                               " member: %s" % m.name)
    tar.extractall(dest)


def pick_archive_root(xdir, version):
    """github tag archives unpack to a single root directory named
    {repo}-{tag without the v}; both spellings are accepted and the
    single-entry fallback keeps a future naming change working."""
    for cand in (xdir / ("libwebp-" + version), xdir / ("libwebp-v" + version)):
        if cand.is_dir():
            return cand
    entries = list(xdir.iterdir())
    if len(entries) == 1 and entries[0].is_dir():
        return entries[0]
    return None


def allowlist_from(src_root, rec):
    """the copy set, straight from the record's rules: dec/demux/webp
    are pure upstream directories and are kept whole; dsp/utils are
    decode/encoder mixes, so they keep .c/.h only and drop every
    encoder-side name; the root keeps its pins (VERSION.imported is
    regenerated, not copied)."""
    keep = []
    for name in ROOT_KEEPERS:
        if name != "VERSION.imported" and (src_root / name).is_file():
            keep.append(name)
    for d in rec["src_dirs"]:
        base = src_root / "src" / d
        if not base.is_dir():
            continue
        for p in sorted(base.iterdir()):
            if not p.is_file() or is_encoder_name(p.name, rec):
                continue
            if d in rec["mixed"] and p.suffix not in (".c", ".h"):
                continue
            keep.append("src/%s/%s" % (d, p.name))
    return keep


def audit_sentence(added, gone):
    """the upgrade-audit clause, in the exact forms the parser reads
    back: singular for one file, comma list for more, clause omitted
    when empty. the round trip through parse_record must reproduce
    added/gone exactly."""
    parts = []
    if len(added) == 1:
        parts.append("the only new file is %s." % added[0])
    elif added:
        parts.append("the new files are %s." % ", ".join(added))
    if len(gone) == 1:
        parts.append("the only file gone is %s." % gone[0])
    elif gone:
        parts.append("the gone files are %s." % ", ".join(gone))
    if not parts:
        return "every file name is unchanged."
    return " ".join(parts) + " all other names unchanged."


def regenerate_record(old, version, sha256, counts, added, gone):
    """rebuild the provenance record in the same wrapped-prose format
    the parser reads: same field spellings, same section markers,
    same count and pattern syntax - only the values move."""
    date = datetime.date.today().isoformat()
    total = sum(counts.values())
    enc_c = [p for p in old["enc_patterns"]] + \
            [n for n in old["enc_names"] if n.endswith(".c")]
    enc_all = old["enc_patterns"] + old["enc_names"]
    lines = [
        "libwebp vendored import - provenance record",
        "============================================",
        "",
        "upstream:        %s" % old["upstream"],
        "version:         %s (stable, refreshed %s)" % (version, date),
        "source tarball:  libwebp-%s.tar.gz (GitHub tag v%s archive)"
        % (version, version),
        "tarball sha256:  %s" % sha256,
        "imported:        %s (voidImageViewer_PLUS,"
        " tools/update_libwebp.py refresh)" % date,
        "",
        "what is compiled (vs2019/vs2026 vcxproj, %d ClCompile entries):"
        % total,
        "  the upstream \"decode only\" file set (libwebpdecoder"
        " semantics) as",
        "  classified by the standing import rules this record carries:",
        "    src/dec (%d) + src/demux (%d) + src/dsp decode set (%d) +"
        % (counts["dec"], counts["demux"], counts["dsp"]),
        "    src/utils decode set (%d)" % counts["utils"],
        "  upgrade audit against the previous %s import: %s"
        % (old["version"], audit_sentence(added, gone)),
        "",
        "pruned at import (not present in this tree at all):",
        "  webp_js/  examples/  imageio/  swig/  man/  gradle/"
        " (+ gradle",
        "  top-level files)  infra/  extras/  sharpyuv/  src/enc/"
        "  src/mux/",
        "  and the encoder-side .c files inside src/dsp and src/utils",
        "  (%s). the decode set is verified self-contained by" % ", ".join(enc_c),
        "  the include-closure scan tools/update_libwebp.py runs after",
        "  every refresh.",
        "",
        "standing tree policy (enforced by tools/update_libwebp.py --check):",
        "  zero encoder files anywhere: no src/enc/, no src/mux/, no",
        "  encoder-side name under src/dsp or src/utils matching the",
        "  patterns above, and none of %s" % ", ".join(enc_all),
        "  under src/utils. the root keeps the license and provenance",
        "  pins only (%s);" % ", ".join(ROOT_KEEPERS),
        "  the import-time root items the %s record named (tests/ doc/"
        % old["version"],
        "  cmake/ ChangeLog NEWS CMakeLists.txt Makefile.am Makefile.vc",
        "  Android.mk) were pruned by the R70 housekeeping and stay pruned.",
        "",
        "kept:  src/{dec,demux,dsp,utils,webp} with all upstream headers",
        "  COPYING  PATENTS  AUTHORS  README.md  configure.ac"
        "  VERSION.imported",
        "",
        "license: BSD-3-Clause (COPYING); the WebP patent grant is in"
        " PATENTS.",
        "",
    ]
    return "\n".join(lines)


def git_summary():
    """the post-refresh change summary. git is optional (a windows box
    without it still gets the full check report); failures are noted
    and skipped, never fatal."""
    if shutil.which("git") is None:
        print("git not found on PATH; skipping the status summary")
        return
    for args, title in (
            (["status", "--short", "--", "libwebp"], "git status --short libwebp/"),
            (["diff", "--stat", "--", "libwebp"], "git diff --stat -- libwebp/")):
        try:
            r = subprocess.run(["git", "-C", str(ROOT)] + args,
                               capture_output=True, text=True,
                               errors="replace", timeout=60)
        except (OSError, subprocess.SubprocessError) as e:
            print("git %s failed: %s" % (args[0], e))
            continue
        if r.returncode != 0:
            print("git %s unavailable (not a repository?); skipped"
                  % args[0])
            continue
        print("--- %s ---" % title)
        print(r.stdout.rstrip("\n") or "(empty)")


def cmd_fetch(version, sha256_arg):
    if not RECORD.is_file():
        print("libwebp/VERSION.imported is missing; nothing describes"
              " the tree this refresh would replace")
        return 1
    old = parse_record(RECORD.read_text(encoding="utf-8", errors="replace"))

    # gate 1: never layer a refresh over drift - afterwards nobody
    # could tell upstream churn from local damage
    print("pre-flight: running the full --check against the current tree")
    print("")
    problems = check_tree(old)
    if problems:
        print("REFUSING to fetch: the working tree fails --check"
              " (%d problems). fix the drift first; a refresh must"
              " land on a clean tree" % len(problems))
        return 1
    print("pre-flight clean; proceeding")
    print("")

    # gate 2: the hash. upstream publishes no checksum for the
    # auto-generated tag archives, so a new version needs a pinned
    # --sha256 from a source the operator trusts; refetching the
    # recorded version can reuse the recorded hash as the authority
    if sha256_arg is not None:
        sha256 = sha256_arg.lower()
    elif version == old["version"] and old["sha256"]:
        sha256 = old["sha256"]
        print("no --sha256 given; the requested version is the recorded"
              " one, so the recorded hash is the authority")
    else:
        print("REFUSING to fetch: --sha256 is required for version %s"
              % version)
        print("  upstream publishes no checksum for the auto-generated"
              " tag archives; pin the hash")
        print("  from a trusted source (a second download you compared,"
              " the release announcement, ...)")
        return 1

    # gate 3: https only, url derived from the recorded upstream
    if not old["upstream"].startswith("https://"):
        print("REFUSING to fetch: the recorded upstream %s is not an"
              " https url" % old["upstream"])
        return 1
    url = old["upstream"].rstrip("/") + "/archive/refs/tags/v" + version + ".tar.gz"
    print("downloading %s" % url)

    with tempfile.TemporaryDirectory(prefix="update-libwebp-") as td:
        tdp = Path(td)
        tarball = tdp / ("libwebp-%s.tar.gz" % version)
        h = hashlib.sha256()
        try:
            req = urllib.request.Request(
                url, headers={"User-Agent": "voidImageViewer-update-libwebp"})
            with urllib.request.urlopen(req, timeout=120) as resp, \
                    open(tarball, "wb") as out:
                while True:
                    chunk = resp.read(65536)
                    if not chunk:
                        break
                    h.update(chunk)
                    out.write(chunk)
        except Exception as e:
            print("download failed: %s" % e)
            return 1
        got = h.hexdigest()
        if got != sha256:
            print("REFUSING to continue: tarball sha256 is %s, expected %s"
                  % (got, sha256))
            return 1
        print("sha256 verified: %s" % got)

        xdir = tdp / "extracted"
        xdir.mkdir()
        try:
            with tarfile.open(tarball, "r:gz") as tar:
                safe_extract(tar, xdir)
        except Exception as e:
            print("REFUSING to continue: extraction failed: %s" % e)
            return 1
        src_root = pick_archive_root(xdir, version)
        if src_root is None:
            print("REFUSING to continue: the archive does not carry the"
                  " expected libwebp-%s root directory" % version)
            return 1

        # the allowlist from the record's own rules, plus a layout
        # gate: if upstream reorganizes beyond the record format the
        # refresh stops for manual review instead of guessing
        keep = allowlist_from(src_root, old)
        found = [d for d in old["src_dirs"] if (src_root / "src" / d).is_dir()]
        if sorted(found) != sorted(old["src_dirs"]):
            print("REFUSING to continue: the upstream layout moved"
                  " (src dirs found: %s); this tool's record format no"
                  " longer describes it - manual review needed"
                  % ",".join(found))
            return 1
        for f in ROOT_KEEPERS:
            if f != "VERSION.imported" and f not in keep:
                print("REFUSING to continue: the archive lacks %s, a"
                      " recorded root keeper" % f)
                return 1

        # snapshot the old compiled set for the upgrade audit, then
        # replace the tree wholesale
        old_src = sorted(
            str(p.relative_to(WEBP)).replace("\\", "/")
            for p in (WEBP / "src").rglob("*")
            if p.is_file() and p.suffix in (".c", ".h"))
        new_src = sorted(f for f in keep if f.endswith((".c", ".h")))
        added = [f for f in new_src if f not in old_src]
        gone = [f for f in old_src if f not in new_src]

        for p in WEBP.iterdir():
            if p.is_dir():
                shutil.rmtree(p)
            else:
                p.unlink()
        for rel in keep:
            dst = WEBP / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src_root / rel, dst)
        counts = {d: len([p for p in (WEBP / "src" / d).glob("*.c")
                          if p.is_file()])
                  for d in ("dec", "demux", "dsp", "utils")}
        print("copied %d files: %d .c compiled set, %d headers,"
              " %d root keepers"
              % (len(keep), sum(counts.values()),
                 sum(1 for f in keep if f.endswith(".h")),
                 sum(1 for f in keep if "/" not in f)))

        RECORD.write_text(
            regenerate_record(old, version, sha256, counts, added, gone),
            encoding="utf-8", newline="\n")
        print("regenerated libwebp/VERSION.imported for %s" % version)

    # the post-refresh verification is the same --check the ci runs
    print("")
    print("post-refresh check (same engine as --check):")
    print("")
    rec = parse_record(RECORD.read_text(encoding="utf-8", errors="replace"))
    problems = check_tree(rec)
    git_summary()
    if problems:
        print("RESULT: the refreshed tree fails its own record"
              " (%d problems)" % len(problems))
        return 1
    print("RESULT: refresh complete, tree clean (exit 0)")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="refresh and verify the vendored libwebp tree"
                    " against libwebp/VERSION.imported")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--check", action="store_true",
                   help="offline: verify the vendored tree against the"
                        " record (the ci dirty-tree hook; the default"
                        " when no mode is given)")
    g.add_argument("--fetch", action="store_true",
                   help="online: refresh the vendored tree from the"
                        " upstream tag archive")
    ap.add_argument("--version", metavar="X.Y.Z",
                    help="the upstream version to fetch (required with"
                         " --fetch)")
    ap.add_argument("--sha256", metavar="HEX",
                    help="the expected tarball sha256 (required with"
                         " --fetch unless refetching the recorded"
                         " version)")
    args = ap.parse_args(argv)
    if args.fetch:
        if not args.version:
            ap.error("--fetch requires --version X.Y.Z")
        if args.sha256 is not None and \
                not re.fullmatch(r"[0-9a-fA-F]{64}", args.sha256):
            ap.error("--sha256 must be 64 hex characters")
        return cmd_fetch(args.version, args.sha256)
    return cmd_check()


if __name__ == "__main__":
    sys.exit(main())
