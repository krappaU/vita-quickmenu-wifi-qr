// SPDX-License-Identifier: MIT
// Wi-Fi QR loader (wifiqr.suprx, under *main in taiHEN's config.txt): loads the core
// (wifiqr_core.suprx) into SceShell once the shell has started.
//
// The core loaded under *main itself crashed on its first library call (2026-11-03: the scan
// button's call into it died at its first log line, and its start-up line never reached the log),
// while the same file loaded later by QuickMenuReborn worked. QuickMenuReborn, also under *main,
// does only this at start-up: a thread that waits a few seconds and then loads its plugins with
// sceKernelLoadStartModule. So does this.
#include <psp2/io/stat.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>

#define DELAY_US (5 * 1000 * 1000)

static const char *const core_paths[] = {
    "ux0:tai/wifiqr_core.suprx",
    "ur0:tai/wifiqr_core.suprx",
};

static int load_thread(SceSize args, void *argp) {
    (void)args, (void)argp;
    sceKernelDelayThread(DELAY_US);
    for (unsigned i = 0; i < sizeof core_paths / sizeof core_paths[0]; i++) {
        SceIoStat st;
        if (sceIoGetstat(core_paths[i], &st) < 0) continue;
        // The core logs "core (wifiqr) loaded" itself; nothing to report from here.
        sceKernelLoadStartModule(core_paths[i], 0, NULL, 0, NULL, NULL);
        break;
    }
    return sceKernelExitDeleteThread(0);
}

int module_start(SceSize args, void *argp) {
    (void)args, (void)argp;
    SceUID th = sceKernelCreateThread("wifiqr_loader", load_thread, 0x10000100, 0x1000, 0, 0, NULL);
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) return SCE_KERNEL_START_NO_RESIDENT;
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
    (void)args, (void)argp;
    return SCE_KERNEL_STOP_SUCCESS;
}

void _start() __attribute__((weak, alias("module_start")));
