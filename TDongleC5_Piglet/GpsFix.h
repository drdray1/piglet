#pragma once

// One-shot GPS fix snapshot used to stamp CSV rows.
//
// Mirrors the GpsFix in "../Arduino Files/Piglet/GPS.h" so both firmwares
// expose the same `GpsFix g = captureGpsFix();` shape at their call sites.
// Arduino can't share files across sketch folders, so it's duplicated here.
//
// This lives in a header rather than inline in the .ino on purpose: the
// Arduino builder auto-generates prototypes and inserts them near the top of
// the sketch, ahead of any type declared in the .ino body. Declaring GpsFix
// there makes the generated `static GpsFix captureGpsFix();` fail with
// "'GpsFix' does not name a type". A header is included before that point.
//
// captureGpsFix() itself stays in the .ino — it reads the sketch-local GPS
// state (gps, gpsHasFix) and the last-known-position cache.
struct GpsFix { double lat, lon, altM, accM; };
