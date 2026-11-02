// SPDX-License-Identifier: MIT
#include "scan.h"

#include <psp2/camera.h>
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
                    log_printf("camera %dx%d %s %u KB: started", w, h, mk->name, (unsigned)(sz / 1024));
                    cam->uid = uid, cam->y = y, cam->w = w, cam->h = h;
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

    SceUInt64 start = sceKernelGetProcessTimeWide(), last_report = 0;
    int frames = 0, in_view = 0;
    for (;;) {
        if (p->cancel && *p->cancel) {
            result = SCAN_CANCELLED;
            break;
        }
        SceUInt64 elapsed = (sceKernelGetProcessTimeWide() - start) / 1000;
        if (elapsed >= (SceUInt64)p->timeout_ms) break;
        if (p->progress && (frames == 0 || elapsed - last_report >= 1000)) {
            last_report = elapsed;
            p->progress((int)((p->timeout_ms - elapsed + 999) / 1000), in_view);
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
        frames++;
        int seen = 0;
        int n = qr_decode(dec, cam.y, cam.w, payload, payload_size, &seen);
        in_view = seen > 0;
        if (n > 0) {
            result = SCAN_OK;
            break;
        }
        sceKernelDelayThread(1000); // let the shell's own threads run between frames
    }
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
