// World Radio — ESP32-S3 firmware.
//
// A color globe of internet radio stations on a 2.4" ILI9341 SPI LCD, tuned
// with a rotary encoder, audio out via a MAX98357A I2S amp.
//
// This is the C++ port of the browser prototype's projection core
// (src/projection.js): same orthographic math, same snap-and-ease navigation.
// Rasterization is delegated to a LovyanGFX sprite (RGB565, PSRAM) instead of
// the hand-rolled 1-bit framebuffer.

#include <Arduino.h>
#include <WiFi.h>
#include <LovyanGFX.hpp>
#include <Audio.h> // schreibfaul1/ESP32-audioI2S

#include "config.h"
#include "coastline.h"
#include "stations.h"

// ---------------------------------------------------------------------------
// Display (Waveshare 2.4" ILI9341, SPI)
// ---------------------------------------------------------------------------

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.pin_sclk = PIN_LCD_SCK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = PIN_LCD_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = PIN_LCD_CS;
      cfg.pin_rst = PIN_LCD_RST;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.invert = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = PIN_LCD_BL;
      cfg.invert = false;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

static LGFX lcd;
static LGFX_Sprite frame(&lcd); // full-screen RGB565 back buffer (PSRAM)

// Palette (RGB565) — mirrors the terminal app's color mode.
static constexpr uint16_t C_BG = 0x0861;     // deep blue-black
static constexpr uint16_t C_COAST = 0x5EDD;  // light cyan
static constexpr uint16_t C_GRAT = 0x2965;   // dim gray-blue
static constexpr uint16_t C_LIMB = 0x33BF;   // blue
static constexpr uint16_t C_MARK = 0xFEA0;   // yellow
static constexpr uint16_t C_SEL = 0xFB08;    // red
static constexpr uint16_t C_TEXT = 0xFFFF;
static constexpr uint16_t C_DIM = 0x8C71;

// Globe placement on the 240x320 portrait screen.
static constexpr int GLOBE_CX = 120;
static constexpr int GLOBE_CY = 132;
static constexpr int GLOBE_R = 104;
static constexpr int BAR_Y = 254; // station info bar

// ---------------------------------------------------------------------------
// Projection core (port of src/projection.js)
// ---------------------------------------------------------------------------

struct Center {
  float lon, lat, sinLat, cosLat;
};

static Center makeCenter(float lonDeg, float latDeg) {
  Center c;
  c.lon = lonDeg * DEG_TO_RAD;
  c.lat = latDeg * DEG_TO_RAD;
  c.sinLat = sinf(c.lat);
  c.cosLat = cosf(c.lat);
  return c;
}

struct Proj {
  float x, y;
  bool front;
};

// Orthographic azimuthal projection — identical math to the JS/Rust versions.
static Proj project(float lonDeg, float latDeg, const Center &c) {
  float lon = lonDeg * DEG_TO_RAD;
  float lat = latDeg * DEG_TO_RAD;
  float dlon = lon - c.lon;
  float cosLatP = cosf(lat), sinLatP = sinf(lat), cosDlon = cosf(dlon);
  float cosc = c.sinLat * sinLatP + c.cosLat * cosLatP * cosDlon;
  float x = cosLatP * sinf(dlon);
  float y = c.cosLat * sinLatP - c.sinLat * cosLatP * cosDlon;
  Proj p;
  p.x = GLOBE_CX + GLOBE_R * x;
  p.y = GLOBE_CY - GLOBE_R * y;
  p.front = cosc >= 0.0f;
  return p;
}

static float shortestLonDelta(float from, float to) {
  float d = fmodf(to - from, 360.0f);
  if (d > 180.0f) d -= 360.0f;
  if (d < -180.0f) d += 360.0f;
  return d;
}

static void drawPolylineCenti(const int16_t *pts, int n, const Center &c, uint16_t color) {
  Proj prev{0, 0, false};
  bool havePrev = false;
  for (int i = 0; i < n; i++) {
    Proj p = project(pts[i * 2] / 100.0f, pts[i * 2 + 1] / 100.0f, c);
    if (havePrev && prev.front && p.front) {
      frame.drawLine((int)prev.x, (int)prev.y, (int)p.x, (int)p.y, color);
    }
    prev = p;
    havePrev = true;
  }
}

static void drawGraticule(const Center &c) {
  for (int lon = -180; lon < 180; lon += 30) {
    Proj prev{0, 0, false};
    bool have = false;
    for (int lat = -80; lat <= 80; lat += 4) {
      Proj p = project((float)lon, (float)lat, c);
      if (have && prev.front && p.front)
        frame.drawLine((int)prev.x, (int)prev.y, (int)p.x, (int)p.y, C_GRAT);
      prev = p;
      have = true;
    }
  }
  for (int lat = -60; lat <= 60; lat += 30) {
    Proj prev{0, 0, false};
    bool have = false;
    for (int lon = -180; lon <= 180; lon += 4) {
      Proj p = project((float)lon, (float)lat, c);
      if (have && prev.front && p.front)
        frame.drawLine((int)prev.x, (int)prev.y, (int)p.x, (int)p.y, C_GRAT);
      prev = p;
      have = true;
    }
  }
}

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

static volatile int encDelta = 0;
static int selected = 0;
static float curLon, curLat, targetLon, targetLat;
static bool playing = false;
static bool dirty = true;
static Audio audio;
static char statusLine[64] = "booting...";

void IRAM_ATTR onEncoderA() {
  // Quadrature: direction from B at A's falling edge.
  if (digitalRead(PIN_ENC_A) == LOW) {
    encDelta += (digitalRead(PIN_ENC_B) == HIGH) ? 1 : -1;
  }
}

static void renderFrame(bool showGraticule) {
  const Center c = makeCenter(curLon, curLat);
  frame.fillSprite(C_BG);

  if (showGraticule) drawGraticule(c);

  // Coastlines
  int off = 0;
  for (int i = 0; i < COAST_NLINES; i++) {
    drawPolylineCenti(&COAST_PTS[off * 2], COAST_LEN[i], c, C_COAST);
    off += COAST_LEN[i];
  }

  // Limb
  frame.drawCircle(GLOBE_CX, GLOBE_CY, GLOBE_R, C_LIMB);

  // Markers
  for (int i = 0; i < N_STATIONS; i++) {
    Proj p = project(STATIONS[i].lon, STATIONS[i].lat, c);
    if (!p.front) continue;
    if (i == selected) {
      frame.fillCircle((int)p.x, (int)p.y, 4, C_SEL);
      frame.drawCircle((int)p.x, (int)p.y, 7, C_SEL);
    } else {
      frame.fillCircle((int)p.x, (int)p.y, 2, C_MARK);
    }
  }

  // Header
  frame.setTextColor(C_DIM, C_BG);
  frame.setTextDatum(lgfx::top_left);
  frame.drawString("WORLD RADIO", 6, 4, &fonts::Font0);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d/%d", selected + 1, N_STATIONS);
  frame.setTextDatum(lgfx::top_right);
  frame.drawString(buf, 234, 4, &fonts::Font0);

  // Station bar
  frame.drawFastHLine(0, BAR_Y, 240, C_GRAT);
  frame.setTextDatum(lgfx::top_left);
  frame.setTextColor(C_TEXT, C_BG);
  frame.drawString(STATIONS[selected].name, 6, BAR_Y + 6, &fonts::Font2);
  frame.setTextColor(C_DIM, C_BG);
  frame.drawString(STATIONS[selected].city, 6, BAR_Y + 24, &fonts::Font2);
  frame.setTextColor(playing ? C_MARK : C_DIM, C_BG);
  frame.setTextDatum(lgfx::top_right);
  frame.drawString(playing ? "LIVE" : "IDLE", 234, BAR_Y + 6, &fonts::Font2);
  frame.setTextColor(C_DIM, C_BG);
  frame.drawString(statusLine, 234, BAR_Y + 44, &fonts::Font0);

  frame.pushSprite(0, 0);
}

static void selectStation(int idx) {
  selected = ((idx % N_STATIONS) + N_STATIONS) % N_STATIONS;
  targetLon = STATIONS[selected].lon;
  targetLat = STATIONS[selected].lat;
  dirty = true;
  if (playing) {
    if (STATIONS[selected].url[0]) {
      audio.connecttohost(STATIONS[selected].url);
      snprintf(statusLine, sizeof(statusLine), "tuning...");
    } else {
      audio.stopSong();
      playing = false;
      snprintf(statusLine, sizeof(statusLine), "no stream for this pin");
    }
  }
}

static void togglePlay() {
  if (playing) {
    audio.stopSong();
    playing = false;
    snprintf(statusLine, sizeof(statusLine), "stopped");
  } else if (STATIONS[selected].url[0]) {
    audio.connecttohost(STATIONS[selected].url);
    playing = true;
    snprintf(statusLine, sizeof(statusLine), "tuning...");
  } else {
    snprintf(statusLine, sizeof(statusLine), "no stream for this pin");
  }
  dirty = true;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), onEncoderA, CHANGE);

  lcd.init();
  lcd.setRotation(0);
  lcd.setBrightness(200);
  frame.setColorDepth(16);
  frame.setPsram(true);
  frame.createSprite(240, 320);

  curLon = targetLon = STATIONS[0].lon;
  curLat = targetLat = STATIONS[0].lat;

  snprintf(statusLine, sizeof(statusLine), "wifi: %s ...", WIFI_SSID);
  renderFrame(true);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(100);
  snprintf(statusLine, sizeof(statusLine),
           WiFi.status() == WL_CONNECTED ? "wifi ok — press knob to play" : "wifi FAILED (check config.h)");

  audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
  audio.setVolume(15); // 0..21

  dirty = true;
}

void loop() {
  // Encoder tune
  int d = encDelta;
  if (d != 0) {
    encDelta = 0;
    selectStation(selected + (d > 0 ? 1 : -1));
  }

  // Push-to-play (debounced)
  static uint32_t lastPress = 0;
  if (digitalRead(PIN_ENC_SW) == LOW && millis() - lastPress > 350) {
    lastPress = millis();
    togglePlay();
  }

  // Ease toward the selected station, ~15 fps while moving.
  static uint32_t lastAnim = 0;
  float dLon = shortestLonDelta(curLon, targetLon);
  float dLat = targetLat - curLat;
  bool moving = fabsf(dLon) > 0.05f || fabsf(dLat) > 0.05f;
  if ((moving || dirty) && millis() - lastAnim > 66) {
    lastAnim = millis();
    if (moving) {
      curLon += dLon * 0.22f;
      curLat += dLat * 0.22f;
    } else {
      curLon = targetLon;
      curLat = targetLat;
    }
    // Skip the graticule mid-spin (cheaper frames), draw it on settle.
    renderFrame(!moving);
    dirty = moving;
  }

  audio.loop(); // feed the stream decoder
}

// Optional status callbacks from the audio library.
void audio_showstation(const char *info) {
  snprintf(statusLine, sizeof(statusLine), "%.60s", info);
  dirty = true;
}
void audio_info(const char *info) { Serial.printf("audio: %s\n", info); }
