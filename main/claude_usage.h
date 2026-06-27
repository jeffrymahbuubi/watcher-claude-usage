#pragma once
#include <stdbool.h>

/* Parsed Claude rate-limit usage snapshot. */
typedef struct {
    bool ok;             /* true if the fetch succeeded and headers were parsed */
    int  http_status;    /* HTTP status code (0 if the request never completed); 429 => rate-limited */
    int  pct_5h;         /* 5-hour window utilization, 0-100 */
    int  pct_7d;         /* 7-day window utilization, 0-100 */
    long reset_5h;       /* 5h window reset, epoch seconds */
    long reset_7d;       /* 7d window reset, epoch seconds */
    char status[16];     /* anthropic-ratelimit-unified-status, e.g. "allowed" */
} usage_t;

/*
 * Provide the OAuth token + claude-code version used for the auth headers. Call
 * once after the config is loaded/provisioned, before claude_usage_fetch().
 * Strings are copied internally.
 */
void claude_usage_set_auth(const char *token, const char *version);

/*
 * Fetch usage via a cheap POST /v1/messages probe (max_tokens:1) and read the
 * `anthropic-ratelimit-unified-*` response headers. Fills *out and returns its ok.
 * (Fallback per D6/D11: /api/oauth/usage needs the user:profile scope the
 * setup-token lacks.)
 */
bool claude_usage_fetch(usage_t *out);
