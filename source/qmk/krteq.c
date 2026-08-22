#include QMK_KEYBOARD_H

bool process_record_kb(uint16_t keycode, keyrecord_t *record)
{
    uint8_t mods = get_mods();
    bool double_shift = (mods & MOD_MASK_SHIFT) == MOD_MASK_SHIFT;

    switch (keycode)
    {
        case KRT_VOL:
            if (record->event.pressed)
            {
                if (mods & MOD_MASK_CTRL)
                {
                    host_consumer_send(AUDIO_MUTE);
                }
                else if (mods & MOD_MASK_SHIFT)
                {
                    host_consumer_send(AUDIO_VOL_UP);
                }
                else
                {
                    host_consumer_send(AUDIO_VOL_DOWN);
                }
            }
            else
            {
                host_consumer_send(0);
            }
            return false;

        case KC_B:
            if (record->event.pressed && double_shift)
            {
                tap_code16(QK_BOOTLOADER);
                return false;
            }
            break;

        case KC_C:
            if (record->event.pressed && double_shift)
            {
                tap_code16(QK_CLEAR_EEPROM);
                return false;
            }
            break;
    }

    return process_record_user(keycode, record);
}

#ifdef OLED_ENABLE
bool oled_task_kb(void)
{
    led_t led_state = host_keyboard_led_state();
    oled_write_P(led_state.num_lock ? PSTR("NUM ") : PSTR("    "), false);
    oled_write_P(led_state.caps_lock ? PSTR("CAP ") : PSTR("    "), false);
    oled_write_P(led_state.scroll_lock ? PSTR("ACCENT ") : PSTR("    "), false);

    if (get_highest_layer(layer_state) >= 3)
    {
        oled_write_P(led_state.scroll_lock ? PSTR("INPUT LOCK") : PSTR("    "), false);
    }
    
    return oled_task_user();
}
#endif

const is31fl3733_led_t PROGMEM g_is31fl3733_leds[IS31FL3733_LED_COUNT] = {
    //  R          G          B
    // Row 0
    {0, SW1_CS1,   SW2_CS1,   SW3_CS1},
    {0, SW10_CS1,  SW11_CS1,  SW12_CS1},

    // Row 1
    {0, SW1_CS2,   SW2_CS2,   SW3_CS2},
    {0, SW4_CS2,   SW5_CS2,   SW6_CS2},
    {0, SW7_CS2,   SW8_CS2,   SW9_CS2},
    {0, SW10_CS2,  SW11_CS2,  SW12_CS2},
    {0, SW1_CS3,   SW2_CS3,   SW3_CS3},
    {0, SW4_CS3,   SW5_CS3,   SW6_CS3},
    {0, SW7_CS3,   SW8_CS3,   SW9_CS3},
    {0, SW10_CS3,  SW11_CS3,  SW12_CS3},
    {0, SW1_CS4,   SW2_CS4,   SW3_CS4},
    {0, SW4_CS4,   SW5_CS4,   SW6_CS4},
    {0, SW7_CS4,   SW8_CS4,   SW9_CS4},
    {0, SW10_CS4,  SW11_CS4,  SW12_CS4},

    // Row 2
    {0, SW1_CS5,   SW2_CS5,   SW3_CS5},
    {0, SW4_CS5,   SW5_CS5,   SW6_CS5},
    {0, SW7_CS5,   SW8_CS5,   SW9_CS5},
    {0, SW10_CS5,  SW11_CS5,  SW12_CS5},
    {0, SW1_CS6,   SW2_CS6,   SW3_CS6},
    {0, SW4_CS6,   SW5_CS6,   SW6_CS6},
    {0, SW7_CS6,   SW8_CS6,   SW9_CS6},
    {0, SW10_CS6,  SW11_CS6,  SW12_CS6},
    {0, SW1_CS7,   SW2_CS7,   SW3_CS7},
    {0, SW4_CS7,   SW5_CS7,   SW6_CS7},
    {0, SW7_CS7,   SW8_CS7,   SW9_CS7},
    {0, SW10_CS7,  SW11_CS7,  SW12_CS7},

    // Row 3
    {0, SW1_CS8,   SW2_CS8,   SW3_CS8},
    {0, SW4_CS8,   SW5_CS8,   SW6_CS8},
    {0, SW7_CS8,   SW8_CS8,   SW9_CS8},
    {0, SW10_CS8,  SW11_CS8,  SW12_CS8},
    {0, SW1_CS9,   SW2_CS9,   SW3_CS9},
    {0, SW4_CS9,   SW5_CS9,   SW6_CS9},
    {0, SW7_CS9,   SW8_CS9,   SW9_CS9},
    {0, SW10_CS9,  SW11_CS9,  SW12_CS9},
    {0, SW1_CS10,  SW2_CS10,  SW3_CS10},
    {0, SW4_CS10,  SW5_CS10,  SW6_CS10},
    {0, SW7_CS10,  SW8_CS10,  SW9_CS10},
    {0, SW10_CS10, SW11_CS10, SW12_CS10},

    // Row 4
    {0, SW1_CS11,  SW2_CS11,  SW3_CS11},
    {0, SW4_CS11,  SW5_CS11,  SW6_CS11},
    {0, SW7_CS11,  SW8_CS11,  SW9_CS11},
    {0, SW10_CS11, SW11_CS11, SW12_CS11},
    {0, SW1_CS12,  SW2_CS12,  SW3_CS12},
    {0, SW4_CS12,  SW5_CS12,  SW6_CS12},
    {0, SW7_CS12,  SW8_CS12,  SW9_CS12},
    {0, SW10_CS12, SW11_CS12, SW12_CS12},
    {0, SW1_CS13,  SW2_CS13,  SW3_CS13},
    {0, SW4_CS13,  SW5_CS13,  SW6_CS13},
    {0, SW7_CS13,  SW8_CS13,  SW9_CS13},
    {0, SW10_CS13, SW11_CS13, SW12_CS13},

    // Row 5
    {0, SW1_CS14,  SW2_CS14,  SW3_CS14},
    {0, SW4_CS14,  SW5_CS14,  SW6_CS14},
    {0, SW7_CS14,  SW8_CS14,  SW9_CS14},
    {0, SW10_CS14, SW11_CS14, SW12_CS14},
    {0, SW1_CS15,  SW2_CS15,  SW3_CS15},
    {0, SW4_CS15,  SW5_CS15,  SW6_CS15},
    {0, SW10_CS15, SW11_CS15, SW12_CS15},
    {0, SW1_CS16,  SW2_CS16,  SW3_CS16},
    {0, SW4_CS16,  SW5_CS16,  SW6_CS16},
    {0, SW7_CS16,  SW8_CS16,  SW9_CS16},
    {0, SW10_CS16, SW11_CS16, SW12_CS16},
};
