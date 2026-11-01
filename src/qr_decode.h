// SPDX-License-Identifier: MIT
// Finds and decodes a QR code in a grayscale image with quirc. No system calls (memory comes from
// libc_shim.c on the Vita), so the host tests run it on generated images.
#ifndef QR_DECODE_H
#define QR_DECODE_H

#include <stdint.h>

#define QR_PAYLOAD_MAX 8896 // quirc's QUIRC_MAX_PAYLOAD

typedef struct qr_decoder qr_decoder;

qr_decoder *qr_decoder_new(int width, int height);
void qr_decoder_free(qr_decoder *d);

// luma: width*height bytes, one row every `pitch` bytes. Returns the payload length (0 if no
// code was decoded); payload is zero-terminated. *seen is set to the number of codes found,
// decoded or not, so the caller can tell "no code in view" from "code too blurry".
int qr_decode(qr_decoder *d, const uint8_t *luma, int pitch, char *payload, int payload_size, int *seen);

#endif
