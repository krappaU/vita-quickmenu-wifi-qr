// SPDX-License-Identifier: MIT
// A log at ux0:data/wifiqr/log.txt: what the plugin did and every system call that failed, for
// working out what the Vita does on hardware nobody has documented.
#ifndef LOG_H
#define LOG_H

#define WIFIQR_DIR "ux0:data/wifiqr"

#ifdef __cplusplus
extern "C" {
#endif

void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Writes text to a new file in WIFIQR_DIR; returns < 0 on failure.
int file_write_lines_begin(const char *path);
void file_write_line(int fd, const char *line);
void file_write_end(int fd);

#ifdef __cplusplus
}
#endif

#endif
