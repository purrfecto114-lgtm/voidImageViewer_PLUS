#!/usr/bin/env python3
"""Check failure-branch control flow and resource-bearing PE artifacts.
No installer execution or Windows GUI execution is performed here.
"""
import argparse,hashlib,json,re,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def nsis_errors(root):
    lines=(root/'nsis/installer.nsi').read_text(encoding='utf-8-sig').splitlines()
    labels={line.strip()[:-1]:i+1 for i,line in enumerate(lines) if re.fullmatch(r'[\w.]+:',line.strip())}
    results=[]
    for label in ['exec_admin_error','exec_install_options_error','uninstall_stage_error']:
        pc=labels[label];exit_code=0;trace=[];aborted=False
        for _ in range(40):
            op=lines[pc].strip();pc+=1
            if not op or op.startswith(';') or op.endswith(':'):continue
            trace.append(op)
            if op.startswith('SetErrorLevel '):exit_code=int(op.split()[1])
            elif op=='Abort':aborted=True;break
            elif op.startswith(('MessageBox ','Delete ','RMDir ')):pass
            else:raise AssertionError(f'{label}: reached {op} before failure exit')
        assert aborted and exit_code!=0,label
        results.append({'label':label,'errorlevel':exit_code,'aborted':aborted,'trace':trace})
    return results

def inspect_pe(path):
    b=path.read_bytes();assert b[:2]==b'MZ'
    pe=struct.unpack_from('<I',b,0x3c)[0];assert b[pe:pe+4]==b'PE\0\0'
    machine,sections=struct.unpack_from('<HH',b,pe+4)
    opt_size,coff=struct.unpack_from('<HH',b,pe+20);opt=pe+24
    magic=struct.unpack_from('<H',b,opt)[0];assert magic in (0x10b,0x20b)
    subsystem,flags=struct.unpack_from('<HH',b,opt+68)
    dd=opt+(112 if magic==0x20b else 96)
    resource_rva,resource_size=struct.unpack_from('<II',b,dd+16)
    config_rva,config_size=struct.unpack_from('<II',b,dd+80)
    section_start=opt+opt_size
    resource_offset=None
    for i in range(sections):
        entry=section_start+i*40
        virtual_size,rva,raw_size,raw=struct.unpack_from('<IIII',b,entry+8)
        if rva<=resource_rva<rva+max(virtual_size,raw_size):resource_offset=raw+resource_rva-rva
    assert resource_offset is not None and resource_size>0
    named,ids=struct.unpack_from('<HH',b,resource_offset+12)
    types={struct.unpack_from('<I',b,resource_offset+16+i*8)[0] for i in range(named+ids)}
    assert {3,5,14,16,24}.issubset(types),types
    assert coff & 0x20 and subsystem==2
    assert flags & 0x4140==0x4140 and config_rva and config_size
    if magic==0x20b:assert flags & 0x20
    return {'file':str(path),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'machine':hex(machine),'coff_characteristics':hex(coff),'dll_characteristics':hex(flags),'LAA':bool(coff&0x20),'ASLR':bool(flags&0x40),'DEP':bool(flags&0x100),'CFG':bool(flags&0x4000),'load_config_bytes':config_size,'resource_types':sorted(types)}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-root',type=Path,default=ROOT);p.add_argument('--out',type=Path);p.add_argument('--binaries',type=Path)
    a=p.parse_args();result={'nsis_error_paths':nsis_errors(a.source_root)}
    if a.binaries:result['binaries']=[inspect_pe(a.binaries/arch/'voidImageViewer.exe') for arch in ['x86','x64','arm64']]
    if a.out:a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
