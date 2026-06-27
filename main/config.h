#pragma once
#include <stdbool.h>

/* Persisted device configuration (P6). Lives in NVS namespace "wcfg".
 * Non-sensitive fields are stored plaintext; wifi_pass + token live in a single
 * AES-256-GCM blob (see secure_store.h). */

#define CFG_SSID_MAX   33
#define CFG_PASS_MAX   64
#define CFG_TOKEN_MAX  300
#define CFG_VER_MAX    32

typedef struct {
    char ssid[CFG_SSID_MAX];
    char wifi_pass[CFG_PASS_MAX];
    char token[CFG_TOKEN_MAX];     /* Claude OAuth setup-token */
    char version[CFG_VER_MAX];     /* claude --version, for the User-Agent header */
    int  poll_s;                   /* poll interval, seconds */
    int  brightness;               /* LCD brightness percent, 5-100 */
} config_t;

/* Load config from NVS, decrypting the secret blob. Returns true if the device
 * has been provisioned and the secrets decrypted cleanly; false otherwise
 * (first boot, or a corrupt/foreign blob -> caller should (re)provision). */
bool config_load(config_t *out);

/* Encrypt + persist the given config to NVS and mark the device provisioned.
 * Returns true on success. */
bool config_save(const config_t *cfg);

/* Erase the config namespace (factory reset -> next boot reprovisions). */
void config_factory_reset(void);

/* Persist ONLY the LCD brightness (NVS 'bri'), clamped 5-100. Deliberately does
 * not touch the encrypted secret blob (unlike config_save), so the on-device
 * brightness slider can save cheaply without re-encrypting the token. */
void config_set_brightness(int brightness);
