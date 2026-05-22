#include "ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

static const char* UPDATE_CHECK_URL =
    "https://api.github.com/repos/harryhcs/glowplug-timer/releases/latest";

static const char* UPDATE_ASSET_URL_PREFIX =
    "https://github.com/harryhcs/glowplug-timer/releases/download/v";

static const int OTA_DASH_LIGHT_PIN = D3;
static const unsigned long OTA_BLINK_INTERVAL_MS = 250;

String otaFetchLatestTag() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, UPDATE_CHECK_URL)) {
    return "";
  }
  http.useHTTP10(true);
  http.addHeader("User-Agent", "glowplug-timer");

  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    http.end();
    return "";
  }

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    return "";
  }

  const char* tag = doc["tag_name"];
  if (!tag) {
    return "";
  }

  String result(tag);
  if (result.startsWith("v")) {
    result.remove(0, 1);
  }
  return result;
}

bool otaApplyUpdate(const String& tag) {
  String url(UPDATE_ASSET_URL_PREFIX);
  url += tag;
  url += "/firmware.bin";

  WiFiClientSecure client;
  client.setInsecure();

  httpUpdate.onProgress([](int /*cur*/, int /*total*/) {
    digitalWrite(OTA_DASH_LIGHT_PIN,
                 (millis() / OTA_BLINK_INTERVAL_MS) % 2 ? HIGH : LOW);
  });

  t_httpUpdate_return ret = httpUpdate.update(client, url);
  return ret == HTTP_UPDATE_OK;
}
