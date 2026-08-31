# Hardware and production notes

The checked-in hardware is a single shared V1 PCB that can be built as either
the transmitter or the receiver. Role is selected by which firmware sketch is
flashed — there is no separate TX/RX KiCad project or BoM.

KiCad 8.0 source (`generator_version 8.0`); title block **Laser Timer**,
rev **V1.0**, date **2024-08-17**. Silkscreen brand text is `Laser Timer V1.0` /
`Aug. 2024` only — no TX/RX role marking.

## Source and manufacturing files

| Path | Contents |
| --- | --- |
| `hardware/transmitter-or-receiver/Laser_Timer_PCB.kicad_pro` | KiCad project. |
| `hardware/transmitter-or-receiver/Laser_Timer_PCB.kicad_sch` | Schematic source. |
| `hardware/transmitter-or-receiver/Laser_Timer_PCB.kicad_pcb` | PCB layout source. |
| `hardware/transmitter-or-receiver/Laser_Timer_PCB-backups/` | Dated KiCad backup zips (not fab packages). |
| `production/transmitter-or-receiver/bom/Laser_Timer_PCBA_BoM.csv` | PCBA bill of materials. |
| `production/transmitter-or-receiver/gerbers/` | Copper, mask, paste, silkscreen, edge-cuts, PTH/NPTH drills. |
| `production/transmitter-or-receiver/gerbers/Laser_Timer_PCB_V1.zip` | Zip of the Gerber/drill set for fab. |
| `docs/Laser_Timer_Schematic.pdf` | Exported schematic PDF. |
| `docs/Laser_Timer_PCBA.jpg` | Assembled board photo. |

Gerber naming: `Laser_Timer_PCB_V1-<layer>.<ext>`. Export metadata in the
copper file: Pcbnew **8.0.1**, `2024-08-17T23:53:32-04:00`, project
`Laser_Timer_PCB`, rev V1.0.

### Connectors (BoM)

| Ref | BoM value | Firmware use |
| --- | --- | --- |
| `J1` | Pwr Btn and Battery | Power path; not driven by v2 sketches. |
| `J3` | LCD | I2C 16×2 backpack; firmware address `0x27`. |
| `J4` | Encoder | RX menu; TX reads the button only (menu stub). |
| `J5` | Sensor | Gate beam, D2, active `HIGH` when broken. |
| `J2`, `J6`–`J10` | Expansion / headers | Includes `/D8` (RX holds high) and `/A0`. |

### BoM highlights

From `Laser_Timer_PCBA_BoM.csv` (DNP column empty — no TX/RX population notes):

| Ref | Part |
| --- | --- |
| `A1` | Arduino Nano v3.x |
| `RF1` | nRF24L01+PA+LNA |
| `U1` | AZ1117 5.0 V LDO |
| `U2` | MIC5504 3.3 V LDO (schematic symbol `MIC5219-3.3YM5`; value MIC5504) |
| `BZ1` | Buzzer |
| `J3` / `J4` / `J5` | LCD / Encoder / Sensor |

No display IC and no SD-card part appear on the BoM. The LCD is an external
module on `J3`. RX firmware still holds `D8` high as `sd_cs_pin`.

## Firmware-to-PCB pin map

Verified against sketch pin constants and Nano pad nets in
`Laser_Timer_PCB.kicad_pcb`:

| Function | Firmware | PCB net / connector |
| --- | --- | --- |
| Gate sensor | `D2`, active `HIGH` | `/S_OUT`, J5 Sensor |
| Buzzer | `D1` | `/BUZZ`, BZ1 (Nano pad `D1` / UART TX) |
| Encoder switch | `D3`, `INPUT_PULLUP` | `/SW`, J4 Encoder |
| Encoder CLK | `D4`, `INPUT` | `/CLK`, J4 Encoder |
| Encoder DAT | `D5`, `INPUT` | `/DAT`, J4 Encoder |
| nRF24 CE | `D9` | `/CE`, RF1 |
| nRF24 CSN | `D10` | `/CSN`, RF1 |
| LCD SDA / SCL | `A4` / `A5` | `/SDA` / `/SCL`, J3 LCD |
| LCD address | `0x27` | Firmware; confirm backpack matches |
| RX SD CS placeholder | `D8` | `/D8`, expansion header |
| Firmware radio power enable | `A0` driven high | Nano pad `A0` → `/A0` (expansion), **not** regulator enable |
| PCB 3.3 V regulator enable | — | Nano pad `A7` → `/3V3_EN` → U2 |
| Battery ADC (unused by v2) | — | Nano pad `A6` → `/+BAT_ADC` |

Transmitter and receiver share this pin map. RX-only firmware implements the
menu, EEPROM settings, finish timer, and the `D8` placeholder. Encoder CLK/DAT
have no MCU pull-ups; the encoder assembly must provide them if needed.

## Power-enable mismatch

Both v2 sketches run this before radio init:

```cpp
pinMode(A0, OUTPUT);
digitalWrite(A0, HIGH);
delay(RADIO_POWER_SETTLE_MS); // 500 ms
```

Checked-in KiCad mapping:

- Nano `A0` → net `/A0` (expansion / header path).
- Nano `A7` → net `/3V3_EN` → U2 (MIC5504) enable.

The firmware “radio power settle” pin is **not** the PCB regulator enable net.
On ATmega328P Nano, `A7` is **analog-input-only** and cannot drive
`pinMode()` / `digitalWrite()`, so renaming the firmware constant from `A0` to
`A7` is not a valid fix.

Before ordering boards, changing regulator control, or debugging radio power-up
failures, choose an explicit hardware strategy:

1. Board revision: route regulator enable to a digital-capable GPIO (for example
   `A0`) and match firmware, or
2. Strap `/3V3_EN` to the required level if software control is not needed.

Confirm intended power sequencing before either option. Radio `begin()` failure
(`Radio not found`) is the firmware-visible symptom when 3.3 V is missing.

## Production checklist

1. If schematic/PCB changed, regenerate Gerbers and BoM from KiCad into
   `production/transmitter-or-receiver/` (do not ship stale 2024-08-17 exports).
   Update `Laser_Timer_PCB_V1.zip` alongside the unpacked files.
2. Confirm TX vs RX population/wiring for the shared PCB; the flashed sketch
   selects the role. Silkscreen is brand/rev only.
3. Confirm LCD backpack address `0x27` or update both sketches.
4. Resolve the `A0` / `A7`/`3V3_EN` mismatch above; do not use `A7` as a digital
   output on Nano.
5. Keep firmware pin constants synchronized with the nets in this document.
6. Remember `D1` is shared with UART TX — buzzer loading can affect uploads.
7. `A6`/`+BAT_ADC` has no v2 firmware reader; do not assume a battery gauge UI
   exists until one is added.
