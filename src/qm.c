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

#include "log.h"
#include "viewfinder.h"
#include "wifiqr_api.h"

// paf::Plugin::Find and paf::Plugin::GetInterface (stubs/ScePafToplevel.yml), called by their
// mangled names: a member function takes the object as its first argument.
void *_ZN3paf6Plugin4FindEPKc(const char *name);
void *_ZNK3paf6Plugin12GetInterfaceEi(const void *plugin, int version);

// Not the IDs the single-plugin version (wifiqr.suprx in ur0:QuickMenuReborn/) used: if that is
// still installed, two widgets with one ID would corrupt QuickMenuReborn's widget list.
#define SEPARATOR_ID "wifiqr_qm_separator"
#define TITLE_ID "wifiqr_qm_title"
#define BUTTON_ID "wifiqr_qm_button"
#define STATUS_ID "wifiqr_qm_status"
#define VIEW_ID "wifiqr_qm_view"
#define WIFI_BUTTON_ID "wifiqr_qm_wifi_button"
#define TEST_BUTTON_ID "wifiqr_qm_test_button"

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
    log_printf("core functions: scan %08x cancel %08x busy %08x", (unsigned)f[0], (unsigned)f[1], (unsigned)f[2]);
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

// --- the system's Wi-Fi Settings popup ---------------------------------------------------------
//
// impose_net_plugin (the popup, in SceShell) publishes a version-1 interface of two functions
// (3.65 build: SetInterface(1, table) in its start-up code):
//   open(callback, userdata): returns 0 or an error (0x80102403 with a NULL callback or while
//     busy); when the popup closes it calls callback(state 0x38, 0x3c, 0x40, result, userdata)
//   close(): closes it with result 0x80102402
typedef void (*popup_done_fn)(int a, int b, int c, int result, void *ud);
typedef struct {
    int (*open)(popup_done_fn done, void *ud);
    int (*close)(void);
} impose_net_iface;

static void on_popup_done(int a, int b, int c, int result, void *ud) {
    (void)ud;
    log_printf("wifi popup closed: result 0x%08x (%d %d %d)", (unsigned)result, a, b, c);
}

static void open_wifi_popup(void) {
    void *plugin = _ZN3paf6Plugin4FindEPKc("impose_net_plugin");
    const impose_net_iface *iface = plugin ? _ZNK3paf6Plugin12GetInterfaceEi(plugin, 1) : NULL;
    log_printf("wifi popup: plugin %p, interface %p", plugin, (const void *)iface);
    if (!iface) {
        sceClibSnprintf(status, sizeof status, "The Wi-Fi popup isn't loaded (plugin %p)", plugin);
        show(STATUS_ID, status);
        return;
    }
    QuickMenuRebornCloseMenu(); // the popup opens over the menu otherwise
    int rc = iface->open(on_popup_done, NULL);
    log_printf("wifi popup: open -> 0x%08x", (unsigned)rc);
    if (rc < 0) sceClibSnprintf(status, sizeof status, "Couldn't open the Wi-Fi popup (0x%08X)", (unsigned)rc);
}

// --- diagnostic: the core's self-test (src/selftest.c), logged from here ---------------------

static void on_selftest_report(const char *text, void *ud) {
    (void)ud;
    log_printf("%s", text);
}

BUTTON_HANDLER(on_test_button) {
    (void)id, (void)hash, (void)eventId, (void)userDat;
    uintptr_t f = 0;
    int rc = taiGetModuleExportFunc(WIFIQR_MODULE, WIFIQR_LIB_NID, WIFIQR_NID_SELFTEST, &f);
    log_printf("self-test button: lookup 0x%08x, function %08x", (unsigned)rc, (unsigned)f);
    if (rc < 0) {
        // Not found: list what is loaded in this process, to see whether the core is there.
        static SceUID ids[256];
        SceSize n = 256;
        int lrc = sceKernelGetModuleList(0xFF, ids, &n);
        log_printf("modules in this process: 0x%08x, %u", (unsigned)lrc, (unsigned)n);
        for (SceSize i = 0; lrc >= 0 && i < n; i++) {
            static SceKernelModuleInfo info;
            sceClibMemset(&info, 0, sizeof info);
            info.size = sizeof info;
            if (sceKernelGetModuleInfo(ids[i], &info) >= 0)
                log_printf("  %-27s %p %s", info.module_name, info.segments[0].vaddr, info.path);
        }
        sceClibSnprintf(status, sizeof status, "Core not found (0x%08X): module list in the log", (unsigned)rc);
        show(STATUS_ID, status);
        return;
    }
    rc = ((wifiqrSelfTest_fn)f)(on_selftest_report, NULL);
    log_printf("self-test button: returned 0x%08x", (unsigned)rc);
    sceClibSnprintf(status, sizeof status, "Self-test finished (0x%08X): see ux0:data/wifiqr/log.txt", (unsigned)rc);
    show(STATUS_ID, status);
}

// --- widgets ----------------------------------------------------------------------------------

BUTTON_HANDLER(on_button) {
    (void)id, (void)hash, (void)eventId, (void)userDat;
    int rc = find_core();
    log_printf("scan button: core lookup 0x%08x", (unsigned)rc);
    if (rc < 0) {
        sceClibSnprintf(status, sizeof status, "Wi-Fi QR core not loaded: wifiqr.suprx under *main, wifiqr_core.suprx next to it (0x%08X)",
                        (unsigned)rc);
        show(STATUS_ID, status);
        return;
    }
    log_printf("scan button: asking the core whether it is busy");
    if (core.is_busy()) {
        core.cancel();
        return;
    }
    load_config();
    log_printf("scan button: starting the scan");
    rc = core.scan(&listener);
    log_printf("scan button: core returned 0x%08x", (unsigned)rc);
    if (rc < 0) {
        sceClibSnprintf(status, sizeof status, "Couldn't start (0x%08X)", (unsigned)rc);
        show(STATUS_ID, status);
    }
}

BUTTON_HANDLER(on_wifi_button) {
    (void)id, (void)hash, (void)eventId, (void)userDat;
    open_wifi_popup();
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
    log_printf("button plugin (wifiqr_qm) loaded");
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

    QuickMenuRebornRegisterWidget(WIFI_BUTTON_ID, NULL, button);
    QuickMenuRebornSetWidgetSize(WIFI_BUTTON_ID, 320, 75, 0, 0);
    QuickMenuRebornSetWidgetColor(WIFI_BUTTON_ID, 1, 1, 1, 1);
    QuickMenuRebornSetWidgetLabel(WIFI_BUTTON_ID, "Wi-Fi Settings");
    QuickMenuRebornRegisterEventHanlder(WIFI_BUTTON_ID, QMR_BUTTON_RELEASE_ID, on_wifi_button, NULL);

    QuickMenuRebornRegisterWidget(TEST_BUTTON_ID, NULL, button);
    QuickMenuRebornSetWidgetSize(TEST_BUTTON_ID, 320, 75, 0, 0);
    QuickMenuRebornSetWidgetColor(TEST_BUTTON_ID, 1, 1, 1, 1);
    QuickMenuRebornSetWidgetLabel(TEST_BUTTON_ID, "Self-test");
    QuickMenuRebornRegisterEventHanlder(TEST_BUTTON_ID, QMR_BUTTON_RELEASE_ID, on_test_button, NULL);

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
    QuickMenuRebornUnregisterWidget(TEST_BUTTON_ID);
    QuickMenuRebornUnregisterWidget(WIFI_BUTTON_ID);
    QuickMenuRebornUnregisterWidget(BUTTON_ID);
    QuickMenuRebornUnregisterWidget(TITLE_ID);
    QuickMenuRebornRemoveSeparator(SEPARATOR_ID);
    return SCE_KERNEL_STOP_SUCCESS;
}

void _start() __attribute__((weak, alias("module_start")));
