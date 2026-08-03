# Security Review — Rheinturm Firmware

Date: 2026-06-27
Scope: BLE configuration interface and WiFi OTA update path (`src/BLEConfigInterface.cpp`).
Reviewer: security review of the OTA/BLE trust boundary, mapped to OWASP Top 10:2025.

This review was prompted by a companion review of the Flutter app. The app code is sound — it
validates the OTA download URL against an HTTPS + `github.com` host allowlist before forwarding
it over BLE. That control is only meaningful if the device honours the same trust assumptions.
It does not. The findings below all live in the firmware.

---

## Finding 1 — OTA firmware download skips TLS certificate verification (HIGH)

**Location:** `src/BLEConfigInterface.cpp:467` (redirect resolver) and `:484` (firmware
download), both calling `WiFiClientSecure::setInsecure()`.

**OWASP:** A08 Software or Data Integrity Failures (primary); A04 Cryptographic Failures; A02
Security Misconfiguration.

**Description:** `_performOta()` opens both the redirect-resolution request and the actual
firmware download with `setInsecure()`, which disables X.509 certificate validation. The TLS
connection is encrypted but unauthenticated — the device will accept a firmware image from any
host that can answer for the URL, regardless of certificate. The app's HTTPS + `github.com`
allowlist provides no protection here, because a network attacker controls which host actually
answers, not the URL string.

**Exploit scenario:** The device fetches OTA over the same WiFi it was just provisioned onto. An
attacker on that LAN (rogue AP, ARP/DNS spoofing, or a malicious router) intercepts the request
to `github.com`/`objects.githubusercontent.com` and serves a crafted firmware binary. With
certificate validation disabled, `httpUpdate.update()` flashes it. Result: **arbitrary code
execution on the device**, persistent across reboot. The OTA can be triggered by any BLE peer
(see Finding 2), so the attacker does not need the legitimate app.

**Recommendation (implemented in this branch):** Replace both `setInsecure()` calls with
root-CA-bundle validation (`setCACertBundle()`), embedding the Mozilla root store via
`platformio.ini`. A bundle rather than a pinned leaf is required because the download redirects
`github.com` → `objects.githubusercontent.com` (Fastly), which chains to a different root, and
because leaf certificates rotate. Fail closed if validation fails.

---

## Finding 2 — BLE GATT interface has no authentication or encryption (HIGH)

**Location:** `src/BLEConfigInterface.cpp` `begin()` (`:190`–`:279`). All characteristics are
created with plain `PROPERTY_READ`/`PROPERTY_WRITE`; there is no `BLESecurity`, no bonding, no
encryption requirement, and no per-characteristic access permission.

**OWASP:** A07 Authentication Failures (primary); A06 Insecure Design; A04 Cryptographic
Failures.

**Description:** The BLE service exposes WiFi credential submission (`CONF_SSID_UUID`,
`CONF_PASSWORD_UUID`), OTA trigger (`OTA_CONTROL_UUID`), and WiFi reset (`CONF_WIFI_RESET_UUID`)
with no link-layer security. Any BLE central in radio range can connect and write these
characteristics. The trust model is implicitly "proximity = authorized", which is too weak for
credential and firmware-update operations.

**Exploit scenario:**
- *Credential theft / injection:* an attacker in BLE range submits their own SSID/password to
  redirect the device onto an attacker-controlled network, or reads back state. Because the link
  is unencrypted, a passive sniffer also captures the legitimate WiFi password in plaintext
  during normal provisioning.
- *Unauthorized OTA:* combined with Finding 1, any nearby device can stage an OTA URL and
  trigger a malicious firmware flash.

**Recommendation (implemented in this branch):** Enable a `BLESecurity` configuration with
bonding and link encryption, and require encryption on the sensitive characteristics
(`ESP_GATT_PERM_*_ENCRYPTED`). The device has no keypad, so "Just Works" pairing is used.

Be precise about what this buys: it **eliminates passive eavesdropping** (the WiFi password no
longer transits in plaintext) and raises the bar for unauthorized access from a silent GATT
write to an **active, visible pairing step**. It does **not** authenticate the peer — with
`ESP_IO_CAP_NONE` the device accepts any pairing request, so a deliberate attacker in range can
still pair, and an active MITM during the initial pairing handshake is not defeated. Genuine
peer authentication requires the passkey-display upgrade in the hardening note below.

---

## Finding 3 — No firmware image signing / secure boot (MEDIUM)

**Location:** `src/BLEConfigInterface.cpp` `_performOta()` / `_downloadFlashAndHash()`;
`include/OtaSigningKey.h`; `src/OtaImageVerifier.cpp`.

**OWASP:** A08 Software or Data Integrity Failures.

**Description:** Firmware authenticity previously rested entirely on the transport. Even with
Finding 1 fixed, TLS to GitHub only provides authenticity in transit — a future TLS regression, a
compromised release asset, or a compromised build/release pipeline could still deliver an
unverified image with nothing left to stop it from being flashed.

**Fix (implemented in this branch, issue #17):** The device now verifies an RSA-2048
RSASSA-PKCS1-v1.5/SHA-256 signature over the firmware image before activating it, independent of
the transport TLS check. The release pipeline (`.github/workflows/release.yml`,
`scripts/sign_firmware.py`) signs `firmware.bin` with a private key held only as the
`OTA_SIGNING_PRIVATE_KEY` GitHub Actions secret, and publishes the signature as a
`firmware.bin.sig` release asset. The device derives the signature URL from the firmware URL
(`<url>.sig`) — no BLE protocol or app change was needed.

`_downloadFlashAndHash` streams the firmware image in a single HTTP GET, feeding each chunk to
both `Update.write()` (the inactive OTA partition) and a running SHA-256 hash — the exact bytes
written to flash are the exact bytes verified, with no window for the two to diverge. Only after
`OtaImageVerifier::verify()` (mbedtls) passes does the code call `Update.end()`, which marks the
partition bootable; on any mismatch `Update.abort()` runs instead and the previous firmware stays
active. An earlier version of this fix hashed the image via a *separate* HTTP GET from the one
that flashed it (effectively via `httpUpdate.update()`) — a design with a TOCTOU gap between the
verified and flashed bytes across two independent downloads. That was caught in review and fixed
before merge; the single-fetch design above closes it. See `test/test_ota_image_verifier` for a
reproduction that signs/tampers/verifies against the real verification code.

ESP32 Secure Boot v2 + Flash Encryption (hardware root of trust) remains a possible future
upgrade but is intentionally not adopted here: it requires burning eFuses (irreversible) with
real bricking risk, which the signed-image approach avoids while still closing the gap.

---

## Optional hardening note — MITM-resistant pairing

The "Just Works" pairing in Finding 2's fix does not authenticate the peer during pairing. The
tower's LED matrix can render digits, so a future upgrade can display a 6-digit passkey and use
`ESP_IO_CAP_DISPLAY_ONLY` for MITM-protected bonding. Documented here; not in scope for this
change.

---

## Status

| # | Finding | Severity | This branch |
|---|---------|----------|-------------|
| 1 | OTA TLS verification disabled | HIGH | Fixed |
| 2 | No BLE authentication/encryption | HIGH | Mitigated (link encryption + bonding; no peer authentication — see Finding 2) |
| 3 | No firmware image signing / secure boot | MEDIUM | Fixed (RSA-2048 signed OTA images, issue #17) |
