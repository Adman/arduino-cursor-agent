/**
 * Cursor Cloud Agents API (v1) client for Arduino (ESP32).
 * Wraps https://cursor.com/docs/cloud-agent/api/endpoints — create a no-repo agent,
 * poll its run, read the final reply, clean up.
 */

#ifndef CURSOR_AGENT_H
#define CURSOR_AGENT_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

enum class CursorRunStatus : uint8_t {
  Unknown,
  Creating,
  Running,
  WaitingForBackgroundWork,
  Finished,
  Error,
  Cancelled,
  Expired,
};

/** What prompt() does with the agent once its run has ended. */
enum class CursorAgentCleanup : uint8_t {
  None,
  Archive,
  Delete,
};

class CursorAgent {
public:
  /** Agent ids look like "bc-<uuid>", run ids like "run-<uuid>". */
  static constexpr size_t kIdCap = 64;

  /**
   * apiKey: user or service-account key from Cursor Dashboard → API Keys.
   * All const char* setters keep the pointer — the string must outlive this object.
   */
  explicit CursorAgent(const char *apiKey);

  /** Model id from GET /v1/models (e.g. "composer-2"). nullptr = account default. */
  void setModel(const char *modelId);

  /** PEM root CA for api.cursor.com. nullptr (default) skips certificate validation. */
  void setCACert(const char *rootCA);

  /** Delay between run status polls in prompt(). Default 10 s (API default limit is 20 req/min). */
  void setPollInterval(uint32_t ms);

  /** Max time prompt() waits for the run to end before cancelling it. Default 5 min. */
  void setRunTimeout(uint32_t ms);

  /** Default: Archive. */
  void setCleanup(CursorAgentCleanup mode);

  /**
   * Create an agent with text as its prompt, wait for the run to end, copy the final
   * reply into out (truncated to outCap - 1 bytes), then clean up the agent.
   * Blocks for the whole run. Returns true only if the run FINISHED with a non-empty reply.
   */
  bool prompt(const char *text, char *out, size_t outCap);

  /** POST /v1/agents (no repos). agentId / runId need kIdCap bytes. */
  bool createAgent(const char *text, char *agentId, size_t agentIdCap, char *runId, size_t runIdCap);

  /**
   * GET /v1/agents/{id}/runs/{runId}. Sets *status; once the run is terminal, copies its
   * reply into result (may be empty). result may be nullptr.
   */
  bool getRun(const char *agentId, const char *runId, CursorRunStatus *status, char *result,
              size_t resultCap);

  /** POST /v1/agents/{id}/runs/{runId}/cancel */
  bool cancelRun(const char *agentId, const char *runId);

  /** POST /v1/agents/{id}/archive */
  bool archiveAgent(const char *agentId);

  /** DELETE /v1/agents/{id} */
  bool deleteAgent(const char *agentId);

  static bool isTerminal(CursorRunStatus status);
  static const char *statusName(CursorRunStatus status);

  /** HTTP status (or negative HTTPClient error) of the last request. */
  int lastHttpCode() const { return lastHttpCode_; }

  /** Human-readable reason for the last failure ("" if none). */
  const char *lastError() const { return lastError_; }

private:
  bool beginRequest(HTTPClient &http, const char *path);
  bool postEmpty(const char *path);
  bool checkResponse(HTTPClient &http, int code);
  void setError(const char *fmt, ...);
  void clearError();

  const char *apiKey_;
  const char *model_ = nullptr;
  const char *caCert_ = nullptr;
  uint32_t pollIntervalMs_ = 10000;
  uint32_t runTimeoutMs_ = 300000;
  CursorAgentCleanup cleanup_ = CursorAgentCleanup::Archive;

  WiFiClientSecure tls_;
  int lastHttpCode_ = 0;
  char lastError_[192] = "";
};

#endif
