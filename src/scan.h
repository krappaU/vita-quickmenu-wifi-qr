// SPDX-License-Identifier: MIT
// Reads frames from the back camera until a QR code decodes, the time runs out or it is cancelled.
#ifndef SCAN_H
#define SCAN_H

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
} scan_hint;

typedef struct {
    int width, height;        // 640x480 or 320x240
    int timeout_ms;
    volatile int *cancel;     // set to non-zero to stop
    // Called twice a second with the most promising hint since the last call.
    void (*progress)(int seconds_left, scan_hint hint);
} scan_params;

// payload receives the decoded text; *camera_error the failing call's result for SCAN_CAMERA_ERROR.
scan_result scan_qr(const scan_params *p, char *payload, int payload_size, int *camera_error);

#endif
