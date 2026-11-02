// SPDX-License-Identifier: MIT
// Parser for Wi-Fi QR codes ("WIFI:T:WPA;S:name;P:password;;"), the format Android's
// Settings > Wi-Fi > Share and iOS use. Plain C with no system calls, so it is tested on the host.
#ifndef WIFI_QR_H
#define WIFI_QR_H

#include <stddef.h>

#define WIFI_SSID_MAX 32   // bytes; the Vita's registry field is 33 with the terminator
#define WIFI_PASS_MAX 64   // a 64-digit hex WPA key; the registry field is 65

typedef enum {
    WIFI_SEC_OPEN = 0,
    WIFI_SEC_WEP,
    WIFI_SEC_WPA, // WPA/WPA2 personal (PSK): what the Vita supports
} wifi_sec;

typedef struct {
    char ssid[WIFI_SSID_MAX + 1];
    char password[WIFI_PASS_MAX + 1];
    wifi_sec sec;
    int hidden;
    // The code said WPA3 (T:SAE). Android writes that for WPA2/WPA3 transition networks too, whose
    // password works over WPA2, so it's saved as WPA; this only explains a failed connection.
    int wpa3;
    int wpa3_only; // R:1, "transition disable": the network asks clients not to fall back to WPA2
} wifi_cred;

typedef enum {
    WIFI_QR_OK = 0,
    WIFI_QR_NOT_WIFI,        // some other QR code
    WIFI_QR_BAD_FORMAT,      // starts with WIFI: but cannot be parsed
    WIFI_QR_NO_SSID,
    WIFI_QR_SSID_TOO_LONG,
    WIFI_QR_BAD_PASSWORD,    // wrong length or characters for its security type
    WIFI_QR_ENTERPRISE,      // 802.1X (EAP): the Vita cannot join it
    WIFI_QR_UNKNOWN_SEC,
} wifi_qr_result;

wifi_qr_result wifi_qr_parse(const char *text, size_t len, wifi_cred *out);
const char *wifi_qr_result_str(wifi_qr_result r);
const char *wifi_sec_str(wifi_sec s);

#endif
