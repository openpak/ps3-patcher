# The PS3 DNS catcher (v0.2.17 design)

The patcher becomes the PS3's `dns_mitm`, the same model as the Switch patcher. A lookup whose
name matches a family on the signed platform list is answered with the OpenPak box address
`145.241.199.19`; every other name resolves exactly as before, through the console's own DNS. The
console's DNS setting is never changed, so the owner's LAN names (Jellyfin, websites) are untouched.

This replaces the old string-rewrite / module-scan / name-table approach (v0.2.6 .. v0.2.16), which
tried to overwrite PSN host strings in vsh memory. The catcher works at the one place every resolver
path converges instead.

## Where the catcher sits: the BIND res_send Qhook

The PS3 resolver is BIND's `res_send()`. On 4.93 it is statically linked into **`vsh.self`** (there
is no separate `libnet` PRX in the vsh process; the standalone `libnet.sprx` carries the same code
for games). BIND keeps a per-resolver **query hook**: before every query is sent to a nameserver,
`res_send()` calls the function installed by `res_send_setqhook()`:

```
int qhook(struct sockaddr_in **ns, const u_char **query, int *querylen,
          u_char *ans, int anssiz, int *resplen);
```

A hook returns:

- `res_goahead` (0) — send the query to the nameserver as normal (every non-matching name);
- `res_done` (3) — do not send anything; `*resplen` bytes already written to `ans` are the reply.

So the catcher is a `qhook`: it decodes the single question, matches it against the signed list
(`catcher.h`), and for a match writes an A reply with the box address and returns `res_done`;
otherwise it returns `res_goahead` and the query goes to the console's configured DNS. Both the
synchronous `gethostbyname` (libnet NID `0x71f4c717`) and the async `cellNetDbResolver` enter
`res_send`, so one hook covers sign-in (libhttp → npsignin/regcam) and everything else.

### vsh.self 4.93 CEX addresses (verified against the decrypted image)

`scratch/pier/analysis/vsh/vsh.elf`, segment 0 loaded at `0x10000`. The `res_send` here is byte-for-byte
the same routine as `libnet.sprx`'s `res_send` (its hook-call block matched exactly), so this is the
real resolver, not a sibling:

| what | address | value |
| --- | --- | --- |
| `res_send`, hook read: `lwz r27,-28712(r2)` | `0x164c0` | `0x83628fd8` |
| next: `lwz r0,0(r27)` | `0x164c4` | `0x801b0000` |
| hook OPD read: `lwz r0,0(r9)` | `0x1650c` | `0x80090000` |
| hook call: `bctrl` | `0x16528` | `0x4e800421` |
| `res_done` branch: `bt 30,…` | `0x16550` | `0x419e0854` |
| `res_done` tail | `0x16da4` | `0x4bfff189` |
| TOC slot `*(TOC-28712)` → Qhook cell address | `0x6ee530` | `0x00706f1c` |
| **Qhook cell** (holds a pointer to the active hook's 8-byte OPD) | `0x706f1c` | `0x006e9480` at boot |
| firmware default hook OPD `{entry, toc}` | `0x6e9480` | `0x00017784`, `0x006f5558` |

Important: the cell is **not** zero at boot — the firmware installs its own default qhook (OPD
`0x6e9480`). So the install is a **chain**, not a bare set:

1. verify all nine values above;
2. save the current cell value (`0x6e9480`);
3. write the address of the plugin's own 8-byte `{entry, toc}` descriptor (`openpak_opd_qhook`,
   module.S) into the cell `0x706f1c` via PS3MAPI `SET_PROC_MEM`.

`res_send` then calls the plugin's `qhook` for every query. For a non-matching name the plugin's
hook calls the saved original (`0x6e9480`) through an 8-byte-descriptor trampoline
(`openpak_call_qhook`, module.S) and returns its result, so pass-through behaviour is exactly stock.
`module_stop` and any reboot restore `0x706f1c` to `0x6e9480`.

(The separate `res_send_setqhook`/`setrhook` pair at `0x59ee94`/`0x59eea0` writes a different,
unused cell `0x704198` = 0; it is not the active resolver's hook and is left alone.)

### Safety / never hang at boot

The install only *writes a pointer*; it never runs hook code at boot. The plugin's `qhook` is first
executed later, when the network stack performs a DNS lookup (STUN/console-info at the XMB, then
sign-in) — long after boot. So a wrong hook cannot block the boot itself; at worst a lookup misbehaves
while the XMB is already up and reachable.

The install is fully guarded: all nine values above must match, including the TOC slot resolving to
`0x706f1c` and the cell still holding the known default `0x6e9480`. If anything is off the hook is
left out and the status screen says `DNS catcher: off - <reason>` (`res_send not 4.93`,
`hook slot moved`, `default hook changed`, `another hook present`, `install write failed`,
`vsh not found`). Re-derive the addresses before enabling it on any other firmware.

### Recovery if a build misbehaves (verified, no XMB needed)

The plugin loads only because its line is in `/dev_hdd0/boot_plugins.txt`, and a reboot restores the
original resolver hook regardless. To stop it:

- **From the XMB** (it always reaches the XMB — see above): open the OpenPak Patcher and choose
  **Switch back to PSN**, which removes the `openpak_ca.sprx` line from `boot_plugins.txt` and the
  CA, then reboot. Or delete `/dev_hdd0/boot_plugins.txt` with any FTP homebrew and reboot.
- **Cobra/Evilnat recovery:** the console's **Recovery Menu / Safe Mode** (power off, then hold Power
  ~5 s until the second beep, release, press again) boots a minimal system that runs independently of
  the normal XMB VSH process, so it is reachable even if the XMB is unhappy; from there restore/exit
  returns to a normal boot where the file can be removed over FTP. Verified behaviour: Cobra loads
  `boot_plugins.txt` for the normal XMB only.

Because the hook never executes during boot and `module_stop`/reboot restore the original pointer,
there is no state that survives a reboot without `boot_plugins.txt` naming the plugin.

## The list

Same signed platform bundle and ceiling the Switch uses, with a `ps3` profile. The `ps3` families are
`.playstation.net .playstation.com .playstation.org .sonyentertainmentnetwork.com`, which cover the
whole 4.93 firmware host inventory (see `firmware-hosts-4.93.md`).

**v0.2.17 ships a bundled list verified at build time.** `tools/stage.sh` fetches the signed ceiling
(`GET /api/v1/network/ceiling`), verifies its Ed25519 signature against the pinned key on the build
host, intersects the `ps3` profile families with the verified ceiling, and emits `plugin/catchlist.h`
(`OPENPAK_LIST_VERIFIED "build"`). The build refuses to bundle a family the signed ceiling does not
allow. The screen shows this as `list build v<N>`.

**v0.2.18 (next) moves this onto the console:** the ed25519 verify at load, the boot/6-hour/reconnect
re-fetch, apply-live, and the cached-list-never-shrinks-to-fallback rule, with source order fetched →
bundled → compiled fallback. Tracked on card openpak/prds#125.

## The catcher core (`plugin/catcher.h`)

Freestanding C, no libc, shared verbatim by the VSH plugin, the host test
(`tools/test_catcher.c`), and Pier (`scratch/pier/src/rpcs3/Emu/NP/openpak_catcher.h`). It decodes
the one-question query, matches family / exact / never entries (lower-cased, fully-qualified dot
tolerated), and writes the A reply (TTL 60) or an empty NOERROR for non-A/AAAA. It never forwards a
caught name. `make test`-style: `cc -O2 -Wall -o t tools/test_catcher.c && ./t`.

## Proven on Pier first

Pier (OpenPak's RPCS3 fork) has the same catcher core wired into its DNS chokepoint
(`Emu/NP/np_dnshook.cpp`, enabled by `PIER_DNS_CATCHER=<list file>`). Run with the config's **DNS set
to plain upstream `1.1.1.1`** (not OpenPak DNS) and the list built from the live ceiling+profile:

```
DnsCatcher: catcher on: 4 families, answer 145.241.199.19 (every other name goes to the configured DNS)
  entry 0: family playstation.com   entry 1: family playstation.net
  entry 2: family playstation.org   entry 3: family sonyentertainmentnetwork.com
sys_net: [Native] Attempting to connect on 1.1.1.1:53          <- socket targets the console's own DNS
DnsCatcher: caught us.np.stun.playstation.net  (family playstation.net) -> 145.241.199.19
DnsCatcher: caught ena.net.playstation.net     (family playstation.net) -> 145.241.199.19
```

The console is pointed at `1.1.1.1`, yet PSN family names are answered with the box address locally
and a non-matching name would get `1.1.1.1`'s real answer — exactly the selective-redirect model.
Evidence: `scratch/ps3cat/pier-catcher-evidence.txt`, full log
`scratch/pier/logs/catcher-proof-*.log`. The firmware's own NP bootstrap (STUN, console-info) fires
these lookups at boot; a full credential sign-in (getCountries → bindAccount) is driven from the XMB
sign-in form, which needs a visible window and input (owner-gated; the box DNS → np-account leg of
that flow is already proven in prior Pier sessions, `openpak-pier` memory).
