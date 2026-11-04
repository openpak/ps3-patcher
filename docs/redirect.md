# PSN name redirect

OpenPak patches the console, never games. The console must reach OpenPak for PSN names without the
user touching DNS. The plugin does it by rewriting the PSN host names the firmware already holds in
memory.

## Where the idea comes from

webMAN MOD's offline mode (`block_online_servers()` / `restore_blocked_urls()`,
`include/ps3mapi/stealth.h`) walks two real-memory ranges with the lv1 peek syscall, looks for a
handful of PSN URL prefixes and overwrites the start of each with NUL bytes, keeping the original
words so `restore_servers` can write them back:

| Range | Real address | Size |
|---|---|---|
| LV1 | `0x880000` .. `0xE1FFFF` | 6 MB |
| LV2 | lv2 base + `0x300000` .. `0x7FFFF8` | 5 MB |

It refuses under PS3HEN (no lv1 poke) and while a game runs. This plugin uses the same ranges, the
same lv1 peek/poke, and the same keep-the-originals restore, but writes an OpenPak host name of the
same length instead of NULs.

## The rewrite

Every PSN host name ends in `playstation.net` or `playstation.org`: 15 characters, exactly as long
as `ps3.openpak.org`. So the rewrite is a suffix swap and no byte ever moves:

    auth.api.np.ac.playstation.net  ->  auth.api.np.ac.ps3.openpak.org

`names/psn-names.txt` lists the suffixes (11 entries). It leaves out XML namespace URIs
(`http://playstation.net/np/rt/...`, compared with what the server sends) and JID formats.

`*.ps3.openpak.org` resolves through the public wildcard record, and `np-account` signs a leaf for
whatever name the console asks for (SNI), so the console needs no DNS setting and no new certificate.

## Behaviour

- **Boot:** `openpak_ca.sprx` applies the three trust pokes, then starts a low-priority thread. After
  5 seconds it scans both ranges in 4 KiB windows (3 ms pause between windows), rewrites each match
  (the whole name must be present; the write is read back, and undone if it does not verify), and
  repeats: every ~2 s for about 24 passes, every 20 s for 36 more, then every 2 minutes. Libraries
  the firmware loads on first use (the sign-in library) appear in memory only then; the next scan
  catches them.
- **lv1 access:** Cobra PS3MAPI through syscall 8 (`0x7777`, `0x1008` / `0x1009`), falling back to
  the plain CFW syscalls 8 / 9. The lv2 base is found by comparing the lv1 view with the syscall 6
  view of the kernel image; no match means no rewrite.
- **Restore:** module stop writes every original word back, newest first (webMAN's
  `restore_servers`). RAM never keeps a rewrite across a reboot, so "Switch back to PSN" plus a restart
  is a full restore.
- **Persistence:** the plugin is in `boot_plugins.txt`, so it does this on every boot.
- **Status:** `/dev_hdd0/openpak/redirect.status` has `state=ok names=N of=M live=L passes=P failed=F
  via=...`, then one line per entry with its live count. The app shows `Redirect: N names`.

## Limits

- Game processes are not covered. A game has its own copies of the libraries in its own memory,
  which this scan does not reach; webMAN's offline mode does not either. Names a game receives from
  the server (service lists in the ticket reply) are the server's to name.
- Under PS3HEN the plugin does nothing (no lv1 poke).
- A name inside a library that loads on first use is rewritten on the next scan, so the first
  sign-in attempt after boot can fail; try again.
