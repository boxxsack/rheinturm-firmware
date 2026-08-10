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
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAxOiivqVda8WAsiM2zhjM
2Xnwp5egf1t3oSqTL2UslvRgbI3HMzWF9qvzDtfn90uiDEyB9F3+Seqj7/qPU0Jx
1CSH3533HYK1417Lc4Y8duWhEi/0rH5soESNjQIqkGhGNASHKg+Fm5JhNC//o8mk
J00nYMnMeX90Pll7rXEfRqqih5E1kvbLL+xblUSOnHs21P0o7viM5exPO0iZhXDb
ppkTp17aIzs4vbSa9TQji8jWsdHlP28jDPzvtiJwJYFWskt7vbEwwiKdYcIiiYKo
eMi2FRw7rrv1JmGq5hnoyWH4JzGEuq1OtgBtY6DkQPpq02jSrxPb6uH4x9D3k239
OwIDAQAB
-----END PUBLIC KEY-----
)PEM";
