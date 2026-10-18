#!/usr/bin/env python3
"""Compile and execute selected production C bodies with small OS adapters.

This is a host-side regression gate, not a Windows GUI test. Bodies are read
from the working tree on every run; no Python copy of the MRU/gesture/ini
logic is used. A full Windows cross-build still checks headers and
integration. Requires a host C compiler (CC, default cc). Run from any
directory.
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
    if suite == "ini":
        ini_h = source("ini.h")
        ini_c = source("ini.c")
        # the two report enums, verbatim from the header
        a_start = ini_h.rindex("enum", 0, ini_h.index("INI_ANOMALY_NONE"))
        anomaly_enum = ini_h[a_start:ini_h.index(";", ini_h.index("INI_ANOMALY_OVERSIZED_VALUE")) + 1]
        l_start = ini_h.rindex("enum", 0, ini_h.index("INI_LOAD_OK"))
        load_enum = ini_h[l_start:ini_h.index(";", ini_h.index("INI_LOAD_UNREADABLE")) + 1]
        field = re.search(r"(?m)^#define\s+_INI_MAX_FIELD\s+\d+", ini_c)
        if not field:
            raise ValueError("ini field-ceiling definition missing")
        # the types and the semantic functions, verbatim from the reader
        kv_typedef = ini_c[ini_c.index("// an ini key to load."):ini_c.index("}_ini_keyvalue_t;") + len("}_ini_keyvalue_t;")]
        ini_typedef = ini_c[ini_c.index("// an ini file."):ini_c.index("}_ini_t;") + len("}_ini_t;")]
        nul_brace = ini_c.rindex("{", 0, ini_c.index("DWORD nul_scan;"))
        nul_block = block(ini_c, nul_brace)
        walk_begin = ini_c.index("is_section = 0;")
        walk_end = ini_c.index("}", ini_c.index("INI_ANOMALY_SECTION_MISSING;")) + 1
        walk_region = ini_c[walk_begin:walk_end]
        dedup_head = ini_c.index("if (count)")
        dedup_brace = ini_c.index("{", dedup_head)
        dedup_body = block(ini_c, dedup_brace)
        dedup_region = ini_c[dedup_head:dedup_brace + len(dedup_body)]
        # the walk region is ini_open's parse core: its locals and its
        # report globals are provided by the wrapper, the way ini_open
        # declares them before the parse. the nul scan rides with it.
        bodies = (
            kv_typedef + "\n" + ini_typedef + "\n"
            + "static _ini_keyvalue_t *kv_start;\n"
              "static _ini_keyvalue_t *kv_last;\n"
              "static size_t count;\n"
              "static int section_found;\n"
              "static _ini_t *ini;\n"
            + function("ini.c", "_ini_line_is_blank") + "\n"
            + function("utf8.c", "utf8_skip_utf8_header") + "\n"
            + function("utf8.c", "utf8_compare") + "\n"
            + function("ini.c", "_ini_keyvalue_compare") + "\n"
            + "static void the_nul_scan(utf8_t *buf, size_t size)\n{\n"
            + nul_block + "\n}\n"
            + "static void the_walk(utf8_t *buf, size_t size, const utf8_t *ascii_section)\n{\n"
              "    const utf8_t *p;\n"
              "    int is_section;\n"
              "    kv_start = 0;\n"
              "    kv_last = 0;\n"
              "    count = 0;\n"
              "    section_found = 0;\n"
              "    _ini_last_state = INI_LOAD_OK;\n"
              "    _ini_last_anomaly = INI_ANOMALY_NONE;\n"
              "    the_nul_scan(buf, size);\n"
            + walk_region + "\n"
              "}\n"
              "static _ini_t *the_result(void)\n{\n"
              "    ini = 0;\n"
            + dedup_region + "\n"
              "    return ini;\n"
              "}\n"
        )
        return "\n".join([anomaly_enum, load_enum, field.group()]) + "\n", bodies
    raise ValueError(suite)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suites", nargs="*", choices=["mru", "gesture", "memory", "ini"])
    parser.add_argument("--sanitize", action="store_true", help="enable undefined-behavior sanitizer")
    parser.add_argument("--mru-case", choices=["rename", "load"], help="run one MRU scenario group")
    parser.add_argument("--gesture-case", choices=["policy", "zoom", "zero", "begin", "touch", "wheel", "bounds"], help="run one input scenario group")
    args = parser.parse_args()
    failed = 0
    with tempfile.TemporaryDirectory(prefix="viv-native-") as directory:
        for suite in args.suites or ["mru", "gesture", "memory", "ini"]:
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
                selected = args.mru_case if suite == "mru" else args.gesture_case if suite == "gesture" else None  # ini has no case selector
                case = [selected] if selected else []
                result = subprocess.run([str(exe)] + case)
            print(f"{suite}: {'PASS' if result.returncode == 0 else 'FAIL'}", flush=True)
            failed += result.returncode != 0
    return bool(failed)


if __name__ == "__main__":
    raise SystemExit(main())
