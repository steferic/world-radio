#ifndef CONFIG_H
#define CONFIG_H

// TFT pins
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4

// Display setup
#define PANEL_WIDTH  240
#define PANEL_HEIGHT 320
#define HALF_HEIGHT  (PANEL_HEIGHT / 2)

// Sphere appearance
#define SPHERE_RADIUS_PX   95
#define CENTER_X           120
#define CENTER_Y           160
#define NUM_LAT_RINGS      7
#define NUM_LON_LINES      8
#define SEGMENTS_PER_LINE  28
#define TILT_DEG           20.0
#define ROTATION_STEP_DEG  4.0
#define FRAME_DELAY_MS     5

#define RGB565(r, g, b) ((uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)))
#define COLOR_FRONT  ST77XX_CYAN
#define COLOR_BACK   RGB565(50, 50, 50)
#define COLOR_LIGHT_MODE false


#endif