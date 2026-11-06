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
 * so the firmware's pinned check accepts our chain. The values here are generated into
 * openpak_ca_patch.h by tools/stage.sh from the CA the network profile serves.
 *
 * It also redirects the PSN host names (redirect.h): a low-priority thread walks the same real
 * memory ranges webMAN MOD's block_online_servers() walks and rewrites each PSN name to an
 * equal-length OpenPak name (".ac.playstation.net" -> ".ac.ps3.openpak.org"), so PSN resolves to
 * OpenPak through public DNS and the console DNS stays Automatic. The thread rescans (modules the
 * firmware loads later, such as the sign-in library, only appear in memory when first used);
 * module_stop puts every original word back, as webMAN's restore_servers does. Every rewrite is
 * verify-before-patch: the full original name must be present, and the write is read back.
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
#include "openpak_names.h"
#include "../app/source/vshnames.h"

/* Two builds of this module differ only in the SELF wrapper (see plugin/Makefile) and in the names of
 * the files they report to, so a console shows which wrapper Cobra accepts. Both run the same code;
 * the second finds the work already done (pokes already applied, names already rewritten). */
#ifndef OPENPAK_VARIANT
#define OPENPAK_VARIANT 1
#endif
#if OPENPAK_VARIANT == 1
#define STATUS_PATH "/dev_hdd0/openpak/plugin.status"
#define REDIR_PATH "/dev_hdd0/openpak/redirect.status"
#elif OPENPAK_VARIANT == 4
#define STATUS_PATH "/dev_hdd0/openpak/plugin.d.status"
#define REDIR_PATH "/dev_hdd0/openpak/redirect.d.status"
#elif OPENPAK_VARIANT == 3
#define STATUS_PATH "/dev_hdd0/openpak/plugin.c.status"
#define REDIR_PATH "/dev_hdd0/openpak/redirect.c.status"
#else
#define STATUS_PATH "/dev_hdd0/openpak/plugin.b.status"
#define REDIR_PATH "/dev_hdd0/openpak/redirect.b.status"
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#define SYS_PRX_RESIDENT 0
#define SYS_PRX_STOP_OK  0

typedef unsigned int u32;
typedef unsigned long long u64;

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

/* Eight-argument raw lv2 syscall (thread create, lv1 peek/poke). */
static long sc8(long n, long a, long b, long c, long d, long e, long f, long g, long h)
{
	register long r11 __asm__("11") = n;
	register long r3 __asm__("3") = a;
	register long r4 __asm__("4") = b;
	register long r5 __asm__("5") = c;
	register long r6 __asm__("6") = d;
	register long r7 __asm__("7") = e;
	register long r8 __asm__("8") = f;
	register long r9 __asm__("9") = g;
	register long r10 __asm__("10") = h;
	__asm__ volatile("sc"
			 : "+r"(r3), "+r"(r4), "+r"(r5), "+r"(r6), "+r"(r7), "+r"(r8), "+r"(r9), "+r"(r10), "+r"(r11)
			 :
			 : "r0", "r12", "lr", "ctr", "xer", "cr0", "cr1", "cr5", "cr6", "cr7", "memory");
	return r3;
}

/* ---- step trace -------------------------------------------------------------------------------
 * /dev_hdd0/openpak/plugin.log gets one line per step ("<variant> <n> <text> <hex>") so the app (and a
 * photo of its details page) shows how far module_start and the scan thread got. Each line is its own
 * open/append/close, best effort: at early boot /dev_hdd0 may not be mounted yet, then the line is lost
 * (the scan thread logs again once the disk is there). */
static volatile int g_tr_n __attribute__((section(".data.rel.ro"))) = 0;

static void tr(const char *what, unsigned long long v)
{
	char b[96];
	int n = 0, k, fd = -1;
	unsigned long long wrote = 0;
	static const char hex[] = "0123456789abcdef";

	b[n++] = (char)('0' + OPENPAK_VARIANT);
	b[n++] = ' ';
	{ int c = ++g_tr_n; b[n++] = (char)('0' + (c / 10) % 10); b[n++] = (char)('0' + c % 10); }
	b[n++] = ' ';
	for (k = 0; what[k] && n < 70; k++) b[n++] = what[k];
	b[n++] = ' ';
	for (k = 15; k >= 0; k--) b[n++] = hex[(v >> (k * 4)) & 15];
	b[n++] = '\n';
	/* sys_fs_open(path, O_WRONLY|O_CREAT|O_APPEND, &fd, 0666): 0x1 | 0x40 | 0x400 */
	if (sc4(801, (long)"/dev_hdd0/openpak/plugin.log", 0x441, (long)&fd, 0666) != 0) {
		sc4(811, (long)"/dev_hdd0/openpak", 0777, 0, 0);
		if (sc4(801, (long)"/dev_hdd0/openpak/plugin.log", 0x441, (long)&fd, 0666) != 0) return;
	}
	sc4(803, fd, (long)b, n, (long)&wrote);
	sc4(804, fd, 0, 0, 0);
}

/* The pokes go into the vsh process's code. A direct store from the plugin thread is not safe: vsh
 * text is not guaranteed writable (v0.2.12 on hardware: plugin resident, nothing patched, no file
 * written). Cobra's PS3MAPI SET_PROC_MEM (syscall 8, 0x7777, 0x32: pid, address, buffer, size) writes it
 * from the kernel, which is what webMAN MOD does. */
static int g_vsh_pid __attribute__((section(".data.rel.ro"))) = 0;

static int find_vsh_pid(void)
{
	unsigned int pids[16];
	char name[40];
	int i, k;

	for (i = 0; i < 16; i++) pids[i] = 0;
	sc4(8, 0x7777, 0x21, (long)pids, 0);                      /* GET_ALL_PROC_PID */
	for (i = 0; i < 16; i++) {
		if (pids[i] <= 1) continue;
		for (k = 0; k < 40; k++) name[k] = 0;
		sc4(8, 0x7777, 0x22, (long)pids[i], (long)name);   /* GET_PROC_NAME_BY_PID */
		for (k = 0; name[k]; k++)
			if (name[k] == '_' && name[k + 1] == 'm' && name[k + 2] == 'a' && name[k + 3] == 'i' &&
			    name[k + 4] == 'n' && name[k + 5] == '_' && name[k + 6] == 'v' && name[k + 7] == 's' &&
			    name[k + 8] == 'h')
				return (int)pids[i];
	}
	return 0;
}

static long write_u32(u32 addr, u32 val)
{
	u32 buf = val;
	long r;
	if (!g_vsh_pid) g_vsh_pid = find_vsh_pid();
	if (!g_vsh_pid) return -1;
	r = sc8(8, 0x7777, 0x32, g_vsh_pid, (long)addr, (long)&buf, 4, 0, 0);   /* SET_PROC_MEM */
	return r;
}

static long mem_get(u32 addr, void *buf, long n)
{
	if (!g_vsh_pid) g_vsh_pid = find_vsh_pid();
	if (!g_vsh_pid) return -1;
	return sc8(8, 0x7777, 0x31, g_vsh_pid, (long)addr, (long)buf, n, 0, 0);     /* GET_PROC_MEM */
}

static long mem_set(u32 addr, const void *buf, long n)
{
	if (!g_vsh_pid) g_vsh_pid = find_vsh_pid();
	if (!g_vsh_pid) return -1;
	return sc8(8, 0x7777, 0x32, g_vsh_pid, (long)addr, (long)buf, n, 0, 0);     /* SET_PROC_MEM */
}

/* The PSN host names the sign-in code really uses are the copies inside the vsh image (33 known
 * addresses on 4.93 CEX, app/source/vshnames.h; the 12 update-list URLs differ on Evilnat and are left alone). v0.2.14 on hardware: the memory walk below reported 11
 * names rewritten, yet the console still asked DNS for *.playstation.net, and the 32 vsh copies were
 * untouched. Those are rewritten here, through PS3MAPI like the pokes, with the same equal-length
 * suffix swap: the whole name must be there, the write is read back. Returns how many are rewritten. */
static int apply_names(void)
{
	unsigned int i, k, j;
	int done = 0;

	for (i = 0; i < sizeof(vsh_names) / sizeof(vsh_names[0]); i++) {
		const char *nm = vsh_names[i].name;
		unsigned char cur[40], want[40];
		unsigned int len = 0;
		int hit = -1;

		while (nm[len]) len++;
		if (len > sizeof(cur)) continue;
		for (k = 0; k < OPENPAK_NAMES_N; k++) {
			unsigned int same = openpak_names[k].len == len;
			for (j = 0; same && j < len; j++) if (openpak_names[k].from[j] != nm[j]) same = 0;
			if (same) { hit = (int)k; break; }
		}
		if (hit < 0) continue;
		if (mem_get(vsh_names[i].addr, cur, (long)len) != 0) { tr("name read failed", vsh_names[i].addr); continue; }
		{
			unsigned int orig = 1, new_ = 1;
			for (j = 0; j < len; j++) {
				if (cur[j] != (unsigned char)openpak_names[hit].from[j]) orig = 0;
				if (cur[j] != (unsigned char)openpak_names[hit].to[j]) new_ = 0;
				want[j] = (unsigned char)openpak_names[hit].to[j];
			}
			if (new_) { done++; continue; }
			if (!orig) {
				/* the bytes found, so the log says what this firmware has there (v0.2.15: the 12
				 * ".update.playstation.net" copies in the update-list URLs differ on Evilnat 4.93) */
				u64 w = 0;
				for (j = 0; j < 8; j++) w = (w << 8) | cur[j];
				tr("name differs", vsh_names[i].addr);
				tr(" found", w);
				continue;
			}
		}
		if (mem_set(vsh_names[i].addr, want, (long)len) != 0) { tr("name write failed", vsh_names[i].addr); continue; }
		if (mem_get(vsh_names[i].addr, cur, (long)len) == 0) {
			unsigned int ok = 1;
			for (j = 0; j < len; j++) if (cur[j] != want[j]) ok = 0;
			if (ok) { done++; tr("name rewritten", vsh_names[i].addr); continue; }
		}
		tr("name verify failed", vsh_names[i].addr);
	}
	return done;
}

/* Boot status for the Patcher app: /dev_hdd0/openpak/plugin.status = "applied=A verified=V total=T".
 * Best effort; failure to write changes nothing about the pokes. */
/* Poke counts kept for retries: [3] = 1 while plugin.status is not yet on disk. At boot the HDD
 * may not be mounted when the plugin starts, so a failed write is retried by the scan thread. */
static volatile int g_pk[4] __attribute__((section(".data.rel.ro"))) = { 0, 0, 0, 0 };

static int write_status(int applied, int verified, int total)
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
	if (sc4(801, (long)STATUS_PATH, 0x241, (long)&fd, 0666) != 0) {
		sc4(811, (long)"/dev_hdd0/openpak", 0777, 0, 0);   /* sys_fs_mkdir, then once more */
		if (sc4(801, (long)STATUS_PATH, 0x241, (long)&fd, 0666) != 0)
			return -1;
	}
	sc4(803, fd, (long)buf, n, (long)&wrote);   /* sys_fs_write */
	sc4(804, fd, 0, 0, 0);                      /* sys_fs_close */
	return wrote == (unsigned long long)n ? 0 : -1;
}

/* ---- PSN name redirect --------------------------------------------------------------------- */

#define SC_PEEK_LV2    6
#define SC_PEEK_LV1    8     /* CFW / Cobra lv1 peek, as webMAN MOD's lv1_peek_cfw() */
#define SC_POKE_LV1    9
#define SC_THREAD_CREATE 52
#define SC_THREAD_START  53
#define SC_THREAD_EXIT   41
#define SC_USLEEP        141

static volatile int g_run __attribute__((section(".data.rel.ro"))) = 1;     /* module_stop clears it; the thread then restores and exits */

static void usleep_(unsigned long us) { sc8(SC_USLEEP, (long)us, 0, 0, 0, 0, 0, 0, 0); }

/* Two ways to reach lv1 memory, tried in this order (the same two webMAN MOD uses):
 *  0: Cobra PS3MAPI through syscall 8 (opcode 0x7777, LV1_PEEK 0x1008 / LV1_POKE 0x1009),
 *     which keeps working when webMAN "partially disables" the plain CFW syscalls (its default);
 *  1: the plain CFW lv1 peek/poke syscalls 8 / 9 (webMAN's lv1_peek_cfw / lv1_poke_cfw). */
static volatile int g_via __attribute__((section(".data.rel.ro"))) = 0;

static u64 lv1_peek(u64 a)
{
	if (g_via == 0) return (u64)sc8(SC_PEEK_LV1, 0x7777, 0x1008, (long)a, 0, 0, 0, 0, 0);
	return (u64)sc8(SC_PEEK_LV1, (long)a, 0, 0, 0, 0, 0, 0, 0);
}

static void lv1_poke(u64 a, u64 v)
{
	if (g_via == 0) sc8(SC_PEEK_LV1, 0x7777, 0x1009, (long)a, (long)v, 0, 0, 0, 0);
	else sc8(SC_POKE_LV1, (long)a, (long)v, 0, 0, 0, 0, 0, 0);
}

#define RD_PEEK(a)     lv1_peek(a)
#define RD_POKE(a, v)  lv1_poke((a), (v))
#define RD_LV2PEEK(a)  ((u64)sc8(SC_PEEK_LV2, (long)(0x8000000000000000ULL | (u64)(a)), 0, 0, 0, 0, 0, 0, 0))
static void pause_tick(void);
#define RD_PAUSE()     pause_tick()
#define RD_RUN()       (g_run)
static volatile int g_hitlog __attribute__((section(".data.rel.ro"))) = 0;
/* every memory-walk rewrite goes to plugin.log (first 24): index of the name and the low 32 bits of the real address */
#define RD_HIT(a, k) do { if (g_hitlog < 24) { g_hitlog++; tr("rd hit", ((unsigned long long)(k) << 32) | ((a) & 0xffffffffULL)); } } while (0)
#include "redirect.h"

/* ---- vsh module scan ------------------------------------------------------------------------
 * v0.2.15 on hardware: sign-in still went to Sony. The sign-in host is built as "%s.%s.%s" from
 * "native-ps3", "np" and a bare "ac.playstation.net" (no leading dot), and that copy lives in vsh.self
 * and in every module carrying the native client: npsignin_plugin and regcam_plugin, which vsh loads
 * only when PSN sign-in is opened, plus explore_plugin and others. The lv1 walk never reaches those
 * modules' pages. So every second the thread asks PS3MAPI for vsh's module list
 * (GET_ALL_PROC_MODULE_PID 0x41) and, for each module id it has not seen yet, reads the module's
 * segments (GET_PROC_MODULE_SEGMENTS 0x48) and rewrites the names in them through GET/SET_PROC_MEM, with
 * the same equal-length swap and read-back as everything else. Our own module is skipped (its name table
 * holds the very strings being matched). Module rewrites are not undone at module_stop: an unloaded
 * module's pages may belong to something else by then. */

#define MOD_MAX 128

struct mod_seg { u64 base, filesz, memsz, index, type; };
struct mod_info {
	u64 size;
	char name[30];
	char version[2];
	u32 modattribute, start_entry, stop_entry, all_segments_num;
	u32 filename, filename_size, segments, segments_num;
};

struct mod_state {
	int seen[MOD_MAX];     /* module ids in the last list, so only new ones are scanned */
	int nseen;
	int scanned, hits, failed, segerr;
	int segerr_logged;
	struct mod_seg seg[10];
	char fname[512];
};

/* Rewrite one match at vsh address a with list entry k: the original must be there, the write is read back. */
static int proc_patch(struct mod_state *ms, u32 a, int k)
{
	const unsigned int len = openpak_names[k].len;
	unsigned char cur[48];

	if (len > sizeof(cur) || mem_get(a, cur, (long)len) != 0) { ms->failed++; return 0; }
	if (!rd_same(cur, openpak_names[k].from, len)) return 0;
	if (mem_set(a, openpak_names[k].to, (long)len) != 0) { ms->failed++; return 0; }
	if (mem_get(a, cur, (long)len) != 0 || !rd_same(cur, openpak_names[k].to, len)) {
		mem_set(a, openpak_names[k].from, (long)len);
		ms->failed++;
		return 0;
	}
	return 1;
}

static int scan_proc_range(struct rd_state *st, struct mod_state *ms, u32 base, u32 size)
{
	u32 off, i, k;
	int hits = 0;

	for (off = 0; off < size && g_run; off += RD_WIN) {
		u32 n = size - off, lim;
		if (n > RD_WIN + RD_OVERLAP) n = RD_WIN + RD_OVERLAP;
		lim = n < RD_WIN ? n : RD_WIN;
		if (mem_get(base + off, st->buf, (long)n) != 0) { ms->failed++; continue; }
		for (i = 0; i < lim; i++) {
			if (!st->first[st->buf[i]]) continue;
			for (k = 0; k < OPENPAK_NAMES_N; k++) {
				if ((unsigned char)openpak_names[k].from[0] != st->buf[i]) continue;
				if (i + openpak_names[k].len > n) continue;
				if (!rd_same(st->buf + i, openpak_names[k].from, openpak_names[k].len)) continue;
				hits += proc_patch(ms, base + off + i, (int)k);
				i += openpak_names[k].len - 1;
				break;
			}
		}
	}
	return hits;
}

static void scan_module(struct rd_state *st, struct mod_state *ms, int id)
{
	struct mod_info info;
	const u32 self = (u32)(u64)openpak_names[0].from;     /* an address inside our own module */
	char *p = (char *)&info;
	unsigned int s, k;
	long r;
	int hits = 0;
	u32 lo = 0;

	for (k = 0; k < sizeof(info); k++) p[k] = 0;
	p = (char *)ms->seg;
	for (k = 0; k < sizeof(ms->seg); k++) p[k] = 0;
	info.size = sizeof(info);
	info.segments = (u32)(u64)ms->seg;
	info.segments_num = sizeof(ms->seg) / sizeof(ms->seg[0]);
	info.filename = (u32)(u64)ms->fname;
	info.filename_size = sizeof(ms->fname);
	r = sc8(8, 0x7777, 0x48, g_vsh_pid, id, (long)&info, 0, 0, 0);   /* GET_PROC_MODULE_SEGMENTS */
	if (r != 0) {
		ms->segerr++;
		if (ms->segerr_logged < 3) { ms->segerr_logged++; tr("mod segments failed", ((u64)(u32)id << 32) | (u32)r); }
		return;
	}
	ms->scanned++;
	for (s = 0; s < info.segments_num && s < sizeof(ms->seg) / sizeof(ms->seg[0]); s++) {
		u64 b = ms->seg[s].base, n = ms->seg[s].filesz;
		if (!n || b >= 0x100000000ULL || n > 0x2000000ULL) continue;
		if (self >= b && self < b + n) return;                  /* our own module */
		if (!lo) lo = (u32)b;
		hits += scan_proc_range(st, ms, (u32)b, (u32)n);
	}
	if (hits) {
		char line[48];
		int q = 0;
		const char *pre = "mod hits ";
		while (*pre) line[q++] = *pre++;
		for (k = 0; k < sizeof(info.name) && info.name[k] && q < 46; k++) line[q++] = info.name[k];
		line[q] = 0;
		tr(line, ((u64)(u32)hits << 32) | lo);
		ms->hits += hits;
	}
}

/* One look at vsh's module list: scan every id that was not there last time. */
static void mod_poll(struct rd_state *st, struct mod_state *ms)
{
	int list[MOD_MAX];
	int i, j, n = 0;

	if (!g_vsh_pid) g_vsh_pid = find_vsh_pid();
	if (!g_vsh_pid) return;
	for (i = 0; i < MOD_MAX; i++) list[i] = 0;
	if (sc4(8, 0x7777, 0x41, g_vsh_pid, (long)list) != 0) return;   /* GET_ALL_PROC_MODULE_PID */
	for (i = 0; i < MOD_MAX && g_run; i++) {
		int id = list[i], old = 0;
		if (id == 0 || id == -1 || id == 1) continue;
		for (j = 0; j < ms->nseen; j++) if (ms->seen[j] == id) { old = 1; break; }
		if (!old) scan_module(st, ms, id);
	}
	for (i = 0; i < MOD_MAX; i++)
		if (list[i] != 0 && list[i] != -1 && list[i] != 1) ms->seen[n++] = list[i];
	ms->nseen = n;
}

static int write_file(const char *path, const char *buf, int n)
{
	int fd = -1;
	unsigned long long wrote = 0;
	/* sys_fs_open(path, O_WRONLY|O_CREAT|O_TRUNC, &fd, mode 0666, NULL, 0) */
	if (sc4(801, (long)path, 0x241, (long)&fd, 0666) != 0) {
		sc4(811, (long)"/dev_hdd0/openpak", 0777, 0, 0);
		if (sc4(801, (long)path, 0x241, (long)&fd, 0666) != 0) return -1;
	}
	sc4(803, fd, (long)buf, n, (long)&wrote);   /* sys_fs_write */
	sc4(804, fd, 0, 0, 0);                      /* sys_fs_close */
	return wrote == (unsigned long long)n ? 0 : -1;
}

static int put_str(char *b, int n, const char *s)
{
	while (*s) b[n++] = *s++;
	return n;
}

static int put_num(char *b, int n, unsigned long v)
{
	char t[24];
	int k = 0;
	if (!v) t[k++] = '0';
	while (v) { t[k++] = (char)('0' + v % 10); v /= 10; }
	while (k) b[n++] = t[--k];
	return n;
}

/* /dev_hdd0/openpak/redirect.status: one summary line, then "<live count> TAB <match>" per entry.
 * state: ok | hen (no lv1 poke under HEN; modules still scanned) | nolv2 (lv2 base not found) | nothread.
 * mods= module ids scanned, modhits= names rewritten in modules, modfail= module reads/writes that failed,
 * segerr= modules whose segments PS3MAPI did not return. */
static int write_redirect_status(const char *state, const struct rd_state *st, const struct mod_state *ms)
{
	char buf[1024];
	int n = 0, k;

	n = put_str(buf, n, "state="); n = put_str(buf, n, state);
	n = put_str(buf, n, " names="); n = put_num(buf, n, st ? (unsigned)rd_names_live(st) : 0);
	n = put_str(buf, n, " of="); n = put_num(buf, n, OPENPAK_NAMES_N);
	n = put_str(buf, n, " live="); n = put_num(buf, n, st ? (unsigned)st->nbk : 0);
	n = put_str(buf, n, " passes="); n = put_num(buf, n, st ? (unsigned)st->passes : 0);
	n = put_str(buf, n, " failed="); n = put_num(buf, n, st ? (unsigned)st->failed : 0);
	n = put_str(buf, n, " via="); n = put_str(buf, n, g_via == 0 ? "ps3mapi" : "cfw");
	n = put_str(buf, n, " mods="); n = put_num(buf, n, ms ? (unsigned)ms->scanned : 0);
	n = put_str(buf, n, " modhits="); n = put_num(buf, n, ms ? (unsigned)ms->hits : 0);
	n = put_str(buf, n, " modfail="); n = put_num(buf, n, ms ? (unsigned)ms->failed : 0);
	n = put_str(buf, n, " segerr="); n = put_num(buf, n, ms ? (unsigned)ms->segerr : 0);
	buf[n++] = '\n';
	for (k = 0; st && k < OPENPAK_NAMES_N && n < 900; k++) {
		n = put_num(buf, n, (unsigned)st->hits[k]);
		buf[n++] = '\t';
		n = put_str(buf, n, openpak_names[k].from);
		buf[n++] = '\n';
	}
	return write_file(REDIR_PATH, buf, n);
}

/* Retry plugin.status until /dev_hdd0 accepts the write. */
static void flush_status(void)
{
	if (g_pk[3] && write_status(g_pk[0], g_pk[1], g_pk[2]) == 0) g_pk[3] = 0;
}

/* The lv1 walk takes seconds; the module list is still polled about once a second from inside it
 * (RD_PAUSE, between windows, when st->buf is free), so a module opened mid-walk is not missed. */
static struct rd_state *volatile g_stp __attribute__((section(".data.rel.ro"))) = 0;
static struct mod_state *volatile g_msp __attribute__((section(".data.rel.ro"))) = 0;
static volatile int g_ticks __attribute__((section(".data.rel.ro"))) = 0;
static volatile int g_hen __attribute__((section(".data.rel.ro"))) = 0;

static void pause_tick(void)
{
	usleep_(3000);
	if (++g_ticks >= 200 && g_stp && g_msp) { g_ticks = 0; mod_poll(g_stp, g_msp); }
}

/* The scan thread. Raw lv2 thread: no libc, no TLS, so it only uses its own stack, the raw
 * syscalls above and read-only tables. Every second: vsh's module list (new modules are scanned at
 * once). The lv1/lv2 walk: 5 s after boot, then every 20 s for 6 passes, then every 2 minutes. */
void scan_thread(u64 arg)
{
	struct rd_state st;
	struct mod_state ms;
	int lastnbk = -1, lastfail = -1, lasthits = -1, lastmods = -1, lastmf = -1, i, dirty = 1;
	unsigned long tick = 0, next_pass = 0;
	const char *state;
	(void)arg;

	tr("scan thread entered", 0);
	rd_init(&st);
	{ char *p = (char *)&ms; unsigned long n = sizeof(ms); while (n--) *p++ = 0; }
	if (!g_hen) {
		for (g_via = 0; g_via < 2; g_via++) {
			st.lv2off = rd_find_lv2off();
			if (st.lv2off) break;
		}
		if (!st.lv2off) g_via = 0;
	}
	tr("lv2off", st.lv2off);
	state = g_hen ? "hen" : st.lv2off ? "ok" : "nolv2";
	for (i = 0; i < 5 && g_run; i++) { flush_status(); usleep_(1000000); }
	mod_poll(&st, &ms);
	tr("modules scanned, hits", ((u64)(u32)ms.scanned << 32) | (u32)ms.hits);
	g_stp = &st;
	g_msp = &ms;
	while (g_run) {
		mod_poll(&st, &ms);
		if (st.lv2off && tick >= next_pass) {
			rd_pass(&st);
			next_pass = tick + (st.passes < 6 ? 20 : 120);
		}
		if (st.nbk != lastnbk || st.failed != lastfail || ms.hits != lasthits || ms.scanned != lastmods ||
		    ms.failed != lastmf) {
			dirty = 1;
			lastnbk = st.nbk; lastfail = st.failed; lasthits = ms.hits; lastmods = ms.scanned; lastmf = ms.failed;
		}
		flush_status();
		if (dirty && write_redirect_status(state, &st, &ms) == 0) dirty = 0;
		usleep_(1000000);
		tick++;
	}
	g_stp = 0;
	g_msp = 0;
	if (st.lv2off) rd_restore(&st);
	write_redirect_status("restored", &st, &ms);
	sc8(SC_THREAD_EXIT, 0, 0, 0, 0, 0, 0, 0, 0);
	for (;;) usleep_(1000000);
}

extern const u32 openpak_opd_scan[2];
struct thread_param { u32 entry; u32 tls; };

static int start_scan_thread(void)
{
	struct thread_param param;
	u64 tid = 0;

	param.entry = (u32)(u64)openpak_opd_scan;    /* 8-byte {entry, toc} descriptor (module.S) */
	param.tls = 0;
	if (sc8(SC_THREAD_CREATE, (long)&tid, (long)&param, 0, 0, 3000, 0x10000, 0, (long)"OpenPak names") != 0)
		return -1;
	if (sc8(SC_THREAD_START, (long)tid, 0, 0, 0, 0, 0, 0, 0) != 0)
		return -1;
	return 0;
}

static void start_redirect(void)
{
	/* PS3HEN has no lv1 poke (webMAN refuses block_online_servers there too): the thread then only scans modules. */
	if ((int)sc8(SC_PEEK_LV1, 0x1337, 0, 0, 0, 0, 0, 0, 0) == 0x1337) g_hen = 1;   /* modules only */
	if (start_scan_thread() != 0) {
		tr("thread create failed", 0);
		write_redirect_status("nothread", NULL, NULL);
	} else {
		tr("thread started", 0);
	}
}

static void apply_pokes(void)
{
	unsigned int i;
	int applied = 0, verified = 0, total = 0;

	/* "ran" marker first, before anything that could fault: applied=0 until the pokes are done */
	write_status(0, 0, (int)(sizeof(openpak_u32_pokes) / sizeof(openpak_u32_pokes[0])) + 1);
	tr("vsh pid", (unsigned long long)(g_vsh_pid = find_vsh_pid()));
	for (i = 0; i < sizeof(openpak_u32_pokes) / sizeof(openpak_u32_pokes[0]); i++) {
		total++;
		tr("poke word", read_u32(openpak_u32_pokes[i].addr));
		if (read_u32(openpak_u32_pokes[i].addr) == openpak_u32_pokes[i].orig) {
			long wr;
			verified++;
			wr = write_u32(openpak_u32_pokes[i].addr, openpak_u32_pokes[i].val);
			tr("poke write ret", (unsigned long long)wr);
			if (read_u32(openpak_u32_pokes[i].addr) == openpak_u32_pokes[i].val) applied++;
		} else if (read_u32(openpak_u32_pokes[i].addr) == openpak_u32_pokes[i].val) {
			verified++;       /* the other build of this plugin got there first */
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
		if (read_u32(OPENPAK_CN_STRING_ADDR) == openpak_cn_words[0]) applied++;
	} else if (read_u32(OPENPAK_CN_STRING_ADDR) == openpak_cn_words[0]) {
		verified++;
		applied++;
	}
#endif
	g_pk[0] = applied; g_pk[1] = verified; g_pk[2] = total;
	g_pk[3] = write_status(applied, verified, total) != 0;
}

/* PRX module entry points, exported by NID from module.S. */
int module_start(u64 args, const void *argp)
{
	(void)argp;
	/* first thing: one short beep (sys_sm_ring_buzzer, as webMAN MOD's BEEP1) and a log line, so a restart
	 * shows whether the start entry runs at all */
	sc4(392, 0x1004, 0x4, 0x6, 0);
	tr("module_start called, args", args);
	apply_pokes();
	tr("pokes done, applied", (unsigned long long)g_pk[0]);
	{
		int nm = apply_names();
		tr("vsh names rewritten", (unsigned long long)nm);
		/* second beep once everything is in place; the buzzer ignores a request made while the first is still
		 * sounding (v0.2.14: the two calls were a few ms apart), so wait for it */
		if (g_pk[0] && g_pk[0] == g_pk[2] && nm > 0) {
			usleep_(700000);
			sc4(392, 0x1004, 0x7, 0x36, 0);
		}
	}
	start_redirect();
	tr("module_start returns", 0);
	/* Stay resident so the pokes remain for the life of the vsh process. */
	return SYS_PRX_RESIDENT;
}

int module_stop(void)
{
	g_run = 0;     /* the scan thread restores every rewritten name and exits */
	return SYS_PRX_STOP_OK;
}
