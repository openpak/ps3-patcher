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

`scratch/pier/analysis/vsh/vsh.elf`, segment 0 loaded at `0x10000`:

| what | address | bytes |
| --- | --- | --- |
| `res_send_setqhook`: `lwz r9,-30940(r2); stw r3,0(r9); blr` | `0x59ee94` | `81 22 87 24  90 69 00 00  4e 80 00 20` |
| `res_send` Qhook call: `lwz r9,-30940(r2); lwz r9,0(r9); lwz r0,0(r9); …; bctrl` | `0x5a03b8` | `81 22 87 24 … 4e 80 04 21` |
| vsh TOC base (from the vsh entry OPD `0x6d5ac8`) | `0x6f5558` | — |
| Qhook pointer cell = `*(TOC-30940)` = `*(0x6edc7c)` | `0x704198` | `0` at boot |

The install is: build a guest 8-byte `{entry, toc}` descriptor for the plugin's `qhook`, then store
its address at `0x704198` in the vsh process (through PS3MAPI `SET_PROC_MEM`, like the CA pokes).
`res_send` then calls it for every query.

The standalone `libnet.sprx` carries the same `res_send` (its own copy of these bytes at text
`+0x9980`, setqhook at `+0x9464`, Qhook cell at data `+0x544`); a game process that resolves through
`libnet` is covered the same way once its module is seen.

### Safety / never hang at boot

Every constant above is checked before the install: the exact instruction words at `0x59ee94`,
`0x5a03b8` and the surrounding bytes must match, the TOC must be `0x6f5558`, the cell must resolve to
`0x704198`, and that cell must still be `0` (no other qhook installed). If any check fails the hook
is **not** installed — the patcher no-ops and sign-in fails cleanly rather than risking a vsh hang.
Re-derive the addresses before enabling it on any other firmware.

## The list

Same signed platform bundle and ceiling the Switch uses, with a `ps3` profile. Source order:
fetched copy, then the bundled copy, then the compiled fallback. The live platform map is
`deploy/netprofile-platforms.yml` on the box; the `ps3` families are
`.playstation.net .playstation.com .playstation.org .sonyentertainmentnetwork.com`, which cover the
whole 4.93 firmware host inventory (see `firmware-hosts-4.93.md`). Refresh at boot, every 6 h and on
reconnect; a failed fetch keeps the last good list and never shrinks to the fallback while a cached
copy exists.

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
