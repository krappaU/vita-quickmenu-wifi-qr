// SPDX-License-Identifier: MIT
#include "net_dyn.h"

#include <stddef.h>
#include <taihen.h>

#define LIB_SCENET 0x6BF8B2A2u
#define LIB_SCENETCTL 0xC6083AE1u

static net_fns fns;
static int resolved;

static int find(const char *module, unsigned lib, unsigned nid, void *slot) {
    uintptr_t f = 0;
    int rc = taiGetModuleExportFunc(module, lib, nid, &f);
    if (rc < 0) return rc;
    *(uintptr_t *)slot = f;
    return 0;
}

int net_resolve(const net_fns **out) {
    if (!resolved) {
        static const struct { const char *module; unsigned lib, nid; size_t offset; } t[] = {
            {"SceNet", LIB_SCENET, 0xF084FCE3, offsetof(net_fns, Socket)},
            {"SceNet", LIB_SCENET, 0x11E5B6F6, offsetof(net_fns, Connect)},
            {"SceNet", LIB_SCENET, 0x065505CA, offsetof(net_fns, Setsockopt)},
            {"SceNet", LIB_SCENET, 0x29822B4D, offsetof(net_fns, SocketClose)},
            {"SceNet", LIB_SCENET, 0x6DA29319, offsetof(net_fns, ResolverCreate)},
            {"SceNet", LIB_SCENET, 0x1EB11857, offsetof(net_fns, ResolverStartNtoa)},
            {"SceNet", LIB_SCENET, 0x3559F098, offsetof(net_fns, ResolverDestroy)},
            {"SceNet", LIB_SCENET, 0x9FA3207B, offsetof(net_fns, Htons)},
            {"SceNet", LIB_SCENET, 0xD5EEB048, offsetof(net_fns, InetPton)},
            {"SceLibNetCtl", LIB_SCENETCTL, 0x6D26AC68, offsetof(net_fns, CtlInetGetState)},
            {"SceLibNetCtl", LIB_SCENETCTL, 0xB26D07F3, offsetof(net_fns, CtlInetGetInfo)},
        };
        for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
            int rc = find(t[i].module, t[i].lib, t[i].nid, (char *)&fns + t[i].offset);
            if (rc < 0) return rc;
        }
        resolved = 1;
    }
    *out = &fns;
    return 0;
}
