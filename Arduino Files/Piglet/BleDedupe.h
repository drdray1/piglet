// Per-device "log once" dedupe ring, shared by Wi-Fi and BLE scanning.
//
// A device (keyed on its 6-byte MAC/BDA + address type) is emitted the FIRST
// time it is seen and suppressed on every later sighting — no time window, no
// GPS distance. Memory is bounded to `maxSize` entries, oldest-inserted evicted
// first (FIFO ring); once a device is evicted it can emit again. This matches
// the upstream JCMK / Biscuit `seen_mac()` + `mac_history[200]` scheme and
// throttles both SD writes (solo) and ESP-Now traffic (node mode).
//
// Wi-Fi callers pass addrType = 0 (BSSIDs have no address type); BLE callers
// pass the NimBLE address type so public/random/RPA/NRPA variants of the same
// MAC stay distinct.
//
// Pure / host-testable: no Arduino, NimBLE, clock, or STL-beyond-container
// dependency.
//
// NOTE: an identical copy lives at PigletNode/BleDedupe.h — keep the two in sync.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_set>

class BleDedupe {
public:
  // maxSize: hard cap on tracked devices (>=1). Default 200 matches upstream.
  explicit BleDedupe(size_t maxSize = 200)
      : maxSize_(maxSize ? maxSize : 1) {}

  // True the first time this device is seen (caller should log/forward it),
  // false on every later sighting — until it is evicted past the cap, after
  // which it is treated as new again. Records first-seen devices.
  bool shouldEmit(const uint8_t mac[6], uint8_t addrType) {
    const uint64_t key = makeKey(mac, addrType);
    if (ring_.find(key) != ring_.end())
      return false;            // already seen -> suppress
    ring_.insert(key);
    order_.push_back(key);
    evictIfNeeded();
    return true;
  }

  size_t size() const { return ring_.size(); }
  void clear() { ring_.clear(); order_.clear(); }

private:
  static uint64_t makeKey(const uint8_t mac[6], uint8_t addrType) {
    uint64_t k = 0;
    for (int i = 0; i < 6; i++) k |= (uint64_t)mac[i] << (i * 8);
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

  std::unordered_set<uint64_t> ring_;  // seen keys
  std::deque<uint64_t> order_;         // insertion order for FIFO eviction
  size_t maxSize_;
};
