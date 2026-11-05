/*
 * OpenPak PS3 Patcher -- self-update (network, files). The decisions (manifest, signature, version
 * order, hash) are in update_core.c and tested on the host.
 *
 * The PS3's TLS stack cannot reach modern TLS, so this talks plain HTTP to a name OpenPak serves
 * (updates.ps3.openpak.org, a relay of the newest GitHub release). Transport is not the trust: the
 * manifest is Ed25519-signed by the pinned release-signing key and names the package's size and
 * sha256. A package that does not match is deleted and never handed on.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ppu-types.h>
#include <net/net.h>
#include <net/netdb.h>
#include <net/socket.h>
#include <netinet/in.h>
#include <lv2/sysfs.h>
#include <sys/file.h>

#include "patcher.h"

#define UP_HOST  "updates.ps3.openpak.org"
#define UP_BASE  "/api/v1/clients/ps3-patcher"
#define UP_IO_US 8000000       /* socket timeout, microseconds (Sony libnet takes an int) */

typedef int (*sink_fn)(void *ctx, const unsigned char *p, size_t n);

static int net_up;

static bool net_start(void)
{
	if (!net_up) {
		if (netInitialize() != 0) { logf_("update: netInitialize failed"); return false; }
		net_up = 1;
	}
	return true;
}

/* One GET over plain HTTP/1.0. Returns the status (200...) or -1 on a connection problem. The body
 * goes to sink; a sink that returns non-zero aborts. At most max body bytes are accepted. */
static int http_get(const char *path, sink_fn sink, void *ctx, size_t max)
{
	struct hostent *he;
	struct sockaddr_in sa;
	char req[256], head[1024];
	unsigned char buf[4096];
	int s, to = UP_IO_US, status = -1;
	size_t hl = 0, total = 0;
	int in_body = 0;
	ssize_t n;

	if (!net_start()) return -1;
	he = gethostbyname(UP_HOST);
	if (!he || !he->h_addr_list || !he->h_addr_list[0] || he->h_length != 4) { logf_("update: cannot resolve " UP_HOST); return -1; }
	memset(&sa, 0, sizeof(sa));
	sa.sin_len = sizeof(sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(80);
	memcpy(&sa.sin_addr, he->h_addr_list[0], 4);
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) return -1;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(to));
	if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) { logf_("update: connect failed"); netClose(s); return -1; }
	snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: " UP_HOST "\r\nUser-Agent: OpenPak-PS3-Patcher/%s\r\nConnection: close\r\n\r\n",
		 path, OPENPAK_VERSION);
	if (send(s, req, strlen(req), 0) < 0) { netClose(s); return -1; }
	while ((n = recv(s, buf, sizeof(buf), 0)) > 0) {
		const unsigned char *p = buf;
		size_t left = (size_t)n;
		if (!in_body) {
			while (left && hl < sizeof(head) - 1) {
				head[hl++] = (char)*p++;
				left--;
				head[hl] = 0;
				if (hl >= 4 && memcmp(head + hl - 4, "\r\n\r\n", 4) == 0) { in_body = 1; break; }
			}
			if (!in_body) { if (hl >= sizeof(head) - 1) break; continue; }
			if (sscanf(head, "HTTP/%*d.%*d %d", &status) != 1) { status = -1; break; }
			if (status != 200) break;
		}
		if (left) {
			total += left;
			if (total > max) { status = -1; break; }
			if (sink(ctx, p, left) != 0) { status = -1; break; }
		}
	}
	netClose(s);
	if (!in_body && status == -1) logf_("update: no complete response");
	return status;
}

struct memsink { unsigned char *p; size_t n, cap; };

static int mem_sink(void *ctx, const unsigned char *p, size_t n)
{
	struct memsink *m = ctx;
	if (m->n + n > m->cap) return 1;
	memcpy(m->p + m->n, p, n);
	m->n += n;
	return 0;
}

static int fetch_mem(const char *path, unsigned char *buf, size_t cap, size_t *len)
{
	struct memsink m = { buf, 0, cap - 1 };
	int st = http_get(path, mem_sink, &m, cap - 1);
	buf[m.n] = 0;
	*len = m.n;
	return st;
}

bool op_update_check(up_manifest *m, char *why, int why_len)
{
	static unsigned char man[2048], sig[512];
	size_t ml = 0, sl = 0;
	int st;

	if (!strcmp(OPENPAK_VERSION, "dev")) { snprintf(why, why_len, "dev build"); return false; }
	st = fetch_mem(UP_BASE "/latest", man, sizeof(man), &ml);
	if (st != 200) { snprintf(why, why_len, st < 0 ? "offline" : "no release manifest (%d)", st); logf_("update: manifest %d", st); return false; }
	st = fetch_mem(UP_BASE "/latest.sig", sig, sizeof(sig), &sl);
	if (st != 200) { snprintf(why, why_len, "no signature (%d)", st); logf_("update: signature %d", st); return false; }
	if (!up_verify(man, ml, (const char *)sig)) { snprintf(why, why_len, "bad signature"); logf_("update: manifest signature does not verify; ignored"); return false; }
	if (!up_parse_manifest((const char *)man, ml, m)) { snprintf(why, why_len, "bad manifest"); logf_("update: signed manifest does not parse"); return false; }
	logf_("update: signed manifest ok: version %s file %s size %llu (this app %s)", m->version, m->file, m->size, OPENPAK_VERSION);
	if (!up_version_newer(m->version, OPENPAK_VERSION)) { snprintf(why, why_len, "current"); return false; }
	return true;
}

bool op_update_declined(const char *version)
{
	char buf[64] = { 0 };
	FILE *f = fopen(OPENPAK_UPDATE_NOTE, "r");
	char want[48];
	if (!f) return false;
	if (fgets(buf, sizeof(buf), f)) {}
	fclose(f);
	snprintf(want, sizeof(want), "declined=%s", version);
	return strncmp(buf, want, strlen(want)) == 0 && (buf[strlen(want)] == '\n' || buf[strlen(want)] == 0);
}

void op_update_decline(const char *version)
{
	FILE *f = fopen(OPENPAK_UPDATE_NOTE, "w");
	if (!f) return;
	fprintf(f, "declined=%s\n", version);
	fclose(f);
}

struct filesink { FILE *f; up_sha256 sha; unsigned long long n; };

static int file_sink(void *ctx, const unsigned char *p, size_t n)
{
	struct filesink *fs = ctx;
	up_sha256_update(&fs->sha, p, n);
	fs->n += n;
	return fwrite(p, 1, n, fs->f) == n ? 0 : 1;
}

bool op_update_fetch(const up_manifest *m, char *path, int path_len, char *err, int err_len)
{
	char part[200], url[200], hex[65];
	struct filesink fs;
	int st;

	if (!up_file_name_ok(m->file)) { snprintf(err, err_len, "bad file name"); return false; }
	sysFsMkdir(OPENPAK_DIR, 0777);
	sysFsMkdir(OPENPAK_UPDATE_DIR, 0777);
	snprintf(path, path_len, "%s/%s", OPENPAK_UPDATE_DIR, m->file);
	snprintf(part, sizeof(part), "%s.part", path);
	snprintf(url, sizeof(url), UP_BASE "/download/%s", m->file);
	remove(part);
	fs.f = fopen(part, "wb");
	if (!fs.f) { snprintf(err, err_len, "cannot write %s", part); return false; }
	fs.n = 0;
	up_sha256_init(&fs.sha);
	st = http_get(url, file_sink, &fs, (size_t)m->size);
	if (fflush(fs.f) != 0) st = -1;
	fclose(fs.f);
	up_sha256_hex(&fs.sha, hex);
	if (st != 200) { snprintf(err, err_len, st < 0 ? "download failed" : "download failed (%d)", st); remove(part); return false; }
	if (fs.n != m->size) { snprintf(err, err_len, "size %llu, expected %llu", fs.n, m->size); logf_("update: %s", err); remove(part); return false; }
	if (strcmp(hex, m->sha256) != 0) { snprintf(err, err_len, "sha256 does not match the signed manifest"); logf_("update: got %s want %s", hex, m->sha256); remove(part); return false; }
	remove(path);
	if (rename(part, path) != 0) { snprintf(err, err_len, "cannot place the package"); remove(part); return false; }
	logf_("update: %s verified (%llu bytes, sha256 %s)", path, fs.n, hex);
	return true;
}

static bool copy_file(const char *from, const char *to)
{
	FILE *a = fopen(from, "rb"), *b;
	unsigned char buf[8192];
	size_t n;
	bool ok = true;
	if (!a) return false;
	b = fopen(to, "wb");
	if (!b) { fclose(a); return false; }
	while ((n = fread(buf, 1, sizeof(buf), a)) > 0)
		if (fwrite(buf, 1, n, b) != n) { ok = false; break; }
	fclose(a);
	if (fflush(b) != 0) ok = false;
	fclose(b);
	return ok;
}

bool op_update_handoff(const char *path, bool plugin_loaded, bool *queued, char *msg, int msg_len)
{
	*queued = false;
	if (plugin_loaded) {
		FILE *f = fopen(OPENPAK_UPDATE_REQ, "w");
		if (f) {
			fprintf(f, "%s\n", path);
			if (fflush(f) == 0) {
				fclose(f);
				*queued = true;
				snprintf(msg, msg_len, "Update downloaded and verified.\n\nClose this app. The OpenPak plugin installs it from the XMB "
					 "in a few seconds (a progress bar shows). Start the app again afterwards.");
				logf_("update: install request written for %s", path);
				return true;
			}
			fclose(f);
		}
		logf_("update: could not write the install request, falling back");
	}
	/* Fallback until the plugin loads: the Package Manager lists /dev_hdd0/packages. */
	{
		char dst[200];
		const char *base = strrchr(path, '/');
		sysFsMkdir("/dev_hdd0/packages", 0777);
		snprintf(dst, sizeof(dst), "/dev_hdd0/packages%s", base ? base : "/update.pkg");
		if (!copy_file(path, dst)) { snprintf(msg, msg_len, "Could not save the package to /dev_hdd0/packages."); return false; }
		snprintf(msg, msg_len, "Update downloaded and verified.\n\nThe plugin is not loaded, so install it by hand: close this app, "
			 "then Game > Package Manager > Install Package Files > Standard, and pick %s. (Temporary step.)", base ? base + 1 : "the package");
		logf_("update: copied to %s for the Package Manager", dst);
		return true;
	}
}
