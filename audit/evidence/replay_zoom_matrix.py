#!/usr/bin/env python3
"""Reproduce the independent 1024-case real-C 1:1 boundary review.

Use --output DIR to retain evidence. This never changes project sources.
Production renderer/input bodies and their constants are extracted through
the project's native regression driver. Window/OS/view-position adapters
remain the same as tests/native/gesture.c; this is not a Windows UI test.
"""
from pathlib import Path
import argparse
import hashlib
import os
import tempfile
import json
import runpy
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]

MAIN = r'''
int main(void) {
    const int dims[][4]={{1,1,1600,1000},{800,600,1600,1200},{4000,3000,1600,1000},{1,4000,1600,1000},{4000,1,1600,1000},{1000000,1,10,10},{1234,777,1,1},{200,400,1920,1080}};
    const int counts[]={-1302,-120,-10,-1,1,10,120,1302};
    int a,f,k,s,fs,j,checked=0;
    init_scales();
    for(a=0;a<8;a++) for(f=0;f<2;f++) for(k=0;k<2;k++) for(s=0;s<2;s++) for(fs=0;fs<2;fs++) for(j=0;j<8;j++) {
        int top,floor,pos,rw,rh,found=0,candidate=0,native,actual,expected;
        geometry(dims[a][0],dims[a][1],dims[a][2],dims[a][3],f);
        config_keep_aspect_ratio=k; config_allow_shrinking=s;
        _viv_is_fullscreen=fs; config_fullscreen_fill_window=1-f;
        top=_viv_zoom_pos_max(); floor=_viv_zoom_pos_floor();
        native=dims[a][0]>=dims[a][1]?dims[a][0]:dims[a][1];
        for(pos=floor;pos<=top;pos++) {
            _viv_zoom_pos=pos; _viv_get_render_size(&rw,&rh);
            actual=dims[a][0]>=dims[a][1]?rw:rh;
            if((counts[j]>0 && actual>native)||(counts[j]<0 && actual<native)) {
                candidate=pos; found=1; if(counts[j]>0) break;
            }
        }
        expected=found?_viv_clamp_zoom_pos(candidate+counts[j]-(counts[j]>0?1:-1)):42;
        _viv_1to1=1; _viv_zoom_pos=42;
        _viv_zoom_steps(counts[j],dims[a][2]/3,dims[a][3]/4);
        if(_viv_zoom_pos!=expected || _viv_1to1!=!found || invalidated!=found || status_updates!=found) {
            fprintf(stderr,"mismatch image=%d fill=%d keep=%d shrink=%d fullscreen=%d steps=%d got mode%d/pos%d/redraw%d expected mode%d/pos%d/redraw%d\n",a,f,k,s,fs,counts[j],_viv_1to1,_viv_zoom_pos,invalidated,!found,expected,found);
            return 2;
        }
        checked++;
    }
    printf("Expanded real C 1:1 matrix: %d cases passed\n",checked);
    return 0;
}
'''


def verify(output):
    driver = runpy.run_path(str(ROOT / "tests/native_regression_test.py"))
    prefix, bodies = driver["production"]("gesture")
    template = (ROOT / "tests/native/gesture.c").read_text().split("int main(")[0]
    source = output / "zoom_matrix.c"
    executable = output / ("zoom_matrix.exe" if os.name == "nt" else "zoom_matrix")
    source.write_text(prefix + template.replace("/* PRODUCTION */", bodies) + MAIN)
    compile_command = shlex.split(os.environ.get("CC", "cc")) + [ "-std=c99", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable",
        "-Wno-unused-but-set-variable", "-fsanitize=undefined",
        "-fno-sanitize-recover=all", str(source), "-o", str(executable),
    ]
    run_command = [str(executable)]
    (output / "commands.txt").write_text(
        "python3 " + shlex.quote(str(Path(__file__).resolve())) + "\n\n"
        + shlex.join(compile_command) + "\n" + shlex.join(run_command) + "\n"
    )
    compiled = subprocess.run(compile_command, cwd=ROOT, capture_output=True, text=True)
    (output / "compile.stdout.txt").write_text(compiled.stdout)
    (output / "compile.stderr.txt").write_text(compiled.stderr)
    run = None
    if compiled.returncode == 0:
        run = subprocess.run(run_command, cwd=ROOT, capture_output=True, text=True)
        (output / "zoom_matrix.stdout.txt").write_text(run.stdout)
        (output / "zoom_matrix.stderr.txt").write_text(run.stderr)
        print(run.stdout, end="")
        print(run.stderr, end="")
    inputs = [
        "src/viv_view.c", "src/viv_render.c", "src/viv.c", "src/viv_state.h",
        "tests/native/gesture.c", "tests/native_regression_test.py",
    ]
    result = {
        "scope": "1024 1:1 boundary cases through extracted production C and host adapters",
        "windows_ui_or_touch_hardware_test": False,
        "compile_command": compile_command,
        "run_command": run_command,
        "compile_returncode": compiled.returncode,
        "run_returncode": None if run is None else run.returncode,
        "generated_source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "source_sha256": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in inputs},
    }
    (output / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    return compiled.returncode if run is None else run.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="retain generated C, commands and raw logs in this directory")
    args = parser.parse_args()
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        return verify(args.output.resolve())
    with tempfile.TemporaryDirectory(prefix="viv-zoom-matrix-") as directory:
        return verify(Path(directory))


if __name__ == "__main__":
    raise SystemExit(main())
