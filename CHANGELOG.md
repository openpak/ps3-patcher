# Changelog

## v0.2.18-pt (pass-through hardware test)

- v0.2.17 froze the XMB on hardware (wave background, no icons) right after its first caught lookup
  (np-account saw GET /netstart/ps3): the hook was installed in module_start and caught/chained lookups
  during the XMB's synchronous start-up. Root cause not yet pinned; this build isolates the install.
- **Pass-through only:** the hook is installed but catches nothing and calls nothing; it counts the lookup
  and returns res_goahead (six instructions: no stack frame, no calls, no I/O), so every name resolves
  through the console's own DNS exactly as without the plugin.
- **Delayed install:** the hook is installed by the plugin thread 45 s after boot, never in module_start.
- **Kill switch:** an empty `openpak_nohook` file in the root of a USB stick (dev_usb000/001) or
  `/dev_hdd0/openpak/nohook` keeps the hook out. Shown on screen after an update and in INSTALL.md.
- Screen: "DNS catcher: pass-through test (installed, nothing redirected), K lookups passed",
  "waiting for the XMB" before the install, or "off - <reason>".

## v0.2.17

- **DNS-catcher pivot.** The patcher is now the PS3's dns_mitm, like the Switch patcher. Instead of
  rewriting PSN host strings in vsh memory (v0.2.6..v0.2.16, which never worked on hardware), it installs
  a `res_send` query hook in the vsh process: a name matching a family on the signed `ps3` list is
  answered with the OpenPak box address, every other name is passed to the console's own DNS unchanged.
  The console's DNS setting is never changed, so the owner's LAN names are untouched. RE and the exact
  4.93 CEX addresses: `docs/dns-catcher.md`; firmware host inventory: `docs/firmware-hosts-4.93.md`.
- **Chokepoint.** BIND `res_send` (statically linked in vsh.self, 0x164c0 on 4.93 CEX) calls the function
  in its Qhook cell (0x706f1c) before each query. Both `gethostbyname` and the async resolver reach it, so
  one hook covers sign-in and everything else. Proven on Pier (OpenPak's RPCS3 fork) with the console DNS
  set to plain upstream: PSN family names caught -> box, other names resolved upstream.
- **Guarded install, never hangs at boot.** The install verifies nine `res_send` words (signature, TOC
  slot, the Qhook cell's firmware default 0x6e9480 and the default OPD) and only then stores one pointer,
  chaining to the saved firmware hook for pass-through names. The hook is a pointer written at boot and
  only run on a later lookup, so a wrong firmware can only no-op, never block boot. `module_stop` and any
  reboot restore the original Qhook pointer.
- **Signed list, verified at build.** `tools/stage.sh` fetches the signed ceiling, verifies its Ed25519
  signature against the pinned key on the build host, intersects the `ps3` profile families with it, and
  bundles `plugin/catchlist.h` (v18: `.playstation.net/.com/.org`, `.sonyentertainmentnetwork.com`). The
  on-console verify + boot/6h/reconnect re-fetch is v0.2.18.
- **Screen.** "DNS catcher: on (list build v18, 4 families), M caught, K passed" or "off - <reason>".
  Caught names are logged (`/dev_hdd0/openpak/catcher.caught.log`); passed names are only counted.
- **Removed** the string-rewrite, lv1/lv2 memory walk, module-scan and vsh-name-table code
  (`plugin/redirect.h`, `plugin/openpak_names.h`, `app/source/vshnames.h`, `names/`,
  `tools/test_redirect.c`). The CA install and the three trust pokes are unchanged.
- Recovery for a bad build is shown on screen after "Switch to OpenPak" and documented in
  `docs/dns-catcher.md`.

## v0.2.16

- v0.2.15 on hardware: pokes 3/3, 20 of 32 vsh names rewritten, yet PSN sign-in still reached Sony (its
  wrong-ID answer is the "device setup password" QR screen) and np-account saw no getCountries/bindAccount.
  Cause: the sign-in host `native-ps3.np.ac.playstation.net` is built as `"%s.%s.%s"` from `native-ps3`, `np`
  and a bare `ac.playstation.net` (no leading dot), so the `.ac.playstation.net` entry never matched it. That
  copy is in vsh.self (0x68bb40) and in every module with the native client: npsignin_plugin and
  regcam_plugin (loaded only when sign-in opens), explore_plugin, nas_plugin and others.
- New list entry `ac.playstation.net` -> `ac.ps3.openpak.org` (after the dotted one), and the vsh copy at
  0x68bb40 joins the vsh name table (33 entries).
- The scan thread polls vsh's module list every second (PS3MAPI GET_ALL_PROC_MODULE_PID) and scans each new
  module's segments (GET_PROC_MODULE_SEGMENTS) through GET/SET_PROC_MEM with the same equal-length swap and
  read-back; our own module is skipped. plugin.log: `modules scanned, hits`, `mod hits <module>` per
  module; redirect.status gains `mods= modhits= modfail= segerr=`, shown on the main page. Works under HEN too.
- The lv1/lv2 walk now runs every 20 s for 6 passes, then every 2 minutes, and polls modules between windows.
- The 12 "name differs" entries are the `.update.playstation.net` part of the region update-list URLs
  (`http://fXX01.ps3.update.playstation.net/update/ps3/list/<cc>/ps3-updatelist.txt`, 0x50 apart); they
  differ on the console's CFW vsh. plugin.log now also prints the 8 bytes found there (` found`).

## v0.2.15

- v0.2.14 on hardware: the plugin runs (pokes 3/3, CN yes, redirect "11 names"), PSN sign-in fails with
  80710A06 (`CELL_HTTPS_ERROR_HANDSHAKE`, RPCS3 `cellHttp.h`), and the box saw no connection from the
  console. "names 0/32" was the signal: the 32 PSN name copies inside the vsh image, which the
  sign-in code uses, were untouched. The memory walk (webMAN's LV1/LV2 ranges) rewrote other copies
  and did not change what the console resolves. With the OpenPak CA replacing the trust roots and
  the names unchanged, the console talks to the real PSN and fails the handshake.
- The plugin now rewrites those 32 vsh copies (app/source/vshnames.h) with the same equal-length suffix
  swap, through PS3MAPI GET/SET_PROC_MEM, verified by read-back, and logs each to plugin.log
  (`name rewritten <addr>`, `vsh names rewritten N`). The memory walk logs its first 24 hits
  (`rd hit <name index><address>`).
- The second beep (every poke and at least one name in place) is now delayed 0.7 s: v0.2.14 issued it a
  few ms after the first and the buzzer ignored it.
- Details: the whole plugin.log is shown after the details pages (paged); the details header shows the
  console clock in UTC (the OpenPak CA is valid from 2026-09-07, leaf certificates from an hour before
  issue); the main page says `names redirected a, original b, of 32`.

## v0.2.14

- v0.2.13 on hardware: all wrappers load into slots 1-3 and none runs: no beep, no plugin.log.
  Cause: function descriptors. Cobra starts a plugin by reading 8 bytes at the module's exported start
  entry as `u32 toc[2]` = {entry, toc} and creating the start thread from that (Mamba
  `lv2/src/modules_c.c` `prx_start_module_with_thread`). Firmware PRXs (checked on `ap_plugin.prx`)
  export 8-byte descriptors {u32 entry, u32 toc}. Our export pointed at the compiler's 24-byte ELFv1
  entry {u64 entry, u64 toc, u64 env}, whose first 8 bytes read as {0, entry}: a start thread at
  address 0, which dies silently while Cobra still reports the load as done.
- `module.S` now exports hand-built 8-byte descriptors for module_start and module_stop, and the scan
  thread is created from one too (`sys_ppu_thread_create` takes the same kind); `tools/elf2prx.py`
  fills in their toc word and relocations (`openpak_opd_*`).
- The pkg carries `USRDIR/wrappers.txt` (one letter per shipped wrapper). Installing a pkg over an older
  one leaves the older version's files in USRDIR, which is why v0.2.13 still saw and installed
  v0.2.12's cc and cd; only listed wrappers count as bundled now, and an update removes the others
  from /dev_hdd0/plugins and the boot list.

## v0.2.13

- v0.2.12 on hardware: Cobra loads the plugin at boot (slot 3, so all three wrappers A, C and D load:
  a fself-wrapped real PRX is accepted, the ET_EXEC build was the only problem), but module_start
  left no trace: no plugin.status, no pokes, no redirect.
- The plugin now traces itself to `/dev_hdd0/openpak/plugin.log` (variant, counter, step, value) from
  its first instruction, and gives one short beep on entry (sys_sm_ring_buzzer, as webMAN MOD) and a
  second one when every poke is in place. The details page prints the log's last lines, or says that
  module_start never wrote one. plugin.status ("ran") is written first, before anything that could
  fault.
- Pokes into vsh code now go through Cobra's PS3MAPI SET_PROC_MEM (pid of `_main_vsh.self`), not a
  direct store from the plugin thread (vsh text is not guaranteed writable), and are verified by reading
  back. The pids come from GET_ALL_PROC_PID / GET_PROC_NAME_BY_PID, as the app does.
- Only wrapper A (fself over the real PRX) ships and is listed: it is what the official CI build can
  produce without keys. C and D remain buildable by hand; an update removes their files and lines.
- `tools/elf2prx.py` refuses a pc-relative relocation that crosses segments (segments are placed
  independently by the loader).

## v0.2.12

- v0.2.11 on hardware: the new files never reached the console (A still the 206088-byte ET_EXEC,
  no cc, the cb line still in boot_plugins.txt). The app had offered "Load it now?" before the setup
  update, and that load ran against the old files. Now every start compares each installed plugin
  file with the copy bundled in the pkg, byte for byte (so size and hash), plus the boot_plugins.txt
  lines (ours present, the retired openpak_cb line absent). Any difference or missing file makes the
  update the first question; it rewrites the files and our boot lines (other lines untouched).
  The version marker is only one input, no longer the deciding one.
- The details page prints, per wrapper, `on disk N, in pkg M: same|DIFFERS|MISSING`.

- Update first: when the installed setup is older than this app, the update question comes before
  "Load it now?" (v0.2.11 hardware: "Load it now" ran against the old v0.2.9 ET_EXEC files and the
  missing cc file, 0x80011148 and 0x80010002). After a successful update, answering No to the restart
  falls through to "Load it now?" with the new files.
- Details page: app version, and for each wrapper `size/needed ok|SHORT` plus `PRX` (e_type 0xFFA4),
  `OLD-EXEC` (the pre-v0.2.11 build) or `type?`, so one photo shows which files are installed.
- Optional wrapper D, `openpak_cd.sprx`: the same PRX signed with scetool exactly as webMAN MOD's own
  sprx (key revision 0x0A, auth id 1070000052000001, APP, compressed, ctrl flags
  4000...0002). Built only when `SCETOOL` and `SCE_DATA` are given to `make -C plugin`; the keys are
  not part of this repository. Ordering: A, C, D.
- `0x80010002` (EINVAL) from Cobra's load call: its loader returns it for a slot >= 7, a file it
  cannot stat, a file under 0x230 bytes, a SELF shorter than header_len + data_len, or an unreadable file
  (Mamba `modulespatch.c` `prx_load_vsh_plugin`). The missing cc file is the first of these.

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
