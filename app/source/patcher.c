/*
 * OpenPak PS3 Patcher -- the console-side logic: status, switch to OpenPak, switch back to PSN.
 *
 * Safety rules (see docs/deploy.md):
 *  - every dev_flash write goes to a temp name, is read back and compared, then renamed;
 *  - the stock CA01.cer / CA59.cer are backed up once to /dev_hdd0/openpak/backup and an
 *    existing backup is never overwritten;
 *  - only CA01.cer and CA59.cer in /dev_flash/data/cert are ever touched;
 *  - free space is checked before writing;
 *  - boot_plugins.txt is edited by line: other plugins' lines are kept byte for byte;
 *  - everything is logged to /dev_hdd0/openpak/patcher.log.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <ppu-lv2.h>
#include <lv2/sysfs.h>
#include <sys/file.h>

#include "patcher.h"

#define CERT_DIR   "/dev_flash/data/cert"
#define BLIND_CERT "/dev_blind/data/cert"
#define MAX_CERT   (64 * 1024)
#define MAX_LIST   (256 * 1024)

/* the clean-shutdown marker the console keeps in the hdd tmp dir */
#define TURNOFF_PATH "/dev_hdd0/" "tm" "p/turnoff"

static const char *const CA_NAMES[2] = { "CA01.cer", "CA59.cer" };

/* ---- log -------------------------------------------------------------------------------- */

static void logf_(const char *fmt, ...)
{
	char line[320], msg[256];
	va_list ap;
	time_t t = time(NULL);
	s32 fd;
	u64 w;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	snprintf(line, sizeof(line), "[%lld] %s\n", (long long)t, msg);
	sysFsMkdir(OPENPAK_DIR, 0777);
	if (sysFsOpen(OPENPAK_LOG, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_APPEND, &fd, NULL, 0) == 0) {
		sysFsWrite(fd, line, strlen(line), &w);
		sysLv2FsFsync(fd);
		sysFsClose(fd);
	}
}

/* ---- file helpers ------------------------------------------------------------------------ */

static bool exists(const char *path)
{
	sysFSStat st;
	return sysFsStat(path, &st) == 0;
}

/* Reads a whole file into a malloc'd buffer (one extra NUL byte). NULL on failure. */
static unsigned char *read_all(const char *path, u64 max, u64 *len)
{
	sysFSStat st;
	s32 fd;
	u64 got = 0, n;
	unsigned char *buf;

	if (sysFsStat(path, &st) != 0 || st.st_size > max)
		return NULL;
	buf = malloc((size_t)st.st_size + 1);
	if (!buf)
		return NULL;
	if (sysFsOpen(path, SYS_O_RDONLY, &fd, NULL, 0) != 0) {
		free(buf);
		return NULL;
	}
	while (got < st.st_size) {
		if (sysFsRead(fd, buf + got, st.st_size - got, &n) != 0 || n == 0)
			break;
		got += n;
	}
	sysFsClose(fd);
	if (got != st.st_size) {
		free(buf);
		return NULL;
	}
	buf[got] = 0;
	*len = got;
	return buf;
}

static bool write_raw(const char *path, const unsigned char *data, u64 len)
{
	s32 fd;
	u64 done = 0, n;

	if (sysFsOpen(path, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, NULL, 0) != 0)
		return false;
	while (done < len) {
		if (sysFsWrite(fd, data + done, len - done, &n) != 0 || n == 0) {
			sysFsClose(fd);
			return false;
		}
		done += n;
	}
	sysLv2FsFsync(fd);
	sysFsClose(fd);
	return true;
}

static bool same_file(const char *path, const unsigned char *data, u64 len)
{
	u64 got;
	unsigned char *b = read_all(path, len + 1, &got);
	bool ok = b && got == len && memcmp(b, data, len) == 0;
	free(b);
	return ok;
}

/* temp name -> write -> read back -> rename over dst. On a rename that the filesystem refuses
 * to do over an existing file, the old file is removed and the rename retried; callers that
 * write dev_flash hold a verified backup before getting here. */
static bool write_atomic(const char *dst, const unsigned char *data, u64 len)
{
	char tmp[160];

	snprintf(tmp, sizeof(tmp), "%s.opnew", dst);
	sysFsUnlink(tmp);
	if (!write_raw(tmp, data, len) || !same_file(tmp, data, len)) {
		logf_("write/verify of %s failed", tmp);
		sysFsUnlink(tmp);
		return false;
	}
	if (sysLv2FsRename(tmp, dst) != 0) {
		sysFsUnlink(dst);
		if (sysLv2FsRename(tmp, dst) != 0) {
			logf_("rename %s -> %s failed", tmp, dst);
			sysFsUnlink(tmp);
			return false;
		}
	}
	if (!same_file(dst, data, len)) {
		logf_("final verify of %s failed", dst);
		return false;
	}
	return true;
}

static void ensure_dirs(void)
{
	sysFsMkdir(OPENPAK_DIR, 0777);
	sysFsMkdir(OPENPAK_BACKUP_DIR, 0777);
	sysFsMkdir("/dev_hdd0/plugins", 0777);
}

/* ---- boot_plugins.txt -------------------------------------------------------------------- */

/* Is line [p, p+n) our plugin path (ignoring CR and surrounding blanks)? */
static bool is_our_line(const unsigned char *p, size_t n)
{
	size_t want = strlen(OPENPAK_PLUGIN_DST);
	while (n && (p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
	while (n && (*p == ' ' || *p == '\t')) { p++; n--; }
	return n == want && memcmp(p, OPENPAK_PLUGIN_DST, want) == 0;
}

static bool list_has_ours(const unsigned char *buf, u64 len)
{
	u64 i = 0;
	while (i < len) {
		u64 j = i;
		while (j < len && buf[j] != '\n') j++;
		if (is_our_line(buf + i, (size_t)(j - i))) return true;
		i = j + 1;
	}
	return false;
}

static bool boot_list_add(void)
{
	u64 len = 0;
	unsigned char *buf = NULL, *out;
	bool ok;

	if (exists(OPENPAK_BOOT_LIST)) {
		buf = read_all(OPENPAK_BOOT_LIST, MAX_LIST, &len);
		if (!buf) return false;
		if (list_has_ours(buf, len)) { free(buf); return true; }
	}
	out = malloc(len + strlen(OPENPAK_PLUGIN_DST) + 4);
	if (!out) { free(buf); return false; }
	if (len) memcpy(out, buf, len);
	if (len && out[len - 1] != '\n') out[len++] = '\n';
	memcpy(out + len, OPENPAK_PLUGIN_DST, strlen(OPENPAK_PLUGIN_DST));
	len += strlen(OPENPAK_PLUGIN_DST);
	out[len++] = '\n';
	ok = write_atomic(OPENPAK_BOOT_LIST, out, len);
	free(out);
	free(buf);
	return ok;
}

static bool boot_list_remove(void)
{
	u64 len = 0, i = 0, o = 0;
	unsigned char *buf, *out;
	bool ok = true;

	if (!exists(OPENPAK_BOOT_LIST)) return true;
	buf = read_all(OPENPAK_BOOT_LIST, MAX_LIST, &len);
	if (!buf) return false;
	if (!list_has_ours(buf, len)) { free(buf); return true; }
	out = malloc(len + 1);
	if (!out) { free(buf); return false; }
	while (i < len) {
		u64 j = i;
		while (j < len && buf[j] != '\n') j++;
		u64 next = (j < len) ? j + 1 : j;
		if (!is_our_line(buf + i, (size_t)(j - i))) {
			memcpy(out + o, buf + i, (size_t)(next - i));
			o += next - i;
		}
		i = next;
	}
	ok = write_atomic(OPENPAK_BOOT_LIST, out, o);
	free(out);
	free(buf);
	return ok;
}

/* ---- status ------------------------------------------------------------------------------ */

static void read_firmware(op_status *s)
{
	u64 len;
	unsigned char *b = read_all("/dev_flash/vsh/etc/version.txt", 4096, &len);
	char *p;

	s->fw[0] = 0;
	s->fw_known = false;
	s->fw_ok = false;
	if (!b) return;
	p = strstr((char *)b, "release:");
	if (p) {
		/* "release:04.9300:" -> "4.93" */
		int major = 0, minor = 0;
		p += 8;
		if (sscanf(p, "%2d.%2d", &major, &minor) == 2) {
			snprintf(s->fw, sizeof(s->fw), "%d.%02d", major, minor);
			s->fw_known = true;
		}
	}
	/* The retail build is the only one these addresses were derived on. */
	s->fw_ok = s->fw_known && strcmp(s->fw, "4.93") == 0 && !strstr((char *)b, "DEX");
	free(b);
}

/* lv2 syscalls answer with 0x8001xxxx (sign-extended or not) on failure; ENOSYS is 0x80010003. */
static bool is_lv2_error(u64 r)
{
	u64 hi = r >> 32;
	return (hi == 0 || hi == 0xffffffffULL) && ((u32)r >> 16) == 0x8001;
}

/* Independent signals; every raw result goes to the log so a run shows which one works.
 *
 *  peek:   syscall 6 (CFW/HEN lv2 peek) on an address inside the lv2 image.
 *  cobra:  exactly webMAN MOD's is_cobra_based() (_Projects_/vsh_menu/include/sys_info.h): syscall 8
 *          opcode 0x7000 (SYSCALL8_OPCODE_GET_VERSION) takes a POINTER to a u32 and writes the
 *          version there; the return value is only a status. A u32 preset to 0x99999999 that has
 *          changed after a non-negative return means Cobra. (v0.2.3 passed a NULL pointer and read
 *          the return value as the version, which is why it saw "not active" on a working Cobra.)
 *  payload: opcode 0x1337 -> 0x1337 = PS3HEN; 0x7FFF -> 0x666 = Mamba; 0x7001 writes a u16 version.
 *  stage2: where Cobra's stage2 lives (webMAN's disable_cobra_stage2 renames these):
 *          sys/stage2.bin, rebug/cobra/stage2.cex and .dex; the renamed forms mean "disabled". */
static void detect_cfw(op_status *s)
{
	u64 r;
	volatile u32 version = 0x99999999;
	volatile u16 version2 = 0;
	bool hen = false, mamba = false;

	{
		lv2syscall1(6, 0x8000000000010000ULL);
		r = (u64)p1;
	}
	s->peek_raw = r;
	s->peek = !is_lv2_error(r);

	{
		lv2syscall2(8, 0x7000, (u64)(u32)(uintptr_t)&version);
		r = (u64)p1;
	}
	s->cobra_raw = r;
	s->version_raw = version;
	s->cobra = (s32)r >= 0 && version != 0x99999999;

	if (s->cobra) {
		u64 h, m, q;
		{ lv2syscall1(8, 0x1337); h = (u64)p1; }
		{ lv2syscall1(8, 0x7FFF); m = (u64)p1; }
		{ lv2syscall2(8, 0x7001, (u64)(u32)(uintptr_t)&version2); q = (u64)p1; }
		hen = (s32)h == 0x1337;
		mamba = (s32)m == 0x666;
		logf_("detect: hen(0x1337)=%llx mamba(0x7fff)=%llx version2(0x7001) ret=%llx val=%04x",
		      (unsigned long long)h, (unsigned long long)m, (unsigned long long)q, (unsigned)version2);
		snprintf(s->payload, sizeof(s->payload), "%s %X.%X", hen ? "PS3HEN" : mamba ? "Mamba" : "Cobra",
			 (unsigned)(version2 >> 8), (version2 & 0xF) ? (unsigned)(version2 & 0xFF) : (unsigned)((version2 >> 4) & 0xF));
	}
	s->hen = hen;

	{
		static const char *const on[] = { "/dev_flash/sys/stage2.bin", "/dev_flash/rebug/cobra/stage2.cex",
						  "/dev_flash/rebug/cobra/stage2.dex" };
		static const char *const off[] = { "/dev_flash/sys/stage2_disabled.bin", "/dev_flash/rebug/cobra/stage2.cex.bak",
						   "/dev_flash/rebug/cobra/stage2.dex.bak" };
		unsigned i;
		for (i = 0; i < 3; i++) {
			bool a = exists(on[i]), b = exists(off[i]);
			s->stage2 = s->stage2 || a;
			s->stage2_off = s->stage2_off || b;
			logf_("detect: %s=%d %s=%d", on[i], a, off[i], b);
		}
	}
	logf_("detect: peek raw=%016llx -> %d; cobra(sc8 0x7000) ret=%016llx version=%08x -> %d; payload='%s'; "
	      "stage2=%d stage2_renamed=%d", (unsigned long long)s->peek_raw, s->peek,
	      (unsigned long long)s->cobra_raw, s->version_raw, s->cobra, s->payload, s->stage2, s->stage2_off);
}

void op_status_read(op_status *s)
{
	u64 l1 = 0, l2 = 0, n;
	unsigned char *c1, *c2, *b1, *b2;
	unsigned char *st;
	char path[160];
	u64 len;

	memset(s, 0, sizeof(*s));
	read_firmware(s);
	detect_cfw(s);
	logf_("firmware: %s known=%d ok=%d", s->fw, s->fw_known, s->fw_ok);

	snprintf(path, sizeof(path), "%s/ca/%s", OPENPAK_USRDIR, CA_NAMES[0]);
	c1 = read_all(path, MAX_CERT, &l1);
	snprintf(path, sizeof(path), "%s/ca/%s", OPENPAK_USRDIR, CA_NAMES[1]);
	c2 = read_all(path, MAX_CERT, &l2);
	s->bundle_ok = c1 && c2 && l1 && l2 && exists(OPENPAK_USRDIR "/openpak_ca.sprx");

	b1 = read_all(CERT_DIR "/CA01.cer", MAX_CERT, &n);
	if (b1 && c1 && n == l1 && memcmp(b1, c1, n) == 0) {
		b2 = read_all(CERT_DIR "/CA59.cer", MAX_CERT, &n);
		s->ca_openpak = b2 && c2 && n == l2 && memcmp(b2, c2, n) == 0;
		free(b2);
	}
	free(b1);
	free(c1);
	free(c2);

	s->plugin_file = exists(OPENPAK_PLUGIN_DST);
	st = read_all(OPENPAK_BOOT_LIST, MAX_LIST, &len);
	s->plugin_listed = st && list_has_ours(st, len);
	free(st);

	st = read_all(OPENPAK_STATUS, 256, &len);
	if (st) {
		int a, v, t;
		if (sscanf((char *)st, "applied=%d verified=%d total=%d", &a, &v, &t) == 3) {
			s->plugin_ran = true;
			s->pokes_applied = a;
			s->pokes_verified = v;
			s->pokes_total = t;
		}
		free(st);
	}

	st = read_all(OPENPAK_REDIRECT_STATUS, 1024, &len);
	if (st) {
		int nm, of, lv, ps, fl;
		char state[16];
		/* first line: state=ok names=N of=M live=L passes=P failed=F via=..; the rest is per-name counts */
		if (sscanf((char *)st, "state=%15s names=%d of=%d live=%d passes=%d failed=%d", state, &nm, &of, &lv, &ps, &fl) == 6) {
			s->redirect_ran = true;
			snprintf(s->redirect_state, sizeof(s->redirect_state), "%s", state);
			s->redirect_names = nm;
			s->redirect_of = of;
			s->redirect_live = lv;
			s->redirect_passes = ps;
			s->redirect_failed = fl;
			logf_("redirect.status: %s", (char *)st);
		}
		free(st);
	}

	snprintf(path, sizeof(path), "%s/%s", OPENPAK_BACKUP_DIR, CA_NAMES[0]);
	s->backup_ok = exists(path);
	snprintf(path, sizeof(path), "%s/%s", OPENPAK_BACKUP_DIR, CA_NAMES[1]);
	s->backup_ok = s->backup_ok && exists(path);
}

bool op_is_openpak(const op_status *s)
{
	return s->ca_openpak && s->plugin_listed;
}

bool op_supported(const op_status *s, char *why, int why_len)
{
	if (!s->fw_known) {
		snprintf(why, why_len, "Could not read the firmware version. The patcher will not touch this console.");
		return false;
	}
	if (!s->fw_ok) {
		snprintf(why, why_len, "OpenPak supports firmware 4.93 CEX only. This console runs %s. "
			 "Nothing was changed.", s->fw);
		return false;
	}
	if (!s->peek && !s->cobra) {
		snprintf(why, why_len, "No custom firmware or HEN found. Enable HEN (or run Evilnat CFW) and "
			 "start this app again.");
		return false;
	}
	if (!s->cobra) {
		snprintf(why, why_len, "Cobra appears to be off (sc8 ret %08x, version %08x). Nothing was "
			 "changed. Details: " OPENPAK_LOG, (unsigned)s->cobra_raw, s->version_raw);
		return false;
	}
	if (!s->peek) {
		snprintf(why, why_len, "Cobra answers but lv2 peek does not. The patcher will not continue.");
		return false;
	}
	if (!s->bundle_ok) {
		snprintf(why, why_len, "The package is incomplete (CA files or plugin missing). Reinstall it.");
		return false;
	}
	return true;
}

/* ---- dev_blind --------------------------------------------------------------------------- */

/* sys_fs_mount, as homebrew installers do: lv2 syscall 837 with the flash device, FAT, the mount
 * point, and write protection off. See docs/deploy.md for sources. */
static int fs_mount_blind(void)
{
	lv2syscall8(837, (u64)"CELL_FS_IOS:BUILTIN_FLSH1", (u64)"CELL_FS_FAT", (u64)"/dev_blind", 0, 0, 0, 0, 0);
	return (int)(s32)p1;
}

static void fs_umount_blind(void)
{
	lv2syscall3(838, (u64)"/dev_blind", 0, 1);
}

/* ---- the switch -------------------------------------------------------------------------- */

#define FAIL(...) do { snprintf(err, err_len, __VA_ARGS__); logf_("FAIL: %s", err); goto done; } while (0)

static bool load_bundle(unsigned char *ca[2], u64 cl[2], unsigned char **sprx, u64 *sl)
{
	char path[160];
	int i;

	for (i = 0; i < 2; i++) {
		snprintf(path, sizeof(path), "%s/ca/%s", OPENPAK_USRDIR, CA_NAMES[i]);
		ca[i] = read_all(path, MAX_CERT, &cl[i]);
		if (!ca[i] || !cl[i]) return false;
	}
	*sprx = read_all(OPENPAK_USRDIR "/openpak_ca.sprx", 4 * 1024 * 1024, sl);
	return *sprx && *sl;
}

bool op_switch_openpak(char *err, int err_len)
{
	op_status s;
	unsigned char *ca[2] = { NULL, NULL }, *sprx = NULL;
	u64 cl[2] = { 0, 0 }, sl = 0;
	bool ok = false, mounted_here = false, wrote[2] = { false, false };
	char path[160], dst[160];
	int i;

	logf_("--- switch to OpenPak");
	op_status_read(&s);
	if (!op_supported(&s, err, err_len)) { logf_("refused: %s", err); return false; }
	if (!load_bundle(ca, cl, &sprx, &sl)) FAIL("Could not read the bundled files.");
	ensure_dirs();
	sysFsUnlink(OPENPAK_STATUS);
	sysFsUnlink(OPENPAK_REDIRECT_STATUS);

	/* 1. back up the stock CAs, once. Never overwrite a backup. */
	for (i = 0; i < 2; i++) {
		unsigned char *cur;
		u64 n;
		snprintf(path, sizeof(path), "%s/%s", OPENPAK_BACKUP_DIR, CA_NAMES[i]);
		if (exists(path)) { logf_("backup %s already exists, kept", path); continue; }
		snprintf(dst, sizeof(dst), "%s/%s", CERT_DIR, CA_NAMES[i]);
		cur = read_all(dst, MAX_CERT, &n);
		if (!cur || !n) { free(cur); FAIL("Could not read the stock %s.", CA_NAMES[i]); }
		if (n == cl[i] && memcmp(cur, ca[i], n) == 0) {
			free(cur);
			FAIL("%s is already OpenPak's and there is no stock backup. Not continuing.", CA_NAMES[i]);
		}
		if (!write_atomic(path, cur, n)) { free(cur); FAIL("Could not back up %s.", CA_NAMES[i]); }
		free(cur);
		logf_("backed up %s (%llu bytes)", dst, (unsigned long long)n);
	}

	/* 2. plugin file and boot_plugins.txt line (before the CA: the plugin alone is harmless) */
	if (!write_atomic(OPENPAK_PLUGIN_DST, sprx, sl)) FAIL("Could not install the plugin.");
	if (!boot_list_add()) FAIL("Could not update boot_plugins.txt.");
	logf_("plugin installed, boot_plugins.txt updated");

	/* 3. dev_flash */
	if (!exists("/dev_blind")) {
		int r = fs_mount_blind();
		if (r != 0 && !exists("/dev_blind")) FAIL("Could not mount dev_flash writable (error %08x).", (unsigned)r);
		mounted_here = true;
	}
	{
		u32 bs = 0;
		u64 freeb = 0;
		if (sysFsGetFreeSize("/dev_blind/", &bs, &freeb) != 0 || bs == 0)
			FAIL("Could not check free space on dev_flash.");
		if (freeb * bs < (cl[0] + cl[1]) * 2 + 65536)
			FAIL("Not enough free space on dev_flash.");
	}
	for (i = 0; i < 2; i++) {
		snprintf(dst, sizeof(dst), "%s/%s", BLIND_CERT, CA_NAMES[i]);
		if (!write_atomic(dst, ca[i], cl[i])) {
			/* undo the first file from the backup so the pair never stays mixed */
			if (i == 1 && wrote[0]) {
				unsigned char *bk; u64 n;
				snprintf(path, sizeof(path), "%s/%s", OPENPAK_BACKUP_DIR, CA_NAMES[0]);
				snprintf(dst, sizeof(dst), "%s/%s", BLIND_CERT, CA_NAMES[0]);
				bk = read_all(path, MAX_CERT, &n);
				if (bk) { write_atomic(dst, bk, n); free(bk); }
			}
			FAIL("Could not write %s.", CA_NAMES[i]);
		}
		wrote[i] = true;
		logf_("wrote %s (%llu bytes)", dst, (unsigned long long)cl[i]);
	}
	ok = true;
	logf_("switched to OpenPak");
done:
	if (mounted_here) fs_umount_blind();
	for (i = 0; i < 2; i++) free(ca[i]);
	free(sprx);
	return ok;
}

bool op_switch_psn(char *err, int err_len)
{
	op_status s;
	unsigned char *bk[2] = { NULL, NULL };
	u64 bl[2] = { 0, 0 };
	bool ok = false, mounted_here = false;
	char path[160], dst[160];
	int i;

	logf_("--- switch back to PSN");
	op_status_read(&s);
	if (!op_supported(&s, err, err_len)) { logf_("refused: %s", err); return false; }
	for (i = 0; i < 2; i++) {
		snprintf(path, sizeof(path), "%s/%s", OPENPAK_BACKUP_DIR, CA_NAMES[i]);
		bk[i] = read_all(path, MAX_CERT, &bl[i]);
		if (!bk[i] || !bl[i]) FAIL("The stock %s backup is missing. Nothing was changed.", CA_NAMES[i]);
	}
	if (!exists("/dev_blind")) {
		int r = fs_mount_blind();
		if (r != 0 && !exists("/dev_blind")) FAIL("Could not mount dev_flash writable (error %08x).", (unsigned)r);
		mounted_here = true;
	}
	for (i = 0; i < 2; i++) {
		snprintf(dst, sizeof(dst), "%s/%s", BLIND_CERT, CA_NAMES[i]);
		if (!write_atomic(dst, bk[i], bl[i])) FAIL("Could not restore %s.", CA_NAMES[i]);
		logf_("restored %s", dst);
	}
	if (!boot_list_remove()) FAIL("Could not update boot_plugins.txt.");
	sysFsUnlink(OPENPAK_PLUGIN_DST);
	sysFsUnlink(OPENPAK_STATUS);
	sysFsUnlink(OPENPAK_REDIRECT_STATUS);
	ok = true;
	logf_("switched back to PSN (backups kept)");
done:
	if (mounted_here) fs_umount_blind();
	for (i = 0; i < 2; i++) free(bk[i]);
	return ok;
}

/* Restart the way webMAN MOD's own restart paths do (include/cmd/restart.h and
 * include/file/file_delete.h del_turnoff): unmount dev_blind, delete the `turnoff` marker, then
 * sys_sm_shutdown (379) with op 0x0200 (SYS_SOFT_REBOOT in webMAN's main.c). The console creates
 * the marker at boot and removes it on a clean shutdown; a reboot syscall that leaves it behind
 * makes the next boot run the HDD check. v0.2.4 used op 0x8201 and left the marker. */
void op_reboot(void)
{
	logf_("reboot requested");
	if (exists("/dev_blind")) fs_umount_blind();
	sysFsUnlink(TURNOFF_PATH);
	sleep(2);                                /* let lv2 finish flushing the file writes */
	{
		lv2syscall3(379, 0x0200, 0, 0);
	}
}
