// SPDX-License-Identifier: MIT
// Diagnostic: loaded under *main at boot, the core crashed on its first library call while the
// same file loaded later worked (2026-11-03), and its own logging was the first thing to fail.
// wifiqrSelfTest calls one harmless function from each library the core imports, reporting
// through the caller (whose logging works) before and after each call, with the first words of
// the function's import stub: a stub the loader linked jumps to the library, one it didn't still
// holds what vita-elf-create wrote. The last "calling" line before a crash names the call.
#include <psp2/camera.h>
#include <stdarg.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/registrymgr.h>
#include <psp2/rtc.h>

#include "log.h"
#include "wifiqr_api.h"
#include "wifiqr_kernel.h"

int SceShell_60129092(void *profiles, unsigned int count);

static wifiqr_report_fn report;
static void *report_ud;

static void say(const char *fmt, ...) {
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    sceClibVsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    report(line, report_ud);
}

// The stub's first three words, read through the Thumb-cleared address.
static void stub_words(const char *name, const void *fn) {
    const unsigned *p = (const unsigned *)((unsigned)fn & ~1u);
    say("calling %s: stub %p = %08x %08x %08x", name, fn, p[0], p[1], p[2]);
}

#define STEP(name, call)                                         \
    do {                                                         \
        stub_words(#name, (const void *)&name);                  \
        int rc_ = (int)(call);                                   \
        say("  %s -> 0x%08x", #name, (unsigned)rc_);             \
    } while (0)

int wifiqrSelfTest(wifiqr_report_fn fn, void *ud) {
    if (!fn) return -1;
    report = fn, report_ud = ud;
    say("self-test: core at %p", (const void *)&wifiqrSelfTest);

    STEP(sceClibStrnlen, sceClibStrnlen("abc", 8));                                // SceLibKernel
    STEP(sceKernelGetProcessTimeWide, (int)sceKernelGetProcessTimeWide());         // SceLibKernel
    STEP(sceKernelDelayThread, sceKernelDelayThread(1));                           // SceThreadmgr
    SceDateTime t;
    STEP(sceRtcGetCurrentClockLocalTime, sceRtcGetCurrentClockLocalTime(&t));      // SceRtcUser
    SceIoStat st;
    STEP(sceIoGetstat, sceIoGetstat("ux0:data", &st));                             // SceLibKernel
    SceUID fd;
    STEP(sceIoOpen, fd = sceIoOpen("ux0:data/wifiqr/log.txt", SCE_O_RDONLY, 0));   // SceLibKernel
    if (fd >= 0) STEP(sceIoClose, sceIoClose(fd));                                 // SceIofilemgr
    SceUID mtx;
    STEP(sceKernelCreateMutex, mtx = sceKernelCreateMutex("wifiqr_test", 0, 0, NULL));
    if (mtx >= 0) {
        STEP(sceKernelLockMutex, sceKernelLockMutex(mtx, 1, NULL));                // SceThreadmgr
        STEP(sceKernelUnlockMutex, sceKernelUnlockMutex(mtx, 1));
        STEP(sceKernelDeleteMutex, sceKernelDeleteMutex(mtx));
    }
    SceUID blk;
    STEP(sceKernelAllocMemBlock, blk = sceKernelAllocMemBlock("wifiqr_test", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x1000, NULL));
    if (blk >= 0) STEP(sceKernelFreeMemBlock, sceKernelFreeMemBlock(blk));         // SceSysmem
    int v = -1;
    STEP(sceRegMgrGetKeyInt, sceRegMgrGetKeyInt("/CONFIG/NET", "wifi_flag", &v)); // SceRegMgr
    int state = -1;
    STEP(sceNetCtlInetGetState, sceNetCtlInetGetState(&state));                    // SceNetCtl
    STEP(sceNetHtons, sceNetHtons(1));                                             // SceNet
    STEP(sceCameraIsActive, sceCameraIsActive(SCE_CAMERA_DEVICE_BACK));            // SceCamera
    stub_words("SceShell_60129092 (not called)", (const void *)&SceShell_60129092);
    STEP(wifiqrPreviewOff, wifiqrPreviewOff());                                    // WifiQrKernel (weak)
    say("calling log_printf");
    log_printf("self-test: the core's own logging works");
    say("self-test: done");
    return 0;
}
