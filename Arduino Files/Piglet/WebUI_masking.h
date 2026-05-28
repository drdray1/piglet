#pragma once
#include <Arduino.h>

// Returns "(set)" if v is non-empty, otherwise an empty string.
// Used by /status.json to hide sensitive config values from the API.
inline String maskedField(const String& v) {
  return v.length() ? String("(set)") : String("");
}
