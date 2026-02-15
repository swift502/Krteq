# Krteq

An extended 5x12 keyboard with 2 extra keys. Intended for ortho layouts with a shifted number row, allowing for the placement of the delete and tilde keys above their usual spots. PCBs are designed in Kicad, case in Blender.

Krteq is a successor to [Krtkus](https://github.com/swift502/Krtkus), learning from it's lessons to make a design that's easier to build, more maintainable, better encapsulated and overall a more complete product.

- Low profile
- Hotswap
- Backlight (single color, no effects)
- QMK/VIA compatible
- Gateron KS-33B (GLP 3.0) switches
- 3D printed case with a 7 degree tilt
- Tray mount PCB, integrated plate
- 233mm × 127mm × 30mm

Connecting the keyboard to [usevia.app](https://usevia.app) requires manually uploading the [design file](production/krteq_via.json) in the design tab.

## Parts

- [PCB](#pcb)
- [Case](#case)
- Raspberry Pi Pico
- 30cm (1ft) USB Panel-Mount Extension Cable
- 61x Gateron KS-33B (GLP 3.0) switches
- 61x MX Low-Profile Keycaps
- Gateron 2u Low-Profile Plate-Mounted Stabilizer
- Rubber feet

## Build

Most of the PCB is meant to come pre-assembled from a PCB manufacturer. In that case, the only thing left to solder is the MCU. I recommend using a socket header for easy Pico replacement, but it can obviously be soldered directly as well.

Once the MCU is soldered, connect the cable. Screw the cable's panel end to the inside wall of the case, then connect the other end to the Pico. The cable is necessary to rotate the USB port orientation, but also to protect Pico's onboard USB port from snapping, which can happen if it's stressed over time.

Finally, slide the top case over the whole assembly, place the switches and keycaps, and you're done!

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

Top case has a "Keychron" variant, which adjusts stabilizer cutouts for their non-standard [triangular stem design](https://www.keychron.com/blogs/news/the-design-details-of-our-keychron-low-profile-keyboard-stabilizers). Make sure to check if your keycaps use the straight or triangular stem design if you plan on using low-profile Keychron keycaps.

## QnA

### Why the rare KS-33B switches?

The main issue I faced when trying to order a pre-assembled low-profile keyboard PCB was the lack of low-profile hotswap sockets offered by PCB manufacturers. Because KS-33Bs share footprint with regular MX switches, they can be used with the widely available regular hotswap sockets.

I'm making a big bet on these switches becoming mainstream to keep this design viable long term. The moment they stop selling, this design becomes pretty much useless.

### So is this board compatible with regular high profile switches?

No, but it would be trivial to make it compatible. Literally just raise the height of the top case by a few millimeters. Maybe adjust stabilizer cutouts. But as it stands, I'm not planning on making this variant unless by some miracle there was a demand for it.

### Why no RGB?

The simple version of backlighting was a lot easier to implement hardware wise, and reduced the amount of components which could fail. Long term reliability was one of the main goals of this design. Also, it's obviously just not that important to me, and I realistically don't expect anyone but me to use this keyboard.

## QMK

### Documentation

- Info json: https://docs.qmk.fm/reference_info_json
- Keycodes: https://docs.qmk.fm/keycodes
- Default keymap: https://docs.qmk.fm/configurator_default_keymaps

### Compiling (Windows)

Install:

- Python: https://www.python.org/
- MSYS: https://msys.qmk.fm

Run the compile script:

```sh
python qmk_compile.py
```

### Flashing (Windows)

Flash the firmware before assembling the case for the first time. Hold the BOOTSEL button while connecting the Pico to have it show up as a storage device, then copy the compiled [firmware file](production/krteq_firmware.uf2) to it.

Once the firmware is flashed, you can enter bootloader mode via a <kbd>LShift</kbd> + <kbd>RShift</kbd> + <kbd>B</kbd> key combination.

To revert to the default keymap by clearing the EEPROM, use a <kbd>LShift</kbd> + <kbd>RShift</kbd> + <kbd>C</kbd> key combination.

## Kicad 9

Switch grid:

| Grid | Size |
| --- | --- |
| Switch 1 | 19.05 |
| Switch 1/4 | 4.7625 |
| Switch 1/16 | 1.190625 |
| Switch 1/32 | 0.5953125 |
| Switch 1/64 | 0.29765625 |
