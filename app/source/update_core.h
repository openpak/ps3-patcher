#ifndef OPENPAK_UPDATE_CORE_H
#define OPENPAK_UPDATE_CORE_H

/* Self-update, the part with no console dependency (host-testable): the signed release manifest,
 * its Ed25519 signature, version order and a streaming SHA-256.
 *
 * Trust: the manifest text is signed with the release-signing key (a separate key from the redirect
 * ceiling key, never used for the redirect list). The signature is 128 hex characters over the exact
 * manifest bytes. Pinned public keys are compiled in; a list so a new key can be added before the old
 * one is retired. The manifest names the package size and sha256. Nothing is installed unless both
 * the signature and the hash verify. */

#include <stddef.h>
#include <stdint.h>

#define UP_TYPE "openpak-ps3-patcher-release"

typedef struct {
	char version[16];
	char file[96];
	unsigned long long size;
	char sha256[65];
} up_manifest;

typedef struct {
	uint32_t state[8];
	uint64_t len;
	uint8_t buf[64];
	size_t blen;
} up_sha256;

void up_sha256_init(up_sha256 *c);
void up_sha256_update(up_sha256 *c, const void *data, size_t n);
void up_sha256_hex(up_sha256 *c, char out[65]);

/* 1 when the signature (hex) verifies under one of the pinned keys, else 0. */
int up_verify(const uint8_t *msg, size_t len, const char *sighex);

/* Parse "key=value" lines. Returns 1 when type, version, file, size and sha256 are all valid. */
int up_parse_manifest(const char *text, size_t len, up_manifest *m);

/* 1 when version a is newer than version b ("1.2.3" style, numeric parts). */
int up_version_newer(const char *a, const char *b);

/* The file name must be a plain package name: no path, no dots-only tricks. */
int up_file_name_ok(const char *name);

#endif
