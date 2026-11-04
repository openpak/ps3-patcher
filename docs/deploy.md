# Deploy: installing the PS3 Patcher on a console

Read `research/ps3/console-setup.md` first for the route choice and the firmware-version check.
**A bad `dev_flash` write can brick. The app backs up first, writes through a temp file, reads it
back and only then renames; it still helps to have a flash backup.**

## Requirements

- **Firmware 4.93 CEX** (Settings > System Settings > System Information). The app refuses any
  other version and changes nothing.
- **Evilnat CFW 4.93 (preferred) or HFW 4.93 + HEN** (re-enable HEN after every boot). The app
  needs the Cobra/HEN syscalls to mount `dev_flash` writable and to load the plugin at boot.
- A USB stick (FAT32) and about 1 MB free on `dev_flash`.

## 1. Install the package

1. Download `OpenPak-PS3-Patcher-<tag>.pkg` from the
   [releases](https://github.com/openpak/ps3-patcher/releases) and copy it to the **root** of the USB stick.
2. Plug it in. XMB: **Game > Package Manager > Install Package Files > Standard**, choose the pkg.
   Title id `OPNP00001`; it appears in the Game column as **OpenPak PS3 Patcher**.

## 2. Switch to OpenPak

Start the app. It shows Network, Firmware, Custom firmware, Plugin and OpenPak CA. Choose **Yes**
at "Switch to OpenPak?". It then:

1. backs up `/dev_flash/data/cert/CA01.cer` and `CA59.cer` to `/dev_hdd0/openpak/backup/` (once; an
   existing backup is never overwritten),
2. copies `openpak_ca.sprx` to `/dev_hdd0/plugins/` and appends
   `/dev_hdd0/plugins/openpak_ca.sprx` to `/dev_hdd0/boot_plugins.txt` (other lines untouched),
3. mounts `dev_flash` writable (`/dev_blind`), checks free space, and replaces the two CA files via
   temp name + read-back + rename; if the second fails the first is restored from the backup,
4. unmounts `dev_blind`.

Everything is logged to `/dev_hdd0/openpak/patcher.log`. Answer **Yes** to restart.

## 3. DNS

The app does not write the network settings (no public tool is known to edit the xRegistry network
profile safely, so it shows the steps). Settings > Network Settings > Internet Connection Settings >
**Custom**. IP Address: Automatic is fine. At **DNS** choose **Manual**, **Primary DNS =
`145.241.199.19`** (Secondary may be the same). Defaults for MTU/proxy/UPnP. Run the connection test.

## 4. Sign in

Create an OpenPak account on the website first (it owns the identity). On the PS3: PlayStation
Network > **Use an Existing Account** > the OpenPak account **email** and **password**. The firmware
sends `bindAccount` to `native-ps3.np.ac.playstation.net`; `np-account` verifies it against the
`account` core. See `research/ps3/np-account-design.md`.

After the restart, start the app again: Plugin should read "loaded, 3/3 patches". "only n/3" means
the vsh did not match the expected 4.93 opcodes; sign-in will then fail cleanly.

## Switch back to PSN

Start the app and choose **Yes** at "Switch back to PSN?". It restores both CA files from the
backup, removes the plugin line and file, and asks to restart. Then set DNS back to **Automatic**.
The backups stay in `/dev_hdd0/openpak/backup/`.

## Appendix: manual route (FTP)

Needs webMAN MOD or similar FTP with `dev_flash` writable.

1. Build or take the files from the release zip: `dev_flash/data/cert/CA01.cer`, `CA59.cer`,
   `openpak_ca.sprx`.
2. Rename the existing `CA01.cer` > `CA01.cer.orig` and `CA59.cer` > `CA59.cer.orig`, upload ours.
3. Put `openpak_ca.sprx` in `/dev_hdd0/plugins/` and add its path as a line to
   `/dev_hdd0/boot_plugins.txt`. Reboot.
4. Rollback: remove the line, restore the `.orig` files, DNS to Automatic.

## Notes

- The OpenPak ps3 TLS terminator in front of `np-account` must offer RSA key-exchange suites and
  TLS 1.0-1.2 (the 4.93 stack offers no ECDHE) -- server side, see
  `research/ps3/psn-signin-findings.md`.
- How `dev_blind` is mounted: lv2 syscall 837 (`sys_fs_mount`) with device
  `CELL_FS_IOS:BUILTIN_FLSH1`, filesystem `CELL_FS_FAT`, path `/dev_blind`, write-protect 0;
  unmount is syscall 838. Same calls as webMAN MOD's standalone homebrew (`_Projects_/updater`,
  `include/file/file_devs.h` in github.com/aldostools/webMAN-MOD).
- Proven on Pier only: the TLS/trust behaviour. Not yet run on a real console: the package install,
  the dev_blind write, the plugin load and the runtime pokes.
