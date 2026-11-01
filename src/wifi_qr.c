// SPDX-License-Identifier: MIT
#include "wifi_qr.h"

#include <string.h>

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int ieq(const char *a, const char *b) {
    while (*a && *b && lower((unsigned char)*a) == lower((unsigned char)*b)) a++, b++;
    return *a == 0 && *b == 0;
}

static int is_hex(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
    }
    return 1;
}

static int is_printable_ascii(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7e) return 0;
    return 1;
}

// Reads one field value up to an unescaped ';' into out (unescaping "\;", "\,", "\:", "\\", "\"").
// Returns the number of input bytes used, or -1 if the value does not fit.
static long read_value(const char *p, const char *end, char *out, size_t cap, size_t *outlen) {
    const char *s = p;
    size_t n = 0;
    while (p < end && *p != ';') {
        char c = *p++;
        if (c == '\\' && p < end) c = *p++;
        if (n + 1 >= cap) return -1;
        out[n++] = c;
    }
    out[n] = 0;
    *outlen = n;
    return (long)(p - s);
}

// Old generators wrap values in double quotes; Android and iOS don't. Strip one pair.
static void unquote(char *v, size_t *n) {
    if (*n >= 2 && v[0] == '"' && v[*n - 1] == '"') {
        memmove(v, v + 1, *n - 2);
        *n -= 2;
        v[*n] = 0;
    }
}

wifi_qr_result wifi_qr_parse(const char *text, size_t len, wifi_cred *out) {
    memset(out, 0, sizeof *out);
    if (len < 5 || !(lower(text[0]) == 'w' && lower(text[1]) == 'i' && lower(text[2]) == 'f' &&
                     lower(text[3]) == 'i' && text[4] == ':'))
        return WIFI_QR_NOT_WIFI;

    const char *p = text + 5, *end = text + len;
    char type[32] = "", val[128];
    int have_ssid = 0, have_type = 0;
    size_t pass_len = 0;

    while (p < end) {
        if (*p == ';') { p++; continue; }      // the closing ";;" and empty fields
        if (*p == '\r' || *p == '\n' || *p == ' ') { p++; continue; }
        const char *colon = memchr(p, ':', (size_t)(end - p));
        if (!colon) return WIFI_QR_BAD_FORMAT;
        size_t keylen = (size_t)(colon - p);
        char key = keylen == 1 ? (char)lower((unsigned char)*p) : 0;
        p = colon + 1;
        size_t n;
        long used = read_value(p, end, val, sizeof val, &n);
        if (used < 0) {
            if (key == 's') return WIFI_QR_SSID_TOO_LONG;
            if (key == 'p') return WIFI_QR_BAD_PASSWORD;
            return WIFI_QR_BAD_FORMAT;
        }
        p += used;
        unquote(val, &n);
        switch (key) {
        case 's':
            if (n > WIFI_SSID_MAX) return WIFI_QR_SSID_TOO_LONG;
            memcpy(out->ssid, val, n + 1);
            have_ssid = n > 0;
            break;
        case 'p':
            if (n > WIFI_PASS_MAX) return WIFI_QR_BAD_PASSWORD;
            memcpy(out->password, val, n + 1);
            pass_len = n;
            break;
        case 't':
            if (n >= sizeof type) return WIFI_QR_UNKNOWN_SEC;
            memcpy(type, val, n + 1);
            have_type = 1;
            break;
        case 'h':
            out->hidden = ieq(val, "true");
            break;
        default:
            break; // R: (transition disable), K:, E:, A:, I:... are ignored
        }
    }
    if (!have_ssid) return WIFI_QR_NO_SSID;

    if (!have_type || type[0] == 0 || ieq(type, "nopass")) {
        out->sec = WIFI_SEC_OPEN;
        out->password[0] = 0;
        return WIFI_QR_OK;
    }
    if (ieq(type, "WEP")) {
        out->sec = WIFI_SEC_WEP;
        // 64/128-bit WEP: 5 or 13 characters, or 10 or 26 hex digits
        if (pass_len == 5 || pass_len == 13) return is_printable_ascii(out->password, pass_len) ? WIFI_QR_OK : WIFI_QR_BAD_PASSWORD;
        if (pass_len == 10 || pass_len == 26) return is_hex(out->password, pass_len) ? WIFI_QR_OK : WIFI_QR_BAD_PASSWORD;
        return WIFI_QR_BAD_PASSWORD;
    }
    if (ieq(type, "WPA") || ieq(type, "WPA2") || ieq(type, "WPA/WPA2") || ieq(type, "WPA2/WPA3") ||
        ieq(type, "WPA-PSK") || ieq(type, "WPA2-PSK")) {
        // WPA2/WPA3 transition networks still accept WPA2, which the Vita can use.
        out->sec = WIFI_SEC_WPA;
        if (pass_len >= 8 && pass_len <= 63) return is_printable_ascii(out->password, pass_len) ? WIFI_QR_OK : WIFI_QR_BAD_PASSWORD;
        if (pass_len == 64) return is_hex(out->password, pass_len) ? WIFI_QR_OK : WIFI_QR_BAD_PASSWORD;
        return WIFI_QR_BAD_PASSWORD;
    }
    if (ieq(type, "SAE") || ieq(type, "WPA3")) return WIFI_QR_WPA3;
    if (ieq(type, "WPA2-EAP") || ieq(type, "EAP") || ieq(type, "WPA-EAP") || ieq(type, "WPA3-EAP"))
        return WIFI_QR_ENTERPRISE;
    return WIFI_QR_UNKNOWN_SEC;
}

const char *wifi_qr_result_str(wifi_qr_result r) {
    switch (r) {
    case WIFI_QR_OK: return "OK";
    case WIFI_QR_NOT_WIFI: return "Not a Wi-Fi QR code";
    case WIFI_QR_BAD_FORMAT: return "Unreadable Wi-Fi QR code";
    case WIFI_QR_NO_SSID: return "QR code has no network name";
    case WIFI_QR_SSID_TOO_LONG: return "Network name is too long";
    case WIFI_QR_BAD_PASSWORD: return "Password is not valid for this network type";
    case WIFI_QR_WPA3: return "WPA3-only network: the Vita can't join it";
    case WIFI_QR_ENTERPRISE: return "Enterprise (802.1X) network: the Vita can't join it";
    case WIFI_QR_UNKNOWN_SEC: return "Unknown security type";
    }
    return "?";
}

const char *wifi_sec_str(wifi_sec s) {
    switch (s) {
    case WIFI_SEC_OPEN: return "open";
    case WIFI_SEC_WEP: return "WEP";
    case WIFI_SEC_WPA: return "WPA/WPA2";
    }
    return "?";
}
