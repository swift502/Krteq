#pragma once

#include "quantum.h"
#include "krteq.h"

void default_user_screen(void);
void shift_user_screen(int shift);
void oled_key_event(uint16_t keycode, keypos_t keypos, bool pressed);
void render_shutdown_screen(bool jump_to_bootloader);
void oled_restart(void);

typedef enum
{
    OLED_SETTING_BRIGHTNESS,
    OLED_SETTING_PRE_CHARGE,
    OLED_SETTING_VCOM,
    OLED_SETTING_COUNT
} oled_setting_t;

void oled_settings_load(void);
void oled_settings_save(void);
uint8_t oled_setting_get(oled_setting_t setting);
void oled_setting_set(oled_setting_t setting, uint8_t value);
