/**
 * Cursor Cloud Agents API v1 — create, poll, cancel, archive, delete.
 */

#include "CursorAgent.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if ARDUINOJSON_VERSION_MAJOR >= 7
#define CURSOR_JSON_DOC(name, capacity) JsonDocument name
#else
#define CURSOR_JSON_DOC(name, capacity) DynamicJsonDocument name(capacity)
#endif

static constexpr char kApiBase[] = "https://api.cursor.com";
static constexpr int32_t kConnectTimeoutMs = 20000;
/** HTTPClient::setTimeout takes uint16_t ms. */
static constexpr uint16_t kIoTimeoutMs = 30000;

/** Copy src into dst, truncating on a UTF-8 character boundary. */
static void copyUtf8(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) {
    return;
  }
  size_t len = src ? strlen(src) : 0;
  if (len >= cap) {
    len = cap - 1;
    while (len > 0 && (static_cast<uint8_t>(src[len]) & 0xC0) == 0x80) {
      len--;
    }
  }
  if (len > 0) {
    memcpy(dst, src, len);
  }
  dst[len] = '\0';
}

static CursorRunStatus parseStatus(const char *s) {
  if (!s) {
    return CursorRunStatus::Unknown;
  }
  if (!strcmp(s, "CREATING")) return CursorRunStatus::Creating;
  if (!strcmp(s, "RUNNING")) return CursorRunStatus::Running;
  if (!strcmp(s, "WAITING_FOR_BACKGROUND_WORK")) return CursorRunStatus::WaitingForBackgroundWork;
  if (!strcmp(s, "FINISHED")) return CursorRunStatus::Finished;
  if (!strcmp(s, "ERROR")) return CursorRunStatus::Error;
  if (!strcmp(s, "CANCELLED")) return CursorRunStatus::Cancelled;
  if (!strcmp(s, "EXPIRED")) return CursorRunStatus::Expired;
  return CursorRunStatus::Unknown;
}

CursorAgent::CursorAgent(const char *apiKey) : apiKey_(apiKey) {}

void CursorAgent::setModel(const char *modelId) { model_ = modelId; }
void CursorAgent::setCACert(const char *rootCA) { caCert_ = rootCA; }
void CursorAgent::setPollInterval(uint32_t ms) { pollIntervalMs_ = ms; }
void CursorAgent::setRunTimeout(uint32_t ms) { runTimeoutMs_ = ms; }
void CursorAgent::setCleanup(CursorAgentCleanup mode) { cleanup_ = mode; }

bool CursorAgent::isTerminal(CursorRunStatus status) {
  return status == CursorRunStatus::Finished || status == CursorRunStatus::Error ||
         status == CursorRunStatus::Cancelled || status == CursorRunStatus::Expired;
}

const char *CursorAgent::statusName(CursorRunStatus status) {
  switch (status) {
  case CursorRunStatus::Creating: return "CREATING";
  case CursorRunStatus::Running: return "RUNNING";
  case CursorRunStatus::WaitingForBackgroundWork: return "WAITING_FOR_BACKGROUND_WORK";
  case CursorRunStatus::Finished: return "FINISHED";
  case CursorRunStatus::Error: return "ERROR";
  case CursorRunStatus::Cancelled: return "CANCELLED";
  case CursorRunStatus::Expired: return "EXPIRED";
  default: return "UNKNOWN";
  }
}

void CursorAgent::setError(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(lastError_, sizeof(lastError_), fmt, args);
  va_end(args);
}

void CursorAgent::clearError() {
  lastHttpCode_ = 0;
  lastError_[0] = '\0';
}

bool CursorAgent::beginRequest(HTTPClient &http, const char *path) {
  clearError();
  if (WiFi.status() != WL_CONNECTED) {
    setError("WiFi not connected");
    return false;
  }
  if (!apiKey_ || !apiKey_[0]) {
    setError("missing API key");
    return false;
  }

  char url[192];
  if (snprintf(url, sizeof(url), "%s%s", kApiBase, path) >= static_cast<int>(sizeof(url))) {
    setError("URL too long");
    return false;
  }

  if (caCert_) {
    tls_.setCACert(caCert_);
  } else {
    tls_.setInsecure();
  }

  http.setReuse(false);
  // api.cursor.com rejects HTTP/1.0 with 426; bodies are read via getString(), which handles chunking.
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kIoTimeoutMs);
  if (!http.begin(tls_, url)) {
    setError("HTTP begin failed");
    return false;
  }

  String auth = String("Bearer ") + apiKey_;
  http.addHeader("Authorization", auth);
  http.addHeader("Accept", "application/json");
  return true;
}

bool CursorAgent::checkResponse(HTTPClient &http, int code) {
  lastHttpCode_ = code;
  if (code >= 200 && code < 300) {
    return true;
  }
  if (code < 0) {
    setError("HTTP %d (%s)", code, HTTPClient::errorToString(code).c_str());
  } else {
    String body = http.getString();
    setError("HTTP %d: %.160s", code, body.c_str());
  }
  return false;
}

bool CursorAgent::postEmpty(const char *path) {
  HTTPClient http;
  if (!beginRequest(http, path)) {
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  const bool ok = checkResponse(http, http.POST("{}"));
  http.end();
  return ok;
}

bool CursorAgent::createAgent(const char *text, char *agentId, size_t agentIdCap, char *runId,
                              size_t runIdCap) {
  if (!text || !text[0] || !agentId || !runId || agentIdCap == 0 || runIdCap == 0) {
    setError("invalid arguments");
    return false;
  }
  agentId[0] = '\0';
  runId[0] = '\0';

  CURSOR_JSON_DOC(req, 256);
  req["prompt"]["text"] = text;
  if (model_ && model_[0]) {
    req["model"]["id"] = model_;
  }
  String body;
  serializeJson(req, body);

  HTTPClient http;
  if (!beginRequest(http, "/v1/agents")) {
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  if (!checkResponse(http, http.POST(body))) {
    http.end();
    return false;
  }

  CURSOR_JSON_DOC(filter, 128);
  filter["agent"]["id"] = true;
  filter["run"]["id"] = true;
  CURSOR_JSON_DOC(resp, 512);
  const String respBody = http.getString();
  http.end();
  const DeserializationError err =
      deserializeJson(resp, respBody, DeserializationOption::Filter(filter));
  if (err) {
    setError("create response JSON: %s", err.c_str());
    return false;
  }

  const char *aid = resp["agent"]["id"].as<const char *>();
  const char *rid = resp["run"]["id"].as<const char *>();
  if (!aid || !rid || strlen(aid) >= agentIdCap || strlen(rid) >= runIdCap) {
    setError("create response missing agent/run id");
    return false;
  }
  strcpy(agentId, aid);
  strcpy(runId, rid);
  return true;
}

bool CursorAgent::getRun(const char *agentId, const char *runId, CursorRunStatus *status, char *result,
                         size_t resultCap) {
  if (status) {
    *status = CursorRunStatus::Unknown;
  }
  if (result && resultCap > 0) {
    result[0] = '\0';
  }
  if (!agentId || !runId) {
    setError("invalid arguments");
    return false;
  }

  char path[160];
  snprintf(path, sizeof(path), "/v1/agents/%s/runs/%s", agentId, runId);

  HTTPClient http;
  if (!beginRequest(http, path)) {
    return false;
  }
  if (!checkResponse(http, http.GET())) {
    http.end();
    return false;
  }

  CURSOR_JSON_DOC(filter, 128);
  filter["status"] = true;
  filter["result"] = true;
  CURSOR_JSON_DOC(resp, 4096 + resultCap);
  const String respBody = http.getString();
  http.end();
  const DeserializationError err =
      deserializeJson(resp, respBody, DeserializationOption::Filter(filter));
  if (err) {
    setError("run response JSON: %s", err.c_str());
    return false;
  }

  const CursorRunStatus st = parseStatus(resp["status"].as<const char *>());
  if (status) {
    *status = st;
  }
  if (result && resultCap > 0 && isTerminal(st)) {
    copyUtf8(result, resultCap, resp["result"] | "");
  }
  return true;
}

bool CursorAgent::cancelRun(const char *agentId, const char *runId) {
  char path[176];
  snprintf(path, sizeof(path), "/v1/agents/%s/runs/%s/cancel", agentId, runId);
  return postEmpty(path);
}

bool CursorAgent::archiveAgent(const char *agentId) {
  char path[128];
  snprintf(path, sizeof(path), "/v1/agents/%s/archive", agentId);
  return postEmpty(path);
}

bool CursorAgent::deleteAgent(const char *agentId) {
  char path[128];
  snprintf(path, sizeof(path), "/v1/agents/%s", agentId);

  HTTPClient http;
  if (!beginRequest(http, path)) {
    return false;
  }
  const bool ok = checkResponse(http, http.sendRequest("DELETE"));
  http.end();
  return ok;
}

bool CursorAgent::prompt(const char *text, char *out, size_t outCap) {
  if (!out || outCap == 0) {
    setError("invalid arguments");
    return false;
  }
  out[0] = '\0';

  char agentId[kIdCap];
  char runId[kIdCap];
  if (!createAgent(text, agentId, sizeof(agentId), runId, sizeof(runId))) {
    return false;
  }

  const uint32_t startMs = millis();
  CursorRunStatus status = CursorRunStatus::Creating;
  bool timedOut = false;
  for (;;) {
    delay(pollIntervalMs_);
    // Failed polls (429, flaky Wi-Fi) are retried until the run timeout.
    if (getRun(agentId, runId, &status, out, outCap) && isTerminal(status)) {
      break;
    }
    if (millis() - startMs >= runTimeoutMs_) {
      timedOut = true;
      break;
    }
  }

  bool ok = false;
  char error[sizeof(lastError_)] = "";
  if (timedOut) {
    snprintf(error, sizeof(error), "run timed out after %lu ms (last status %s)",
             static_cast<unsigned long>(millis() - startMs), statusName(status));
    cancelRun(agentId, runId);
  } else if (status != CursorRunStatus::Finished) {
    snprintf(error, sizeof(error), "run ended with status %s", statusName(status));
  } else if (out[0] == '\0') {
    snprintf(error, sizeof(error), "run finished with an empty reply");
  } else {
    ok = true;
  }
  const int httpCode = lastHttpCode_;

  if (cleanup_ == CursorAgentCleanup::Archive) {
    archiveAgent(agentId);
  } else if (cleanup_ == CursorAgentCleanup::Delete) {
    deleteAgent(agentId);
  }

  lastHttpCode_ = httpCode;
  memcpy(lastError_, error, sizeof(lastError_));
  return ok;
}
