// Skipped: the single-pass /logs walk in WigleUpload.cpp
// (uploadAllCsvsToWigle / uploadAllCsvsToWdgwars) is exercised against the
// Arduino `SD` API and is not directly testable host-side without either
// (a) extracting the inner loop behind a directory-iterator abstraction, or
// (b) building a full FatFs/SD fake.
//
// Coverage is integration-only for now: manual SD flash + observation that
// uploadTotalFiles still equals the eligible CSV count, and that the maxFiles
// cap is honoured. Tracked in docs/TEST_PLAN.md.
//
// Leaving this file in place so future contributors find the documented gap
// when grepping for "wigle" or "upload" in test/.

#include "doctest.h"

TEST_CASE("WigleUpload single-pass walk: skipped — integration test only" * doctest::skip(true)) {
  // Intentionally empty. See file header comment for the rationale.
}
