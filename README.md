# PS3 Patcher

OpenPak's PS3 Patcher. A single `.pkg` you install from a USB stick and run from the XMB. It
switches a PlayStation 3 between PSN and OpenPak's PSN replacement, and back: it installs the
OpenPak CA into the firmware trust store and a VSH plugin that applies the vsh pokes the firmware
needs to accept our certificate chain. Patch the console, never the game.

| | |
| --- | --- |
| Console | PlayStation 3 |
| Kind | Client (patcher) |
| Firmware | 4.93 retail (CEX); re-derive addresses for other versions |
| Setup | Evilnat CFW, or HFW + HEN (both run the stock 4.93 vsh) |
| Download | [Releases](https://github.com/openpak/ps3-patcher/releases) (`vX.Y.Z` tags): `OpenPak-PS3-Patcher-<tag>.pkg` |
| Title id | `OPNP00001` (homebrew-style, installs as a Game) |
| Licence | AGPL-3.0-only, see [LICENSE](LICENSE) |

## Install

1. Copy `OpenPak-PS3-Patcher-<tag>.pkg` to the root of a USB stick (FAT32).
2. XMB: **Game > Package Manager > Install Package Files > Standard**, pick the pkg.
3. Start **OpenPak PS3 Patcher** from the Game column. It shows the status and asks
   **Switch to OpenPak?**. Choose Yes.
4. Set the console DNS (the app shows how), restart, and sign in with your OpenPak email and password.

To go back, start the app again and choose **Switch back to PSN?**. Full steps, safety notes and
the manual FTP route: [docs/deploy.md](docs/deploy.md).

## What it does

- **Status:** network (PSN or OpenPak), firmware, CFW/HEN found, plugin loaded, CA installed. It
  refuses anything but firmware 4.93 CEX.
- **Switch to OpenPak:** backs up the stock `CA01.cer`/`CA59.cer` once to
  `/dev_hdd0/openpak/backup/`, writes the OpenPak CA over them in `dev_flash` (temp file,
  read-back check, rename), installs `openpak_ca.sprx` to `/dev_hdd0/plugins/` and adds its line
  to `/dev_hdd0/boot_plugins.txt`, then asks to restart.
- **Switch back to PSN:** restores the backups, removes the plugin line and file. Other plugins'
  lines are never touched.
- **DNS** is not written by the app (see deploy.md); the app shows the exact settings.
- **VSH plugin** -- `openpak_ca.sprx` applies three pokes to the running vsh process at boot
  (verify-before-patch, so a wrong firmware no-ops instead of bricking): the per-file SHA-1 gate so
  our replaced certs load, and the NP-profile issuer-CN pin so our chain is accepted.
- Log: `/dev_hdd0/openpak/patcher.log`.

The full hardware route, the CFW-vs-HEN decision, the CA-naming decision and the
firmware-version re-derivation are in
[`research/ps3/console-setup.md`](../../../../research/ps3/console-setup.md).

## Build

```sh
make stage     # fetch the CA (sha256-pinned), write dist/ and plugin/openpak_ca_patch.h
make plugin    # build openpak_ca.sprx  (needs PSL1GHT / ps3toolchain)
make pkg VERSION=0.2.0   # stage + plugin + OpenPak-PS3-Patcher-v0.2.0.pkg (in app/)
```

The CA is fetched pin-checked at build time and bundled in the pkg; the console never downloads it.
`make stage` needs only `curl`, `openssl` and `python3`. `make plugin` needs PSL1GHT; if it is not
installed locally, the tagged release build in CI produces the sprx and the pkg (see
`.github/workflows/release.yml`). No binary is committed.

For a firmware other than 4.93, re-derive the addresses (see the research doc) and export them:

```sh
ADDR_SHA1_BRANCH=0x... ADDR_CN_CMPLEN=0x... ADDR_CN_MEMLEN=0x... ADDR_CN_STRING=0x... make stage
```

## Install

See [docs/deploy.md](docs/deploy.md) for DNS and account setup, rollback and the manual route. What is patched and why: [docs/protocol.md](docs/protocol.md).

## Licence

See [LICENSE](LICENSE).
