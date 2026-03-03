# Krteq

An extended 5x12 keyboard with 2 extra keys. Intended for ortho layouts with a shifted number row, allowing for the placement of the delete and tilde keys above their usual spots. PCBs are designed in Kicad, case in Blender.

Krteq is a successor to [Krtkus](https://github.com/swift502/Krtkus), learning from it's lessons to make a design that's easier to build, more maintainable, better encapsulated and overall a more complete product.

- Low profile
- Hotswap
- Backlight (single color, breathing effect only)
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
- Rubber feet

### PCB

Fabrication files in [production/pcb](production/pcb) are provided in several manufacturing formats. If another format was needed, the source Kicad 9 project is available in [source/kicad](source/kicad). All the parts are SMD. The PCB is intended to be ordered already assembled for an easier (although more expensive) build process.

#### Suggested BOM

| Part | Manufacturer Part Number |
|---|---|
| Switch Diode | 1N4148W |
| Backlight LED | MHT151WDT |
| GSD NMOS | HX2302A |
| R330Ω | 0603WAF3300T5E |
| R470Ω | 0603WAF4700T5E |
| R10kΩ | 0603WAF1002T5E |
| Switch socket | CPG151101S11-16 |

### Case

Case files for 3D printing can be found in [production/stl](production/stl). Tested with PLA, 0.15mm layer height, 100% infill. 

Top case has a "Keychron" variant, which adjusts stabilizer cutouts for their non-standard [triangular stem design](https://www.keychron.com/blogs/news/the-design-details-of-our-keychron-low-profile-keyboard-stabilizers). Make sure to check if your keycaps use the straight or triangular stem design if you plan on using low-profile Keychron keycaps.

### Assembly

First, make sure to [flash the firmware](#flashing-windows) to the Pico before assembling anything.

Assuming you get the PCB pre-assembled, the only thing left to solder is the MCU. I recommend using a socket header for easy Pico replacement, but it can obviously be soldered directly as well. Once soldered or placed in the socket:

1. Attach the 2u stabilizer to the top case
2. Install the switches
3. Connect the USB cable
4. Push the top case down over the bottom case, it will snap in place
5. Install the keycaps

And you're done!

## QnA

### Why the unusual KS-33B switches?

The problem I had when trying to order a pre-assembled low-profile hotswap keyboard PCB was the lack of low-profile hotswap sockets offered by PCB manufacturers. Because KS-33Bs share footprint with regular MX switches, they can be used with the much more common and widely available regular hotswap sockets.

I'm making a big bet on these switches taking over current KS-33s, as the entire concept of getting a pre-assembled low-profile PCB completely relies on them.

### So is this board compatible with high profile switches?

Not now, but it would be trivial to make. Just raise the height of the top case by a few millimeters, and maybe adjust stabilizer cutouts.

### Why no RGB?

The simple backlighting was easier to implement and reduced the amount of components that could fail, which was desirable, as longevity was one of the main goals of this design.

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
python qmk_compile.py
```

### Flashing (Windows)

Hold the BOOTSEL button while connecting the Pico to a computer to have it show up as a storage device, then copy the compiled [firmware file](production/krteq_firmware.uf2) over to it.

Once the firmware is flashed, you can enter bootloader mode via a <kbd>LShift</kbd> + <kbd>RShift</kbd> + <kbd>B</kbd> key combination.

To revert to the default keymap, use a <kbd>LShift</kbd> + <kbd>RShift</kbd> + <kbd>C</kbd> key combination.

## Kicad 9

Switch grid:

| Grid | Size |
| --- | --- |
| Switch 1 | 19.05 |
| Switch 1/4 | 4.7625 |
| Switch 1/16 | 1.190625 |
| Switch 1/32 | 0.5953125 |
| Switch 1/64 | 0.29765625 |
