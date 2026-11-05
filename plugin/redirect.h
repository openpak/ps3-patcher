/*
 * redirect.h -- the OpenPak PSN name rewrite, shared by the plugin and the host-side test.
 *
 * What it does is what webMAN MOD's block_online_servers() / restore_blocked_urls() do
 * (include/ps3mapi/stealth.h): walk real memory through the lv1 peek/poke syscalls (8 / 9), find
 * PSN host-name strings and overwrite them in place, keeping the original words so they can be
 * put back. webMAN writes NUL bytes (the name no longer resolves); this writes an OpenPak host
 * name of the same length (".ac.playstation.net" -> ".ac.ps3.openpak.org"), so the name resolves
 * through public DNS to OpenPak with no console DNS setting.
 *
 * The caller provides the backend before including this file:
 *   RD_PEEK(addr)   u64 at lv1 real address addr (8-aligned)
 *   RD_POKE(addr,v) store u64 v at lv1 real address addr (8-aligned)
 *   RD_LV2PEEK(a)   u64 at lv2 virtual address a (the syscall 6 peek, address without the 0x8000.. tag)
 *   RD_PAUSE()      yield between windows
 *   RD_RUN()        non-zero while the scan should continue
 */
#ifndef OPENPAK_REDIRECT_H
#define OPENPAK_REDIRECT_H

#include "openpak_names.h"

#define RD_WIN     4096     /* bytes read per window */
#define RD_OVERLAP 64       /* extra bytes read so a name straddling the window edge still matches */
#define RD_MAXW    7        /* 8-byte words one replacement can touch (40 bytes + misalignment) */
#define RD_MAXBK   192

/* webMAN's scan ranges: LV1 0x880000..0xE1FFFF and LV2 0x300000..0x7FFFF8 (+ the lv2 base). */
#define RD_LV1_START 0x880000ULL
#define RD_LV1_STOP  0xE20000ULL
#define RD_LV2_START 0x300000ULL
#define RD_LV2_STOP  0x800000ULL

struct rd_backup {
	unsigned long long addr;               /* 8-aligned first word */
	unsigned int nwords;
	unsigned long long orig[RD_MAXW];
};

struct rd_state {
	unsigned long long lv2off;
	int nbk;                               /* live replacements (== backup entries) */
	int failed;                            /* replacements that did not verify and were undone */
	int passes;
	int hits[OPENPAK_NAMES_N];             /* live replacements per list entry */
	unsigned char first[256];
	unsigned char buf[RD_WIN + RD_OVERLAP];
	struct rd_backup bk[RD_MAXBK];
};

static int rd_same(const unsigned char *a, const char *b, unsigned int n)
{
	unsigned int i;
	for (i = 0; i < n; i++)
		if (a[i] != (unsigned char)b[i]) return 0;
	return 1;
}

static void rd_unpack(unsigned char *dst, unsigned long long w)
{
	int b;
	for (b = 0; b < 8; b++) dst[b] = (unsigned char)(w >> (56 - 8 * b));
}

static unsigned long long rd_pack(const unsigned char *src)
{
	unsigned long long w = 0;
	int b;
	for (b = 0; b < 8; b++) w = (w << 8) | src[b];
	return w;
}

static void rd_init(struct rd_state *st)
{
	unsigned int i;
	char *p = (char *)st;
	unsigned long n = sizeof(*st);
	while (n--) *p++ = 0;
	for (i = 0; i < OPENPAK_NAMES_N; i++) st->first[(unsigned char)openpak_names[i].from[0]] = 1;
}

/* The lv2 image sits at a multiple of 16 MiB in lv1 real memory (0x01000000 on a retail CEX
 * console; webMAN probes 1..15 the same way). Accept an offset only when the lv1 view and the
 * syscall 6 view of the kernel image agree on non-zero words. Returns 0 when none does. */
static unsigned long long rd_find_lv2off(void)
{
	unsigned long long off;
	for (off = 1; off < 16; off++) {
		int k, ok = 0;
		for (k = 0; k < 16; k++) {
			unsigned long long a = 0x10000ULL + (unsigned long long)k * 0x20000ULL;
			unsigned long long v1 = RD_PEEK(off * 0x1000000ULL + a);
			unsigned long long v2 = RD_LV2PEEK(a);
			if (v1 && v1 == v2) ok++;
		}
		if (ok >= 12) return off * 0x1000000ULL;
	}
	return 0;
}

/* Overwrite one match at real address a with list entry k, verified, with the original words kept. */
static void rd_patch(struct rd_state *st, unsigned long long a, int k)
{
	const unsigned int len = openpak_names[k].len;
	const unsigned long long s = a & ~7ULL;
	const unsigned long long e = (a + len + 7) & ~7ULL;
	const unsigned int nw = (unsigned int)((e - s) / 8);
	unsigned char tmp[RD_MAXW * 8];
	struct rd_backup *bk;
	unsigned int j;

	if (nw > RD_MAXW || st->nbk >= RD_MAXBK) { st->failed++; return; }
	bk = &st->bk[st->nbk];
	bk->addr = s;
	bk->nwords = nw;
	for (j = 0; j < nw; j++) {
		bk->orig[j] = RD_PEEK(s + 8ULL * j);
		rd_unpack(tmp + 8 * j, bk->orig[j]);
	}
	/* verify-before-patch again on the words we are about to write */
	if (!rd_same(tmp + (a - s), openpak_names[k].from, len)) return;
	for (j = 0; j < len; j++) tmp[(a - s) + j] = (unsigned char)openpak_names[k].to[j];
	for (j = 0; j < nw; j++) RD_POKE(s + 8ULL * j, rd_pack(tmp + 8 * j));
	for (j = 0; j < nw; j++) {
		if (RD_PEEK(s + 8ULL * j) != rd_pack(tmp + 8 * j)) {
			unsigned int q;
			for (q = 0; q < nw; q++) RD_POKE(s + 8ULL * q, bk->orig[q]);
			st->failed++;
			return;
		}
	}
	st->nbk++;
	st->hits[k]++;
#ifdef RD_HIT
	RD_HIT(a, k);
#endif
}

static void rd_scan(struct rd_state *st, unsigned long long start, unsigned long long stop)
{
	unsigned long long base;
	unsigned int i, k;

	for (base = start; base < stop && RD_RUN(); base += RD_WIN) {
		for (i = 0; i < (RD_WIN + RD_OVERLAP) / 8; i++)
			rd_unpack(st->buf + 8 * i, RD_PEEK(base + 8ULL * i));
		for (i = 0; i < RD_WIN; i++) {
			if (!st->first[st->buf[i]]) continue;
			for (k = 0; k < OPENPAK_NAMES_N; k++) {
				if ((unsigned char)openpak_names[k].from[0] != st->buf[i]) continue;
				if (!rd_same(st->buf + i, openpak_names[k].from, openpak_names[k].len)) continue;
				rd_patch(st, base + i, (int)k);
				i += openpak_names[k].len - 1;
				break;
			}
		}
		RD_PAUSE();
	}
}

/* One pass over both ranges. */
static void rd_pass(struct rd_state *st)
{
	rd_scan(st, RD_LV1_START, RD_LV1_STOP);
	rd_scan(st, st->lv2off + RD_LV2_START, st->lv2off + RD_LV2_STOP);
	st->passes++;
}

/* restore_blocked_urls(): put every original word back, newest first. */
static void rd_restore(struct rd_state *st)
{
	int n;
	unsigned int j;
	for (n = st->nbk - 1; n >= 0; n--)
		for (j = 0; j < st->bk[n].nwords; j++) RD_POKE(st->bk[n].addr + 8ULL * j, st->bk[n].orig[j]);
	st->nbk = 0;
	for (n = 0; n < OPENPAK_NAMES_N; n++) st->hits[n] = 0;
}

static int rd_names_live(const struct rd_state *st)
{
	int n, c = 0;
	for (n = 0; n < OPENPAK_NAMES_N; n++) if (st->hits[n]) c++;
	return c;
}

#endif
