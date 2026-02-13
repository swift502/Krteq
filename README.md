# Krteq

An extended 5x12 keyboard with 2 extra keys. Intended for ortho layouts with a shifted number row, allowing for the placement of the delete and tilde keys above their usual spots. PCBs are designed in Kicad, case in Blender.

Krteq is a successor to [Krtkus](https://github.com/swift502/Krtkus), learning from it's lessons to make a design that's easier to build, more maintainable, better encapsulated and overall a more complete product.

- Low profile
- Hotswap
- Backlight (single color, no effects)
- QMK/VIA compatible
- Gateron LP 3.0 switches
- 3D printed case with a 7 degree tilt
- Tray mount PCB, integrated plate
- 233mm × 127mm × 30mm

## Parts

- [PCB](#pcb)
- [Case](#case)
- Raspberry Pi Pico
- 30cm (1ft) USB Panel-Mount Extension Cable
- 61x Gateron KS-33B (GLP 3.0) switches
- 61x MX Low-Profile Keycaps
- Gateron 2u Low-Profile Plate-Mounted Stabilizer
- Rubber feet

## PCB

Fabrication files in [production/pcb](production/pcb) are provided in several manufacturing formats. Additional formats can easily be exported from the Kicad 9 project if needed. All the parts are SMD. The PCB was meant to be ordered already assembled for an easier (although more expensive) build process.

### Suggested BOM

| Part | Manufacturer Part Number |
|---|---|
| Switch Diode | 1N4148W |
| Backlight LED | MHT151WDT |
| GSD NMOS | HX2302A |
| R82Ω | 0603WAF820JT5E |
| R470Ω | 0603WAF4700T5E |
| R10kΩ | 0603WAF1002T5E |
| R330Ω | 0603WAF3300T5E |
| Switch socket | CPG151101S11-16 |

## Case

Case can be found in [production/case](production/case). Designed for PLA, 100% infill, 0.15mm. The top and the bottom pieces just snap together, no screws needed.  The PCB is then sandwiched between the two parts.

Top case has a "Keychron" variant, which adjusts stabilizer cutout for their non-standard [triangular stem design](https://www.keychron.com/blogs/news/the-design-details-of-our-keychron-low-profile-keyboard-stabilizers). Make sure to check if your keycaps use the straight or triangular stem design if you plan on using low-profile Keychron keycaps.

## Build

Most of the PCB is meant to come pre-assembled from a PCB manufacturer. In that case, the only thing left to solder is the MCU. I recommend using a socket header for easy Pico replacement, but it can obviously be soldered directly as well.

Once the MCU is soldered, assemble the cable. Screw the cable's panel end to the inside wall of the case, then connect the other end to the Pico. The cable is used to correct USB port orientation, but also to protect Pico's USB port from snapping, which can happen if it's stressed over time.

Once the cable is secured and connected like this, you can assemble the case by just sliding the top part over the bottom part. Make sure the cable doesn't get squeezed. Finally, assemble the switches and keycaps, and you're done!

## QMK

### Documentation

- Info: https://docs.qmk.fm/reference_info_json
- Keycodes: https://docs.qmk.fm/keycodes

### Compiling (Windows)

Install:

- Python: https://www.python.org/
- MSYS: https://msys.qmk.fm

Run the compile script:

```sh
python qmk_compile.py
```

### Flashing (Windows)

Flash the Pico before assembling the case for the first time. Hold the BOOTSEL button while connecting the USB cable to have it show up as a storage device, then copy the compiled .uf2 file to it.

Once the firmware is uploaded, flashing can be triggered via the <kbd>LShift</kbd> + <kbd>RShift</kbd> + <kbd>B</kbd> key combination.

To clear keymap overrides and revert to the default keymap, use the <kbd>LShift</kbd> + <kbd>RShift</kbd> + <kbd>C</kbd> key combination.

```sh

### Resetting

Once the firmware is flashed, it provides key combinations to enter bootloader or clear the keyboard's persistent storage.

- <kbd>LShift</kbd> + <kbd>RShift</kbd> + <kbd>B</kbd> - Puts the keyboard into bootloader mode for flashing
- <kbd>LShift</kbd> + <kbd>RShift</kbd> + <kbd>C</kbd> - Clears EEPROM and reverts to the default keymap

## Kicad 9

Switch grid reference:

| Grid | Size |
| --- | --- |
| Switch 1 | 19.05 |
| Switch 1/4 | 4.7625 |
| Switch 1/16 | 1.190625 |
| Switch 1/32 | 0,5953125 |
| Switch 1/64 | 0.29765625 |