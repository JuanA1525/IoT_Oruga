// ...existing code...
#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <mbedtls/aes.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include "LoRaBoards.h"
 
// Opcional: fuerza que el build sea para ESP32 (T-Beam)
#if !defined(ESP32)
#error "Este receptor está pensado para LilyGO T-Beam (ESP32)."
#endif
 
// Configuración de radio (ajusta a tu región: 868.0 / 915.0, etc.)
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           910.0   // MHz
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   17      // dBm
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             125.0   // kHz
#endif
 
static bool beginLoRa() {
  // Pines definidos por LoRaBoards.h para el T-Beam
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN, RADIO_CS_PIN);
  LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);
 
#ifdef RADIO_TCXO_ENABLE
  pinMode(RADIO_TCXO_ENABLE, OUTPUT);
  digitalWrite(RADIO_TCXO_ENABLE, HIGH);
  delay(10);
#endif
 
  if (!LoRa.begin((long)(CONFIG_RADIO_FREQ * 1000000))) {
    Serial.println("LoRa init failed. Check wiring/pins.");
    return false;
  }
 
  // Parámetros básicos (ajusta si tu emisor usa otros)
  LoRa.setTxPower(CONFIG_RADIO_OUTPUT_POWER);
  LoRa.setSignalBandwidth(CONFIG_RADIO_BW * 1000);
  LoRa.setSpreadingFactor(7);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  LoRa.receive(); // modo RX continuo
 
  Serial.print("LoRa RX listo @ ");
  Serial.print(CONFIG_RADIO_FREQ, 1);
  Serial.println(" MHz");
  return true;
}
 
// === AES-256-CTR decryption matching TX format ===
// Must match the TX key in src/main.cpp exactly (32 bytes)
static const uint8_t AES256_KEY[32] = {
  0x60,0x3d,0xeb,0x10,0x15,0xca,0x71,0xbe,
  0x2b,0x73,0xae,0xf0,0x85,0x7d,0x77,0x81,
  0x1f,0x35,0x2c,0x07,0x3b,0x61,0x08,0xd7,
  0x2d,0x98,0x10,0xa3,0x09,0x14,0xdf,0xf4
};
 
// Packet layout:
// [0]=0x01, [1..4]=seq (BE), [5..16]=IV(12), [17..18]=len (BE), [19..]=ciphertext
static bool decryptPacket(const uint8_t* pkt, size_t pktLen, String& jsonOut, uint32_t &seqOut)
{
  if (pktLen < 19) return false;
  if (pkt[0] != 0x01) return false; // version
 
  uint32_t seq = ((uint32_t)pkt[1] << 24) | ((uint32_t)pkt[2] << 16) | ((uint32_t)pkt[3] << 8) | pkt[4];
  uint16_t len = ((uint16_t)pkt[17] << 8) | pkt[18];
  if ((size_t)19 + len > pktLen) return false;
 
  const uint8_t *iv = &pkt[5];
  const uint8_t *ct = &pkt[19];
 
  uint8_t nonce_counter[16];
  memcpy(nonce_counter, iv, 12);
  nonce_counter[12] = pkt[1];
  nonce_counter[13] = pkt[2];
  nonce_counter[14] = pkt[3];
  nonce_counter[15] = pkt[4];
 
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, AES256_KEY, 256);
 
  uint8_t stream_block[16];
  size_t nc_off = 0;
  std::unique_ptr<uint8_t[]> out(new uint8_t[len]);
 
  int rc = mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, nonce_counter, stream_block, ct, out.get());
  mbedtls_aes_free(&ctx);
  if (rc != 0) return false;
 
  jsonOut = String((const char*)out.get(), len);
  seqOut = seq;
  return true;
}
 
// ==== Cloud agent encrypted upload (AES-256-GCM with token-derived key) ====
// Fill these with your Wi-Fi and API endpoint
static const char* WIFI_SSID = "JohanCelular";
static const char* WIFI_PASS = "contraseñaSegura1";
static const char* API_URL   = "http://54.81.22.123:5000/api/ingest"; // use http or https
 
// Token passphrase used to derive the AES-256 key (must match server)
static const char* AGENT_TOKEN = "Benchopo2025";
 
static void wifiEnsureConnected()
{
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("[NET] Connecting WiFi "); Serial.print(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[NET] WiFi OK, IP="); Serial.println(WiFi.localIP());
  } else {
    Serial.println("[NET] WiFi failed");
  }
}
 
// Derive 32-byte key = SHA-256(token)
static void deriveKeyFromToken(uint8_t outKey[32])
{
  mbedtls_md_context_t ctx;
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, info, 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, reinterpret_cast<const unsigned char*>(AGENT_TOKEN), strlen(AGENT_TOKEN));
  mbedtls_md_finish(&ctx, outKey);
  mbedtls_md_free(&ctx);
}
 
// Base64 helper using mbedTLS
static String b64encode(const uint8_t* data, size_t len)
{
  size_t outLen = 0;
  // Calculate needed size (4/3 growth + padding)
  size_t cap = ((len + 2) / 3) * 4 + 4;
  std::unique_ptr<unsigned char[]> out(new unsigned char[cap]);
  if (mbedtls_base64_encode(out.get(), cap, &outLen, data, len) != 0) return String("");
  return String(reinterpret_cast<char*>(out.get()), outLen);
}
 
// Encrypt plaintext JSON for the agent using AES-256-GCM
// Output is a compact JSON: {"v":1,"iv":"...","tag":"...","ct":"..."}
static bool encryptForAgent(const String& plaintextJson, String& outBody)
{
  uint8_t key[32];
  deriveKeyFromToken(key);
 
  // Random 12-byte IV/nonce
  uint8_t iv[12];
  for (int i = 0; i < 12; ++i) iv[i] = (uint8_t)(esp_random() & 0xFF);
 
  const uint8_t* pt = reinterpret_cast<const uint8_t*>(plaintextJson.c_str());
  size_t ptLen = plaintextJson.length();
  std::unique_ptr<uint8_t[]> ct(new uint8_t[ptLen]);
  uint8_t tag[16];
 
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (rc == 0) rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, ptLen, iv, sizeof(iv),
                                              nullptr, 0, pt, ct.get(), sizeof(tag), tag);
  mbedtls_gcm_free(&gcm);
  if (rc != 0) return false;
 
  String ivB64 = b64encode(iv, sizeof(iv));
  String ctB64 = b64encode(ct.get(), ptLen);
  String tagB64 = b64encode(tag, sizeof(tag));
  if (ivB64.length() == 0 || ctB64.length() == 0 || tagB64.length() == 0) return false;
 
  outBody.reserve(ivB64.length() + ctB64.length() + tagB64.length() + 40);
  outBody = String("{") +
            "\"v\":1,\"iv\":\"" + ivB64 + "\",\"tag\":\"" + tagB64 +
            "\",\"ct\":\"" + ctB64 + "\"}";
  return true;
}
 
static void postToAgent(const String& body)
{
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NET] Skip POST, WiFi not connected");
    return;
  }
  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  Serial.print("[NET] POST "); Serial.print(API_URL); Serial.print(" -> "); Serial.println(code);
  if (code > 0) {
    String resp = http.getString();
    Serial.print("[NET] Resp: "); Serial.println(resp);
  }
  http.end();
}
 
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Serial.println("\nLoRa RX simple | Imprimir paquetes por Serial");
 
  if (!beginLoRa()) {
    Serial.println("Fallo al iniciar LoRa.");
  }
 
  // Connect WiFi (optional; fill credentials first)
  if (strlen(WIFI_SSID) > 0 && WIFI_SSID[0] != '<') {
    wifiEnsureConnected();
  } else {
    Serial.println("[NET] WiFi credentials not set; cloud POST disabled");
  }
}
 
void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize > 0) {
    // Lee el paquete
    uint8_t buf[256];
    int len = 0;
    while (LoRa.available() && len < (int)sizeof(buf)) {
      buf[len++] = (uint8_t)LoRa.read();
    }
    // Drena cualquier byte extra si el paquete supera 256B
    while (LoRa.available()) { LoRa.read(); }
 
    // Info del paquete
    int rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();
 
    Serial.print("Paquete LoRa | len=");
    Serial.print(len);
    Serial.print(" RSSI=");
    Serial.print(rssi);
    Serial.print(" dBm SNR=");
    Serial.print(snr, 1);
    Serial.println(" dB");
 
    // HEX
    Serial.print("HEX: ");
    for (int i = 0; i < len; ++i) {
      if (buf[i] < 16) Serial.print('0');
      Serial.print(buf[i], HEX);
      if (i + 1 < len) Serial.print(' ');
    }
    Serial.println();
 
    // ASCII
    Serial.print("ASCII: ");
    for (int i = 0; i < len; ++i) {
      char c = (char)buf[i];
      if (c >= 32 && c <= 126) {
        Serial.print(c);
      } else {
        Serial.print('.');
      }
    }
    Serial.println();
    Serial.println();
 
    // Try to decrypt as encrypted JSON packet
    String json;
    uint32_t seq = 0;
    if (decryptPacket(buf, len, json, seq)) {
      Serial.print("DEC | seq=");
      Serial.print(seq);
      Serial.print(" json_len=");
      Serial.print(json.length());
      Serial.print(" -> ");
      Serial.println(json);
      Serial.println();
 
      // Encrypt for cloud agent and POST
      String encBody;
      if (encryptForAgent(json, encBody)) {
        postToAgent(encBody);
      } else {
        Serial.println("[NET] Agent encryption failed");
      }
    }
  }
 
  // Pequeño respiro para la CPU
  delay(2);
}
// ...existing code...