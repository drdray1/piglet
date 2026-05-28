// Coverage for maskedField (the helper extracted from WebUI.cpp's handleStatus).
// Production call sites: cfg.wigleBasicToken, cfg.wdgwarsApiKey, cfg.homePsk.
//
// Includes the real production header — only Arduino.h is stubbed (test/stubs).

#include "doctest.h"
#include "WebUI_masking.h"

TEST_CASE("maskedField: empty input yields empty string") {
  CHECK(maskedField(String("")) == "");
}

TEST_CASE("maskedField: non-empty input yields the literal \"(set)\"") {
  CHECK(maskedField(String("abc123"))           == "(set)");
  CHECK(maskedField(String("x"))                == "(set)");
  CHECK(maskedField(String("a very long token that looks vaguely real")) == "(set)");
}

TEST_CASE("maskedField: never leaks the original value, regardless of length") {
  // Belt-and-braces: confirm we don't accidentally return the input.
  String secret("supersecret-abc-123-XYZ");
  String out = maskedField(secret);
  CHECK(out != secret);
  CHECK(out == "(set)");
}
