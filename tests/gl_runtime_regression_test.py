#!/usr/bin/env python3
"""Run extracted production upload/render functions on a real Mesa EGL context.

Requires a host C compiler and libEGL.so.1/libGL.so.1. Win32 and WGL calls are
adapters; this does not execute Windows, a WGL pixel format, or a Windows driver.
"""
import argparse,hashlib,json,shlex,subprocess
from pathlib import Path
from repair_regression_test import ROOT,extract

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-root',type=Path,default=ROOT)
    p.add_argument('--out',type=Path,default=ROOT/'build-mingw/gl-test')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    body='\n'.join(extract(a.source_root,'hwgl.c',n) for n in ['_viv_gl_texture_upload','_viv_hwgl_render'])
    source=(ROOT/'tests/repair/common.h').read_text()+(ROOT/'tests/repair/gl_runtime.c').read_text().replace('/* PRODUCTION */',body)
    path=a.out/'gl_runtime.c';exe=a.out/'gl_runtime';path.write_text(source)
    cmd=['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=undefined','-fno-sanitize-recover=all',str(path),'-o',str(exe),'-l:libEGL.so.1','-l:libGL.so.1']
    r=subprocess.run(cmd,capture_output=True,text=True);log='$ '+shlex.join(cmd)+'\n'+r.stdout+r.stderr
    row={'compile_exit':r.returncode,'production_body_sha256_lf':hashlib.sha256(body.encode()).hexdigest()}
    if not r.returncode:
        r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=45);log+=r.stdout+r.stderr;row['run_exit']=r.returncode
    (a.out/'result.json').write_text(json.dumps(row,indent=2));(a.out/'gl_runtime.log').write_text(log);print(log)
    return int(row['compile_exit']!=0 or row.get('run_exit')!=0)
if __name__=='__main__':raise SystemExit(main())
