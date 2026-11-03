// SPDX-License-Identifier: MIT
// Opens the system's Wi-Fi Settings popup (impose_net_plugin) the way the shell itself does.
//
// The popup is a PAF plugin the shell loads on demand, with memory it takes from a queue it
// shares with its other popups, and unloads when the popup closes. Its network service asks for
// it through a function the shell doesn't export (docs/impose_net.md, "Loading it"): this file
// finds that function in SceShell's code at the offset it has on firmware 3.65, checks the code
// there is what it expects, and calls it. On any other firmware the check fails and the button
// reports an error instead of jumping somewhere wrong.
#include <paf.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <taihen.h>

#include "log.h"
#include "wifiqr_api.h"

namespace {

// Called when the popup has closed and been unloaded: result 0 when the user connected to a
// network, 0x80102402/0x80102408 when they cancelled. a, b, c are what the popup reports.
typedef void (*popup_done_fn)(int a, int b, int c, int result, void *ud);

// SceShell, firmware 3.65: open(exec_mode, done, userdata). Returns 0 once the request is queued,
// negative if done is NULL. exec_mode is the value the shell's network service passes for the
// usual case (1); 0 makes the popup start with the access point auto-configuration check.
typedef int (*shell_open_fn)(int exec_mode, popup_done_fn done, void *ud);
const unsigned SHELL_OPEN_OFFSET = 0xE26CC; // from the start of SceShell's code segment
// The function's first instructions on 3.65 (push, argument moves, the NULL check, its branch).
const unsigned char SHELL_OPEN_CODE[] = {0x2d, 0xe9, 0xf0, 0x47, 0x5f, 0xea, 0x01, 0x09, 0x92, 0x46, 0x80, 0x46,
                                         0x4f, 0xf0, 0x00, 0x04, 0x02, 0xd1, 0x5f, 0xf0, 0xff, 0x30, 0x10, 0xe1};
const int POPUP_EXEC_MODE = 1;

shell_open_fn shell_open;

int find_shell_open(void) {
    if (shell_open) return 0;
    tai_module_info_t tai;
    tai.size = sizeof tai;
    int rc = taiGetModuleInfo("SceShell", &tai);
    if (rc < 0) {
        log_printf("wifi popup: taiGetModuleInfo(SceShell) 0x%08x", (unsigned)rc);
        return rc;
    }
    SceKernelModuleInfo info;
    sceClibMemset(&info, 0, sizeof info);
    info.size = sizeof info;
    rc = sceKernelGetModuleInfo(tai.modid, &info);
    if (rc < 0) {
        log_printf("wifi popup: sceKernelGetModuleInfo(SceShell) 0x%08x", (unsigned)rc);
        return rc;
    }
    const unsigned char *code = (const unsigned char *)info.segments[0].vaddr + SHELL_OPEN_OFFSET;
    log_printf("wifi popup: SceShell nid %08x, code at %p (%x bytes); open at %p", tai.module_nid,
               info.segments[0].vaddr, info.segments[0].memsz, (const void *)code);
    if (info.segments[0].memsz < SHELL_OPEN_OFFSET + sizeof SHELL_OPEN_CODE ||
        sceClibMemcmp(code, SHELL_OPEN_CODE, sizeof SHELL_OPEN_CODE) != 0) {
        log_printf("wifi popup: the shell's code doesn't match firmware 3.65's");
        return WIFIQR_ERROR_UNSUPPORTED_FIRMWARE;
    }
    shell_open = (shell_open_fn)((unsigned)code | 1); // Thumb
    return 0;
}

void on_popup_done(int a, int b, int c, int result, void *ud) {
    (void)ud;
    log_printf("wifi popup closed: result 0x%08x (%d %d %d)", (unsigned)result, a, b, c);
}

} // namespace

extern "C" int wifiqrOpenWifiSettings(void) {
    if (paf::Plugin::Find("impose_net_plugin")) {
        log_printf("wifi popup: already open");
        return WIFIQR_ERROR_BUSY;
    }
    int rc = find_shell_open();
    if (rc < 0) return rc;
    rc = shell_open(POPUP_EXEC_MODE, on_popup_done, NULL);
    log_printf("wifi popup: open -> 0x%08x", (unsigned)rc);
    return rc;
}
