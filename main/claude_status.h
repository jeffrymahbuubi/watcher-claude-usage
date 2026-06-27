#pragma once
#include <stdbool.h>

/* Claude.com service-health snapshot (FR4), from status.claude.com. */
typedef struct {
    bool ok;                 /* fetch + parse succeeded */
    char indicator[16];      /* none | minor | major | critical | maintenance */
    char description[64];    /* e.g. "All Systems Operational" */
    int  incident_count;     /* number of unresolved incidents */
    char incident[96];       /* first unresolved incident name, or "" */
} service_status_t;

/*
 * Fetch GET https://status.claude.com/api/v2/summary.json (no auth) and parse the
 * overall `status` indicator/description plus any unresolved `incidents`. Fills
 * *out and returns its ok. Cheap, does not touch the Anthropic quota.
 */
bool claude_status_fetch(service_status_t *out);
