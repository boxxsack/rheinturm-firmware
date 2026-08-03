#pragma once

#include <cstddef>
#include <cstdint>

// Verifies an OTA firmware image against an embedded RSA public key before it
// is allowed to be flashed (issue #17: defense-in-depth image-integrity
// check, independent of the OTA transport TLS verification from PR #16).
//
// Signing scheme: RSASSA-PKCS1-v1.5 over the SHA-256 digest of the raw
// firmware.bin bytes (see scripts/sign_firmware.py). PKCS1-v1.5 was chosen
// over RSA-PSS for its simpler, version-stable mbedtls verification call
// (mbedtls_pk_verify) with no salt-length/padding-mode pitfalls.
//
// This is a pure function with no Arduino/ESP-IDF dependency (only mbedtls),
// so it is testable on the host — see test/test_ota_image_verifier.
namespace OtaImageVerifier {

constexpr size_t kSha256DigestSize = 32;
constexpr size_t kRsa2048SignatureSize = 256;

// Returns true only if `signature` is a valid RSASSA-PKCS1-v1.5/SHA-256
// signature over `digest`, produced by the private key matching
// `publicKeyPem`. Any parsing failure, length mismatch, or signature
// mismatch returns false — there is no partial-success case.
//
// digest        must point to exactly kSha256DigestSize bytes.
// signature     must point to exactly signatureLen bytes.
// signatureLen  must equal kRsa2048SignatureSize (else rejected outright).
// publicKeyPem  must be a null-terminated PEM-encoded RSA public key.
bool verify(const uint8_t *digest, const uint8_t *signature, size_t signatureLen, const char *publicKeyPem);

}  // namespace OtaImageVerifier
