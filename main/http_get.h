#pragma once

/* A single request header (name/value). */
typedef struct {
    const char *name;
    const char *value;
} http_header_t;

/*
 * HTTPS GET `url` with optional request headers, validating the server cert with
 * the bundled CA roots (esp_crt_bundle). Response body (truncated to out_size-1)
 * is written NUL-terminated into `out`. Logs the status line.
 * Returns the HTTP status code, or -1 on a transport/TLS error.
 */
int https_get(const char *url, const http_header_t *headers, int nheaders,
              char *out, int out_size);
