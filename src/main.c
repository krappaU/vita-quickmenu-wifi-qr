// SPDX-License-Identifier: MIT
// Wi-Fi QR: a Quick Menu widget (via QuickMenuReborn, running in SceShell next to any game) that
// scans a Wi-Fi QR code with the back camera, saves the network and connects to it.
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <quickmenureborn/qm_reborn.h>
#include <stdarg.h>

#include "connect.h"
#include "log.h"
#include "profiles.h"
#include "scan.h"
#include "viewfinder.h"
#include "wifi_qr.h"

#define SEPARATOR_ID "wifiqr_separator"
#define TITLE_ID "wifiqr_title"
#define BUTTON_ID "wifiqr_button"
#define STATUS_ID "wifiqr_status"
#define VIEW_ID "wifiqr_view"

// The process's default user priority. Offsets from it are only allowed in a small range:
// 0x10000100 + 0x40 failed with SCE_KERNEL_ERROR_ILLEGAL_PRIORITY (0x80028023) on 2026-11-02.
#define WORKER_PRIORITY 0x10000100

#define BUTTON_SCAN "Scan Wi-Fi QR"
#define BUTTON_CANCEL "Cancel"

static struct {
    int live_labels;    // update labels from the worker thread while the menu is open
    int resolution;     // 640 or 320
    int scan_seconds;
    int connect_seconds;
    int snapshots;      // save frames that didn't decode, for debugging: they can show the password
    int viewfinder;     // the text viewfinder while scanning
} cfg = {1, 640, 30, 30, 0, 1};

static volatile int busy, cancel;
static char status[256] = "Show the network's QR code (Android: Wi-Fi > Share)";
static char button_label[32] = BUTTON_SCAN;
static char view_text[VIEW_TEXT_MAX] = ""; // the text viewfinder while scanning, empty otherwise

static void show(const char *id, const char *label) {
    if (cfg.live_labels) QuickMenuRebornSetWidgetLabel(id, label);
}

static void set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    sceClibVsnprintf(status, sizeof status, fmt, ap);
    va_end(ap);
    log_printf("status: %s", status);
    show(STATUS_ID, status);
}

static void set_button(const char *label) {
    sceClibStrncpy(button_label, label, sizeof button_label - 1);
    show(BUTTON_ID, button_label);
}

static void load_config(void) {
    char buf[512];
    SceUID fd = sceIoOpen(WIFIQR_DIR "/config.txt", SCE_O_RDONLY, 0);
    if (fd < 0) return;
    int n = sceIoRead(fd, buf, sizeof buf - 1);
    sceIoClose(fd);
    if (n <= 0) return;
    buf[n] = 0;
    for (char *line = buf; line && *line;) {
        char *next = sceClibStrchr(line, '\n');
        if (next) *next++ = 0;
        char *eq = sceClibStrchr(line, '=');
        if (eq) {
            *eq = 0;
            int v = 0;
            for (char *p = eq + 1; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
            if (!sceClibStrcmp(line, "live_labels")) cfg.live_labels = v != 0;
            else if (!sceClibStrcmp(line, "resolution")) cfg.resolution = v == 320 ? 320 : 640;
            else if (!sceClibStrcmp(line, "scan_seconds") && v > 0) cfg.scan_seconds = v;
            else if (!sceClibStrcmp(line, "connect_seconds") && v > 0) cfg.connect_seconds = v;
            else if (!sceClibStrcmp(line, "snapshots")) cfg.snapshots = v != 0;
            else if (!sceClibStrcmp(line, "viewfinder")) cfg.viewfinder = v != 0;
        }
        line = next;
    }
}

static void dump_line(void *ud, const char *line) { file_write_line(*(int *)ud, line); }
static void log_line(void *ud, const char *line) { (void)ud; log_printf("  %s", line); }

// A full copy of the saved networks before the first change of each session (passwords included:
// the registry holds them as plain text too).
static void backup_profiles(void) {
    static int done;
    if (done) return;
    SceDateTime t;
    sceRtcGetCurrentClockLocalTime(&t);
    char path[128];
    sceClibSnprintf(path, sizeof path, WIFIQR_DIR "/backup-%04d%02d%02d-%02d%02d%02d.txt", t.year, t.month,
                    t.day, t.hour, t.minute, t.second);
    int fd = file_write_lines_begin(path);
    if (fd < 0) {
        log_printf("backup %s: 0x%08x", path, fd);
        return;
    }
    profiles_dump(dump_line, &fd, 0);
    file_write_end(fd);
    log_printf("backup written: %s", path);
    done = 1;
}

// The text viewfinder (see viewfinder.h): what the back camera sees, ~3 times a second.
static void on_viewfinder(const uint8_t *luma, int w, int h, const int *cx, const int *cy, int corners) {
    viewfinder_render(luma, w, h, w, cx, cy, corners, view_text, sizeof view_text);
    show(VIEW_ID, view_text);
}

static void view_clear(void) {
    view_text[0] = 0;
    show(VIEW_ID, view_text);
}

static void on_scan_progress(int seconds_left, scan_hint hint) {
    switch (hint) {
    case SCAN_HINT_BLURRY: set_status("Code seen but blurry: hold it a bit further away (%d s)", seconds_left); break;
    case SCAN_HINT_CODE: set_status("QR code in view, hold still... (%d s)", seconds_left); break;
    case SCAN_HINT_PARTIAL: set_status("Part of the code in view: center it in the camera (%d s)", seconds_left); break;
    case SCAN_HINT_SCREEN: set_status("Screen in view, looking for the code... (%d s)", seconds_left); break;
    default:
        set_status("Hold the phone against the camera, then pull it slowly away (%d s)", seconds_left);
        break;
    }
}

static int worker(SceSize args, void *argp) {
    (void)args, (void)argp;
    static char payload[1024];
    log_printf("--- scan (resolution %d, live labels %d)", cfg.resolution, cfg.live_labels);
    static int listed;
    if (!listed) { // slow (~1.5 s per network), so once per session
        log_printf("saved networks (secrets masked):");
        profiles_dump(log_line, NULL, 1);
        listed = 1;
    }

    scan_params sp = {
        .width = cfg.resolution,
        .height = cfg.resolution == 640 ? 480 : 240,
        .timeout_ms = cfg.scan_seconds * 1000,
        .cancel = &cancel,
        .progress = on_scan_progress,
        .snapshots = cfg.snapshots,
        .viewfinder = cfg.viewfinder ? on_viewfinder : NULL,
    };
    int cam_err = 0;
    scan_result sr = scan_qr(&sp, payload, sizeof payload, &cam_err);
    view_clear();
    switch (sr) {
    case SCAN_OK: break;
    case SCAN_CANCELLED: set_status("Cancelled"); goto done;
    case SCAN_TIMEOUT: set_status("No Wi-Fi QR code found. Try again closer, with the screen bright"); goto done;
    case SCAN_NO_MEMORY: set_status("Not enough memory for the camera"); goto done;
    case SCAN_CAMERA_ERROR:
        set_status("Camera unavailable (0x%08X). See ux0:data/wifiqr/log.txt", (unsigned)cam_err);
        goto done;
    }

    wifi_cred c;
    wifi_qr_result pr = wifi_qr_parse(payload, sceClibStrnlen(payload, sizeof payload), &c);
    if (pr != WIFI_QR_OK) {
        log_printf("QR payload starts with: %.12s", payload); // never the whole thing: it has the password
        set_status("%s", wifi_qr_result_str(pr));
        goto done;
    }
    log_printf("QR: \"%s\", %s, password %d chars%s", c.ssid, wifi_sec_str(c.sec),
               (int)sceClibStrnlen(c.password, sizeof c.password), c.hidden ? ", hidden" : "");

    set_status("Saving %s...", c.ssid);
    backup_profiles();
    profiles_outcome po = profiles_save(&c);
    log_printf("save: %s, slot %d, wifi_security %d, reg error 0x%08x", profiles_result_str(po.result), po.slot,
               po.security_value, (unsigned)po.reg_error);
    if (po.result >= PROFILES_ERR_FULL) {
        if (po.result == PROFILES_ERR_REGISTRY) set_status("%s (0x%08X)", profiles_result_str(po.result), (unsigned)po.reg_error);
        else if (po.result == PROFILES_ERR_NO_EXAMPLE) set_status("%s (%s)", profiles_result_str(po.result), wifi_sec_str(c.sec));
        else set_status("%s", profiles_result_str(po.result));
        goto done;
    }

    set_status("%s %s. Connecting...", c.ssid, profiles_result_str(po.result));
    set_button(BUTTON_CANCEL);
    char now[40];
    connect_result cr = connect_now(c.ssid, cfg.connect_seconds * 1000, &cancel, now, sizeof now);
    switch (cr) {
    case CONNECT_TARGET: set_status("Connected to %s", c.ssid); break;
    case CONNECT_OTHER: set_status("Saved %s, but connected to %s", c.ssid, now); break;
    case CONNECT_NONE: set_status("Saved %s, not connected yet (out of range?)", c.ssid); break;
    }

done:
    set_button(BUTTON_SCAN);
    busy = 0;
    return sceKernelExitDeleteThread(0);
}

BUTTON_HANDLER(on_button) {
    (void)id, (void)hash, (void)eventId, (void)userDat;
    if (busy) {
        cancel = 1;
        set_status("Cancelling...");
        return;
    }
    load_config();
    SceUID th = sceKernelCreateThread("wifiqr_worker", worker, WORKER_PRIORITY, 0x10000, 0, 0, NULL);
    if (th < 0) {
        set_status("Couldn't start (0x%08X)", (unsigned)th);
        return;
    }
    busy = 1;
    cancel = 0;
    set_button(BUTTON_CANCEL);
    sceKernelStartThread(th, 0, NULL);
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
    cancel = 1;
    for (int i = 0; busy && i < 100; i++) sceKernelDelayThread(50 * 1000);
    QuickMenuRebornUnregisterWidget(VIEW_ID);
    QuickMenuRebornUnregisterWidget(STATUS_ID);
    QuickMenuRebornUnregisterWidget(BUTTON_ID);
    QuickMenuRebornUnregisterWidget(TITLE_ID);
    QuickMenuRebornRemoveSeparator(SEPARATOR_ID);
    return SCE_KERNEL_STOP_SUCCESS;
}

void _start() __attribute__((weak, alias("module_start")));
