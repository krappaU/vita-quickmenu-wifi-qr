# impose_net_plugin: the system's Wi-Fi Settings popup

Notes from reading `vs0:vsh/shell/impose_net_plugin.suprx` (firmware 3.65, decrypted on the
console). Addresses are in the module's own address space (segment 0 at `0x81000000`, segment 1
at `0x8101D000`). No Sony code is kept in this repository.

## What it is

The popup that lists Wi-Fi networks when an app needs the network and none is connected, with
the entries `wifi::wps`, `wifi::aoss`, `wifi::rakuraku` and `wifi::manual` on the page
`page_popup_list_view`. It is a PAF plugin running in SceShell; its texts are in
`vs0:vsh/shell/impose_net_plugin.rco`.

Imports of note: `SceRegMgr` (it reads and writes the profiles itself), `SceShell` (22 network
functions the shell exports), `SceNetInternal` (one), `SceShellSvc`, and PAF.

## Saving a network (function `0x81005574`)

1. Writes the profile's registry keys under `/CONFIG/NET/NN/` through generic helpers
   (`0x8100c9c0`, `0x8100cd7c`, which look up a key's type with `sceRegMgrGetKeysInfo` and then
   call `sceRegMgrSetKeyInt`/`Str`).
2. Writes `/COMMON/conf_type`, then `/COMMON/conf_flag` = 1.
3. Calls `SceShell_60129092(entries, 1)` (NID `0x60129092`, library `SceShell` `0x2404AE25`)
   with one `0x478`-byte structure, cleared and then filled like the registry keys:

   | Offset | Field |
   |---|---|
   | `+0x000` | slot, 1-based |
   | `+0x004` | `conf_flag` (1) |
   | `+0x008` | `conf_type` |
   | `+0x00C` | `conf_name[65]` |
   | `+0x050` | `conf_serial_no` |
   | `+0x054` | `enable_auto_connect` (1) |
   | `+0x058` | `mtu` |
   | `+0x060` | `ssid[33]` |
   | `+0x084` | `wifi_security` (2 = WPA/WPA2-PSK: then `wpa_key`, else `wep_key`) |
   | `+0x088` | `wep_key[27]` |
   | `+0x0A4` | `wpa_key[65]` |
   | `+0x0E8` | `ip_config` |
   | `+0x31C` | `dns_flag` |

   This updates the shell's in-memory copy of the saved networks, which is otherwise read only
   at boot: a profile written to the registry alone shows in Settings but connects with the
   stale copy until a reboot (tested 2026-11-02). `src/profiles.c` builds the same structure.

## Opening the popup

At start-up the plugin calls `paf::Plugin::SetInterface(1, 0x8101D018)`; the table holds two
functions:

- `open(callback, userdata)` (`0x8100016C`): returns `0x80102403` if `callback` is NULL or the
  popup is busy (its state at `+0x18` is not 0 or 1), else sets the popup up (`0x810028AA`,
  which stores `callback` at state `+0x10` and `userdata` at `+0x14`), schedules the page on the
  main thread (`MainThreadCallList::Register`) and returns 0. When the popup finishes it calls
  `callback(state+0x38, state+0x3C, state+0x40, result = state+0x20, userdata)` (`0x81002A26`).
- `close()` (`0x810001A2`): sets the result to `0x80102402` and closes it.

The module also sets a separate module interface (`paf::Module::SetInterface(1, 0x8101D020)`:
load, start and stop callbacks).

`src/popup.cpp` (in the core) reaches it with `paf::Plugin::Find("impose_net_plugin")` (NID
`0x004D98CC`, matching the plugin's own import) and `paf::Plugin::GetInterface(1)` (`0x1DF2C6FD`).

## Loading it

The plugin is loaded only when the shell needs it: from the Quick Menu, `Plugin::Find` returns
NULL (2026-11-03). Its module interface (`paf::Module::SetInterface(1, 0x8101D020)`) holds the
plugin callbacks a `Plugin::LoadAsync` with `module_file = vs0:vsh/shell/impose_net_plugin.suprx`
uses: set-param (`0x81000034`, stores 50), init (`0x8100003A`: creates the popup's state, then
`SetInterface(1, …)` and `SetInitEventHandler`), start, stop and exit. So the shell loads it with
`Plugin::LoadAsync`. When the popup loads the keyboard plugin the same way (`0x810046B0`), it
first makes a `SurfacePool` (0x12C000 bytes) and a `HeapAllocator` (0xD4000 bytes) out of memory
it owns, and sets name, caller name, resource file, module file, interface version 1, module
option 1, option 16 and a few arguments. The shell's values for impose_net_plugin need shell.self.

PAF NIDs: [GrapheneCt/ScePaf-RE](https://github.com/GrapheneCt/ScePaf-RE) `lib/*.emd`; a NID is
the first 4 bytes of SHA-1 of the mangled name plus the library's `nidsuffix`, little-endian.
