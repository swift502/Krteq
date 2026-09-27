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
// Drawing
//==============================================================================

// Indexed BMP palette: 0 = black, 1 = white, 2 = transparent
#define IMAGE_TRANSPARENT 2

// BMP header fields are unaligned little endian, so they must be read byte-wise
static uint32_t image_field(const uint8_t *image, uint8_t offset, uint8_t size)
{
    uint32_t value = 0;
    while (size--) value |= (uint32_t)image[offset + size] << (size * 8);
    return value;
}

static void draw_image(const uint8_t *image, uint8_t x, uint8_t y)
{
    uint16_t pixels = image_field(image, 0x0A, 4);
    uint8_t width   = image_field(image, 0x12, 4);
    uint8_t height  = image_field(image, 0x16, 4);
    uint8_t depth   = image_field(image, 0x1C, 2);
    uint8_t stride  = (width * depth + 31) / 32 * 4;

    for (uint8_t iy = 0; iy < height; iy++)
    {
        // BMP rows are stored bottom-up
        const uint8_t *row = image + pixels + (height - 1 - iy) * stride;

        for (uint8_t ix = 0; ix < width; ix++)
        {
            uint16_t bit = ix * depth;
            uint8_t pixel = (row[bit / 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1);
            if (pixel != IMAGE_TRANSPARENT) oled_write_pixel(x + ix, y + iy, pixel);
        }
    }
}

static void draw_text(uint8_t x, uint8_t y, const char *text)
{
    for (uint8_t i = 0; text[i]; i++)
    {
        for (uint8_t ix = 0; ix < OLED_FONT_WIDTH; ix++)
        {
            uint8_t glyph = pgm_read_byte(&font[(text[i] - OLED_FONT_START) * OLED_FONT_WIDTH + ix]);

            for (uint8_t iy = 0; iy < OLED_FONT_HEIGHT; iy++)
            {
                oled_write_pixel(x + i * OLED_FONT_WIDTH + ix, y + iy, glyph >> iy & 1);
            }
        }
    }
}

static void draw_number(uint8_t center_x, uint8_t y, uint16_t value)
{
    char text[6];
    char *digit = text + sizeof(text) - 1;

    *digit = '\0';
    do *--digit = '0' + value % 10; while (value /= 10);

    draw_text(center_x - strlen(digit) * OLED_FONT_WIDTH / 2, y, digit);
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

#define BONGO_CAT_X 65
#define BONGO_CAT_Y 0
#define BONGO_PAW_LEFT_X 44
#define BONGO_PAW_RIGHT_X 93
#define BONGO_PAW_Y 12
#define BONGO_HITS_X (BONGO_PAW_LEFT_X / 2)
#define BONGO_HITS_Y ((OLED_DISPLAY_HEIGHT - OLED_FONT_HEIGHT) / 2)
#define BONGO_PAW_DURATION 150

static const uint8_t bongo_cat_image[] = {
#embed "bitmaps/bongo_cat.bmp"
};

static const uint8_t bongo_paw_up_image[] = {
#embed "bitmaps/bongo_paw_up.bmp"
};

static const uint8_t bongo_paw_down_image[] = {
#embed "bitmaps/bongo_paw_down.bmp"
};

static uint16_t bongo_hits;
static uint16_t bongo_paw_timer;
static bool bongo_right_paw;
static bool bongo_paw_down;
static bool bongo_redraw;

void bongo_key_event(bool pressed)
{
    if (!pressed) return;

    bongo_hits++;
    bongo_right_paw = !bongo_right_paw;
    bongo_paw_timer = timer_read();
    bongo_paw_down = true;
    bongo_redraw = true;
}

static const uint8_t *bongo_paw_image(bool right)
{
    return bongo_paw_down && bongo_right_paw == right ? bongo_paw_down_image : bongo_paw_up_image;
}

static void bongo_cat_init(void)
{
    bongo_redraw = true;
}

static void bongo_cat_update(void)
{
    // Guarded by bongo_paw_down so the elapsed check cannot wrap back into range
    if (bongo_paw_down && timer_elapsed(bongo_paw_timer) > BONGO_PAW_DURATION)
    {
        bongo_paw_down = false;
        bongo_redraw = true;
    }

    if (!bongo_redraw) return;
    bongo_redraw = false;

    oled_clear();
    draw_image(bongo_cat_image, BONGO_CAT_X, BONGO_CAT_Y);
    draw_image(bongo_paw_image(false), BONGO_PAW_LEFT_X, BONGO_PAW_Y);
    draw_image(bongo_paw_image(true), BONGO_PAW_RIGHT_X, BONGO_PAW_Y);
    draw_number(BONGO_HITS_X, BONGO_HITS_Y, bongo_hits);
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
            case USER_SCREEN_BONGO_CAT:  bongo_cat_init();  break;
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
