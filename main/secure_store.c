#include "secure_store.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_log.h"
#include "mbedtls/gcm.h"
#include "mbedtls/sha256.h"
#include <string.h>

static const char *TAG = "secure";

/* Compile-time pepper. Obfuscation only (see secure_store.h threat model). */
static const char PEPPER[] = "watcher-claude-usage/v1/9b0c-pepper";

/* key = SHA-256( MAC(6) || salt || PEPPER ) -> 32 bytes for AES-256. */
static bool derive_key(const uint8_t *salt, uint8_t key[32])
{
    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return false;

    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    if (mbedtls_sha256_starts(&c, 0) != 0) { mbedtls_sha256_free(&c); return false; }
    mbedtls_sha256_update(&c, mac, sizeof mac);
    mbedtls_sha256_update(&c, salt, SECURE_SALT_LEN);
    mbedtls_sha256_update(&c, (const uint8_t *)PEPPER, sizeof PEPPER - 1);
    int r = mbedtls_sha256_finish(&c, key);
    mbedtls_sha256_free(&c);
    return r == 0;
}

bool secure_encrypt(const uint8_t *plain, size_t len, uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (out_cap < len + SECURE_OVERHEAD) return false;

    uint8_t *salt  = out;
    uint8_t *nonce = out + SECURE_SALT_LEN;
    uint8_t *tag   = out + SECURE_SALT_LEN + SECURE_NONCE_LEN;
    uint8_t *ct    = out + SECURE_OVERHEAD;

    esp_fill_random(salt, SECURE_SALT_LEN);
    esp_fill_random(nonce, SECURE_NONCE_LEN);

    uint8_t key[32];
    if (!derive_key(salt, key)) return false;

    mbedtls_gcm_context g;
    mbedtls_gcm_init(&g);
    bool ok = false;
    if (mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
        mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, len, nonce, SECURE_NONCE_LEN,
                                  NULL, 0, plain, ct, SECURE_TAG_LEN, tag) == 0) {
        ok = true;
        if (out_len) *out_len = len + SECURE_OVERHEAD;
    }
    mbedtls_gcm_free(&g);
    memset(key, 0, sizeof key);
    if (!ok) ESP_LOGE(TAG, "encrypt failed");
    return ok;
}

bool secure_decrypt(const uint8_t *blob, size_t blob_len, uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (blob_len < SECURE_OVERHEAD) return false;
    size_t ct_len = blob_len - SECURE_OVERHEAD;
    if (out_cap < ct_len) return false;

    const uint8_t *salt  = blob;
    const uint8_t *nonce = blob + SECURE_SALT_LEN;
    const uint8_t *tag   = blob + SECURE_SALT_LEN + SECURE_NONCE_LEN;
    const uint8_t *ct    = blob + SECURE_OVERHEAD;

    uint8_t key[32];
    if (!derive_key(salt, key)) return false;

    mbedtls_gcm_context g;
    mbedtls_gcm_init(&g);
    bool ok = false;
    if (mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
        mbedtls_gcm_auth_decrypt(&g, ct_len, nonce, SECURE_NONCE_LEN, NULL, 0,
                                 tag, SECURE_TAG_LEN, ct, out) == 0) {
        ok = true;
        if (out_len) *out_len = ct_len;
    }
    mbedtls_gcm_free(&g);
    memset(key, 0, sizeof key);
    if (!ok) ESP_LOGW(TAG, "decrypt failed (tamper or wrong device)");
    return ok;
}
