# Changelog

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
