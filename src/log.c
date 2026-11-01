// SPDX-License-Identifier: MIT
#include "log.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <stdarg.h>

static SceUID log_lock = -1;

static void ensure_dir(void) { sceIoMkdir(WIFIQR_DIR, 0777); }

void log_printf(const char *fmt, ...) {
    char line[512];
    SceDateTime t;
    sceRtcGetCurrentClockLocalTime(&t);
    int n = sceClibSnprintf(line, sizeof line, "%04d-%02d-%02d %02d:%02d:%02d ", t.year, t.month, t.day,
                            t.hour, t.minute, t.second);
    va_list ap;
    va_start(ap, fmt);
    sceClibVsnprintf(line + n, sizeof line - n - 1, fmt, ap);
    va_end(ap);
    n = sceClibStrnlen(line, sizeof line - 2);
    line[n++] = '\n';

    if (log_lock < 0) log_lock = sceKernelCreateMutex("wifiqr_log", 0, 0, NULL);
    if (log_lock >= 0) sceKernelLockMutex(log_lock, 1, NULL);
    ensure_dir();
    SceUID fd = sceIoOpen(WIFIQR_DIR "/log.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, line, n);
        sceIoClose(fd);
    }
    if (log_lock >= 0) sceKernelUnlockMutex(log_lock, 1);
}

int file_write_lines_begin(const char *path) {
    ensure_dir();
    return sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
}

void file_write_line(int fd, const char *line) {
    if (fd < 0) return;
    sceIoWrite(fd, line, sceClibStrnlen(line, 1024));
    sceIoWrite(fd, "\n", 1);
}

void file_write_end(int fd) {
    if (fd >= 0) sceIoClose(fd);
}
