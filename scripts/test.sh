#!/bin/bash
# Host tests: the QR parser, the profile writer (against an in-memory registry) and QR decoding
# with quirc on generated camera-like images.
# Needs gcc and python3 with the `qrcode` and `pillow` packages (pip install qrcode==8.0
# pillow==11.0.0) for the decode test; without them that test is skipped.
set -euo pipefail
cd "$(dirname "$0")/.."
scripts/fetch_deps.sh >/dev/null
OUT=build-test
mkdir -p "$OUT/img"
CFLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -Isrc"
Q=third_party/quirc/lib
QFLAGS="-DQUIRC_FLOAT_TYPE=float -DQUIRC_USE_TGMATH -I$Q"

gcc $CFLAGS -o "$OUT/test_wifi_qr" tests/test_wifi_qr.c src/wifi_qr.c
gcc $CFLAGS -o "$OUT/test_profiles" tests/test_profiles.c tests/reg_fake.c src/profiles.c
gcc $CFLAGS -o "$OUT/test_viewfinder" tests/test_viewfinder.c src/viewfinder.c
for f in decode quirc version_db; do
    gcc -c -std=c11 -O1 -g -w -fsanitize=address,undefined $QFLAGS -o "$OUT/$f.o" "$Q/$f.c"
done
# quirc's identify.c, plus the corner-square grouping fallback (src/quirc_ext.c includes it)
gcc -c -std=c11 -O1 -g -w -fsanitize=address,undefined $QFLAGS -Isrc -o "$OUT/identify.o" src/quirc_ext.c
gcc $CFLAGS $QFLAGS -o "$OUT/test_qr_decode" tests/test_qr_decode.c src/qr_decode.c src/wifi_qr.c \
    "$OUT"/decode.o "$OUT"/identify.o "$OUT"/quirc.o "$OUT"/version_db.o -lm

status=0
"$OUT/test_wifi_qr" || status=1
"$OUT/test_profiles" || status=1
"$OUT/test_viewfinder" || status=1

PY=${PYTHON:-python3}
if "$PY" -c "import qrcode, PIL" 2>/dev/null; then
    args=()
    while IFS=' ' read -r path payload; do args+=("$path" "$payload"); done < <("$PY" tests/make_qr.py "$OUT/img")
    "$OUT/test_qr_decode" "${args[@]}" || status=1
else
    echo "qr_decode: SKIPPED (python packages qrcode and pillow not installed)"
fi
exit $status
