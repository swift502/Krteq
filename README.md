# Krteq

An extended 5x12 keyboard with 2 extra keys. Intended for ortho layouts with a shifted number row, allowing for the placement of the delete and tilde keys above their usual spots. PCBs are designed in Kicad, case in Blender.

Krteq is a successor to [Krtkus](https://github.com/swift502/Krtkus), aiming to improve the former design to create a more maintainable, better encapsulated and overall more complete product.

- Low profile
- Hotswap
- Backlight (single color, no effects)
- QMK/VIA compatible
- Gateron LP 3.0 switches
- Screwless PCB edge mount
- 3D printed case with a 7 degree tilt
- ?mm × ?mm × ?mm

## Parts

- [PCB](#pcb)
- [Case](#case)
- Raspberry Pi Pico
- 15cm (6") USB Panel Mount Extension Cable
- 61x Gateron KS-33B (GLP 3.0) switches
- 61x MX Low-Profile Keycaps
- Gateron 2u Low-Profile Plate Mounted Stabilizer
- Rubber feet

## PCB

The PCB is designed to be ordered already assembled from JLCPCB with the following parts:

| Part | Mfr | JLC |
| :-: | --- | --- |
| Hotswap socket |  |  |
| D | 1N4148WS | C2128 |
| LED | MHT151WDT | C401114 |
| NMOS | HX2302A | C296295 |
| R100 | 0603WAF1000T5E | C22775 | >> lower for leds 82
| R330 | 0603WAF3300T5E | C23138 |
| R470 | 0603WAF4700T5E | C23179 | >> use for all gate resistors
| R10K | 0603WAF1002T5E | C25804 |
| R100K | 0603WAF1003T5E | C25803 | >> use 10k instead

## Case

## Modularity

