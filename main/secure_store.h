#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Lightweight at-rest obfuscation for the OAuth token + WiFi password (P6 / NFR1).
 *
 * AES-256-GCM. The key is derived on-device from SHA-256(MAC || random salt ||
 * compiled pepper); the salt is generated once at provisioning and stored with
 * the ciphertext, so the device can auto-decrypt on every boot with no PIN
 * (boot-and-go — user decision 2026-06-24).
 *
 * THREAT MODEL: this defeats casual flash-string extraction (`strings` over a
 * dump) and provides integrity (GCM tag). It does NOT defeat a determined
 * attacker who dumps THIS chip's flash and reads the firmware (MAC is readable
 * and the pepper is in the image) — that is the accepted tradeoff for a headless
 * ambient device. Real at-rest protection would need ESP32 HW flash-encryption
 * (irreversible eFuse burn) — deliberately not enabled here.
 *
 * Blob layout: [salt:16][nonce:12][tag:16][ciphertext:len].
 */

#define SECURE_SALT_LEN  16
#define SECURE_NONCE_LEN 12
#define SECURE_TAG_LEN   16
#define SECURE_OVERHEAD  (SECURE_SALT_LEN + SECURE_NONCE_LEN + SECURE_TAG_LEN)

/* Encrypt `len` bytes of `plain` into `out` (must hold len + SECURE_OVERHEAD).
 * Generates a fresh random salt + nonce. Returns true on success; *out_len set. */
bool secure_encrypt(const uint8_t *plain, size_t len, uint8_t *out, size_t out_cap, size_t *out_len);

/* Decrypt a blob produced by secure_encrypt into `out` (must hold blob_len -
 * SECURE_OVERHEAD). Returns false if the blob is malformed or the GCM tag fails
 * (tamper / wrong device). *out_len set on success. */
bool secure_decrypt(const uint8_t *blob, size_t blob_len, uint8_t *out, size_t out_cap, size_t *out_len);
