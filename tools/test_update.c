/* Host test of the self-update core. Usage: test_update <manifest.txt> <manifest.sig> <good-sha256>
 * (a manifest signed by the pinned release key) -- run by CI after signing; with no arguments it runs
 * the self-contained checks only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../app/source/update_core.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static char *slurp(const char *p, size_t *n)
{
	FILE *f = fopen(p, "rb");
	char *b;
	if (!f) return NULL;
	b = malloc(70000);
	*n = fread(b, 1, 69999, f);
	b[*n] = 0;
	fclose(f);
	return b;
}

int main(int argc, char **argv)
{
	up_sha256 s;
	char hex[65];
	up_manifest m;
	const char *ok = "type=openpak-ps3-patcher-release\nversion=0.2.9\nfile=OpenPak-PS3-Patcher-v0.2.9.pkg\nsize=349680\nsha256=0000000000000000000000000000000000000000000000000000000000000000\n";

	up_sha256_init(&s); up_sha256_update(&s, "abc", 3); up_sha256_hex(&s, hex);
	CHECK(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	up_sha256_init(&s);
	{ char big[1000]; int i; memset(big, 'a', sizeof(big)); for (i = 0; i < 1000; i++) up_sha256_update(&s, big, sizeof(big)); }
	up_sha256_hex(&s, hex);
	CHECK(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
	up_sha256_init(&s); up_sha256_hex(&s, hex);
	CHECK(!strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

	CHECK(up_parse_manifest(ok, strlen(ok), &m) && !strcmp(m.version, "0.2.9") && m.size == 349680);
	CHECK(!up_parse_manifest("type=other\nversion=0.2.9\n", 24, &m));
	{ char bad[400]; snprintf(bad, sizeof(bad), "%s", ok); bad[strstr(bad, "OpenPak-PS3") - bad] = '/'; CHECK(!up_parse_manifest(bad, strlen(bad), &m)); }
	CHECK(!up_file_name_ok("../x.pkg") && !up_file_name_ok("a/b.pkg") && !up_file_name_ok("x.elf") && up_file_name_ok("OpenPak-PS3-Patcher-v0.2.9.pkg"));
	CHECK(up_version_newer("0.2.9", "0.2.8") && up_version_newer("0.10.0", "0.9.9") && !up_version_newer("0.2.8", "0.2.8") && !up_version_newer("0.2.7", "0.2.8"));
	CHECK(!up_verify((const uint8_t *)ok, strlen(ok), "00"));
	CHECK(!up_verify((const uint8_t *)ok, strlen(ok), "zz"));

	if (argc == 4) {
		size_t mn = 0, sn = 0;
		char *man = slurp(argv[1], &mn), *sig = slurp(argv[2], &sn);
		CHECK(man && sig);
		if (man && sig) {
			CHECK(up_verify((const uint8_t *)man, mn, sig));
			man[0] ^= 1;                                   /* any change to the manifest breaks it */
			CHECK(!up_verify((const uint8_t *)man, mn, sig));
			man[0] ^= 1;
			CHECK(up_parse_manifest(man, mn, &m) && !strcmp(m.sha256, argv[3]));
		}
	}
	printf(fails ? "FAILED (%d)\n" : "ok\n", fails);
	return fails != 0;
}
