#!/bin/sh
# Builds lib<Library>_stub.a for the PAF libraries named on the command line, in <out_dir>.
# vita-libs-gen writes one assembly file per function; its Makefile then archives them with one
# command line, which for PAF's ~4000 long mangled names exceeds the shell's argument limit, so
# this assembles and archives them through xargs instead.
#
# usage: paf_stubs.sh <ScePaf-RE lib dir> <out_dir> ScePafStdc ScePafToplevel ...
set -eu
LIBDIR=$1
OUT=$2
shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
cd "$OUT"
python3 "$HERE/paf_yml.py" "$LIBDIR" ScePaf.yml "$@"
rm -f ./*.S ./*.o
vita-libs-gen ScePaf.yml . >/dev/null
for lib in "$@"; do
    rm -f "lib${lib}_stub.a"
    find . -maxdepth 1 -name "ScePaf_${lib}_*.S" -print0 |
        xargs -0 -P "$(nproc)" -I{} sh -c 'arm-vita-eabi-as --defsym GEN_WEAK_EXPORTS=0 "$1" -o "${1%.S}.o"' _ {}
    find . -maxdepth 1 -name "ScePaf_${lib}_*.o" -print0 | xargs -0 arm-vita-eabi-ar cr "lib${lib}_stub.a"
    arm-vita-eabi-ranlib "lib${lib}_stub.a"
done
