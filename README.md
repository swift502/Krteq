![](images/1.webp)

# Krteq

[![](https://img.shields.io/badge/PCB-blue)](#pcb)
[![](https://img.shields.io/badge/Case-orange)](#case)
[![](https://img.shields.io/badge/Firmware-gray)](#firmware)

An extended 5x12 keyboard with 2 extra keys. Intended for ortho layouts with a shifted number row, allowing for the placement of the delete and tilde keys above their usual spots.

Krteq is a successor to [Krtkus](https://github.com/swift502/Krtkus), learning from its lessons to make a design that's easier to build, more maintainable, better encapsulated and overall a more complete product.

### Features

- Low profile
- Hotswap
- RGB backlight
- QMK/VIA compatible
- 3D printed case with a 7 degree tilt
- Tray mount
- 233mm × 121mm × 30mm

Connecting the keyboard to [usevia.app](https://usevia.app) requires manually uploading the [design file](production/krteq_via.json) in the design tab.

![](images/2.webp)

## Build guide

### Parts

- [PCB](#pcb)
- [Case](#case)
- Keycaps
- 61x Gateron KS-33 switch
- Gateron 2u low profile plate-mounted stabilizer

<!-- -->

- Raspberry Pi Pico
- USB-C breakout board
- OLED display module

<!-- -->

- 11x short wire
- 6x M2x6 screw
- 2x M2.5x6 screw
- Rubber feet

### Breakout boards

The **USB-C breakout board** is expected to match the specs of the Adafruit USB-C breakout board with two M2.5 screw holes 15.24mm apart, 2.54mm from the front edge, and built-in resistors for the CC lines.

The **OLED display module** is expected to be a 128x32 I2C OLED screen. These screens are not very standardized, so the PCB cutout is generous to fit modules with slightly different dimensions. Any OLED roughly equal or less than 38x12mm should fit, and at most require slight modifications to the display cutout in the plate.

> [!WARNING]
> TODO IMAGE

### Assembly

1. Perform the initial firmware flash on the Pico
1. Solder the Pico onto the PCB
1. Solder the PCB wire ends to the J1, J2 and J3 interfaces
1. Attach the stabilizer to the plate
1. Screw the USB board to the plate
1. Place the OLED module into plate's display slot
1. Connect the plate to the PCB by pushing switches through them
1. Solder component wire ends to the respective breakout boards
1. Screw the plate to the case
1. Put some keycaps on and you're done!

## PCB

The Kicad 10 project can be found in [source/kicad](source/kicad). All parts are SMD, the PCB is intended to be ordered with pre-assembled SMD components for an easier, although more expensive build process.

To generate fabrication files, open the Kicad project and use an export plugin provided by the PCB manufacturer you want to order from. The order process is then highly dependent on the manufacturer.

### Suggested BOM

| Part | Package | Manufacturer Part Number |
|---|---|---|
| Switch diode | SOD-123 | 1N4148W |
| Switch socket | SMD | KS-2P02B01-02 |
| RGB LED | SMD-4P,3.2x1.3mm | A-SP1513R6GHB1C-A01-2A |
| RGB driver | QFN-48-EP(6x6) | IS31FL3733B-QFLS4-TR |
| 0.1µF capacitor | 0402 | CL05B104KO5NNNC |
| 0.47µF capacitor | 0603 | CL10B474KA8NNNC |
| 22µF/10V capacitor | CASE-A-3216-18(mm) | T520A226M010ATE080 |
| 2kΩ resistor | 0603 | 0603WAF2001T5E |
| 20kΩ resistor | 0603 | 0603WAF2002T5E |
| 100kΩ resistor | 0603 | 0603WAF1003T5E |

## Case

Case blend file can be found in [source/Krteq.blend](source/Krteq.blend). It's designed non-destructively, so it should be fairly easy to modify if needed.

STL files for 3D printing can be found in [production/stl](production/stl). Recommended printing parameters are:

- PLA
- 100% infill
- 0.15mm layer height

The plate has a "keychron" variant, which adjusts stabilizer cutouts for their non-standard [triangular stem design](https://www.keychron.com/blogs/news/the-design-details-of-our-keychron-low-profile-keyboard-stabilizers).

## Firmware

QMK/VIA setup with a few custom features:

- custom keycodes for volume, screen and RGB control
- pressing <kbd>LShift</kbd> + <kbd>RShift</kbd> + <kbd>B</kbd> enters bootloader mode
- pressing <kbd>LShift</kbd> + <kbd>RShift</kbd> + <kbd>C</kbd> reverts to the default keymap

### Compiling (Windows)

Install:

- Python: https://www.python.org/
- MSYS: https://msys.qmk.fm

Run the compile script:

```sh
python source/scripts/qmk_compile.py
```

### Flashing (Windows)

For the initial flash, hold the BOOTSEL button while connecting the Pico to a computer to have it show up as a storage device, then copy the compiled [firmware file](production/krteq_firmware.uf2) over to it.

Once flashed, you can trigger the bootloader mode again by simply pressing the <kbd>LShift</kbd> + <kbd>RShift</kbd> + <kbd>B</kbd> key combination.

### Adding custom OLED screens

User keymaps can completely override oled rendering via `oled_task_user()`, but Krteq exposes a modular screen system, where screens are accessed using the `KRT_SCR` keycode.

You can hook into this built-in screen system and create a custom screen. Here's a minimal example of a custom screen definition in a user keymap:

```c
static void custom_start(void)
{
    oled_write_P(PSTR("Hello world"), false);
}

// Create a new screen instance
static const krteq_screen_t custom_screen = {
    .name = "Custom screen",
    .start = custom_start,
};

// Override the list of user screens
const krteq_screen_t *const user_screens[] = {
    &custom_screen
};
```

All fields of `krteq_screen_t` are optional, unset callbacks are simply skipped:

| Property     | Type                                                                   | Description |
| ------------ | ---------------------------------------------------------------------- | ----------- |
| `.name`      | `string`                                                               | Display name |
| `.start`     | `void start(void)`                                                     | Initialize screen state and initial draw |
| `.update`    | `void update(void)`                                                    | Refresh of the screen every OLED task cycle |
| `.key_event` | `void key_event(uint16_t keycode, uint8_t x, uint8_t y, bool pressed)` | Key press event, `keycode` is a QMK keycode (`KC_A`, `KC_SPC`), `x` (0-127) and `y` (0-31) are normalized screen coordinates of the pressed key relative to their physical position on the keyboard |
| `.reset`     | `void reset(void)`                                                     | Screen state reset |

You can combine custom screens with the built-in ones. The default screen configuration looks like this. You can reshuffle it, remove some of the screens and add your own.

```c
const krteq_screen_t *const user_screens[] = {
    &krteq_screen_indicators,
    &krteq_screen_bongo_cat,
    &krteq_screen_game_of_life,
    &krteq_screen_waves,
    &krteq_screen_ball_pit,
    &krteq_screen_system_info
};
```

Hooking into the built-in screen system is recommended as it ensures that other system features such as the boot logo, input lock and RGB overlays continue to work properly.

## Documentation

### QMK

- Info.json: https://docs.qmk.fm/reference_info_json
- Keycodes: https://docs.qmk.fm/keycodes
- Default keymap: https://docs.qmk.fm/configurator_default_keymaps
- RGB matrix: https://docs.qmk.fm/features/rgb_matrix
- OLED: https://docs.qmk.fm/features/oled_driver