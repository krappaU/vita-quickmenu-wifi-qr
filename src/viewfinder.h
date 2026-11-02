// SPDX-License-Identifier: MIT
// A coarse camera viewfinder drawn with text, for a QuickMenuReborn label: its images are loaded
// once per menu opening and never freed until it closes, so a picture per frame would fill the
// shell's memory, but labels can change freely. Full-width characters keep every cell the same
// width in the system's proportional font.
#ifndef VIEWFINDER_H
#define VIEWFINDER_H

#include <stdint.h>

#define VIEW_COLS 24
#define VIEW_ROWS 12
// QuickMenuReborn labels hold 0x400 bytes: 3 UTF-8 bytes per cell, a newline per row, the terminator.
#define VIEW_TEXT_MAX (VIEW_COLS * VIEW_ROWS * 3 + VIEW_ROWS + 1)

// Renders the luma image (w x h, one row every `pitch` bytes) into out as VIEW_ROWS lines of
// VIEW_COLS cells: light, mid or dark by the image's own range (so a dim picture still shows),
// and a marker where a QR corner square was found (corners_x/y in image pixels).
// Returns the length written.
int viewfinder_render(const uint8_t *y, int w, int h, int pitch, const int *corners_x, const int *corners_y,
                      int corners, char *out, int out_size);

#endif
