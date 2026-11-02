// SPDX-License-Identifier: MIT
#include "qr_decode.h"

#include <stdlib.h>
#include <string.h>

#include "quirc.h"
#include "quirc_internal.h" // num_capstones: no public accessor

struct qr_decoder {
    struct quirc *q;
    int w, h;
    uint16_t *rows;  // ENH_RMAX*2+1 rows of horizontal box sums, for enhance()
};

#define ENH_RMAX 4

// Sharpens (unsharp mask with a (2r+1)^2 box blur, `amount` x the detail) and stretches the
// contrast (1st..99th percentile to 0..255). The Vita's fixed-focus back camera gives soft, dim
// pictures of a code held close: a real frame of 2026-11-02 with all three corner squares found
// decoded only after both, and only for some settings, so qr_decode tries several.
static void enhance(qr_decoder *d, const uint8_t *src, int pitch, uint8_t *dst, int r_, int amount) {
    const int w = d->w, h = d->h;
    const int ENH_R = r_, ENH_K = 2 * r_ + 1;
    int next = 0; // next source row whose horizontal sums go into the ring
    for (int oy = 0; oy < h; oy++) {
        int need = oy + ENH_R < h ? oy + ENH_R : h - 1;
        for (; next <= need; next++) {
            const uint8_t *r = src + next * pitch;
            uint16_t *o = d->rows + (next % ENH_K) * w;
            for (int x = 0; x < w; x++) {
                int s = 0;
                for (int dx = -ENH_R; dx <= ENH_R; dx++) {
                    int xx = x + dx < 0 ? 0 : x + dx >= w ? w - 1 : x + dx;
                    s += r[xx];
                }
                o[x] = (uint16_t)s;
            }
        }
        const uint16_t *rr[ENH_K];
        for (int k = 0; k < ENH_K; k++) {
            int y = oy - ENH_R + k;
            y = y < 0 ? 0 : y >= h ? h - 1 : y;
            rr[k] = d->rows + (y % ENH_K) * w;
        }
        const uint8_t *s = src + oy * pitch;
        uint8_t *o = dst + oy * w;
        for (int x = 0; x < w; x++) {
            int sum = 0;
            for (int k = 0; k < ENH_K; k++) sum += rr[k][x];
            int v = s[x] + amount * (s[x] - sum / (ENH_K * ENH_K));
            o[x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    }

    unsigned hist[256] = {0};
    for (int i = 0; i < w * h; i++) hist[dst[i]]++;
    unsigned cut = (unsigned)(w * h) / 100, acc = 0;
    int lo = 0, hi = 255;
    while (lo < 255 && (acc += hist[lo]) <= cut) lo++;
    acc = 0;
    while (hi > 0 && (acc += hist[hi]) <= cut) hi--;
    if (hi <= lo) return;
    uint8_t lut[256];
    for (int v = 0; v < 256; v++) {
        int t = (v - lo) * 255 / (hi - lo);
        lut[v] = (uint8_t)(t < 0 ? 0 : t > 255 ? 255 : t);
    }
    for (int i = 0; i < w * h; i++) dst[i] = lut[dst[i]];
}

qr_decoder *qr_decoder_new(int width, int height) {
    qr_decoder *d = malloc(sizeof *d);
    if (!d) return NULL;
    d->q = quirc_new();
    d->rows = malloc(sizeof(uint16_t) * (2 * ENH_RMAX + 1) * (size_t)width);
    if (!d->q || !d->rows || quirc_resize(d->q, width, height) < 0) {
        if (d->q) quirc_destroy(d->q);
        free(d->rows);
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
    free(d->rows);
    free(d);
}

// One pass of quirc over the image already in its buffer.
static int decode_pass(qr_decoder *d, char *payload, int payload_size, qr_stats *st) {
    quirc_end(d->q);
    int n = quirc_count(d->q);
    if (n > st->codes) st->codes = n;
    if (d->q->num_capstones > st->capstones) {
        st->capstones = d->q->num_capstones;
        for (int i = 0; i < st->capstones && i < 4; i++) {
            st->corner_x[i] = d->q->capstones[i].center.x;
            st->corner_y[i] = d->q->capstones[i].center.y;
        }
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

int qr_decode(qr_decoder *d, const uint8_t *luma, int pitch, char *payload, int payload_size, qr_stats *st) {
    qr_stats local;
    if (!st) st = &local;
    memset(st, 0, sizeof *st);

    int w, h;
    uint8_t *img = quirc_begin(d->q, &w, &h);
    for (int y = 0; y < h; y++) memcpy(img + y * w, luma + y * pitch, (size_t)w);
    int n = decode_pass(d, payload, payload_size, st);
    if (n > 0 || st->capstones == 0) return n;

    // Part of a code was seen: try again on sharpened, contrast-stretched copies, a few settings
    // in turn (each costs one more decode, so only on such frames). Order: the ones that decoded
    // the real 2026-11-02 frame in most variations first.
    static const struct { int r, amount; } settings[] = {{3, 2}, {4, 2}, {2, 2}, {3, 3}};
    for (unsigned i = 0; i < sizeof settings / sizeof settings[0]; i++) {
        img = quirc_begin(d->q, &w, &h);
        enhance(d, luma, pitch, img, settings[i].r, settings[i].amount);
        st->enhanced = (int)i + 1;
        n = decode_pass(d, payload, payload_size, st);
        if (n > 0) return n;
    }
    return 0;
}
