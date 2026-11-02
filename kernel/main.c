// SPDX-License-Identifier: MIT
// Kernel helper: draws the camera preview over SceShell's frames, the way PSVshell draws its
// overlay. QuickMenuReborn can't show a changing picture (it loads each image once per menu
// opening and never frees it while open), but every frame the shell shows passes through
// ksceDisplaySetFrameBufInternal, so the preview is copied into each one just before it's shown.
#include <psp2kern/display.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/threadmgr.h>
#include <stdint.h>
#include <taihen.h>

#include "wifiqr_kernel.h"

#define BORDER 2
#define MARGIN 8
#define TIMEOUT_US 1000000 // hide the preview if the plugin stops sending frames

static tai_hook_ref_t display_ref;
static SceUID display_hook = -1;

static uint8_t preview[PREVIEW_W * PREVIEW_H];
static int corners[8], ncorners;
static volatile int shown;          // a preview is set
static SceUID owner_pid;            // the process whose frames get it (SceShell)
static volatile SceUInt64 last_set; // system time of the last frame, in us

static void draw(const SceDisplayFrameBuf *fb) {
    if (fb->pixelformat != SCE_DISPLAY_PIXELFORMAT_A8B8G8R8) return;
    const int ow = PREVIEW_W + 2 * BORDER, oh = PREVIEW_H + 2 * BORDER;
    if ((int)fb->width < ow + 2 * MARGIN || (int)fb->height < oh + 2 * MARGIN) return;
    // Top right of the screen.
    const int x0 = (int)fb->width - ow - MARGIN, y0 = MARGIN;
    static uint32_t line[PREVIEW_W + 2 * BORDER];
    for (int row = 0; row < oh; row++) {
        int py = row - BORDER;
        for (int col = 0; col < ow; col++) {
            int px = col - BORDER;
            uint32_t c;
            if (py < 0 || py >= PREVIEW_H || px < 0 || px >= PREVIEW_W) {
                c = 0xFFFFFFFF; // white border
            } else {
                uint32_t v = preview[py * PREVIEW_W + px];
                c = 0xFF000000 | v << 16 | v << 8 | v; // A8B8G8R8
                for (int i = 0; i < ncorners; i++) {   // a red box around each corner square
                    int dx = px - corners[2 * i], dy = py - corners[2 * i + 1];
                    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
                    if ((ax <= 9 && ay <= 9) && (ax >= 8 || ay >= 8)) c = 0xFF0000FF;
                }
            }
            line[col] = c;
        }
        void *dst = (uint32_t *)fb->base + (y0 + row) * fb->pitch + x0;
        ksceKernelMemcpyKernelToUser(dst, line, sizeof(uint32_t) * ow);
    }
}

static int display_hook_fn(int head, int index, const SceDisplayFrameBuf *pParam, int sync) {
    // Same conditions as PSVshell, plus: only the process that asked for it (SceShell, which
    // draws the Quick Menu), and only while frames keep coming.
    if (head && pParam && pParam->base && shown && ksceKernelGetProcessId() == owner_pid) {
        if (ksceKernelGetSystemTimeWide() - last_set < TIMEOUT_US) draw(pParam);
        else shown = 0;
    }
    return TAI_CONTINUE(int, display_ref, head, index, pParam, sync);
}

int wifiqrPreviewSet(const void *gray, int w, int h, const int *user_corners, int n) {
    uint32_t state;
    ENTER_SYSCALL(state);
    int ret = 0;
    if (display_hook < 0) {
        ret = display_hook;
    } else if (w != PREVIEW_W || h != PREVIEW_H || !gray) {
        ret = -1;
    } else {
        ksceKernelMemcpyUserToKernel(preview, gray, sizeof preview);
        int c[8] = {0};
        if (n < 0) n = 0;
        if (n > 4) n = 4;
        if (n && user_corners) ksceKernelMemcpyUserToKernel(c, user_corners, sizeof(int) * 2 * n);
        for (int i = 0; i < 8; i++) corners[i] = c[i];
        ncorners = n;
        owner_pid = ksceKernelGetProcessId();
        last_set = ksceKernelGetSystemTimeWide();
        shown = 1;
    }
    EXIT_SYSCALL(state);
    return ret;
}

int wifiqrPreviewOff(void) {
    uint32_t state;
    ENTER_SYSCALL(state);
    shown = 0;
    EXIT_SYSCALL(state);
    return 0;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp) {
    (void)args, (void)argp;
    // SceDisplayForDriver / ksceDisplaySetFrameBufInternal, the NIDs PSVshell uses (3.60, 3.65).
    display_hook = taiHookFunctionExportForKernel(KERNEL_PID, &display_ref, "SceDisplay", 0x9FED47AC, 0x16466675,
                                                  display_hook_fn);
    return SCE_KERNEL_START_SUCCESS; // stay loaded even if the hook failed: the syscall reports it
}

int module_stop(SceSize args, void *argp) {
    (void)args, (void)argp;
    if (display_hook >= 0) taiHookReleaseForKernel(display_hook, display_ref);
    return SCE_KERNEL_STOP_SUCCESS;
}
