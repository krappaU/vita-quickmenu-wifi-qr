#!/usr/bin/env python3
# Writes a vita-libs-gen table (YAML) for the shell's UI library (PAF) from ScePaf-RE's .emd
# files, for the libraries named on the command line. A function or variable without an explicit
# nidvalue has the NID Sony's tools give it: the first 4 bytes of SHA-1 of its mangled name plus
# the library's nidsuffix, little-endian. (Checked against the 3.65 impose_net_plugin's imports:
# paf::Plugin::Find is 0x004D98CC either way.)
#
# usage: paf_yml.py <ScePaf-RE lib dir> <out.yml> ScePafStdc ScePafToplevel ...
import glob
import hashlib
import os
import re
import sys


def nid(name, suffix):
    return int.from_bytes(hashlib.sha1((name + suffix).encode()).digest()[:4], "little")


def main():
    libdir, out, wanted = sys.argv[1], sys.argv[2], set(sys.argv[3:])
    libs = {}
    for path in sorted(glob.glob(os.path.join(libdir, "*.emd"))):
        suffix = ""
        for line in open(path, encoding="utf-8", errors="replace"):
            m = re.match(r'Library: (\S+) nidsuffix: "(.*)"', line)
            if m:
                suffix = m.group(2)
                continue
            m = re.match(r"Library: (\S+) libnamenid: (0x[0-9A-Fa-f]+)", line)
            if m:
                libs.setdefault(m.group(1), {"nid": int(m.group(2), 16), "functions": {}, "variables": {}})
                continue
            m = re.match(r"Library: (\S+) (function|variable): (\S+)(?: nidvalue: (0x[0-9A-Fa-f]+))?", line)
            if m and m.group(1) in libs:
                kind = "functions" if m.group(2) == "function" else "variables"
                value = int(m.group(4), 16) if m.group(4) else nid(m.group(3), suffix)
                libs[m.group(1)][kind][m.group(3)] = value
    missing = wanted - set(libs)
    if missing:
        sys.exit("paf_yml: no .emd for " + ", ".join(sorted(missing)))
    with open(out, "w") as f:
        f.write("modules:\n  ScePaf:\n    nid: 0x00000000\n    libraries:\n")
        for name in sorted(wanted):
            lib = libs[name]
            f.write(f"      {name}:\n        kernel: false\n        nid: 0x{lib['nid']:08X}\n")
            for kind in ("functions", "variables"):
                if lib[kind]:
                    f.write(f"        {kind}:\n")
                    for sym, value in sorted(lib[kind].items()):
                        f.write(f"          {sym}: 0x{value:08X}\n")


main()
