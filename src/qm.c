// SPDX-License-Identifier: MIT
// Wi-Fi QR Quick Menu button (wifiqr_qm.suprx, loaded by QuickMenuReborn): a button, a status
// line and the text viewfinder. All the work happens in the core plugin (wifiqr.suprx), found at
// run time through taiHEN.
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <quickmenureborn/qm_reborn.h>
#include <taihen.h>

#include "viewfinder.h"
#include "wifiqr_api.h"

#define SEPARATOR_ID "wifiqr_separator"
#define TITLE_ID "wifiqr_title"
#define BUTTON_ID "wifiqr_button"
#define STATUS_ID "wifiqr_status"
#define VIEW_ID "wifiqr_view"

#define BUTTON_SCAN "Scan Wi-Fi QR"
#define BUTTON_CANCEL "Cancel"

static int live_labels = 1; // update labels from the core's thread while the menu is open
static char status[256] = "Show the network's QR code (Android: Wi-Fi > Share)";
static char button_label[32] = BUTTON_SCAN;
static char view_text[VIEW_TEXT_MAX] = "";

static void show(const char *id, const char *label) {
    if (live_labels) QuickMenuRebornSetWidgetLabel(id, label);
}

static void load_config(void) {
    char buf[512];
    SceUID fd = sceIoOpen("ux0:data/wifiqr/config.txt", SCE_O_RDONLY, 0);
    if (fd < 0) return;
    int n = sceIoRead(fd, buf, sizeof buf - 1);
    sceIoClose(fd);
    if (n <= 0) return;
    buf[n] = 0;
    const char *p = sceClibStrstr(buf, "live_labels=");
    if (p) live_labels = p[12] != '0';
}

// --- the core's API, looked up when needed --------------------------------------------------

static struct {
    wifiqrScan_fn scan;
    wifiqrCancel_fn cancel;
    wifiqrIsBusy_fn is_busy;
} core;

static int find_core(void) {
    if (core.scan) return 0;
    uintptr_t f[3];
    static const unsigned nids[3] = {WIFIQR_NID_SCAN, WIFIQR_NID_CANCEL, WIFIQR_NID_ISBUSY};
    for (int i = 0; i < 3; i++) {
        int rc = taiGetModuleExportFunc(WIFIQR_MODULE, WIFIQR_LIB_NID, nids[i], &f[i]);
        if (rc < 0) return rc;
    }
    core.scan = (wifiqrScan_fn)f[0];
    core.cancel = (wifiqrCancel_fn)f[1];
    core.is_busy = (wifiqrIsBusy_fn)f[2];
    return 0;
}

// --- callbacks from the core (its worker thread) --------------------------------------------

static void on_status(const char *text, void *ud) {
    (void)ud;
    sceClibStrncpy(status, text, sizeof status - 1);
    show(STATUS_ID, status);
}

static void on_view(const char *text, void *ud) {
    (void)ud;
    sceClibStrncpy(view_text, text, sizeof view_text - 1);
    show(VIEW_ID, view_text);
}

static void on_busy(int busy, void *ud) {
    (void)ud;
    sceClibStrncpy(button_label, busy ? BUTTON_CANCEL : BUTTON_SCAN, sizeof button_label - 1);
    show(BUTTON_ID, button_label);
}

static const WifiQrListener listener = {sizeof(WifiQrListener), on_status, on_view, on_busy, NULL};

// --- widgets ----------------------------------------------------------------------------------

BUTTON_HANDLER(on_button) {
    (void)id, (void)hash, (void)eventId, (void)userDat;
    int rc = find_core();
    if (rc < 0) {
        sceClibSnprintf(status, sizeof status, "Wi-Fi QR core not loaded: add wifiqr.suprx under *main (0x%08X)",
                        (unsigned)rc);
        show(STATUS_ID, status);
        return;
    }
    if (core.is_busy()) {
        core.cancel();
        return;
    }
    load_config();
    rc = core.scan(&listener);
    if (rc < 0) {
        sceClibSnprintf(status, sizeof status, "Couldn't start (0x%08X)", (unsigned)rc);
        show(STATUS_ID, status);
    }
}

// The menu was opened: show what happened while it was closed (or with live labels off).
// QuickMenuReborn creates the widgets one by one and calls each one's handler right after
// creating it, so a handler may only touch its own widget: the ones after it don't exist yet
// (setting the status label from the button's handler froze the shell on 2026-11-02).
ONLOAD_HANDLER(on_load) {
    if (!sceClibStrcmp(id, STATUS_ID)) QuickMenuRebornSetWidgetLabel(STATUS_ID, status);
    else if (!sceClibStrcmp(id, VIEW_ID)) QuickMenuRebornSetWidgetLabel(VIEW_ID, view_text);
    else if (!sceClibStrcmp(id, BUTTON_ID)) QuickMenuRebornSetWidgetLabel(BUTTON_ID, button_label);
}

int module_start(SceSize args, void *argp) {
    (void)args, (void)argp;
    load_config();
    QuickMenuRebornSeparator(SEPARATOR_ID, SCE_SEPARATOR_HEIGHT);

    QuickMenuRebornRegisterWidget(TITLE_ID, NULL, text);
    QuickMenuRebornSetWidgetSize(TITLE_ID, SCE_PLANE_WIDTH, 50, 0, 0);
    QuickMenuRebornSetWidgetColor(TITLE_ID, 1, 1, 1, 1);
    QuickMenuRebornSetWidgetLabel(TITLE_ID, "Wi-Fi QR");

    QuickMenuRebornRegisterWidget(BUTTON_ID, NULL, button);
    QuickMenuRebornSetWidgetSize(BUTTON_ID, 320, 75, 0, 0);
    QuickMenuRebornSetWidgetColor(BUTTON_ID, 1, 1, 1, 1);
    QuickMenuRebornSetWidgetLabel(BUTTON_ID, button_label);
    QuickMenuRebornRegisterEventHanlder(BUTTON_ID, QMR_BUTTON_RELEASE_ID, on_button, NULL);
    QuickMenuRebornAssignOnLoadHandler(on_load, BUTTON_ID);

    QuickMenuRebornRegisterWidget(STATUS_ID, NULL, text);
    QuickMenuRebornSetWidgetSize(STATUS_ID, SCE_PLANE_WIDTH, 60, 0, 0);
    QuickMenuRebornSetWidgetColor(STATUS_ID, 1, 1, 1, 1);
    QuickMenuRebornSetWidgetLabel(STATUS_ID, status);
    QuickMenuRebornAssignOnLoadHandler(on_load, STATUS_ID);

    // 12 lines of full-width characters; the height is a guess at the system font's line pitch.
    QuickMenuRebornRegisterWidget(VIEW_ID, NULL, text);
    QuickMenuRebornSetWidgetSize(VIEW_ID, SCE_PLANE_WIDTH, 420, 0, 0);
    QuickMenuRebornSetWidgetColor(VIEW_ID, 1, 1, 1, 1);
    QuickMenuRebornSetWidgetLabel(VIEW_ID, view_text);
    QuickMenuRebornAssignOnLoadHandler(on_load, VIEW_ID);
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
    (void)args, (void)argp;
    // The core may still call back into this module: stop the scan and wait for it.
    if (core.is_busy && core.is_busy()) {
        core.cancel();
        for (int i = 0; core.is_busy() && i < 100; i++) sceKernelDelayThread(50 * 1000);
    }
    QuickMenuRebornUnregisterWidget(VIEW_ID);
    QuickMenuRebornUnregisterWidget(STATUS_ID);
    QuickMenuRebornUnregisterWidget(BUTTON_ID);
    QuickMenuRebornUnregisterWidget(TITLE_ID);
    QuickMenuRebornRemoveSeparator(SEPARATOR_ID);
    return SCE_KERNEL_STOP_SUCCESS;
}

void _start() __attribute__((weak, alias("module_start")));
