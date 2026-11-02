// SPDX-License-Identifier: MIT
// The kernel helper's system calls, for the Quick Menu plugin (linked with weak stubs: without the
// kernel plugin installed, the calls fail and the text viewfinder is used instead).
#ifndef WIFIQR_KERNEL_H
#define WIFIQR_KERNEL_H

#define PREVIEW_W 320
#define PREVIEW_H 240

// Shows a PREVIEW_W x PREVIEW_H grayscale picture over the calling process's frames (SceShell's,
// so the Quick Menu) until wifiqrPreviewOff, or for 1 s after the last call. corners: up to 4
// x,y pairs in preview pixels, marked in red. Returns 0, or the error that stopped the
// display hook from being installed.
int wifiqrPreviewSet(const void *gray, int w, int h, const int *corners, int ncorners);
int wifiqrPreviewOff(void);

#endif
