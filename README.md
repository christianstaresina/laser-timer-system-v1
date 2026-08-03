# Laser Timer System

A portable two-gate laser timing system for sprint and speed training. Gate 1
(transmitter) sends a radio start event when its laser beam breaks. Gate 2
(receiver) starts and stops the timer, shows elapsed time on a 16x2 LCD, and can
display average speed from a configured distance.

## Repository layout

| Path | Purpose |
| --- | --- |
| `firmware/transmitter/laser_timer_v2_tx/` | Active v2 Arduino sketch for the start / gate 1 unit. |
| `firmware/receiver/laser_timer_v2_rx/` | Active v2 Arduino sketch for the finish / gate 2 unit (LCD menu + EEPROM settings). |
| `firmware/common/radio_protocol_v2.h` | Reference copy of the v2 nRF24 packet contract. Keep synchronized with each sketch-local `src/radio_protocol_v2.h`. |
| `hardware/transmitter-or-receiver/` | KiCad source for the shared V1 transmitter/receiver PCB. |
| `production/transmitter-or-receiver/` | Checked-in Gerbers, drill files, and PCBA BoM. |
| `docs/firmware-v2.md` | Firmware architecture, protocol, workflows, settings, and pitfalls. |
| `docs/hardware-production.md` | Pin map, production artifacts, and PCB/firmware power constraints. |

Open each v2 sketch **folder** in the Arduino IDE so the adjacent `src/` files
compile with the `.ino`. Older `laser_timer_pcb_v1_2024_*` sketches are not the
active path.

## Quick start

1. Install Arduino IDE (or CLI) with an AVR / Nano board package.
2. Install libraries: `RF24`, `LiquidCrystal_I2C` (plus built-in `SPI`, `Wire`,
   `EEPROM`).
3. Board: Arduino Nano / ATmega328 (match the processor variant on the board).
4. Flash `firmware/transmitter/laser_timer_v2_tx/laser_timer_v2_tx.ino` to gate 1.
5. Flash `firmware/receiver/laser_timer_v2_rx/laser_timer_v2_rx.ino` to gate 2.

Shared hardware assumptions: nRF24 on CE `D9` / CSN `D10`, beam sensor on `D2`
(active `HIGH` when broken), buzzer on `D1`, LCD I2C `0x27`. Receiver also uses
encoder `D3`/`D4`/`D5`. Both sketches drive `A0` high before radio init; see
`docs/hardware-production.md` for the PCB `A0` vs `A7`/`3V3_EN` mismatch.

## Operating workflow

1. Power the **receiver** first so it is listening.
2. Power the transmitter. Setup blocks on pairing until a `CMD_PING` send reports
   success, then listens for finish packets.
3. On the receiver, long-press the encoder (≥ 800 ms) and select **Stopwatch** to
   arm timing.
4. Break gate 1 → transmitter bursts `CMD_GATE1_OPEN`; receiver starts the timer
   when armed.
5. Break gate 2 → receiver stops, optionally prints speed, sends
   `CMD_GATE2_CLOSED`, and shows **Finished**; transmitter shows **Timer
   complete!** and returns to idle.

Details, menu tree, protocol constants, and known runtime constraints:
[`docs/firmware-v2.md`](docs/firmware-v2.md).

## Hardware and production

One shared PCB is assembled for either role; manufacturing outputs live under
`production/transmitter-or-receiver/`. Before board spins or pin changes, read
[`docs/hardware-production.md`](docs/hardware-production.md).
