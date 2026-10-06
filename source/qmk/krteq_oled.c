#include "krteq_oled.h"
#include "version.h"
#include OLED_FONT_H

#define OLED_PAGES (OLED_DISPLAY_HEIGHT / 8)
#define INDICATORS_LAYER 1
#define INPUT_LOCK_LAYER 4

// Offsets into the keyboard datablock, sized by EECONFIG_KB_DATA_SIZE
#define DATA_SCREEN_OFFSET 0
#define DATA_BONGO_HITS_OFFSET 1

enum screens
{
    USER_SCREEN_INDICATORS,
    USER_SCREEN_BONGO_CAT,
    USER_SCREEN_LIFE,
    USER_SCREEN_RIPPLE,
    USER_SCREEN_SYSTEM_INFO,
    USER_SCREEN_COUNT,

    SYSTEM_SCREEN_LOGO,
    SYSTEM_SCREEN_SELECT,
    SYSTEM_SCREEN_INPUT_LOCK,
    SYSTEM_SCREEN_RGB
};

static int current_user_screen = 0;
static int selected_user_screen;
static int last_rendered_screen = -1;
static bool screen_redraw; // Set on a screen switch or by events, cleared by the screen once drawn

static void load_user_screen(void)
{
    uint8_t screen = 0;
    eeconfig_read_kb_datablock(&screen, DATA_SCREEN_OFFSET, sizeof(screen));

    // A stale block from an older layout could name a screen that no longer exists
    if (screen < USER_SCREEN_COUNT) current_user_screen = screen;
}

static void save_user_screen(void)
{
    uint8_t screen = current_user_screen;
    eeconfig_update_kb_datablock(&screen, DATA_SCREEN_OFFSET, sizeof(screen));
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

// One vertical slice of a glyph, with bit 0 at the top
static uint8_t font_column(char character, uint8_t column)
{
    return pgm_read_byte(&font[(character - OLED_FONT_START) * OLED_FONT_WIDTH + column]);
}

static void draw_text_at(uint8_t x, uint8_t y, const char *text, bool inverted)
{
    for (uint8_t i = 0; text[i]; i++)
    {
        for (uint8_t ix = 0; ix < OLED_FONT_WIDTH; ix++)
        {
            uint8_t glyph = font_column(text[i], ix);

            for (uint8_t iy = 0; iy < OLED_FONT_HEIGHT; iy++)
            {
                oled_write_pixel(x + i * OLED_FONT_WIDTH + ix, y + iy, (glyph >> iy & 1) != inverted);
            }
        }
    }
}

static void draw_text(uint8_t center_x, uint8_t y, const char *text, bool inverted)
{
    draw_text_at(center_x - strlen(text) * OLED_FONT_WIDTH / 2, y, text, inverted);
}

static void fill_rect(uint8_t left, uint8_t top, uint8_t width, uint8_t height, bool on)
{
    for (uint8_t iy = 0; iy < height; iy++)
    {
        for (uint8_t ix = 0; ix < width; ix++)
        {
            oled_write_pixel(left + ix, top + iy, on);
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

//==============================================================================
// Key positions
//==============================================================================

// Span of the physical key coordinates declared by the RGB matrix layout
#define KEY_SPACE_WIDTH 224
#define KEY_SPACE_HEIGHT 64

// Projects the physical location of a key onto the screen
static bool key_position(keypos_t key, uint8_t *x, uint8_t *y)
{
    uint8_t led = g_led_config.matrix_co[key.row][key.col];
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

static const uint8_t bongo_cat_sleep_image[] = {
#embed "bitmaps/bongo_cat_sleep.bmp"
};

static const uint8_t bongo_paw_down_image[] = {
#embed "bitmaps/bongo_paw_down.bmp"
};

static const uint8_t bongo_cat_zz_image[] = {
#embed "bitmaps/bongo_cat_zz.bmp"
};

static const uint8_t bongo_trophy_image[] = {
#embed "bitmaps/trophy.bmp"
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

// Every strike starts raised so the paw is always seen coming down
static void bongo_key_event(uint16_t keycode, bool pressed)
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

// Animations in flight are dropped, since their 16 bit timers may have wrapped while hidden
// Sleep carries over, an awake cat starts a fresh sleep countdown
static void bongo_cat_init(void)
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

    oled_clear();
    draw_image(bongo_sleeping || bongo_blinking ? bongo_cat_sleep_image : bongo_cat_image, BONGO_CAT_X, BONGO_CAT_Y);

    for (uint8_t paw = 0; paw < BONGO_PAW_COUNT; paw++)
    {
        const uint8_t *image = bongo_paw_up_image;
        if (bongo_sleeping) image = bongo_paw_down_image;
        else if (bongo_paw_state[paw] == BONGO_PAW_HIT) image = bongo_paw_hit_image;

        draw_image(image, bongo_paw_x[paw], BONGO_PAW_Y);
    }

    if (bongo_sleeping) draw_image(bongo_cat_zz_image, BONGO_ZZ_X, BONGO_ZZ_Y);

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
}

// The hit count is persistent and survives the reset
static void bongo_cat_reset(void)
{
    bongo_last_keycode = 0;
    bongo_paw = 0;
    memset(bongo_paw_state, BONGO_PAW_IDLE, sizeof(bongo_paw_state));
    bongo_sleeping = false;
    bongo_blinking = false;
}

//==============================================================================
// Ripple screen
//==============================================================================

#define RIPPLE_FRAME_DURATION 33
#define RIPPLE_IDLE_DURATION 15000
#define RIPPLE_DAMPING 6 // Waves lose one part in 2^n of their height per frame
#define RIPPLE_DROP_RADIUS 2
#define RIPPLE_DROP_HEIGHT 1200
#define RIPPLE_CREST 60 // Height that lights a pixel

#define RIPPLE_CELLS (OLED_DISPLAY_WIDTH * OLED_DISPLAY_HEIGHT)

static int16_t ripple_heights[2][RIPPLE_CELLS];
static uint8_t ripple_front;
static uint16_t ripple_timer;
static uint16_t ripple_idle_timer;
static bool ripple_seeded;

static void ripple_drop(int16_t x, int16_t y)
{
    for (int8_t dy = -RIPPLE_DROP_RADIUS; dy <= RIPPLE_DROP_RADIUS; dy++)
    {
        for (int8_t dx = -RIPPLE_DROP_RADIUS; dx <= RIPPLE_DROP_RADIUS; dx++)
        {
            int16_t drop_x = x + dx;
            int16_t drop_y = y + dy;

            if (dx * dx + dy * dy > RIPPLE_DROP_RADIUS * RIPPLE_DROP_RADIUS) continue;
            if (drop_x < 1 || drop_x >= OLED_DISPLAY_WIDTH - 1) continue;
            if (drop_y < 1 || drop_y >= OLED_DISPLAY_HEIGHT - 1) continue;

            ripple_heights[ripple_front][drop_y * OLED_DISPLAY_WIDTH + drop_x] = RIPPLE_DROP_HEIGHT;
        }
    }
}

static void ripple_key_event(keypos_t key, bool pressed)
{
    uint8_t x, y;
    if (!pressed || !key_position(key, &x, &y)) return;

    ripple_idle_timer = timer_read();
    ripple_drop(x, y);
}

static void ripple_draw(void)
{
    const int16_t *heights = ripple_heights[ripple_front];

    for (uint8_t page = 0; page < OLED_PAGES; page++)
    {
        for (uint8_t x = 0; x < OLED_DISPLAY_WIDTH; x++)
        {
            uint8_t column = 0;

            for (uint8_t bit = 0; bit < 8; bit++)
            {
                if (heights[(page * 8 + bit) * OLED_DISPLAY_WIDTH + x] > RIPPLE_CREST) column |= 1 << bit;
            }

            oled_write_raw_byte(column, page * OLED_DISPLAY_WIDTH + x);
        }
    }
}

static void ripple_init(void)
{
    ripple_timer = timer_read();
    ripple_idle_timer = timer_read();

    if (!ripple_seeded)
    {
        ripple_seeded = true;
        ripple_drop(OLED_DISPLAY_WIDTH / 2, OLED_DISPLAY_HEIGHT / 2);
    }

    ripple_draw();
}

static void ripple_update(void)
{
    if (timer_elapsed(ripple_timer) < RIPPLE_FRAME_DURATION) return;
    ripple_timer = timer_read();

    // Idle hands get a drop of their own somewhere on the surface
    if (timer_elapsed(ripple_idle_timer) > RIPPLE_IDLE_DURATION)
    {
        ripple_idle_timer = timer_read();
        ripple_drop(1 + random_value() % (OLED_DISPLAY_WIDTH - 2), 1 + random_value() % (OLED_DISPLAY_HEIGHT - 2));
    }

    const int16_t *previous = ripple_heights[ripple_front];
    int16_t *current = ripple_heights[ripple_front ^= 1];

    // Wave equation over a still border, so the screen edges reflect
    for (uint8_t y = 1; y < OLED_DISPLAY_HEIGHT - 1; y++)
    {
        for (uint8_t x = 1; x < OLED_DISPLAY_WIDTH - 1; x++)
        {
            uint16_t cell = y * OLED_DISPLAY_WIDTH + x;
            int16_t height = (previous[cell - 1] + previous[cell + 1] +
                              previous[cell - OLED_DISPLAY_WIDTH] + previous[cell + OLED_DISPLAY_WIDTH]) / 2 - current[cell];

            current[cell] = height - (height >> RIPPLE_DAMPING);
        }
    }

    ripple_draw();
}

static void ripple_reset(void)
{
    memset(ripple_heights, 0, sizeof(ripple_heights));
    ripple_front = 0;
    ripple_seeded = false;
}

//==============================================================================
// Game of life screen
//==============================================================================

#define LIFE_FRAME_DURATION 100
#define LIFE_SLICES 6
#define LIFE_SLICE_DURATION (LIFE_FRAME_DURATION / LIFE_SLICES)
#define LIFE_SLICE_ROWS (LIFE_HEIGHT / LIFE_SLICES)
#define LIFE_IDLE_DURATION 5000
#define LIFE_MARGIN_X 6
#define LIFE_MARGIN_Y 8
#define LIFE_GLIDER_Y 2
#define LIFE_WIDTH (OLED_DISPLAY_WIDTH + LIFE_MARGIN_X * 2)
#define LIFE_HEIGHT (OLED_DISPLAY_HEIGHT + LIFE_MARGIN_Y * 2)
#define LIFE_BYTES (LIFE_HEIGHT / 8 * LIFE_WIDTH)

// A whole page of vertical margin keeps the visible window byte aligned with the display
_Static_assert(LIFE_MARGIN_Y % 8 == 0, "The life margin must be a whole number of pages tall");
_Static_assert(LIFE_HEIGHT % LIFE_SLICES == 0, "Every slice must cover the same number of rows");

static uint8_t life_cells[LIFE_BYTES];
static uint8_t life_next[LIFE_BYTES];
static uint16_t life_timer;
static uint16_t life_idle_timer;
static uint8_t life_slice;
static bool life_seeded;

static bool life_inside(int16_t x, int16_t y)
{
    return x >= 0 && x < LIFE_WIDTH && y >= 0 && y < LIFE_HEIGHT;
}

static uint16_t life_index(int16_t x, int16_t y)
{
    return y / 8 * LIFE_WIDTH + x;
}

// The grid extends past the screen, so patterns leave view before hitting a wall
static bool life_cell(int16_t x, int16_t y)
{
    if (!life_inside(x, y)) return false;
    return life_cells[life_index(x, y)] >> (y % 8) & 1;
}

static void life_set_cell(uint8_t *cells, int16_t x, int16_t y, bool alive)
{
    if (!life_inside(x, y)) return;

    uint16_t index = life_index(x, y);
    uint8_t mask = 1 << (y % 8);

    if (alive) cells[index] |= mask;
    else cells[index] &= ~mask;
}

// Written to both buffers so a cell still lands whole partway through a generation
static void life_add_cell(int16_t x, int16_t y)
{
    life_set_cell(life_cells, x, y, true);
    life_set_cell(life_next, x, y, true);
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

static void life_key_event(keypos_t key, bool pressed)
{
    uint8_t x, y;
    if (!pressed || !key_position(key, &x, &y)) return;

    life_idle_timer = timer_read();
    life_bomb(x + LIFE_MARGIN_X, y + LIFE_MARGIN_Y, BOMB_SHAPE_HORIZONTAL);
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
    life_timer = timer_read();
    life_idle_timer = timer_read();

    if (!life_seeded)
    {
        life_seeded = true;
        life_bomb(LIFE_WIDTH / 2, LIFE_HEIGHT / 2, BOMB_SHAPE_VERTICAL);
    }

    life_draw();
}

static void life_update(void)
{
    if (timer_elapsed(life_timer) < LIFE_SLICE_DURATION) return;
    life_timer = timer_read();

    // One generation is spread over several slices, so no single pass stalls the keyboard
    uint8_t first = life_slice * LIFE_SLICE_ROWS;

    for (uint8_t y = first; y < first + LIFE_SLICE_ROWS; y++)
    {
        for (uint8_t x = 0; x < LIFE_WIDTH; x++)
        {
            uint8_t neighbours = 0;

            for (int8_t dy = -1; dy <= 1; dy++)
            {
                for (int8_t dx = -1; dx <= 1; dx++)
                {
                    if (dx || dy) neighbours += life_cell(x + dx, y + dy);
                }
            }

            life_set_cell(life_next, x, y, neighbours == 3 || (neighbours == 2 && life_cell(x, y)));
        }
    }

    if (++life_slice < LIFE_SLICES) return;
    life_slice = 0;

    memcpy(life_cells, life_next, sizeof(life_cells));

    // Spawn gliders on inactivity
    if (timer_elapsed(life_idle_timer) > LIFE_IDLE_DURATION)
    {
        life_idle_timer = timer_read();
        life_spawn_glider();
    }

    life_draw();
}

static void life_reset(void)
{
    memset(life_cells, 0, sizeof(life_cells));
    memset(life_next, 0, sizeof(life_next));
    life_slice = 0;
    life_seeded = false;
}

//==============================================================================
// System info screen
//==============================================================================

#define INFO_INTERVAL 1000
#define INFO_ROW_HEIGHT 8
#define INFO_BUILD_DATE_LENGTH 10 // QMK_BUILDDATE also carries a time, which is just noise here

_Static_assert(OLED_DISPLAY_HEIGHT / INFO_ROW_HEIGHT >= 4, "The system info screen needs four rows of text");

static uint16_t info_timer;

// DEVICE_VER packs the keyboard.json version as BCD, two digits of major and one each of the rest
static void print_device_version(char *out)
{
    *out++ = 'v';
    out = print_number(out, (DEVICE_VER >> 12 & 0xF) * 10 + (DEVICE_VER >> 8 & 0xF), 1);
    *out++ = '.';
    out = print_number(out, DEVICE_VER >> 4 & 0xF, 1);
    *out++ = '.';
    print_number(out, DEVICE_VER & 0xF, 1);
}

static uint16_t bcd_value(uint16_t bcd)
{
    uint16_t value = 0;
    for (int8_t shift = 12; shift >= 0; shift -= 4) value = value * 10 + (bcd >> shift & 0xF);
    return value;
}

// QMK_VERSION_BCD packs major and minor into a byte each and patch into the low half
static void print_qmk_version(char *out)
{
    out = print_number(out, bcd_value(QMK_VERSION_BCD >> 24 & 0xFF), 1);
    *out++ = '.';
    out = print_number(out, bcd_value(QMK_VERSION_BCD >> 16 & 0xFF), 1);
    *out++ = '.';
    print_number(out, bcd_value(QMK_VERSION_BCD & 0xFFFF), 1);
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
    if (!screen_redraw && timer_elapsed(info_timer) < INFO_INTERVAL) return;

    screen_redraw = false;
    info_timer = timer_read();

    char device_version[12];
    print_device_version(device_version);

    char uptime[16];
    print_uptime(uptime);

    char build_date[INFO_BUILD_DATE_LENGTH + 1];
    memcpy(build_date, QMK_BUILDDATE, INFO_BUILD_DATE_LENGTH);
    build_date[INFO_BUILD_DATE_LENGTH] = '\0';

    char qmk_version[16];
    print_qmk_version(qmk_version);

    oled_clear();
    render_info_row(0, PRODUCT, device_version);
    render_info_row(1, "Uptime", uptime);
    render_info_row(2, "QMK", qmk_version);
    render_info_row(3, "Built", build_date);
}

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

static void logo_init(void)
{
    logo_timer = timer_read();
    draw_image(logo_image, LOGO_X, LOGO_Y);
}

static void logo_update(void)
{
    logo_finished = timer_elapsed(logo_timer) > LOGO_DURATION;
}

static void logo_reset(void)
{
    logo_finished = false;
}

//==============================================================================
// Overlays
//==============================================================================

// Short lived screen shown over the user screen, closing itself once its time is up
static int overlay = -1;
static uint16_t overlay_timer;

// A selection is kept however it ends, timed out or cut short
static void close_overlay(void)
{
    if (overlay == SYSTEM_SCREEN_SELECT && selected_user_screen != current_user_screen)
    {
        bongo_save_hits();
        current_user_screen = selected_user_screen;
        save_user_screen();
    }

    overlay = -1;
}

static void open_overlay(int screen)
{
    if (overlay != screen) close_overlay();

    overlay = screen;
    overlay_timer = timer_read();
}

//==============================================================================
// Screen select screen
//==============================================================================

#define SELECT_DURATION 1000
#define SELECT_SQUARE_SIZE 7
#define SELECT_SQUARE_GAP 4
#define SELECT_SQUARE_Y 4
#define SELECT_NAME_Y 20
#define SELECT_SQUARE_PITCH (SELECT_SQUARE_SIZE + SELECT_SQUARE_GAP)
#define SELECT_SQUARE_LEFT ((OLED_DISPLAY_WIDTH - (USER_SCREEN_COUNT * SELECT_SQUARE_PITCH - SELECT_SQUARE_GAP)) / 2)
#define SELECT_UNNAMED "Undefined"

static const char *const select_names[USER_SCREEN_COUNT] = {
    [USER_SCREEN_INDICATORS]  = "Indicators",
    [USER_SCREEN_BONGO_CAT]   = "Bongo cat",
    [USER_SCREEN_LIFE]        = "Game of life",
    [USER_SCREEN_RIPPLE]      = "Waves",
    [USER_SCREEN_SYSTEM_INFO] = "System info"
};

// A fresh selection starts from the screen in use
static void start_selecting(void)
{
    if (overlay != SYSTEM_SCREEN_SELECT) selected_user_screen = current_user_screen;
    open_overlay(SYSTEM_SCREEN_SELECT);
    screen_redraw = true;
}

void default_user_screen(void)
{
    start_selecting();
    selected_user_screen = 0;
}

void shift_user_screen(int shift)
{
    start_selecting();
    selected_user_screen += shift;
    selected_user_screen %= USER_SCREEN_COUNT;
    if (selected_user_screen < 0) selected_user_screen += USER_SCREEN_COUNT;
}

static void draw_square(uint8_t x, uint8_t y, bool filled)
{
    fill_rect(x, y, SELECT_SQUARE_SIZE, SELECT_SQUARE_SIZE, true);
    if (!filled) fill_rect(x + 1, y + 1, SELECT_SQUARE_SIZE - 2, SELECT_SQUARE_SIZE - 2, false);
}

static void screen_select_update(void)
{
    if (timer_elapsed(overlay_timer) >= SELECT_DURATION)
    {
        close_overlay();
        return;
    }

    if (!screen_redraw) return;
    screen_redraw = false;

    oled_clear();

    for (uint8_t screen = 0; screen < USER_SCREEN_COUNT; screen++)
    {
        draw_square(SELECT_SQUARE_LEFT + screen * SELECT_SQUARE_PITCH, SELECT_SQUARE_Y, screen == selected_user_screen);
    }

    const char *name = select_names[selected_user_screen];
    draw_text(OLED_DISPLAY_WIDTH / 2, SELECT_NAME_Y, name ? name : SELECT_UNNAMED, false);
}

//==============================================================================
// Input lock screen
//==============================================================================

static const uint8_t input_lock_image[] = {
#embed "bitmaps/input_lock.bmp"
};

static void input_lock_update(void)
{
    if (!screen_redraw) return;
    screen_redraw = false;

    draw_icon_text(input_lock_image, "Input lock");
}

//==============================================================================
// RGB status screen
//==============================================================================

#define RGB_STATUS_DURATION 3000
#define RGB_TITLE_Y 4
#define RGB_CONTENT_Y 20
#define RGB_BAR_WIDTH OLED_DISPLAY_WIDTH
#define RGB_BAR_HEIGHT 7 // Matches the lit rows of a glyph
#define RGB_BAR_PADDING 2

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

// Effects missing here fall back to their number, see the animations in keyboard.json
static const char *const rgb_effect_names[RGB_MATRIX_EFFECT_MAX] = {
    [RGB_MATRIX_SOLID_COLOR]          = "Solid color",
    [RGB_MATRIX_RIVERFLOW]            = "Riverflow",
    [RGB_MATRIX_SOLID_REACTIVE_CROSS] = "Reactive cross"
};

static int8_t rgb_status_component;

static void rgb_status_show(int8_t component)
{
    rgb_status_component = component;
    open_overlay(SYSTEM_SCREEN_RGB);
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

static void draw_slider(const char *title, uint8_t value, uint8_t maximum)
{
    uint8_t fill = (uint16_t)value * (RGB_BAR_WIDTH - RGB_BAR_PADDING * 2) / maximum;

    fill_rect(0, RGB_CONTENT_Y, RGB_BAR_WIDTH, RGB_BAR_HEIGHT, true);
    fill_rect(1, RGB_CONTENT_Y + 1, RGB_BAR_WIDTH - 2, RGB_BAR_HEIGHT - 2, false);
    fill_rect(RGB_BAR_PADDING, RGB_CONTENT_Y + RGB_BAR_PADDING, fill, RGB_BAR_HEIGHT - RGB_BAR_PADDING * 2, true);

    char text[4];
    print_number(text, value, 1);

    draw_text_at(0, RGB_TITLE_Y, title, false);
    draw_text_at(OLED_DISPLAY_WIDTH - strlen(text) * OLED_FONT_WIDTH, RGB_TITLE_Y, text, false);
}

static void draw_labelled(const char *title, const char *text)
{
    draw_text(OLED_DISPLAY_WIDTH / 2, RGB_TITLE_Y, title, false);
    draw_text(OLED_DISPLAY_WIDTH / 2, RGB_CONTENT_Y, text, false);
}

static void rgb_status_update(void)
{
    if (timer_elapsed(overlay_timer) >= RGB_STATUS_DURATION)
    {
        close_overlay();
        return;
    }

    if (!screen_redraw) return;
    screen_redraw = false;

    uint8_t mode = rgb_matrix_get_mode();
    const char *name = mode < RGB_MATRIX_EFFECT_MAX ? rgb_effect_names[mode] : NULL;
    hsv_t hsv = rgb_matrix_get_hsv();

    char number[4];
    print_number(number, mode, 1);

    // Mode 0 is RGB_MATRIX_NONE, so effects are already numbered from 1
    char effect_title[20] = "RGB Effect ";
    char *out = print_number(effect_title + strlen(effect_title), mode, 1);
    *out++ = '/';
    print_number(out, RGB_MATRIX_EFFECT_MAX - 1, 1);

    oled_clear();

    switch (rgb_status_component)
    {
        case RGB_STATUS_POWER:      draw_labelled("RGB", rgb_matrix_is_enabled() ? "Enabled" : "Disabled");          break;
        case RGB_STATUS_EFFECT:     draw_labelled(effect_title, name ? name : number);                      break;
        case RGB_STATUS_HUE:        draw_slider("RGB Hue", hsv.h, UINT8_MAX);                              break;
        case RGB_STATUS_SATURATION: draw_slider("RGB Saturation", hsv.s, UINT8_MAX);                       break;
        case RGB_STATUS_BRIGHTNESS: draw_slider("RGB Brightness", hsv.v, RGB_MATRIX_MAXIMUM_BRIGHTNESS);   break;
        case RGB_STATUS_SPEED:      draw_slider("RGB Speed", rgb_matrix_get_speed(), UINT8_MAX);            break;
    }
}

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

void oled_key_event(uint16_t keycode, keypos_t key, bool pressed)
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

    switch (last_rendered_screen)
    {
        case USER_SCREEN_BONGO_CAT: bongo_key_event(keycode, pressed); break;
        case USER_SCREEN_LIFE:      life_key_event(key, pressed);      break;
        case USER_SCREEN_RIPPLE:    ripple_key_event(key, pressed);    break;
    }
}

static void render_screen(int screen)
{
    if (screen != last_rendered_screen)
    {
        last_rendered_screen = screen;
        screen_redraw = true;
        oled_clear();

        switch (screen)
        {
            case USER_SCREEN_BONGO_CAT: bongo_cat_init(); break;
            case USER_SCREEN_LIFE:      life_init();      break;
            case USER_SCREEN_RIPPLE:    ripple_init();    break;
            case SYSTEM_SCREEN_LOGO:    logo_init();      break;
        }
    }

    switch (screen)
    {
        case USER_SCREEN_INDICATORS:   indicators_update();    break;
        case USER_SCREEN_BONGO_CAT:    bongo_cat_update();     break;
        case USER_SCREEN_LIFE:         life_update();          break;
        case USER_SCREEN_RIPPLE:       ripple_update();        break;
        case USER_SCREEN_SYSTEM_INFO:  system_info_update();   break;
        case SYSTEM_SCREEN_LOGO:       logo_update();          break;
        case SYSTEM_SCREEN_SELECT:     screen_select_update(); break;
        case SYSTEM_SCREEN_INPUT_LOCK: input_lock_update();    break;
        case SYSTEM_SCREEN_RGB:        rgb_status_update();    break;
    }
}

oled_rotation_t oled_init_kb(oled_rotation_t rotation)
{
    load_user_screen();
    bongo_load_hits();
    return OLED_ROTATION_180;
}

bool oled_task_kb(void)
{
    if (!oled_task_user())
    {
        return false;
    }

    uint8_t layer = get_highest_layer(layer_state);
    int screen;

    if (!logo_finished)
    {
        screen = SYSTEM_SCREEN_LOGO;
    }
    else if (layer >= INPUT_LOCK_LAYER)
    {
        screen = SYSTEM_SCREEN_INPUT_LOCK;
    }
    else if (layer == INDICATORS_LAYER)
    {
        screen = USER_SCREEN_INDICATORS;
    }
    else if (overlay >= 0)
    {
        screen = overlay;
    }
    else
    {
        screen = current_user_screen;
    }

    // An overlay outranked by another screen is cut short rather than left waiting underneath
    if (overlay >= 0 && screen != overlay) close_overlay();

    render_screen(screen);
    return false;
}

void oled_restart(void)
{
    bongo_save_hits();

    oled_clear();
    oled_render_dirty(true);
    last_rendered_screen = -1;

    logo_reset();
    bongo_cat_reset();
    life_reset();
    ripple_reset();
}