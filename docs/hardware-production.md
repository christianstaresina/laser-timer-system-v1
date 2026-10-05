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

## Power tree

Verified against Nano / regulator pad nets in `Laser_Timer_PCB.kicad_pcb`:

```
J1 pin 1  /+BAT     battery / pre-switch (also J2 pin 1)
J1 pin 2  +9V       switched rail
              ├─ U1 AZ1117 VIN  → board +5V  (J3 / J4 / J5 VCC, U2 VIN)
              └─ Nano VIN
J1 pin 3  GND

U2 MIC5504 VIN = board +5V
U2 EN          = Nano A7  (/3V3_EN)   — firmware does not drive this pin
U2 OUT         = board +3V3           — RF1 VCC only
```

Two **5 V** nets and two **3.3 V** nets exist and are **not** tied together:

| Net | Source | Used by |
| --- | --- | --- |
| `+5V` | U1 output | LCD / encoder / sensor (J3–J5), U2 VIN |
| `/5V` | Nano `+5V` pin | J8 pin 2 only |
| `+3V3` | U2 output | RF1 VCC |
| `/3V3` | Nano `3V3` pin | J8 pin 1 only |

Do not jumper J8 `/5V` onto board `+5V` while USB is plugged in (backfeed). Do
not power RF1 from the Nano 3.3 V pin — firmware `"Radio not found"` is the
usual symptom when board `+3V3` is missing. RF1 `IRQ` is unconnected. U2 pin 4
(BYP) is the unconnected net `unconnected-(U2-BP-Pad4)`.

Decoupling on those rails (BoM values, PCB nets):

| Ref | Value | Net |
| --- | --- | --- |
| `C1` | 10 µF | `+9V` to GND |
| `C2` | 22 µF | board `+5V` to GND |
| `C3` | 1 µF | board `+5V` to GND |
| `C4` | 1 µF | board `+3V3` to GND |
| `C5` | 10 µF | board `+3V3` to GND |

`BZ1` (footprint TDK PS1240P02BT) is wired from `/BUZZ` (Nano D1) straight to
GND. There is no transistor and no series resistor. Firmware `tone()` at
2000 Hz drives the pin directly, which is also why USB uploads fight the buzzer.

## Connectors (BoM)

| Ref | BoM value | Firmware use |
| --- | --- | --- |
| `J1` | Pwr Btn and Battery | Power path; not driven by v2 sketches. |
| `J3` | LCD | I2C 16×2 backpack; firmware address `0x27`. |
| `J4` | Encoder | RX menu; TX reads the button only (menu stub). |
| `J5` | Sensor | Gate beam, D2, active `HIGH` when broken. |
| `J2`, `J6`–`J10` | Expansion / headers | See table below. v2 holds `/D8` high; drives `/A0` high. |

User-facing housings are JST EH 2.50 mm vertical (`B*B-EH-A`). Pad 1 is the
square / round-rect pin on the PCB.

| Connector | Pad | Net | Typical use |
|-----------|----:|-----|-------------|
| **J1** Pwr Btn and Battery | 1 | `/+BAT` | Battery / pre-switch rail |
| | 2 | `+9V` | Switched 9 V rail into U1 and Nano VIN |
| | 3 | `GND` | Ground |
| **J3** LCD | 1 | `+5V` | Backpack VCC |
| | 2 | `/SCL` | I2C SCL (Nano A5) |
| | 3 | `/SDA` | I2C SDA (Nano A4) |
| | 4 | `GND` | Ground |
| **J4** Encoder | 1 | `+5V` | Encoder VCC |
| | 2 | `/SW` | Button (Nano D3, `INPUT_PULLUP`) |
| | 3 | `/CLK` | CLK (Nano D4, no MCU pull-up) |
| | 4 | `/DAT` | DAT (Nano D5, no MCU pull-up) |
| | 5 | `GND` | Ground |
| **J5** Sensor | 1 | `+5V` | Sensor VCC |
| | 2 | `/S_OUT` | Beam output (Nano D2) |
| | 3 | `GND` | Ground |

`J2` is a 2-pin EH header on `/+BAT` and `GND` (not a named user cable).

### Expansion headers (2.54 mm pin headers)

v2 firmware does not implement these as user features. Pin order is PCB pad
number (pad 1 square).

| Header | Pads | Nets |
| --- | ---: | --- |
| **J2** | 1–2 | `/+BAT`, `GND` |
| **J6** | 1–5 | `/AREF`, `/A0`, `/A1`, `/A2`, `/A3` |
| **J7** | 1–2 | `/RESET`, `/RESET` (both pads the reset net) |
| **J8** | 1–2 | `/3V3` (Nano 3.3 V pin), `/5V` (Nano 5 V pin) |
| **J9** | 1 | `/D0/RX` (Nano UART RX; TX/buzzer is D1, not on this header) |
| **J10** | 1–3 | `/D6`, `/D7`, `/D8` |

### BoM highlights

From `Laser_Timer_PCBA_BoM.csv` (DNP column empty — no TX/RX population notes):

| Ref | Part |
| --- | --- |
| `A1` | Arduino Nano v3.x |
| `RF1` | nRF24L01+PA+LNA |
| `U1` | AZ1117 5.0 V LDO |
| `U2` | MIC5504 3.3 V LDO (schematic symbol `MIC5219-3.3YM5`; value MIC5504) |
| `BZ1` | Buzzer |
| `R1`, `R2` | 510k battery divider on `/+BAT_ADC` (unused by v2) |
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
| nRF24 MOSI | `D11` (hardware SPI) | `/MOSI`, RF1 |
| nRF24 MISO | `D12` (hardware SPI) | `/MISO`, RF1 |
| nRF24 SCK | `D13` (hardware SPI) | `/SCK`, RF1 |
| nRF24 IRQ | — | RF1 pad unconnected |
| LCD SDA / SCL | `A4` / `A5` | `/SDA` / `/SCL`, J3 LCD |
| LCD address | `0x27` | Firmware; confirm backpack matches |
| RX SD CS placeholder | `D8` | `/D8`, J10 pin 3 |
| Firmware radio power enable | `A0` driven high | Nano pad `A0` → `/A0` (J6 pin 2), **not** regulator enable |
| PCB 3.3 V regulator enable | — | Nano pad `A7` → `/3V3_EN` → U2 |
| Battery ADC (unused by v2) | — | Nano pad `A6` → `/+BAT_ADC` (R1 510k from `+9V`, R2 510k to GND) |
| Nano VIN | — | `+9V` (switched battery rail) |
| Nano `+5V` pin | — | `/5V` (J8 pin 2 only; not board `+5V`) |

Transmitter and receiver share this pin map. RX-only firmware implements the
menu, EEPROM settings, finish timer, and the `D8` placeholder. Encoder CLK/DAT
have no MCU pull-ups; the encoder assembly must provide them if needed.

RF1 VCC is net `+3V3` (U2), not the Nano 3.3 V pin. A missing 3.3 V rail is
the usual hardware cause of firmware `"Radio not found"`.

## Test points

1.0 mm SMD pads on the front copper. Use them for bring-up before blaming the
sketches. Rails here are the board nets, not the Nano `5V` / `3V3` pins.

| Ref | Net |
| --- | --- |
| `TP1` | `+9V` |
| `TP2` | board `+5V` |
| `TP3` | board `+3V3` (RF1 VCC) |
| `TP4` | `GND` |
| `TP5` | `/CE` (D9) |
| `TP6` | `/CSN` (D10) |
| `TP7` | `/MOSI` (D11) |
| `TP8` | `/MISO` (D12) |
| `TP9` | `/SCK` (D13) |

`Radio not found` with TP3 near 0 V means U2 is off or unpowered (see the
`A0` / `A7` mismatch below). TP3 at 3.3 V and a dead radio points at CE/CSN/SPI
(TP5–TP9) or the module.

## Power-enable mismatch

Both v2 sketches run this before radio init:

```cpp
pinMode(A0, OUTPUT);
digitalWrite(A0, HIGH);
delay(RADIO_POWER_SETTLE_MS); // 500 ms
```

Checked-in KiCad mapping:

- Nano `A0` → net `/A0` (J6 pin 2 / expansion).
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
3. Confirm LCD backpack address `0x27` or update both sketches. J3 pin order is
   `+5V / SCL / SDA / GND` — not the common `GND / VCC / SDA / SCL` module order.
4. Resolve the `A0` / `A7`/`3V3_EN` mismatch above; do not use `A7` as a digital
   output on Nano.
5. Keep firmware pin constants synchronized with the nets in this document.
6. Remember `D1` is shared with UART TX. `BZ1` is driven directly from that pin
   to GND (no transistor), so buzzer loading can affect uploads.
7. `A6`/`+BAT_ADC` is a 1:1 divider (R1/R2 510k from the `+9V` rail to GND).
   v2 firmware never reads it; do not assume a battery gauge UI exists until
   one is added.
8. Keep board `+5V`/`+3V3` distinct from Nano `/5V`/`/3V3` unless a revision
   explicitly ties them.
