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

typedef struct {
    int width, height;        // 640x480 or 320x240
    int timeout_ms;
    volatile int *cancel;     // set to non-zero to stop
    // Called about once a second with whether a code is in view, for the status line.
    void (*progress)(int seconds_left, int code_in_view);
} scan_params;

// payload receives the decoded text; *camera_error the failing call's result for SCAN_CAMERA_ERROR.
scan_result scan_qr(const scan_params *p, char *payload, int payload_size, int *camera_error);

#endif
