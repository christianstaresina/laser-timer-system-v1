# Laser Timer System

A portable two-gate laser timing system for sprint and speed training. Gate 1
(transmitter) sends a radio start event when its laser beam breaks. Gate 2
(receiver) starts and stops the timer, shows elapsed time on a 16×2 LCD, and can
display average speed from a configured distance.

v1 hardware is a **single shared PCB**. Role is selected by which v2 sketch is
flashed. There is no in-tree v1 sketch; the active firmware is v2 only.

## Repository layout

| Path | Purpose |
| --- | --- |
| `firmware/transmitter/laser_timer_v2_tx/` | Gate 1 / start-unit Arduino sketch. |
| `firmware/receiver/laser_timer_v2_rx/` | Gate 2 / finish-unit sketch (LCD menu + EEPROM). |
| `firmware/common/radio_protocol_v2.h` | Reference copy of the nRF24 packet contract. Keep in sync with each sketch-local `src/radio_protocol_v2.h`. |
| `hardware/transmitter-or-receiver/` | KiCad 8 source for the shared V1 PCB. |
| `production/transmitter-or-receiver/` | Gerbers, drills, zip, and PCBA BoM. |
| `docs/firmware-v2.md` | Firmware architecture, protocol, workflows, settings, and pitfalls. |
| `docs/hardware-production.md` | Pin map, JST harness pinouts, power tree, expansion headers, and fab artifacts. |
| `docs/Laser_Timer_Schematic.pdf` | Exported schematic. |
| `docs/Laser_Timer_PCBA.jpg` | Assembled board photo. |

Open each v2 sketch **folder** in the Arduino IDE so the adjacent `src/` files
compile with the `.ino`. Sketches include their local `src/radio_protocol_v2.h`;
`firmware/common/` is not on the compile path.

## Quick start

1. Install Arduino IDE (or CLI) with an AVR / Nano board package.
2. Install libraries: `RF24` (TMRh20), `LiquidCrystal_I2C` (plus built-in `SPI`,
   `Wire`, `EEPROM` on the receiver).
3. Board: Arduino Nano / ATmega328P (match the processor variant on the module).
4. Flash `firmware/transmitter/laser_timer_v2_tx/laser_timer_v2_tx.ino` to gate 1.
5. Flash `firmware/receiver/laser_timer_v2_rx/laser_timer_v2_rx.ino` to gate 2.

The buzzer shares Nano UART TX (`D1`). Disconnect the buzzer or expect flaky USB
uploads. Shared hardware assumptions: nRF24 CE `D9` / CSN `D10` / SPI `D11`–`D13`,
beam sensor on `D2` (active `HIGH` when broken), LCD I2C `0x27`. Receiver encoder:
button `D3`, CLK `D4`, DAT `D5`. Both sketches drive `A0` high before radio init;
the PCB maps regulator enable to `A7`. Accessories run on board `+5V` (U1); the
radio runs on board `+3V3` (U2). Those rails are **not** the Nano’s `5V` / `3V3`
pins — see [`docs/hardware-production.md`](docs/hardware-production.md).

## Operating workflow

1. Power the **receiver** first so it is listening (` Pairing...` /
   `Waiting for link`).
2. Power the transmitter. `setup()` blocks in `waitForPairing()` until
   `sendPacket(CMD_PING)` returns true, then listens for finish packets.
3. Confirm the receiver leaves pairing (it accepts **any** valid magic packet,
   not only pings). Idle **bottom** row should read `Ready`. The receiver paints
   `Link OK!` and then replaces it in the same `loop()`; it does not hold that
   screen for 1.5 s. The transmitter does, via `delay` inside `waitForPairing()`.
   The RX menu can be opened before the first packet; pairing still completes
   in the background. A finish within 1.5 s of that first packet can have its
   frozen time cleared when the receiver’s pairing timer expires.
4. On the receiver, long-press the encoder (≥ 800 ms) and select **Stopwatch**
   to arm. The handler paints `Starting...`. The next `loop()` replaces it
   with the live time if a count is running, otherwise with the idle screen
   (pairing text if no packet has arrived, else `Ready`, `Align laser`, or
   `No TX signal`). The 300 ms toast flag does not keep that text up. Arming
   is **RAM-only** — a receiver power cycle returns to disarmed even though
   Speed / distance / buzzer persist in EEPROM. Speed print-on-finish defaults
   **ON** (40 yd).
5. Break gate 1 → TX bursts `CMD_GATE1_OPEN` and shows `Timer counting` /
   `Waiting gate 2`. Armed RX starts the seconds display on the **top** row.
6. Break gate 2 (a **new** beam-break while counting) → RX freezes time,
   optionally prints speed, sends `CMD_GATE2_CLOSED`, shows `Finished`. TX shows
   `Timer complete!` then returns to idle art. RX stays **armed** for the next
   start packet until it is power-cycled.

A second gate-1 break while RX is already counting does **not** restart the
clock. If the finish beam is already broken when the start packet arrives, the
clock starts but will not stop until the beam restores and breaks again. If the
**start** beam is still broken when the finish packet is sent, the transmitter
is still repeating `GATE1_OPEN`, and the receiver can start a new count
immediately because it stays armed.

There is no serial console; status is LCD text only. Gate inputs float (no
pull-up, no debounce). `Align laser` can appear while the receiver is disarmed;
the align beep runs only after Stopwatch is armed. Menu confirmations are
redrawn over in the select handler. `Starting...` is replaced on the next
loop as well (live time if counting, otherwise idle). The 300 ms flag still
ignores in-menu encoder input, and when it expires it blanks the top row
during a count or a `Finished` display. Holding the button after a menu press
stalls the receiver loop until release. A long-press leaves Main, Speed,
Radio, and Buzzer; on the custom distance editor it does nothing (a short
press is the only exit, and it saves). Protocol, menu tree, and
source-verified constraints:
[`docs/firmware-v2.md`](docs/firmware-v2.md).

## Hardware and production

One shared PCB is assembled for either role. User-facing JST pinouts (power,
LCD, encoder, sensor), expansion headers, and manufacturing outputs live under
[`docs/hardware-production.md`](docs/hardware-production.md) and
`production/transmitter-or-receiver/`. Read that hardware note before board
spins, harness builds, or pin changes.
