// SPDX-License-Identifier: MIT
// The core plugin's API (wifiqr.suprx, loaded into SceShell by taiHEN), for front ends in the same
// process such as the Quick Menu button (wifiqr_qm.suprx). Front ends look the functions up at
// run time with taiGetModuleExportFunc, so they work whatever order the plugins load in, and
// can tell the user when the core isn't installed.
#ifndef WIFIQR_API_H
#define WIFIQR_API_H

#define WIFIQR_MODULE "WifiQr"
#define WIFIQR_LIB_NID 0x57514331u
// Function NIDs, derived from the names by vita-elf-export; the build checks they still match
// (CMakeLists.txt, check_nids).
#define WIFIQR_NID_SCAN 0x4E7E790Au
#define WIFIQR_NID_CANCEL 0x656D8F50u
#define WIFIQR_NID_ISBUSY 0xCF961134u
#define WIFIQR_NID_SELFTEST 0x4D6EA946u
#define WIFIQR_NID_OPENWIFI 0xFB224694u

#define WIFIQR_ERROR_BUSY ((int)0x80010010) // a scan is already running
#define WIFIQR_ERROR_POPUP_NOT_LOADED ((int)0x80010002) // the Wi-Fi Settings popup isn't loaded

// Callbacks from the core's worker thread. The structure is copied; ud is passed back as is.
typedef struct {
    unsigned int size;                              // sizeof(WifiQrListener)
    void (*status)(const char *text, void *ud);     // a new status line
    void (*view)(const char *text, void *ud);       // text viewfinder; "" when not scanning. May be NULL
    void (*busy)(int busy, void *ud);               // 1 when a scan starts, 0 when it has finished
    void *ud;
} WifiQrListener;

// Starts scanning, then saving and connecting, on the core's own thread. Returns 0, or
// WIFIQR_ERROR_BUSY, or the error that kept the thread from starting.
typedef int (*wifiqrScan_fn)(const WifiQrListener *listener);
// Asks the running scan or connection to stop. Returns 0.
typedef int (*wifiqrCancel_fn)(void);
// 1 while a scan, save or connection is running.
typedef int (*wifiqrIsBusy_fn)(void);
// Diagnostic (src/selftest.c): calls one function of each library the core imports, reporting
// each step through report(text, ud) before and after the call.
typedef void (*wifiqr_report_fn)(const char *text, void *ud);
typedef int (*wifiqrSelfTest_fn)(wifiqr_report_fn report, void *ud);
// Opens the system's Wi-Fi Settings popup (src/popup.cpp). Call it on the UI's main thread (a
// widget's event handler). Returns 0, or the popup's error, or WIFIQR_ERROR_POPUP_NOT_LOADED.
typedef int (*wifiqrOpenWifiSettings_fn)(void);

#endif
