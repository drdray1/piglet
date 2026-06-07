#include "BleScanner.h"

#if PIGLET_HAS_BLE

#include "Globals.h"
#include "Config.h"
#include "BleCsv.h"
#include "BleDedupe.h"

#include <NimBLEDevice.h>
#include <deque>
#include <cstdlib>
#include <cstring>
#include "esp_coexist.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ---- Global instance ----
BleScanner bleScanner;

namespace {

// Shared state touched by BOTH the NimBLE host task (onResult) and the loop task
// (consumeResults/tick). Guarded by gMutex. The NimBLE callback is a task
// context (not a hard ISR), so a blocking mutex is safe to take there.
SemaphoreHandle_t gMutex = nullptr;
BleDedupe*        gDedupe = nullptr;        // sized from cfg at begin()
std::deque<BleObservation> gPending;        // hand-off FIFO, bounded by cfg
uint32_t          gLifetimeUnique = 0;
size_t            gMaxResults = 500;

struct Lock {
  bool held = false;
  Lock()  { if (gMutex) held = (xSemaphoreTake(gMutex, portMAX_DELAY) == pdTRUE); }
  ~Lock() { if (held) xSemaphoreGive(gMutex); }
};

// ---- NimBLE scan callback ----
// RUNS IN THE NIMBLE HOST TASK. Do ONLY byte-copies + container ops under the
// lock here: no SD, no WiFi, no HTTP, no Serial.printf (those take mutexes the
// loop task may hold). The loop task drains gPending at its own pace.
class Observer : public NimBLEScanCallbacks {
public:
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    if (!dev || !gDedupe) return;

    // Address (display order is big-endian, MSB first).
    std::string a = dev->getAddress().toString();
    if (a.size() < 17) return;
    uint8_t bda[6];
    parseBda(a.c_str(), bda);
    uint8_t addrType = dev->getAddress().getType();

    uint32_t now = millis();

    Lock lk;
    // Log-once gate; when dedup is disabled every observation is emitted.
    if (cfg.dedupEnabled && !gDedupe->shouldEmit(bda, addrType)) return;  // already seen
    gLifetimeUnique++;

    if (gPending.size() >= gMaxResults) gPending.pop_front();  // bound memory

    BleObservation o = {};
    formatBda(bda, o.addr);
    o.addrType     = addrType;
    o.channel      = 37;     // NimBLE 2.x doesn't reliably expose it (see §17)
    o.rssi         = (int8_t)dev->getRSSI();
    o.observedAtMs = now;

    std::string name = dev->getName();
    std::strncpy(o.name, name.c_str(), sizeof(o.name) - 1);

    if (dev->haveManufacturerData()) {
      std::string md = dev->getManufacturerData();
      if (md.size() >= 2)
        o.mfgrId = (uint16_t)((uint8_t)md[0] | ((uint8_t)md[1] << 8));
    }

    if (dev->haveServiceUUID()) {
      char* p = o.serviceUuids;
      size_t rem = sizeof(o.serviceUuids);
      for (int i = 0; i < dev->getServiceUUIDCount() && rem > 5; i++) {
        char uuid[5];
        // Keep only 16-bit UUIDs, normalised to 4-hex uppercase (e.g. FE9F).
        if (!normalizeBleUuid16(dev->getServiceUUID(i).toString(), uuid)) continue;
        int n = std::snprintf(p, rem, "%s%s", (p == o.serviceUuids ? "" : ";"),
                              uuid);
        if (n < 0 || (size_t)n >= rem) break;
        p += n; rem -= (size_t)n;
      }
    }

    gPending.push_back(o);
  }
};

Observer gObserver;

}  // namespace

// ---- BleScanner methods ----

void BleScanner::begin() {
  if (initialised_) return;

  if (!gMutex) gMutex = xSemaphoreCreateMutex();
  gMaxResults = cfg.bleMaxResults ? cfg.bleMaxResults : 200;
  if (!gDedupe)
    gDedupe = new BleDedupe(gMaxResults);

  NimBLEDevice::init("piglet");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&gObserver, /*wantDuplicates=*/false);
  scan->setActiveScan(false);   // PASSIVE ONLY — never transmit SCAN_REQ (coex)
  // NimBLE 2.x: time params are milliseconds (was 0.625 ms units in 1.x).
  scan->setInterval(100);       // 100 ms between channel listens
  scan->setWindow(60);          // 60 ms listen -> 60% duty, leaves coex airtime

  initialised_ = true;
  Serial.println("[BLE] NimBLE observer initialised");
}

void BleScanner::startScan() {
  if (!initialised_) begin();
  if (scanRunning_) return;

  esp_coex_preference_set(ESP_COEX_PREFER_BT);  // favour BLE during the window
  // NimBLE 2.x: start() duration is in MILLISECONDS (was seconds in 1.x).
  // Non-blocking; NimBLE stops itself when the duration elapses.
  uint32_t durMs = (uint32_t)cfg.bleScanDuration * 1000UL;
  NimBLEDevice::getScan()->start(durMs, /*is_continue=*/false);
  scanRunning_  = true;
  scanEndsAtMs_ = millis() + (uint32_t)cfg.bleScanDuration * 1000UL;
  Serial.printf("[BLE] scan window start (%u s)\n", (unsigned)cfg.bleScanDuration);
}

void BleScanner::stopScan() {
  if (!scanRunning_) return;
  NimBLEDevice::getScan()->stop();
  esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
  scanRunning_ = false;
}

size_t BleScanner::consumeResults(std::vector<BleObservation>& out) {
  Lock lk;
  size_t n = gPending.size();
  out.reserve(out.size() + n);
  while (!gPending.empty()) {
    out.push_back(gPending.front());
    gPending.pop_front();
  }
  return n;
}

void BleScanner::tick() {
  // Window expiry — NimBLE has already stopped on its own timer; just restore
  // the coex preference and clear our flag.
  bool windowEnded = false;
  if (scanRunning_ && millis() >= scanEndsAtMs_) {
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
    scanRunning_ = false;
    windowEnded = true;
  }

  // Snapshot counts under the lock. The log-once ring self-bounds via FIFO
  // eviction at its cap, so there is nothing to prune here.
  uint32_t life = 0; size_t tracked = 0;
  if (gDedupe) {
    Lock lk;
    life = gLifetimeUnique;
    tracked = gDedupe->size();
  }

  if (windowEnded)
    Serial.printf("[BLE] window end — %u unique lifetime, %u tracked\n",
                  (unsigned)life, (unsigned)tracked);
}

uint32_t BleScanner::lifetimeUniqueCount() const {
  Lock lk;
  return gLifetimeUnique;
}

size_t BleScanner::dedupeWindowSize() const {
  Lock lk;
  return gDedupe ? gDedupe->size() : 0;
}

#endif  // PIGLET_HAS_BLE
