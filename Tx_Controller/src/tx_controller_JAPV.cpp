#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <esp_system.h>
#include <HTTPClient.h>
#include "ControlProtocol.h"
#include "LoRaBoards.h"

// ---------- Board selection: LilyGO T-Beam (ESP32) ----------
#if !defined(ESP32)
#error "This TX build targets the LilyGO T-Beam (ESP32)."
#endif

#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           910.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   17
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             125.0
#endif

// ---------- Cloud polling configuration ----------
#ifndef CONFIG_WIFI_STA_SSID
#define CONFIG_WIFI_STA_SSID ""
#endif
#ifndef CONFIG_WIFI_STA_PASS
#define CONFIG_WIFI_STA_PASS ""
#endif
#ifndef CONFIG_API_URL
#define CONFIG_API_URL "http://54.81.22.123:1026/v2/entities/oruga/attrs"
#endif
#ifndef CONFIG_POLL_INTERVAL_MS
#define CONFIG_POLL_INTERVAL_MS 500
#endif

// No AP / Web UI: cloud-only control

uint8_t sequenceCounter = 0;
uint8_t currentLeftSpeed = 10;
uint8_t currentRightSpeed = 10;
String lastState = "STOP";
unsigned long g_lastPollMs = 0;
uint8_t lastSentLeftSpeed = 10;
uint8_t lastSentRightSpeed = 10;
TankControl::Command lastCommandSent = TankControl::Command::Stop;
bool apiErrorActive = false;

// (Web UI removed)

TankControl::Command parseCommand(const String &action) {
  if (action == "forward") return TankControl::Command::Forward;
  if (action == "backward") return TankControl::Command::Backward;
  if (action == "left") return TankControl::Command::Left;
  if (action == "right") return TankControl::Command::Right;
  if (action == "speed") return TankControl::Command::SetSpeed;
  return TankControl::Command::Stop;
}

bool sendLoRaFrame(TankControl::Command cmd, uint8_t leftSpeed, uint8_t rightSpeed) {
  TankControl::ControlFrame frame;
  TankControl::initFrame(frame, cmd, leftSpeed, rightSpeed, sequenceCounter++);

  uint8_t encrypted[TankControl::kFrameSize];
  if (!TankControl::encryptFrame(frame, encrypted, sizeof(encrypted))) {
    Serial.println("Encrypt failed");
    return false;
  }

  LoRa.idle();
  LoRa.beginPacket();
  LoRa.write(encrypted, sizeof(encrypted));
  bool ok = LoRa.endPacket() == 1;
  LoRa.receive();

  if (ok) {
    Serial.print("TX -> cmd=");
    Serial.print(static_cast<int>(frame.command));
    Serial.print(" seq=");
    Serial.print(frame.sequence);
    Serial.print(" left=");
    Serial.print(frame.leftSpeed);
    Serial.print(" right=");
    Serial.println(frame.rightSpeed);
  } else {
    Serial.println("LoRa TX failed");
  }
  return ok;
}

void sendSpectrumTestBurst() {
  static constexpr size_t kBurstSize = 192;
  uint8_t payload[kBurstSize];
  for (size_t i = 0; i < kBurstSize; ++i) {
    payload[i] = static_cast<uint8_t>(random(0, 256));
  }

  Serial.println("Sending LoRa spectrum test burst...");
  LoRa.idle();
  LoRa.beginPacket();
  LoRa.write(payload, sizeof(payload));
  if (LoRa.endPacket() == 1) {
    Serial.print("Burst length: ");
    Serial.println(sizeof(payload));
  } else {
    Serial.println("Spectrum test burst failed to transmit");
  }
  LoRa.receive();
}

// (Web handlers removed)

bool beginLoRa() {
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN, RADIO_CS_PIN);
  LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);

#ifdef RADIO_TCXO_ENABLE
  pinMode(RADIO_TCXO_ENABLE, OUTPUT);
  digitalWrite(RADIO_TCXO_ENABLE, HIGH);
#endif

  if (!LoRa.begin(CONFIG_RADIO_FREQ * 1000000)) {
    Serial.println("LoRa init failed. Check wiring.");
    return false;
  }

  LoRa.setTxPower(CONFIG_RADIO_OUTPUT_POWER);
  LoRa.setSignalBandwidth(CONFIG_RADIO_BW * 1000);
  LoRa.setSpreadingFactor(7);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  LoRa.receive();

  Serial.println("LoRa radio ready (TX).");
  return true;
}

// ----------- Wi-Fi STA helper (optional) -----------
bool hasStaCredentials() {
  return strlen(CONFIG_WIFI_STA_SSID) > 0;
}

void beginStaIfConfigured() {
  if (!hasStaCredentials()) {
    Serial.println("STA Wi-Fi not configured (CONFIG_WIFI_STA_SSID empty). Skipping cloud polling connectivity.");
    return;
  }

  if (strlen(CONFIG_WIFI_STA_PASS) == 0) {
    WiFi.begin(CONFIG_WIFI_STA_SSID); // open network
  } else {
    WiFi.begin(CONFIG_WIFI_STA_SSID, CONFIG_WIFI_STA_PASS);
  }
  Serial.print("Connecting to STA Wi-Fi: ");
  Serial.print(CONFIG_WIFI_STA_SSID);
  // Try up to ~5 seconds
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 5000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("STA connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("STA connect timeout; will keep trying in background.");
  }
}

// ----------- Minimal JSON parsing for new API shape -----------
// Expecting:
// {
//   "estado": { "type": "String", "value": "stop", ... },
//   "left_speed": { "type": "int", "value": 10, ... },
//   "right_speed": { "type": "int", "value": 10, ... }
// }

static bool extractStringFieldValue(const String &json, const char *fieldName, String &out) {
  String key = String('"') + fieldName + '"';
  int keyPos = json.indexOf(key);
  if (keyPos < 0) return false;
  int valueKey = json.indexOf("\"value\"", keyPos);
  if (valueKey < 0) return false;
  int colon = json.indexOf(':', valueKey);
  if (colon < 0) return false;
  int firstQuote = json.indexOf('"', colon + 1);
  if (firstQuote < 0) return false;
  int secondQuote = json.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) return false;
  out = json.substring(firstQuote + 1, secondQuote);
  return true;
}

static bool extractIntFieldValue(const String &json, const char *fieldName, int &out) {
  String key = String('"') + fieldName + '"';
  int keyPos = json.indexOf(key);
  if (keyPos < 0) return false;
  int valueKey = json.indexOf("\"value\"", keyPos);
  if (valueKey < 0) return false;
  int colon = json.indexOf(':', valueKey);
  if (colon < 0) return false;
  // number may have spaces; read until non-digit/non-space/non-minus
  int i = colon + 1;
  while (i < (int)json.length() && (json[i] == ' ' || json[i] == '\t')) i++;
  int start = i;
  while (i < (int)json.length() && ((json[i] >= '0' && json[i] <= '9') || json[i] == '-')) i++;
  if (i == start) return false;
  out = String(json.substring(start, i)).toInt();
  return true;
}

static bool parseApiPayload(const String &json, String &estado, int &left, int &right) {
  String estadoTmp;
  int leftTmp = -1, rightTmp = -1;
  bool okEstado = extractStringFieldValue(json, "estado", estadoTmp);
  bool okL = extractIntFieldValue(json, "left_speed", leftTmp);
  bool okR = extractIntFieldValue(json, "right_speed", rightTmp);
  if (!okEstado || !okL || !okR) return false;
  estado = estadoTmp;
  left = leftTmp;
  right = rightTmp;
  return true;
}

void updateLastStateFromCmd(TankControl::Command cmd) {
  switch (cmd) {
    case TankControl::Command::Forward: lastState = "FORWARD"; break;
    case TankControl::Command::Backward: lastState = "BACKWARD"; break;
    case TankControl::Command::Left: lastState = "LEFT"; break;
    case TankControl::Command::Right: lastState = "RIGHT"; break;
    case TankControl::Command::Stop: lastState = "STOP"; break;
    case TankControl::Command::SetSpeed: lastState = "SPEED"; break;
    default: break;
  }
}

void pollCloudAndControlTank() {
  // poll every CONFIG_POLL_INTERVAL_MS
  unsigned long now = millis();
  if (now - g_lastPollMs < CONFIG_POLL_INTERVAL_MS) return;
  g_lastPollMs = now;

  // If STA not connected or unavailable, send STOP for safety
  if (!hasStaCredentials() || WiFi.status() != WL_CONNECTED) {
    if (!apiErrorActive) {
      Serial.println("[CLOUD] STA not connected or credentials missing → sending STOP (entering error state)");
      sendLoRaFrame(TankControl::Command::Stop, currentLeftSpeed, currentRightSpeed);
      updateLastStateFromCmd(TankControl::Command::Stop);
      lastCommandSent = TankControl::Command::Stop;
      apiErrorActive = true;
    }
    return;
  }

  HTTPClient http;
  Serial.printf("[CLOUD] GET %s\n", CONFIG_API_URL);
  http.begin(CONFIG_API_URL);
  http.setTimeout(1000); // 1s timeout to avoid long blocks
  int code = http.GET();
  Serial.printf("[CLOUD] HTTP status: %d\n", code);
  if (code == HTTP_CODE_OK) {
    apiErrorActive = false; // recovered
    String body = http.getString();
    Serial.print("[CLOUD] body: ");
    Serial.println(body);
    String estado;
    int l, r;
    if (!parseApiPayload(body, estado, l, r)) {
      // malformed payload → STOP on transition
      if (!apiErrorActive) {
        Serial.println("[CLOUD] malformed JSON → sending STOP (entering error state)");
        sendLoRaFrame(TankControl::Command::Stop, currentLeftSpeed, currentRightSpeed);
        updateLastStateFromCmd(TankControl::Command::Stop);
        lastCommandSent = TankControl::Command::Stop;
        apiErrorActive = true;
      }
      http.end();
      return;
    }
    // Normalize and clamp
    estado.toLowerCase();
    l = constrain(l, 0, 255);
    r = constrain(r, 0, 255);
    Serial.printf("[CLOUD] parsed estado='%s' left=%d right=%d\n", estado.c_str(), l, r);

    // 1) If speeds changed vs last sent, update and send SetSpeed
    if (l != lastSentLeftSpeed || r != lastSentRightSpeed) {
      currentLeftSpeed = (uint8_t)l;
      currentRightSpeed = (uint8_t)r;
      Serial.printf("[CLOUD] speeds changed → SetSpeed L=%d R=%d\n", currentLeftSpeed, currentRightSpeed);
      if (sendLoRaFrame(TankControl::Command::SetSpeed, currentLeftSpeed, currentRightSpeed)) {
        lastSentLeftSpeed = currentLeftSpeed;
        lastSentRightSpeed = currentRightSpeed;
        updateLastStateFromCmd(TankControl::Command::SetSpeed);
      }
    }

    // 2) If estado changed, send movement command
    TankControl::Command cmd = parseCommand(estado);
    if (cmd != lastCommandSent) {
      Serial.printf("[CLOUD] estado changed → send cmd=%d with L=%d R=%d\n", static_cast<int>(cmd), currentLeftSpeed, currentRightSpeed);
      if (sendLoRaFrame(cmd, currentLeftSpeed, currentRightSpeed)) {
        lastCommandSent = cmd;
        updateLastStateFromCmd(cmd);
      }
    }
  } else {
    // Any HTTP or transport error → STOP for safety only on transition
    if (!apiErrorActive) {
      Serial.println("[CLOUD] request failed → sending STOP (entering error state)");
      sendLoRaFrame(TankControl::Command::Stop, currentLeftSpeed, currentRightSpeed);
      updateLastStateFromCmd(TankControl::Command::Stop);
      lastCommandSent = TankControl::Command::Stop;
      apiErrorActive = true;
    }
  }
  http.end();
}

void setup() {
  setupBoards(/*disable_u8g2=*/true);  // keep OLED splash disabled
  Serial.begin(115200);
  delay(50);
  delay(1500); // allow PMU rails to stabilize per LoRaBoards reference implementation
  Serial.println("\nT-Beam TX | LoRa Tank Controller (Cloud-only)");

  bool radioReady = beginLoRa();
  if (!radioReady) {
    Serial.println("LoRa setup failed; reboot after checking the radio module.");
  } else {
    randomSeed(esp_random());
    sendSpectrumTestBurst();
    // Set initial speeds to 10 on the receiver
    sendLoRaFrame(TankControl::Command::SetSpeed, currentLeftSpeed, currentRightSpeed);
  }
  // STA-only Wi-Fi for cloud access
  WiFi.mode(WIFI_STA);
  beginStaIfConfigured();
}

void loop() {
  pollCloudAndControlTank();
}
