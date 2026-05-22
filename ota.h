#pragma once
#include <Arduino.h>

String otaFetchLatestTag();
bool otaIsNewer(const String& latest, const String& current);
bool otaApplyUpdate(const String& tag);
