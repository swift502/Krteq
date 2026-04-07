#include QMK_KEYBOARD_H
#include "hal.h"

// PWM0 A - GP0 (Num Lock)
// PWM0 B - GP1 (Caps Lock)
static PWMConfig pwm0_cfg = {
    .frequency = 1000000,
    .period = 511,
    .callback = NULL,
    .channels = {
        {PWM_OUTPUT_ACTIVE_HIGH, NULL},
        {PWM_OUTPUT_ACTIVE_HIGH, NULL}
    }
};

// PWM1 A - GP2 (Scroll Lock)
static PWMConfig pwm1_cfg = {
    .frequency = 1000000,
    .period = 511,        
    .callback = NULL,
    .channels = {
        {PWM_OUTPUT_ACTIVE_HIGH, NULL},
        {PWM_OUTPUT_DISABLED, NULL}
    }
};

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
                reset_keyboard();
                return false;
            }
            break;

        case KC_C:
            if (record->event.pressed && double_shift)
            {
                eeconfig_disable();
                soft_reset_keyboard();
                return false;
            }
            break;
    }

    return process_record_user(keycode, record);
}

void keyboard_post_init_kb(void)
{
    // 2.19.2. Function Select (PWM)
    // https://pip-assets.raspberrypi.com/categories/814-rp2040/documents/RP-008371-DS-1-rp2040-datasheet.pdf?disposition=inline
    palSetLineMode(GP0, PAL_MODE_ALTERNATE(4));
    palSetLineMode(GP1, PAL_MODE_ALTERNATE(4));
    palSetLineMode(GP2, PAL_MODE_ALTERNATE(4));

    pwmStart(&PWMD0, &pwm0_cfg);
    pwmStart(&PWMD1, &pwm1_cfg);

    // Initialize off
    pwmEnableChannel(&PWMD0, 0, 0);
    pwmEnableChannel(&PWMD0, 1, 0);
    pwmEnableChannel(&PWMD1, 0, 0);
}

bool led_update_kb(led_t led_state)
{
    bool res = led_update_user(led_state);

    if (res)
    {
        pwmEnableChannel(&PWMD0, 0, led_state.num_lock ? 1 : 0);
        pwmEnableChannel(&PWMD0, 1, led_state.caps_lock ? 1 : 0);
        pwmEnableChannel(&PWMD1, 0, led_state.scroll_lock ? 1 : 0);
    }

    return res;
}