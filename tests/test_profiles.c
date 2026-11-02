// SPDX-License-Identifier: MIT
#include "../src/profiles.h"
#include "../src/reg.h"
#include "check.h"
#include "reg_fake.h"

// Values made up for the tests, distinct from the system's (open 0, WPA 2), so the tests show
// whether a value was learned from a saved network or taken from the fallback.
#define SEC_OPEN 10
#define SEC_WEP 11
#define SEC_WPA 12

static void cat(char *out, int slot, const char *sub) { snprintf(out, 48, "/CONFIG/NET/%02d/%s", slot, sub); }

static void add(int slot, const char *ssid, int sec, const char *wpa, const char *wep, int serial, int ip_config, int proxy) {
    char c[48];
    cat(c, slot, "WIFI");
    reg_set_str(c, "ssid", ssid, 33);
    reg_set_int(c, "wifi_security", sec);
    reg_set_str(c, "wpa_key", wpa, 65);
    reg_set_str(c, "wep_key", wep, 27);
    cat(c, slot, "COMMON");
    reg_set_str(c, "conf_name", ssid, 65);
    reg_set_int(c, "conf_serial_no", serial);
    reg_set_int(c, "conf_flag", 1);
    reg_set_int(c, "device", 2);
    reg_set_int(c, "enable_auto_connect", 1);
    reg_set_int(c, "mtu", 1500);
    cat(c, slot, "IP");
    reg_set_int(c, "ip_config", ip_config);
    reg_set_str(c, "ip_address", ip_config ? "192.168.1.50" : "", 16);
    cat(c, slot, "APP");
    reg_set_int(c, "http_proxy_flag", proxy);
    reg_set_str(c, "http_proxy_server", proxy ? "proxy.example" : "", 256);
}

static int geti(int slot, const char *sub, const char *name) {
    char c[48];
    int v;
    cat(c, slot, sub);
    reg_get_int(c, name, &v);
    return v;
}

static const char *gets_(int slot, const char *sub, const char *name) {
    static char buf[4][256];
    static int n;
    char c[48];
    cat(c, slot, sub);
    char *b = buf[n++ % 4];
    reg_get_str(c, name, b, 256);
    return b;
}

static wifi_cred cred(const char *ssid, wifi_sec sec, const char *pass) {
    wifi_cred c;
    memset(&c, 0, sizeof c);
    strcpy(c.ssid, ssid);
    strcpy(c.password, pass);
    c.sec = sec;
    return c;
}

static void standard_setup(void) {
    reg_fake_reset();
    add(1, "Home", SEC_WPA, "homepass1", "", 1, 0, 0);
    add(2, "Office-static", SEC_WPA, "officepass", "", 2, 1, 0); // static IP: must not leak into a new profile
    add(3, "Cafe", SEC_OPEN, "", "", 5, 0, 0);
    add(5, "Old router", SEC_WEP, "", "abcde", 3, 0, 0);       // slot 4 is free
}

int main(void) {
    profiles_outcome o;
    wifi_cred c;

    // A new WPA network goes into the first free slot with the system's defaults for a new
    // network (not another slot's static IP or proxy) and the WPA value learned from saved ones
    standard_setup();
    c = cred("Hotel", WIFI_SEC_WPA, "welcome123");
    o = profiles_save(&c);
    CHECK_INT(o.result, PROFILES_ADDED);
    CHECK_INT(o.slot, 4);
    CHECK_INT(o.security_value, SEC_WPA);
    CHECK_STR(gets_(4, "WIFI", "ssid"), "Hotel");
    CHECK_STR(gets_(4, "COMMON", "conf_name"), "Hotel");
    CHECK_STR(gets_(4, "WIFI", "wpa_key"), "welcome123");
    CHECK_STR(gets_(4, "WIFI", "wep_key"), "");
    CHECK_INT(geti(4, "WIFI", "wifi_security"), SEC_WPA);
    CHECK_INT(geti(4, "COMMON", "conf_flag"), 1);
    CHECK_INT(geti(4, "COMMON", "conf_serial_no"), 0);
    CHECK_INT(geti(4, "COMMON", "enable_auto_connect"), 1);
    CHECK_INT(geti(4, "COMMON", "device"), 0);
    CHECK_INT(geti(4, "COMMON", "mtu"), 0);
    CHECK_INT(geti(4, "APP", "http_proxy_flag"), 0);
    CHECK_INT(geti(4, "APP", "http_proxy_port"), 8080);
    CHECK_STR(gets_(4, "APP", "http_proxy_server"), "");
    CHECK_INT(geti(4, "IP", "ip_config"), 0);
    CHECK_STR(gets_(4, "IP", "ip_address"), "");
    // the other slots are untouched
    CHECK_STR(gets_(1, "WIFI", "ssid"), "Home");
    CHECK_STR(gets_(1, "WIFI", "wpa_key"), "homepass1");
    CHECK_INT(geti(2, "IP", "ip_config"), 1);

    // Scanning it again changes nothing
    o = profiles_save(&c);
    CHECK_INT(o.result, PROFILES_UNCHANGED);
    CHECK_INT(o.slot, 4);

    // A new password for a saved network updates that slot only
    c = cred("Home", WIFI_SEC_WPA, "newpassword");
    o = profiles_save(&c);
    CHECK_INT(o.result, PROFILES_UPDATED);
    CHECK_INT(o.slot, 1);
    CHECK_STR(gets_(1, "WIFI", "wpa_key"), "newpassword");
    CHECK_INT(geti(1, "COMMON", "conf_serial_no"), 1);

    // A saved network switched from WPA to open: takes the open value from another slot
    c = cred("Home", WIFI_SEC_OPEN, "");
    o = profiles_save(&c);
    CHECK_INT(o.result, PROFILES_UPDATED);
    CHECK_INT(geti(1, "WIFI", "wifi_security"), SEC_OPEN);
    CHECK_STR(gets_(1, "WIFI", "wpa_key"), "");

    // WEP and open networks use their own values
    standard_setup();
    c = cred("Guest", WIFI_SEC_OPEN, "");
    o = profiles_save(&c);
    CHECK_INT(o.result, PROFILES_ADDED);
    CHECK_INT(geti(4, "WIFI", "wifi_security"), SEC_OPEN);
    c = cred("Attic", WIFI_SEC_WEP, "0123456789");
    o = profiles_save(&c);
    CHECK_INT(o.result, PROFILES_ADDED);
    CHECK_INT(o.slot, 6);
    CHECK_INT(geti(6, "WIFI", "wifi_security"), SEC_WEP);
    CHECK_STR(gets_(6, "WIFI", "wep_key"), "0123456789");
    CHECK_STR(gets_(6, "WIFI", "wpa_key"), "");

    // WEP with no saved WEP network: its value is unknown, so nothing is written
    reg_fake_reset();
    add(1, "Home", SEC_WPA, "homepass1", "", 1, 0, 0);
    c = cred("Attic", WIFI_SEC_WEP, "abcde");
    o = profiles_save(&c);
    CHECK_INT(o.result, PROFILES_ERR_NO_EXAMPLE);
    CHECK_STR(gets_(2, "WIFI", "ssid"), "");

    // No saved networks at all: WPA and open use the values the system wrote (2 and 0)
    reg_fake_reset();
    c = cred("Hotel", WIFI_SEC_WPA, "welcome123");
    o = profiles_save(&c);
    CHECK_INT(o.result, PROFILES_ADDED);
    CHECK_INT(o.slot, 1);
    CHECK_INT(geti(1, "WIFI", "wifi_security"), 2);
    c = cred("Airport", WIFI_SEC_OPEN, "");
    o = profiles_save(&c);
    CHECK_INT(o.result, PROFILES_ADDED);
    CHECK_INT(o.slot, 2);
    CHECK_INT(geti(2, "WIFI", "wifi_security"), 0);

    // All 30 slots used
    reg_fake_reset();
    for (int i = 1; i <= PROFILE_SLOTS; i++) {
        char name[16];
        snprintf(name, sizeof name, "Net %d", i);
        add(i, name, SEC_WPA, "password", "", i, 0, 0);
    }
    c = cred("Hotel", WIFI_SEC_WPA, "welcome123");
    CHECK_INT(profiles_save(&c).result, PROFILES_ERR_FULL);
    // ...but updating one of them still works
    c = cred("Net 30", WIFI_SEC_WPA, "welcome123");
    CHECK_INT(profiles_save(&c).result, PROFILES_UPDATED);

    // A registry failure part-way leaves the new slot free (the name is written last)
    for (int fail = 0; fail < 40; fail++) {
        standard_setup();
        reg_fake_fail_on_write = fail;
        reg_fake_writes = 0;
        c = cred("Hotel", WIFI_SEC_WPA, "welcome123");
        o = profiles_save(&c);
        if (o.result == PROFILES_ERR_REGISTRY) {
            CHECK_STR(gets_(4, "WIFI", "ssid"), "");
            CHECK_TRUE(o.reg_error < 0);
        } else {
            CHECK_INT(o.result, PROFILES_ADDED); // fewer writes than `fail`
            break;
        }
    }

    // The dump masks secrets when asked
    standard_setup();
    static char out[8192];
    out[0] = 0;
    void append(void *ud, const char *line);
    profiles_dump(append, out, 1);
    CHECK_TRUE(strstr(out, "/CONFIG/NET/01/WIFI/ssid=Home\n") != NULL);
    CHECK_TRUE(strstr(out, "/CONFIG/NET/01/WIFI/wpa_key=<9 chars>\n") != NULL);
    CHECK_TRUE(strstr(out, "homepass1") == NULL);
    CHECK_TRUE(strstr(out, "/CONFIG/NET/04/") == NULL); // free slots are skipped
    out[0] = 0;
    profiles_dump(append, out, 0);
    CHECK_TRUE(strstr(out, "/CONFIG/NET/01/WIFI/wpa_key=homepass1\n") != NULL);

    return CHECK_DONE("profiles");
}

void append(void *ud, const char *line) {
    strcat((char *)ud, line);
    strcat((char *)ud, "\n");
}
