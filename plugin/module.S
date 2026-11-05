/*
 * PS3 PRX module export table for openpak_ca.sprx.
 *
 * Layout verified byte-for-byte against a real firmware VSH plugin
 * (scratch/pier/decrypted/vshmod/ap_plugin.prx): a resident library entry exporting
 * module_start (NID 0xBC9A0086), module_stop (0xAB779874) and module_info (0xD7F43016), with no
 * imported libraries (lib.stub is empty). lv2.ld brackets .lib.ent with __libentstart/__libentend
 * and .lib.stub with __libstubstart/__libstubend; lv2-sprx.o's .sys_proc_prx_param references
 * those four symbols, and sprxlinker turns it into a loadable PRX.
 */

	/* module_info -- the 0xD7F43016 variable export. */
	.section .rodata.sceResident, "a"
	.align 2
openpak_module_info:
	.short 0x0000              /* attributes */
	.short 0x0101              /* version    */
#ifndef MODNAME
#define MODNAME "openpak_ca"
#endif
	.asciz MODNAME             /* name[28]   */
	.space 28 - 11
	.int 0                     /* toc base (loader uses the OPD TOC; 0 is fine with no imports) */
	.int __libentstart
	.int __libentend
	.int __libstubstart
	.int __libstubend

	/* NID and address tables for the resident exports. */
	.section .rodata.sceFNID, "a"
	.align 2
openpak_nid_table:
	.int 0xBC9A0086            /* module_start */
	.int 0xAB779874            /* module_stop  */
	.int 0xD7F43016            /* module_info  */

	.section .rodata.sceResident, "a"
	.align 2
openpak_add_table:
	.int openpak_opd_start     /* 8-byte {entry, toc} descriptors, see below */
	.int openpak_opd_stop
	.int openpak_module_info

	/* The resident library entry itself (0x1c bytes). */
	.section .lib.ent, "a"
	.align 2
openpak_lib_ent:
	.byte 0x1c                 /* struct size */
	.byte 0x00
	.short 0x0000              /* version */
	.short 0x8000              /* attribute: module's own resident library */
	.short 0x0002              /* num_func = module_start, module_stop */
	.short 0x0001              /* num_var  = module_info */
	.short 0x0000              /* num_tls */
	.int 0x00000000            /* reserved / hashes */
	.int 0                     /* libname = 0 (resident module entry) */
	.int openpak_nid_table
	.int openpak_add_table

	/* PS3 user-mode function descriptors are 8 bytes, {u32 entry, u32 toc} (the firmware's own PRXs,
	 * e.g. ap_plugin.prx, export those). The compiler's own .opd entries are the 24-byte ELFv1 kind;
	 * lv2 and Cobra read the exported start/stop entry and every thread entry as the 8-byte kind
	 * (Cobra: copy_from_process(.., &toc, sizeof(u32[2])) then creates the start thread from it), so an
	 * export that pointed at a 24-byte entry started the thread at address 0. The toc word is filled
	 * in by tools/elf2prx.py (symbols openpak_opd_*) because it is the module's TOC base. */
	.section .rodata.opd32, "a"
	.align 3
	.globl openpak_opd_start
openpak_opd_start:
	.int .module_start
	.int 0
	.globl openpak_opd_stop
openpak_opd_stop:
	.int .module_stop
	.int 0
	.globl openpak_opd_scan
openpak_opd_scan:
	.int .scan_thread
	.int 0
