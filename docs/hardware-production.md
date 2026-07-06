# Hardware and production notes

The checked-in hardware is a single shared PCB for either the transmitter or
receiver role.

## Source and manufacturing files

| Path | Contents |
| --- | --- |
| `hardware/transmitter-or-receiver/Laser_Timer_PCB.kicad_pro` | KiCad project. |
| `hardware/transmitter-or-receiver/Laser_Timer_PCB.kicad_sch` | Schematic source. |
| `hardware/transmitter-or-receiver/Laser_Timer_PCB.kicad_pcb` | PCB layout source. |
| `production/transmitter-or-receiver/bom/Laser_Timer_PCBA_BoM.csv` | Generated PCBA bill of materials. |
| `production/transmitter-or-receiver/gerbers/` | Generated copper, mask, paste, silkscreen, edge-cuts, and drill outputs. |

The BoM includes an Arduino Nano, nRF24L01+PA+LNA module, 5 V and 3.3 V
regulators, buzzer, LCD connector, encoder connector, sensor connector, headers,
and test points. It does not include SD-card hardware even though receiver
firmware currently reserves `D8` as `sd_cs_pin`.

## Firmware-to-PCB pin map

| Function | Firmware pin/value | PCB net or connector |
| --- | --- | --- |
| Gate sensor output | `D2`, active `HIGH` | `/S_OUT`, J5 Sensor |
| Buzzer | `D1` | `/BUZZ`, BZ1 |
| Encoder switch | `D3`, `INPUT_PULLUP` | `/SW`, J4 Encoder |
| Encoder CLK | `D4` | `/CLK`, J4 Encoder |
| Encoder DAT | `D5` | `/DAT`, J4 Encoder |
| nRF24 CE | `D9` | `/CE`, RF1 |
| nRF24 CSN | `D10` | `/CSN`, RF1 |
| LCD I2C SDA | `A4` / I2C | `/SDA`, J3 LCD |
| LCD I2C SCL | `A5` / I2C | `/SCL`, J3 LCD |
| LCD address | `0x27` | Firmware value; confirm the display backpack matches. |
| Receiver SD CS placeholder | `D8` | `/D8`, J10 expansion header |

The firmware uses the same pin assignments for transmitter and receiver except
that receiver-specific modules implement the menu, EEPROM settings, finish
timer, and the `D8` CS placeholder.

## Power-enable mismatch to resolve before board changes

Both v2 sketches run this sequence before radio initialization:

```cpp
pinMode(A0, OUTPUT);
digitalWrite(A0, HIGH);
delay(RADIO_POWER_SETTLE_MS);
```

The checked-in KiCad PCB maps:

- Nano `A0` to net `/A0` and expansion header J6.
- Nano `A7` to net `/3V3_EN`.
- `/3V3_EN` to the MIC5504 3.3 V regulator enable pin.

That means the current firmware power-enable pin does not match the checked-in
PCB's 3.3 V regulator enable net. Before ordering boards, changing regulator
control, or debugging radio power-up failures, decide whether the firmware
should drive the PCB's `A7` enable net or whether a different board revision
wires `A0` to radio power.

## Production checklist

Before sending the files in `production/transmitter-or-receiver/` to a board
house or assembler:

1. Re-open the KiCad project and regenerate Gerbers/BoM from the source files if
   the schematic or PCB changed.
2. Confirm the hardware role-specific population/wiring for transmitter vs
   receiver. The PCB is shared, but the flashed sketch determines the unit role.
3. Confirm the LCD backpack address is `0x27` or update both sketches.
4. Confirm the nRF24 module power path and the `A0`/`A7` enable mismatch above.
5. Keep firmware pin constants synchronized with the PCB nets listed in this
   document.
