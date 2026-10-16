#pragma once

#include "quantum.h"

enum keyboard_keycodes
{
    KRT_VOL = QK_KB_0,
    KRT_SCR,
    KRT_RGB
};

typedef struct
{
    const char *name;
    void (*start)(void);
    void (*update)(void);
    void (*key_event)(uint16_t keycode, uint8_t x, uint8_t y, bool pressed);
    void (*reset)(void);
} krteq_screen_t;

// Built-in screens
extern const krteq_screen_t krteq_screen_indicators;
extern const krteq_screen_t krteq_screen_bongo_cat;
extern const krteq_screen_t krteq_screen_game_of_life;
extern const krteq_screen_t krteq_screen_waves;
extern const krteq_screen_t krteq_screen_ball_pit;
extern const krteq_screen_t krteq_screen_system_info;

#define KRTEQ_MAX_SCREENS 255
extern const krteq_screen_t *const user_screens[KRTEQ_MAX_SCREENS];
