# Wi-Fi QR for PS Vita

A Quick Menu widget that scans a Wi-Fi QR code with the Vita's back camera, saves the network and
connects to it, without closing your game.

On Android, open **Settings → Wi-Fi**, tap the network (or the gear next to it) and choose **Share**.
The phone shows a QR code. On the Vita, hold the PS button and tap **Scan Wi-Fi QR**. Hold the
phone's screen against the back of the Vita over the camera, then pull it slowly away. iOS shares
the same code format in its Wi-Fi settings.

While scanning, a 320×240 camera preview appears at the top right of the screen, with red boxes
on the QR corner squares found. QuickMenuReborn can't show a changing picture (it loads each image
once per menu opening and keeps it until the menu closes), so a small kernel plugin,
`wifiqr_kernel.skprx`, draws it into SceShell's frames as they're shown, the way PSVshell draws its
overlay. Without it, a text viewfinder (24×12 full-width characters with `◎` on corner squares)
is shown instead. The status line says what the camera sees: nothing, a screen, part of a code (one or two of its corner squares), a whole code, or a
whole code that doesn't decode within 2 s (too close for the fixed-focus camera: move it further).
Frames where corner squares were found but nothing decoded are tried again sharpened and
contrast-stretched, with a few settings: the camera's pictures of a close code are soft and dim.

> **Status: first build, not yet tested on a console.** The parser, the profile writer and QR
> decoding are tested on a PC (`scripts/test.sh`). What the Vita does is still to be checked, see
> [Testing on a console](#testing-on-a-console).

## How it works

1. **Widget.** [QuickMenuReborn](https://github.com/Ibrahim778/QuickMenuReborn) adds the widget
   to the Quick Menu. The plugin runs in SceShell, the system shell that stays open next to every
   game, so the game keeps running.
2. **Scan.** It reads 640×480 frames from the back camera (320×240 if memory is short) and decodes
   them with [quirc](https://github.com/dlbeer/quirc) until it finds a code, for up to 30 seconds.
   The camera needs physically contiguous memory, which is scarce in SceShell, so the plugin tries
   each kind that can provide it and logs the results.
3. **Parse.** It reads the `WIFI:T:WPA;S:<name>;P:<password>;;` text. Networks the Vita can't join
   are rejected with a message: WPA3-only (SAE) and enterprise (802.1X). 5 GHz-only networks can't
   be detected from the code.
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
5. **Connect.** The Vita connects on demand: when an app uses the network, it joins a saved network
   that's in range, preferring the stronger signal. So the plugin does a DNS lookup, then if nothing
   happens after 5 s a TCP connection, and waits for the connection. If the Vita is already
   connected to another network, it stays on it; the status line says so.

## Install

1. Install [QuickMenuReborn](https://github.com/Ibrahim778/QuickMenuReborn) (its plugin under
   `*main` in your taiHEN config, plus the `ur0:QuickMenuReborn/` folder with `qmr_plugin.rco`).
2. Copy `wifiqr.suprx` into `ur0:QuickMenuReborn/` (`ux0:` if that's where your QuickMenuReborn
   folder is). Don't add it to `config.txt`: QuickMenuReborn loads it.
3. For the camera preview: copy `wifiqr_kernel.skprx` to `ur0:tai/` and add it under `*KERNEL` in
   `ur0:tai/config.txt`:
   ```
   *KERNEL
   ur0:tai/wifiqr_kernel.skprx
   ```
   It hooks `ksceDisplaySetFrameBufInternal` with the NIDs PSVshell uses, known for firmware 3.60
   and 3.65. If the hook fails, the log says so and the text viewfinder is used.
4. Reboot.

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
- [ ] **A code decodes on the console.** A real frame with all three corners found (2026-11-02)
      only decoded after sharpening and a contrast stretch, now built in. With `snapshots=1`, the
      first frame of each kind that didn't decode is saved as `ux0:data/wifiqr/frame-<kind>.pgm`.
- [ ] **The registry write is enough.** The new network shows in Settings → Network → Wi-Fi Settings
      and works.
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
pip install qrcode==8.0   # for the QR decoding test
```

```bash
scripts/test.sh
```

`scripts/fetch_deps.sh` downloads QuickMenuReborn and quirc at pinned commits into `third_party/`.
QuickMenuReborn's link stubs are generated from its NID table (`QuickMenuReborn.yml`).

## License

MIT. quirc is ISC-licensed and is downloaded at build time, not stored in this repository.
