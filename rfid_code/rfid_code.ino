// RFID Record Player
// RFID scan -> ESP32-S3 -> Spotify Web API directly. No backend server.
//
// Required libraries (Library Manager):
//   - MFRC522
//   - ArduinoJson
// WiFi, HTTPClient, WiFiClientSecure and base64 are built into the ESP32 core.
//
// Credentials live in secrets.h (see secrets.example.h). Never commit that file.

#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <base64.h>
#include "secrets.h"

// --- RFID wiring ---
#define SS_PIN   10
#define RST_PIN  14
#define SCK_PIN  12
#define MISO_PIN 13
#define MOSI_PIN 11

MFRC522 mfrc522(SS_PIN, RST_PIN);

// --- Tag UID -> Spotify track URI ---
struct TagMapping {
  const char* uid;
  const char* trackUri;
};

TagMapping tagMap[] = {
  {"00639C5C", "spotify:track:68HocO7fx9z0MgDU0ZPHro"},
  // Add more records here: {"UID", "spotify:track:..."},
};
const int NUM_TAGS = sizeof(tagMap) / sizeof(tagMap[0]);

// --- Token cache ---
String accessToken = "";
unsigned long tokenExpiresAt = 0;

// --- Scan debounce ---
String lastUid = "";
unsigned long lastScanTime = 0;
const unsigned long SCAN_COOLDOWN_MS = 3000;

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
}

void setup() {
  Serial.begin(115200);

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  SPI.setFrequency(1000000);
  mfrc522.PCD_Init();

  Serial.print("RC522 ");
  mfrc522.PCD_DumpVersionToSerial();

  connectWiFi();
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  String uid = getUidString();
  unsigned long now = millis();

  // Ignore the same tag if it was just scanned
  if (uid == lastUid && (now - lastScanTime) < SCAN_COOLDOWN_MS) {
    mfrc522.PICC_HaltA();
    return;
  }
  lastUid = uid;
  lastScanTime = now;

  Serial.println("Tag scanned: " + uid);

  String trackUri = lookupTrack(uid);
  if (trackUri == "") {
    Serial.println("Unknown tag " + uid + " (add it to tagMap[])");
  } else {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    playTrack(trackUri);
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

String getUidString() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

String lookupTrack(const String& uid) {
  for (int i = 0; i < NUM_TAGS; i++) {
    if (uid == tagMap[i].uid) return String(tagMap[i].trackUri);
  }
  return "";
}

// Exchange the refresh token for a short-lived access token (cached until expiry)
String getAccessToken() {
  unsigned long now = millis();
  if (accessToken != "" && now < tokenExpiresAt) {
    return accessToken;
  }

  WiFiClientSecure client;
  client.setInsecure();  // skips certificate validation

  HTTPClient https;
  https.begin(client, "https://accounts.spotify.com/api/token");
  https.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String credentials = String(CLIENT_ID) + ":" + String(CLIENT_SECRET);
  https.addHeader("Authorization", "Basic " + base64::encode(credentials));

  String body = "grant_type=refresh_token&refresh_token=" + String(REFRESH_TOKEN);
  int httpCode = https.POST(body);

  String token = "";
  if (httpCode == 200) {
    StaticJsonDocument<1024> doc;
    deserializeJson(doc, https.getString());

    token = doc["access_token"].as<String>();
    int expiresIn = doc["expires_in"].as<int>();

    accessToken = token;
    tokenExpiresAt = now + (expiresIn - 60) * 1000UL;
    Serial.println("Access token refreshed");
  } else {
    Serial.println("Token refresh failed, HTTP " + String(httpCode));
    Serial.println(https.getString());
  }

  https.end();
  return token;
}

void playTrack(const String& trackUri) {
  String token = getAccessToken();
  if (token == "") {
    Serial.println("No access token, aborting");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.begin(client, "https://api.spotify.com/v1/me/player/play");
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + token);

  String body = "{\"uris\":[\"" + trackUri + "\"]}";
  int httpCode = https.PUT(body);

  if (httpCode == 204) {
    Serial.println("Playing: " + trackUri);
  } else if (httpCode == 404) {
    Serial.println("No active Spotify device. Open Spotify on your phone or PC first.");
  } else {
    Serial.println("Play failed, HTTP " + String(httpCode));
    Serial.println(https.getString());
  }

  https.end();
}
