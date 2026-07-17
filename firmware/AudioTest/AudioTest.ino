// AudioTest — minimal internet-radio audio bring-up.
//
// No display, no encoder: connect WiFi, stream a station out the MAX98357A.
// Works on a classic ESP32 (ESP32-D0WD / WROOM-32 boards) as well as the S3.
//
// Board (Tools):  "ESP32 Dev Module" for a classic ESP32
//                 Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
//
// Wiring (matches the main WorldRadio config):
//   MAX98357A BCLK -> GPIO 2
//   MAX98357A LRC  -> GPIO 15
//   MAX98357A DIN  -> GPIO 4
//   MAX98357A VIN  -> 5V, GND -> GND, speaker on +/-

#include <Arduino.h>
#include <WiFi.h>
#include <Audio.h> // schreibfaul1/ESP32-audioI2S

// ---- WiFi ----
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

// ---- MAX98357A pins ----
#define PIN_I2S_BCLK 2
#define PIN_I2S_LRC 15
#define PIN_I2S_DOUT 4 // wires to the amp's DIN

// Station to play. KQED (https). If TLS is too heavy for a no-PSRAM board,
// swap in the plain-http fallback below.
static const char *STREAM_URL = "https://streams.kqed.org/kqedradio";
// static const char *STREAM_URL = "http://stream.radioparadise.com/mp3-128";

Audio audio;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[AudioTest] boot");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[AudioTest] connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.printf("\n[AudioTest] wifi ok, ip=%s\n", WiFi.localIP().toString().c_str());

  audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
  audio.setVolume(15); // 0..21
  Serial.printf("[AudioTest] connecting to stream: %s\n", STREAM_URL);
  audio.connecttohost(STREAM_URL);
}

void loop() {
  audio.loop(); // feed the decoder
}

// Library status callbacks -> serial, so you can watch it work.
void audio_info(const char *info) { Serial.printf("[audio] %s\n", info); }
void audio_showstation(const char *info) { Serial.printf("[station] %s\n", info); }
void audio_showstreamtitle(const char *info) { Serial.printf("[title] %s\n", info); }
