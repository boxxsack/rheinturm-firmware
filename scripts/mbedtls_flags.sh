#!/usr/bin/env bash
# Locates a host-installed mbedtls for the PlatformIO `native` test environment
# (test/test_ota_image_verifier) and prints the compiler flags for it.
#
# macOS: brew install mbedtls
# Debian/Ubuntu: apt install libmbedtls-dev
set -euo pipefail

if command -v brew >/dev/null 2>&1 && brew --prefix mbedtls >/dev/null 2>&1; then
    prefix="$(brew --prefix mbedtls)"
    echo "-I${prefix}/include -L${prefix}/lib -lmbedtls -lmbedx509 -lmbedcrypto"
elif [ -f /usr/include/mbedtls/pk.h ]; then
    echo "-lmbedtls -lmbedx509 -lmbedcrypto"
else
    echo "error: mbedtls not found. Install it: 'brew install mbedtls' (macOS) or 'apt install libmbedtls-dev' (Linux)." >&2
    exit 1
fi
