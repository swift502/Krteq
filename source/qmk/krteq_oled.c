#include <ctype.h>
#include <math.h>
#include <lib/lib8tion/lib8tion.h>
#include "krteq_oled.h"
#include "version.h"
#include OLED_FONT_H

#define OLED_PAGES (OLED_DISPLAY_HEIGHT / 8)
#define FRAME_DURATION_30_FPS 33 // Milliseconds per frame
#define INDICATORS_LAYER 1
#define INPUT_LOCK_LAYER 4

// Offsets into the EEPROM keyboard datablock
#define DATA_SCREEN_OFFSET 0
#define DATA_BONGO_HITS_OFFSET 1

static int current_user_screen = 0;
static const krteq_screen_t *last_rendered_screen = NULL;
static bool screen_redraw;

static uint8_t user_screen_count(void)
{
    uint8_t count = 0;
    while (count < UINT8_MAX && user_screens[count]) count++;
    return count;
}

static const krteq_screen_t *get_user_screen(int index)
{
    if (user_screen_count() == 0) return &krteq_screen_indicators;
    return user_screens[index];
}

static void load_user_screen(void)
{
    uint8_t screen = 0;
    eeconfig_read_kb_datablock(&screen, DATA_SCREEN_OFFSET, sizeof(screen));

    if (screen < user_screen_count()) current_user_screen = screen;
}

static void save_user_screen(void)
{
    uint8_t screen = current_user_screen;
    eeconfig_update_kb_datablock(&screen, DATA_SCREEN_OFFSET, sizeof(screen));
}

// Restarts the timer if it has finished
static bool timer_finished(uint16_t *timer, uint16_t duration)
{
    if (timer_elapsed(*timer) < duration) return false;

    *timer = timer_read();
    return true;
}

//==============================================================================
// Drawing
//==============================================================================

static uint8_t frame_buffer[OLED_PAGES * OLED_DISPLAY_WIDTH];
static bool frame_open; // Drawing goes to frame_buffer while open

static bool on_screen(int16_t x, int16_t y)
{
    return x >= 0 && x < OLED_DISPLAY_WIDTH && y >= 0 && y < OLED_DISPLAY_HEIGHT;
}

static void frame_clear(void)
{
    memset(frame_buffer, 0, sizeof(frame_buffer));
    frame_open = true;
}

static void frame_plot(int16_t x, int16_t y)
{
    if (on_screen(x, y)) frame_buffer[y / 8 * OLED_DISPLAY_WIDTH + x] |= 1 << (y % 8);
}

// Bresenham, stops at the screen edge
static void frame_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    int16_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int16_t dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int8_t sx = x1 > x0 ? 1 : -1;
    int8_t sy = y1 > y0 ? 1 : -1;
    int16_t error = dx + dy;

    while (on_screen(x0, y0))
    {
        frame_plot(x0, y0);
        if (x0 == x1 && y0 == y1) return;

        int16_t doubled = error * 2;
        if (doubled >= dy)
        {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx)
        {
            error += dx;
            y0 += sy;
        }
    }
}

// QMK only marks changed bytes as dirty
static void frame_show(void)
{
    for (uint16_t i = 0; i < sizeof(frame_buffer); i++) oled_write_raw_byte(frame_buffer[i], i);
    frame_open = false;
}

static void draw_pixel(int16_t x, int16_t y, bool on)
{
    if (!frame_open)
    {
        oled_write_pixel(x, y, on);
        return;
    }

    if (!on_screen(x, y)) return;

    uint8_t *byte = &frame_buffer[y / 8 * OLED_DISPLAY_WIDTH + x];
    uint8_t bit = 1 << (y % 8);
    *byte = on ? *byte | bit : *byte & ~bit;
}

// Indexed BMP palette: 0 = black, 1 = white, 2 = transparent
#define IMAGE_TRANSPARENT 2

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
            if (pixel != IMAGE_TRANSPARENT) draw_pixel(x + ix, y + iy, pixel);
        }
    }
}

static uint8_t font_column(char character, uint8_t column)
{
    return pgm_read_byte(&font[(character - OLED_FONT_START) * OLED_FONT_WIDTH + column]);
}

static void draw_text_at(int16_t x, uint8_t y, const char *text, bool inverted)
{
    for (uint8_t i = 0; text[i]; i++)
    {
        for (uint8_t ix = 0; ix < OLED_FONT_WIDTH; ix++)
        {
            int16_t px = x + i * OLED_FONT_WIDTH + ix;
            if (px < 0 || px >= OLED_DISPLAY_WIDTH) continue;

            uint8_t glyph = font_column(text[i], ix);

            for (uint8_t iy = 0; iy < OLED_FONT_HEIGHT; iy++)
            {
                draw_pixel(px, y + iy, (glyph >> iy & 1) != inverted);
            }
        }
    }
}

static void draw_text(int16_t center_x, uint8_t y, const char *text, bool inverted)
{
    draw_text_at(center_x - (int16_t)strlen(text) * OLED_FONT_WIDTH / 2, y, text, inverted);
}

static void fill_rect(uint8_t left, uint8_t top, uint8_t width, uint8_t height, bool on)
{
    for (uint8_t iy = 0; iy < height; iy++)
    {
        for (uint8_t ix = 0; ix < width; ix++)
        {
            draw_pixel(left + ix, top + iy, on);
        }
    }
}

// Box with rounded corners
static void draw_box(uint8_t left, uint8_t top, uint8_t width, uint8_t height, bool filled)
{
    fill_rect(left + 1, top, width - 2, height, filled);
    fill_rect(left, top + 1, width, height - 2, filled);
}

#define ICON_SIZE 14
#define ICON_TEXT_GAP 6
#define ICON_Y 8
#define ICON_TEXT_Y ((OLED_DISPLAY_HEIGHT - OLED_FONT_HEIGHT) / 2)

static void draw_icon_text(const uint8_t *image, const char *text)
{
    uint8_t text_width = strlen(text) * OLED_FONT_WIDTH;
    uint8_t left = (OLED_DISPLAY_WIDTH - (ICON_SIZE + ICON_TEXT_GAP + text_width)) / 2;

    draw_image(image, left, ICON_Y);
    draw_text(left + ICON_SIZE + ICON_TEXT_GAP + text_width / 2, ICON_TEXT_Y, text, false);
}

// Zero padded to width, returns the end of the string
static char *print_number(char *out, uint32_t value, uint8_t width)
{
    char digits[10];
    uint8_t count = 0;

    do digits[count++] = '0' + value % 10; while (value /= 10);
    while (count < width) digits[count++] = '0';

    while (count) *out++ = digits[--count];
    *out = '\0';
    return out;
}

static void draw_number(uint8_t center_x, uint8_t y, uint32_t value)
{
    char text[11];
    print_number(text, value, 1);
    draw_text(center_x, y, text, false);
}

#define TITLE_Y 4
#define CONTENT_Y 20
#define UNNAMED "Undefined"

static void draw_labelled(const char *title, const char *text)
{
    draw_text(OLED_DISPLAY_WIDTH / 2, TITLE_Y, title, false);
    draw_text(OLED_DISPLAY_WIDTH / 2, CONTENT_Y, text, false);
}

#define SELECT_TITLE_Y 0
#define SELECT_NAME_Y 13
#define SELECT_SLIDE_DISTANCE OLED_DISPLAY_WIDTH
#define SELECT_SLIDE_DURATION 200
#define SELECT_FRAME_DURATION FRAME_DURATION_30_FPS
#define SELECT_DOT_WIDTH 7
#define SELECT_DOT_HEIGHT 5
#define SELECT_DOT_Y (OLED_DISPLAY_HEIGHT - SELECT_DOT_HEIGHT / 2 - 1)
#define SELECT_DOT_GAP 8
#define SELECT_PROGRESS_MAX 256

typedef struct
{
    uint8_t from;
    uint8_t to;
    int8_t direction;
    bool animating;
    uint16_t timer;
    uint16_t frame_timer;
} selection_t;

typedef const char *(*selection_name_t)(uint8_t index);

static void selection_reset(selection_t *selection, uint8_t index)
{
    selection->from = selection->to = index;
    selection->animating = false;
}

static void selection_move(selection_t *selection, uint8_t index, int8_t direction)
{
    if (index == selection->to) return;

    selection->from = selection->to;
    selection->to = index;
    selection->direction = direction;
    selection->animating = true;
    selection->timer = selection->frame_timer = timer_read();
}

// Steps to a neighbour slide towards it, wrapping around the ends
static void selection_follow(selection_t *selection, uint8_t index, uint8_t count)
{
    uint8_t from = selection->to;
    int8_t direction = index > from ? 1 : -1;

    if (index == (from + 1) % count) direction = 1;
    else if (index == (from + count - 1) % count) direction = -1;

    selection_move(selection, index, direction);
}

static bool selection_frame(selection_t *selection)
{
    if (!selection->animating || !timer_finished(&selection->frame_timer, SELECT_FRAME_DURATION)) return false;

    if (timer_elapsed(selection->timer) >= SELECT_SLIDE_DURATION) selection->animating = false;
    return true;
}

// Quadratic ease out
static uint16_t selection_progress(const selection_t *selection)
{
    uint16_t elapsed = timer_elapsed(selection->timer);
    if (!selection->animating || elapsed >= SELECT_SLIDE_DURATION) return SELECT_PROGRESS_MAX;

    uint32_t remaining = SELECT_SLIDE_DURATION - elapsed;
    return SELECT_PROGRESS_MAX - SELECT_PROGRESS_MAX * remaining * remaining / (SELECT_SLIDE_DURATION * SELECT_SLIDE_DURATION);
}

static void draw_selection_name(const char *name, int16_t offset)
{
    draw_text(OLED_DISPLAY_WIDTH / 2 + offset, SELECT_NAME_Y, name ? name : UNNAMED, false);
}

static void draw_selection(const char *title, const selection_t *selection, uint8_t count, selection_name_t name)
{
    if (count == 0) count = 1;

    uint16_t progress = selection_progress(selection);
    int16_t shift = SELECT_SLIDE_DISTANCE * progress / SELECT_PROGRESS_MAX;

    draw_text(OLED_DISPLAY_WIDTH / 2, SELECT_TITLE_Y, title, false);

    if (selection->animating) draw_selection_name(name(selection->from), -selection->direction * shift);
    draw_selection_name(name(selection->to), selection->direction * (SELECT_SLIDE_DISTANCE - shift));

    uint8_t gap = count > 1 ? MIN(SELECT_DOT_GAP, (OLED_DISPLAY_WIDTH - SELECT_DOT_WIDTH) / (count - 1)) : 0;
    uint8_t left = OLED_DISPLAY_WIDTH / 2 - (count - 1) * gap / 2;

    for (uint8_t i = 0; i < count; i++) draw_pixel(left + i * gap, SELECT_DOT_Y, true);

    int16_t from_x = left + selection->from * gap;
    int16_t to_x = left + selection->to * gap;
    int16_t dot_x = from_x + (to_x - from_x) * (int16_t)progress / SELECT_PROGRESS_MAX;

    draw_box(dot_x - SELECT_DOT_WIDTH / 2, SELECT_DOT_Y - SELECT_DOT_HEIGHT / 2, SELECT_DOT_WIDTH, SELECT_DOT_HEIGHT, true);
}

//==============================================================================
// Key positions
//==============================================================================

// RGB matrix LED coordinate space
#define KEY_SPACE_WIDTH 224
#define KEY_SPACE_HEIGHT 64

static bool key_position(keypos_t keypos, uint8_t *x, uint8_t *y)
{
    uint8_t led = g_led_config.matrix_co[keypos.row][keypos.col];
    if (led == NO_LED) return false;

    *x = (uint16_t)g_led_config.point[led].x * (OLED_DISPLAY_WIDTH - 1) / KEY_SPACE_WIDTH;
    *y = (uint16_t)g_led_config.point[led].y * (OLED_DISPLAY_HEIGHT - 1) / KEY_SPACE_HEIGHT;
    return true;
}

//==============================================================================
// Random
//==============================================================================

static uint16_t random_value(void)
{
    static uint16_t state;

    if (!state) state = timer_read() | 1;

    state ^= state << 7;
    state ^= state >> 9;
    state ^= state << 8;
    return state;
}

//==============================================================================
// Indicators screen
//==============================================================================

#define INDICATOR_PADDING_X 5
#define INDICATOR_PADDING_Y 3
#define INDICATOR_HEIGHT (OLED_FONT_HEIGHT + INDICATOR_PADDING_Y * 2)
#define INDICATOR_TOP ((OLED_DISPLAY_HEIGHT - INDICATOR_HEIGHT) / 2)
#define INDICATOR_NUM_X 13
#define INDICATOR_CAP_X 55
#define INDICATOR_ACC_X 97

static uint8_t indicator_leds;

static void render_indicator(uint8_t x, const char *label, bool enabled)
{
    uint8_t width = strlen(label) * OLED_FONT_WIDTH;

    draw_box(x - INDICATOR_PADDING_X, INDICATOR_TOP, width + INDICATOR_PADDING_X * 2, INDICATOR_HEIGHT, enabled);
    draw_text(x + width / 2, INDICATOR_TOP + INDICATOR_PADDING_Y, label, enabled);
}

static void indicators_update(void)
{
    led_t leds = host_keyboard_led_state();
    if (!screen_redraw && leds.raw == indicator_leds) return;

    screen_redraw = false;
    indicator_leds = leds.raw;

    render_indicator(INDICATOR_NUM_X, "NUM", leds.num_lock);
    render_indicator(INDICATOR_CAP_X, "CAP", leds.caps_lock);
    render_indicator(INDICATOR_ACC_X, "ACC", leds.scroll_lock);
}

const krteq_screen_t krteq_screen_indicators = {
    .name   = "Indicators",
    .update = indicators_update
};

//==============================================================================
// Bongo cat screen
//==============================================================================

#define BONGO_CAT_X 65
#define BONGO_CAT_Y 0
#define BONGO_PAW_COUNT 2
#define BONGO_PAW_LEFT_X 44
#define BONGO_PAW_RIGHT_X 93
#define BONGO_PAW_Y 12
#define BONGO_HITS_X (BONGO_PAW_LEFT_X / 2)
#define BONGO_HITS_Y ((OLED_DISPLAY_HEIGHT - OLED_FONT_HEIGHT) / 2)
#define BONGO_TROPHY_HITS 1000000 // Set to 10 to debug trophy layout
#define BONGO_TROPHY_WIDTH 10
#define BONGO_TROPHY_HEIGHT 8
#define BONGO_TROPHY_GAP 1
#define BONGO_TROPHY_ROW_HEIGHT (BONGO_TROPHY_HEIGHT > OLED_FONT_HEIGHT ? BONGO_TROPHY_HEIGHT : OLED_FONT_HEIGHT)
#define BONGO_TROPHY_Y (OLED_DISPLAY_HEIGHT - BONGO_TROPHY_HEIGHT)
#define BONGO_TROPHY_TEXT_Y (OLED_DISPLAY_HEIGHT - OLED_FONT_HEIGHT + 1) // Glyphs leave their bottom row blank
#define BONGO_TROPHY_HITS_Y ((OLED_DISPLAY_HEIGHT - BONGO_TROPHY_ROW_HEIGHT - OLED_FONT_HEIGHT) / 2)
#define BONGO_RAISED_DURATION 50
#define BONGO_DOWN_DURATION 150
#define BONGO_BLINK_MIN_DELAY 1000
#define BONGO_BLINK_MAX_DELAY 5000
#define BONGO_BLINK_DURATION 100
#define BONGO_SLEEP_DELAY 60000
#define BONGO_ZZ_X 56
#define BONGO_ZZ_Y 2
#define BONGO_EYE_LEFT_X 76
#define BONGO_EYE_LEFT_Y 21
#define BONGO_EYE_RIGHT_X 101
#define BONGO_EYE_RIGHT_Y 23

enum bongo_paw_states
{
    BONGO_PAW_IDLE,
    BONGO_PAW_RAISED,
    BONGO_PAW_HIT
};

static const uint8_t bongo_cat_image[] = {
#embed "bitmaps/bongo_cat.bmp"
};

static const uint8_t bongo_paw_up_image[] = {
#embed "bitmaps/bongo_paw_up.bmp"
};

static const uint8_t bongo_paw_hit_image[] = {
#embed "bitmaps/bongo_paw_hit.bmp"
};

static const uint8_t bongo_eye_closed_image[] = {
#embed "bitmaps/bongo_eye_closed.bmp"
};

static const uint8_t bongo_paw_down_image[] = {
#embed "bitmaps/bongo_paw_down.bmp"
};

static const uint8_t bongo_zz_image[] = {
#embed "bitmaps/bongo_zz.bmp"
};

static const uint8_t bongo_trophy_image[] = {
#embed "bitmaps/bongo_trophy.bmp"
};

static const uint8_t bongo_paw_x[BONGO_PAW_COUNT] = { BONGO_PAW_LEFT_X, BONGO_PAW_RIGHT_X };

static uint32_t bongo_hits;
static uint32_t bongo_saved_hits;
static uint16_t bongo_last_keycode;
static uint16_t bongo_paw_timer[BONGO_PAW_COUNT];
static uint8_t bongo_paw_state[BONGO_PAW_COUNT];
static uint8_t bongo_paw;
static uint32_t bongo_sleep_timer;
static uint16_t bongo_blink_timer;
static uint16_t bongo_blink_delay;
static bool bongo_blinking;
static bool bongo_sleeping;

static void bongo_schedule_blink(void)
{
    bongo_blinking = false;
    bongo_blink_timer = timer_read();
    bongo_blink_delay = BONGO_BLINK_MIN_DELAY + random_value() % (BONGO_BLINK_MAX_DELAY - BONGO_BLINK_MIN_DELAY + 1);
}

static void bongo_wake(void)
{
    if (bongo_sleeping || bongo_blinking) screen_redraw = true;
    bongo_sleeping = false;
    bongo_sleep_timer = timer_read32();
    bongo_schedule_blink();
}

static void bongo_load_hits(void)
{
    eeconfig_read_kb_datablock(&bongo_hits, DATA_BONGO_HITS_OFFSET, sizeof(bongo_hits));
    bongo_saved_hits = bongo_hits;
}

static void bongo_save_hits(void)
{
    if (bongo_hits == bongo_saved_hits) return;

    bongo_saved_hits = bongo_hits;
    eeconfig_update_kb_datablock(&bongo_hits, DATA_BONGO_HITS_OFFSET, sizeof(bongo_hits));
}

static void advance_paw(uint8_t paw)
{
    switch (bongo_paw_state[paw])
    {
        case BONGO_PAW_RAISED:
            if (timer_elapsed(bongo_paw_timer[paw]) < BONGO_RAISED_DURATION) return;
            bongo_paw_state[paw] = BONGO_PAW_HIT;
            break;

        case BONGO_PAW_HIT:
            if (timer_elapsed(bongo_paw_timer[paw]) < BONGO_DOWN_DURATION) return;
            bongo_paw_state[paw] = BONGO_PAW_IDLE;
            break;

        default:
            return;
    }

    bongo_paw_timer[paw] = timer_read();
    screen_redraw = true;
}

static void advance_idle(void)
{
    if (bongo_sleeping) return;

    if (timer_elapsed32(bongo_sleep_timer) >= BONGO_SLEEP_DELAY)
    {
        bongo_sleeping = true;
        bongo_blinking = false;
        screen_redraw = true;
        bongo_save_hits();
        return;
    }

    for (uint8_t paw = 0; paw < BONGO_PAW_COUNT; paw++)
    {
        if (bongo_paw_state[paw] != BONGO_PAW_IDLE)
        {
            bongo_blink_timer = timer_read();
            return;
        }
    }

    if (bongo_blinking)
    {
        if (timer_elapsed(bongo_blink_timer) < BONGO_BLINK_DURATION) return;
        bongo_schedule_blink();
    }
    else
    {
        if (timer_elapsed(bongo_blink_timer) < bongo_blink_delay) return;
        bongo_blinking = true;
        bongo_blink_timer = timer_read();
    }

    screen_redraw = true;
}

static void draw_trophies(uint32_t trophies)
{
    char text[12];
    strcpy(print_number(text, trophies, 1), "x");

    uint8_t text_width = strlen(text) * OLED_FONT_WIDTH;
    uint8_t left = BONGO_HITS_X - (text_width + BONGO_TROPHY_GAP + BONGO_TROPHY_WIDTH) / 2;

    draw_text_at(left, BONGO_TROPHY_TEXT_Y, text, false);
    draw_image(bongo_trophy_image, left + text_width + BONGO_TROPHY_GAP, BONGO_TROPHY_Y);
}

static void bongo_cat_init(void)
{
    bongo_load_hits();
    bongo_last_keycode = 0;
    bongo_paw = 0;
    bongo_sleeping = false;
    bongo_blinking = false;
}

// 16 bit timers may have wrapped while hidden, so animations are reset
static void bongo_cat_start(void)
{
    memset(bongo_paw_state, BONGO_PAW_IDLE, sizeof(bongo_paw_state));
    bongo_schedule_blink();
    bongo_sleep_timer = timer_read32();
}

static void bongo_cat_update(void)
{
    for (uint8_t paw = 0; paw < BONGO_PAW_COUNT; paw++) advance_paw(paw);
    advance_idle();

    if (!screen_redraw) return;
    screen_redraw = false;

    frame_clear();
    draw_image(bongo_cat_image, BONGO_CAT_X, BONGO_CAT_Y);

    if (bongo_sleeping || bongo_blinking)
    {
        draw_image(bongo_eye_closed_image, BONGO_EYE_LEFT_X, BONGO_EYE_LEFT_Y);
        draw_image(bongo_eye_closed_image, BONGO_EYE_RIGHT_X, BONGO_EYE_RIGHT_Y);
    }

    for (uint8_t paw = 0; paw < BONGO_PAW_COUNT; paw++)
    {
        const uint8_t *image = bongo_paw_up_image;
        if (bongo_sleeping) image = bongo_paw_down_image;
        else if (bongo_paw_state[paw] == BONGO_PAW_HIT) image = bongo_paw_hit_image;

        draw_image(image, bongo_paw_x[paw], BONGO_PAW_Y);
    }

    if (bongo_sleeping) draw_image(bongo_zz_image, BONGO_ZZ_X, BONGO_ZZ_Y);

    uint32_t trophies = bongo_hits / BONGO_TROPHY_HITS;
    uint32_t shown = trophies ? bongo_hits % BONGO_TROPHY_HITS : bongo_hits;

    if (trophies)
    {
        draw_number(BONGO_HITS_X, BONGO_TROPHY_HITS_Y, shown);
        draw_trophies(trophies);
    }
    else
    {
        draw_number(BONGO_HITS_X, BONGO_HITS_Y, shown);
    }

    frame_show();
}

static void bongo_key_event(uint16_t keycode, uint8_t x, uint8_t y, bool pressed)
{
    if (!pressed) return;

    if (bongo_hits < UINT32_MAX) bongo_hits++;
    bongo_wake();

    if (keycode != bongo_last_keycode) bongo_paw ^= 1;
    bongo_last_keycode = keycode;

    // Restarting a raised paw would keep it from ever hitting under fast repeats
    if (bongo_paw_state[bongo_paw] == BONGO_PAW_RAISED) return;

    bongo_paw_state[bongo_paw] = BONGO_PAW_RAISED;
    bongo_paw_timer[bongo_paw] = timer_read();
    screen_redraw = true;
}

const krteq_screen_t krteq_screen_bongo_cat = {
    .name      = "Bongo cat",
    .init      = bongo_cat_init,
    .start     = bongo_cat_start,
    .update    = bongo_cat_update,
    .key_event = bongo_key_event
};

//==============================================================================
// Waves screen
//==============================================================================

#define WAVE_FRAME_DURATION FRAME_DURATION_30_FPS
#define WAVE_IDLE_DURATION 15000
#define WAVE_DAMPING 6 // Height lost per frame, 1/2^n
#define WAVE_DROP_RADIUS 2
#define WAVE_DROP_HEIGHT 1200
#define WAVE_CREST 60 // Height that lights a pixel

// One cell border held at zero
#define WAVE_WIDTH (OLED_DISPLAY_WIDTH + 2)
#define WAVE_HEIGHT (OLED_DISPLAY_HEIGHT + 2)
#define WAVE_CELLS (WAVE_WIDTH * WAVE_HEIGHT)

static int16_t wave_heights[2][WAVE_CELLS];
static uint8_t wave_front;
static uint16_t wave_timer;
static uint16_t wave_idle_timer;

// Takes screen coordinates
static void wave_drop(int16_t x, int16_t y)
{
    for (int8_t dy = -WAVE_DROP_RADIUS; dy <= WAVE_DROP_RADIUS; dy++)
    {
        for (int8_t dx = -WAVE_DROP_RADIUS; dx <= WAVE_DROP_RADIUS; dx++)
        {
            int16_t drop_x = x + 1 + dx;
            int16_t drop_y = y + 1 + dy;

            if (dx * dx + dy * dy > WAVE_DROP_RADIUS * WAVE_DROP_RADIUS) continue;
            if (drop_x < 1 || drop_x >= WAVE_WIDTH - 1) continue;
            if (drop_y < 1 || drop_y >= WAVE_HEIGHT - 1) continue;

            wave_heights[wave_front][drop_y * WAVE_WIDTH + drop_x] = WAVE_DROP_HEIGHT;
        }
    }
}

static void wave_draw(void)
{
    const int16_t *heights = wave_heights[wave_front];

    for (uint8_t page = 0; page < OLED_PAGES; page++)
    {
        for (uint8_t x = 0; x < OLED_DISPLAY_WIDTH; x++)
        {
            uint8_t column = 0;

            for (uint8_t bit = 0; bit < 8; bit++)
            {
                if (heights[(page * 8 + bit + 1) * WAVE_WIDTH + x + 1] > WAVE_CREST) column |= 1 << bit;
            }

            oled_write_raw_byte(column, page * OLED_DISPLAY_WIDTH + x);
        }
    }
}

static void wave_init(void)
{
    memset(wave_heights, 0, sizeof(wave_heights));
    wave_front = 0;
    wave_drop(OLED_DISPLAY_WIDTH / 2, OLED_DISPLAY_HEIGHT / 2);
}

static void wave_start(void)
{
    wave_timer = timer_read();
    wave_idle_timer = timer_read();
}

static void wave_update(void)
{
    if (!timer_finished(&wave_timer, WAVE_FRAME_DURATION) && !screen_redraw) return;
    screen_redraw = false;

    if (timer_finished(&wave_idle_timer, WAVE_IDLE_DURATION))
    {
        wave_drop(random_value() % OLED_DISPLAY_WIDTH, random_value() % OLED_DISPLAY_HEIGHT);
    }

    const int16_t *previous = wave_heights[wave_front];
    int16_t *current = wave_heights[wave_front ^= 1];

    for (uint8_t y = 1; y < WAVE_HEIGHT - 1; y++)
    {
        for (uint8_t x = 1; x < WAVE_WIDTH - 1; x++)
        {
            uint16_t cell = y * WAVE_WIDTH + x;
            int16_t height = (previous[cell - 1] + previous[cell + 1] +
                              previous[cell - WAVE_WIDTH] + previous[cell + WAVE_WIDTH]) / 2 - current[cell];

            current[cell] = height - (height >> WAVE_DAMPING);
        }
    }

    wave_draw();
}

static void wave_key_event(uint16_t keycode, uint8_t x, uint8_t y, bool pressed)
{
    if (!pressed) return;

    wave_idle_timer = timer_read();
    wave_drop(x, y);
}

const krteq_screen_t krteq_screen_waves = {
    .name      = "Waves",
    .init      = wave_init,
    .start     = wave_start,
    .update    = wave_update,
    .key_event = wave_key_event
};

//==============================================================================
// Cursor screen
//==============================================================================

// Idles along a lemniscate of Gerono, x = cos t, y = sin 2t, which keeps round ends when stretched wide
// Positions are in pixels, pixel centres sit on whole numbers, speeds are per frame

#define CURSOR_FRAME_DURATION FRAME_DURATION_30_FPS
#define CURSOR_CHASE_SPRING 0.1f // Higher value makes it move to a pressed key faster, 0 to 4 - 2 * damping
#define CURSOR_CHASE_DAMPING 0.35f // Higher value makes it overshoot the key less, 0 (no damping) to 1 (stops dead every frame)
#define CURSOR_CHASE_LAUNCH 7.0f // Higher value makes it shoot off towards a pressed key harder, speed in pixels per frame added towards the key
#define CURSOR_CHASE_LAUNCH_SCALE 0.08f // Higher value makes the kick grow more with distance, launch * (1 + distance * scale)
#define CURSOR_TRACK_SPRING 0.02f // Higher value makes it rejoin the idle path faster, 0 to 4 - 2 * damping
#define CURSOR_TRACK_DAMPING 0.28f // Higher value makes it overshoot the idle path less, 0 (no damping) to 1 (stops dead every frame)
#define CURSOR_WALL_BOUNCE 0.15f // Higher value makes it bounce off the screen edge harder, share of speed kept, 0 (stops dead) to 1 (no loss)
#define CURSOR_PATH_WIDTH 40.0f // Higher value makes the infinity symbol wider, half width in pixels
#define CURSOR_PATH_HEIGHT 8.0f // Higher value makes the infinity symbol taller, half height in pixels
#define CURSOR_PATH_SPEED 0.91f // Higher value makes it move along the idle path faster, pixels per frame
#define CURSOR_PATH_ACCELERATION 0.012f // Higher value makes it get up to idle speed faster
#define CURSOR_PATH_SAMPLES 64 // Points checked for the nearest place to rejoin the path
#define CURSOR_MAX_RADIUS 5.0f // Radius when still
#define CURSOR_MIN_RADIUS 2.5f // Radius at the fast speed
#define CURSOR_FAST_SPEED 6.0f // Higher value makes it shrink less when moving, pixels per frame
#define CURSOR_BLUR_SPEED 1.5f // Higher value makes it skip the motion blur at faster idle speeds, pixels per frame
#define CURSOR_MIN_X CURSOR_MAX_RADIUS
#define CURSOR_MAX_X (OLED_DISPLAY_WIDTH - 1 - CURSOR_MAX_RADIUS)
#define CURSOR_MIN_Y CURSOR_MAX_RADIUS
#define CURSOR_MAX_Y (OLED_DISPLAY_HEIGHT - 1 - CURSOR_MAX_RADIUS)
#define CURSOR_TURN (2.0f * (float)M_PI)

static float cursor_x, cursor_y;
static float cursor_vx, cursor_vy;
static float cursor_previous_x, cursor_previous_y;
static float cursor_move_x, cursor_move_y; // Movement this frame
static float cursor_move_scale; // Inverse squared length of the movement, 0 when still
static float cursor_target_x, cursor_target_y;
static float cursor_radius;
static float cursor_phase; // Radians along the path
static float cursor_path_speed;
static float cursor_path_x, cursor_path_y; // Last target on the path
static uint8_t cursor_held_keys;
static bool cursor_chasing;
static uint16_t cursor_timer;

static void cursor_path(float phase, float *x, float *y)
{
    *x = OLED_DISPLAY_WIDTH / 2 + CURSOR_PATH_WIDTH * cosf(phase);
    *y = OLED_DISPLAY_HEIGHT / 2 + CURSOR_PATH_HEIGHT * sinf(2.0f * phase);
}

// Pixels travelled per radian of phase, from the derivative of the path
static float cursor_path_rate(float phase)
{
    float dx = -CURSOR_PATH_WIDTH * sinf(phase);
    float dy = 2.0f * CURSOR_PATH_HEIGHT * cosf(2.0f * phase);

    return sqrtf(dx * dx + dy * dy);
}

// Advances by the path speed in pixels, so the speed is even along the whole path
// The rate is taken at the midpoint of the step, which keeps the distance accurate on curves
static void cursor_path_advance(void)
{
    float step = cursor_path_speed / cursor_path_rate(cursor_phase);

    cursor_phase += cursor_path_speed / cursor_path_rate(cursor_phase + step / 2.0f);
    if (cursor_phase >= CURSOR_TURN) cursor_phase -= CURSOR_TURN;
}

// Starts from rest at the nearest point, so the cursor glides off instead of being yanked
static void cursor_rejoin_path(void)
{
    float nearest = INFINITY;

    for (uint8_t i = 0; i < CURSOR_PATH_SAMPLES; i++)
    {
        float phase = i * CURSOR_TURN / CURSOR_PATH_SAMPLES;
        float x, y;
        cursor_path(phase, &x, &y);

        float distance = (x - cursor_x) * (x - cursor_x) + (y - cursor_y) * (y - cursor_y);
        if (distance < nearest)
        {
            nearest = distance;
            cursor_phase = phase;
        }
    }

    cursor_path(cursor_phase, &cursor_path_x, &cursor_path_y);
    cursor_path_speed = 0;
}

// Spring damper step, semi-implicit Euler, the target velocity and acceleration let the cursor follow a moving target exactly
static float cursor_spring(float *position, float velocity, float target, float target_velocity, float target_acceleration)
{
    float spring = cursor_chasing ? CURSOR_CHASE_SPRING : CURSOR_TRACK_SPRING;
    float damping = cursor_chasing ? CURSOR_CHASE_DAMPING : CURSOR_TRACK_DAMPING;

    velocity += target_acceleration + spring * (target - *position) + damping * (target_velocity - velocity);
    *position += velocity;

    return velocity;
}

// Returns whether the position hit the edge, flipping the velocity away from it
static bool cursor_wall(float *position, float *velocity, float minimum, float maximum)
{
    if (*position >= minimum && *position <= maximum) return false;

    *position = MIN(MAX(*position, minimum), maximum);
    *velocity = -*velocity;
    return true;
}

// Bounces off the screen edge, losing speed in every direction
static void cursor_bounce(void)
{
    bool hit_x = cursor_wall(&cursor_x, &cursor_vx, CURSOR_MIN_X, CURSOR_MAX_X);
    bool hit_y = cursor_wall(&cursor_y, &cursor_vy, CURSOR_MIN_Y, CURSOR_MAX_Y);

    if (hit_x || hit_y)
    {
        cursor_vx *= CURSOR_WALL_BOUNCE;
        cursor_vy *= CURSOR_WALL_BOUNCE;
    }
}

static void cursor_move(void)
{
    if (cursor_chasing)
    {
        cursor_vx = cursor_spring(&cursor_x, cursor_vx, cursor_target_x, 0, 0);
        cursor_vy = cursor_spring(&cursor_y, cursor_vy, cursor_target_y, 0, 0);
        cursor_bounce();
        return;
    }

    cursor_path_speed = MIN(cursor_path_speed + CURSOR_PATH_ACCELERATION, CURSOR_PATH_SPEED);

    float x, y, next_x, next_y;
    cursor_path(cursor_phase, &x, &y);
    cursor_path_advance();
    cursor_path(cursor_phase, &next_x, &next_y);

    float previous_x = cursor_path_x;
    float previous_y = cursor_path_y;
    cursor_path_x = x;
    cursor_path_y = y;

    cursor_vx = cursor_spring(&cursor_x, cursor_vx, x, x - previous_x, next_x - 2.0f * x + previous_x);
    cursor_vy = cursor_spring(&cursor_y, cursor_vy, y, y - previous_y, next_y - 2.0f * y + previous_y);
    cursor_bounce();
}

static void cursor_resize(void)
{
    float speed = MIN(sqrtf(cursor_vx * cursor_vx + cursor_vy * cursor_vy), CURSOR_FAST_SPEED);
    cursor_radius = CURSOR_MAX_RADIUS - (CURSOR_MAX_RADIUS - CURSOR_MIN_RADIUS) * speed / CURSOR_FAST_SPEED;
}

// Measures the movement this frame once, so per pixel checks avoid division
static void cursor_sweep(void)
{
    // A still ball when idling slowly, the blur would only smear it
    if (!cursor_chasing && cursor_vx * cursor_vx + cursor_vy * cursor_vy < CURSOR_BLUR_SPEED * CURSOR_BLUR_SPEED)
    {
        cursor_previous_x = cursor_x;
        cursor_previous_y = cursor_y;
    }

    cursor_move_x = cursor_x - cursor_previous_x;
    cursor_move_y = cursor_y - cursor_previous_y;

    float length = cursor_move_x * cursor_move_x + cursor_move_y * cursor_move_y;
    cursor_move_scale = length > 0 ? 1.0f / length : 0;
}

// Squared distance to the path covered this frame, from the previous position to the current one
static float cursor_swept_distance(float x, float y)
{
    float dx = x - cursor_previous_x;
    float dy = y - cursor_previous_y;

    // Moves to the nearest point on the segment
    float along = MIN(MAX((dx * cursor_move_x + dy * cursor_move_y) * cursor_move_scale, 0.0f), 1.0f);
    dx -= cursor_move_x * along;
    dy -= cursor_move_y * along;

    return dx * dx + dy * dy;
}

// Capsule covering the movement this frame, a frame of motion blur
static void cursor_draw(void)
{
    frame_clear();

    float reach = cursor_radius * cursor_radius;
    int16_t left = floorf(MIN(cursor_previous_x, cursor_x) - cursor_radius);
    int16_t right = ceilf(MAX(cursor_previous_x, cursor_x) + cursor_radius);
    int16_t top = floorf(MIN(cursor_previous_y, cursor_y) - cursor_radius);
    int16_t bottom = ceilf(MAX(cursor_previous_y, cursor_y) + cursor_radius);

    for (int16_t py = top; py <= bottom; py++)
    {
        for (int16_t px = left; px <= right; px++)
        {
            if (cursor_swept_distance(px, py) <= reach) frame_plot(px, py);
        }
    }

    frame_show();
}

static void cursor_init(void)
{
    cursor_phase = 0;
    cursor_path_speed = CURSOR_PATH_SPEED;
    cursor_path(cursor_phase, &cursor_x, &cursor_y);
    cursor_path_x = cursor_x;
    cursor_path_y = cursor_y;
    cursor_previous_x = cursor_x;
    cursor_previous_y = cursor_y;
    cursor_vx = 0;
    cursor_vy = 0;
    cursor_radius = CURSOR_MAX_RADIUS;
    cursor_chasing = false;
}

static void cursor_start(void)
{
    cursor_timer = timer_read();

    // Keys may have been released while another screen was showing
    cursor_held_keys = 0;
}

static void cursor_update(void)
{
    if (!timer_finished(&cursor_timer, CURSOR_FRAME_DURATION) && !screen_redraw) return;
    screen_redraw = false;

    cursor_previous_x = cursor_x;
    cursor_previous_y = cursor_y;

    cursor_move();
    cursor_resize();
    cursor_sweep();

    // Checks the whole movement, so a fast cursor can't skip over the target between frames
    if (cursor_chasing && !cursor_held_keys && cursor_swept_distance(cursor_target_x, cursor_target_y) <= cursor_radius * cursor_radius)
    {
        cursor_chasing = false;
        cursor_rejoin_path();
    }

    cursor_draw();
}

static void cursor_key_event(uint16_t keycode, uint8_t x, uint8_t y, bool pressed)
{
    if (!pressed)
    {
        if (cursor_held_keys) cursor_held_keys--;
        return;
    }

    if (cursor_held_keys < UINT8_MAX) cursor_held_keys++;

    cursor_chasing = true;
    cursor_target_x = MIN(MAX(x, CURSOR_MIN_X), CURSOR_MAX_X);
    cursor_target_y = MIN(MAX(y, CURSOR_MIN_Y), CURSOR_MAX_Y);

    // Adds to the current velocity, so it keeps some momentum while heading for the key
    float dx = cursor_target_x - cursor_x;
    float dy = cursor_target_y - cursor_y;
    float distance = sqrtf(dx * dx + dy * dy);

    if (distance > 0)
    {
        float launch = CURSOR_CHASE_LAUNCH * (1.0f + distance * CURSOR_CHASE_LAUNCH_SCALE);
        cursor_vx += dx * launch / distance;
        cursor_vy += dy * launch / distance;
    }
}

const krteq_screen_t krteq_screen_cursor = {
    .name      = "Cursor",
    .init      = cursor_init,
    .start     = cursor_start,
    .update    = cursor_update,
    .key_event = cursor_key_event
};

//==============================================================================
// Starfield screen
//==============================================================================

#define STARFIELD_FRAME_DURATION FRAME_DURATION_30_FPS
#define STARFIELD_COUNT 48
#define STARFIELD_SHIFT 8 // Fixed point fractional bits
#define STARFIELD_FAR_DEPTH 1024
#define STARFIELD_NEAR_DEPTH 8
#define STARFIELD_NEAR ((int32_t)STARFIELD_NEAR_DEPTH << STARFIELD_SHIFT)
#define STARFIELD_FOCAL OLED_DISPLAY_WIDTH
// Fills the screen at the far plane
#define STARFIELD_SPREAD_X (STARFIELD_FAR_DEPTH * OLED_DISPLAY_WIDTH / 2 / STARFIELD_FOCAL)
#define STARFIELD_SPREAD_Y (STARFIELD_FAR_DEPTH * OLED_DISPLAY_HEIGHT / 2 / STARFIELD_FOCAL)
#define STARFIELD_BASE_SPEED (6 << STARFIELD_SHIFT)
#define STARFIELD_MAX_BOOST (64 << STARFIELD_SHIFT)
#define STARFIELD_KICK (3 << STARFIELD_SHIFT)
#define STARFIELD_DRAG 6

struct star
{
    int16_t x;
    int16_t y;
    int32_t z;
    int16_t trail_x; // Last drawn screen position
    int16_t trail_y;
};

static struct star starfield[STARFIELD_COUNT];
static uint16_t starfield_timer;
static int32_t starfield_boost;


static void starfield_project(const struct star *star, int16_t *x, int16_t *y)
{
    *x = OLED_DISPLAY_WIDTH / 2 + ((int32_t)star->x * STARFIELD_FOCAL << STARFIELD_SHIFT) / star->z;
    *y = OLED_DISPLAY_HEIGHT / 2 + ((int32_t)star->y * STARFIELD_FOCAL << STARFIELD_SHIFT) / star->z;
}

static void starfield_spawn(struct star *star, int32_t z)
{
    star->x = (int16_t)(random_value() % (STARFIELD_SPREAD_X * 2 + 1)) - STARFIELD_SPREAD_X;
    star->y = (int16_t)(random_value() % (STARFIELD_SPREAD_Y * 2 + 1)) - STARFIELD_SPREAD_Y;
    star->z = z;
    starfield_project(star, &star->trail_x, &star->trail_y);
}

static void starfield_init(void)
{
    for (uint8_t i = 0; i < STARFIELD_COUNT; i++)
    {
        int32_t depth = STARFIELD_NEAR_DEPTH + random_value() % (STARFIELD_FAR_DEPTH - STARFIELD_NEAR_DEPTH);
        starfield_spawn(&starfield[i], depth << STARFIELD_SHIFT);
    }

    starfield_boost = 0;
}

static void starfield_start(void)
{
    starfield_timer = timer_read();
}

static void starfield_update(void)
{
    if (!timer_finished(&starfield_timer, STARFIELD_FRAME_DURATION) && !screen_redraw) return;
    screen_redraw = false;

    int32_t speed = STARFIELD_BASE_SPEED + starfield_boost;

    // Rounded up so the boost reaches zero
    starfield_boost -= (starfield_boost + (1 << STARFIELD_DRAG) - 1) >> STARFIELD_DRAG;

    frame_clear();

    for (uint8_t i = 0; i < STARFIELD_COUNT; i++)
    {
        struct star *star = &starfield[i];

        star->z -= speed;

        if (star->z >= STARFIELD_NEAR)
        {
            int16_t x, y;
            starfield_project(star, &x, &y);

            frame_line(star->trail_x, star->trail_y, x, y);
            star->trail_x = x;
            star->trail_y = y;

            if (on_screen(x, y)) continue;
        }

        // Depth jitter keeps stars respawned together from moving as a sheet
        starfield_spawn(star, (int32_t)(STARFIELD_FAR_DEPTH - random_value() % 64) << STARFIELD_SHIFT);
    }

    frame_show();
}

static void starfield_key_event(uint16_t keycode, uint8_t x, uint8_t y, bool pressed)
{
    if (!pressed) return;

    starfield_boost += STARFIELD_KICK;
    if (starfield_boost > STARFIELD_MAX_BOOST) starfield_boost = STARFIELD_MAX_BOOST;
}

const krteq_screen_t krteq_screen_starfield = {
    .name      = "Starfield",
    .init      = starfield_init,
    .start     = starfield_start,
    .update    = starfield_update,
    .key_event = starfield_key_event
};

//==============================================================================
// Game of life screen
//==============================================================================

#define LIFE_FRAME_DURATION 100
#define LIFE_IDLE_DURATION 5000
#define LIFE_MARGIN_X 6
#define LIFE_MARGIN_Y 8
#define LIFE_GLIDER_Y 2
#define LIFE_WIDTH (OLED_DISPLAY_WIDTH + LIFE_MARGIN_X * 2)
#define LIFE_HEIGHT (OLED_DISPLAY_HEIGHT + LIFE_MARGIN_Y * 2)
#define LIFE_PAGES (LIFE_HEIGHT / 8)
#define LIFE_BYTES (LIFE_PAGES * LIFE_WIDTH)

_Static_assert(LIFE_MARGIN_Y % 8 == 0, "The life margin must be a whole number of pages tall");

static uint8_t life_cells[LIFE_BYTES];
static uint8_t life_next[LIFE_BYTES];
static uint16_t life_timer;
static uint16_t life_idle_timer;

static void life_add_cell(int16_t x, int16_t y)
{
    if (x < 0 || x >= LIFE_WIDTH || y < 0 || y >= LIFE_HEIGHT) return;
    life_cells[y / 8 * LIFE_WIDTH + x] |= 1 << (y % 8);
}

enum bomb_shapes
{
    BOMB_SHAPE_HORIZONTAL,
    BOMB_SHAPE_VERTICAL
};

static void life_bomb(int16_t x, int16_t y, uint8_t shape)
{
    for (int8_t side = -1; side <= 1; side += 2)
    {
        for (int8_t dy = -1; dy <= 1; dy++)
        {
            for (int8_t dx = -1; dx <= 1; dx++)
            {
                if (!dx && !dy) continue;

                switch (shape)
                {
                    case BOMB_SHAPE_HORIZONTAL: life_add_cell(x + side * 2 + dx, y + dy); break;
                    case BOMB_SHAPE_VERTICAL:   life_add_cell(x + dx, y + side * 2 + dy); break;
                }
            }
        }
    }
}

// Glider heading down and right
static const uint8_t life_glider[3] = { 0b010, 0b001, 0b111 };

static void life_spawn_glider(void)
{
    bool downward = random_value() & 1;
    bool rightward = random_value() & 1;
    uint8_t x = 1 + random_value() % (LIFE_WIDTH - 5);
    uint8_t y = downward ? LIFE_GLIDER_Y : LIFE_HEIGHT - 3 - LIFE_GLIDER_Y;

    for (uint8_t row = 0; row < 3; row++)
    {
        for (uint8_t column = 0; column < 3; column++)
        {
            if (!(life_glider[row] >> (2 - column) & 1)) continue;
            life_add_cell(x + (rightward ? column : 2 - column), y + (downward ? row : 2 - row));
        }
    }
}

// Page column in bits 8-15, with the neighbouring pages' bytes below and above
static uint32_t life_column(uint8_t page, int16_t x)
{
    if (x < 0 || x >= LIFE_WIDTH) return 0;

    const uint8_t *column = life_cells + page * LIFE_WIDTH + x;
    uint32_t word = (uint32_t)column[0] << 8;

    if (page > 0) word |= column[-LIFE_WIDTH];
    if (page < LIFE_PAGES - 1) word |= (uint32_t)column[LIFE_WIDTH] << 16;
    return word;
}

// Counts neighbours of a whole page column at once, bitwise. Modulo 8 is enough, 8 neighbours dies like 0.
static uint8_t life_step(uint32_t left, uint32_t centre, uint32_t right)
{
    // Each side column sums its three cells, the centre only its two
    uint32_t left_low   = (left << 1) ^ left ^ (left >> 1);
    uint32_t left_high  = ((left << 1) & left) | ((left >> 1) & ((left << 1) ^ left));
    uint32_t right_low  = (right << 1) ^ right ^ (right >> 1);
    uint32_t right_high = ((right << 1) & right) | ((right >> 1) & ((right << 1) ^ right));
    uint32_t centre_low  = (centre << 1) ^ (centre >> 1);
    uint32_t centre_high = (centre << 1) & (centre >> 1);

    uint32_t ones = left_low ^ right_low ^ centre_low;
    uint32_t carry = (left_low & right_low) | (centre_low & (left_low ^ right_low));
    uint32_t twos_sum = left_high ^ right_high ^ centre_high;
    uint32_t twos_carry = (left_high & right_high) | (centre_high & (left_high ^ right_high));
    uint32_t twos = twos_sum ^ carry;
    uint32_t fours = twos_carry ^ (twos_sum & carry);

    // Born with 3, survives with 2 or 3
    return (twos & ~fours & (ones | centre)) >> 8;
}

static void life_draw(void)
{
    for (uint8_t page = 0; page < OLED_PAGES; page++)
    {
        const uint8_t *row = life_cells + (page + LIFE_MARGIN_Y / 8) * LIFE_WIDTH + LIFE_MARGIN_X;

        for (uint8_t x = 0; x < OLED_DISPLAY_WIDTH; x++)
        {
            oled_write_raw_byte(row[x], page * OLED_DISPLAY_WIDTH + x);
        }
    }
}

static void life_init(void)
{
    memset(life_cells, 0, sizeof(life_cells));
    life_bomb(LIFE_WIDTH / 2, LIFE_HEIGHT / 2, BOMB_SHAPE_VERTICAL);
}

static void life_start(void)
{
    life_timer = timer_read();
    life_idle_timer = timer_read();
}

static void life_update(void)
{
    if (!timer_finished(&life_timer, LIFE_FRAME_DURATION) && !screen_redraw) return;
    screen_redraw = false;

    for (uint8_t page = 0; page < LIFE_PAGES; page++)
    {
        uint32_t left = 0;
        uint32_t centre = life_column(page, 0);

        for (uint8_t x = 0; x < LIFE_WIDTH; x++)
        {
            uint32_t right = life_column(page, x + 1);
            life_next[page * LIFE_WIDTH + x] = life_step(left, centre, right);
            left = centre;
            centre = right;
        }
    }

    memcpy(life_cells, life_next, sizeof(life_cells));

    if (timer_finished(&life_idle_timer, LIFE_IDLE_DURATION))
    {
        life_spawn_glider();
    }

    life_draw();
}

static void life_key_event(uint16_t keycode, uint8_t x, uint8_t y, bool pressed)
{
    if (!pressed) return;

    life_idle_timer = timer_read();
    life_bomb(x + LIFE_MARGIN_X, y + LIFE_MARGIN_Y, BOMB_SHAPE_HORIZONTAL);
}

const krteq_screen_t krteq_screen_game_of_life = {
    .name      = "Game of life",
    .init      = life_init,
    .start     = life_start,
    .update    = life_update,
    .key_event = life_key_event
};

//==============================================================================
// System info screen
//==============================================================================

#define INFO_INTERVAL 1000
#define INFO_ROW_HEIGHT 8
#define INFO_BUILD_DATE_LENGTH 10 // Date part of QMK_BUILDDATE

_Static_assert(OLED_DISPLAY_HEIGHT / INFO_ROW_HEIGHT >= 4, "The system info screen needs four rows of text");

static uint16_t info_timer;

static uint16_t bcd_value(uint16_t bcd)
{
    uint16_t value = 0;
    for (int8_t shift = 12; shift >= 0; shift -= 4) value = value * 10 + (bcd >> shift & 0xF);
    return value;
}

static void print_version(char *out, uint16_t major, uint16_t minor, uint16_t patch)
{
    out = print_number(out, major, 1);
    *out++ = '.';
    out = print_number(out, minor, 1);
    *out++ = '.';
    print_number(out, patch, 1);
}

// DEVICE_VER packs the keyboard.json version as BCD, two digits of major and one each of the rest
static void print_device_version(char *out)
{
    *out++ = 'v';
    print_version(out, bcd_value(DEVICE_VER >> 8 & 0xFF), DEVICE_VER >> 4 & 0xF, DEVICE_VER & 0xF);
}

// QMK_VERSION_BCD packs major and minor into a byte each and patch into the low half
static void print_qmk_version(char *out)
{
    print_version(out, bcd_value(QMK_VERSION_BCD >> 24 & 0xFF), bcd_value(QMK_VERSION_BCD >> 16 & 0xFF), bcd_value(QMK_VERSION_BCD & 0xFFFF));
}

static void print_uptime(char *out)
{
    uint32_t seconds = timer_read32() / 1000;

    if (seconds >= 24 * 60 * 60)
    {
        out = print_number(out, seconds / (24 * 60 * 60), 1);
        *out++ = 'd';
        *out++ = ' ';
    }

    out = print_number(out, seconds / (60 * 60) % 24, 2);
    *out++ = ':';
    out = print_number(out, seconds / 60 % 60, 2);
    *out++ = ':';
    print_number(out, seconds % 60, 2);
}

static void render_info_row(uint8_t row, const char *label, const char *value)
{
    uint8_t y = row * INFO_ROW_HEIGHT;
    uint8_t width = strlen(value) * OLED_FONT_WIDTH;

    draw_text_at(0, y, label, false);
    draw_text_at(width < OLED_DISPLAY_WIDTH ? OLED_DISPLAY_WIDTH - width : 0, y, value, false);
}

static void system_info_update(void)
{
    if (!timer_finished(&info_timer, INFO_INTERVAL) && !screen_redraw) return;
    screen_redraw = false;

    char device_version[12];
    print_device_version(device_version);

    char uptime[16];
    print_uptime(uptime);

    char build_date[INFO_BUILD_DATE_LENGTH + 1];
    memcpy(build_date, QMK_BUILDDATE, INFO_BUILD_DATE_LENGTH);
    build_date[INFO_BUILD_DATE_LENGTH] = '\0';

    char qmk_version[16];
    print_qmk_version(qmk_version);

    frame_clear();
    render_info_row(0, PRODUCT, device_version);
    render_info_row(1, "Uptime", uptime);
    render_info_row(2, "QMK", qmk_version);
    render_info_row(3, "Built", build_date);
    frame_show();
}

const krteq_screen_t krteq_screen_system_info = {
    .name   = "System info",
    .update = system_info_update
};

//==============================================================================
// Logo screen
//==============================================================================

#define LOGO_DURATION 3000
#define LOGO_X 20
#define LOGO_Y 2

static const uint8_t logo_image[] = {
#embed "bitmaps/logo.bmp"
};

static uint16_t logo_timer;
static bool logo_finished = false;

static void logo_start(void)
{
    logo_timer = timer_read();
    draw_image(logo_image, LOGO_X, LOGO_Y);
}

static void logo_update(void)
{
    logo_finished = timer_elapsed(logo_timer) > LOGO_DURATION;
}

static const krteq_screen_t system_screen_logo = {
    .start  = logo_start,
    .update = logo_update
};

//==============================================================================
// Overlays
//==============================================================================

static const krteq_screen_t system_screen_select;
static selection_t screen_selection;

static const krteq_screen_t *overlay = NULL;
static uint16_t overlay_timer;
static uint16_t overlay_duration;

static void close_overlay(void)
{
    if (overlay == &system_screen_select && screen_selection.to != current_user_screen)
    {
        bongo_save_hits();
        current_user_screen = screen_selection.to;
        save_user_screen();
    }

    overlay = NULL;
}

static void open_overlay(const krteq_screen_t *screen, uint16_t duration)
{
    if (overlay != screen) close_overlay();

    overlay = screen;
    overlay_timer = timer_read();
    overlay_duration = duration;
}

//==============================================================================
// Screen select screen
//==============================================================================

#define SELECT_DURATION 1500

static const char *user_screen_name(uint8_t index)
{
    return get_user_screen(index)->name;
}

static void start_selecting(void)
{
    if (overlay != &system_screen_select) selection_reset(&screen_selection, current_user_screen);

    open_overlay(&system_screen_select, SELECT_DURATION);
    screen_redraw = true;
}

void default_user_screen(void)
{
    start_selecting();
    selection_move(&screen_selection, 0, -1);
}

void shift_user_screen(int shift)
{
    start_selecting();

    int count = user_screen_count();
    if (count == 0) return;

    int index = (screen_selection.to + shift) % count;
    if (index < 0) index += count;

    selection_move(&screen_selection, index, shift < 0 ? -1 : 1);
}

static void screen_select_update(void)
{
    if (!selection_frame(&screen_selection) && !screen_redraw) return;
    screen_redraw = false;

    frame_clear();
    draw_selection("Screen", &screen_selection, user_screen_count(), user_screen_name);
    frame_show();
}

static const krteq_screen_t system_screen_select = {
    .update = screen_select_update
};

//==============================================================================
// Input lock screen
//==============================================================================

static const uint8_t input_lock_image[] = {
#embed "bitmaps/input_lock.bmp"
};

static void input_lock_start(void)
{
    draw_icon_text(input_lock_image, "Input lock");
}

static const krteq_screen_t system_screen_input_lock = {
    .start = input_lock_start
};

//==============================================================================
// RGB status screen
//==============================================================================

#define RGB_STATUS_DURATION 3000
#define RGB_BAR_WIDTH OLED_DISPLAY_WIDTH
#define RGB_BAR_HEIGHT 7
#define RGB_BAR_PADDING 2
#define RGB_HUE_MAXIMUM (UINT8_MAX + 1 - RGB_MATRIX_HUE_STEP) // Hue wraps, so the step before 0 is the top

static const krteq_screen_t system_screen_rgb;

enum rgb_status_components
{
    RGB_STATUS_NONE = -1,
    RGB_STATUS_POWER,
    RGB_STATUS_EFFECT,
    RGB_STATUS_HUE,
    RGB_STATUS_SATURATION,
    RGB_STATUS_BRIGHTNESS,
    RGB_STATUS_SPEED
};

static int8_t rgb_status_component;

// Mode 0 is RGB_MATRIX_NONE, so effects are counted from mode 1
#define RGB_EFFECT_COUNT (RGB_MATRIX_EFFECT_MAX - 1)

static selection_t rgb_effect_selection;

static uint8_t rgb_effect_index(void)
{
    uint8_t mode = rgb_matrix_get_mode();
    return mode > 0 && mode < RGB_MATRIX_EFFECT_MAX ? mode - 1 : 0;
}

static const char *get_effect_display_name(uint8_t index)
{
    static char display_name[32];
    const char *mode = rgb_matrix_get_mode_name(index + 1);

    // Shorten long names
    if (strncmp(mode, "SOLID_REACTIVE", strlen("SOLID_REACTIVE")) == 0) mode += strlen("SOLID_");
    if (strcmp(mode, "RAINBOW_MOVING_CHEVRON") == 0) mode = "RAINBOW_CHEVRON";

    uint8_t i = 0;
    for (; mode[i] && i < sizeof(display_name) - 1; i++)
    {
        char c = mode[i];
        if (c == '_') c = ' ';
        else if (i > 0) c = tolower(c);

        display_name[i] = c;
    }
    display_name[i] = '\0';

    return display_name;
}

// Runs before the effect changes, so the selection starts from the old effect
static void rgb_status_show(int8_t component)
{
    bool showing_effect = overlay == &system_screen_rgb && rgb_status_component == RGB_STATUS_EFFECT;
    if (component == RGB_STATUS_EFFECT && !showing_effect) selection_reset(&rgb_effect_selection, rgb_effect_index());

    rgb_status_component = component;
    open_overlay(&system_screen_rgb, RGB_STATUS_DURATION);
    screen_redraw = true;
}

static int8_t rgb_key_component(uint16_t keycode)
{
    switch (keycode)
    {
        case RM_TOGG:                     return RGB_STATUS_POWER;
        case RM_NEXT: case RM_PREV:
        case KRT_RGB:                     return RGB_STATUS_EFFECT;
        case RM_HUEU: case RM_HUED:       return RGB_STATUS_HUE;
        case RM_SATU: case RM_SATD:       return RGB_STATUS_SATURATION;
        case RM_VALU: case RM_VALD:       return RGB_STATUS_BRIGHTNESS;
        case RM_SPDU: case RM_SPDD:       return RGB_STATUS_SPEED;
        default:                          return RGB_STATUS_NONE;
    }
}

// Shown in key steps, rounded since the top may be clamped short of a full step
static void draw_slider(const char *title, uint8_t value, uint8_t maximum, uint8_t step)
{
    uint8_t max_steps = (maximum + step / 2) / step;
    uint8_t steps = MIN((value + step / 2) / step, max_steps);
    uint8_t fill = (uint16_t)steps * (RGB_BAR_WIDTH - RGB_BAR_PADDING * 2) / max_steps;

    fill_rect(0, CONTENT_Y, RGB_BAR_WIDTH, RGB_BAR_HEIGHT, true);
    fill_rect(1, CONTENT_Y + 1, RGB_BAR_WIDTH - 2, RGB_BAR_HEIGHT - 2, false);
    fill_rect(RGB_BAR_PADDING, CONTENT_Y + RGB_BAR_PADDING, fill, RGB_BAR_HEIGHT - RGB_BAR_PADDING * 2, true);

    char text[4];
    print_number(text, steps, 1);

    draw_text_at(0, TITLE_Y, title, false);
    draw_text_at(OLED_DISPLAY_WIDTH - strlen(text) * OLED_FONT_WIDTH, TITLE_Y, text, false);
}

static void rgb_status_update(void)
{
    // The effect changes after the key event, so it's picked up here
    bool frame = false;
    if (rgb_status_component == RGB_STATUS_EFFECT)
    {
        selection_follow(&rgb_effect_selection, rgb_effect_index(), RGB_EFFECT_COUNT);
        frame = selection_frame(&rgb_effect_selection);
    }

    if (!frame && !screen_redraw) return;
    screen_redraw = false;

    hsv_t hsv = rgb_matrix_get_hsv();

    frame_clear();

    switch (rgb_status_component)
    {
        case RGB_STATUS_POWER:      draw_labelled("RGB", rgb_matrix_is_enabled() ? "Enabled" : "Disabled");                  break;
        case RGB_STATUS_EFFECT:     draw_selection("RGB Effect", &rgb_effect_selection, RGB_EFFECT_COUNT, get_effect_display_name);  break;
        case RGB_STATUS_HUE:        draw_slider("RGB Hue", hsv.h, RGB_HUE_MAXIMUM, RGB_MATRIX_HUE_STEP);                             break;
        case RGB_STATUS_SATURATION: draw_slider("RGB Saturation", hsv.s, UINT8_MAX, RGB_MATRIX_SAT_STEP);                            break;
        case RGB_STATUS_BRIGHTNESS: draw_slider("RGB Brightness", hsv.v, RGB_MATRIX_MAXIMUM_BRIGHTNESS, RGB_MATRIX_VAL_STEP);        break;
        case RGB_STATUS_SPEED:      draw_slider("RGB Speed", rgb_matrix_get_speed(), UINT8_MAX, RGB_MATRIX_SPD_STEP);                break;
    }

    frame_show();
}

static const krteq_screen_t system_screen_rgb = {
    .update = rgb_status_update
};

//==============================================================================
// Shutdown screen
//==============================================================================

static const uint8_t bootloader_image[] = {
#embed "bitmaps/bootloader.bmp"
};

static const uint8_t restart_image[] = {
#embed "bitmaps/restart.bmp"
};

void render_shutdown_screen(bool jump_to_bootloader)
{
    bongo_save_hits();
    oled_clear();

    if (jump_to_bootloader)
    {
        draw_icon_text(bootloader_image, "Bootloader");
    }
    else
    {
        draw_icon_text(restart_image, "Rebooting");
    }

    oled_render_dirty(true);
}

//==============================================================================
// Screen dispatch
//==============================================================================

__attribute__((weak)) const krteq_screen_t *const user_screens[] = {
    &krteq_screen_indicators,
    &krteq_screen_bongo_cat,
    &krteq_screen_starfield,
    &krteq_screen_waves,
    &krteq_screen_cursor,
    &krteq_screen_game_of_life,
    &krteq_screen_system_info
};

void oled_key_event(uint16_t keycode, keypos_t keypos, bool pressed)
{
    if (keycode == KRT_SCR) return;
    if (IS_QK_MOMENTARY(keycode)) return;

    int8_t component = rgb_key_component(keycode);
    if (component != RGB_STATUS_NONE)
    {
        if (pressed) rgb_status_show(component);
        return;
    }

    uint8_t x, y;
    if (!key_position(keypos, &x, &y)) return;

    if (last_rendered_screen && last_rendered_screen->key_event)
    {
        last_rendered_screen->key_event(keycode, x, y, pressed);
    }
}

static void render_screen(const krteq_screen_t *screen)
{
    if (screen != last_rendered_screen)
    {
        last_rendered_screen = screen;
        oled_clear();
        screen_redraw = true;

        if (screen->start) screen->start();
    }

    if (screen->update) screen->update();
}

static void init_screens(void)
{
    for (uint8_t screen = 0; screen < user_screen_count(); screen++)
    {
        if (user_screens[screen]->init) user_screens[screen]->init();
    }
}

oled_rotation_t oled_init_kb(oled_rotation_t rotation)
{
    load_user_screen();
    init_screens();
    return OLED_ROTATION_180;
}

static const krteq_screen_t *active_screen(void)
{
    if (!logo_finished) return &system_screen_logo;

    uint8_t layer = get_highest_layer(layer_state);
    if (layer >= INPUT_LOCK_LAYER) return &system_screen_input_lock;
    if (layer == INDICATORS_LAYER) return &krteq_screen_indicators;

    if (overlay) return overlay;
    return get_user_screen(current_user_screen);
}

bool oled_task_kb(void)
{
    if (!oled_task_user())
    {
        return false;
    }

    if (overlay && timer_elapsed(overlay_timer) >= overlay_duration) close_overlay();

    const krteq_screen_t *screen = active_screen();

    // Close an overlay hidden by a higher priority screen
    if (overlay && screen != overlay) close_overlay();

    render_screen(screen);
    return false;
}

void oled_restart(void)
{
    bongo_save_hits();

    oled_clear();
    oled_render_dirty(true);
    last_rendered_screen = NULL;
    logo_finished = false;

    init_screens();
}