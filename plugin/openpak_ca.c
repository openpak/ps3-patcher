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
 * Build with PSL1GHT (see Makefile). No binary is committed; CI or a local PSL1GHT builds it.
 */

#include <ppu-types.h>
#include <sys/prx.h>

#include "openpak_ca_patch.h"

SYS_MODULE_INFO(OPENPAK_CA, 0, 1, 0);

/* vsh text is mapped executable+writable in the vsh process under Cobra, so a direct store
 * reaches it; we still flush the instruction cache for the lines we touch. */
static void write_u32(unsigned int addr, unsigned int val)
{
	*(volatile unsigned int *)(uint64_t)addr = val;
	__asm__ volatile("dcbst 0,%0; sync; icbi 0,%0; isync" : : "r"((uint64_t)addr) : "memory");
}

static unsigned int read_u32(unsigned int addr)
{
	return *(volatile unsigned int *)(uint64_t)addr;
}

/* Patch only if the target still holds the expected original word. Returns 1 if applied. */
static int poke_verified(unsigned int addr, unsigned int val, unsigned int orig)
{
	if (read_u32(addr) != orig)
		return 0;
	write_u32(addr, val);
	return 1;
}

static void apply_pokes(void)
{
	unsigned int i;

	for (i = 0; i < sizeof(openpak_u32_pokes) / sizeof(openpak_u32_pokes[0]); i++)
		poke_verified(openpak_u32_pokes[i].addr,
			      openpak_u32_pokes[i].val,
			      openpak_u32_pokes[i].orig);

#if !OPENPAK_SKIP_CN_POKES
	/* Overwrite the 24-byte pinned CN field only if it still begins with "SCEI". */
	if (read_u32(OPENPAK_CN_STRING_ADDR) == OPENPAK_CN_STRING_ORIG0) {
		for (i = 0; i < 6; i++)
			write_u32(OPENPAK_CN_STRING_ADDR + i * 4, openpak_cn_words[i]);
	}
#endif
}

int openpak_ca_start(uint64_t ppu_thr_id);
int openpak_ca_stop(void);

SYS_MODULE_START(openpak_ca_start);
SYS_MODULE_STOP(openpak_ca_stop);

int openpak_ca_start(uint64_t ppu_thr_id)
{
	(void)ppu_thr_id;
	apply_pokes();
	/* Stay resident so the pokes remain for the life of the vsh process. */
	return SYS_PRX_RESIDENT;
}

int openpak_ca_stop(void)
{
	return SYS_PRX_STOP_OK;
}
