# CursorAgent

Small **Arduino / ESP32** library for the **[Cursor Cloud Agents API](https://cursor.com/docs/cloud-agent/api/endpoints)** (`v1`, public beta). It starts a **no-repo** cloud agent with your prompt, polls the run until it ends, hands you the agent's final reply, and archives (or deletes) the agent afterwards.

Cloud agents run on Cursor-hosted VMs with internet access, so a prompt can ask the agent to read a web page — something a plain chat-completion API cannot do.

## Requirements

- **Board:** ESP32 (`library.properties` targets `esp32`)
- **Core:** Arduino-ESP32 with `WiFi`, `HTTPClient`, `WiFiClientSecure`
- **Library:** [ArduinoJson](https://arduinojson.org/) 6.x or 7.x
- **Cursor plan with Cloud Agents**, and an API key from [Cursor Dashboard → API Keys](https://cursor.com/dashboard) (user key or service-account key)

Connect Wi-Fi before calling any method; methods return `false` if Wi-Fi is not connected.

## Install

1. Clone or copy this folder into your Arduino `libraries` directory (the folder must contain `library.properties` and `src/`), or symlink it:

   ```bash
   ln -s /path/to/arduino-cursor-agent ~/Arduino/libraries/arduino-cursor-agent
   ```

2. Open **File → Examples → CursorAgent → SummarizeUrl**.

## Usage

```cpp
#include <CursorAgent.h>

CursorAgent agent("your-cursor-api-key");

char reply[1024];
agent.setModel("composer-2");
if (agent.prompt("Read https://example.com and summarize it in one sentence.", reply, sizeof(reply))) {
  Serial.println(reply);
} else {
  Serial.println(agent.lastError());
}
```

`prompt()` blocks for the whole run: one VM start plus the agent's work, typically tens of seconds to a few minutes.

## API

| Method | HTTP | Notes |
|--------|------|-------|
| `prompt(text, out, outCap)` | all of the below | Create, poll, clean up. `true` only if the run `FINISHED` with a non-empty reply. |
| `createAgent(text, agentId, cap, runId, cap)` | `POST /v1/agents` | No `repos`/`env` → no-repo agent. Id buffers need `CursorAgent::kIdCap` bytes. |
| `getRun(agentId, runId, &status, result, cap)` | `GET /v1/agents/{id}/runs/{runId}` | `result` is filled once the run is terminal. |
| `cancelRun(agentId, runId)` | `POST …/runs/{runId}/cancel` | |
| `archiveAgent(agentId)` | `POST /v1/agents/{id}/archive` | Reversible. |
| `deleteAgent(agentId)` | `DELETE /v1/agents/{id}` | Permanent. |

Settings:

| Setter | Default | Notes |
|--------|---------|-------|
| `setModel(id)` | account default | Ids from `GET /v1/models`, e.g. `composer-2`. |
| `setPollInterval(ms)` | 10 000 | API default rate limit is 20 requests/minute. |
| `setRunTimeout(ms)` | 300 000 | On timeout the run is cancelled. |
| `setCleanup(mode)` | `Archive` | `None`, `Archive`, or `Delete`. |
| `setCACert(pem)` | none | Without it, TLS certificates are **not** validated. |

String pointers passed to the constructor and setters are stored, not copied — keep them alive (string literals or globals).

On failure, `lastError()` returns a short reason (including the start of the error response body for HTTP errors) and `lastHttpCode()` the HTTP status.

## Costs and caveats

- Each `prompt()` is a full Cloud Agent run, billed as Cloud Agent usage on your Cursor plan — much more expensive than a single chat completion.
- The agent auto-runs tools on its VM. Keep prompts narrow (e.g. "Do not create or modify any files") and treat replies as untrusted text.
- If your Cloud Agents network access is set to *Allowlist only*, the agent can only reach allowlisted domains.
- The v1 API is in public beta and may change.

## Version

Current library version is declared in `library.properties` (e.g. `version=0.1.0`).
