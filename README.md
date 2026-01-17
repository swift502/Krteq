# Krteq

## Parts

### PCB

The PCB is designed to be ordered from JLCPCB already assembled using the following parts.

| Part | Mfr | JLC |
| --- | --- |  --- |
| Diode | 1N4148WS | C2128 |
| LED | MHT151WDT | C401114 |
| Mosfet | HX2302A | C296295 |
| R330 | 0603WAF3300T5E | C23138 |
| R470 | 0603WAF4700T5E | C23179 |
| R10K | 0603WAF1002T5E | C25804 |

### Case

Print all case files, . Assembly requires no screws or glue, pieces just snap together.

### Purchase (keyboard)

- 61x Gateron KS-33B (GLP 3.0) switches
- 61x MX Low-Profile Shinethrough Keycaps
- Gateron 2u Low-Profile Plate Mounted Stabilizer

### Purchase (electronics)

- Raspberry Pi Pico
- 6" (15cm) Panel Mount USB Extension Cable
- 3x 5mm LEDs
- 3x THT Resistors

## Options

### Inidicator LEDs

The colors can be anything you like, just select resistor values to give them desired brightness. Here's the components I went with:

- Num Lock: Green, XXXΩ
- Caps Lock: Red, XXXΩ
- Scroll Lock: Blue, XXXΩ

### MCU hot-swap

The RPi can be soldered directly, but the design has enough space for low-profile pin headers to make the RPi replacable in case something goes wrong.

### USB type

Micro B is cheaper and the only official Pi Pico version. USB-C is obviously better but will likely be more expensive, especially the extension cable, and requires an unofficial third-party Pi Pico clone.
