// SPDX-License-Identifier: MIT
// The system's Wi-Fi Settings popup (impose_net_plugin, a PAF plugin in SceShell), through the
// interface it publishes: see docs/impose_net.md.
#include <paf.h>

#include "log.h"
#include "wifiqr_api.h"

namespace {

// impose_net_plugin's version-1 plugin interface (3.65: SetInterface(1, table) at start-up).
typedef void (*popup_done_fn)(int a, int b, int c, int result, void *ud);
struct impose_net_iface {
    int (*open)(popup_done_fn done, void *ud); // 0, or 0x80102403 (NULL callback, or busy)
    int (*close)(void);                        // closes it with result 0x80102402
};

void on_popup_done(int a, int b, int c, int result, void *ud) {
    (void)ud;
    log_printf("wifi popup closed: result 0x%08x (%d %d %d)", (unsigned)result, a, b, c);
}

} // namespace

extern "C" int wifiqrOpenWifiSettings(void) {
    paf::Plugin *plugin = paf::Plugin::Find("impose_net_plugin");
    const impose_net_iface *iface =
        plugin ? static_cast<const impose_net_iface *>(plugin->GetInterface(1)) : NULL;
    log_printf("wifi popup: plugin %p, interface %p", (void *)plugin, (const void *)iface);
    if (!iface) return WIFIQR_ERROR_POPUP_NOT_LOADED;
    int rc = iface->open(on_popup_done, NULL);
    log_printf("wifi popup: open -> 0x%08x", (unsigned)rc);
    return rc;
}
