# Krteq

[![](https://img.shields.io/badge/PCB-blue)](#pcb)
[![](https://img.shields.io/badge/Case-orange)](#case)
[![](https://img.shields.io/badge/Firmware-gray)](#firmware)

An extended 5x12 keyboard with 2 extra keys. Intended for ortho layouts with a shifted number row, allowing for the placement of the delete and tilde keys above their usual spots.

Krteq is a successor to [Krtkus](https://github.com/swift502/Krtkus), learning from its lessons to make a design that's easier to build, more maintainable, better encapsulated and overall a more complete product.

### Features

- Low profile
- Hotswap
- White backlight (only breathing effect)
- QMK/VIA compatible
- Gateron KS-33B (GLP 3.0) switches
- 3D printed case with a 7 degree tilt
- Tray mount PCB, integrated plate
- 233mm × 127mm × 30mm

Connecting the keyboard to [usevia.app](https://usevia.app) requires manually uploading the [design file](production/krteq_via.json) in the design tab.

## PCB

The Kicad 10 project can be found in [source/kicad](source/kicad). All parts are SMD. The PCB is intended to be ordered pre-assembled for an easier, although more expensive build process.

Suggested resistor values correspond to max safe LED brightness, which is later adjusted using software. Backlight intensity is controlled by QMK's backlight system, and indicator brightness is adjustable via custom PWM code.

To generate fabrication files, open the Kicad project and use an export plugin provided by one of the PCB manufacturers you want to order from. The order process is then highly dependent on the manufacturer.

### Suggested BOM

| Part | Manufacturer Part Number |
|---|---|
| Switch Diode | 1N4148W |
| Backlight LED | MHT151WDT |
| GSD NMOS | HX2302A |
| R82Ω | 0603WAF820JT5E |
| R330Ω | 0603WAF3300T5E |
| R470Ω | 0603WAF4700T5E |
| R10kΩ | 0603WAF1002T5E |
| Switch socket | CPG151101S11-16 |

## Case

Case is designed non-destructively in Blender, so it should be fairly easy to modify if needed. STL files for 3D printing can be found in [production/stl](production/stl). Tested with PLA, 0.15mm layer height, 100% infill.

You'll need to print at least 3 files. Top and bottom in one or two colors, and the LED cover strip using transparent filament.

The top and bottom parts have a "keychron" variant, which adjusts stabilizer cutouts for their non-standard [triangular stem design](https://www.keychron.com/blogs/news/the-design-details-of-our-keychron-low-profile-keyboard-stabilizers).

## Firmware

A pre-compiled firmware file can be found here [production/krteq_firmware.uf2](production/krteq_firmware.uf2).

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

## Build guide

### Parts

- [PCB](#pcb)
- [Case](#case)
- Raspberry Pi Pico
- 30cm (1ft) USB panel-mount extension cable
- 61x Gateron KS-33B (GLP 3.0) switches
- 61x MX low profile keycaps
- Gateron 2u low profile plate-mounted stabilizer
- 3x M2x6 flat head screw
- Rubber feet

### Assembly

Assuming you get the PCB pre-assembled, the only thing left to solder is the MCU. I recommend using a socket header for easy Pico replacement, but it can obviously be soldered directly as well.

1. Install the Pico onto the PCB (socketed or soldered)
2. Attach the indicator LED cover strip to the top case
3. Attach the 2u stabilizer to the top case
4. Push the corner switches through the top case and into the PCB sockets to hold the two parts together
5. Connect the panel mount USB cable to the Pico and screw the other end to the case's USB port cutout

> [!IMPORTANT]
> Now is your last chance to do the initial [firmware flash](#flashing-windows). After this point the Bootsel button won't be accessible. I also recommend plugging the keyboard in and testing it works at this point. If you find an issue, it's still very easy to test connectivity or swap out the Pico.

6. Slide the top assembly into the bottom case rails
7. Screw the top and bottom cases together using the 3 M2x6 screws
8. Install the remaining switches and keycaps

And you're done!

## QnA

### Why the unusual KS-33B switches?

The problem I had when trying to order a pre-assembled low profile hotswap keyboard PCB was the lack of low profile hotswap sockets offered by PCB manufacturers. Because KS-33Bs share footprint with regular MX switches, they can be used with the much more common and widely available regular hotswap sockets.

I'm making a big bet on these switches taking over current KS-33s, as the entire concept of getting a pre-assembled low profile PCB hinges on them.

### So is this board compatible with high profile switches?

Footprint-wise, yes, but currently there's no compatible high-profile top case variant. However, it would be trivial to make one if there was demand for it.

### Why no RGB?

The simple backlighting was a lot easier to implement and reduced the number of components which could fail, which aligned with this project's goal of maximizing longevity.

### Why include scroll lock indicator?

I really wanted an LED for my custom letter accent input method. I wanted it to be an actual compose LED, which would be supported by QMK, but unfortunately Windows doesn't implement the compose lock state. So ultimately the third LED ended up being a regular scroll lock. I still like it as a general purpose LED that can be used for custom functionality via AHK scripts, especially because it's not labeled in any specific way.

## Documentation

### QMK

- Info.json: https://docs.qmk.fm/reference_info_json
- Keycodes: https://docs.qmk.fm/keycodes
- Default keymap: https://docs.qmk.fm/configurator_default_keymaps
- Backlight: https://docs.qmk.fm/features/backlight

### Kicad

Switch grid reference table:

| Grid | Size |
| --- | --- |
| Switch 1 | 19.05 |
| Switch 1/4 | 4.7625 |
| Switch 1/16 | 1.190625 |
| Switch 1/32 | 0.5953125 |
| Switch 1/64 | 0.29765625 |