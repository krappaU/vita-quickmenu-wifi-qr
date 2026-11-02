// SPDX-License-Identifier: MIT
#ifndef QUIRC_EXT_H
#define QUIRC_EXT_H

struct quirc;

// After quirc_end(): if corner squares were found but none grouped into a code, try grouping
// them by their centres alone (see quirc_ext.c). Returns the number of codes then found.
int quirc_group_by_centres(struct quirc *q);

#endif
