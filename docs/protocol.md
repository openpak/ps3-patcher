# Protocol: what the PS3 Patcher patches, and why

OpenPak patches the console, not the game. For a PS3 to use OpenPak's PSN replacement the firmware
must (1) trust the OpenPak CA, (2) resolve `*.playstation.net` to OpenPak, and (3) accept our chain
in vsh's pinned check. DNS is XMB configuration (see deploy.md). This document covers 1 and 3,
which both live inside `vsh.self`.

All addresses are firmware **4.93 retail CEX** `vsh.self` virtual addresses, confirmed from the
disassembly in `scratch/pier/analysis/vsh/vsh.s` and summarised in
`research/ps3/console-setup.md`. Re-derive them for any other version (byte patterns in the
research doc).

## Why files alone do not work

The boot trust store is built by `vsh.self` itself (not `libssl.prx`). vsh carries its own 59-entry
pin table (descriptors at vaddr `0x7023c0`, `0x30` bytes each, 20-byte SHA-1 at `+0x14`). The
per-file routine `FUN_00588fe0` hashes each `CAnn.cer` and compares it to the pinned SHA-1; any
byte difference aborts the whole store build, so `cellHttpsInit` never succeeds and every `https`
URI returns `0x80710007` with no traffic. `CA_LIST.cer` and a new `CA60.cer` are not read at all.
So we must both replace a `CAnn.cer` **and** change what vsh enforces.

## The CA files

`dev_flash/data/cert/CA01.cer` and `CA59.cer` are replaced with the OpenPak CA, **PEM** encoded
(this is the format the firmware's `CAnn.cer` use; the store loader runs a PEM scanner looking for
`-----END `). Both ends are written because the boot store reads `CA01..CA59` while the NP login
service reads only `CA01-05`. The originals are kept as `CA01.cer.orig` / `CA59.cer.orig`.

The CA is fetched from `https://openpak.org/api/v1/network/profile?platform=ps3` and verified
against the profile's `sha256` pin before use (same contract as `scratch/pier/install-ca.sh`).

## The three vsh pokes

Applied by `openpak_ca.sprx` to the running vsh process, each verify-before-patch.

### 1. File-content gate -- `0x589198`
```
589194  cmpwi cr7, r3, 0          ; r3 = memcmp(pin, sha1(file), 20)
589198  bf 30, 0x5891a0           ; 40 9e 00 08  (orig)  -> 60 00 00 00 (nop)
58919c  li r31, 0                 ; success path
```
Nop'ing the mismatch branch makes every `CAnn.cer` load regardless of content, so our replaced
files are accepted. One write, independent of the file bytes (robust against newline drift).
*Alternative (not used by default):* rewrite the pin of each replaced slot with the SHA-1 of the
installed file -- CA59 pin at `0x7034b4`, CA01 pin at `0x7023d4` -- keeping the gate live for the
other files but fragile.

### 2 & 3. NP-profile issuer-CN pin -- `0x22ca08`, `0x22ca1c`, string `0x684188`
```
22ca08  cmpwi cr7, r0, 17         ; 2f 80 00 11  issuer-CN length must be 17
22ca18  addi r4, r4, 16776        ; r4 = 0x684188 (pinned CN field)
22ca1c  li r5, 17                 ; 38 a0 00 11  memcmp length
22ca20  bl memcmp                 ; memcmp(CN, "SCEI DNAS Root 05", 17)
```
vsh pins the issuer CN to the exact 17-byte string `SCEI DNAS Root 05` in a 24-byte field at
`0x684188`. To accept the OpenPak CA (`OpenPak Network CA`, 18 bytes):
- `0x22ca08` -> `0x2f800000 | len`  (length compare)
- `0x22ca1c` -> `0x38a00000 | len`  (memcmp length)
- `0x684188` -> our CN, NUL-padded to 24 bytes

`tools/stage.sh` generates these three values from the CN of whatever CA the profile serves. If
that CN is exactly `SCEI DNAS Root 05` (the backend chose to match the stock pin), the two CN pokes
are identity no-ops and the string overwrite is skipped -- only poke 1 is needed. See the
CA-naming decision in the research doc.

## Delivery

The pokes are applied at runtime by a VSH plugin, not by modifying `vsh.self` on disk: nothing on
`dev_flash` changes except the two `.cer` files, so rollback is removing the plugin line and
restoring the `.orig` certs. `openpak_ca_patch.h` (generated) carries the poke values and the
verify-before-patch originals; `openpak_ca.c` applies them in `module_start`. `module.S` carries
the PRX export table — module_start (NID `0xBC9A0086`), module_stop (`0xAB779874`) and module_info
(`0xD7F43016`), no imported libraries — modelled byte-for-byte on a real firmware VSH plugin, so
`sprxlinker` + `fself` produce the sprx Cobra loads. The plugin build is verified to produce a
well-formed SCE fake-self; loading and running it on real hardware is not yet verified.
