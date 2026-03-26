# Krteq

An extended 5x12 keyboard with 2 extra keys. Intended for ortho layouts with a shifted number row, allowing for the placement of the delete and tilde keys above their usual spots. PCBs are designed in Kicad, case in Blender.

Krteq is a successor to [Krtkus](https://github.com/swift502/Krtkus), learning from it's lessons to make a design that's easier to build, more maintainable, better encapsulated and overall a more complete product.

- Low profile
- Hotswap
- White backlight (only breathing effect)
- QMK/VIA compatible
- Gateron KS-33B (GLP 3.0) switches
- 3D printed case with a 7 degree tilt
- Tray mount PCB, integrated plate
- 233mm × 127mm × 30mm

Connecting the keyboard to [usevia.app](https://usevia.app) requires manually uploading the [design file](production/krteq_via.json) in the design tab.

## Build

### Parts

- [PCB](#pcb)
- [Case](#case)
- Raspberry Pi Pico
- 30cm (1ft) USB Panel-Mount Extension Cable
- 61x Gateron KS-33B (GLP 3.0) switches
- 61x MX Low-Profile Keycaps
- Gateron 2u Low-Profile Plate-Mounted Stabilizer
- 2x M2x6 screws
- Rubber feet

### PCB

Fabrication files in [production/pcb](production/pcb) are provided in several manufacturing formats. If another format was needed, the source Kicad 10 project is available in [source/kicad](source/kicad). All the parts are SMD. The PCB is intended to be ordered already assembled for an easier (although more expensive) build process.

#### Suggested BOM

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

### Case

Case files for 3D printing can be found in [production/stl](production/stl). Tested with PLA, 0.15mm layer height, 100% infill.

Top case has a "Keychron" variant, which adjusts stabilizer cutouts for their non-standard [triangular stem design](https://www.keychron.com/blogs/news/the-design-details-of-our-keychron-low-profile-keyboard-stabilizers). Make sure to check if your keycaps use the straight or triangular stem design if you plan on using low-profile Keychron keycaps.

### Assembly

First, make sure to [flash the firmware](#flashing-windows) to the Pico before assembling anything.

Assuming you get the PCB pre-assembled, the only thing left to solder is the MCU. I recommend using a socket header for easy Pico replacement, but it can obviously be soldered directly as well.

1. Install the Pico onto the PCB (socketed or soldered)
2. Attach the 2u stabilizer to the top case
3. Push few of the switches through the top case and into the PCB sockets, just enough to hold the two parts together
4. Connect the panel mount USB cable to the Pico and screw the other end to the case's USB port cutout
5. Slide the top assembly into the bottom case rails, and secure it with three M2x6 screws from the bottom
6. Install the remaining switches and keycaps

And you're done!

## QnA

### Why the unusual KS-33B switches?

The problem I had when trying to order a pre-assembled low-profile hotswap keyboard PCB was the lack of low-profile hotswap sockets offered by PCB manufacturers. Because KS-33Bs share footprint with regular MX switches, they can be used with the much more common and widely available regular hotswap sockets.

I'm making a big bet on these switches taking over current KS-33s, as the entire concept of getting a pre-assembled low-profile PCB hinges on them.

### So is this board compatible with high profile switches?

Not now, but it would be trivial to make. Just raise the height of the top case by a few millimeters, and maybe adjust stabilizer cutouts.

### Why no RGB?

The simple backlighting was easier to implement and reduced the amount of components that could fail, which aligned with this project's goal of maximizing longevity.

## QMK

### Documentation

- Info json: https://docs.qmk.fm/reference_info_json
- Keycodes: https://docs.qmk.fm/keycodes
- Default keymap: https://docs.qmk.fm/configurator_default_keymaps
- Backlight: https://docs.qmk.fm/features/backlight

### Compiling (Windows)

Install:

- Python: https://www.python.org/
- MSYS: https://msys.qmk.fm

Run the compile script:

```sh
python scripts/qmk_compile.py
```

### Flashing (Windows)

Hold the BOOTSEL button while connecting the Pico to a computer to have it show up as a storage device, then copy the compiled [firmware file](production/krteq_firmware.uf2) over to it.

Once the firmware is flashed, you can enter bootloader mode via a <kbd>LShift</kbd> + <kbd>RShift</kbd> + <kbd>B</kbd> key combination.

To revert to the default keymap, use a <kbd>LShift</kbd> + <kbd>RShift</kbd> + <kbd>C</kbd> key combination.

## Kicad

Switch grid:

| Grid | Size |
| --- | --- |
| Switch 1 | 19.05 |
| Switch 1/4 | 4.7625 |
| Switch 1/16 | 1.190625 |
| Switch 1/32 | 0.5953125 |
| Switch 1/64 | 0.29765625 |
