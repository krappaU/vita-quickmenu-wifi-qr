#!/bin/sh
# Builds build/wifiqr.suprx in the VitaSDK Docker image.
set -eu
cd "$(dirname "$0")/.."
scripts/fetch_deps.sh
IMAGE=${VITASDK_IMAGE:-vitasdk/vitasdk:latest}
docker run --rm -v "$PWD":/w -w /w "$IMAGE" sh -c \
  'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j"$(nproc)"'
ls -l build/wifiqr.suprx build/wifiqr_qm.suprx build/wifiqr_kernel.skprx
