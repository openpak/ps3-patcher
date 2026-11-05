#!/usr/bin/env python3
"""elf2prx.py IN.elf OUT.prx

Turn the ET_EXEC image that `ppu-gcc -Wl,-q -T plugin/prx.ld` produces (two PT_LOAD segments at vaddr
0, relocations kept) into a PS3 PRX: ET_SCE_PPURELEXEC (0xFFA4), OSABI 0x66, no section headers,
segment 0 = text/rodata, segment 1 = data, plus a PT_SCE_PPURELA (0x700000a4) segment of 24-byte
relocation records {offset u64, unk u16, index_value u8, index_addr u8, type u32, addend u64}, where
offset is relative to the segment holding the field and addend relative to the segment index_value.
Layout and record format follow the firmware's own VSH plugins (ap_plugin.prx) and RPCS3's
ppu_load_prx().

p_paddr of segment 0 is the file offset of the module info (openpak_module_info), as in firmware
PRXs; its toc field is relocated to the TOC base (.TOC.).
"""
import struct
import sys

R_ADDR32, R_ADDR64, R_TOC = 1, 38, 51
# PPC64 ELF relocation numbers that stay valid wherever the segments land: REL24 10, REL14 11, REL32 26,
# REL64 44 (pc-relative inside one segment) and TOC16 47-50, TOC16_DS 63, TOC16_LO_DS 64 (r2-relative
# into the data segment, where every TOC entry lives).
SKIP = {10, 11, 26, 44, 47, 48, 49, 50, 63, 64}


def u(fmt, d, o):
    return struct.unpack_from(">" + fmt, d, o)


def main(src, dst):
    d = open(src, "rb").read()
    assert d[:4] == b"\x7fELF" and d[4] == 2 and d[5] == 2
    phoff, shoff = u("QQ", d, 32)[0], u("Q", d, 40)[0]
    phnum = u("H", d, 56)[0]
    shnum, shstrndx = u("H", d, 60)[0], u("H", d, 62)[0]
    shentsize = u("H", d, 58)[0]

    segs = []
    for i in range(phnum):
        t, fl, off, va, pa, fs, ms, al = u("IIQQQQQQ", d, phoff + 56 * i)
        if t == 1:
            segs.append(dict(off=off, va=va, fs=fs, ms=ms))
    if len(segs) != 2 or segs[0]["va"] != 0:
        sys.exit("expected two PT_LOAD segments, the first at vaddr 0, got %r" % segs)

    secs = []
    for i in range(shnum):
        n, ty, fl, addr, off, size, link, info, al, es = u("IIQQQQIIQQ", d, shoff + shentsize * i)
        secs.append(dict(name=n, type=ty, flags=fl, addr=addr, off=off, size=size, link=link, info=info, es=es))
    strsec = secs[shstrndx]
    for s in secs:
        e = d.index(b"\0", strsec["off"] + s["name"])
        s["nm"] = d[strsec["off"] + s["name"]:e].decode()

    symtab = next(s for s in secs if s["type"] == 2)
    strtab = secs[symtab["link"]]
    syms = []
    for k in range(symtab["size"] // 24):
        nm, info, other, shndx, val, sz = u("IBBHQQ", d, symtab["off"] + 24 * k)
        e = d.index(b"\0", strtab["off"] + nm)
        syms.append(dict(name=d[strtab["off"] + nm:e].decode(), shndx=shndx, val=val, info=info))
    byname = {s["name"]: s for s in syms if s["name"]}

    def segof(addr, inclusive=False):
        for i, sg in enumerate(segs):
            if sg["va"] <= addr < sg["va"] + sg["ms"] or (inclusive and addr == sg["va"] + sg["ms"]):
                return i
        return None

    def seg_of_symbol(si, value):
        """Segment a symbol belongs to. By its section when that has bytes (so a pointer to the first
        byte of .data is a data pointer although segment 0 ends exactly there); otherwise by value,
        preferring segment 0 at the boundary (the end-of-table symbols __libentend/__libstubend)."""
        sy = syms[si]
        if 0 < sy["shndx"] < 0xff00:
            sec = secs[sy["shndx"]]
            if sec["flags"] & 2 and sec["size"]:
                return segof(sec["addr"])
        return segof(value, True)

    # The TOC base is not in the symbol table; every .opd entry carries it (R_PPC64_TOC), already
    # resolved by the linker.
    toc = None
    for rs in secs:
        if rs["type"] != 4:
            continue
        for k in range(rs["size"] // 24):
            roff, rinfo, radd = u("QQq", d, rs["off"] + 24 * k)
            if rinfo & 0xFFFFFFFF == R_TOC:
                ls = segof(roff)
                v = int.from_bytes(d[segs[ls]["off"] + roff - segs[ls]["va"]:][:8], "big")
                if toc is not None and toc != v:
                    sys.exit("two different TOC bases")
                toc = v
    if toc is None or toc < segs[1]["va"]:
        sys.exit("TOC base not found (or below the data segment)")
    modinfo = byname["openpak_module_info"]["val"]

    def image(i):
        sg = segs[i]
        b = bytearray(d[sg["off"]:sg["off"] + sg["fs"]])
        return b

    imgs = [image(0), image(1)]

    relocs = []   # (locseg, offset, valseg, type, ptr)
    for rs in secs:
        if rs["type"] != 4:      # SHT_RELA
            continue
        tgt = secs[rs["info"]]
        if not tgt["flags"] & 2:   # not allocated
            continue
        for k in range(rs["size"] // 24):
            roff, rinfo, radd = u("QQq", d, rs["off"] + 24 * k)
            rtype, rsym = rinfo & 0xFFFFFFFF, rinfo >> 32
            ls = segof(roff)
            if ls is None:
                sys.exit("reloc outside any segment at %#x (%s)" % (roff, rs["nm"]))
            lo = roff - segs[ls]["va"]
            if rtype in (R_ADDR32, R_ADDR64):
                size = 4 if rtype == R_ADDR32 else 8
                val = int.from_bytes(imgs[ls][lo:lo + size], "big")
                vs = seg_of_symbol(rsym, val)
                if vs is None:
                    sys.exit("reloc target %#x outside any segment (%s)" % (val, rs["nm"]))
                relocs.append((ls, lo, vs, rtype, val - segs[vs]["va"]))
            elif rtype == R_TOC:
                relocs.append((ls, lo, 1, R_ADDR64, toc - segs[1]["va"]))
            elif rtype in SKIP:
                # position independent only while source and target stay in one segment (calls) or the
                # TOC entry is in the data segment
                if rtype in (47, 48, 49, 50, 63, 64):
                    if ls != 0 and ls != 1:
                        sys.exit("bad toc16 location")
                continue
            else:
                sys.exit("unsupported relocation type %d in %s at %#x" % (rtype, rs["nm"], roff))

    # module info: the toc field, and the table words must be relocated even if the linker left them
    mi_seg = segof(modinfo)
    mo = modinfo - segs[mi_seg]["va"]
    have = {(r[0], r[1]) for r in relocs}
    if (mi_seg, mo + 0x20) not in have:
        relocs.append((mi_seg, mo + 0x20, 1, R_ADDR32, toc - segs[1]["va"]))
    for f in (0x24, 0x28, 0x2c, 0x30):
        if (mi_seg, mo + f) not in have:
            sys.exit("module info field +%#x has no relocation" % f)

    relocs.sort(key=lambda r: (r[0], r[1]))
    rel = b"".join(struct.pack(">QHBBIQ", r[1], 0, r[2], r[0], r[3], r[4]) for r in relocs)

    hdr = 0x40 + 3 * 56
    hdr = (hdr + 15) & ~15
    # segment 1 vaddr = end of segment 0 is what the linker script produced (16-aligned)
    off0 = hdr + segs[0]["va"]
    off1 = hdr + segs[1]["va"]
    end1 = off1 + segs[1]["fs"]
    offr = (end1 + 15) & ~15
    out = bytearray(offr + len(rel))
    out[0:16] = bytes([0x7f, 0x45, 0x4c, 0x46, 2, 2, 1, 0x66, 0, 0, 0, 0, 0, 0, 0, 0])
    struct.pack_into(">HHIQQQIHHHHHH", out, 16, 0xFFA4, 21, 1, 0, 64, 0, 0x01000000, 64, 56, 3, 0, 0, 0)
    ph = 64
    struct.pack_into(">IIQQQQQQ", out, ph, 1, 0x400005, off0, segs[0]["va"], off0 + modinfo, segs[0]["fs"], segs[0]["ms"], 0x10)
    struct.pack_into(">IIQQQQQQ", out, ph + 56, 1, 0x600006, off1, segs[1]["va"], 0, segs[1]["fs"], segs[1]["ms"], 0x10)
    struct.pack_into(">IIQQQQQQ", out, ph + 112, 0x700000a4, 0, offr, 0, 0, len(rel), 0, 0x10)
    out[off0:off0 + segs[0]["fs"]] = imgs[0]
    out[off1:off1 + segs[1]["fs"]] = imgs[1]
    out[offr:offr + len(rel)] = rel
    open(dst, "wb").write(out)
    print("prx: seg0 %#x+%#x seg1 %#x+%#x(mem %#x) module info %#x toc %#x, %d relocations" % (
        segs[0]["va"], segs[0]["fs"], segs[1]["va"], segs[1]["fs"], segs[1]["ms"], modinfo, toc, len(relocs)))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
