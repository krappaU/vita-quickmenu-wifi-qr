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
    PROFILES_ERR_NO_EXAMPLE,    // no saved network of this security type to learn its value from
    PROFILES_ERR_REGISTRY,      // a registry call failed
} profiles_result;

typedef struct {
    profiles_result result;
    int slot;              // 1..30
    int template_slot;     // the saved network copied for a new profile (0 if none)
    int security_value;    // the wifi_security value written
    int reg_error;         // the failing registry call's result, for PROFILES_ERR_REGISTRY
} profiles_outcome;

// Adds the network, or updates the saved one with the same name.
// A new profile copies every key from an existing saved network (IP, proxy and connection settings
// as the Vita's own Settings wrote them), then sets the name, security and password.
// The wifi_security number is taken from a saved network of the same type, because the meaning of
// its values is not documented.
profiles_outcome profiles_save(const wifi_cred *c);

// Writes every key of every used slot as "path/name=value" lines (passwords included), for a backup.
typedef void (*profiles_line_fn)(void *ud, const char *line);
void profiles_dump(profiles_line_fn fn, void *ud, int mask_secrets);

const char *profiles_result_str(profiles_result r);

#endif
