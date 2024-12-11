#pragma once

//        REV10                     REV8
//
//    11 10  9  8  7            10  9  8  7  6
// 12    +-------+    6     11    +-------+     5
// 13    |       |    5     12    |       |     4
// USB   | OLED  |   []     USB   | OLED  |    []
// 14    |       |    4     13    |       |     3
// 15    +-------+    3     14    +-------+     2
//    16 17  0  1  2            15  x  x  0  1
//
//
//       coordin8_t                Angle256
//
//   (0,0)       (255,0)              64
//       +-------+                +-------+
//       |       |                |       |
//    USB| OLED  |[]          128 | OLED  |[] 0 (256)
//       |       |                |       |
//       +-------+                +-------+
// (0,255)       (255,255)           192

#define COUNT_OF_PIXELS RGB_LEN // 18 for Rev10, 16 for Rev8
#define PIXEL_MASK_ALL ((1u << COUNT_OF_PIXELS) - 1)

// This allows callers to set all pixels
// to arbitrary colors, while maintaining
// typesafety for the function parameter.
typedef struct _all_pixel_uint32_colors_t {
    uint32_t color[COUNT_OF_PIXELS];
} all_pixel_uint32_colors_t;

#pragma region // 8-bit scaled pixel coordinates and angle256
/// @brief Scaled coordinates in range [0..255]
typedef struct _coordin8 {
    uint8_t x;
    uint8_t y;
} coordin8_t;

/// @brief Each pixel's coordinate in a 256x256 grid, as
///        extracted from the PCB layout and Pick'n'Place data.
/// @details The grid is oriented with the origin (0,0) at the
///          upper left, similar to a PC screen.  Orient the PCB
///          so the USB port is on the left, and the
///          plank connector is on the right.
///
///            y
///          x +---------------> +x
///            |    11  10 ...
///            |  12
///            |  ...
///            V
///           +y
///
extern const coordin8_t pixel_coordin8[COUNT_OF_PIXELS];
/// @brief Angular position in 1/256th-circle units, as
///        extracted from the PCB layout and Pick'n'Place data.
///        From the center of the PCB, the zero angle is
///        directly towards the center of the plank connector,
///        with angles increasing in the anti-clockwise direction.
extern const uint8_t pixel_angle256[COUNT_OF_PIXELS];

typedef enum _led_effect {
    LED_EFFECT_DISABLED = 0,
    LED_EFFECT_SOLID = 1,
    LED_EFFECT_ANGLE_WIPE = 2,
    LED_EFFECT_CENTER_WIPE = 3,
    LED_EFFECT_CLOCKWISE_WIPE = 4,
    LED_EFFECT_TOP_SIDE_WIPE = 5,
    LED_EFFECT_SCANNER = 6,
    LED_EFFECT_GENTLE_GLOW = 7,

    LED_EFFECT_PARTY_MODE, // NOTE: This must be the last effect
    MAX_LED_EFFECT,
} led_effect_t;
#define DEFAULT_LED_EFFECT = LED_EFFECT_GENTLE_GLOW;

// allows led_effect_t to be used as a boolean; also relied upon in party mode handling
static_assert(LED_EFFECT_DISABLED == 0, "LED_EFFECT_DISABLED must be zero");
// party mode handling is uniquely handled, so must be the last effect listed
static_assert(MAX_LED_EFFECT - 1 == LED_EFFECT_PARTY_MODE, "LED_EFFECT_PARTY_MODE must be the last effect");

// TODO: review and provide a more useful client-focused API.
// TODO: adjust to use RGB color type instead of uint32_t.
void rgb_init(void);
void rgb_put(uint32_t color);
void rgb_irq_enable(bool enable);
void rgb_set_all(uint8_t r, uint8_t g, uint8_t b);
void rgb_set_effect(led_effect_t new_effect);

void rgb_set_pixels_by_mask(uint32_t mask_of_pixels, uint32_t color);
void rgb_set_single_pixel(uint8_t index, uint32_t color);
void rgb_set_all_pixels(all_pixel_uint32_colors_t* colors);
