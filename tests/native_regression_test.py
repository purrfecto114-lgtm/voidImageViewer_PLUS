#!/usr/bin/env python3
"""Compile and execute selected production C bodies with small OS adapters.

This is a host-side regression gate, not a Windows GUI test. Bodies are read
from the working tree on every run; no Python copy of the MRU/gesture logic
is used. A full Windows cross-build still checks headers and integration.
Requires a host C compiler (CC, default cc). Run from any directory.
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


def production(suite):
    if suite == "mru":
        config = source("config.c")
        start = config.index("// recent files mru: recent_file_0")
        start = config.index("{", start)
        cap = re.search(r"(?m)^#define\s+CONFIG_RECENT_FILE_COUNT\s+\d+", source("config.h"))
        if not cap:
            raise ValueError("MRU capacity definition missing")
        bodies = [cap.group(), function("viv_playlist.c", "_viv_icompare_filename"),
                  function("config.c", "_config_recent_key_name"),
                  "static void load_recent(ini_t *ini)\n" + block(config, start)]
        for name in ("push", "remove", "remove_filename", "rename", "clear"):
            bodies.append(function("viv_recent.c", "_viv_recent_file_" + name))
        # Capacity must precede the adapter's globals.
        return bodies[0] + "\n", "\n".join(bodies[1:])
    if suite == "gesture":
        text = source("viv_view.c")
        start = text.index("static DWORD _viv_gesture_zoom_dist")
        end = text.index("int _viv_is_touch_click", start)
        state = source("viv_state.h")
        names = ("_VIV_ZOOM_MAX", "_VIV_ZOOM_SHRINK_STEPS", "_VIV_ZOOM_STEPS_PER_NOTCH")
        constants = [re.search(r"(?m)^#define\s+" + name + r"\s+\d+", state).group() for name in names]
        enum_start = state.rindex("enum", 0, state.index("_VIV_DOING_NOTHING,"))
        constants.append(state[enum_start:state.index(";", enum_start) + 1])
        render = source("viv_render.c")
        cache_start = render.index("static int _viv_zoom_pos_max_cache =")
        cache_end = render.index("// the first ladder position", cache_start)
        init = source("viv.c")
        init_start = init.index("{", init.index("// init the zoom ladder:"))
        bodies = ["static void init_scales(void)\n" + block(init, init_start),
                  function("viv_render.c", "_viv_clamp_double"),
                  function("viv_render.c", "_viv_get_render_size"),
                  render[cache_start:cache_end],
                  function("viv_render.c", "_viv_zoom_pos_max"),
                  function("viv_render.c", "_viv_zoom_pos_floor"),
                  function("viv_render.c", "_viv_clamp_zoom_pos"),
                  function("viv_view.c", "_viv_zoom_steps"),
                  function("viv_view.c", "_viv_do_mousewheel_action"), text[start:end],
                  function("viv_view.c", "_viv_is_touch_click"),
                  function("viv_view.c", "_viv_gesture_reset"),
                  function("viv_view.c", "_viv_on_gesture")]
        return "\n".join(constants) + "\n", "\n".join(bodies)
    if suite == "memory":
        text = source("mem.c")
        start = text.index("typedef struct mem_debug_s")
        end = text.index("void mem_debug(void)", start)
        magic = re.search(r"(?m)^#define\s+MEM_MAGIC_SIZE\s+\d+", text)
        if not magic:
            raise ValueError("debug allocation magic-size definition missing")
        return magic.group() + "\n", text[start:end] + function("mem.c", "mem_debug")
    raise ValueError(suite)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suites", nargs="*", choices=["mru", "gesture", "memory"])
    parser.add_argument("--sanitize", action="store_true", help="enable undefined-behavior sanitizer")
    parser.add_argument("--mru-case", choices=["rename", "load"], help="run one MRU scenario group")
    parser.add_argument("--gesture-case", choices=["policy", "zoom", "zero", "begin", "touch", "wheel", "bounds"], help="run one input scenario group")
    args = parser.parse_args()
    failed = 0
    with tempfile.TemporaryDirectory(prefix="viv-native-") as directory:
        for suite in args.suites or ["mru", "gesture", "memory"]:
            prefix, bodies = production(suite)
            template = (ROOT / "tests" / "native" / f"{suite}.c").read_text()
            code = prefix + template.replace("/* PRODUCTION */", bodies)
            path = Path(directory) / f"{suite}.c"
            exe = path.with_suffix(".exe" if os.name == "nt" else "")
            path.write_text(code)
            command = shlex.split(os.environ.get("CC", "cc")) + ["-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-but-set-variable"]
            if args.sanitize:
                command += ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
            result = subprocess.run(command + [str(path), "-o", str(exe)])
            if result.returncode == 0:
                selected = args.mru_case if suite == "mru" else args.gesture_case if suite == "gesture" else None
                case = [selected] if selected else []
                result = subprocess.run([str(exe)] + case)
            print(f"{suite}: {'PASS' if result.returncode == 0 else 'FAIL'}", flush=True)
            failed += result.returncode != 0
    return bool(failed)


if __name__ == "__main__":
    raise SystemExit(main())
