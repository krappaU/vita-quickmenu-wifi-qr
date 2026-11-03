// SPDX-License-Identifier: MIT
// Wi-Fi QR core: scans a Wi-Fi QR code with the back camera, saves the network the way the
// system's Wi-Fi Settings does and connects to it. Loaded into SceShell by taiHEN (*main), it has
// no UI of its own: front ends such as the Quick Menu button (qm.c) call its API (wifiqr_api.h)
// and show what it reports.
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <stdarg.h>

#include "connect.h"
#include "log.h"
#include "net_dyn.h"
#include "profiles.h"
#include "scan.h"
#include "viewfinder.h"
#include "wifiqr_api.h"
#include "wifiqr_kernel.h"

// Exported by SceShell to its plugins (stubs/SceShell.yml): hands saved profiles to the shell's
// cached copy of the networks. See profiles_shell_entry.
int SceShell_60129092(void *profiles, unsigned int count);
#include "wifi_qr.h"

// The process's default user priority. Offsets from it are only allowed in a small range:
// 0x10000100 + 0x40 failed with SCE_KERNEL_ERROR_ILLEGAL_PRIORITY (0x80028023) on 2026-11-02.
#define WORKER_PRIORITY 0x10000100

static struct {
    int resolution;     // 640 or 320
    int scan_seconds;
    int connect_seconds;
    int snapshots;      // save frames that didn't decode, for debugging: they can show the password
    int viewfinder;     // the text viewfinder while scanning
} cfg = {640, 30, 30, 0, 1};

static volatile int busy, cancel;
static WifiQrListener listener; // the front end that started the current scan
static char status[256];
static char view_text[VIEW_TEXT_MAX] = ""; // the text viewfinder while scanning, empty otherwise

static void set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    sceClibVsnprintf(status, sizeof status, fmt, ap);
    va_end(ap);
    log_printf("status: %s", status);
    if (listener.status) listener.status(status, listener.ud);
}

static void show_view(const char *text) {
    if (listener.view) listener.view(text, listener.ud);
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
            if (!sceClibStrcmp(line, "resolution")) cfg.resolution = v == 320 ? 320 : 640;
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

// The picture preview, drawn over the Quick Menu by the kernel helper (wifiqr_kernel.skprx).
// 1 untried, 2 working, 0 unavailable (not installed, or its display hook failed).
static int kpreview = 1;

static void on_preview(const uint8_t *luma, int w, int h, const int *cx, const int *cy, int corners) {
    static uint8_t small[PREVIEW_W * PREVIEW_H];
    if (!kpreview) return;
    if (kpreview == 1 && !import_linked((const void *)&wifiqrPreviewSet)) {
        // Weak import, kernel helper not installed: its stub would jump to address 0.
        log_printf("picture preview unavailable (kernel helper not loaded): using the text viewfinder");
        kpreview = 0;
        return;
    }
    // Box-average down to the preview size (2x2 from 640x480; 1:1 from 320x240).
    const int sx = w / PREVIEW_W, sy = h / PREVIEW_H;
    if (sx < 1 || sy < 1) return;
    if (sx == 2 && sy == 2) { // 640x480: shifts, not a software divide per pixel
        for (int y = 0; y < PREVIEW_H; y++) {
            const uint8_t *a = luma + 2 * y * w, *b = a + w;
            uint8_t *o = small + y * PREVIEW_W;
            for (int x = 0; x < PREVIEW_W; x++) o[x] = (uint8_t)((a[2 * x] + a[2 * x + 1] + b[2 * x] + b[2 * x + 1]) >> 2);
        }
    } else {
        const unsigned n = (unsigned)(sx * sy);
        for (int y = 0; y < PREVIEW_H; y++)
            for (int x = 0; x < PREVIEW_W; x++) {
                unsigned sum = 0;
                for (int j = 0; j < sy; j++)
                    for (int i = 0; i < sx; i++) sum += luma[(y * sy + j) * w + x * sx + i];
                small[y * PREVIEW_W + x] = (uint8_t)(n == 1 ? sum : sum / n);
            }
    }
    int c[8];
    for (int i = 0; i < corners && i < 4; i++) c[2 * i] = cx[i] / sx, c[2 * i + 1] = cy[i] / sy;
    int rc = wifiqrPreviewSet(small, PREVIEW_W, PREVIEW_H, c, corners);
    if (rc < 0) {
        log_printf("picture preview unavailable (0x%08x): using the text viewfinder", (unsigned)rc);
        kpreview = 0;
    } else if (kpreview == 1) {
        log_printf("picture preview: on");
        kpreview = 2;
    }
}

// The text viewfinder (see viewfinder.h): what the back camera sees, ~3 times a second. Only
// when the picture preview isn't working.
static void on_viewfinder(const uint8_t *luma, int w, int h, const int *cx, const int *cy, int corners) {
    if (kpreview == 2) return;
    viewfinder_render(luma, w, h, w, cx, cy, corners, view_text, sizeof view_text);
    show_view(view_text);
}

static void view_clear(void) {
    view_text[0] = 0;
    show_view(view_text);
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
    log_printf("--- scan (resolution %d)", cfg.resolution);
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
        .preview = cfg.viewfinder ? on_preview : NULL,
    };
    int cam_err = 0;
    scan_result sr = scan_qr(&sp, payload, sizeof payload, &cam_err);
    if (kpreview == 2) wifiqrPreviewOff();
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
    log_printf("QR: \"%s\", %s%s, password %d chars%s", c.ssid, wifi_sec_str(c.sec),
               c.wpa3_only ? " (code says WPA3 only)" : c.wpa3 ? " (code says WPA3: trying WPA2)" : "",
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

    // Update the shell's cached copy of the networks, as the Wi-Fi Settings popup does after
    // writing the registry: without it the new profile only works after a reboot (2026-11-02).
    {
        static unsigned char entry[SHELL_PROFILE_SIZE];
        profiles_shell_entry(entry, po.slot, &c, po.security_value);
        int rc = SceShell_60129092(entry, 1);
        log_printf("shell profile update (slot %d): 0x%08x", po.slot, (unsigned)rc);
    }

    set_status("%s %s. Connecting...", c.ssid, profiles_result_str(po.result));
    char now[40];
    connect_result cr = connect_now(c.ssid, cfg.connect_seconds * 1000, &cancel, now, sizeof now);
    switch (cr) {
    case CONNECT_TARGET: set_status("Connected to %s", c.ssid); break;
    case CONNECT_OTHER: set_status("Saved %s, but connected to %s", c.ssid, now); break;
    case CONNECT_NONE:
        if (c.wpa3) set_status("Saved %s, not connected: it may be WPA3-only (the Vita needs WPA2)", c.ssid);
        else set_status("Saved %s, not connected yet (out of range?)", c.ssid);
        break;
    }

done:
    busy = 0;
    if (listener.busy) listener.busy(0, listener.ud);
    return sceKernelExitDeleteThread(0);
}

// --- API (wifiqr_api.h) -----------------------------------------------------------------------

int wifiqrScan(const WifiQrListener *l) {
    log_printf("api: scan requested (listener %p, size %u)", (const void *)l, l ? l->size : 0);
    if (busy) return WIFIQR_ERROR_BUSY;
    sceClibMemset(&listener, 0, sizeof listener);
    if (l) sceClibMemcpy(&listener, l, l->size < sizeof listener ? l->size : sizeof listener);
    load_config();
    log_printf("api: config read, starting the worker");
    SceUID th = sceKernelCreateThread("wifiqr_worker", worker, WORKER_PRIORITY, 0x10000, 0, 0, NULL);
    if (th < 0) {
        log_printf("worker thread: 0x%08x", (unsigned)th);
        return th;
    }
    busy = 1;
    cancel = 0;
    if (listener.busy) listener.busy(1, listener.ud);
    int rc = sceKernelStartThread(th, 0, NULL);
    log_printf("api: worker 0x%08x started: 0x%08x", (unsigned)th, (unsigned)rc);
    return 0;
}

int wifiqrCancel(void) {
    if (busy) {
        cancel = 1;
        set_status("Cancelling...");
    }
    return 0;
}

int wifiqrIsBusy(void) { return busy; }

int module_start(SceSize args, void *argp) {
    (void)args, (void)argp;
    // Nothing until a front end asks: this runs early in the shell's start-up. One log line, to
    // tell from the log whether a crash came before or after the core loaded.
    log_printf("core (wifiqr) loaded");
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
    (void)args, (void)argp;
    cancel = 1;
    for (int i = 0; busy && i < 100; i++) sceKernelDelayThread(50 * 1000);
    return SCE_KERNEL_STOP_SUCCESS;
}

void _start() __attribute__((weak, alias("module_start")));
