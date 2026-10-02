#pragma once

#include "quantum.h"
#include "krteq.h"

void default_user_screen(void);
void shift_user_screen(int shift);
void oled_key_event(uint16_t keycode, keypos_t key, bool pressed);
void render_shutdown_screen(bool jump_to_bootloader);
