// SPDX-License-Identifier: MIT
#include <stdlib.h>

#include "../src/viewfinder.h"
#include "check.h"

static int count(const char *s, const char *needle) {
    int n = 0;
    for (const char *p = strstr(s, needle); p; p = strstr(p + 1, needle)) n++;
    return n;
}

int main(void) {
    const int w = 640, h = 480;
    unsigned char *img = malloc(w * h);
    // dim left half, bright right half: the range is the image's own, so both show
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) img[y * w + x] = x < w / 2 ? 10 : 60;
    char out[VIEW_TEXT_MAX];
    int corners_x[2] = {600, 10}, corners_y[2] = {10, 470};
    int len = viewfinder_render(img, w, h, w, corners_x, corners_y, 2, out, sizeof out);
    CHECK_INT(len, (int)strlen(out));
    CHECK_TRUE(len < 0x400);                                 // fits a QuickMenuReborn label
    CHECK_INT(count(out, "\n"), VIEW_ROWS - 1);
    CHECK_INT(count(out, "\xe2\x97\x8e"), 2);                // two corner markers
    CHECK_INT(count(out, "\xe2\x96\xa0"), VIEW_ROWS * VIEW_COLS / 2 - 1); // dark half, minus a marker
    CHECK_INT(count(out, "\xe3\x80\x80"), VIEW_ROWS * VIEW_COLS / 2 - 1); // light half, minus a marker
    // first line: 12 dark cells, then light ones, the corner marker in the last cell but one
    CHECK_TRUE(strncmp(out, "\xe2\x96\xa0", 3) == 0);
    CHECK_TRUE(strncmp(out + 22 * 3, "\xe2\x97\x8e", 3) == 0);
    // a flat image doesn't divide by zero, and a small buffer is never overrun
    memset(img, 128, w * h);
    char small[40];
    len = viewfinder_render(img, w, h, w, NULL, NULL, 0, small, sizeof small);
    CHECK_TRUE(len < (int)sizeof small);
    CHECK_INT(len, (int)strlen(small));
    free(img);
    return CHECK_DONE("viewfinder");
}
