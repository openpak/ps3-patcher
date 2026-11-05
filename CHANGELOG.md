# Changelog

## v0.2.11

- v0.2.10 on hardware: Cobra's load call now reaches the kernel module loader, which answers
  0x80011148 = CELL_PRX_ERROR_UNSUPPORTED_PRX_TYPE (RPCS3 `sys_prx.h`; its loader raises it when the
  file does not decrypt or is not an `ET_SCE_PPURELEXEC` ELF). Cause: our sprx was an `ET_EXEC` image
  linked at fixed addresses 0x10000/0x10000000 by PSL1GHT's `lv2.ld` + `sprxlinker`. A firmware PRX
  (checked against `ap_plugin.prx`) is `e_type` 0xFFA4, OSABI 0x66, two load segments at vaddr 0, and
  a `PT_SCE_PPURELA` (0x700000a4) segment of 24-byte relocation records; the module info is found
  through `p_paddr` of the first segment.
- The plugin is now linked with `plugin/prx.ld` (PRX layout at vaddr 0, no PSL1GHT stub objects), converted by
  `tools/elf2prx.py` into that layout (77 relocations: ADDR32/ADDR64 and the TOC base; module info
  `toc` relocated), then wrapped by `fself`. The conversion is checked by loading the result the way
  RPCS3's `ppu_load_prx` does (relocate, read the module info and export table, follow the OPDs).
- `openpak_cb.sprx` (compressed `make_self`, can never pass Cobra's size guard) is gone; an update
  removes its file, its boot_plugins line and its status file. Other lines are kept as they were.
  Wrappers: A (fself) and C (fself with webMAN's application info).

## v0.2.10

- Cause found for "Cobra plugins: query failed" and "Cobra load call ... 0x80010000": both were this
  app, not Cobra. Cobra's `ps3mapi_get_vsh_plugin_info()` returns ENOENT (0x80010006) for an empty
  slot (Cobra/Mamba `stage2/modulespatch.c`: `if (vsh_plugins[slot] == 0) return ENOENT`); webMAN MOD
  ignores the return value and tests the cleared buffer. The app treated 0x80010006 as an error, so
  it never found a free slot, never called Cobra's load (opcode 0x1EE7) and printed its own
  placeholder 0x80010000 (v0.2.7/0.2.8 printed 0). PS3MAPI itself works (core 0x124, mode 0).
- Cobra refuses a VSH plugin whose file is shorter than SELF header_len + data_len (same file, the
  "semi-brick" guard). `openpak_cb.sprx` (make_self, compressed) is 9424 bytes against 206344
  required, so it can never load. The details page prints this check for every wrapper.
- New wrapper `openpak_cc.sprx`: the fself with the SELF application info of webMAN MOD's own sprx
  (auth id 1070000052000001, version 0003004000000000). Boot list order: A, C, B.
- "Load it now" tries A, C, B in turn into a free slot (1..6, as Cobra's boot loader) with Cobra's
  call and shows every raw return code; it stops at the first accepted one.
- A details page opens first: full PS3MAPI line, all seven slots, the first lines of boot_plugins.txt
  with CR count, size check per wrapper, which wrappers ran at boot, last load result. Also written
  to `/dev_hdd0/openpak/diag.txt`.

## v0.2.9

- v0.2.8 on hardware: the plugin never ran from boot_plugins.txt. Cause candidate: the SELF wrapper.
  Every VSH plugin known to load under Cobra is a signed or Sony-SDK-made SELF (webMAN MOD: key
  revision 0x0A; Akari: 0x0004, `0x240` header); ours was PSL1GHT `fself` (revision 0x8000, `0x410`
  header). Akari's metadata and sections are encrypted, so that layout cannot be generated without
  keys; this release instead ships the same plugin in two wrappers and installs both:
  `openpak_ca.sprx` (fself) and `openpak_cb.sprx` (the toolchain's `make_self`, key revision 1,
  `0x3f0` header). Both are listed in boot_plugins.txt; each reports to its own status files, and the
  second finds the pokes already applied. The status screen shows `Plugin: loaded 3/3 (A, B or A+B)`:
  which wrapper Cobra accepted. Switching back removes both.
- The status screen shows the raw PS3MAPI answers (core version, syscall 8 restriction mode, process
  list, plugin slots 0 and 1) on a `PS3MAPI:` line, so they can be photographed. The mode is read, not changed.
- Includes the v0.2.8 update-available flow.

## v0.2.8

- Update flow on launch: the app compares the installed setup with what it carries (version marker
  `/dev_hdd0/openpak/installed.txt`, plugin file byte for byte, CA files, boot_plugins line). If
  installed but older or different: "OpenPak update available (installed vX, this app vY). Install
  it?" Yes re-applies with the same backup, verify and rename rules, then "OpenPak setup updated.
  Restart to apply?"; No goes on to "Switch back to PSN?". Current: status, then "Switch back to
  PSN?". Not installed: "Switch to OpenPak?". The sprx format is unchanged here.

## v0.2.7

- v0.2.6 on hardware: plugin never ran ("not run yet" after restarts, sign-in 80710007, no traffic).
  The status no longer depends on the plugin writing files. The app asks Cobra directly (PS3MAPI,
  syscall 8 opcode 0x7777) which VSH plugin slots are loaded (0x47), and reads the three trust
  pokes, the pinned CN field and the 32 static PSN names in the vsh process (0x31). The screen shows
  "Cobra plugins", "vsh now: pokes a/3, CN yes|no, names n/32".
- "Load it now?" appears when the plugin file is installed but Cobra has not loaded it: it calls
  Cobra's load-VSH-plugin (opcode 0x1EE7) into a free slot and shows the raw return code.
- The plugin retries its status files until /dev_hdd0 accepts the write (creates the directory
  too), instead of giving up at boot.

## v0.2.6

- No more DNS step. The plugin rewrites the PSN host names held in the firmware's memory to
  equal-length OpenPak names (`.ac.playstation.net` -> `.ac.ps3.openpak.org`, 11 suffixes), the way
  webMAN MOD's `block_online_servers` / `restore_blocked_urls` rewrite them to nothing. They resolve
  through public DNS to OpenPak, so the console DNS stays Automatic. Verify-before-patch (the whole
  name must be present, the write is read back), rescans for libraries that load later, puts every
  original word back on module stop; a reboot always restores. Not covered: game processes.
- The status screen shows `Redirect: N names`; the temporary DNS dialog is gone.
- The name list is `names/psn-names.txt` (match TAB replace, same length), bundled at build time by
  `tools/stage.sh`; `NAMES_EXTRA=<file>` appends entries of the same shape (where the network's
  single signed list plugs in). `tools/test_redirect.c` tests the rewrite on the host.

## v0.2.5

- Clean restart: the restart prompt now unmounts dev_blind, removes the `turnoff` marker and calls
  sys_sm_shutdown with op 0x0200, as webMAN MOD's restart paths do. v0.2.4 used op 0x8201 and left
  the marker, so the next boot ran the HDD check. The log is synced before the restart.
- The manual DNS screen is kept and marked as temporary.

## v0.2.4

- Cobra detection now matches webMAN MOD's `is_cobra_based()`: the version comes back through a
  pointer argument (preset 0x99999999), not as the return value; v0.2.3 passed NULL and so called a
  working Cobra "not active". Payload (Cobra/Mamba/PS3HEN) and version shown; stage2 checked at
  `sys/stage2.bin` and `rebug/cobra/stage2.cex|dex` (and their renamed-disabled forms).
- A Cobra-off refusal says "Cobra appears to be off" with the raw values and the log path. It never
  advises toggling Cobra.

## v0.2.3

- Exit path rebuilt after the whole-console freeze on a refusal: v0.2.0 and v0.2.2-era code left
  main with a flip queued and the RSX live. Now: dialog closed, last flip awaited, `rsxFinish`
  drains the GPU, callback unregistered, `sysProcessExit(0)`. No reboot or lv2 call on quit paths.
  (v0.2.2 had already added the vsynced frames, detection signals and the clean-exit call; its
  teardown lacked the final flip wait and `rsxFinish`.)

## v0.2.2

- Fix the false "no custom firmware" refusal. Detection now uses independent signals, all shown on
  the status screen and logged raw to `patcher.log`: lv2 peek (syscall 6), Cobra (syscall 8 opcode
  0x7000, any non-error answer), `stage2.bin`, and the HEN marker. The patcher proceeds only when
  peek works AND Cobra is active; CFW with Cobra off gets the Toggle Cobra steps.
- Fix the flicker: the old loop redrew the whole framebuffer on every frame and never waited for
  the flip, so the dialog alternated with a half-drawn background. The background is now drawn once
  into both buffers and each frame is a vsynced double-buffered flip.
- Fix the blank screen after OK on a refusal: main returned with RSX still initialised. Every path
  now ends on a visible dialog and leaves through `sysProcessExit`. A refused state offers no
  install choice, only the reason.

## v0.2.1

- XMB icon fills the full 320x176 frame (logo plus "OpenPak / PS3 Patcher"); it was a square tile between
  dark bars. Source is `app/icon.svg`, same mark and colours as switch-patcher.

## v0.2.0

- New: `OpenPak-PS3-Patcher-<tag>.pkg`, an XMB app (title id `OPNP00001`) installed from a USB stick
  with the Package Manager. Status screen, switch to OpenPak and back to PSN, restart prompt.
  4.93 CEX only; stock CA01/CA59 backed up once, dev_flash writes via temp file + read-back +
  rename, plugin line added to and removed from `boot_plugins.txt` without touching other lines,
  log at `/dev_hdd0/openpak/patcher.log`. DNS is shown as on-screen steps, not written.
- Plugin: writes `/dev_hdd0/openpak/plugin.status` at boot so the app can show whether it loaded.
- Docs: user flow first, manual FTP route as an appendix.

## v0.1.1

- Fix the release build: the previous workflow referenced a non-existent container image. It now
  runs `make stage` on the runner and compiles the plugin in `docker.io/hldtux/ps3dev:latest`
  (a maintained PSL1GHT/ps3toolchain image), publishing `openpak_ca.sprx` plus the CA files and
  install notes as the release asset.
- `plugin/module.S`: real PS3 PRX export table (module_start / module_stop / module_info by NID),
  modelled byte-for-byte on a firmware VSH plugin, so the toolchain produces a valid fake-signed
  sprx. `plugin/Makefile` updated to the verified build (lv2.ld + lv2-sprx.o, sprxlinker, fself).
  Build confirmed locally; loading on real hardware remains unverified.

## v0.1.0

First real patcher (was a placeholder).

- `tools/stage.sh`: fetch the OpenPak CA (sha256-pinned from the ps3 network profile), stage it as
  `dev_flash/data/cert/CA01.cer` and `CA59.cer` (PEM), and generate the VSH-plugin poke header from
  the CA's CN. Non-4.93 firmware: override the vsh addresses via `ADDR_*`.
- `plugin/openpak_ca.c` + `plugin/Makefile`: VSH plugin (`openpak_ca.sprx`, PSL1GHT) that applies
  the three firmware-4.93 vsh pokes (file-content SHA-1 gate + NP-profile issuer-CN pin) to the
  running vsh process at boot, each verify-before-patch so a wrong firmware no-ops instead of
  bricking. Auto-skips the CN pokes when the CA CN is `SCEI DNAS Root 05`.
- `docs/protocol.md`, `docs/deploy.md`: what is patched and why, and the install/DNS/account/
  rollback steps. Route, decisions and re-derivation: `research/ps3/console-setup.md`.
- `.github/workflows/release.yml`: tag build of the sprx (no binary committed).

Unverified on real hardware: proven on Pier only.
