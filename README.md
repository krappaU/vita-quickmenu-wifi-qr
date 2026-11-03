# Wi-Fi QR for PS Vita

A Quick Menu widget that scans a Wi-Fi QR code with the Vita's back camera, saves the network and
connects to it, without closing your game.

On Android, open **Settings → Wi-Fi**, tap the network (or the gear next to it) and choose **Share**.
The phone shows a QR code. On the Vita, hold the PS button and tap **Scan Wi-Fi QR**. Hold the
phone's screen against the back of the Vita over the camera, then pull it slowly away. iOS shares
the same code format in its Wi-Fi settings.

While scanning, a 320×240 camera preview appears at the bottom middle of the screen, with red boxes
on the QR corner squares found. QuickMenuReborn can't show a changing picture (it loads each image
once per menu opening and keeps it until the menu closes), so a small kernel plugin,
`wifiqr_kernel.skprx`, draws it into SceShell's frames as they're shown, the way PSVshell draws its
overlay. Without it, a text viewfinder (24×12 full-width characters with `◎` on corner squares)
is shown instead. The status line says what the camera sees: nothing, a screen, part of a code (one or two of its corner squares), a whole code, or a
whole code that doesn't decode within 2 s (too close for the fixed-focus camera: move it further).
Frames where corner squares were found but nothing decoded are tried again sharpened and
contrast-stretched, with a few settings: the camera's pictures of a close code are soft and dim.
Android's share screen draws the code's corner squares with rounded corners, which quirc can't
group into a code; `src/quirc_ext.c` groups them by their centres instead and tries the grid sizes
around the estimate.

> **Status: first build, not yet tested on a console.** The parser, the profile writer and QR
> decoding are tested on a PC (`scripts/test.sh`). What the Vita does is still to be checked, see
> [Testing on a console](#testing-on-a-console).

## How it works

1. **Plugins.** The core (`wifiqr.suprx`) runs in SceShell, the system shell that stays open next
   to every game, so the game keeps running. Front ends call its API (`src/wifiqr_api.h`): for now
   the Quick Menu button (`wifiqr_qm.suprx`, through
   [QuickMenuReborn](https://github.com/Ibrahim778/QuickMenuReborn)); next, an entry in the
   system's Wi-Fi Settings popup.
2. **Scan.** It reads 640×480 frames from the back camera (320×240 if memory is short) and decodes
   them with [quirc](https://github.com/dlbeer/quirc) until it finds a code, for up to 30 seconds.
   The camera needs physically contiguous memory, which is scarce in SceShell, so the plugin tries
   each kind that can provide it and logs the results.
3. **Parse.** It reads the `WIFI:T:WPA;S:<name>;P:<password>;;` text. Enterprise (802.1X) networks
   are rejected with a message. `T:SAE` (WPA3) is saved as WPA2 and tried: Android writes it for
   WPA2/WPA3 transition networks too, whose password works over WPA2; if the connection fails the
   status says the network may be WPA3-only. 5 GHz-only networks can't be detected from the code.
4. **Save.** Saved networks live in the registry, in 30 slots `/CONFIG/NET/01` to `/CONFIG/NET/30`
   (layout from [Vita Account Manager](https://github.com/windsurfer1122/Vita-Account-Manager) and
   [Registry Editor MOD](https://github.com/devnoname120/RegistryEditorMOD)).
   - A network with the same name is updated in place.
   - A new one goes in the first free slot with the values the Vita's own Wi-Fi Settings writes
     for a new network (automatic IP, no proxy, auto-connect on), found by comparing the registry
     before and after the system saved one. The name is written last, so a slot that is only half
     written still counts as free.
   - The `wifi_security` number is copied from a saved network of the same type. Without one, WPA
     uses 2 and open networks 0, the values the system wrote. WEP has no confirmed value, so it
     needs one WEP network already saved through Settings.
   - Before the first change in each session, every saved network is copied to
     `ux0:data/wifiqr/backup-<date>.txt`.
   - The shell keeps its own copy of the saved networks, read at boot, so a profile written only
     to the registry shows in Settings but doesn't connect until a reboot. The plugin then does
     what the system's Wi-Fi Settings popup does after writing the registry (found in its 3.65
     code): it hands the profile to `SceShell_60129092` as a 0x478-byte structure laid out like
     the registry keys (see `src/profiles.h`).
5. **Connect.** The Vita connects on demand: when an app uses the network, it joins a saved network
   that's in range, preferring the stronger signal. So the plugin does a DNS lookup, then if nothing
   happens after 5 s a TCP connection, and waits for the connection. If the Vita is already
   connected to another network, it stays on it; the status line says so.

## Install

Three plugins:

| File | What | Where |
|---|---|---|
| `wifiqr.suprx` | the core: camera, decoding, saving, connecting | `ur0:tai/`, under `*main` |
| `wifiqr_qm.suprx` | the Quick Menu button | `ur0:QuickMenuReborn/` (needs [QuickMenuReborn](https://github.com/Ibrahim778/QuickMenuReborn)) |
| `wifiqr_kernel.skprx` | the camera preview over the shell's frames (optional) | `ur0:tai/`, under `*KERNEL` |

```
*KERNEL
ur0:tai/wifiqr_kernel.skprx
*main
ur0:tai/wifiqr.suprx
```

Use the `config.txt` taiHEN actually reads: `ux0:tai/config.txt` if it exists, else
`ur0:tai/config.txt`. Remove the old single `wifiqr.suprx` from `ur0:QuickMenuReborn/` (the
button is now `wifiqr_qm.suprx`). The kernel plugin hooks `ksceDisplaySetFrameBufInternal` with
the NIDs PSVshell uses, known for firmware 3.60 and 3.65; without it, or if the hook fails, the
button shows a text viewfinder instead. Reboot after installing.

The button finds the core at run time (`taiGetModuleExportFunc`), so the order the plugins load in
doesn't matter; without the core it says so.

The core is loaded early in the shell's start-up, so it imports only libraries present from the
start of the shell's process. The network libraries (libnet, libnetctl) are loaded later, on
demand: the core finds their functions at run time (`src/net_dyn.c`). Importing them made the
core fail to load, or crash on calls through unlinked import stubs, which jump to address 0
(found with a self-test, 2026-11-03; `wifiqrSelfTest` is still exported).

## Settings

`ux0:data/wifiqr/config.txt`, one `key=value` per line, all optional:

| Key | Default | |
|---|---|---|
| `resolution` | `640` | `320` uses less memory, but dense codes (long passwords) then need the phone closer |
| `scan_seconds` | `30` | how long to look for a code |
| `connect_seconds` | `30` | how long to wait for the connection |
| `viewfinder` | `1` | `0` turns off the text viewfinder |
| `snapshots` | `0` | `1` saves frames that didn't decode as `frame-*.pgm`, for debugging. **They can show the password**, so delete them afterwards |
| `live_labels` | `1` | update the status line while the menu is open. Set it to `0` if the shell crashes during a scan; the status then updates when you reopen the menu |

## Log

`ux0:data/wifiqr/log.txt` records each scan:
- the saved networks, with passwords masked
- the camera memory used
- frames scanned
- the slot written and the `wifi_security` value used
- each network state change and which trigger caused it

The QR payload and passwords are never logged.

## Testing on a console

Things only a real Vita can answer. The log shows each of them:

- [x] **The camera works from SceShell** with `RW_UNCACHE` memory plus the PHYCONT allocation
      attribute (2026-11-02). `sceCameraStart` refused ordinary memory with `0x80024003` (not
      physically contiguous), the PHYCONT type failed with `0x80024A00` and CDRAM with `0x80024309`
      (full at 512 KB). Frames are real; decoding a 640×480 frame takes 35–45 ms.
- [x] **A printed code decodes on the console** (2026-11-02, a router's label, first try with the
      picture preview) and the network is saved (slot 5, `wifi_security` 2).
- [x] **A code on a phone screen decodes on the console** (2026-11-02) with the centre-based
      grouping of Android's rounded corner squares, which plain quirc couldn't group. With `snapshots=1`, the first frame of each kind that didn't
      decode is saved as `ux0:data/wifiqr/frame-<kind>.pgm`.
- [x] **A saved profile is correct:** it shows in Settings and connects after a reboot (2026-11-02).
- [x] **It connects without a reboot** now that the shell's copy is updated too (2026-11-03).
- [ ] **The Quick Menu's "Wi-Fi Settings" button opens the system popup** through the shell's own
      loader (`docs/impose_net.md`, "Loading it"); the log shows `wifi popup: open -> 0x00000000`
      and, after it closes, `wifi popup closed: result …`. Firmware 3.65 only.
- [ ] **Scanning in games:** the camera delivers no frames to SceShell while a game is in the
      foreground (2026-11-02: the buffer stayed untouched). Works in LiveArea.
- [ ] **The DNS lookup starts the connection.** `netctl state: connecting` should follow
      `trigger: DNS lookup`. If it only follows `trigger: TCP connect`, swap their order.
- [ ] **Live label updates don't crash the shell** (`live_labels`).
- [x] **What the `wifi_security` values mean:** open 0, WPA/WPA2-PSK 2 (WEP not seen yet).
- [x] **What the system writes for a new network:** see step 4.

## Build

The Vita build runs in the VitaSDK Docker image; the tests need gcc and python3:

```bash
scripts/build.sh   # -> build/wifiqr.suprx
```

```bash
pip install qrcode==8.0 pillow==11.0.0   # for the QR decoding test
```

```bash
scripts/test.sh
```

`scripts/fetch_deps.sh` downloads QuickMenuReborn and quirc at pinned commits into `third_party/`.
QuickMenuReborn's link stubs are generated from its NID table (`QuickMenuReborn.yml`).

## License

MIT. quirc is ISC-licensed and is downloaded at build time, not stored in this repository.
