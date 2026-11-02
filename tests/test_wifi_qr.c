// SPDX-License-Identifier: MIT
#include "../src/wifi_qr.h"
#include "check.h"

static wifi_qr_result parse(const char *s, wifi_cred *c) { return wifi_qr_parse(s, strlen(s), c); }

int main(void) {
    wifi_cred c;

    // What Android's Settings > Wi-Fi > Share produces
    CHECK_INT(parse("WIFI:S:Hotel Guest;T:WPA;P:welcome123;H:false;;", &c), WIFI_QR_OK);
    CHECK_STR(c.ssid, "Hotel Guest");
    CHECK_STR(c.password, "welcome123");
    CHECK_INT(c.sec, WIFI_SEC_WPA);
    CHECK_INT(c.hidden, 0);

    // Fields in any order, lower-case prefix, hidden network
    CHECK_INT(parse("wifi:T:WPA;P:secret-pass;S:Home;H:true;;", &c), WIFI_QR_OK);
    CHECK_STR(c.ssid, "Home");
    CHECK_INT(c.hidden, 1);

    // Escaped special characters in the name and password
    CHECK_INT(parse("WIFI:T:WPA;S:My\\;Net\\:5\\\\x;P:pa\\;ss\\,wo\\\"rd;;", &c), WIFI_QR_OK);
    CHECK_STR(c.ssid, "My;Net:5\\x");
    CHECK_STR(c.password, "pa;ss,wo\"rd");

    // Quoted values (older generators)
    CHECK_INT(parse("WIFI:S:\"Cafe\";T:WPA;P:\"12345678\";;", &c), WIFI_QR_OK);
    CHECK_STR(c.ssid, "Cafe");
    CHECK_STR(c.password, "12345678");

    // UTF-8 network names pass through as bytes
    CHECK_INT(parse("WIFI:S:Caf\xc3\xa9 Wi-Fi;T:WPA;P:12345678;;", &c), WIFI_QR_OK);
    CHECK_STR(c.ssid, "Caf\xc3\xa9 Wi-Fi");

    // Open networks: nopass, empty type or no type at all
    CHECK_INT(parse("WIFI:S:Airport;T:nopass;;", &c), WIFI_QR_OK);
    CHECK_INT(c.sec, WIFI_SEC_OPEN);
    CHECK_STR(c.password, "");
    CHECK_INT(parse("WIFI:S:Airport;;", &c), WIFI_QR_OK);
    CHECK_INT(c.sec, WIFI_SEC_OPEN);
    CHECK_INT(parse("WIFI:S:Airport;T:;P:;;", &c), WIFI_QR_OK);
    CHECK_INT(c.sec, WIFI_SEC_OPEN);

    // Android adds R: for WPA2/WPA3 transition networks; unknown fields are ignored
    CHECK_INT(parse("WIFI:S:Mesh;T:WPA;R:1;P:abcdefgh;K:xyz;;", &c), WIFI_QR_OK);
    CHECK_INT(c.sec, WIFI_SEC_WPA);
    CHECK_INT(parse("WIFI:S:Mesh;T:WPA2;P:abcdefgh;;", &c), WIFI_QR_OK);

    // WPA password lengths: 8..63 characters, or 64 hex digits
    CHECK_INT(parse("WIFI:S:x;T:WPA;P:1234567;;", &c), WIFI_QR_BAD_PASSWORD);
    CHECK_INT(parse("WIFI:S:x;T:WPA;P:123456789012345678901234567890123456789012345678901234567890123;;", &c), WIFI_QR_OK);
    CHECK_INT(parse("WIFI:S:x;T:WPA;P:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef;;", &c), WIFI_QR_OK);
    CHECK_INT(parse("WIFI:S:x;T:WPA;P:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg;;", &c), WIFI_QR_BAD_PASSWORD);
    CHECK_INT(parse("WIFI:S:x;T:WPA;P:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0;;", &c), WIFI_QR_BAD_PASSWORD);
    CHECK_INT(parse("WIFI:S:x;T:WPA;;", &c), WIFI_QR_BAD_PASSWORD);

    // WEP: 5/13 characters or 10/26 hex digits
    CHECK_INT(parse("WIFI:S:old;T:WEP;P:abcde;;", &c), WIFI_QR_OK);
    CHECK_INT(c.sec, WIFI_SEC_WEP);
    CHECK_INT(parse("WIFI:S:old;T:WEP;P:0123456789;;", &c), WIFI_QR_OK);
    CHECK_INT(parse("WIFI:S:old;T:WEP;P:0123456789abcdef0123456789;;", &c), WIFI_QR_OK);
    CHECK_INT(parse("WIFI:S:old;T:WEP;P:abcdef;;", &c), WIFI_QR_BAD_PASSWORD);
    CHECK_INT(parse("WIFI:S:old;T:WEP;P:012345678g;;", &c), WIFI_QR_BAD_PASSWORD);

    // WPA3 (SAE): Android writes it for WPA2/WPA3 transition networks too, so it's tried as WPA
    CHECK_INT(parse("WIFI:S:A_SUS_NETWORK;T:SAE;P:abcdefgh;H:false;;", &c), WIFI_QR_OK);
    CHECK_INT(c.sec, WIFI_SEC_WPA);
    CHECK_INT(c.wpa3, 1);
    CHECK_INT(c.wpa3_only, 0);
    CHECK_STR(c.password, "abcdefgh");
    CHECK_INT(parse("WIFI:S:new;T:SAE;R:1;P:abcdefgh;;", &c), WIFI_QR_OK); // transition disable
    CHECK_INT(c.wpa3_only, 1);
    CHECK_INT(parse("WIFI:S:new;T:WPA;R:1;P:abcdefgh;;", &c), WIFI_QR_OK); // R without SAE: ignored
    CHECK_INT(c.wpa3, 0);
    CHECK_INT(c.wpa3_only, 0);
    CHECK_INT(parse("WIFI:S:new;T:SAE;P:1234567;;", &c), WIFI_QR_BAD_PASSWORD);

    // Networks the Vita can't join
    CHECK_INT(parse("WIFI:S:work;T:WPA2-EAP;E:PEAP;I:me;P:abcdefgh;;", &c), WIFI_QR_ENTERPRISE);
    CHECK_INT(parse("WIFI:S:x;T:FOO;P:abcdefgh;;", &c), WIFI_QR_UNKNOWN_SEC);

    // Limits and malformed input
    CHECK_INT(parse("WIFI:S:123456789012345678901234567890AB;T:WPA;P:12345678;;", &c), WIFI_QR_OK);
    CHECK_INT(parse("WIFI:S:123456789012345678901234567890ABC;T:WPA;P:12345678;;", &c), WIFI_QR_SSID_TOO_LONG);
    CHECK_INT(parse("WIFI:T:WPA;P:12345678;;", &c), WIFI_QR_NO_SSID);
    CHECK_INT(parse("WIFI:S:;T:WPA;P:12345678;;", &c), WIFI_QR_NO_SSID);
    CHECK_INT(parse("https://example.com", &c), WIFI_QR_NOT_WIFI);
    CHECK_INT(parse("WIFI", &c), WIFI_QR_NOT_WIFI);
    CHECK_INT(parse("WIFI:garbage", &c), WIFI_QR_BAD_FORMAT);
    CHECK_INT(parse("WIFI:S:x;T:WPA;P:\x01\x02\x03\x04\x05\x06\x07\x08;;", &c), WIFI_QR_BAD_PASSWORD);
    // A trailing backslash or a missing final ";;" still parses
    CHECK_INT(parse("WIFI:S:Net;T:WPA;P:12345678", &c), WIFI_QR_OK);
    CHECK_STR(c.password, "12345678");
    CHECK_INT(parse("WIFI:S:Net\\", &c), WIFI_QR_OK);
    CHECK_STR(c.ssid, "Net\\");
    // An overlong value of another field is rejected rather than overflowing
    char big[400] = "WIFI:S:x;Z:";
    memset(big + strlen(big), 'a', 300);
    CHECK_INT(parse(big, &c), WIFI_QR_BAD_FORMAT);

    return CHECK_DONE("wifi_qr");
}
