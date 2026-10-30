#!/usr/bin/env python3
"""Check every payload file against verification/files.sha256 (integrity only)."""
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[1]

def main():
    entries=(ROOT/'verification/files.sha256').read_text(encoding='utf-8').splitlines()
    failed=[]
    for line in entries:
        digest,name=line.split('  ',1)
        path=(ROOT/name).resolve()
        if not path.is_relative_to(ROOT) or not path.is_file():failed.append(name);continue
        if hashlib.sha256(path.read_bytes()).hexdigest()!=digest:failed.append(name)
    if failed:
        print('FAIL:',', '.join(failed));return 1
    print(f'PASS: {len(entries)} payload files match SHA-256; this is not an origin signature.');return 0
if __name__=='__main__':raise SystemExit(main())
