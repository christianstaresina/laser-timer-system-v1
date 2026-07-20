# Laser Timer System

A portable two-gate laser timing system for sprint and speed training. The
transmitter unit sits at gate 1, sends a radio start event when its laser beam
is broken, and waits for the receiver unit at gate 2 to report that the run has
finished. The receiver displays elapsed time and can also display average speed
from the configured gate distance.

## Repository layout

| Path | Purpose |
| --- | --- |
| `firmware/transmitter/laser_timer_v2_tx/` | Arduino sketch for the gate 1 transmitter/start unit. |
| `firmware/receiver/laser_timer_v2_rx/` | Arduino sketch for the gate 2 receiver/finish unit, LCD menu, and EEPROM-backed settings. |
| `firmware/common/radio_protocol_v2.h` | Reference copy of the v2 nRF24 packet format and timing constants. |
| `hardware/transmitter-or-receiver/` | KiCad source for the shared transmitter/receiver PCB. |
| `production/transmitter-or-receiver/` | Generated BoM and Gerbers for the shared PCB. |
| `docs/firmware-v2.md` | Firmware setup, radio protocol, operating workflow, menu settings, and troubleshooting. |
| `docs/hardware-production.md` | Hardware/firmware pin map, production artifacts, and PCB constraints. |

## Firmware quick start

The active firmware is v2 and is organized as two Arduino IDE sketches. Open the
sketch folder itself in the Arduino IDE:

- Transmitter: `firmware/transmitter/laser_timer_v2_tx`
- Receiver: `firmware/receiver/laser_timer_v2_rx`

Required Arduino libraries:

- Built in: `SPI`, `Wire`, `EEPROM`
- Install separately: `RF24`, `LiquidCrystal_I2C`

Flash the receiver and transmitter separately to ATmega328/Arduino Nano based
boards. The sketches expect a 16x2 I2C LCD at address `0x27`, an nRF24L01+PA+LNA
radio on CE `D9` and CSN `D10`, a beam sensor on `D2`, a buzzer on `D1`, and a
rotary encoder on `D3`/`D4`/`D5`.

## Operating workflow

1. Power the receiver at gate 2 first so it can listen for the transmitter.
2. Power the transmitter at gate 1. Its setup loop sends `CMD_PING` packets
   until the receiver acknowledges the link.
3. On the receiver, long-press the encoder and select **Stopwatch** to arm the
   timer.
4. Break the gate 1 beam to start timing.
5. Break the gate 2 beam to stop timing. The receiver displays the result and
   sends `CMD_GATE2_CLOSED` back to the transmitter.

See `docs/firmware-v2.md` for the packet format, menu tree, saved settings, and
common pitfalls.

## Hardware and production

The KiCad design is a single shared PCB that can be assembled for either the
transmitter or receiver role. Manufacturing outputs are checked in under
`production/transmitter-or-receiver/`.

Before spinning hardware or changing firmware pin assignments, review
`docs/hardware-production.md`. It documents the current PCB pin map and calls
out a source-verified mismatch: the v2 firmware drives `A0` high for radio power
settling, while the checked-in KiCad PCB labels the 3.3 V regulator enable net
as `A7`/`3V3_EN`. Nano `A7` is analog-input-only, so this requires a hardware or
power-strapping decision rather than a firmware-only pin rename.
