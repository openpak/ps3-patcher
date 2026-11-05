# Self-update

On launch the app asks `http://updates.ps3.openpak.org/api/v1/clients/ps3-patcher/latest` for the newest
release. If it is newer than the app and not already declined, it asks "OpenPak PS3 Patcher vY is
available (you have vX). Update now?". Saying no is remembered per version
(`/dev_hdd0/openpak/update.txt`); the next release asks again.

## Why plain HTTP

The 4.93 TLS stack cannot reach GitHub or `openpak.org` (RSA key exchange only, TLS 1.0-1.2). The
mirror is a website endpoint on a DNS-only name that relays the newest GitHub release. The transport
is not the trust:

- `manifest.txt` (`type`, `version`, `file`, `size`, `sha256`) is signed with the release-signing
  key. `manifest.sig` is the Ed25519 signature, 128 hex characters, over the exact manifest bytes.
  The public key is pinned in `app/source/update_core.c` (a list, so it can be rotated). This key is
  separate from the redirect ceiling key and never signs the redirect list.
- The package is downloaded to `/dev_hdd0/openpak/update/<file>.part`, its size and sha256 are
  checked against the signed manifest, and only then is it renamed. A mismatch deletes it.
- Nothing unverified is ever handed on.

## Installing

The app cannot replace itself while it runs. With the plugin loaded, the app writes the verified
package path into `/dev_hdd0/openpak/update/install.req` and exits to the XMB; the plugin sees the
request, waits until no game is running, and installs the package silently through vsh's
`game_ext_plugin` (`installPKG`), the way webMAN MOD's installer does. The result is in
`install.ack`. Until the plugin loads, the app copies the package to `/dev_hdd0/packages` and says to
use Game > Package Manager > Install Package Files (temporary).

## Release side

The release workflow builds the package, writes and signs `manifest.txt` with the
`PS3_RELEASE_SIGNING_KEY` Actions secret, checks the result with the app's own verifier, and
publishes `manifest.txt` and `manifest.sig` with the package. The website relays them; no deploy and
no manual step per release.
