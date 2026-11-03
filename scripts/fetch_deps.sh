#!/bin/sh
# Fetches third-party code into third_party/ (ignored by git) at pinned commits.
# - QuickMenuReborn (Ibrahim778): the Quick Menu widget API, header and NID table.
# - quirc (dlbeer, ISC): QR code detection and decoding.
# - ScePaf-RE (GrapheneCt): headers and NID tables for the shell's UI library (PAF), restored
#   from debug data; no licence file, so fetched, not kept in this repository.
# - vitasdk-paf-component (Princess-of-Sleeping, MIT): its patch that makes those headers build
#   with VitaSDK. The result is assembled in third_party/paf/include.
set -eu
cd "$(dirname "$0")/.."
QMR_COMMIT=a3e067e630722bab6f6e245e587243390519a052
QUIRC_COMMIT=f2e4271ae43a15d9263704e07bc86cba5697d74d
SCEPAF_RE_COMMIT=b76d6ae00516b107d37e7e8a1a16211e3b729a07
PAF_COMPONENT_COMMIT=9a68b07aa2d7247a756765bf154f292cda20aed3
mkdir -p third_party

fetch() { # url dir commit
    if [ ! -d "third_party/$2/.git" ]; then
        git init -q "third_party/$2"
        git -C "third_party/$2" remote add origin "$1"
    fi
    if [ "$(git -C "third_party/$2" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$3" ]; then
        git -C "third_party/$2" fetch -q --depth 1 origin "$3"
        git -C "third_party/$2" checkout -q FETCH_HEAD
    fi
}
fetch https://github.com/Ibrahim778/QuickMenuReborn QuickMenuReborn "$QMR_COMMIT"
fetch https://github.com/dlbeer/quirc quirc "$QUIRC_COMMIT"
fetch https://github.com/GrapheneCt/ScePaf-RE ScePaf-RE "$SCEPAF_RE_COMMIT"
fetch https://github.com/Princess-of-Sleeping/vitasdk-paf-component paf-component "$PAF_COMPONENT_COMMIT"

# PAF headers: ScePaf-RE's, patched for VitaSDK, plus paf-component's replacements for the two
# headers that need the official SDK (paf::map, paf::math).
STAMP="third_party/paf/.built-$SCEPAF_RE_COMMIT-$PAF_COMPONENT_COMMIT"
if [ ! -f "$STAMP" ]; then
    rm -rf third_party/paf
    mkdir -p third_party/paf
    cp -r third_party/ScePaf-RE/include third_party/paf/include
    cp -r third_party/ScePaf-RE/lib third_party/paf/lib
    (cd third_party/paf && patch -p2 -s < ../paf-component/vitasdk_2.patch)
    cp third_party/paf-component/include/map third_party/paf/include/paf/std/map
    cp third_party/paf-component/include/math.h third_party/paf/include/paf/math/math.h
    touch "$STAMP"
fi
echo "third_party ready"
