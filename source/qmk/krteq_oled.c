#include <ctype.h>
#include "krteq_oled.h"
#include "version.h"
#include OLED_FONT_H

#define OLED_PAGES (OLED_DISPLAY_HEIGHT / 8)
#define FRAME_DURATION_30_FPS 33 // Milliseconds per frame
#define INDICATORS_LAYER 1
#define INPUT_LOCK_LAYER 4

// Offsets into the keyboard datablock, sized by EECONFIG_KB_DATA_SIZE
#define DATA_SCREEN_OFFSET 0
#define DATA_BONGO_HITS_OFFSET 1

static int current_user_screen = 0;
static const krteq_screen_t *last_rendered_screen = NULL;
static bool screen_redraw; // Set on a screen switch or by events, cleared by the screen once drawn

static uint8_t user_screen_count(void)
{
    uint8_t count = 0;
    while (count < UINT8_MAX && user_screens[count]) count++;
    return count;
}

// An empty keymap list falls back to the indicators
static const krteq_screen_t *get_user_screen(int index)
{
    if (user_screen_count() == 0) return &krteq_screen_indicators;
    return user_screens[index];
}

static void load_user_screen(void)
{
    uint8_t screen = 0;
    eeconfig_read_kb_datablock(&screen, DATA_SCREEN_OFFSET, sizeof(screen));

    // A stale block from an older layout or keymap could name a screen that no longer exists
    if (screen < user_screen_count()) current_user_screen = screen;
}

static void save_user_screen(void)
{
    uint8_t screen = current_user_screen;
    eeconfig_update_kb_datablock(&screen, DATA_SCREEN_OFFSET, sizeof(screen));
}

// Whether the duration has passed since the timer was last restarted, restarting it if so
static bool timer_finished(uint16_t *timer, uint16_t duration)
{
    if (timer_elapsed(*timer) < duration) return false;

    *timer = timer_read();
    return true;
}

//==============================================================================
// Drawing
//==============================================================================

// Off screen frame for the screens that redraw in full, which clear it and redraw it every frame
static uint8_t frame_buffer[OLED_PAGES * OLED_DISPLAY_WIDTH];
static bool frame_open; // Between frame_clear and frame_show, the drawing helpers draw into the frame

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

// Only bytes that changed are marked dirty, so unchanged parts of the frame cost no display traffic
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
            if (pixel != IMAGE_TRANSPARENT) draw_pixel(x + ix, y + iy, pixel);
        }
    }
}

// One vertical slice of a glyph, with bit 0 at the top
static uint8_t font_column(char character, uint8_t column)
{
    return pgm_read_byte(&font[(character - OLED_FONT_START) * OLED_FONT_WIDTH + column]);
}

// Only the part of the text that lands on the screen is drawn, so text can slide in from off screen
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

// Two overlapping rects leave the corner pixels out to round the box off
static void draw_box(uint8_t left, uint8_t top, uint8_t width, uint8_t height, bool filled)
{
    fill_rect(left + 1, top, width - 2, height, filled);
    fill_rect(left, top + 1, width, height - 2, filled);
}

// Icon and label are centered together, so the pair shifts with the length of the label
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

// Writes the decimal value padded with zeros up to the given width, returning the terminator so writes can chain on
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

// Title over a line of content, shared by the overlays
#define TITLE_Y 4
#define CONTENT_Y 20
#define UNNAMED "Undefined"

static void draw_labelled(const char *title, const char *text)
{
    draw_text(OLED_DISPLAY_WIDTH / 2, TITLE_Y, title, false);
    draw_text(OLED_DISPLAY_WIDTH / 2, CONTENT_Y, text, false);
}

// Title over a name, with a row of dots marking the position along the bottom.
// A move slides the old name out and the new one in from the pressed side.
#define SELECT_TITLE_Y 0
#define SELECT_NAME_Y 13
#define SELECT_SLIDE_DISTANCE OLED_DISPLAY_WIDTH
#define SELECT_SLIDE_DURATION 200
#define SELECT_FRAME_DURATION FRAME_DURATION_30_FPS
#define SELECT_DOT_WIDTH 7
#define SELECT_DOT_HEIGHT 5
#define SELECT_DOT_Y (OLED_DISPLAY_HEIGHT - SELECT_DOT_HEIGHT / 2 - 1) // The current dot rests on the bottom edge
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

// The first frame is left to the caller's redraw, later ones follow at the frame rate
static void selection_move(selection_t *selection, uint8_t index, int8_t direction)
{
    if (index == selection->to) return;

    selection->from = selection->to;
    selection->to = index;
    selection->direction = direction;
    selection->animating = true;
    selection->timer = selection->frame_timer = timer_read();
}

// For moves made elsewhere, a step to a neighbour slides that way, wrapping around the ends, while jumps slide by order
static void selection_follow(selection_t *selection, uint8_t index, uint8_t count)
{
    uint8_t from = selection->to;
    int8_t direction = index > from ? 1 : -1;

    if (index == (from + 1) % count) direction = 1;
    else if (index == (from + count - 1) % count) direction = -1;

    selection_move(selection, index, direction);
}

// Whether a frame is due, capped at the frame rate while sliding. The settled frame is the last one.
static bool selection_frame(selection_t *selection)
{
    if (!selection->animating || !timer_finished(&selection->frame_timer, SELECT_FRAME_DURATION)) return false;

    if (timer_elapsed(selection->timer) >= SELECT_SLIDE_DURATION) selection->animating = false;
    return true;
}

// Eased out, so the slide starts fast and settles gently
static uint16_t selection_progress(const selection_t *selection)
{
    uint16_t elapsed = timer_elapsed(selection->timer);
    if (!selection->animating || elapsed >= SELECT_SLIDE_DURATION) return SELECT_PROGRESS_MAX;

    uint32_t remaining = SELECT_SLIDE_DURATION - elapsed;
    return SELECT_PROGRESS_MAX - SELECT_PROGRESS_MAX * remaining * remaining / (SELECT_SLIDE_DURATION * SELECT_SLIDE_DURATION);
}

// Centered, then offset by the slide
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

    // The old name is pushed out by the new one following a slide distance behind
    if (selection->animating) draw_selection_name(name(selection->from), -selection->direction * shift);
    draw_selection_name(name(selection->to), selection->direction * (SELECT_SLIDE_DISTANCE - shift));

    // Dots shrink their spacing to fit longer lists
    uint8_t gap = count > 1 ? MIN(SELECT_DOT_GAP, (OLED_DISPLAY_WIDTH - SELECT_DOT_WIDTH) / (count - 1)) : 0;
    uint8_t left = OLED_DISPLAY_WIDTH / 2 - (count - 1) * gap / 2;

    for (uint8_t i = 0; i < count; i++) draw_pixel(left + i * gap, SELECT_DOT_Y, true);

    // The current dot glides along with the names, sweeping across the row when wrapping around the ends
    int16_t from_x = left + selection->from * gap;
    int16_t to_x = left + selection->to * gap;
    int16_t dot_x = from_x + (to_x - from_x) * (int16_t)progress / SELECT_PROGRESS_MAX;

    draw_box(dot_x - SELECT_DOT_WIDTH / 2, SELECT_DOT_Y - SELECT_DOT_HEIGHT / 2, SELECT_DOT_WIDTH, SELECT_DOT_HEIGHT, true);
}

//==============================================================================
// Key positions
//==============================================================================

// Span of the physical key coordinates declared by the RGB matrix layout
#define KEY_SPACE_WIDTH 224
#define KEY_SPACE_HEIGHT 64

// Projects the physical location of a key onto the screen
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

// Blinking only fills quiet moments, so any paw movement pushes the next blink back
static void advance_idle(void)
{
    if (bongo_sleeping) return;

    // Latched until the next hit wakes the cat
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

// The hit count is persistent, so it is read back rather than cleared
static void bongo_cat_init(void)
{
    bongo_load_hits();
    bongo_last_keycode = 0;
    bongo_paw = 0;
    bongo_sleeping = false;
    bongo_blinking = false;
}

// Animations in flight are dropped, since their 16 bit timers may have wrapped while hidden
// Sleep carries over, an awake cat starts a fresh sleep countdown
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

    // The base image has its eyes open, so only closed eyes are drawn over it
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

// Every strike starts raised so the paw is always seen coming down
static void bongo_key_event(uint16_t keycode, uint8_t x, uint8_t y, bool pressed)
{
    if (!pressed) return;

    if (bongo_hits < UINT32_MAX) bongo_hits++;
    bongo_wake();

    if (keycode != bongo_last_keycode) bongo_paw ^= 1;
    bongo_last_keycode = keycode;

    // Restarting an already raised paw would starve its strike under fast repeats
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
#define WAVE_DAMPING 6 // Waves lose one part in 2^n of their height per frame
#define WAVE_DROP_RADIUS 2
#define WAVE_DROP_HEIGHT 1200
#define WAVE_CREST 60 // Height that lights a pixel

// The grid reaches one cell past each screen edge, where it is held still so waves reflect just off screen
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

    // Idle hands get a drop of their own somewhere on the surface
    if (timer_finished(&wave_idle_timer, WAVE_IDLE_DURATION))
    {
        wave_drop(random_value() % OLED_DISPLAY_WIDTH, random_value() % OLED_DISPLAY_HEIGHT);
    }

    const int16_t *previous = wave_heights[wave_front];
    int16_t *current = wave_heights[wave_front ^= 1];

    // Wave equation over a still border, so the edges reflect
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
// Bubbles screen
//==============================================================================

#define BUBBLE_FRAME_DURATION FRAME_DURATION_30_FPS
#define BUBBLE_POP_INTERVAL 10000 // A bubble pops on its own this long after the last pop
#define BUBBLE_SHIFT 8 // Positions and velocities are fixed point pixels with this many fractional bits
#define BUBBLE_MIN_SPEED 32 // New bubbles drift between these speeds along each axis
#define BUBBLE_MAX_SPEED 96
#define BUBBLE_SPEED_LIMIT 160 // Caps each axis so blasts can't fling bubbles across the screen
#define BUBBLE_BLAST 5000 // Push from a pop on the bubbles around it, fading with distance
#define BUBBLE_BLAST_RANGE 24 // Pixels
#define BUBBLE_BLAST_SOFTENING 16 // Squared pixels, tames blasts right next to the pop
#define BUBBLE_WALL 6 // Bubbles within the margin are pushed inwards by one part in 2^n of how deep in they are per frame
#define BUBBLE_WALL_MARGIN 4 // Pixels from the edges where the push starts, measured to the bubble's edge
#define BUBBLE_FIELD 5 // Bubbles within range push apart, as a pair, by one part in 2^n of how far inside it they are per frame
#define BUBBLE_FIELD_RANGE 4 // Pixels between two bubbles' edges where they start pushing each other
#define BUBBLE_DRAG 12 // Bubbles lose one part in 2^n of their velocity per frame
#define BUBBLE_WANDER 16 // Random nudge per frame, which the drag smooths into a slow airy float
#define BUBBLE_POP_FRAMES 4

struct bubble
{
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    uint8_t radius;
    uint8_t pop; // Frames of the pop burst left, zero while floating
};

static const uint8_t bubble_radii[] = { 3, 3, 3, 4, 5, 6, 7, 8, 9 };

#define BUBBLE_COUNT ARRAY_SIZE(bubble_radii)

static struct bubble bubbles[BUBBLE_COUNT];
static uint16_t bubble_timer;
static uint16_t bubble_pop_timer;
static uint8_t bubble_next; // Idle pops take the next bubble in turn

static int32_t bubble_speed(void)
{
    int32_t speed = BUBBLE_MIN_SPEED + random_value() % (BUBBLE_MAX_SPEED - BUBBLE_MIN_SPEED);
    return random_value() & 1 ? speed : -speed;
}

// Places the bubble just off the edge it is drifting away from, so it floats in
static void bubble_spawn(struct bubble *bubble)
{
    int16_t radius = bubble->radius;
    int16_t x = random_value() % OLED_DISPLAY_WIDTH;
    int16_t y = random_value() % OLED_DISPLAY_HEIGHT;

    bubble->vx = bubble_speed();
    bubble->vy = bubble_speed();
    bubble->pop = 0;

    if (random_value() & 1)
    {
        x = bubble->vx > 0 ? -radius : OLED_DISPLAY_WIDTH + radius;
    }
    else
    {
        y = bubble->vy > 0 ? -radius : OLED_DISPLAY_HEIGHT + radius;
    }

    bubble->x = (int32_t)x << BUBBLE_SHIFT;
    bubble->y = (int32_t)y << BUBBLE_SHIFT;
}

// Pops the bubble, blasting the ones close by away from it
static void bubble_pop(struct bubble *popped)
{
    bubble_pop_timer = timer_read();

    if (!popped || popped->pop) return;
    popped->pop = BUBBLE_POP_FRAMES;

    for (uint8_t i = 0; i < BUBBLE_COUNT; i++)
    {
        struct bubble *bubble = &bubbles[i];
        int16_t dx = (bubble->x - popped->x) >> BUBBLE_SHIFT;
        int16_t dy = (bubble->y - popped->y) >> BUBBLE_SHIFT;
        int32_t distance_squared = dx * dx + dy * dy;

        if (bubble->pop || distance_squared > BUBBLE_BLAST_RANGE * BUBBLE_BLAST_RANGE) continue;

        bubble->vx += dx * BUBBLE_BLAST / (distance_squared + BUBBLE_BLAST_SOFTENING);
        bubble->vy += dy * BUBBLE_BLAST / (distance_squared + BUBBLE_BLAST_SOFTENING);
    }
}

// The floating bubble nearest to the given pixel, or NULL if all are popping
static struct bubble *bubble_closest(uint8_t x, uint8_t y)
{
    struct bubble *closest = NULL;
    int32_t closest_distance = INT32_MAX;

    for (uint8_t i = 0; i < BUBBLE_COUNT; i++)
    {
        struct bubble *bubble = &bubbles[i];
        if (bubble->pop) continue;

        int32_t dx = (bubble->x >> BUBBLE_SHIFT) - x;
        int32_t dy = (bubble->y >> BUBBLE_SHIFT) - y;
        int32_t distance_squared = dx * dx + dy * dy;

        if (distance_squared < closest_distance)
        {
            closest = bubble;
            closest_distance = distance_squared;
        }
    }

    return closest;
}

// Bubbles nearing an edge are pushed back inwards, harder the closer they get
static void bubble_wall(int32_t position, int32_t *velocity, int32_t low, int32_t high)
{
    if (position < low) *velocity += (low - position) >> BUBBLE_WALL;
    if (position > high) *velocity -= (position - high) >> BUBBLE_WALL;
}

static uint32_t square_root(uint32_t value)
{
    uint32_t root = 0;

    for (uint32_t bit = 1ul << 30; bit; bit >>= 2)
    {
        if (value >= root + bit)
        {
            value -= root + bit;
            root = (root >> 1) + bit;
        }
        else
        {
            root >>= 1;
        }
    }

    return root;
}

// Nearby bubbles gently push each other apart, harder the closer they get
// Mass goes with area, so each bubble gives way by the other's share of the total
static void bubble_push(struct bubble *a, struct bubble *b)
{
    int32_t dx = b->x - a->x;
    int32_t dy = b->y - a->y;
    int32_t reach = (int32_t)(a->radius + b->radius + BUBBLE_FIELD_RANGE) << BUBBLE_SHIFT;

    if (dx >= reach || dx <= -reach || dy >= reach || dy <= -reach) return;

    int32_t distance = square_root(dx * dx + dy * dy);
    if (!distance || distance >= reach) return;

    int32_t force = (reach - distance) >> BUBBLE_FIELD;
    int32_t mass_a = a->radius * a->radius;
    int32_t mass_b = b->radius * b->radius;
    int32_t force_a = force * mass_b / (mass_a + mass_b);
    int32_t force_b = force - force_a;

    a->vx -= dx * force_a / distance;
    a->vy -= dy * force_a / distance;
    b->vx += dx * force_b / distance;
    b->vy += dy * force_b / distance;
}

// A random nudge, then drag that rounds away from zero so it still bites at the slowest speeds
static int32_t bubble_float(int32_t velocity)
{
    velocity += (int32_t)(random_value() % (BUBBLE_WANDER * 2 + 1)) - BUBBLE_WANDER;
    return velocity - (velocity + (velocity > 0 ? 1 : -1) * ((1 << BUBBLE_DRAG) - 1)) / (1 << BUBBLE_DRAG);
}

static void bubble_draw(void)
{
    frame_clear();

    for (uint8_t i = 0; i < BUBBLE_COUNT; i++)
    {
        struct bubble *bubble = &bubbles[i];
        int16_t x = bubble->x >> BUBBLE_SHIFT;
        int16_t y = bubble->y >> BUBBLE_SHIFT;
        int16_t radius = bubble->radius;

        // A popping bubble bursts into a ring of droplets flying outwards
        if (bubble->pop)
        {
            int16_t spread = radius + (BUBBLE_POP_FRAMES - bubble->pop) * 2;
            int16_t diagonal = spread * 181 >> 8; // Spread / sqrt(2)

            frame_plot(x + spread, y);
            frame_plot(x - spread, y);
            frame_plot(x, y + spread);
            frame_plot(x, y - spread);
            frame_plot(x + diagonal, y + diagonal);
            frame_plot(x + diagonal, y - diagonal);
            frame_plot(x - diagonal, y + diagonal);
            frame_plot(x - diagonal, y - diagonal);
            continue;
        }

        // Pixels within half a pixel of the radius, r - 0.5 < d <= r + 0.5 squared and rounded
        for (int16_t dy = -radius; dy <= radius; dy++)
        {
            for (int16_t dx = -radius; dx <= radius; dx++)
            {
                int16_t distance_squared = dx * dx + dy * dy;
                if (distance_squared > radius * radius - radius && distance_squared <= radius * radius + radius) frame_plot(x + dx, y + dy);
            }
        }

        // A glint of light in the upper left
        if (radius >= 4) frame_plot(x - radius / 2, y - radius / 2);
    }

    frame_show();
}

static void bubble_init(void)
{
    for (uint8_t i = 0; i < BUBBLE_COUNT; i++)
    {
        struct bubble *bubble = &bubbles[i];
        uint8_t radius = bubble_radii[i];

        // Spawned as usual, then moved on screen so the first bubbles are already there
        bubble->radius = radius;
        bubble_spawn(bubble);
        bubble->x = (int32_t)(radius + random_value() % (OLED_DISPLAY_WIDTH - radius * 2)) << BUBBLE_SHIFT;
        bubble->y = (int32_t)(radius + random_value() % (OLED_DISPLAY_HEIGHT - radius * 2)) << BUBBLE_SHIFT;
    }

    bubble_next = 0;
}

static void bubble_start(void)
{
    bubble_timer = timer_read();
    bubble_pop_timer = timer_read();
}

static void bubble_update(void)
{
    if (!timer_finished(&bubble_timer, BUBBLE_FRAME_DURATION) && !screen_redraw) return;
    screen_redraw = false;

    if (timer_finished(&bubble_pop_timer, BUBBLE_POP_INTERVAL))
    {
        bubble_pop(&bubbles[bubble_next]);
        bubble_next = (bubble_next + 1) % BUBBLE_COUNT;
    }

    for (uint8_t i = 0; i < BUBBLE_COUNT; i++)
    {
        for (uint8_t j = i + 1; j < BUBBLE_COUNT; j++)
        {
            if (!bubbles[i].pop && !bubbles[j].pop) bubble_push(&bubbles[i], &bubbles[j]);
        }
    }

    for (uint8_t i = 0; i < BUBBLE_COUNT; i++)
    {
        struct bubble *bubble = &bubbles[i];

        // Popped bubbles come back from off screen once the burst is over
        if (bubble->pop)
        {
            if (!--bubble->pop) bubble_spawn(bubble);
            continue;
        }

        bubble->vx = MAX(-BUBBLE_SPEED_LIMIT, MIN(BUBBLE_SPEED_LIMIT, bubble_float(bubble->vx)));
        bubble->vy = MAX(-BUBBLE_SPEED_LIMIT, MIN(BUBBLE_SPEED_LIMIT, bubble_float(bubble->vy)));
        bubble->x += bubble->vx;
        bubble->y += bubble->vy;

        int32_t reach = (int32_t)(bubble->radius + BUBBLE_WALL_MARGIN) << BUBBLE_SHIFT;

        bubble_wall(bubble->x, &bubble->vx, reach, ((OLED_DISPLAY_WIDTH - 1) << BUBBLE_SHIFT) - reach);
        bubble_wall(bubble->y, &bubble->vy, reach, ((OLED_DISPLAY_HEIGHT - 1) << BUBBLE_SHIFT) - reach);
    }

    bubble_draw();
}

static void bubble_key_event(uint16_t keycode, uint8_t x, uint8_t y, bool pressed)
{
    if (pressed) bubble_pop(bubble_closest(x, y));
}

const krteq_screen_t krteq_screen_bubbles = {
    .name      = "Bubbles",
    .init      = bubble_init,
    .start     = bubble_start,
    .update    = bubble_update,
    .key_event = bubble_key_event
};

//==============================================================================
// Starfield screen
//==============================================================================

#define STARFIELD_FRAME_DURATION FRAME_DURATION_30_FPS
#define STARFIELD_COUNT 48
#define STARFIELD_SHIFT 8 // Depths and speeds are fixed point with this many fractional bits
#define STARFIELD_FAR_DEPTH 1024
#define STARFIELD_NEAR_DEPTH 8
#define STARFIELD_NEAR ((int32_t)STARFIELD_NEAR_DEPTH << STARFIELD_SHIFT)
#define STARFIELD_FOCAL OLED_DISPLAY_WIDTH
// Spawn spread that lands exactly on the screen edges at the far plane
#define STARFIELD_SPREAD_X (STARFIELD_FAR_DEPTH * OLED_DISPLAY_WIDTH / 2 / STARFIELD_FOCAL)
#define STARFIELD_SPREAD_Y (STARFIELD_FAR_DEPTH * OLED_DISPLAY_HEIGHT / 2 / STARFIELD_FOCAL)
#define STARFIELD_BASE_SPEED (6 << STARFIELD_SHIFT) // Depth travelled per frame while idle
#define STARFIELD_MAX_BOOST (64 << STARFIELD_SHIFT)
#define STARFIELD_KICK (3 << STARFIELD_SHIFT) // Boost added per key press, felt on the very next frame
// Drag in proportion to speed, so steady typing settles at a speed matching its pace
#define STARFIELD_DRAG 6 // The boost loses one part in 2^n per frame

struct star
{
    int16_t x;
    int16_t y;
    int32_t z;
    int16_t trail_x; // Last drawn screen position, the streak runs from here
    int16_t trail_y;
};

static struct star starfield[STARFIELD_COUNT];
static uint16_t starfield_timer;
static int32_t starfield_boost;

// Bresenham, stopping once the line leaves the screen
static void starfield_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
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

    // Rounds up so the boost settles all the way back to zero
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

            // Streaks grow with speed on their own, as each frame spans more of the screen
            starfield_line(star->trail_x, star->trail_y, x, y);
            star->trail_x = x;
            star->trail_y = y;

            if (on_screen(x, y)) continue;
        }

        // Past the camera or off the edge, so it starts over far away
        // A small depth jitter keeps stars respawned together from flying in a sheet
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

// A whole page of vertical margin keeps the visible window byte aligned with the display
_Static_assert(LIFE_MARGIN_Y % 8 == 0, "The life margin must be a whole number of pages tall");

static uint8_t life_cells[LIFE_BYTES];
static uint8_t life_next[LIFE_BYTES];
static uint16_t life_timer;
static uint16_t life_idle_timer;

// The grid extends past the screen, so patterns leave view before hitting a wall
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

// Glider heading down and right, mirrored into the other three diagonals
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

// One column of a page with the touching rows of the pages above and below, the page itself in bits 8 to 15
static uint32_t life_column(uint8_t page, int16_t x)
{
    if (x < 0 || x >= LIFE_WIDTH) return 0;

    const uint8_t *column = life_cells + page * LIFE_WIDTH + x;
    uint32_t word = (uint32_t)column[0] << 8;

    if (page > 0) word |= column[-LIFE_WIDTH];
    if (page < LIFE_PAGES - 1) word |= (uint32_t)column[LIFE_WIDTH] << 16;
    return word;
}

// Every bit of the words is a cell, so the eight neighbours of a whole page column are counted at once
// by adding them up bitwise. The count is only needed modulo 8, as a full 8 neighbours dies the same as 0.
static uint8_t life_step(uint32_t left, uint32_t centre, uint32_t right)
{
    // Each side column sums its three cells, the centre only its two
    uint32_t left_low   = (left << 1) ^ left ^ (left >> 1);
    uint32_t left_high  = ((left << 1) & left) | ((left >> 1) & ((left << 1) ^ left));
    uint32_t right_low  = (right << 1) ^ right ^ (right >> 1);
    uint32_t right_high = ((right << 1) & right) | ((right >> 1) & ((right << 1) ^ right));
    uint32_t centre_low  = (centre << 1) ^ (centre >> 1);
    uint32_t centre_high = (centre << 1) & (centre >> 1);

    // Then the column sums are added together, one bit position at a time
    uint32_t ones = left_low ^ right_low ^ centre_low;
    uint32_t carry = (left_low & right_low) | (centre_low & (left_low ^ right_low));
    uint32_t twos_sum = left_high ^ right_high ^ centre_high;
    uint32_t twos_carry = (left_high & right_high) | (centre_high & (left_high ^ right_high));
    uint32_t twos = twos_sum ^ carry;
    uint32_t fours = twos_carry ^ (twos_sum & carry);

    // Two or three neighbours, with two only keeping a live cell alive
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

    // Columns slide through a window of three, so each is read only once per page
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

    // Spawn gliders on inactivity
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
#define INFO_BUILD_DATE_LENGTH 10 // QMK_BUILDDATE also carries a time, which is just noise here

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

// Defined in the screen select section, which in turn builds on the overlays
static const krteq_screen_t system_screen_select;
static selection_t screen_selection;

// Short lived screen shown over the user screen, closed by the dispatch once its time is up
static const krteq_screen_t *overlay = NULL;
static uint16_t overlay_timer;
static uint16_t overlay_duration;

// A selection is kept however it ends, timed out or cut short
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

// A fresh selection starts from the screen in use
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
#define RGB_BAR_HEIGHT 7 // Matches the lit rows of a glyph
#define RGB_BAR_PADDING 2
#define RGB_HUE_MAXIMUM (UINT8_MAX + 1 - RGB_MATRIX_HUE_STEP) // Hue wraps, so the step before 0 is the top

// Defined at the end of this section
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

    // Format
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

// Called as the key goes down, before the effect steps, so a fresh effect selection starts from the old one
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

// Values are shown in key steps, rounded since the last step clamps short of a full one at the top
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
    // The effect steps after the key reaches the overlay, so the move is picked up here
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
    &krteq_screen_bubbles,
    &krteq_screen_game_of_life,
    &krteq_screen_system_info
};

void oled_key_event(uint16_t keycode, keypos_t keypos, bool pressed)
{
    // Keys that drive the screens themselves must not disturb their contents
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

// Runs at boot and on wake while the logo plays, so every screen starts from a fresh state
// Screens left out of the list are never shown, so they have no state to set up
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

// The first match wins, so the boot logo and layer screens outrank the overlays, which outrank the user screen
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

    // An overlay outranked by another screen is cut short rather than left waiting underneath
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