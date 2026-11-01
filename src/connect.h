// SPDX-License-Identifier: MIT
// Makes the Vita connect now. It connects on demand: when an app asks for the network, it joins a
// saved network in range (tested 2026-11 with Pokerogue, whose first request is a DNS lookup).
// So: ask for a DNS lookup, then if nothing happens a plain TCP connection, and wait.
#ifndef CONNECT_H
#define CONNECT_H

typedef enum {
    CONNECT_TARGET = 0,  // connected to the network just saved
    CONNECT_OTHER,       // connected, but to another saved network
    CONNECT_NONE,        // still not connected when the time ran out
} connect_result;

connect_result connect_now(const char *ssid, int timeout_ms, volatile int *cancel, char *connected_ssid, int size);

#endif
