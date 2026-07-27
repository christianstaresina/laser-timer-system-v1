# Hardware and production notes

The checked-in hardware is a single shared PCB that can be built as either the
transmitter or the receiver. Firmware role is selected by which sketch is
flashed.

## Source and manufacturing files

| Path | Contents |
| --- | --- |
| `hardware/transmitter-or-receiver/Laser_Timer_PCB.kicad_pro` | KiCad project. |
| `hardware/transmitter-or-receiver/Laser_Timer_PCB.kicad_sch` | Schematic. |
| `hardware/transmitter-or-receiver/Laser_Timer_PCB.kicad_pcb` | PCB layout. |
| `production/transmitter-or-receiver/bom/Laser_Timer_PCBA_BoM.csv` | PCBA bill of materials. |
| `production/transmitter-or-receiver/gerbers/` | Copper, mask, paste, silkscreen, edge-cuts, and drill outputs. |
| `docs/Laser_Timer_Schematic.pdf` | Exported schematic PDF. |
| `docs/Laser_Timer_PCBA.jpg` | Assembled board photo. |

The BoM includes an Arduino Nano, nRF24L01+PA+LNA, 5 V and 3.3 V regulators,
buzzer, LCD / encoder / sensor connectors, headers, and test points. It does
not include SD-card hardware even though RX firmware reserves `D8` as
`sd_cs_pin`.

## Firmware-to-PCB pin map

Verified against sketch pin constants and Nano pad nets in
`Laser_Timer_PCB.kicad_pcb`:

| Function | Firmware | PCB net / connector |
| --- | --- | --- |
| Gate sensor | `D2`, active `HIGH` | `/S_OUT`, J5 Sensor |
| Buzzer | `D1` | `/BUZZ`, BZ1 (Nano pad `D1/TX`) |
| Encoder switch | `D3`, `INPUT_PULLUP` | `/SW`, J4 Encoder |
| Encoder CLK | `D4` | `/CLK`, J4 Encoder |
| Encoder DAT | `D5` | `/DAT`, J4 Encoder |
| nRF24 CE | `D9` | `/CE`, RF1 |
| nRF24 CSN | `D10` | `/CSN`, RF1 |
| LCD SDA / SCL | `A4` / `A5` | `/SDA` / `/SCL`, J3 LCD |
| LCD address | `0x27` | Firmware; confirm backpack matches |
| RX SD CS placeholder | `D8` | `/D8`, expansion header |
| Firmware radio power enable | `A0` driven high | Nano pad `A0` → `/A0` (expansion), **not** regulator enable |
| PCB 3.3 V regulator enable | — | Nano pad `A7` → `/3V3_EN` |

Transmitter and receiver share the same pin map; RX-only modules implement the
menu, EEPROM settings, finish timer, and the `D8` placeholder.

## Power-enable mismatch

Both v2 sketches run this before radio init:

```cpp
pinMode(A0, OUTPUT);
digitalWrite(A0, HIGH);
delay(RADIO_POWER_SETTLE_MS); // 500 ms
```

Checked-in KiCad mapping:

- Nano `A0` → net `/A0` (expansion / header path).
- Nano `A7` → net `/3V3_EN` → MIC5504 3.3 V regulator enable.

So the firmware “radio power settle” pin is **not** the PCB regulator enable
net. On ATmega328P Nano, `A7` is **analog-input-only** and cannot drive
`pinMode()` / `digitalWrite()`, so renaming the firmware constant from `A0` to
`A7` is not a valid fix.

Before ordering boards, changing regulator control, or debugging radio power-up
failures, choose an explicit hardware strategy:

1. Board revision: route regulator enable to a digital-capable GPIO (for example
   `A0`) and match firmware, or
2. Strap `/3V3_EN` to the required level if software control is not needed.

Confirm intended power sequencing before either option.

## Production checklist

1. If schematic/PCB changed, regenerate Gerbers and BoM from KiCad into
   `production/transmitter-or-receiver/` (do not ship stale exports).
2. Confirm TX vs RX population/wiring for the shared PCB; the flashed sketch
   selects the role.
3. Confirm LCD backpack address `0x27` or update both sketches.
4. Resolve the `A0` / `A7`/`3V3_EN` mismatch above; do not use `A7` as a digital
   output on Nano.
5. Keep firmware pin constants synchronized with the nets in this document.
6. Remember `D1` is shared with UART TX — buzzer loading can affect uploads.
