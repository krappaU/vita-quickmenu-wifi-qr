// SPDX-License-Identifier: MIT
#include "viewfinder.h"

#include <string.h>

static const char LIGHT[] = "\xe3\x80\x80";  // U+3000 ideographic space
static const char MID[] = "\xe3\x83\xbb";    // U+30FB katakana middle dot
static const char DARK[] = "\xe2\x96\xa0";   // U+25A0 black square
static const char CORNER[] = "\xe2\x97\x8e"; // U+25CE bullseye

int viewfinder_render(const uint8_t *y, int w, int h, int pitch, const int *corners_x, const int *corners_y,
                      int corners, char *out, int out_size) {
    unsigned cell[VIEW_ROWS][VIEW_COLS];
    unsigned lo = 255, hi = 0;
    const int step = 4; // sample every 4th pixel each way: plenty for a cell of ~27x40 pixels
    for (int r = 0; r < VIEW_ROWS; r++) {
        for (int c = 0; c < VIEW_COLS; c++) {
            int x0 = c * w / VIEW_COLS, x1 = (c + 1) * w / VIEW_COLS;
            int y0 = r * h / VIEW_ROWS, y1 = (r + 1) * h / VIEW_ROWS;
            unsigned sum = 0, n = 0;
            for (int yy = y0; yy < y1; yy += step)
                for (int xx = x0; xx < x1; xx += step) sum += y[yy * pitch + xx], n++;
            unsigned v = n ? sum / n : 0;
            cell[r][c] = v;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
    }
    unsigned range = hi > lo ? hi - lo : 1;

    int len = 0;
    for (int r = 0; r < VIEW_ROWS; r++) {
        for (int c = 0; c < VIEW_COLS; c++) {
            const char *g;
            int marked = 0;
            for (int i = 0; i < corners; i++)
                if (corners_x[i] * VIEW_COLS / w == c && corners_y[i] * VIEW_ROWS / h == r) marked = 1;
            if (marked) {
                g = CORNER;
            } else {
                unsigned t = (cell[r][c] - lo) * 3 / range; // 0..3
                g = t >= 2 ? LIGHT : t == 1 ? MID : DARK;
            }
            if (len + 3 + 2 > out_size) goto done;
            memcpy(out + len, g, 3);
            len += 3;
        }
        if (r < VIEW_ROWS - 1) out[len++] = '\n';
    }
done:
    out[len] = 0;
    return len;
}
