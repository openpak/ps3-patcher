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
	.asciz "openpak_ca"        /* name[28]   */
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
	.int module_start          /* ELFv1: resolves to the function's .opd descriptor */
	.int module_stop
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
