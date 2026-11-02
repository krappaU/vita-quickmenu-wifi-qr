// SPDX-License-Identifier: MIT
#include "qr_decode.h"

#include <stdlib.h>
#include <string.h>

#include "quirc.h"
#include "quirc_internal.h" // num_capstones: no public accessor

struct qr_decoder {
    struct quirc *q;
    int w, h;
};

qr_decoder *qr_decoder_new(int width, int height) {
    qr_decoder *d = malloc(sizeof *d);
    if (!d) return NULL;
    d->q = quirc_new();
    if (!d->q || quirc_resize(d->q, width, height) < 0) {
        if (d->q) quirc_destroy(d->q);
        free(d);
        return NULL;
    }
    d->w = width;
    d->h = height;
    return d;
}

void qr_decoder_free(qr_decoder *d) {
    if (!d) return;
    quirc_destroy(d->q);
    free(d);
}

int qr_decode(qr_decoder *d, const uint8_t *luma, int pitch, char *payload, int payload_size, qr_stats *st) {
    int w, h;
    uint8_t *img = quirc_begin(d->q, &w, &h);
    for (int y = 0; y < h; y++) memcpy(img + y * w, luma + y * pitch, (size_t)w);
    quirc_end(d->q);

    int n = quirc_count(d->q);
    if (st) {
        st->codes = n;
        st->capstones = d->q->num_capstones;
    }
    for (int i = 0; i < n; i++) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(d->q, i, &code);
        quirc_decode_error_t err = quirc_decode(&code, &data);
        if (err == QUIRC_ERROR_DATA_ECC) {
            // A mirrored image (e.g. a selfie-style camera) decodes after flipping.
            quirc_flip(&code);
            err = quirc_decode(&code, &data);
        }
        if (err != QUIRC_SUCCESS) continue;
        int len = data.payload_len < payload_size - 1 ? data.payload_len : payload_size - 1;
        memcpy(payload, data.payload, (size_t)len);
        payload[len] = 0;
        return len;
    }
    return 0;
}
