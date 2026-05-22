#pragma once
#include <Arduino.h>

bool otaFetchLatest(String& tagOut, String& binUrlOut);
bool otaIsNewer(const String& latest, const String& current);
bool otaApplyUpdate(const String& binUrl);
