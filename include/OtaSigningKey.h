#pragma once

// OTA image-signing trust anchor (issue #17 defense-in-depth: cryptographic
// verification of the firmware image itself, independent of the OTA
// transport TLS check added in PR #16).
//
// This is the PUBLIC half of an RSA-2048 keypair. The matching PRIVATE key
// lives only as the `OTA_SIGNING_PRIVATE_KEY` GitHub Actions secret used by
// the release workflow (see scripts/sign_firmware.py and
// .github/workflows/release.yml) — it is never committed to this repo.
//
// Signature scheme: RSASSA-PKCS1-v1.5 over the SHA-256 digest of the raw
// firmware.bin bytes. Verified with OtaImageVerifier (see
// include/OtaImageVerifier.h) before the downloaded image is flashed.
//
// SHA-256 fingerprint of this public key (DER-encoded SubjectPublicKeyInfo):
//   eb9ab26d5a78decb7c130245a0fd0ff8ad228596a137da1cfd0e147c1ec64b4
//
// To rotate this key: generate a new RSA-2048 keypair, replace the PEM below,
// update the `OTA_SIGNING_PRIVATE_KEY` secret, and ship a release signed with
// the OLD key that embeds the NEW public key before retiring the old private
// key (otherwise devices on old firmware can no longer verify new releases).
static const char *OTA_SIGNING_PUBLIC_KEY = R"PEM(
-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsGBAJv1SUCq0Pey4CwK9
jhK2lADVqdX9EyLZoV1u3ApxsRfbXUZwYPj0lB52teAUIoo0BhVLqan0i3M3lwet
6PwXK3inumXmcgdNmPI+kzC88OxHA0PyaptrXR2w+MJVBiH9K27/tliR+MzEQAL6
qcuVoMeSJ6f5FyuIN8xRmAevqXmtGQDbomaBh+RqmJFrwEpKoll5/0feMv/ev0TK
oNHdtHeWEI9Aq1ynvmQTjvR+KlshGsOKMdoEmxH6QuXJCSqy/4ccsqDUSPKAgPTp
N+tBX5EcbT+nDTSUh5O2xcpmSPG5MbPHryRwdkxsGanj2Hw3hyTtm0haL/22cro6
uwIDAQAB
-----END PUBLIC KEY-----
)PEM";
