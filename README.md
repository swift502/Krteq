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
- 233mm × 125mm × 30mm

Connecting the keyboard to [usevia.app](https://usevia.app) requires manually uploading the [design file](production/krteq_via.json) in the design tab.

![](images/2.webp)

## Build guide

### Parts

- [PCB](#pcb)
- [Case](#case)
- Raspberry Pi Pico
- 61x Gateron KS-33 switches
- 61x MX low profile keycaps
- Gateron 2u low profile plate-mounted stabilizer
- 6x M2x6 flat head screw
- 2x M2.5x5 screw
- Rubber feet

> [!WARNING]
> TODO Verify length of all screws

### Breakout boards

- 128x32 OLED display breakout
- Adafruit style USB-C breakout

> [!WARNING]
> TODO IMAGE

### Assembly

> [!WARNING]
> TODO Update assembly guide

1. Order pre-assembled PCB from a manufacturer of your choice
1. Perform the initial firmware flash on the Pico
1. Solder the Pico onto the PCB
1. Solder the OLED breakout onto the PCB
1. Screw the USB breakout to the top case
1. Solder connecting wires between the USB breakout and the Pico TP pads
1. Attach the 2u stabilizer to the top case
1. Screw the top case to the bottom case
1. Install the switches and keycaps

And it's done!

## PCB

The Kicad 10 project can be found in [source/kicad](source/kicad). All parts are SMD. The PCB is intended to be ordered pre-assembled for an easier, although more expensive build process.

To generate fabrication files, open the Kicad project and use an export plugin provided by one of the PCB manufacturers you want to order from. The order process is then highly dependent on the manufacturer.

### Footprints ToDo

- switch diode ✓
- switch socket ✓
- ks-33 1u ✓
- rgb led

### 3D models ToDo

- switch socket ✓
- ks-33 ✓
- rgb led ✓

### Suggested BOM

| Part | Manufacturer Part Number |
|---|---|
| Switch diode | 1N4148W |
| Switch socket | CPG151101S11-2 |
| RGB LED | A-SP1513R6GHB1C-A01-2A |
| RGB driver | IS31FL3733-QFLS4-TR |
| 0.1µF capacitor | CL05B104KO5NNNC |
| 0.47µF capacitor | CL10B474KA8NNNC |
| 2kΩ resistor | 0603WAF2001T5E |
| 20kΩ resistor | 0603WAF2002T5E |
| 100kΩ resistor | 0603WAF1003T5E |

## Case

Case is designed non-destructively in Blender here [source/Krteq.blend](source/Krteq.blend), so it should be fairly easy to modify if needed. STL files for 3D printing can be found in [production/stl](production/stl). Tested with PLA, 0.15mm layer height, 100% infill.

The top part also has a "keychron" variant, which adjusts stabilizer cutouts for their non-standard [triangular stem design](https://www.keychron.com/blogs/news/the-design-details-of-our-keychron-low-profile-keyboard-stabilizers).

## Firmware

QMK/VIA setup with a few custom features:

- custom `KRT_VOL` keycode for combined volume control
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

## Documentation

### QMK

- Info.json: https://docs.qmk.fm/reference_info_json
- Keycodes: https://docs.qmk.fm/keycodes
- Default keymap: https://docs.qmk.fm/configurator_default_keymaps
- RGB matrix: https://docs.qmk.fm/features/rgb_matrix
- OLED: https://docs.qmk.fm/features/oled_driver

### Kicad

Switch grid reference table:

| Grid | Size |
| --- | --- |
| Switch 1 | 19.05 |
| Switch 1/4 | 4.7625 |
| Switch 1/16 | 1.190625 |
| Switch 1/32 | 0.5953125 |
| Switch 1/64 | 0.29765625 |