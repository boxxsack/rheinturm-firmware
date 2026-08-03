#!/usr/bin/env python3
"""Signs firmware.bin for OTA image-integrity verification (issue #17).

Produces a 256-byte RSA-2048 RSASSA-PKCS1-v1.5/SHA-256 signature over the raw
firmware.bin bytes, matching the scheme verified on-device by
OtaImageVerifier::verify() (src/OtaImageVerifier.cpp). The device fetches this
file from "<firmware-url>.sig" alongside the firmware.bin release asset.

Usage:
    python3 scripts/sign_firmware.py <private_key.pem> <firmware.bin> <output.sig>

The private key is never committed to this repo; the release workflow reads
it from the OTA_SIGNING_PRIVATE_KEY GitHub Actions secret. The matching
public key is embedded in firmware at include/OtaSigningKey.h.
"""
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding


def main() -> int:
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} <private_key.pem> <firmware.bin> <output.sig>", file=sys.stderr)
        return 2

    key_path, firmware_path, sig_path = sys.argv[1:4]

    with open(key_path, "rb") as f:
        private_key = serialization.load_pem_private_key(f.read(), password=None)

    with open(firmware_path, "rb") as f:
        firmware_bytes = f.read()

    signature = private_key.sign(firmware_bytes, padding.PKCS1v15(), hashes.SHA256())

    if len(signature) != 256:
        print(f"error: expected a 256-byte RSA-2048 signature, got {len(signature)} bytes "
              f"(is the key RSA-2048?)", file=sys.stderr)
        return 1

    with open(sig_path, "wb") as f:
        f.write(signature)

    print(f"Signed {firmware_path} ({len(firmware_bytes)} bytes) -> {sig_path} ({len(signature)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
