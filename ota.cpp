#include "ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static const char* UPDATE_CHECK_URL =
    "https://api.github.com/repos/harryhcs/glowplug-timer/releases/latest";

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
