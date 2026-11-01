// SPDX-License-Identifier: MIT
#include "connect.h"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#include "log.h"

static const char *state_name(int s) {
    switch (s) {
    case SCE_NETCTL_STATE_DISCONNECTED: return "disconnected";
    case SCE_NETCTL_STATE_CONNECTING: return "connecting";
    case SCE_NETCTL_STATE_FINALIZING: return "finalizing";
    case SCE_NETCTL_STATE_CONNECTED: return "connected";
    }
    return "?";
}

static int get_state(void) {
    int s = -1, rc = sceNetCtlInetGetState(&s);
    if (rc < 0) {
        log_printf("sceNetCtlInetGetState: 0x%08x", rc);
        return -1;
    }
    return s;
}

static void get_ssid(char *out, int size) {
    SceNetCtlInfo info;
    sceClibMemset(&info, 0, sizeof info);
    out[0] = 0;
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_SSID, &info) >= 0) sceClibStrncpy(out, info.ssid, size - 1);
    out[size - 1] = 0;
    SceNetCtlInfo sec;
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_WIFI_SECURITY, &sec) >= 0)
        log_printf("connected network's wifi_security (netctl): %u", sec.wifi_security);
}

static void trigger_dns(void) {
    int rid = sceNetResolverCreate("wifiqr", NULL, 0);
    if (rid < 0) {
        log_printf("sceNetResolverCreate: 0x%08x", rid);
        return;
    }
    SceNetInAddr addr;
    int rc = sceNetResolverStartNtoa(rid, "www.playstation.net", &addr, 0, 0, 0);
    log_printf("trigger: DNS lookup -> 0x%08x", rc);
    sceNetResolverDestroy(rid);
}

static void trigger_tcp(void) {
    int s = sceNetSocket("wifiqr", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (s < 0) {
        log_printf("sceNetSocket: 0x%08x", s);
        return;
    }
    int nb = 1;
    sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nb, sizeof nb);
    SceNetSockaddrIn sa;
    sceClibMemset(&sa, 0, sizeof sa);
    sa.sin_len = sizeof sa;
    sa.sin_family = SCE_NET_AF_INET;
    sa.sin_port = sceNetHtons(80);
    sceNetInetPton(SCE_NET_AF_INET, "1.1.1.1", &sa.sin_addr);
    int rc = sceNetConnect(s, (SceNetSockaddr *)&sa, sizeof sa);
    log_printf("trigger: TCP connect -> 0x%08x", rc);
    sceNetSocketClose(s);
}

connect_result connect_now(const char *ssid, int timeout_ms, volatile int *cancel, char *connected_ssid, int size) {
    connected_ssid[0] = 0;
    int s = get_state();
    log_printf("netctl state before: %s", state_name(s));
    if (s == SCE_NETCTL_STATE_CONNECTED) {
        get_ssid(connected_ssid, size);
        return sceClibStrcmp(connected_ssid, ssid) == 0 ? CONNECT_TARGET : CONNECT_OTHER;
    }

    SceUInt64 start = sceKernelGetProcessTimeWide();
    int last = s, seen_activity = 0, tcp_tried = 0;
    trigger_dns();
    for (;;) {
        unsigned ms = (unsigned)((sceKernelGetProcessTimeWide() - start) / 1000);
        s = get_state();
        if (s != last) {
            log_printf("netctl state: %s after %u ms", state_name(s), ms);
            last = s;
        }
        if (s == SCE_NETCTL_STATE_CONNECTING || s == SCE_NETCTL_STATE_FINALIZING) seen_activity = 1;
        if (s == SCE_NETCTL_STATE_CONNECTED) break;
        if (ms >= (unsigned)timeout_ms || (cancel && *cancel)) return CONNECT_NONE;
        // The DNS lookup didn't start a connection: try a plain TCP connection once.
        if (!seen_activity && !tcp_tried && ms >= 5000) {
            tcp_tried = 1;
            trigger_tcp();
        }
        sceKernelDelayThread(250 * 1000);
    }
    get_ssid(connected_ssid, size);
    log_printf("connected to \"%s\"", connected_ssid);
    return sceClibStrcmp(connected_ssid, ssid) == 0 ? CONNECT_TARGET : CONNECT_OTHER;
}
