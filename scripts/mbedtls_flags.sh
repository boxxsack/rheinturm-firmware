#!/usr/bin/env bash
# Locates a host-installed mbedtls for the PlatformIO `native` test environment
# (test/test_ota_image_verifier) and prints the compiler flags for it.
#
# macOS: brew install mbedtls
# Debian/Ubuntu: apt install libmbedtls-dev
set -euo pipefail

error_message="error: mbedtls not found. Install it: 'brew install mbedtls' (macOS) or 'apt install libmbedtls-dev' (Linux)."

has_mbedtls_files() {
    local include_dir="$1"
    local lib_dir="$2"
    local library

    [ -f "$include_dir/mbedtls/pk.h" ] || return 1
    for library in mbedtls mbedx509 mbedcrypto; do
        [ -f "$lib_dir/lib${library}.a" ] ||
            [ -f "$lib_dir/lib${library}.so" ] ||
            [ -f "$lib_dir/lib${library}.dylib" ] || return 1
    done
}

if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists mbedtls mbedx509 mbedcrypto; then
    include_dir="$(pkg-config --variable=includedir mbedtls)"
    lib_dir="$(pkg-config --variable=libdir mbedtls)"
    if has_mbedtls_files "$include_dir" "$lib_dir"; then
        # mbedtls.pc only lists "-lmbedtls" in its public Libs field; mbedcrypto
        # and mbedx509 (needed for the mbedtls_pk_* symbols we call) are only in
        # Requires.private, so all three modules must be queried explicitly.
        pkg-config --cflags --libs mbedtls mbedx509 mbedcrypto
        exit 0
    fi
fi

if command -v brew >/dev/null 2>&1; then
    prefix="$(brew --prefix mbedtls 2>/dev/null || true)"
    if [ -n "$prefix" ] && has_mbedtls_files "$prefix/include" "$prefix/lib"; then
        echo "-I${prefix}/include -L${prefix}/lib -lmbedtls -lmbedx509 -lmbedcrypto"
        exit 0
    fi
fi

# Debian/Ubuntu's libmbedtls-dev does not ship pkg-config .pc files on all
# releases (e.g. Debian bookworm, Ubuntu 22.04); fall back to the default
# system include/lib search path used by apt packages.
if [ -f /usr/include/mbedtls/pk.h ]; then
    echo "-lmbedtls -lmbedx509 -lmbedcrypto"
    exit 0
fi

echo "$error_message" >&2
exit 1
