// SPDX-License-Identifier: MIT
#include "profiles.h"

#include <string.h>

#include "reg.h"
#include "util.h"

typedef struct {
    const char *sub;   // under /CONFIG/NET/NN/
    const char *name;
    int is_str;
    int size;          // string field size including the terminator
} key_def;

// Every key of a profile (sizes from Registry Editor MOD's regs.c).
static const key_def KEYS[] = {
    {"APP", "http_proxy_flag", 0, 4},
    {"APP", "http_proxy_port", 0, 4},
    {"APP", "http_proxy_server", 1, 256},
    {"COMMON", "conf_flag", 0, 4},
    {"COMMON", "conf_name", 1, 65},
    {"COMMON", "conf_serial_no", 0, 4},
    {"COMMON", "conf_type", 0, 4},
    {"COMMON", "device", 0, 4},
    {"COMMON", "enable_auto_connect", 0, 4},
    {"COMMON", "ether_mode", 0, 4},
    {"COMMON", "mtu", 0, 4},
    {"IP", "auth_key", 1, 128},
    {"IP", "auth_name", 1, 128},
    {"IP", "default_route", 1, 16},
    {"IP", "dhcp_hostname", 1, 256},
    {"IP", "dns_flag", 0, 4},
    {"IP", "ip_address", 1, 16},
    {"IP", "ip_config", 0, 4},
    {"IP", "netmask", 1, 16},
    {"IP", "primary_dns", 1, 16},
    {"IP", "secondary_dns", 1, 16},
    {"WIFI", "ssid", 1, 33},
    {"WIFI", "wep_key", 1, 27},
    {"WIFI", "wifi_security", 0, 4},
    {"WIFI", "wpa_key", 1, 65},
};
#define NKEYS (int)(sizeof KEYS / sizeof KEYS[0])

typedef struct {
    int used;
    char ssid[33];
    char wpa_key[65];
    char wep_key[27];
    int security;
    int serial;
    int ip_config;
    int proxy_flag;
} slot_info;

static void cat_path(char *out, int size, int slot, const char *sub) {
    u_snprintf(out, size, "/CONFIG/NET/%02d/%s", slot, sub);
}

static void read_slot(int slot, slot_info *s) {
    char cat[48];
    memset(s, 0, sizeof *s);
    cat_path(cat, sizeof cat, slot, "WIFI");
    if (reg_get_str(cat, "ssid", s->ssid, sizeof s->ssid) < 0 || s->ssid[0] == 0) return;
    s->used = 1;
    reg_get_str(cat, "wpa_key", s->wpa_key, sizeof s->wpa_key);
    reg_get_str(cat, "wep_key", s->wep_key, sizeof s->wep_key);
    reg_get_int(cat, "wifi_security", &s->security);
    cat_path(cat, sizeof cat, slot, "COMMON");
    reg_get_int(cat, "conf_serial_no", &s->serial);
    cat_path(cat, sizeof cat, slot, "IP");
    reg_get_int(cat, "ip_config", &s->ip_config);
    cat_path(cat, sizeof cat, slot, "APP");
    reg_get_int(cat, "http_proxy_flag", &s->proxy_flag);
}

static wifi_sec slot_type(const slot_info *s) {
    if (s->wpa_key[0]) return WIFI_SEC_WPA;
    if (s->wep_key[0]) return WIFI_SEC_WEP;
    return WIFI_SEC_OPEN;
}

// The most common value among used slots: what most saved networks use (most likely automatic IP
// and no proxy), so a new profile doesn't inherit one network's static IP or proxy.
static int most_common(const slot_info *slots, int field) {
    int best = 0, best_n = -1;
    for (int i = 0; i < PROFILE_SLOTS; i++) {
        if (!slots[i].used) continue;
        int v = field ? slots[i].proxy_flag : slots[i].ip_config, n = 0;
        for (int j = 0; j < PROFILE_SLOTS; j++)
            if (slots[j].used && (field ? slots[j].proxy_flag : slots[j].ip_config) == v) n++;
        if (n > best_n) best = v, best_n = n;
    }
    return best;
}

static int copy_key(int from, int to, const key_def *k) {
    char src[48], dst[48];
    cat_path(src, sizeof src, from, k->sub);
    cat_path(dst, sizeof dst, to, k->sub);
    if (k->is_str) {
        char buf[256];
        int rc = reg_get_str(src, k->name, buf, k->size);
        if (rc < 0) return rc;
        return reg_set_str(dst, k->name, buf, k->size);
    }
    int v = 0, rc = reg_get_int(src, k->name, &v);
    if (rc < 0) return rc;
    return reg_set_int(dst, k->name, v);
}

#define CHECK(call) do { int rc_ = (call); if (rc_ < 0) { o.result = PROFILES_ERR_REGISTRY; o.reg_error = rc_; return o; } } while (0)

static profiles_outcome write_secrets(profiles_outcome o, int slot, const wifi_cred *c, int security) {
    char cat[48];
    cat_path(cat, sizeof cat, slot, "WIFI");
    CHECK(reg_set_int(cat, "wifi_security", security));
    CHECK(reg_set_str(cat, "wpa_key", c->sec == WIFI_SEC_WPA ? c->password : "", 65));
    CHECK(reg_set_str(cat, "wep_key", c->sec == WIFI_SEC_WEP ? c->password : "", 27));
    return o;
}

profiles_outcome profiles_save(const wifi_cred *c) {
    profiles_outcome o = {0};
    slot_info slots[PROFILE_SLOTS];
    for (int i = 0; i < PROFILE_SLOTS; i++) read_slot(i + 1, &slots[i]);

    int same = -1, free_slot = -1, max_serial = 0;
    for (int i = 0; i < PROFILE_SLOTS; i++) {
        if (!slots[i].used) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (same < 0 && strcmp(slots[i].ssid, c->ssid) == 0) same = i;
        if (slots[i].serial > max_serial) max_serial = slots[i].serial;
    }

    // The wifi_security value for this type, learned from a saved network of the same type.
    // Prefer the network being updated, then one matching the usual IP/proxy settings.
    int want_ip = most_common(slots, 0), want_proxy = most_common(slots, 1);
    int example = -1;
    if (same >= 0 && slot_type(&slots[same]) == c->sec) example = same;
    for (int pass = 0; pass < 2 && example < 0; pass++)
        for (int i = 0; i < PROFILE_SLOTS && example < 0; i++)
            if (slots[i].used && slot_type(&slots[i]) == c->sec &&
                (pass == 1 || (slots[i].ip_config == want_ip && slots[i].proxy_flag == want_proxy)))
                example = i;
    if (example < 0) {
        o.result = PROFILES_ERR_NO_EXAMPLE;
        return o;
    }
    o.security_value = slots[example].security;

    if (same >= 0) {
        const slot_info *s = &slots[same];
        o.slot = same + 1;
        const char *cur = c->sec == WIFI_SEC_WPA ? s->wpa_key : c->sec == WIFI_SEC_WEP ? s->wep_key : "";
        if (slot_type(s) == c->sec && s->security == o.security_value && strcmp(cur, c->password) == 0) {
            o.result = PROFILES_UNCHANGED;
            return o;
        }
        o = write_secrets(o, o.slot, c, o.security_value);
        if (o.result == PROFILES_ERR_REGISTRY) return o;
        o.result = PROFILES_UPDATED;
        return o;
    }

    if (free_slot < 0) {
        o.result = PROFILES_ERR_FULL;
        return o;
    }
    o.slot = free_slot + 1;
    o.template_slot = example + 1;

    // Everything from the template except the name, which goes last: until then the slot's ssid
    // is still empty, so a half-written profile stays a free slot.
    for (int k = 0; k < NKEYS; k++) {
        if (strcmp(KEYS[k].name, "ssid") == 0) continue;
        CHECK(copy_key(o.template_slot, o.slot, &KEYS[k]));
    }
    char cat[48];
    cat_path(cat, sizeof cat, o.slot, "COMMON");
    CHECK(reg_set_str(cat, "conf_name", c->ssid, 65));
    CHECK(reg_set_int(cat, "conf_serial_no", max_serial + 1));
    o = write_secrets(o, o.slot, c, o.security_value);
    if (o.result == PROFILES_ERR_REGISTRY) return o;
    cat_path(cat, sizeof cat, o.slot, "WIFI");
    CHECK(reg_set_str(cat, "ssid", c->ssid, 33));
    o.result = PROFILES_ADDED;
    return o;
}

void profiles_dump(profiles_line_fn fn, void *ud, int mask_secrets) {
    char cat[48], line[400], buf[256];
    char wifi_flag[16] = "?";
    int v;
    if (reg_get_int("/CONFIG/NET", "wifi_flag", &v) >= 0) u_snprintf(wifi_flag, sizeof wifi_flag, "%d", v);
    u_snprintf(line, sizeof line, "/CONFIG/NET/wifi_flag=%s", wifi_flag);
    fn(ud, line);
    for (int slot = 1; slot <= PROFILE_SLOTS; slot++) {
        cat_path(cat, sizeof cat, slot, "WIFI");
        if (reg_get_str(cat, "ssid", buf, 33) < 0 || buf[0] == 0) continue;
        for (int k = 0; k < NKEYS; k++) {
            const key_def *kd = &KEYS[k];
            cat_path(cat, sizeof cat, slot, kd->sub);
            if (kd->is_str) {
                if (reg_get_str(cat, kd->name, buf, kd->size) < 0) u_snprintf(buf, sizeof buf, "<error>");
                int secret = strcmp(kd->name, "wpa_key") == 0 || strcmp(kd->name, "wep_key") == 0 ||
                             strcmp(kd->name, "auth_key") == 0;
                if (mask_secrets && secret && buf[0]) u_snprintf(buf, sizeof buf, "<%d chars>", (int)strlen(buf));
                u_snprintf(line, sizeof line, "%s/%s=%s", cat, kd->name, buf);
            } else {
                int iv = 0;
                if (reg_get_int(cat, kd->name, &iv) < 0) u_snprintf(line, sizeof line, "%s/%s=<error>", cat, kd->name);
                else u_snprintf(line, sizeof line, "%s/%s=%d", cat, kd->name, iv);
            }
            fn(ud, line);
        }
    }
}

const char *profiles_result_str(profiles_result r) {
    switch (r) {
    case PROFILES_ADDED: return "added";
    case PROFILES_UPDATED: return "updated";
    case PROFILES_UNCHANGED: return "already saved";
    case PROFILES_ERR_FULL: return "All 30 network slots are used. Delete one in Settings";
    case PROFILES_ERR_NO_EXAMPLE: return "Add one network of this type in Settings first";
    case PROFILES_ERR_REGISTRY: return "Registry write failed";
    }
    return "?";
}
