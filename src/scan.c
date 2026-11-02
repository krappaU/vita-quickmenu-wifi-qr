// SPDX-License-Identifier: MIT
#include "scan.h"

#include <psp2/camera.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "libc_shim.h"
#include "log.h"
#include "qr_decode.h"

// The camera writes frames straight into memory, so sceCameraStart wants a buffer that is
// physically contiguous: ordinary user memory fails with SCE_KERNEL_ERROR_VARANGE_IS_NOT_PHYSICAL_CONTINUOUS
// (0x80024003). In SceShell (logs of 2026-11-02) the PHYCONT memory type is refused with
// SCE_KERNEL_ERROR_SYSMEM_ADDRESS_SPACE_ERROR (0x80024A00) and CDRAM is full at 512 KB
// (0x80024309). So try every kind that can give contiguous memory, at both resolutions, and log
// each result. All are uncached, so the CPU reads what the camera wrote.
typedef struct {
    const char *name;
    int type;
    unsigned attr;     // SceKernelAllocMemBlockOpt attr, 0 for none
    size_t align;      // block size granularity of the type
} mem_kind;

static const mem_kind KINDS[] = {
    {"PHYCONT_NC", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, 0, 0x100000},
    {"RW_UNCACHE+ATTR_PHYCONT", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_PHYCONT, 0x100000},
    {"MAIN_CDIALOG_NC", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_CDIALOG_NC_RW, 0, 0x40000},
    {"CDIALOG_NC", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDIALOG_NC_RW, 0, 0x40000},
    {"CDRAM", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 0, 0x40000},
};

typedef struct {
    SceUID uid;
    uint8_t *y;   // the Y plane: the grayscale image
    size_t size;  // of the memory block
    int w, h;
} camera;

static void log_free_memory(void) {
    SceKernelFreeMemorySizeInfo fm;
    sceClibMemset(&fm, 0, sizeof fm);
    fm.size = sizeof fm;
    if (sceKernelGetFreeMemorySize(&fm) >= 0)
        log_printf("free memory: user %d KB, cdram %d KB, phycont %d KB", fm.size_user / 1024,
                   fm.size_cdram / 1024, fm.size_phycont / 1024);
}

// Opens and starts the back camera with the first memory kind it accepts. Returns 0, or the last
// error from the camera.
static int camera_start(camera *cam, int prefer_640) {
    static const int sizes[][2] = {{640, 480}, {320, 240}};
    int last_err = -1;
    log_free_memory();
    for (int si = prefer_640 ? 0 : 1; si < 2; si++) {
        const int w = sizes[si][0], h = sizes[si][1];
        const size_t ysize = (size_t)w * h, csize = ysize / 4;
        for (unsigned k = 0; k < sizeof KINDS / sizeof KINDS[0]; k++) {
            const mem_kind *mk = &KINDS[k];
            size_t sz = (ysize + 2 * csize + mk->align - 1) & ~(mk->align - 1);
            SceKernelAllocMemBlockOpt opt;
            sceClibMemset(&opt, 0, sizeof opt);
            opt.size = sizeof opt;
            opt.attr = mk->attr;
            SceUID uid = sceKernelAllocMemBlock("wifiqr_cam", mk->type, sz, mk->attr ? &opt : NULL);
            if (uid < 0) {
                log_printf("camera %dx%d %s %u KB: alloc 0x%08x", w, h, mk->name, (unsigned)(sz / 1024), uid);
                continue;
            }
            void *base = NULL;
            sceKernelGetMemBlockBase(uid, &base);
            uint8_t *y = base;
            sceClibMemset(y, 0x55, ysize); // a marker: shows in the log if the camera never writes

            SceCameraInfo info;
            sceClibMemset(&info, 0, sizeof info);
            info.size = sizeof info;
            info.priority = SCE_CAMERA_PRIORITY_SHARE;
            info.format = SCE_CAMERA_FORMAT_YUV420_PLANE;
            info.resolution = w == 640 ? SCE_CAMERA_RESOLUTION_640_480 : SCE_CAMERA_RESOLUTION_320_240;
            info.framerate = SCE_CAMERA_FRAMERATE_30_FPS;
            info.sizeIBase = ysize;
            info.sizeUBase = csize;
            info.sizeVBase = csize;
            info.pIBase = y;
            info.pUBase = y + ysize;
            info.pVBase = y + ysize + csize;

            int rc = sceCameraOpen(SCE_CAMERA_DEVICE_BACK, &info);
            if (rc >= 0) {
                rc = sceCameraStart(SCE_CAMERA_DEVICE_BACK);
                if (rc >= 0) {
                    log_printf("camera %dx%d %s %u KB: started (info: %ux%u pitch %u buffer %u, I %p U %p V %p)", w, h,
                               mk->name, (unsigned)(sz / 1024), info.width, info.height, info.pitch, info.buffer,
                               info.pIBase, info.pUBase, info.pVBase);
                    cam->uid = uid, cam->y = y, cam->size = sz, cam->w = w, cam->h = h;
                    return 0;
                }
                log_printf("camera %dx%d %s %u KB: sceCameraStart 0x%08x", w, h, mk->name, (unsigned)(sz / 1024), rc);
                sceCameraClose(SCE_CAMERA_DEVICE_BACK);
            } else {
                log_printf("camera %dx%d %s %u KB: sceCameraOpen 0x%08x", w, h, mk->name, (unsigned)(sz / 1024), rc);
            }
            last_err = rc;
            sceKernelFreeMemBlock(uid);
        }
    }
    return last_err;
}

// Brightness of a frame, sampled: enough to tell a real picture from an empty buffer (still
// 0x55 everywhere: the camera never wrote) and to spot a phone screen.
typedef struct {
    unsigned min, max, mean;
    unsigned marker_pct;  // share of pixels still 0x55
    unsigned bright_pct;  // share of pixels in the bright area (a lit screen), 0 if none
} frame_stats;

static frame_stats measure(const uint8_t *y, int w, int h) {
    frame_stats f = {255, 0, 0, 0, 0};
    unsigned sum = 0, n = 0, marker = 0;
    for (int i = 0; i < w * h; i += 97) {
        unsigned v = y[i];
        if (v < f.min) f.min = v;
        if (v > f.max) f.max = v;
        if (v == 0x55) marker++;
        sum += v, n++;
    }
    f.mean = sum / n;
    f.marker_pct = marker * 100 / n;
    // A lit phone screen is the brightest thing in a room: pixels near the frame's maximum, when
    // that maximum is clearly above the average (snapshots of 2026-11-02 peaked at 84-135 without one).
    if (f.max >= 150 && f.max >= f.mean + 60) {
        unsigned t = f.max - 50, bright = 0;
        for (int i = 0; i < w * h; i += 97) bright += y[i] >= t;
        f.bright_pct = bright * 100 / n;
    }
    return f;
}

static scan_hint hint_for(const frame_stats *f, const qr_stats *q) {
    if (q->codes > 0 || q->capstones >= 3) return SCAN_HINT_CODE;
    if (q->capstones > 0) return SCAN_HINT_PARTIAL;
    if (f->bright_pct >= 2) return SCAN_HINT_SCREEN;
    return SCAN_HINT_NOTHING;
}

// Saves the grayscale frame as ux0:data/wifiqr/frame-<name>.pgm, to see what the camera saw.
static void save_pgm(const char *name, const uint8_t *y, int w, int h) {
    char path[64], hdr[32];
    sceClibSnprintf(path, sizeof path, WIFIQR_DIR "/frame-%s.pgm", name);
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) {
        log_printf("save %s: 0x%08x", path, fd);
        return;
    }
    int n = sceClibSnprintf(hdr, sizeof hdr, "P5 %d %d 255\n", w, h);
    sceIoWrite(fd, hdr, n);
    sceIoWrite(fd, y, (SceSize)w * h);
    sceIoClose(fd);
    log_printf("saved %s", path);
}

scan_result scan_qr(const scan_params *p, char *payload, int payload_size, int *camera_error) {
    camera cam = {.uid = -1};
    *camera_error = camera_start(&cam, p->width == 640);
    if (*camera_error < 0) return SCAN_CAMERA_ERROR;

    // quirc's image plus its working memory: ~2x the frame is plenty.
    const size_t ysize = (size_t)cam.w * cam.h;
    scan_result result = SCAN_TIMEOUT;
    int rc = arena_open(ysize * 2 + 256 * 1024);
    qr_decoder *dec = rc >= 0 ? qr_decoder_new(cam.w, cam.h) : NULL;
    if (!dec) {
        log_printf("decoder heap: 0x%08x", rc);
        result = SCAN_NO_MEMORY;
        goto out_stop;
    }

    static const char *const hint_names[] = {"nothing", "screen", "partial", "code"};
    SceUInt64 start = sceKernelGetProcessTimeWide(), last_report = 0, last_diag = 0;
    int frames = 0, snapped = 0; // snapped: bit per hint whose first frame was saved
    scan_hint best = SCAN_HINT_NOTHING; // the most promising hint since the last report
    uint8_t *y = cam.y;
    for (;;) {
        if (p->cancel && *p->cancel) {
            result = SCAN_CANCELLED;
            break;
        }
        SceUInt64 elapsed = (sceKernelGetProcessTimeWide() - start) / 1000;
        if (elapsed >= (SceUInt64)p->timeout_ms) break;
        // Twice a second, so the hint follows the user's hand; the best of the period, so a
        // code seen in one frame out of ten still shows.
        if (p->progress && (frames == 0 || elapsed - last_report >= 500)) {
            last_report = elapsed;
            p->progress((int)((p->timeout_ms - elapsed + 999) / 1000), best);
            best = SCAN_HINT_NOTHING;
        }

        SceCameraRead rd;
        sceClibMemset(&rd, 0, sizeof rd);
        rd.size = sizeof rd;
        rd.mode = 0; // wait for the next frame
        rc = sceCameraRead(SCE_CAMERA_DEVICE_BACK, &rd);
        if (rc < 0) {
            log_printf("sceCameraRead: 0x%08x", rc);
            *camera_error = rc;
            result = SCAN_CAMERA_ERROR;
            break;
        }
        // The read may say where this frame is (with several buffers it isn't always the start).
        y = cam.y;
        if (rd.pIBase && (uint8_t *)rd.pIBase >= cam.y && (uint8_t *)rd.pIBase + ysize <= cam.y + cam.size)
            y = rd.pIBase;
        frames++;
        SceUInt64 t0 = sceKernelGetProcessTimeWide();
        qr_stats q = {0, 0};
        int n = qr_decode(dec, y, cam.w, payload, payload_size, &q);
        unsigned decode_ms = (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000);
        frame_stats f = measure(y, cam.w, cam.h);
        scan_hint h = hint_for(&f, &q);
        if (h > best) best = h;

        if (frames <= 2 || elapsed - last_diag >= 1000 || (h > SCAN_HINT_NOTHING && !(snapped & (1 << h)))) {
            last_diag = elapsed;
            log_printf("frame %u at %u ms: Y min %u max %u mean %u, bright %u%%, 0x55 %u%%, corners %d, codes %d, "
                       "decode %u ms -> %s",
                       (unsigned)rd.frame, (unsigned)elapsed, f.min, f.max, f.mean, f.bright_pct, f.marker_pct,
                       q.capstones, q.codes, decode_ms, hint_names[h]);
        }
        // The first frame of each kind, to see why it didn't decode.
        if (h > SCAN_HINT_NOTHING && !(snapped & (1 << h)) && n == 0) {
            snapped |= 1 << h;
            save_pgm(hint_names[h], y, cam.w, cam.h);
        }
        if (n > 0) {
            result = SCAN_OK;
            break;
        }
        sceKernelDelayThread(1000); // let the shell's own threads run between frames
    }
    if (result != SCAN_OK && frames > 0) save_pgm("last", y, cam.w, cam.h);
    log_printf("scan: %d frames at %dx%d in %u ms, result %d", frames, cam.w, cam.h,
               (unsigned)((sceKernelGetProcessTimeWide() - start) / 1000), result);

out_stop:
    sceCameraStop(SCE_CAMERA_DEVICE_BACK);
    sceCameraClose(SCE_CAMERA_DEVICE_BACK);
    sceKernelFreeMemBlock(cam.uid);
    qr_decoder_free(dec);
    arena_close();
    return result;
}
