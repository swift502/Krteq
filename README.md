# Krteq

## Parts

### Build from source data

- PCB
- Case

### Buy (keyboard)

- 61x Gateron KS-33B (GLP 3.0) switches
- 61x MX Low-Profile Shinethrough Keycaps
- Gateron 2u Low-Profile Plate Mounted Stabilizer

### Buy (electronics)

- Raspberry Pi Pico
- 6" (15cm) Panel Mount USB Extension Cable
- 3x 5mm LEDs
- 3x THT Resistors

### SMD Parts

The PCB is designed to be ordered from JLCPCB already assembled with these parts: 

| Part | Mfr | JLC |
| --- | --- |  --- |
| Mosfet | HX2302A | C296295 |
| Led | MHT151WDT | C401114 |
| Diode | 1N4148WS | C2128 |
| R330 | 0603WAF3300T5E | C23138 |
| R470 | 0603WAF4700T5E | C23179 |
| R10K | 0603WAF1002T5E | C25804 |

## Options

### Customization

- Case: Filament colors
- Inidicator LEDs:
    - Colors
    - Brightness (resistor values)

### Options

- PCB: Can be ordered from JLCPCB to have all SMD parts already soldered on
- MCU hot-swap: The RPi can be soldered directly, but the case can also fit low-profile pin headers to make the board replacable in case something goes wrong
- USB type: Micro B or USB C (RPI and extension cable variants)