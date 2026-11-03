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

Once loaded it can be reached with `paf::Plugin::Find("impose_net_plugin")` (NID `0x004D98CC`)
and `paf::Plugin::GetInterface(1)` (`0x1DF2C6FD`), which is what the shell's own callbacks do.

## Loading it

The plugin is loaded only when the shell needs it: from the Quick Menu, `Plugin::Find` returns
NULL (2026-11-03). Read from `vs0:vsh/shell/shell.self` (3.65, code segment at `0x81000000`):

- `0x810e26cc` **open(exec_mode, done, userdata)**: returns -1 if `done` is NULL; otherwise makes a
  `0x30`-byte request (`+8` done, `+0xC` userdata, `+0x10` exec_mode; vtable `0x8152fda8`) and
  queues it (`0x810e1eda`) with the shell's popup memory manager (global `0x81544468`, guarded by
  an `RMutex`, serviced through `MainThreadCallList`: it never blocks the caller). `0x810e290a`
  cancels. The shell's network service reaches both through a callback table at `0x815021b4`
  (entries 3 and 4), passing exec_mode 1 for its usual requests and 0 otherwise; nothing exports
  them.
- When memory is free the manager calls the request's load method (`0x810e29da`) with a block of
  at least `0xFA000` bytes: a `SurfacePool` of `0xAF000` bytes at its start, a `HeapAllocator`
  ("SceImposeNetPluginPrivateHeap") of `0x4B000` after it, then `Plugin::LoadAsync(param,
  0x810e2d6b, 1)` with name `impose_net_plugin`, caller `__main__`, resource and module files
  `vs0:vsh/shell/impose_net_plugin.{rco,suprx}`, interface version 1, module option 1, option 28,
  `surface_pool`, `private_heap`, `unload_callback = 0x810e2df7`, `param` = the request, and the
  int arguments `userdata` (the request), `memblockaddr` (block + `0xFA000`, the rest of the block:
  the popup loads the keyboard plugin there, `0x200000` bytes) and `exec_mode`.
- The load-finished callback reads `userdata` back and calls the popup's `open(0x810e2ed3,
  request)`; that callback stores the result (`0x80102408` if cancelled) and calls
  `Plugin::UnloadAsync("impose_net_plugin")`; the unload callback frees the pool and heap and then
  calls the request's `done(a, b, c, result, userdata)`.

`src/popup.cpp` calls the open function directly: it takes SceShell's code segment from
`sceKernelGetModuleInfo`, adds `0xE26CC`, and calls it only if the first 24 bytes there are the
ones 3.65 has. The shell then handles memory, loading and unloading as for its own requests.

In the popup, `exec_mode` is stored and, when 0, a check of `/CONFIG/NET/ap_auto_config` runs first
(`0x810052c0`).

The popup itself loads the keyboard plugin the same way (`0x810046B0`): a `SurfacePool`
(0x12C000 bytes) and a `HeapAllocator` (0xD4000 bytes) at `memblockaddr`.

PAF NIDs: [GrapheneCt/ScePaf-RE](https://github.com/GrapheneCt/ScePaf-RE) `lib/*.emd`; a NID is
the first 4 bytes of SHA-1 of the mangled name plus the library's `nidsuffix`, little-endian.
