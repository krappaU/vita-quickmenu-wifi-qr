#!/usr/bin/env python3
# Checks that the function NIDs in src/wifiqr_api.h match the core's exports as vita-elf-export
# computed them: front ends look the API up by those numbers.
import re
import sys

exports = open(sys.argv[1]).read()
header = open(sys.argv[2]).read()
want = {
    "wifiqrScan": "WIFIQR_NID_SCAN",
    "wifiqrCancel": "WIFIQR_NID_CANCEL",
    "wifiqrIsBusy": "WIFIQR_NID_ISBUSY",
    "wifiqrSelfTest": "WIFIQR_NID_SELFTEST",
}
ok = True
lib = re.search(r"WifiQr:\s*\n\s*nid:\s*(0x[0-9a-fA-F]+)", exports.split("libraries:")[1])
hl = re.search(r"#define WIFIQR_LIB_NID (0x[0-9A-Fa-f]+)", header)
if not lib or not hl or int(lib.group(1), 16) != int(hl.group(1), 16):
    print("check_nids: library NID differs", lib and lib.group(1), hl and hl.group(1))
    ok = False
for fn, macro in want.items():
    e = re.search(fn + r":\s*(0x[0-9a-fA-F]+)", exports)
    h = re.search(r"#define " + macro + r" (0x[0-9A-Fa-f]+)", header)
    if not e or not h or int(e.group(1), 16) != int(h.group(1), 16):
        print(f"check_nids: {fn} is {e and e.group(1)} in the core, {h and h.group(1)} in wifiqr_api.h")
        ok = False
sys.exit(0 if ok else 1)
