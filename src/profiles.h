// SPDX-License-Identifier: MIT
// The Vita's saved networks: 30 slots in the registry, /CONFIG/NET/01 .. /CONFIG/NET/30, each with
// WIFI/, COMMON/, IP/ and APP/ keys (layout from Vita Account Manager and Registry Editor MOD).
// A slot with an empty WIFI/ssid is free.
#ifndef PROFILES_H
#define PROFILES_H

#include "wifi_qr.h"

#define PROFILE_SLOTS 30

typedef enum {
    PROFILES_ADDED = 0,
    PROFILES_UPDATED,           // a profile with the same network name was changed
    PROFILES_UNCHANGED,         // the same network and password were already saved
    PROFILES_ERR_FULL,          // all 30 slots are in use
    PROFILES_ERR_NO_EXAMPLE,    // WEP, and no saved WEP network to learn its security value from
    PROFILES_ERR_REGISTRY,      // a registry call failed
} profiles_result;

typedef struct {
    profiles_result result;
    int slot;              // 1..30
    int security_value;    // the wifi_security value written
    int reg_error;         // the failing registry call's result, for PROFILES_ERR_REGISTRY
} profiles_outcome;

// Adds the network, or updates the saved one with the same name. A new profile gets the values
// the Vita's own Wi-Fi Settings writes for a new network (automatic IP, no proxy), then the name,
// security and password. The wifi_security number comes from a saved network of the same type,
// or the values seen from the system (open 0, WPA 2).
profiles_outcome profiles_save(const wifi_cred *c);

// Writes every key of every used slot as "path/name=value" lines (passwords included), for a backup.
typedef void (*profiles_line_fn)(void *ud, const char *line);
void profiles_dump(profiles_line_fn fn, void *ud, int mask_secrets);

const char *profiles_result_str(profiles_result r);

// The shell keeps its own copy of the saved networks, read at boot: a profile written only to the
// registry shows in Settings but connects with the stale copy until a reboot (2026-11-02). The
// system's Wi-Fi Settings popup (impose_net_plugin, function at 0x81005574 of its 3.65 build)
// writes the registry, sets conf_flag, then hands the profile to SceShell_60129092(entries, 1) as
// this structure, laid out like the registry keys (offsets from its stores and copy sizes):
#define SHELL_PROFILE_SIZE 0x478
//   +0x000 slot (1-based)      +0x004 conf_flag (1)    +0x008 conf_type (0)
//   +0x00C conf_name[65]       +0x050 conf_serial_no   +0x054 enable_auto_connect (1)
//   +0x058 mtu                 +0x060 ssid[33]         +0x084 wifi_security
//   +0x088 wep_key[27]         +0x0A4 wpa_key[65]      +0x0E8 ip_config (0)   +0x31C dns_flag
// Everything else zero, as the popup leaves it.
void profiles_shell_entry(unsigned char out[SHELL_PROFILE_SIZE], int slot, const wifi_cred *c, int security);

#endif
