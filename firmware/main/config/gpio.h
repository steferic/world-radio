#pragma once

#include "sdkconfig.h"

// MAX98357A I2S DAC/amp wiring
#if CONFIG_SPIRAM_MODE_OCT
    #include "boards/n16r8.h"
#elif CONFIG_SPIRAM_MODE_QUAD
    #include "boards/n8r2.h"
#else
    #error "No board detected: set CONFIG_SPIRAM_MODE_QUAD or _OCT in the sdkconfig fragment for the target board."
#endif

// Volume knob
#define VOLUME_POT_GPIO          GPIO_NUM_4

// Rotary encoder
#define ROTARY_ENCODER_GPIO_A     GPIO_NUM_15
#define ROTARY_ENCODER_GPIO_B     GPIO_NUM_16
#define ROTARY_ENCODER_PUSHBUTTON GPIO_NUM_47

// Standalone pushbutton
#define PUSHBUTTON_GPIO           GPIO_NUM_21

// LCD (ST7789, 240x320 SPI TFT)
#define LCD_SPI_HOST        SPI2_HOST
#define LCD_SCK_GPIO        GPIO_NUM_2
#define LCD_MOSI_GPIO       GPIO_NUM_42
#define LCD_CS_GPIO         GPIO_NUM_39
#define LCD_DC_GPIO         GPIO_NUM_40
#define LCD_RESET_GPIO      GPIO_NUM_41