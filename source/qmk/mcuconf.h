#pragma once

#include_next <mcuconf.h>

// PWM0 A - GP0 (Num Lock)
// PWM0 B - GP1 (Caps Lock)
#undef RP_PWM_USE_PWM0
#define RP_PWM_USE_PWM0 TRUE

// PWM1 A - GP2 (Scroll Lock)
#undef RP_PWM_USE_PWM1
#define RP_PWM_USE_PWM1 TRUE

// PWM6 B - GP13 (Backlight)
#undef RP_PWM_USE_PWM6
#define RP_PWM_USE_PWM6 TRUE