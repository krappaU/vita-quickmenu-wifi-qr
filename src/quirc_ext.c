// SPDX-License-Identifier: MIT
// quirc's identify.c, compiled here so a fallback can use its internal functions.
//
// quirc groups corner squares (capstones) into codes using each square's own four corners to
// find its orientation. Android's Wi-Fi share screen draws the corner squares with rounded
// corners, so the "corners" quirc finds lie anywhere along the curves, the orientations disagree
// and the squares are never grouped: real frames of 2026-11-02, sharp and square-on, had three
// capstones and no grid (ZXing read them). The fallback uses only the squares' centres: one
// square at a right angle to two others at about equal distances gives the code's axes; each
// square then gets corners rebuilt along those axes, sized from its pixel area (ring and stone
// cover 24 + 9 of the 7x7 modules), and quirc's own record_qr_grid takes over (alignment pattern,
// perspective fitting). The area underestimates the module size on a bright screen (the glow eats
// into dark modules: 4.7 px estimated for 5.6 real on 2026-11-02, so version 6 was read instead of
// 5), so the grid sizes around the estimate are each recorded as a candidate; wrong ones just
// fail to decode.
#include "identify.c"

#include "quirc_ext.h"

static quirc_float_t module_size(const struct quirc *q, const struct quirc_capstone *cap) {
    int px = q->regions[cap->ring].count + q->regions[cap->stone].count;
    return sqrt((quirc_float_t)px / (quirc_float_t)33.0);
}

static void rebuild_corners(struct quirc_capstone *cap, quirc_float_t half, quirc_float_t e1x, quirc_float_t e1y,
                            quirc_float_t e2x, quirc_float_t e2y) {
    static const int sign[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}; // cyclic
    for (int k = 0; k < 4; k++) {
        quirc_float_t x = cap->center.x + half * (sign[k][0] * e1x + sign[k][1] * e2x);
        quirc_float_t y = cap->center.y + half * (sign[k][0] * e1y + sign[k][1] * e2y);
        cap->corners[k].x = (int)rint(x);
        cap->corners[k].y = (int)rint(y);
    }
    perspective_setup(cap->c, cap->corners, 7.0, 7.0);
}

int quirc_group_by_centres(struct quirc *q) {
    if (q->num_grids > 0 || q->num_capstones < 3) return q->num_grids;
    const int n = q->num_capstones;
    for (int b = 0; b < n; b++) {          // b: the corner square at the right angle
        for (int a = 0; a < n; a++) {
            for (int c = a + 1; c < n; c++) {
                if (a == b || c == b) continue;
                struct quirc_capstone *A = &q->capstones[a], *B = &q->capstones[b], *C = &q->capstones[c];
                if (A->qr_grid >= 0 || B->qr_grid >= 0 || C->qr_grid >= 0) continue; // already tried
                quirc_float_t ax = A->center.x - B->center.x, ay = A->center.y - B->center.y;
                quirc_float_t cx = C->center.x - B->center.x, cy = C->center.y - B->center.y;
                quirc_float_t la = sqrt(ax * ax + ay * ay), lc = sqrt(cx * cx + cy * cy);
                if (la < 8 || lc < 8) continue;
                quirc_float_t ratio = la / lc;
                if (ratio < (quirc_float_t)0.8 || ratio > (quirc_float_t)1.25) continue; // equal legs
                if (fabs((ax * cx + ay * cy) / (la * lc)) > (quirc_float_t)0.2) continue; // ~90 degrees
                // The three squares are the same size in a code.
                quirc_float_t ma = module_size(q, A), mb = module_size(q, B), mc = module_size(q, C);
                quirc_float_t lo = ma < mb ? ma : mb, hi = ma > mb ? ma : mb;
                lo = mc < lo ? mc : lo, hi = mc > hi ? mc : hi;
                if (hi > lo * (quirc_float_t)1.4) continue;
                // ...and far apart compared with a module: centres are at least 14 modules apart.
                if (la < 10 * lo) continue;

                quirc_float_t e1x = ax / la, e1y = ay / la, e2x = cx / lc, e2y = cy / lc;
                // Corner square centres are (size - 7) modules apart.
                quirc_float_t dist = (la + lc) / 2, m = (ma + mb + mc) / 3;
                int est = 4 * (int)rint((dist / m + 7 - 17) / 4) + 17;
                static const int offsets[] = {0, -4, 4, -8, 8}; // the glow makes the estimate too big
                for (unsigned o = 0; o < sizeof offsets / sizeof offsets[0]; o++) {
                    int size = est + offsets[o];
                    if (size < 21 || size > 177) continue;
                    quirc_float_t half = (quirc_float_t)3.5 * dist / (quirc_float_t)(size - 7);
                    rebuild_corners(A, half, e1x, e1y, e2x, e2y);
                    rebuild_corners(B, half, e1x, e1y, e2x, e2y);
                    rebuild_corners(C, half, e1x, e1y, e2x, e2y);
                    int idx = q->num_grids;
                    record_qr_grid(q, a, b, c);
                    if (q->num_grids > idx && q->grids[idx].grid_size != size) {
                        q->grids[idx].grid_size = size; // the timing estimate can be off by a version
                        setup_qr_perspective(q, idx);
                    }
                    if (q->num_grids >= QUIRC_MAX_GRIDS) return q->num_grids;
                }
            }
        }
    }
    return q->num_grids;
}
