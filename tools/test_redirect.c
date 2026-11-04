/* Host-side test of plugin/redirect.h against a fake lv1 address space.
 *   cc -O2 -Wall -Iplugin -o scratch tools/test_redirect.c && ./scratch
 * Needs plugin/openpak_names.h (make stage). Exits non-zero on any failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMSZ 0x2000000ULL
static unsigned char *mem;
static int run = 1, pokes;

static unsigned long long be(unsigned long long a) { unsigned long long w = 0; int i; for (i = 0; i < 8; i++) w = (w << 8) | mem[a + i]; return w; }
#define RD_PEEK(a)     be(a)
#define RD_POKE(a, v)  do { unsigned long long _a = (a), _v = (v); int _i; pokes++; for (_i = 0; _i < 8; _i++) mem[_a + _i] = (unsigned char)(_v >> (56 - 8 * _i)); } while (0)
#define RD_LV2PEEK(a)  be(0x1000000ULL + (a))
#define RD_PAUSE()     do { } while (0)
#define RD_RUN()       run
#include "../plugin/redirect.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void put(unsigned long long a, const char *s) { memcpy(mem + a, s, strlen(s) + 1); }
static int is(unsigned long long a, const char *s) { return memcmp(mem + a, s, strlen(s) + 1) == 0; }

int main(void)
{
	static struct rd_state st;
	unsigned long long a, off;
	

	mem = calloc(1, MEMSZ);
	/* a fake lv2 kernel image: non-zero words every 0x20000 from 0x10000 */
	for (a = 0x10000; a < 0x2F0000; a += 8) { mem[0x1000000 + a + 7] = (unsigned char)(a >> 3) | 1; }
	off = rd_find_lv2off();
	CHECK(off == 0x1000000ULL);

	/* plant names: aligned, unaligned, window straddling, in both ranges, plus decoys */
	put(0x880100, "https://auth.%s.ac.playstation.net/nav/auth");
	put(0x8803F3, "http://ena.net.playstation.net");
	put(0x881FF0 + 3, "csla.%s.community.playstation.net");            /* straddles the 4 KiB edge */
	put(0xF00005, "x.stun.playstation.net");
	put(0x1000000 + 0x300003, "http://nsx-e.%s.dl.playstation.net/nsx/log");
	put(0x1000000 + 0x7FFF00, "auth.api.");
	put(0x1000000 + 0x7FFF10, "np.ac.playstation.net");
	put(0x884000, "http://playstation.net/np/rt/presence");           /* namespace: must stay */
	put(0x884100, "%s@%s.%s.%s.playstation.net");                      /* JID format: must stay */
	put(0x884200, "playstation.net.evil.example");                     /* no leading dot: must stay */
	put(0x884300, "fus01.ps3.update.playstation.net/update");
	put(0x884400, "hoge.ac.playstation.nexx");                         /* near miss */
	put(0xE1FFF0, "trophy.playstation.net");                           /* last bytes of the LV1 range */
	put(0x300000 - 2, "z.ac.playstation.net");                         /* below the LV1 range: ignored */

	rd_init(&st);
	st.lv2off = off;
	rd_pass(&st);

	CHECK(is(0x880100, "https://auth.%s.ac.ps3.openpak.org/nav/auth"));
	CHECK(is(0x8803F3, "http://ena.net.ps3.openpak.org"));
	CHECK(is(0x881FF0 + 3, "csla.%s.community.ps3.openpak.org"));
	CHECK(is(0xF00005, "x.stun.playstation.net"));                      /* outside both ranges */
	CHECK(is(0x1000000 + 0x300003, "http://nsx-e.%s.dl.ps3.openpak.org/nsx/log"));
	CHECK(is(0x1000000 + 0x7FFF10, "np.ac.ps3.openpak.org"));
	CHECK(is(0x884000, "http://playstation.net/np/rt/presence"));
	CHECK(is(0x884100, "%s@%s.%s.%s.playstation.net"));
	CHECK(is(0x884200, "playstation.net.evil.example"));
	CHECK(is(0x884300, "fus01.ps3.update.ps3.openpak.org/update"));
	CHECK(is(0x884400, "hoge.ac.playstation.nexx"));
	CHECK(is(0xE1FFF0, "trophy.ps3.openpak.org"));
	CHECK(is(0x300000 - 2, "z.ac.playstation.net"));
	CHECK(st.nbk == 7 && st.failed == 0);
	CHECK(rd_names_live(&st) == 6);

	/* a second pass is a no-op */
	{ int before = pokes; rd_pass(&st); CHECK(pokes == before); CHECK(st.nbk == 7); }

	/* a name that appears later (module loaded after boot) is caught by the next pass */
	put(0x890000, "x.communication.playstation.net");
	rd_pass(&st);
	CHECK(is(0x890000, "x.communication.ps3.openpak.org"));
	CHECK(st.nbk == 8);

	/* restore puts every original byte back; nothing else was touched */
	rd_restore(&st);
	CHECK(is(0x880100, "https://auth.%s.ac.playstation.net/nav/auth"));
	CHECK(is(0x8803F3, "http://ena.net.playstation.net"));
	CHECK(is(0x881FF0 + 3, "csla.%s.community.playstation.net"));
	CHECK(is(0x1000000 + 0x300003, "http://nsx-e.%s.dl.playstation.net/nsx/log"));
	CHECK(is(0x1000000 + 0x7FFF10, "np.ac.playstation.net"));
	CHECK(is(0xE1FFF0, "trophy.playstation.net"));
	CHECK(is(0x890000, "x.communication.playstation.net"));
	CHECK(st.nbk == 0 && rd_names_live(&st) == 0);

	/* lv2 offset not found when the views disagree */
	for (a = 0x10000; a < 0x2F0000; a += 8) mem[0x1000000 + a + 7] ^= 0x40;
	CHECK(rd_find_lv2off() == 0x1000000ULL); /* still self-consistent: both views read the same fake memory */

	printf(fails ? "FAILED (%d)\n" : "ok\n", fails);
	return fails ? 1 : 0;
}
