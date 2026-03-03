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

// #include "hal.h"

// // 1. Define the desired maximum brightness (0-255)
// #define INDICATOR_BRIGHTNESS 20 

// // 2. Configure PWM Slice 0 (Controls GP0 and GP1)
// static PWMConfig pwm0_cfg = {
//     .frequency = 1000000, // 1 MHz PWM clock frequency
//     .period = 255,        // 255 ticks per period
//     .callback = NULL,     // No callback needed
//     .channels = {
//         {PWM_OUTPUT_ACTIVE_HIGH, NULL}, // Channel 0 (A) -> Maps to GP0
//         {PWM_OUTPUT_ACTIVE_HIGH, NULL}  // Channel 1 (B) -> Maps to GP1
//     }
// };

// // 3. Configure PWM Slice 1 (Controls GP2)
// static PWMConfig pwm1_cfg = {
//     .frequency = 1000000,
//     .period = 255,        
//     .callback = NULL,
//     .channels = {
//         {PWM_OUTPUT_ACTIVE_HIGH, NULL}, // Channel 0 (A) -> Maps to GP2
//         {PWM_OUTPUT_DISABLED, NULL}     // Channel 1 (B) -> GP3 (Unused here)
//     }
// };

// // 4. Initialize the pins and start the drivers
// void keyboard_post_init_kb(void) {
//     // Route the GPIO pins to the PWM peripheral (RP2040 Alternate Function 4 is PWM)
//     palSetLineMode(GP0, PAL_MODE_ALTERNATE(4));
//     palSetLineMode(GP1, PAL_MODE_ALTERNATE(4));
//     palSetLineMode(GP2, PAL_MODE_ALTERNATE(4));

//     // Start the ChibiOS PWM drivers with our configs
//     pwmStart(&PWMD0, &pwm0_cfg);
//     pwmStart(&PWMD1, &pwm1_cfg);

//     // Ensure all LEDs are initially turned off (0 duty cycle)
//     pwmEnableChannel(&PWMD0, 0, 0); // GP0
//     pwmEnableChannel(&PWMD0, 1, 0); // GP1
//     pwmEnableChannel(&PWMD1, 0, 0); // GP2
// }

// bool led_update_kb(led_t led_state)
// {
//     bool res = led_update_user(led_state);

//     if (res) {
//         // Control GP0 (Num Lock)
//         if (led_state.num_lock) {
//             pwmEnableChannel(&PWMD0, 0, INDICATOR_BRIGHTNESS); 
//         } else {
//             pwmEnableChannel(&PWMD0, 0, 0); 
//         }
    
//         // Control GP1 (Caps Lock)
//         if (led_state.caps_lock) {
//             pwmEnableChannel(&PWMD0, 1, INDICATOR_BRIGHTNESS);
//         } else {
//             pwmEnableChannel(&PWMD0, 1, 0);
//         }
    
//         // Control GP2 (Scroll Lock)
//         if (led_state.scroll_lock) {
//             pwmEnableChannel(&PWMD1, 0, INDICATOR_BRIGHTNESS);
//         } else {
//             pwmEnableChannel(&PWMD1, 0, 0);
//         }
//     }

//     return res;
// }