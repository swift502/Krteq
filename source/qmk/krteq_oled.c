#include "krteq_oled.h"
#include OLED_FONT_H

#define LOGO_DURATION 3000
uint16_t logo_timer;
bool logo_finished = false;
int selected_user_screen = 0;
int last_rendered_screen = -1;

enum screens
{
    USER_SCREEN_INDICATORS,
    USER_SCREEN_BONGO_CAT,
    USER_SCREEN_COUNT,

    SYSTEM_SCREEN_LOGO,
    SYSTEM_SCREEN_INPUT_LOCK
};

void default_user_screen(void)
{
    selected_user_screen = 0;
}

void shift_user_screen(int shift)
{
    selected_user_screen += shift;
    selected_user_screen %= USER_SCREEN_COUNT;
    if (selected_user_screen < 0) selected_user_screen += USER_SCREEN_COUNT;
}

#define INDICATOR_PADDING_X 5
#define INDICATOR_PADDING_Y 3
#define INDICATOR_HEIGHT (OLED_FONT_HEIGHT + INDICATOR_PADDING_Y * 2)
#define INDICATOR_TOP ((OLED_DISPLAY_HEIGHT - INDICATOR_HEIGHT) / 2)
#define OLED_PAGES (OLED_DISPLAY_HEIGHT / 8)

void render_indicator(uint8_t x, const char *label, bool enabled)
{
    uint8_t width = strlen(label) * OLED_FONT_WIDTH;
    uint8_t left = x - INDICATOR_PADDING_X;
    uint8_t total = width + INDICATOR_PADDING_X * 2;
    uint32_t box = enabled ? (((uint32_t)1 << INDICATOR_HEIGHT) - 1) << INDICATOR_TOP : 0;

    for (uint8_t i = 0; i < total; i++)
    {
        uint32_t column = box;

        // Rounded corners
        if (i == 0 || i == total - 1)
        {
            column &= ~(((uint32_t)1 << INDICATOR_TOP) | ((uint32_t)1 << (INDICATOR_TOP + INDICATOR_HEIGHT - 1)));
        }

        if (i >= INDICATOR_PADDING_X && i < INDICATOR_PADDING_X + width)
        {
            uint8_t text = i - INDICATOR_PADDING_X;
            uint8_t glyph = pgm_read_byte(&font[(label[text / OLED_FONT_WIDTH] - OLED_FONT_START) * OLED_FONT_WIDTH + text % OLED_FONT_WIDTH]);
            uint32_t bits = (uint32_t)glyph << (INDICATOR_TOP + INDICATOR_PADDING_Y);
            column = enabled ? column & ~bits : column | bits;
        }

        for (uint8_t page = 0; page < OLED_PAGES; page++)
        {
            oled_write_raw_byte(column >> (page * 8), page * OLED_DISPLAY_WIDTH + left + i);
        }
    }
}

void render_screen(int screen)
{
    bool init = screen != last_rendered_screen;

    if (init)
    {
        oled_clear();
        last_rendered_screen = screen;

        // Init
        switch (screen)
        {
        }
    }

    // Update
    switch (screen)
    {
        case USER_SCREEN_INDICATORS:
        {
            static uint8_t last_leds;
            led_t led_state = host_keyboard_led_state();
            if (init || led_state.raw != last_leds)
            {
                last_leds = led_state.raw;
                render_indicator(12, "NUM", led_state.num_lock);
                render_indicator(55, "CAP", led_state.caps_lock);
                render_indicator(98, "ACC", led_state.scroll_lock);
            }
            break;
        }

        case USER_SCREEN_BONGO_CAT:
            oled_write_P(PSTR("Bongo cat"), false);
            break;

        case SYSTEM_SCREEN_LOGO:
            oled_write_P(PSTR("Logo"), false);
            break;

        case SYSTEM_SCREEN_INPUT_LOCK:
            oled_write_P(PSTR("Input lock"), false);
            break;
    }
}

oled_rotation_t oled_init_kb(oled_rotation_t rotation)
{
    logo_timer = timer_read();
    return OLED_ROTATION_180;
}

bool oled_task_kb(void)
{
    if (!oled_task_user())
    {
        return false;
    }

    if (!logo_finished)
    {
        render_screen(SYSTEM_SCREEN_LOGO);
        logo_finished = timer_elapsed(logo_timer) > LOGO_DURATION;
    }
    else if (get_highest_layer(layer_state) >= 4)
    {
        render_screen(SYSTEM_SCREEN_INPUT_LOCK);
    }
    else
    {
        render_screen(selected_user_screen);
    }

    return false;
}
