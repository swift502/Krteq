#pragma once

#include "quantum.h"

enum keyboard_keycodes
{
    KRT_VOL = QK_KB_0,
    KRT_SCR,
    KRT_RGB
};

// VIA custom value channels, 1-4 are reserved by QMK
enum krteq_via_channels
{
    KRTEQ_CHANNEL_OLED = id_custom_channel
};

typedef struct
{
    const char *name;
    void (*init)(void);
    void (*start)(void);
    void (*update)(void);
    void (*key_event)(uint16_t keycode, uint8_t x, uint8_t y, bool pressed);
} krteq_screen_t;

// Built-in screens
extern const krteq_screen_t krteq_screen_indicators;
extern const krteq_screen_t krteq_screen_bongo_cat;
extern const krteq_screen_t krteq_screen_starfield;
extern const krteq_screen_t krteq_screen_matrix;
extern const krteq_screen_t krteq_screen_waves;
extern const krteq_screen_t krteq_screen_game_of_life;
extern const krteq_screen_t krteq_screen_system_info;

extern const krteq_screen_t *const user_screens[UINT8_MAX];
