#include "ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

static const char* UPDATE_CHECK_URL =
    "https://api.github.com/repos/harryhcs/glowplug-timer/releases/latest";

static const int DASH_LIGHT = D3;
static const unsigned long OTA_BLINK_INTERVAL_MS = 250;

bool otaFetchLatest(String& tagOut, String& binUrlOut) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, UPDATE_CHECK_URL)) {
    return false;
  }
  http.useHTTP10(true);
  http.addHeader("User-Agent", "glowplug-timer");

  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  // Filter keeps only the fields we need — release JSON can be 10KB+.
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    return false;
  }

  const char* tag = doc["tag_name"];
  if (!tag) {
    return false;
  }
  String tagStr(tag);
  if (tagStr.startsWith("v")) {
    tagStr.remove(0, 1);
  }

  // Prefer an asset whose name ends in .bin; first match wins.
  String binUrl;
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    const char* name = asset["name"];
    const char* url = asset["browser_download_url"];
    if (!name || !url) continue;
    String n(name);
    if (n.endsWith(".bin")) {
      binUrl = url;
      break;
    }
  }
  if (binUrl.length() == 0) {
    return false;
  }

  tagOut = tagStr;
  binUrlOut = binUrl;
  return true;
}

bool otaIsNewer(const String& latest, const String& current) {
  int la = 0, lb = 0, lc = 0;
  int ca = 0, cb = 0, cc = 0;
  sscanf(latest.c_str(), "%d.%d.%d", &la, &lb, &lc);
  sscanf(current.c_str(), "%d.%d.%d", &ca, &cb, &cc);
  if (la != ca) return la > ca;
  if (lb != cb) return lb > cb;
  return lc > cc;
}

bool otaApplyUpdate(const String& binUrl, String& errOut) {
  WiFiClientSecure client;
  client.setInsecure();

  httpUpdate.onProgress([](int cur, int total) {
    bool on = (millis() / OTA_BLINK_INTERVAL_MS) % 2 == 0;
    digitalWrite(DASH_LIGHT, on ? HIGH : LOW);
  });
  // GitHub release downloads 302 to objects.githubusercontent.com — follow them.
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  t_httpUpdate_return ret = httpUpdate.update(client, binUrl);
  if (ret == HTTP_UPDATE_OK) {
    errOut = "";
    return true;
  }
  errOut = String(httpUpdate.getLastError()) + " " + httpUpdate.getLastErrorString();
  return false;
}
