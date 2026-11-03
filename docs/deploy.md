# Deploy: installing the PS3 Patcher on a console

Read `research/ps3/console-setup.md` first for the route choice and the firmware-version check.
This is the step list. **Back up before any `dev_flash` write -- a bad write can brick.**

## 0. Prerequisites (ask the owner)

- Exact firmware version (Settings -> System -> System Information). These addresses are **4.93
  retail CEX** only; re-derive for anything else (research doc).
- CFW or HEN present. Needed to write `dev_flash` and to load a VSH plugin:
  - **Evilnat CFW 4.93 (CEX)** -- preferred, persistent.
  - **HFW 4.93 + HEN** -- for OFW-locked consoles; works identically but HEN must be re-enabled
    after each boot.
- Console model (CECH-xxxx) -- decides CFW-capable vs HEN-only.
- A way to reach the console's filesystem: webMAN MOD (FTP) or multiMAN.

## 1. Back up

- Full NAND/NOR flash dump if you have a hardware flasher, E3, or multiMAN's flash backup.
- At minimum, FTP-copy the whole `/dev_flash/data/cert/` directory off the console.

## 2. Build the files

From the repo root on your workstation:
```sh
make stage     # writes dist/dev_flash/data/cert/{CA01,CA59}.cer and plugin/openpak_ca_patch.h
make plugin    # builds plugin/openpak_ca.sprx  (needs PSL1GHT; else take it from the CI release)
```

## 3. Install the CA files

Over FTP (webMAN MOD), into `/dev_flash/data/cert/`:
1. Rename the existing `CA01.cer` -> `CA01.cer.orig` and `CA59.cer` -> `CA59.cer.orig` (keep them;
   this is your file-level rollback).
2. Upload `dist/dev_flash/data/cert/CA01.cer` and `CA59.cer`.

`dev_flash` must be mounted writable -- CFW does this; under HEN, webMAN's FTP exposes it writable
while HEN is active. If the upload is refused, `dev_flash` is not writable: do not force it.

## 4. Install the VSH plugin

1. Copy `plugin/openpak_ca.sprx` to `/dev_hdd0/plugins/openpak_ca.sprx` (any readable path works).
2. Add its path as a line in `/dev_hdd0/boot_plugins.txt` (create the file if absent). One plugin
   path per line, for example:
   ```
   /dev_hdd0/plugins/openpak_ca.sprx
   ```
3. Reboot. Cobra loads the plugin into the vsh process; it verifies the original opcodes and
   applies the pokes. If the firmware is not the one these addresses target, it no-ops and the
   trust change silently does not take (sign-in will fail cleanly) -- re-derive and rebuild.

## 5. DNS

Settings -> Network Settings -> Internet Connection Settings -> **Custom**. IP Address: Automatic
is fine. At **DNS**, choose **Manual**, **Primary DNS = `145.241.199.19`** (Secondary may be the
same). Defaults for MTU/proxy/UPnP. Run the connection test.

## 6. Sign in

Create an OpenPak account on the website first (it owns the identity). On the PS3: PlayStation
Network -> **Use an Existing Account** -> enter the OpenPak account **email** and **password**.
The firmware sends `bindAccount` to `native-ps3.np.ac.playstation.net`; `np-account` verifies it
against the `account` core and returns the NP credential and your **online ID**. See
`research/ps3/np-account-design.md`.

## Rollback

1. Remove the `openpak_ca.sprx` line from `/dev_hdd0/boot_plugins.txt` (or delete the sprx);
   reboot. The pokes were RAM-only; they are gone.
2. Restore `CA01.cer.orig` -> `CA01.cer` and `CA59.cer.orig` -> `CA59.cer`.
3. Set DNS back to Automatic.

## Notes

- The OpenPak ps3 TLS terminator in front of `np-account` must offer RSA key-exchange suites and
  TLS 1.0-1.2 (the 4.93 stack offers no ECDHE) -- a server-side prerequisite, see
  `research/ps3/psn-signin-findings.md`. Not a console step.
- Everything here was proven on Pier only; the VSH-plugin delivery and the runtime poke have not
  yet been run on real hardware.
