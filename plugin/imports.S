/*
 * Imports of openpak_ca.sprx, in the PSL1GHT stub style (ppu/sprx/common/exports.S and libexport.c in
 * ps3dev/PSL1GHT build their stub libraries the same way): one 0x2c-byte header per library in
 * .lib.stub, the NID list in .rodata.sceFNID, the loader-filled pointer table in
 * .data.sceFStub.<library>, and a call stub per function in .sceStub.text with an OPD so C can call it.
 * sprxlinker only fixes the import counts; the order of the NID run and the pointer table must agree,
 * and the exports' NIDs (module.S) must come first in .rodata.sceFNID (link module.o first).
 *
 *   paf            View_Find 0xF21655F3, plugin_GetInterface 0x23AFB290   (names as in webMAN MOD vsh/paf.h)
 *   sysPrxForUser  sys_ppu_thread_create 0x24A1EA07, sys_ppu_thread_exit 0xAFF080A4
 */

	.section ".rodata.sceResident", "a"
	.align 2
paf_name:
	.asciz "paf"
sysprx_name:
	.asciz "sysPrxForUser"

	.section ".rodata.sceFNID", "a"
	.align 2
paf_fnid:
	.int 0xF21655F3            /* View_Find */
	.int 0x23AFB290            /* plugin_GetInterface */
sysprx_fnid:
	.int 0x24A1EA07            /* sys_ppu_thread_create */
	.int 0xAFF080A4            /* sys_ppu_thread_exit */

	.section ".data.sceFStub.paf", "aw"
	.align 2
paf_fstub:
	.int __paf_view_find
	.int __paf_plugin_get_interface

	.section ".data.sceFStub.sysPrxForUser", "aw"
	.align 2
sysprx_fstub:
	.int __sys_ppu_thread_create_
	.int __sys_ppu_thread_exit_

	.section ".lib.stub", "a"
	.align 2
	.int 0x2c000001            /* size 0x2c, version 1 */
	.short 0x0001              /* attributes */
	.short 0                   /* imports: set by sprxlinker */
	.int 0
	.int 0
	.int paf_name
	.int paf_fnid
	.int paf_fstub
	.int 0, 0, 0, 0
	.int 0x2c000001
	.short 0x0001
	.short 0
	.int 0
	.int 0
	.int sysprx_name
	.int sysprx_fnid
	.int sysprx_fstub
	.int 0, 0, 0, 0

	.macro IMPORT name, table, off
	.align 2
	.section ".sceStub.text", "ax"
	.globl __\name
__\name:
	mflr	0
	std	0,16(1)
	std	2,40(1)
	stdu	1,-128(1)
	lis	12,(\table + \off)@ha
	lwz	12,(\table + \off)@l(12)
	lwz	0,0(12)
	lwz	2,4(12)
	mtctr	0
	bctrl
	addi	1,1,128
	ld	2,40(1)
	ld	0,16(1)
	mtlr	0
	blr
	.align 3
	.section ".opd", "aw"
	.globl \name
\name:
	.quad __\name, .TOC.@tocbase, 0
	.endm

	IMPORT paf_view_find, paf_fstub, 0
	IMPORT paf_plugin_get_interface, paf_fstub, 4
	IMPORT sys_ppu_thread_create_, sysprx_fstub, 0
	IMPORT sys_ppu_thread_exit_, sysprx_fstub, 4
