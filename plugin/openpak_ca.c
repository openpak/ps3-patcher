/*
 * openpak_ca.sprx -- OpenPak PS3 trust VSH plugin.
 *
 * Loaded into the vsh process at boot from /dev_hdd0/boot_plugins.txt on a Cobra-enabled console
 * (Evilnat CFW or HFW+HEN). It applies the three vsh pokes that let the firmware trust the
 * OpenPak CA installed in dev_flash/data/cert/CA01.cer and CA59.cer:
 *
 *   1. file-content SHA-1 gate  (0x589198 bf 30,+8 -> nop)  so our replaced CAnn.cer load,
 *   2. NP-profile issuer-CN length compare (0x22ca08),
 *   3. NP-profile issuer-CN memcmp length (0x22ca1c) + the pinned CN string at 0x684188,
 *
 * so the firmware's pinned check accepts our chain. Addresses, opcodes and the full rationale are
 * in research/ps3/console-setup.md; the values here are generated into openpak_ca_patch.h by
 * tools/stage.sh from the CA the network profile serves.
 *
 * SAFETY: every poke is verify-before-patch. If the word at the target address is not the exact
 * original this plugin expects, that poke is skipped. On a firmware these 4.93 addresses do not
 * match, the plugin therefore no-ops instead of corrupting vsh -- the trust change simply does
 * not take and sign-in fails cleanly. Re-derive the addresses (console-setup.md) before forcing
 * it on another version.
 *
 * No imports: it touches only its own address space (the vsh process) with direct stores and an
 * icache flush, so the module needs no lib.stub. The PRX module export table (module_start /
 * module_stop / module_info) is in module.S. Build with PSL1GHT (see Makefile); no binary is
 * committed. Loading as a VSH plugin is UNVERIFIED on real hardware (proven on Pier only).
 */

#include "openpak_ca_patch.h"

#define SYS_PRX_RESIDENT 0
#define SYS_PRX_STOP_OK  0

typedef unsigned int u32;
typedef unsigned long long u64;

/* vsh text is mapped executable+writable in the vsh process under Cobra, so a direct store
 * reaches it; we still flush the instruction cache for the lines we touch. */
static void write_u32(u32 addr, u32 val)
{
	*(volatile u32 *)(u64)addr = val;
	__asm__ volatile("dcbst 0,%0; sync; icbi 0,%0; isync" : : "r"((u64)addr) : "memory");
}

static u32 read_u32(u32 addr)
{
	return *(volatile u32 *)(u64)addr;
}

/* Raw lv2 syscall (no imports, so the module keeps an empty lib.stub). */
static long sc4(long n, long a, long b, long c, long d)
{
	register long r11 __asm__("11") = n;
	register long r3 __asm__("3") = a;
	register long r4 __asm__("4") = b;
	register long r5 __asm__("5") = c;
	register long r6 __asm__("6") = d;
	register long r7 __asm__("7") = 0;
	register long r8 __asm__("8") = 0;
	__asm__ volatile("sc"
			 : "+r"(r3), "+r"(r4), "+r"(r5), "+r"(r6), "+r"(r7), "+r"(r8), "+r"(r11)
			 :
			 : "r0", "r9", "r10", "r12", "lr", "ctr", "xer", "cr0", "cr1", "cr5",
			   "cr6", "cr7", "memory");
	return r3;
}

/* Boot status for the Patcher app: /dev_hdd0/openpak/plugin.status = "applied=A verified=V total=T".
 * Best effort; failure to write changes nothing about the pokes. */
static void write_status(int applied, int verified, int total)
{
	char buf[48];
	int n = 0;
	int vals[3];
	static const char *const keys[3] = { "applied=", "verified=", "total=" };
	int k, j;
	int fd = -1;
	unsigned long long wrote = 0;

	vals[0] = applied; vals[1] = verified; vals[2] = total;
	for (k = 0; k < 3; k++) {
		for (j = 0; keys[k][j]; j++)
			buf[n++] = keys[k][j];
		buf[n++] = (char)('0' + (vals[k] / 10) % 10);
		buf[n++] = (char)('0' + vals[k] % 10);
		buf[n++] = (k == 2) ? '\n' : ' ';
	}
	/* sys_fs_open(path, O_WRONLY|O_CREAT|O_TRUNC, &fd, mode 0666, NULL, 0) */
	if (sc4(801, (long)"/dev_hdd0/openpak/plugin.status", 0x241, (long)&fd, 0666) != 0)
		return;
	sc4(803, fd, (long)buf, n, (long)&wrote);   /* sys_fs_write */
	sc4(804, fd, 0, 0, 0);                      /* sys_fs_close */
}

static void apply_pokes(void)
{
	unsigned int i;
	int applied = 0, verified = 0, total = 0;

	for (i = 0; i < sizeof(openpak_u32_pokes) / sizeof(openpak_u32_pokes[0]); i++) {
		total++;
		if (read_u32(openpak_u32_pokes[i].addr) == openpak_u32_pokes[i].orig) {
			verified++;
			write_u32(openpak_u32_pokes[i].addr, openpak_u32_pokes[i].val);
			applied++;
		}
	}

#if !OPENPAK_SKIP_CN_POKES
	/* Overwrite the 24-byte pinned CN field only if it still begins with "SCEI". */
	total++;
	if (read_u32(OPENPAK_CN_STRING_ADDR) == OPENPAK_CN_STRING_ORIG0) {
		verified++;
		for (i = 0; i < 6; i++)
			write_u32(OPENPAK_CN_STRING_ADDR + i * 4, openpak_cn_words[i]);
		applied++;
	}
#endif
	write_status(applied, verified, total);
}

/* PRX module entry points, exported by NID from module.S. */
int module_start(u64 args, const void *argp)
{
	(void)args;
	(void)argp;
	apply_pokes();
	/* Stay resident so the pokes remain for the life of the vsh process. */
	return SYS_PRX_RESIDENT;
}

int module_stop(void)
{
	return SYS_PRX_STOP_OK;
}
