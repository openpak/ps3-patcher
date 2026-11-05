#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "update_core.h"
#include "vendor/tweetnacl.h"

/* tweetnacl needs a randombytes for key generation; verification never calls it. */
void randombytes(unsigned char *p, unsigned long long n) { memset(p, 0, (size_t)n); }

/* Pinned release-signing public keys (Ed25519, hex). Not the redirect-ceiling key. */
static const char *const pinned_keys[] = {
	"0b0dba96e04e792c9f1ca859eb02d2ddfef9ee8a60f3a856d256d5ab8420d1f8",
};

static int hexval(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int unhex(const char *h, size_t hl, uint8_t *out, size_t outlen)
{
	size_t i;
	if (hl != outlen * 2) return 0;
	for (i = 0; i < outlen; i++) {
		int a = hexval(h[2 * i]), b = hexval(h[2 * i + 1]);
		if (a < 0 || b < 0) return 0;
		out[i] = (uint8_t)(a * 16 + b);
	}
	return 1;
}

int up_verify(const uint8_t *msg, size_t len, const char *sighex)
{
	uint8_t sig[64], pk[32];
	uint8_t *sm, *m;
	unsigned long long mlen = 0;
	size_t i, sl = strlen(sighex);
	int ok = 0;

	while (sl && (sighex[sl - 1] == '\n' || sighex[sl - 1] == '\r' || sighex[sl - 1] == ' ')) sl--;
	if (!unhex(sighex, sl, sig, 64) || len == 0 || len > (1 << 16)) return 0;
	sm = malloc(len + 64);
	m = malloc(len + 64);
	if (!sm || !m) { free(sm); free(m); return 0; }
	memcpy(sm, sig, 64);
	memcpy(sm + 64, msg, len);
	for (i = 0; i < sizeof(pinned_keys) / sizeof(pinned_keys[0]) && !ok; i++) {
		if (!unhex(pinned_keys[i], 64, pk, 32)) continue;
		if (crypto_sign_open(m, &mlen, sm, len + 64, pk) == 0 && mlen == len) ok = 1;
	}
	free(sm);
	free(m);
	return ok;
}

int up_file_name_ok(const char *n)
{
	size_t i, l = strlen(n);
	if (l < 5 || l > 90 || n[0] == '.') return 0;
	if (strcmp(n + l - 4, ".pkg") != 0) return 0;
	for (i = 0; i < l; i++) {
		char c = n[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
			return 0;
	}
	return strstr(n, "..") == NULL;
}

static int version_ok(const char *v)
{
	int dots = 0;
	size_t i, l = strlen(v);
	if (l == 0 || l > 14) return 0;
	for (i = 0; i < l; i++) {
		if (v[i] == '.') { dots++; if (i == 0 || v[i - 1] == '.') return 0; }
		else if (v[i] < '0' || v[i] > '9') return 0;
	}
	return dots == 2 && v[l - 1] != '.';
}

int up_version_newer(const char *a, const char *b)
{
	int i;
	for (i = 0; i < 3; i++) {
		long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
		if (x != y) return x > y;
		if (*a == '.') a++;
		if (*b == '.') b++;
	}
	return 0;
}

int up_parse_manifest(const char *text, size_t len, up_manifest *m)
{
	char line[200];
	size_t i = 0;
	int have_type = 0, have_ver = 0, have_file = 0, have_size = 0, have_hash = 0;

	memset(m, 0, sizeof(*m));
	while (i < len) {
		size_t j = i, n;
		char *eq;
		while (j < len && text[j] != '\n') j++;
		n = j - i;
		if (n && text[i + n - 1] == '\r') n--;
		if (n >= sizeof(line)) return 0;
		memcpy(line, text + i, n);
		line[n] = 0;
		i = j + 1;
		if (!n) continue;
		eq = strchr(line, '=');
		if (!eq) return 0;
		*eq++ = 0;
		if (!strcmp(line, "type")) have_type = !strcmp(eq, UP_TYPE);
		else if (!strcmp(line, "version")) { if (!version_ok(eq)) return 0; snprintf(m->version, sizeof(m->version), "%s", eq); have_ver = 1; }
		else if (!strcmp(line, "file")) { if (!up_file_name_ok(eq)) return 0; snprintf(m->file, sizeof(m->file), "%s", eq); have_file = 1; }
		else if (!strcmp(line, "size")) { m->size = strtoull(eq, NULL, 10); have_size = m->size > 0 && m->size <= (16ULL << 20); }
		else if (!strcmp(line, "sha256")) {
			uint8_t d[32];
			if (!unhex(eq, strlen(eq), d, 32)) return 0;
			snprintf(m->sha256, sizeof(m->sha256), "%s", eq);
			have_hash = 1;
		}
	}
	return have_type && have_ver && have_file && have_size && have_hash;
}

/* ---- SHA-256 (FIPS 180-4) ---- */

static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void block(up_sha256 *c, const uint8_t *p)
{
	uint32_t w[64], a, b, cc, d, e, f, g, h, t1, t2;
	int i;
	for (i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (i = 16; i < 64; i++) {
		uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	a = c->state[0]; b = c->state[1]; cc = c->state[2]; d = c->state[3];
	e = c->state[4]; f = c->state[5]; g = c->state[6]; h = c->state[7];
	for (i = 0; i < 64; i++) {
		t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
		h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
	}
	c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
	c->state[4] += e; c->state[5] += f; c->state[6] += g; c->state[7] += h;
}

void up_sha256_init(up_sha256 *c)
{
	static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	memcpy(c->state, iv, sizeof(iv));
	c->len = 0;
	c->blen = 0;
}

void up_sha256_update(up_sha256 *c, const void *data, size_t n)
{
	const uint8_t *p = data;
	c->len += n;
	while (n) {
		size_t k = 64 - c->blen;
		if (k > n) k = n;
		memcpy(c->buf + c->blen, p, k);
		c->blen += k; p += k; n -= k;
		if (c->blen == 64) { block(c, c->buf); c->blen = 0; }
	}
}

void up_sha256_hex(up_sha256 *c, char out[65])
{
	uint64_t bits = c->len * 8;
	uint8_t pad[72];
	size_t padn = (c->blen < 56) ? 56 - c->blen : 120 - c->blen;
	int i;
	memset(pad, 0, sizeof(pad));
	pad[0] = 0x80;
	for (i = 0; i < 8; i++) pad[padn + i] = (uint8_t)(bits >> (56 - 8 * i));
	{
		up_sha256 t = *c;
		up_sha256_update(&t, pad, padn + 8);
		for (i = 0; i < 8; i++) sprintf(out + 8 * i, "%08x", t.state[i]);
	}
	out[64] = 0;
}
