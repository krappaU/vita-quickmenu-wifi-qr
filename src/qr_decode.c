// SPDX-License-Identifier: MIT
#include "qr_decode.h"

#include <stdlib.h>
#include <string.h>

#include "quirc.h"
#include "quirc_internal.h" // num_capstones: no public accessor

struct qr_decoder {
    struct quirc *q;
    int w, h;
    uint16_t *rows;    // ENH_RMAX*2+1 rows of horizontal box sums, for enhance()
    uint32_t *colsum;  // running vertical sums of those, one per column
    uint8_t *frame;    // the current frame, copied once into cached memory (see qr_decode)
    unsigned turn;     // which enhance setting the next frame tries
};

#define ENH_RMAX 4

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// Within the rectangle [x0,x1) x [y0,y1) of dst (the rest is left as it is): a (2r+1)^2 box blur
// of src, then dst = src + amount * (src - blur), clamped (amount > 0 sharpens: an unsharp mask;
// -1 gives the blur itself), then a contrast stretch of the 1st..99th percentile to 0..255.
// The Vita's fixed-focus back camera gives soft, dim pictures of a code held close: a real frame
// of 2026-11-02 with all three corner squares found decoded only after sharpening and the stretch,
// and only for some settings; a phone screen adds moire, which the blur setting removes.
// Running sums and a multiply instead of a divide: the Cortex-A9 has no integer divide, and the
// first version, with both per pixel over the whole frame, took ~0.5 s a pass on the console.
static void enhance(qr_decoder *d, const uint8_t *src, uint8_t *dst, int r, int amount, int x0, int y0, int x1,
                    int y1) {
    const int w = d->w, h = d->h, K = 2 * r + 1;
    const int rw = x1 - x0;
    const uint32_t inv = (65536u + (unsigned)(K * K) / 2) / (unsigned)(K * K); // 1/K^2 in 16.16
    int computed = -1; // highest source row whose horizontal sums are in the ring

    // Horizontal sums of row y over the rectangle's columns, edges clamped to the image.
    #define HROW(y) (d->rows + ((y) % K) * w)
    #define COMPUTE(yy) do { \
        const uint8_t *sr = src + (yy) * w; \
        uint16_t *o = HROW(yy); \
        int acc = 0; \
        for (int k = -r; k <= r; k++) acc += sr[clampi(x0 + k, 0, w - 1)]; \
        for (int x = x0; x < x1; x++) { \
            o[x] = (uint16_t)acc; \
            acc += sr[clampi(x + r + 1, 0, w - 1)] - sr[clampi(x - r, 0, w - 1)]; \
        } \
        computed = (yy); \
    } while (0)

    // Column sums for the first output row: rows y0-r .. y0+r, clamped.
    for (int x = x0; x < x1; x++) d->colsum[x] = 0;
    for (int k = -r; k <= r; k++) {
        int yy = clampi(y0 + k, 0, h - 1);
        if (yy > computed) COMPUTE(yy);
    }
    // (Rows may repeat at the top edge; recompute the sums from the ring by clamped index.)
    for (int k = -r; k <= r; k++) {
        const uint16_t *hr = HROW(clampi(y0 + k, 0, h - 1));
        for (int x = x0; x < x1; x++) d->colsum[x] += hr[x];
    }

    unsigned hist[256] = {0};
    for (int y = y0; y < y1; y++) {
        const uint8_t *sr = src + y * w;
        uint8_t *o = dst + y * w;
        for (int x = x0; x < x1; x++) {
            int blur = (int)((d->colsum[x] * inv) >> 16);
            int v = clampi(sr[x] + amount * (sr[x] - blur), 0, 255);
            o[x] = (uint8_t)v;
            hist[v]++;
        }
        if (y + 1 < y1) { // slide the window down: drop row y-r, add row y+r+1 (clamped)
            int out = clampi(y - r, 0, h - 1), in = clampi(y + r + 1, 0, h - 1);
            const uint16_t *ho = HROW(out);
            for (int x = x0; x < x1; x++) d->colsum[x] -= ho[x];
            if (in > computed) COMPUTE(in); // reuses the slot of row in-K = y-r: read above first
            const uint16_t *hi = HROW(in);
            for (int x = x0; x < x1; x++) d->colsum[x] += hi[x];
        }
    }
    #undef COMPUTE
    #undef HROW

    unsigned n = (unsigned)(rw * (y1 - y0)), cut = n / 100, acc = 0;
    int lo = 0, hi = 255;
    while (lo < 255 && (acc += hist[lo]) <= cut) lo++;
    acc = 0;
    while (hi > 0 && (acc += hist[hi]) <= cut) hi--;
    if (hi <= lo) return;
    uint8_t lut[256];
    const int scale = (255 << 16) / (hi - lo);
    for (int v = 0; v < 256; v++) lut[v] = (uint8_t)clampi(((v - lo) * scale) >> 16, 0, 255);
    for (int y = y0; y < y1; y++) {
        uint8_t *o = dst + y * w;
        for (int x = x0; x < x1; x++) o[x] = lut[o[x]];
    }
}

qr_decoder *qr_decoder_new(int width, int height) {
    qr_decoder *d = malloc(sizeof *d);
    if (!d) return NULL;
    d->q = quirc_new();
    d->rows = malloc(sizeof(uint16_t) * (2 * ENH_RMAX + 1) * (size_t)width);
    d->colsum = malloc(sizeof(uint32_t) * (size_t)width);
    d->frame = malloc((size_t)width * height);
    d->turn = 0;
    if (!d->q || !d->rows || !d->colsum || !d->frame || quirc_resize(d->q, width, height) < 0) {
        if (d->q) quirc_destroy(d->q);
        free(d->rows);
        free(d->colsum);
        free(d->frame);
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
    free(d->colsum);
    free(d->frame);
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

    // One block copy out of the camera's buffer, then everything reads the copy: the camera's
    // memory is uncached, and reading it pixel by pixel made each sharpened pass take ~1 s on the
    // console (2026-11-02) instead of a few ms.
    int w = d->w, h = d->h;
    for (int y = 0; y < h; y++) memcpy(d->frame + y * w, luma + y * pitch, (size_t)w);
    uint8_t *img = quirc_begin(d->q, &w, &h);
    memcpy(img, d->frame, (size_t)w * h);
    int n = decode_pass(d, payload, payload_size, st);
    if (n > 0 || st->capstones == 0) return n;

    // Part of a code was seen: try again on an enhanced copy (see enhance), one setting per
    // frame in turn: while the code is held in view, consecutive frames try them all, at the cost
    // of one extra pass each. Ordered by what decoded the real frames of 2026-11-02; the blur is
    // for the moire of a phone screen.
    static const struct { int r, amount; } settings[] = {{3, 2}, {4, 2}, {1, -1}, {2, 2}, {3, 3}};
    const unsigned nsettings = sizeof settings / sizeof settings[0];
    unsigned k = d->turn++ % nsettings;

    // Only around the corner squares when all three were found: the code is near them.
    int x0 = 0, y0 = 0, x1 = w, y1 = h;
    if (d->q->num_capstones >= 3) {
        int minx = w, miny = h, maxx = 0, maxy = 0;
        for (int i = 0; i < d->q->num_capstones; i++)
            for (int c = 0; c < 4; c++) {
                const struct quirc_point *pt = &d->q->capstones[i].corners[c];
                if (pt->x < minx) minx = pt->x;
                if (pt->x > maxx) maxx = pt->x;
                if (pt->y < miny) miny = pt->y;
                if (pt->y > maxy) maxy = pt->y;
            }
        // The fourth corner of a rotated code can lie outside the corner squares' box: add 40%.
        int mx = (maxx - minx) * 2 / 5 + 16, my = (maxy - miny) * 2 / 5 + 16;
        x0 = clampi(minx - mx, 0, w), x1 = clampi(maxx + mx, 0, w);
        y0 = clampi(miny - my, 0, h), y1 = clampi(maxy + my, 0, h);
        if (x1 - x0 < 16 || y1 - y0 < 16) x0 = 0, y0 = 0, x1 = w, y1 = h;
    }
    img = quirc_begin(d->q, &w, &h);
    memcpy(img, d->frame, (size_t)w * h);
    enhance(d, d->frame, img, settings[k].r, settings[k].amount, x0, y0, x1, y1);
    st->enhanced = (int)k + 1;
    return decode_pass(d, payload, payload_size, st);
}

const uint8_t *qr_decoder_frame(const qr_decoder *d) { return d->frame; }
