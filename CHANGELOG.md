# Changelog

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
