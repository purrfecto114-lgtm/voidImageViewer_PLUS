#!/usr/bin/env python3
"""The mutation teeth table: every pin this round landed must bite.

Each row names one mutation - an exact source swap the round's own
guards must catch. the harness applies the mutation, runs the suites
the row names, expects a nonzero exit (a red), restores the tree, and
reports the percentage. the table is deterministic: the same rows,
the same order, the same verdicts - "we caught it" is a number
anyone can recompute, not a sentence in a worklog.

Usage: python3 tools/mutation_teeth.py  (from the repository root)
"""
import subprocess
import sys
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
os.chdir(ROOT)

SUITES = {
    "menu": ["python3", "tests/menu_structure_test.py"],
    "sim": ["python3", "tests/simulation_test.py"],
}

# (name, file, find, replace, suites-that-must-go-red)
MUTATIONS = [
    ("the toggle anchor regresses to the wrong pane",
     "src/viv_wndproc.c",
     "if (item == _viv_status_frame_pane_index())",
     "if (item == (int)SendMessage(_viv_status_hwnd,SB_GETPARTS,0,0) - 2)",
     ["menu"]),

    ("the layout measures the opening text again (the 9-to-10 hitch)",
     "src/viv_chrome.c",
     "_viv_status_frame_text_at(reserve_buf,_viv_slot_current.frame_count);",
     "_viv_status_frame_text(reserve_buf);",
     ["sim"]),

    ("the paint-level frame guard comes off",
     "src/viv_wndproc.c",
     'frame_state_ok = (_viv_slot_current.frame_count) && (_viv_frame_state_guard("paint"));',
     "frame_state_ok = (_viv_slot_current.frame_count);",
     ["menu"]),

    ("the truncated wrap waits forever again",
     "src/viv_wndproc.c",
     "_viv_frame_looped = 1;\r\n\t\t\t\t\t\t\t\t\t_viv_frame_position = -1;",
     "_viv_frame_looped = 0;\r\n\t\t\t\t\t\t\t\t\t_viv_frame_position = -1;",
     ["menu"]),

    ("the delete pair loses its fourth element",
     "src/viv_view.c",
     "_viv_should_activate_preload_on_load = 0;\r\n\t\t\t\t_viv_slot_preload.fd.cFileName[0] = 0;",
     "_viv_should_activate_preload_on_load = 0;\r\n\t\t\t\t_viv_slot_preload.fd.cFileName[0] = 1;",
     ["menu"]),

    ("the strip stops answering the keyboard",
     "src/viv_toolbar.c",
     "case WM_KEYDOWN:",
     "case 0x0BAD:",
     ["menu"]),

    ("the dead arrow lights for any hidden group again",
     "src/viv_toolbar.c",
     "_viv_toolbar_arrow_left = (itemi < _VIV_TOOLBAR_ITEM_COUNT) ? 1 : 0;",
     "_viv_toolbar_arrow_left = (_viv_toolbar_page > 0) ? 1 : 0;",
     ["menu"]),

    ("the frame step pauses on a refused step again",
     "src/viv_anim.c",
     "// the pause rides the step that actually moved: refusing",
     "// the pause rides any step request: refusing",
     ["menu"]),
]


def run_suite(key):
    r = subprocess.run(SUITES[key], capture_output=True, text=True)
    return r.returncode


def main():
    caught = 0
    escaped = []
    for name, path, find, replace, suites in MUTATIONS:
        with open(path, "rb") as f:
            original = f.read()
        src = original.decode("latin-1")
        if src.count(find) != 1:
            print("SKIP (anchor drifted): %s" % name)
            escaped.append(name)
            continue
        with open(path, "wb") as f:
            f.write(src.replace(find, replace).encode("latin-1"))
        reds = {k: run_suite(k) for k in suites}
        with open(path, "wb") as f:
            f.write(original)
        if all(rc != 0 for rc in reds.values()):
            caught += 1
            print("CAUGHT  %s  (red in %s)" % (name, ",".join(suites)))
        else:
            escaped.append(name)
            print("ESCAPED %s  (exit codes %s)" % (name, reds))
    total = len(MUTATIONS)
    pct = (100 * caught) // total if total else 100
    print()
    print("teeth: %d/%d CAUGHT (%d%%)" % (caught, total, pct))
    if escaped:
        print("escaped rows: %s" % escaped)
    return 0 if not escaped else 1


if __name__ == "__main__":
    sys.exit(main())
