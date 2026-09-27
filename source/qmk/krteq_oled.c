#include "krteq_oled.h"
#include OLED_FONT_H

#define OLED_PAGES (OLED_DISPLAY_HEIGHT / 8)
#define INPUT_LOCK_LAYER 4

enum screens
{
    USER_SCREEN_INDICATORS,
    USER_SCREEN_BONGO_CAT,
    USER_SCREEN_COUNT,

    SYSTEM_SCREEN_LOGO,
    SYSTEM_SCREEN_INPUT_LOCK
};

static int selected_user_screen = 0;
static int last_rendered_screen = -1;

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

//==============================================================================
// Indicators screen
//==============================================================================

#define INDICATOR_PADDING_X 5
#define INDICATOR_PADDING_Y 3
#define INDICATOR_HEIGHT (OLED_FONT_HEIGHT + INDICATOR_PADDING_Y * 2)
#define INDICATOR_TOP ((OLED_DISPLAY_HEIGHT - INDICATOR_HEIGHT) / 2)
#define INDICATOR_NUM_X 12
#define INDICATOR_CAP_X 55
#define INDICATOR_ACC_X 98

static uint8_t indicator_leds;
static bool indicator_redraw;

static void render_indicator(uint8_t x, const char *label, bool enabled)
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

static void indicators_init(void)
{
    indicator_redraw = true;
}

static void indicators_update(void)
{
    led_t leds = host_keyboard_led_state();
    if (!indicator_redraw && leds.raw == indicator_leds) return;

    indicator_redraw = false;
    indicator_leds = leds.raw;

    render_indicator(INDICATOR_NUM_X, "NUM", leds.num_lock);
    render_indicator(INDICATOR_CAP_X, "CAP", leds.caps_lock);
    render_indicator(INDICATOR_ACC_X, "ACC", leds.scroll_lock);
}

//==============================================================================
// Bongo cat screen
//==============================================================================

static void bongo_cat_update(void)
{
    oled_write_P(PSTR("Bongo cat"), false);
}

//==============================================================================
// Logo screen
//==============================================================================

#define LOGO_DURATION 3000

static uint16_t logo_timer;
static bool logo_finished = false;

static void logo_init(void)
{
    logo_timer = timer_read();
}

static void logo_update(void)
{
    oled_write_P(PSTR("Logo"), false);
    logo_finished = timer_elapsed(logo_timer) > LOGO_DURATION;
}

//==============================================================================
// Input lock screen
//==============================================================================

static void input_lock_update(void)
{
    oled_write_P(PSTR("Input lock"), false);
}

//==============================================================================
// Screen dispatch
//==============================================================================

static void render_screen(int screen)
{
    if (screen != last_rendered_screen)
    {
        last_rendered_screen = screen;
        oled_clear();

        switch (screen)
        {
            case USER_SCREEN_INDICATORS: indicators_init(); break;
            case SYSTEM_SCREEN_LOGO:     logo_init();       break;
        }
    }

    switch (screen)
    {
        case USER_SCREEN_INDICATORS:   indicators_update(); break;
        case USER_SCREEN_BONGO_CAT:    bongo_cat_update();  break;
        case SYSTEM_SCREEN_LOGO:       logo_update();       break;
        case SYSTEM_SCREEN_INPUT_LOCK: input_lock_update(); break;
    }
}

oled_rotation_t oled_init_kb(oled_rotation_t rotation)
{
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
    }
    else if (get_highest_layer(layer_state) >= INPUT_LOCK_LAYER)
    {
        render_screen(SYSTEM_SCREEN_INPUT_LOCK);
    }
    else
    {
        render_screen(selected_user_screen);
    }

    return false;
}
