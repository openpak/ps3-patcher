# PS3 Patcher

OpenPak's PS3 Patcher. It makes a PlayStation 3 trust OpenPak's PSN replacement: it stages the
OpenPak CA into the firmware trust store and ships a VSH plugin that applies the vsh pokes the
firmware needs to accept our certificate chain. Patch the console, never the game.

| | |
| --- | --- |
| Console | PlayStation 3 |
| Kind | Client (patcher) |
| Firmware | 4.93 retail (CEX); re-derive addresses for other versions |
| Setup | Evilnat CFW, or HFW + HEN (both run the stock 4.93 vsh) |
| Licence | AGPL-3.0-only, see [LICENSE](LICENSE) |

## What it does

1. **CA files** -- stages the OpenPak CA (fetched pin-checked from the network profile) as
   `dev_flash/data/cert/CA01.cer` and `CA59.cer` (PEM, the format this firmware's `CAnn.cer` use).
2. **VSH plugin** -- `openpak_ca.sprx`, loaded from `boot_plugins.txt`, applies three pokes to the
   running vsh process at boot (verify-before-patch, so a wrong firmware no-ops instead of
   bricking): the per-file SHA-1 gate so our replaced certs load, and the NP-profile issuer-CN pin
   so our chain is accepted.
3. Then you set the console's **DNS** to the OpenPak primary and sign in with an **OpenPak
   account**.

The full hardware route, the CFW-vs-HEN decision, the CA-naming decision and the
firmware-version re-derivation are in
[`research/ps3/console-setup.md`](../../../../research/ps3/console-setup.md).

## Build

```sh
make stage     # fetch the CA (sha256-pinned), write dist/ and plugin/openpak_ca_patch.h
make plugin    # build openpak_ca.sprx  (needs PSL1GHT / ps3toolchain)
```

`make stage` needs only `curl`, `openssl` and `python3`. `make plugin` needs PSL1GHT; if it is not
installed locally, the tagged release build in CI produces the sprx (see
`.github/workflows/release.yml`). No binary is committed.

For a firmware other than 4.93, re-derive the addresses (see the research doc) and export them:

```sh
ADDR_SHA1_BRANCH=0x... ADDR_CN_CMPLEN=0x... ADDR_CN_MEMLEN=0x... ADDR_CN_STRING=0x... make stage
```

## Install

See [docs/deploy.md](docs/deploy.md) for the step-by-step (back up first), DNS and account setup,
and rollback. What is patched and why: [docs/protocol.md](docs/protocol.md).

## Licence

See [LICENSE](LICENSE).
