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

// What the decoder saw in a frame, for guiding the user (there is no camera preview).
typedef struct {
    int capstones; // QR corner squares found: 1-2 means part of a code is in view
    int codes;     // codes found (all three corners), decoded or not
} qr_stats;

// luma: width*height bytes, one row every `pitch` bytes. Returns the payload length (0 if no
// code was decoded); payload is zero-terminated.
int qr_decode(qr_decoder *d, const uint8_t *luma, int pitch, char *payload, int payload_size, qr_stats *st);

#endif
