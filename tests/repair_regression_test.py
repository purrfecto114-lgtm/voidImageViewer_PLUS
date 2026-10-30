#!/usr/bin/env python3
"""Run current production C bodies with Win32/COM fault adapters and UBSan.

These tests do not execute Windows, WGL, a D3D9 driver, or an NSIS installer.
--source-root supports a red/green comparison against the unmodified tree.
"""
import argparse, hashlib, json, os, re, shlex, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
TOKENS=re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',re.S)
def extract(root,file,name):
    source=(root/'src'/file).read_text(encoding='utf-8-sig')
    m=re.search(r'(?m)^[\w *]+\b'+re.escape(name)+r'\([^;{}]*\)\s*\n\{',source)
    if not m: raise ValueError(f'missing {file}:{name}')
    depth=0
    for t in TOKENS.finditer(source,source.index('{',m.start())):
        if t.group()=='{':depth+=1
        elif t.group()=='}':
            depth-=1
            if not depth:return source[m.start():t.end()]
    raise ValueError('unterminated function')
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,default=ROOT)
    parser.add_argument('--out',type=Path)
    parser.add_argument('suites',nargs='*')
    args=parser.parse_args()
    suites={'d3d':('hwd3d.c',['_viv_d3d_attempt_reset','_viv_hwd3d_render']),
            'gl':('hwgl.c',['_viv_hwgl_render']),
            'mipmap':('viv_render.c',['_viv_get_mipmap']),
            'shell':('os.c',['os_shell_execute']),
            'orientation':('viv_render.c',['_viv_orientate_hbitmap']),
            'rotate_atomic':('viv_view.c',['_viv_edit_rotate']),
            'd3d_upload':('hwd3d.c',['_viv_hwd3d_release_image','_viv_d3d_texture_upload']),
            'gl_lifecycle':('hwgl.c',['_viv_hwgl_release_image']),
            'cache_budget':('viv_load.c',['_viv_frame_set_bytes']),
            'allocator':('mem.c',['mem_try_alloc_debug','mem_cc','mem_free_debug'])}
    rows=[]
    with tempfile.TemporaryDirectory(prefix='viv-repair-') as tmp:
        out=args.out or Path(tmp);out.mkdir(parents=True,exist_ok=True)
        for name in args.suites or suites:
            file,names=suites[name];names=list(names)
            source=(args.source_root/'src'/file).read_text()
            if name=='mipmap' and '_viv_mipmap_build(' in source:names.insert(0,'_viv_mipmap_build')
            if name=='rotate_atomic' and '_viv_prepare_oriented_frames(' in source:names.insert(0,'_viv_prepare_oriented_frames')
            bodies='\n'.join(extract(args.source_root,'viv_render.c' if n=='_viv_prepare_oriented_frames' else file,n) for n in names)
            if name=='allocator':
                start=source.index('typedef struct mem_debug_s');end=source.index('void mem_debug(void)',start)
                bodies='#define MEM_MAGIC_SIZE 64\n'+source[start:end]+bodies
            code=(ROOT/'tests/repair/common.h').read_text()+(ROOT/f'tests/repair/{name}.c').read_text().replace('/* PRODUCTION */',bodies)
            path=out/(name+'.c');exe=out/(name+'.bin');path.write_text(code)
            cmd=shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-int-conversion','-fsanitize=undefined','-fno-sanitize-recover=all',str(path),'-o',str(exe)]
            r=subprocess.run(cmd,capture_output=True,text=True);log='$ '+shlex.join(cmd)+'\n'+r.stdout+r.stderr
            row={'suite':name,'production_body_sha256_lf':hashlib.sha256(bodies.encode()).hexdigest(),'compile_exit':r.returncode}
            if r.returncode==0:
                r=subprocess.run([str(exe)],capture_output=True,text=True);row['run_exit']=r.returncode;log+=r.stdout+r.stderr
            row['passed']=row['compile_exit']==0 and row.get('run_exit')==0
            (out/(name+'.log')).write_text(log);rows.append(row)
            print(f"{name}: {'PASS' if row['passed'] else 'FAIL'}",flush=True)
            if not row['passed']:print(log[-3000:],flush=True)
        (out/'results.json').write_text(json.dumps(rows,indent=2))
    return int(any(not r['passed'] for r in rows))
if __name__=='__main__':raise SystemExit(main())
