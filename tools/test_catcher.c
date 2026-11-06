/* Host-side test of plugin/catcher.h and plugin/catchlist.h.
 *   cc -O2 -Wall -o <out> tools/test_catcher.c && <out>
 * Exits non-zero on any failure. */
#include <stdio.h>
#include <string.h>
#include "../plugin/catcher.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int query(unsigned char *b, const char *name, int qtype, int edns)
{
	int n = 12, i = 0;
	memset(b, 0, 12);
	b[0] = 0x12; b[1] = 0x34; b[2] = 0x01; b[5] = 1;
	while (name[i]) {
		int s = i;
		while (name[i] && name[i] != '.') i++;
		b[n++] = (unsigned char)(i - s);
		memcpy(b + n, name + s, (size_t)(i - s));
		n += i - s;
		if (name[i]) i++;
	}
	b[n++] = 0;
	b[n++] = 0; b[n++] = (unsigned char)qtype; b[n++] = 0; b[n++] = 1;
	if (edns) {   /* OPT record */
		static const unsigned char opt[11] = { 0, 0, 41, 4, 0xd0, 0, 0, 0, 0, 0, 0 };
		b[11] = 1;
		memcpy(b + n, opt, sizeof opt);
		n += sizeof opt;
	}
	return n;
}

int main(void)
{
	static catcher_list l;
	unsigned char q[512], a[512];
	char name[CATCHER_NAME];
	int n, r, hit;

	l.ip[0] = 145; l.ip[1] = 241; l.ip[2] = 199; l.ip[3] = 19;
	CHECK(catcher_add(&l, CATCHER_FAMILY, ".playstation.net", 16));
	CHECK(catcher_add(&l, CATCHER_FAMILY, "*.PlayStation.com", 17));
	CHECK(catcher_add(&l, CATCHER_EXACT, "time.example.org", 16));
	CHECK(catcher_add(&l, CATCHER_NEVER, "jellyfin.playstation.net", 24));
	CHECK(!catcher_add(&l, CATCHER_FAMILY, ".com", 4));          /* one label */
	CHECK(!catcher_add(&l, CATCHER_FAMILY, "a..b", 4));
	CHECK(!catcher_add(&l, CATCHER_FAMILY, "a b.com", 7));
	CHECK(l.n == 4);

	CHECK(catcher_match(&l, "native-ps3.np.ac.playstation.net") == 0);
	CHECK(catcher_match(&l, "playstation.net") == 0);
	CHECK(catcher_match(&l, "playstation.net.") == 0);
	CHECK(catcher_match(&l, "xplaystation.net") < 0);
	CHECK(catcher_match(&l, "playstation.net.evil.org") < 0);
	CHECK(catcher_match(&l, "store.playstation.com") == 1);
	CHECK(catcher_match(&l, "time.example.org") == 2);
	CHECK(catcher_match(&l, "a.time.example.org") < 0);
	CHECK(catcher_match(&l, "jellyfin.playstation.net") < 0);
	CHECK(catcher_match(&l, "jellyfin.lan") < 0);

	/* caught A query, mixed case, with EDNS */
	n = query(q, "Native-PS3.np.ac.PlayStation.net", 1, 1);
	r = catcher_handle(&l, q, n, a, sizeof a, name, &hit);
	CHECK(hit == 0);
	CHECK(strcmp(name, "native-ps3.np.ac.playstation.net") == 0);
	CHECK(r == n - 11 + 16);
	CHECK(a[0] == 0x12 && a[1] == 0x34);
	CHECK(a[2] == 0x81 && a[3] == 0x80);
	CHECK(a[5] == 1 && a[7] == 1 && a[9] == 0 && a[11] == 0);
	CHECK(memcmp(a + r - 4, l.ip, 4) == 0);
	CHECK(a[r - 16] == 0xc0 && a[r - 15] == 0x0c);

	/* caught AAAA: empty NOERROR */
	n = query(q, "auth.np.ac.playstation.net", 28, 0);
	r = catcher_handle(&l, q, n, a, sizeof a, name, &hit);
	CHECK(r == n && a[7] == 0 && a[3] == 0x80);

	/* passed */
	n = query(q, "jellyfin.lan", 1, 0);
	CHECK(catcher_handle(&l, q, n, a, sizeof a, name, &hit) == 0 && hit < 0);
	CHECK(strcmp(name, "jellyfin.lan") == 0);

	/* too small an answer buffer, responses, truncated */
	n = query(q, "a.playstation.net", 1, 0);
	CHECK(catcher_handle(&l, q, n, a, n + 15, name, &hit) == 0);
	q[2] |= 0x80;
	CHECK(catcher_handle(&l, q, n, a, sizeof a, name, &hit) == 0);
	q[2] &= 0x7f;
	CHECK(catcher_handle(&l, q, n - 3, a, sizeof a, name, &hit) == 0);
	/* empty list passes everything */
	{
		static catcher_list e;
		CHECK(catcher_handle(&e, q, n, a, sizeof a, name, &hit) == 0);
	}

	printf(fails ? "catcher: %d failures\n" : "catcher: ok\n", fails);
	return fails != 0;
}
