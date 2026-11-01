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

// Memory the camera writes into. It isn't documented which kinds the camera accepts from a
// SceShell plugin, so try them in turn and log the one that worked. All are uncached, so the CPU
// sees what the camera wrote.
static SceUID alloc_camera_block(size_t size, void **base) {
    static const struct { int type; size_t align; const char *name; } kinds[] = {
        {SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, 0x100000, "PHYCONT_NC"},
        {SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 0x40000, "CDRAM"},
        {SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 0x1000, "RW_UNCACHE"},
    };
    for (unsigned i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        size_t sz = (size + kinds[i].align - 1) & ~(kinds[i].align - 1);
        SceUID uid = sceKernelAllocMemBlock("wifiqr_cam", kinds[i].type, sz, NULL);
        if (uid >= 0) {
            sceKernelGetMemBlockBase(uid, base);
            log_printf("camera buffer: %s, %u KB", kinds[i].name, (unsigned)(sz / 1024));
            return uid;
        }
        log_printf("camera buffer %s: 0x%08x", kinds[i].name, uid);
    }
    return -1;
}

scan_result scan_qr(const scan_params *p, char *payload, int payload_size, int *camera_error) {
    const int w = p->width, h = p->height;
    const int res = w == 640 ? SCE_CAMERA_RESOLUTION_640_480 : SCE_CAMERA_RESOLUTION_320_240;
    const size_t ysize = (size_t)w * h, csize = ysize / 4;
    scan_result result = SCAN_TIMEOUT;
    *camera_error = 0;

    // quirc's image plus its working memory: ~2x the frame is plenty.
    int rc = arena_open(ysize * 2 + 256 * 1024);
    if (rc < 0) {
        log_printf("heap: 0x%08x", rc);
        return SCAN_NO_MEMORY;
    }
    qr_decoder *dec = qr_decoder_new(w, h);
    void *cam_base = NULL;
    SceUID cam_uid = dec ? alloc_camera_block(ysize + 2 * csize, &cam_base) : -1;
    if (!dec || cam_uid < 0) {
        arena_close();
        return SCAN_NO_MEMORY;
    }

    uint8_t *ybuf = cam_base;
    SceCameraInfo info;
    sceClibMemset(&info, 0, sizeof info);
    info.size = sizeof info;
    info.priority = SCE_CAMERA_PRIORITY_SHARE;
    info.format = SCE_CAMERA_FORMAT_YUV420_PLANE; // the Y plane is the grayscale image quirc wants
    info.resolution = res;
    info.framerate = SCE_CAMERA_FRAMERATE_30_FPS;
    info.sizeIBase = ysize;
    info.sizeUBase = csize;
    info.sizeVBase = csize;
    info.pIBase = ybuf;
    info.pUBase = ybuf + ysize;
    info.pVBase = ybuf + ysize + csize;
    info.pitch = 0;

    rc = sceCameraOpen(SCE_CAMERA_DEVICE_BACK, &info);
    if (rc < 0) {
        log_printf("sceCameraOpen: 0x%08x", rc);
        *camera_error = rc;
        result = SCAN_CAMERA_ERROR;
        goto out_free;
    }
    rc = sceCameraStart(SCE_CAMERA_DEVICE_BACK);
    if (rc < 0) {
        log_printf("sceCameraStart: 0x%08x", rc);
        *camera_error = rc;
        result = SCAN_CAMERA_ERROR;
        goto out_close;
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
        int n = qr_decode(dec, ybuf, w, payload, payload_size, &seen);
        in_view = seen > 0;
        if (n > 0) {
            result = SCAN_OK;
            break;
        }
        sceKernelDelayThread(1000); // let the shell's own threads run between frames
    }
    log_printf("scan: %d frames in %u ms, result %d", frames,
               (unsigned)((sceKernelGetProcessTimeWide() - start) / 1000), result);

    sceCameraStop(SCE_CAMERA_DEVICE_BACK);
out_close:
    sceCameraClose(SCE_CAMERA_DEVICE_BACK);
out_free:
    sceKernelFreeMemBlock(cam_uid);
    qr_decoder_free(dec);
    arena_close();
    return result;
}
