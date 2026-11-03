// SPDX-License-Identifier: MIT
// The network functions the core uses, looked up at run time instead of imported.
//
// libnet (SceNet) and libnetctl (SceLibNetCtl) are separate system modules the shell loads on
// demand (vs0:sys/external/). A module taiHEN loads under *main, early in the shell's start-up,
// that imports them may fail to load or be left with unlinked imports, whose stubs jump to
// address 0 (2026-11-03: the core loaded under *main was missing from the shell's module list).
// taiGetModuleExportFunc finds them whenever they are loaded.
#ifndef NET_DYN_H
#define NET_DYN_H

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

typedef struct {
    int (*Socket)(const char *name, int domain, int type, int protocol);
    int (*Connect)(int s, const SceNetSockaddr *name, unsigned int namelen);
    int (*Setsockopt)(int s, int level, int optname, const void *optval, unsigned int optlen);
    int (*SocketClose)(int s);
    int (*ResolverCreate)(const char *name, SceNetResolverParam *param, int flags);
    int (*ResolverStartNtoa)(int rid, const char *hostname, SceNetInAddr *addr, int timeout, int retry, int flags);
    int (*ResolverDestroy)(int rid);
    unsigned short (*Htons)(unsigned short v);
    int (*InetPton)(int af, const char *src, void *dst);
    int (*CtlInetGetState)(int *state);
    int (*CtlInetGetInfo)(int code, SceNetCtlInfo *info);
} net_fns;

// Fills *out (once; later calls reuse it). Returns 0, or taiHEN's error for the first function
// it couldn't find (the libraries aren't loaded yet).
int net_resolve(const net_fns **out);

// 1 if the import stub of fn was linked by the module loader. vita-elf-create writes unlinked
// stubs as "sub r12, pc, #8; mov pc, #0", which jumps to address 0: calling a weak import whose
// library isn't loaded crashes rather than failing (2026-11-03, the kernel helper not installed).
static inline int import_linked(const void *fn) {
    return *(const unsigned *)((unsigned)fn & ~1u) != 0xE24FC008u;
}

#endif
