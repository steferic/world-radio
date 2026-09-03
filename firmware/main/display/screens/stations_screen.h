#pragma once

#include "display/screen.h"

// A scrollable list of stations. Selecting one currently just updates
// the now-playing display and pops back -- actually swapping the audio
// stream is a follow-up (it needs to stop and restart the http fetch +
// mp3 decoder cleanly, which is a bigger change than the UI work here).
extern const screen_t stations_screen;
