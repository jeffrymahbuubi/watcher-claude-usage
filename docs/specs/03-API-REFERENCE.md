# 03 — API Reference (Anthropic Data Sources)

> The easy-to-get-wrong external details, isolated. All facts here come from the research phase
> (reference-project source + community reverse-engineering + Anthropic docs).
> **Confidence is flagged per item.** **Last updated:** 2026-06-24 · Parent: [SPEC.md](SPEC.md)

## 1. Authentication
- **Credential:** Claude Code **OAuth token**, obtained by the user via `claude setup-token`.
- **Usage:** sent as `Authorization: Bearer <token>`.
- **Required headers** (omitting these causes failures / aggressive rate-limiting):
  - `Authorization: Bearer <oauth-token>`
  - `anthropic-beta: oauth-2025-04-20`
  - `User-Agent: claude-code/<version>`  ← **critical**: without a Claude-Code UA you hit a heavily
    rate-limited bucket (429s). (Confidence: high.)

## 2. Primary endpoint — `GET https://api.anthropic.com/api/oauth/usage`
- **Method:** GET, headers as §1. (This is the same endpoint Claude Code's own `/usage` command uses.)
- **Response schema (reverse-engineered, medium confidence):**
  ```json
  {
    "five_hour":        { "utilization": 0-100, "resets_at": "<ISO8601>" },
    "seven_day":        { "utilization": 0-100, "resets_at": "<ISO8601>" },
    "seven_day_opus":   null | { "utilization": 0-100, "resets_at": "<ISO8601>" },
    "seven_day_sonnet": null | { "utilization": 0-100, "resets_at": "<ISO8601>" },
    "extra_usage":      { "is_enabled": bool, "monthly_limit": null|num,
                          "used_credits": null|num, "utilization": null|num }
  }
  ```
- **Fields used by this project:** `five_hour`, `seven_day`, `seven_day_opus`, `seven_day_sonnet`
  (utilization % + `resets_at`).
- **⚠️ `extra_usage.used_credits` is NOT spend** — it is the opt-in overage-credit allowance, not a
  cost figure. Do **not** display it as "cost." (Confidence: medium on exact semantics.)
- **No token counts, no $ cost** are present in this response. (Confidence: high.)
- **⚠️ Access requirement (empirical, 2026-06-24 — HIGH confidence, tested on-device):** with a
  **`claude setup-token`** credential this endpoint returns **HTTP 403** `permission_error`:
  *"OAuth token does not meet scope requirement `user:profile`"*. The setup-token carries inference
  scope only; the rich endpoint needs the **full interactive-login** OAuth token (what Claude Code's own
  `/usage` uses), which expires and needs refresh. **On-device with only a setup-token → use the §3
  `/v1/messages` fallback.** (See Decision D11.)

## 3. Fallback — `/v1/messages` rate-limit headers
The on-device approach used by `claude-usage-stick`'s firmware, kept as a fallback:
- **Probe call:** `POST https://api.anthropic.com/v1/messages` with `"max_tokens": 1` and a cheap
  model (e.g. `claude-haiku-4-5-20251001`) — a deliberately near-free request.
- **Read response headers:**
  - `anthropic-ratelimit-unified-5h-utilization` (0.0–1.0 → ×100 for %)
  - `anthropic-ratelimit-unified-5h-reset` (epoch)
  - `anthropic-ratelimit-unified-7d-utilization`, `anthropic-ratelimit-unified-7d-reset`
- **Trade-off:** each poll consumes a tiny bit of quota; gives only 5h/7d (no per-model buckets).
- **✅ NOW THE PRIMARY source** (oauth/usage is unusable with a setup-token — D11). **Confirmed on-device 2026-06-24** (HTTP 200, OAuth setup-token, `max_tokens:1` haiku probe). Full response header set:
  - `anthropic-ratelimit-unified-5h-utilization` / `-7d-utilization` — **0.0–1.0** (×100 for %)
  - `anthropic-ratelimit-unified-5h-reset` / `-7d-reset` — **epoch SECONDS**
  - `anthropic-ratelimit-unified-5h-status` / `-7d-status` / `-status` — `allowed` | (warning?) | `rejected`
  - `anthropic-ratelimit-unified-representative-claim` (e.g. `five_hour`), `-fallback-percentage`, `-reset`
  - `anthropic-ratelimit-unified-overage-status`, `-overage-disabled-reason`
  - Example seen: 5h util `0.23`, 7d util `0.08`. → we get **5h/7d util% + reset + per-window status** (no per-model).

## 4. Service status — `status.claude.com`
- `GET https://status.claude.com/api/v2/incidents/unresolved.json` → current unresolved incidents.
- (Also `.../api/v2/components.json` / `summary.json` for per-component health.)
- No auth required. Used for FR4 (service/model status). (Confidence: high — used by the stick.)

## 5. Polling & Rate-Limit Strategy
- Default interval: **conservative** (e.g. 60–120 s); make it FR8-configurable.
- On `429`: exponential backoff, lengthen interval, surface a brief "rate-limited" state.
- `/api/oauth/usage` is a read endpoint (no token spend like the probe), so it is the cheaper poll.

## 6. Deliberately NOT used (documented so we don't revisit)
| Source | Why excluded |
|--------|--------------|
| **Admin Usage & Cost API** (`/v1/organizations/usage_report/messages`, `/cost_report`) | Needs an **org Admin API key** ("unavailable for individual accounts"); reflects API-console usage, not subscription Claude Code; daily-aggregated. |
| **Claude Code Analytics API** (`/v1/organizations/usage_report/claude_code`) | Has tokens + `estimated_cost`, but **still needs an Admin API key + org**; not callable with the OAuth token. |
| **ccusage / local `~/.claude` JSONL** | Accurate tokens/$ but **requires a host PC** to read the files — breaks standalone scope. |

→ These are exactly why **tokens + $ cost are out of scope** (Decision D3).

## 7. Sources
- `oauramos/claude-usage-stick`: `src/api.cpp`, `src/config.h`, `src/status.cpp`, `server/usage_proxy.py`
- `/api/oauth/usage` schema (community RE): https://github.com/Maciek-roboblog/Claude-Code-Usage-Monitor/issues/202
- Usage & Cost Admin API: https://platform.claude.com/docs/en/manage-claude/usage-cost-api
- Claude Code Analytics API: https://platform.claude.com/docs/en/manage-claude/claude-code-analytics-api
