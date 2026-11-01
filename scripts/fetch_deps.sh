#!/bin/sh
# Fetches third-party code into third_party/ (ignored by git) at pinned commits.
# - QuickMenuReborn (Ibrahim778): the Quick Menu widget API, header and NID table.
# - quirc (dlbeer, ISC): QR code detection and decoding.
set -eu
cd "$(dirname "$0")/.."
QMR_COMMIT=a3e067e630722bab6f6e245e587243390519a052
QUIRC_COMMIT=f2e4271ae43a15d9263704e07bc86cba5697d74d
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
echo "third_party ready"
