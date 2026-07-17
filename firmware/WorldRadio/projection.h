// projection.h — the portable orthographic projection core.
//
// C++ twin of the browser's src/projection.js and terminal-radio's globe.rs:
// same math, no display/IO dependencies. Lives in a header (not the .ino) so
// the Arduino preprocessor's auto-generated prototypes can't land above the
// type definitions.

#pragma once
#include <Arduino.h>

struct Center {
  float lon, lat, sinLat, cosLat;
};

struct Proj {
  float x, y;
  bool front;
};

static inline Center makeCenter(float lonDeg, float latDeg) {
  Center c;
  c.lon = lonDeg * DEG_TO_RAD;
  c.lat = latDeg * DEG_TO_RAD;
  c.sinLat = sinf(c.lat);
  c.cosLat = cosf(c.lat);
  return c;
}

// Orthographic azimuthal projection ("globe as seen from space").
// cx/cy/r place the globe on screen; front=false means back hemisphere (clip).
static inline Proj projectTo(float lonDeg, float latDeg, const Center &c,
                             float cx, float cy, float r) {
  float lon = lonDeg * DEG_TO_RAD;
  float lat = latDeg * DEG_TO_RAD;
  float dlon = lon - c.lon;
  float cosLatP = cosf(lat), sinLatP = sinf(lat), cosDlon = cosf(dlon);
  float cosc = c.sinLat * sinLatP + c.cosLat * cosLatP * cosDlon;
  float x = cosLatP * sinf(dlon);
  float y = c.cosLat * sinLatP - c.sinLat * cosLatP * cosDlon;
  Proj p;
  p.x = cx + r * x;
  p.y = cy - r * y;
  p.front = cosc >= 0.0f;
  return p;
}

static inline float shortestLonDelta(float from, float to) {
  float d = fmodf(to - from, 360.0f);
  if (d > 180.0f) d -= 360.0f;
  if (d < -180.0f) d += 360.0f;
  return d;
}
