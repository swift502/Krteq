#include "krteq_oled.h"
#include OLED_FONT_H

#define OLED_PAGES (OLED_DISPLAY_HEIGHT / 8)
#define INDICATORS_LAYER 1
#define SCREEN_SELECT_LAYER 2
#define INPUT_LOCK_LAYER 4

enum screens
{
    USER_SCREEN_INDICATORS,
    USER_SCREEN_BONGO_CAT,
    USER_SCREEN_LIFE,
    USER_SCREEN_RIPPLE,
    USER_SCREEN_COUNT,

    SYSTEM_SCREEN_LOGO,
    SYSTEM_SCREEN_SELECT,
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

static void draw_text(uint8_t center_x, uint8_t y, const char *text)
{
    uint8_t x = center_x - strlen(text) * OLED_FONT_WIDTH / 2;

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

static void draw_number(uint8_t center_x, uint8_t y, uint32_t value)
{
    char text[11];
    char *digit = text + sizeof(text) - 1;

    *digit = '\0';
    do *--digit = '0' + value % 10; while (value /= 10);

    draw_text(center_x, y, digit);
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
#define BONGO_PAW_COUNT 2
#define BONGO_PAW_LEFT_X 44
#define BONGO_PAW_RIGHT_X 93
#define BONGO_PAW_Y 12
#define BONGO_HITS_X (BONGO_PAW_LEFT_X / 2)
#define BONGO_HITS_Y ((OLED_DISPLAY_HEIGHT - OLED_FONT_HEIGHT) / 2)
#define BONGO_HITS_MAX 999999
#define BONGO_RAISED_DURATION 50
#define BONGO_DOWN_DURATION 150

enum bongo_paw_states
{
    BONGO_PAW_IDLE,
    BONGO_PAW_RAISED,
    BONGO_PAW_DOWN
};

static const uint8_t bongo_cat_image[] = {
#embed "bitmaps/bongo_cat.bmp"
};

static const uint8_t bongo_paw_up_image[] = {
#embed "bitmaps/bongo_paw_up.bmp"
};

static const uint8_t bongo_paw_down_image[] = {
#embed "bitmaps/bongo_paw_down.bmp"
};

static const uint8_t bongo_paw_x[BONGO_PAW_COUNT] = { BONGO_PAW_LEFT_X, BONGO_PAW_RIGHT_X };

static uint32_t bongo_hits;
static uint16_t bongo_last_keycode;
static uint16_t bongo_paw_timer[BONGO_PAW_COUNT];
static uint8_t bongo_paw_state[BONGO_PAW_COUNT];
static uint8_t bongo_paw;
static bool bongo_redraw;

// The tally counts every key, even while another screen is drawn
static void bongo_count_hit(void)
{
    if (bongo_hits <= BONGO_HITS_MAX) bongo_hits++;
}

// Every strike starts raised so the paw is always seen coming down
static void bongo_key_event(uint16_t keycode, bool pressed)
{
    if (!pressed) return;

    if (keycode != bongo_last_keycode) bongo_paw ^= 1;
    bongo_last_keycode = keycode;

    // Restarting an already raised paw would starve its strike under fast repeats
    if (bongo_paw_state[bongo_paw] == BONGO_PAW_RAISED) return;

    bongo_paw_state[bongo_paw] = BONGO_PAW_RAISED;
    bongo_paw_timer[bongo_paw] = timer_read();
    bongo_redraw = true;
}

// Idle paws never read the timer, so it cannot wrap back into range
static void advance_paw(uint8_t paw)
{
    switch (bongo_paw_state[paw])
    {
        case BONGO_PAW_RAISED:
            if (timer_elapsed(bongo_paw_timer[paw]) < BONGO_RAISED_DURATION) return;
            bongo_paw_state[paw] = BONGO_PAW_DOWN;
            break;

        case BONGO_PAW_DOWN:
            if (timer_elapsed(bongo_paw_timer[paw]) < BONGO_DOWN_DURATION) return;
            bongo_paw_state[paw] = BONGO_PAW_IDLE;
            break;

        default:
            return;
    }

    bongo_paw_timer[paw] = timer_read();
    bongo_redraw = true;
}

static void bongo_cat_init(void)
{
    bongo_redraw = true;
}

static void bongo_cat_update(void)
{
    for (uint8_t paw = 0; paw < BONGO_PAW_COUNT; paw++) advance_paw(paw);

    if (!bongo_redraw) return;
    bongo_redraw = false;

    oled_clear();
    draw_image(bongo_cat_image, BONGO_CAT_X, BONGO_CAT_Y);

    for (uint8_t paw = 0; paw < BONGO_PAW_COUNT; paw++)
    {
        bool down = bongo_paw_state[paw] == BONGO_PAW_DOWN;
        draw_image(down ? bongo_paw_down_image : bongo_paw_up_image, bongo_paw_x[paw], BONGO_PAW_Y);
    }

    if (bongo_hits > BONGO_HITS_MAX)
    {
        draw_text(BONGO_HITS_X, BONGO_HITS_Y, "999999+");
    }
    else
    {
        draw_number(BONGO_HITS_X, BONGO_HITS_Y, bongo_hits);
    }
}

//==============================================================================
// Ripple screen
//==============================================================================

#define RIPPLE_FRAME_DURATION 33
#define RIPPLE_DAMPING 6 // Waves lose one part in 2^n of their height per frame
#define RIPPLE_DROP_RADIUS 2
#define RIPPLE_DROP_HEIGHT 1200
#define RIPPLE_CREST 60 // Height that lights a pixel

#define RIPPLE_CELLS (OLED_DISPLAY_WIDTH * OLED_DISPLAY_HEIGHT)

static int16_t ripple_heights[2][RIPPLE_CELLS];
static uint8_t ripple_front;
static uint16_t ripple_timer;

static void ripple_key_event(keypos_t key, bool pressed)
{
    uint8_t x, y;
    if (!pressed || !key_position(key, &x, &y)) return;

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

// The water keeps running while other screens are shown, so entering only redraws it
static void ripple_init(void)
{
    ripple_timer = timer_read();
    ripple_draw();
}

static void ripple_update(void)
{
    if (timer_elapsed(ripple_timer) < RIPPLE_FRAME_DURATION) return;
    ripple_timer = timer_read();

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

//==============================================================================
// Game of life screen
//==============================================================================

#define LIFE_FRAME_DURATION 120
#define LIFE_BYTES (OLED_PAGES * OLED_DISPLAY_WIDTH)

static uint8_t life_cells[LIFE_BYTES];
static uint8_t life_next[LIFE_BYTES];
static uint16_t life_timer;

static bool life_inside(int16_t x, int16_t y)
{
    return x >= 0 && x < OLED_DISPLAY_WIDTH && y >= 0 && y < OLED_DISPLAY_HEIGHT;
}

// Everything beyond the screen edges counts as dead, so the world has real walls
static bool life_cell(int16_t x, int16_t y)
{
    if (!life_inside(x, y)) return false;
    return life_cells[y / 8 * OLED_DISPLAY_WIDTH + x] >> (y % 8) & 1;
}

static void life_set_cell(uint8_t *cells, int16_t x, int16_t y, bool alive)
{
    if (!life_inside(x, y)) return;
    uint8_t mask = 1 << (y % 8);

    if (alive) cells[y / 8 * OLED_DISPLAY_WIDTH + x] |= mask;
    else cells[y / 8 * OLED_DISPLAY_WIDTH + x] &= ~mask;
}

// A pair of 3x3 rings, which collapse into a spreading burst of life
static void life_key_event(keypos_t key, bool pressed)
{
    uint8_t x, y;
    if (!pressed || !key_position(key, &x, &y)) return;

    for (int8_t side = -1; side <= 1; side += 2)
    {
        for (int8_t dy = -1; dy <= 1; dy++)
        {
            for (int8_t dx = -1; dx <= 1; dx++)
            {
                if (dx || dy) life_set_cell(life_cells, x + side * 2 + dx, y + dy, true);
            }
        }
    }
}

static void life_draw(void)
{
    for (uint16_t i = 0; i < LIFE_BYTES; i++) oled_write_raw_byte(life_cells[i], i);
}

// The colony keeps living while other screens are shown, so entering only redraws it
static void life_init(void)
{
    life_timer = timer_read();
    life_draw();
}

static void life_update(void)
{
    if (timer_elapsed(life_timer) < LIFE_FRAME_DURATION) return;
    life_timer = timer_read();

    for (uint8_t y = 0; y < OLED_DISPLAY_HEIGHT; y++)
    {
        for (uint8_t x = 0; x < OLED_DISPLAY_WIDTH; x++)
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

    memcpy(life_cells, life_next, sizeof(life_cells));

    life_draw();
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

//==============================================================================
// Screen select screen
//==============================================================================

#define SELECT_SQUARE_SIZE 7
#define SELECT_SQUARE_GAP 4
#define SELECT_SQUARE_Y 6
#define SELECT_NAME_Y (SELECT_SQUARE_Y + SELECT_SQUARE_SIZE + SELECT_SQUARE_GAP)
#define SELECT_SQUARE_PITCH (SELECT_SQUARE_SIZE + SELECT_SQUARE_GAP)
#define SELECT_SQUARE_LEFT ((OLED_DISPLAY_WIDTH - (USER_SCREEN_COUNT * SELECT_SQUARE_PITCH - SELECT_SQUARE_GAP)) / 2)

static const char *const select_names[] = { "Indicators", "Bongo cat", "Game of life", "Waves" };

_Static_assert(ARRAY_SIZE(select_names) == USER_SCREEN_COUNT, "Every user screen needs a name");
_Static_assert(USER_SCREEN_COUNT * SELECT_SQUARE_PITCH - SELECT_SQUARE_GAP <= OLED_DISPLAY_WIDTH, "Too many user screens to fit a row of squares");

static int select_shown_screen;
static bool select_redraw;

static void draw_square(uint8_t x, uint8_t y, bool filled)
{
    for (uint8_t iy = 0; iy < SELECT_SQUARE_SIZE; iy++)
    {
        for (uint8_t ix = 0; ix < SELECT_SQUARE_SIZE; ix++)
        {
            bool edge = ix == 0 || iy == 0 || ix == SELECT_SQUARE_SIZE - 1 || iy == SELECT_SQUARE_SIZE - 1;
            oled_write_pixel(x + ix, y + iy, filled || edge);
        }
    }
}

static void screen_select_init(void)
{
    select_redraw = true;
}

static void screen_select_update(void)
{
    if (!select_redraw && select_shown_screen == selected_user_screen) return;

    select_redraw = false;
    select_shown_screen = selected_user_screen;

    oled_clear();

    for (uint8_t screen = 0; screen < USER_SCREEN_COUNT; screen++)
    {
        draw_square(SELECT_SQUARE_LEFT + screen * SELECT_SQUARE_PITCH, SELECT_SQUARE_Y, screen == selected_user_screen);
    }

    draw_text(OLED_DISPLAY_WIDTH / 2, SELECT_NAME_Y, select_names[selected_user_screen]);
}

//==============================================================================
// Input lock screen
//==============================================================================

#define INPUT_LOCK_TEXT "Input lock"
#define INPUT_LOCK_ICON_X 23
#define INPUT_LOCK_ICON_Y 5
#define INPUT_LOCK_TEXT_X 74
#define INPUT_LOCK_TEXT_Y 12

static const uint8_t input_lock_image[] = {
#embed "bitmaps/lock.bmp"
};

static bool input_lock_redraw;

static void input_lock_init(void)
{
    input_lock_redraw = true;
}

static void input_lock_update(void)
{
    if (!input_lock_redraw) return;
    input_lock_redraw = false;

    draw_image(input_lock_image, INPUT_LOCK_ICON_X, INPUT_LOCK_ICON_Y);
    draw_text(INPUT_LOCK_TEXT_X, INPUT_LOCK_TEXT_Y, INPUT_LOCK_TEXT);
}

//==============================================================================
// Screen dispatch
//==============================================================================

// The simulations only run while drawn, so they must only be disturbed while drawn too
void oled_key_event(uint16_t keycode, keypos_t key, bool pressed)
{
    if (pressed) bongo_count_hit();

    // Keys that drive the screens themselves must not disturb their contents
    if (keycode == KRT_SCR) return;
    if (IS_QK_MOMENTARY(keycode)) return;

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
        oled_clear();

        switch (screen)
        {
            case USER_SCREEN_INDICATORS:   indicators_init();    break;
            case USER_SCREEN_BONGO_CAT:    bongo_cat_init();     break;
            case USER_SCREEN_LIFE:         life_init();          break;
            case USER_SCREEN_RIPPLE:       ripple_init();        break;
            case SYSTEM_SCREEN_LOGO:       logo_init();          break;
            case SYSTEM_SCREEN_SELECT:     screen_select_init(); break;
            case SYSTEM_SCREEN_INPUT_LOCK: input_lock_init();    break;
        }
    }

    switch (screen)
    {
        case USER_SCREEN_INDICATORS:   indicators_update();    break;
        case USER_SCREEN_BONGO_CAT:    bongo_cat_update();     break;
        case USER_SCREEN_LIFE:         life_update();          break;
        case USER_SCREEN_RIPPLE:       ripple_update();        break;
        case SYSTEM_SCREEN_LOGO:       logo_update();          break;
        case SYSTEM_SCREEN_SELECT:     screen_select_update(); break;
        case SYSTEM_SCREEN_INPUT_LOCK: input_lock_update();    break;
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

    uint8_t layer = get_highest_layer(layer_state);

    if (!logo_finished)
    {
        render_screen(SYSTEM_SCREEN_LOGO);
    }
    else if (layer >= INPUT_LOCK_LAYER)
    {
        render_screen(SYSTEM_SCREEN_INPUT_LOCK);
    }
    else if (layer == INDICATORS_LAYER)
    {
        // Layer 1 temporarily takes over the user selected screen
        render_screen(USER_SCREEN_INDICATORS);
    }
    else if (layer == SCREEN_SELECT_LAYER)
    {
        render_screen(SYSTEM_SCREEN_SELECT);
    }
    else
    {
        render_screen(selected_user_screen);
    }

    return false;
}
