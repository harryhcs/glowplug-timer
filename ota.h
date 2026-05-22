#pragma once
#include <Arduino.h>

String otaFetchLatestTag();
bool otaApplyUpdate(const String& tag);
