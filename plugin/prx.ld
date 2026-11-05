/*
 * Linker script for openpak_*.sprx: a real PS3 PRX layout (what lv2's module loader accepts), not
 * an executable. Two load segments at vaddr 0 -- text and read-only data first (R+X), then data
 * (R+W) directly after it -- with no fixed addresses. tools/elf2prx.py turns the result into
 * ET_SCE_PPURELEXEC (0xFFA4) with a PT_SCE_PPURELA (0x700000a4) relocation segment, the shape of the
 * firmware's own VSH plugins (scratch/pier/decrypted/vshmod/ap_plugin.prx). The PSL1GHT lv2.ld
 * instead produces an ET_EXEC image at 0x10000/0x10000000; Cobra's loader answered it with
 * 0x80011148 (CELL_PRX_ERROR_UNSUPPORTED_PRX_TYPE).
 */
OUTPUT_FORMAT("elf64-powerpc", "elf64-powerpc", "elf64-powerpc")
OUTPUT_ARCH(powerpc:common64)
ENTRY(module_start)
PHDRS
{
	seg_text PT_LOAD FLAGS(5);
	seg_data PT_LOAD FLAGS(6);
}
SECTIONS
{
	. = 0;
	.text : { *(.text .text.* .sfpr .glink) } :seg_text
	.rodata : { *(.rodata .rodata.*) }
	.rodata.sceResident : { KEEP (*(.rodata.sceResident)) }
	.rodata.sceFNID     : { KEEP (*(.rodata.sceFNID .rodata.sceFNID.*)) }
	. = ALIGN(4);
	PROVIDE (__libentstart = .);
	.lib.ent  : { KEEP (*(.lib.ent)) }
	PROVIDE (__libentend = .);
	PROVIDE (__libstubstart = .);
	.lib.stub : { KEEP (*(.lib.stub)) }
	PROVIDE (__libstubend = .);
	. = ALIGN(16);
	.data : { *(.data .data.* .data.rel.ro .data.rel.ro.*) } :seg_data
	.opd  : ALIGN(8) { KEEP (*(.opd)) }
	.got  : ALIGN(8) { *(.got .toc) }
	.sbss : { *(.sbss .sbss.*) }
	.bss  : { *(.bss .bss.* COMMON) }
	/DISCARD/ : { *(.eh_frame) *(.comment) *(.gnu.*) *(.note.*) *(.interp) *(.dynamic) *(.dynsym) *(.dynstr) *(.hash) }
}
