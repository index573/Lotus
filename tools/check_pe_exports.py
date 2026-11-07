#!/usr/bin/env python3
"""Checks that a Windows DLL exports the given function names, undecorated.
FL Studio finds native plugins by the export CreatePlugInstance, so CI runs:

    python tools/check_pe_exports.py Lotus_x64.dll CreatePlugInstance

Prints the export list; exits 1 if a name is missing or the file isn't a 64-bit DLL."""
import struct, sys


def exports(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE file")
    machine, nsections = struct.unpack_from("<HH", data, pe + 4)
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    if struct.unpack_from("<H", data, opt)[0] != 0x20B:
        raise ValueError("not a 64-bit (PE32+) image")
    export_rva, export_size = struct.unpack_from("<II", data, opt + 112)
    sections = []
    for i in range(nsections):
        s = opt + opt_size + 40 * i
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, s + 8)
        sections.append((va, max(vsize, rawsize), rawptr))

    def off(rva):
        for va, size, raw in sections:
            if va <= rva < va + size:
                return rva - va + raw
        raise ValueError("RVA %x outside sections" % rva)

    if export_rva == 0:
        return machine, []
    d = off(export_rva)
    nnames = struct.unpack_from("<I", data, d + 24)[0]
    names_rva = struct.unpack_from("<I", data, d + 32)[0]
    names = []
    for i in range(nnames):
        p = off(struct.unpack_from("<I", data, off(names_rva) + 4 * i)[0])
        names.append(data[p:data.index(b"\0", p)].decode("ascii"))
    return machine, names


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    try:
        machine, names = exports(sys.argv[1])
    except (OSError, ValueError, struct.error) as e:
        print("%s: %s" % (sys.argv[1], e))
        return 1
    print("machine 0x%04x, exports: %s" % (machine, ", ".join(names) or "(none)"))
    missing = [n for n in sys.argv[2:] if n not in names]
    if machine != 0x8664:
        print("expected an x64 DLL")
        return 1
    if missing:
        print("missing: " + ", ".join(missing))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
