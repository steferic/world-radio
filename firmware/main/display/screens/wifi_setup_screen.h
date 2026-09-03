#pragma once

#include "display/screen.h"

// Placeholder for Wi-Fi credentials entry. Currently shows the SSID from
// config.h and a "not yet implemented" note. Wired into the menu so the
// navigation flow can be exercised end-to-end; the actual text-input
// widget (character-cycle? on-screen keyboard? BLE provisioning?) is a
// separate design decision that this scaffold doesn't force.
extern const screen_t wifi_setup_screen;
