/**
 * SummarizeUrl — ESP32 sketch for the CursorAgent library.
 *
 * Asks a Cursor Cloud Agent to read a web page and summarize it, then prints
 * the reply. The agent runs on a Cursor-hosted VM with internet access, so it
 * fetches the page itself. A run usually takes from tens of seconds to a few minutes.
 *
 * Board: ESP32 (see library.properties).
 * Dependencies: ArduinoJson (Library Manager).
 * API key: Cursor Dashboard → API Keys (user key or service-account key).
 */

#include <WiFi.h>

#include <CursorAgent.h>

// ---------- Edit for your network / account ----------
static constexpr char WIFI_SSID[] = "your-ssid";
static constexpr char WIFI_PASS[] = "your-password";
static constexpr char CURSOR_API_KEY[] = "your-cursor-api-key";
// -----------------------------------------------------

static constexpr char PAGE_URL[] = "https://en.wikipedia.org/wiki/E_Ink";

static CursorAgent agent(CURSOR_API_KEY);
static char reply[1024];

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println(F("CursorAgent — summarize a URL"));

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print(F("WiFi connecting"));
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  agent.setModel("composer-2");
  agent.setCleanup(CursorAgentCleanup::Archive);

  char prompt[384];
  snprintf(prompt, sizeof(prompt),
           "Read the web page at %s and summarize it in at most 300 characters. "
           "Reply with only the summary as plain text. Do not create or modify any files.",
           PAGE_URL);

  Serial.println(F("Running agent..."));
  const unsigned long t0 = millis();
  if (agent.prompt(prompt, reply, sizeof(reply))) {
    Serial.print(F("Done in "));
    Serial.print((millis() - t0) / 1000);
    Serial.println(F(" s:"));
    Serial.println(reply);
  } else {
    Serial.print(F("Failed: "));
    Serial.println(agent.lastError());
  }
}

void loop() {
  delay(60000);
}
