// Minimal host-side stub for Arduino.h.
// Provides just enough String to compile and exercise the helpers under test.
// Only used when -I test/stubs precedes any path that would resolve the real header.
#pragma once

#include <cstddef>
#include <cstring>
#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}

  size_t length() const { return s_.size(); }
  const char* c_str() const { return s_.c_str(); }

  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator==(const char* o) const { return s_ == (o ? o : ""); }
  bool operator!=(const String& o) const { return !(*this == o); }

  String operator+(const String& o) const { return String(s_ + o.s_); }

 private:
  std::string s_;
};

inline bool operator==(const char* lhs, const String& rhs) { return rhs == lhs; }
