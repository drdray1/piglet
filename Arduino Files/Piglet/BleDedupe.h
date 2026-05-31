// Per-device dedupe for BLE observations, shared by solo and mesh-node scanning.
//
// A device (keyed on its BDA + address type) is emitted at most once per
// `windowSec`; repeat sightings inside that window are suppressed. Memory is
// bounded to `maxSize` entries, oldest-inserted evicted first. This throttles
// both SD writes (solo) and ESP-Now traffic (node mode).
//
// Pure / host-testable: the caller injects `nowMs` (millis()) so tests can drive
// a virtual clock. No Arduino, NimBLE, or STL-beyond-container dependency.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>

class BleDedupe {
public:
  // windowSec: dedupe window. maxSize: hard cap on tracked devices (>=1).
  BleDedupe(uint32_t windowSec, size_t maxSize)
      : windowMs_(windowSec * 1000UL), maxSize_(maxSize ? maxSize : 1) {}

  // True if this device should be logged now — first sighting, or the dedupe
  // window has elapsed since it was last logged. Records the emit time.
  // A window of 0 means "never suppress" (every advert emits).
  bool shouldEmit(const uint8_t bda[6], uint8_t addrType, uint32_t nowMs) {
    const uint64_t key = makeKey(bda, addrType);
    auto it = ring_.find(key);
    if (it != ring_.end()) {
      if (windowMs_ != 0 && (uint32_t)(nowMs - it->second) < windowMs_)
        return false;        // within window -> suppress
      it->second = nowMs;    // first time, or window elapsed -> re-emit
      return true;
    }
    ring_.emplace(key, nowMs);
    order_.push_back(key);
    evictIfNeeded();
    return true;
  }

  // Drop entries not seen for at least one window (memory pruning). Optional —
  // shouldEmit() already handles re-emit timing; this just frees slots for
  // devices that have gone away. Cheap: order_ is roughly insertion-ordered and
  // millis() is monotonic, so the oldest live at the front.
  void expire(uint32_t nowMs) {
    if (windowMs_ == 0) return;
    while (!order_.empty()) {
      const uint64_t key = order_.front();
      auto it = ring_.find(key);
      if (it == ring_.end()) { order_.pop_front(); continue; }  // stale slot
      if ((uint32_t)(nowMs - it->second) < windowMs_) break;    // newer behind it
      ring_.erase(it);
      order_.pop_front();
    }
  }

  size_t size() const { return ring_.size(); }
  void clear() { ring_.clear(); order_.clear(); }

private:
  static uint64_t makeKey(const uint8_t bda[6], uint8_t addrType) {
    uint64_t k = 0;
    for (int i = 0; i < 6; i++) k |= (uint64_t)bda[i] << (i * 8);
    k |= (uint64_t)addrType << 48;   // different addr type => different device
    return k;
  }

  void evictIfNeeded() {
    while (ring_.size() > maxSize_ && !order_.empty()) {
      const uint64_t key = order_.front();
      order_.pop_front();
      ring_.erase(key);
    }
  }

  std::unordered_map<uint64_t, uint32_t> ring_;  // key -> last-emit millis
  std::deque<uint64_t> order_;                   // insertion order for eviction
  uint32_t windowMs_;
  size_t   maxSize_;
};
