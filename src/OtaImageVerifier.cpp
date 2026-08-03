#include "OtaImageVerifier.h"

#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/version.h"
#if MBEDTLS_VERSION_MAJOR >= 4
#include "psa/crypto.h"
#endif

#include <cstring>

namespace OtaImageVerifier {

bool verify(const uint8_t *digest, const uint8_t *signature, size_t signatureLen, const char *publicKeyPem) {
    if (!digest || !signature || !publicKeyPem || signatureLen != kRsa2048SignatureSize) {
        return false;
    }

#if MBEDTLS_VERSION_MAJOR >= 4
    // mbedtls 4.x routes PK operations through PSA; PSA must be initialized
    // before any cryptographic operation, including parsing a public key.
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
#endif

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    bool valid = false;
    int ret = mbedtls_pk_parse_public_key(&pk, reinterpret_cast<const unsigned char *>(publicKeyPem), std::strlen(publicKeyPem) + 1);
    if (ret == 0) {
        ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, kSha256DigestSize, signature, signatureLen);
        valid = (ret == 0);
    }

    mbedtls_pk_free(&pk);
    return valid;
}

}  // namespace OtaImageVerifier
