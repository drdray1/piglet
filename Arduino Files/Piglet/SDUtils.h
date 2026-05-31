#pragma once
#include <Arduino.h>
#include <vector>

struct BleObservation;   // defined in BleScanner.h

bool   openLogFile();
void   closeLogFile();
void   appendWigleRow(const String& mac, const String& ssid, const String& auth,
                      const String& firstSeen, int channel, int rssi,
                      double lat, double lon, double altM, double accM);
// BLE observation row (Type=BLE). Mirrors appendWigleRow; addrType is the
// NimBLE address-type code (0=public,1=random,2=resolvable,3=non-resolvable),
// serviceUuids is a ';'-joined 16-bit UUID list (may be empty), mfgrId is the
// LE-decoded company identifier (0 if none). See BleCsv.h for the layout.
void   appendBleRow(const String& bda, const String& name, uint8_t addrType,
                    const String& firstSeen, int channel, int rssi,
                    double lat, double lon, double altM, double accM,
                    const String& serviceUuids, uint16_t mfgrId);
// Write a batch of BLE observations using a single GPS snapshot for the whole
// batch (one fix per scan window — see BLE_PHASE2_PLAN.md §9). No-fix rows are
// written with 0,0,0,0, matching the Wi-Fi path. Used by solo mode; the Core
// has its own equivalent in MeshNode.cpp for forwarded node observations.
void   writeBleRowsFromObs(const std::vector<BleObservation>& obs);

String normalizeSdPath(const char* dir, const char* nameIn);
String pathBasename(const String& p);
bool   isAllowedDataPath(const String& p);
bool   moveToUploaded(const String& srcPath);
