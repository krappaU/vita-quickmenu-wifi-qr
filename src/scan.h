// SPDX-License-Identifier: MIT
// Reads frames from the back camera until a QR code decodes, the time runs out or it is cancelled.
#ifndef SCAN_H
#define SCAN_H

#include <stdint.h>

typedef enum {
    SCAN_OK = 0,
    SCAN_CANCELLED,
    SCAN_TIMEOUT,
    SCAN_NO_MEMORY,
    SCAN_CAMERA_ERROR,
} scan_result;

// What the last frames showed, for the status line (there is no camera preview).
typedef enum {
    SCAN_HINT_NOTHING = 0,
    SCAN_HINT_SCREEN,   // a bright area, probably the phone's screen
    SCAN_HINT_PARTIAL,  // one or two of the QR code's corner squares
    SCAN_HINT_CODE,     // a whole code: decoding
    SCAN_HINT_BLURRY,   // a whole code for 2 s without decoding: too close for the fixed focus
} scan_hint;

typedef struct {
    int width, height;        // 640x480 or 320x240
    int timeout_ms;
    volatile int *cancel;     // set to non-zero to stop
    int snapshots;            // save frames that didn't decode (they can show the password)
    // Called about 3 times a second with the current frame and where corner squares are, for
    // the text viewfinder. NULL: none.
    void (*viewfinder)(const uint8_t *luma, int w, int h, const int *corners_x, const int *corners_y, int corners);
    // Called with every frame, for the picture preview drawn by the kernel helper. NULL: none.
    void (*preview)(const uint8_t *luma, int w, int h, const int *corners_x, const int *corners_y, int corners);
    // Called twice a second with the most promising hint since the last call.
    void (*progress)(int seconds_left, scan_hint hint);
} scan_params;

// payload receives the decoded text; *camera_error the failing call's result for SCAN_CAMERA_ERROR.
scan_result scan_qr(const scan_params *p, char *payload, int payload_size, int *camera_error);

#endif
