#!/usr/bin/env python3
"""Check the machine and GUI subsystem of a cross-built PE (no Windows needed).

This checks PE header fields only. It does not replace pe_security_check.ps1
or prove that the program runs. Usage: pe_header_check.py viv.exe --machine x64
"""
import argparse
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path)
    parser.add_argument("--machine", required=True, choices=["x64", "arm64"])
    args = parser.parse_args()
    try:
        data = args.file.read_bytes()
        if len(data) < 64 or data[:2] != b"MZ":
            raise ValueError("missing DOS header")
        pe = struct.unpack_from("<I", data, 0x3c)[0]
        if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("missing PE signature/header")
        machine = struct.unpack_from("<H", data, pe + 4)[0]
        size = struct.unpack_from("<H", data, pe + 20)[0]
        opt = pe + 24
        if size < 72 or opt + size > len(data):
            raise ValueError("truncated optional header")
        magic = struct.unpack_from("<H", data, opt)[0]
        subsystem, flags = struct.unpack_from("<HH", data, opt + 68)
        print(f"{args.file}: machine=0x{machine:04x}, magic=0x{magic:04x}, "
              f"subsystem={subsystem}, DllCharacteristics=0x{flags:04x}, bytes={len(data)}")
        if machine != {"x64": 0x8664, "arm64": 0xaa64}[args.machine] or magic != 0x20b:
            raise ValueError("expected the selected 64-bit machine and PE32+ header")
        if subsystem != 2:
            raise ValueError("expected IMAGE_SUBSYSTEM_WINDOWS_GUI (2)")
    except (OSError, ValueError, struct.error) as error:
        print(f"FAIL: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
