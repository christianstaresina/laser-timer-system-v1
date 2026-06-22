# Laser Timer System

A portable two-gate laser timing system for speed training. The start gate
transmits a radio event when its laser beam is crossed, the finish gate starts
and stops the timer, and the finish gate LCD displays elapsed time plus optional
average speed.

## Repository layout

| Path | Purpose |
| --- | --- |
| `firmware/transmitter/laser_timer_v2_tx` | Active v2 Arduino sketch for gate 1 / start unit. |
| `firmware/receiver/laser_timer_v2_rx` | Active v2 Arduino sketch for gate 2 / finish unit with LCD menu and EEPROM-backed settings. |
| `firmware/common/radio_protocol_v2.h` | Shared copy of the v2 nRF24 packet contract. Keep it synchronized with each sketch-local `src/radio_protocol_v2.h`. |
| `hardware/transmitter-or-receiver` | KiCad source for the shared V1 transmitter/receiver PCB. |
| `production/transmitter-or-receiver` | V1 production outputs: Gerbers, drill files, and PCBA BOM. |

The v2 firmware replaced the older `laser_timer_pcb_v1_2024_*` sketches. Open
the v2 sketch folders directly in Arduino IDE so the adjacent `src/` files are
compiled with the `.ino`.

## Hardware and firmware assumptions

Both v2 sketches target an ATmega328-class Arduino Nano with:

- nRF24L01+PA+LNA radio using `RF24 radio(9, 10)`.
- 16x2 I2C LCD at address `0x27`.
- Laser gate sensor on digital pin 2. The firmware treats `HIGH` as "beam
  broken / athlete crossed".
- Buzzer on digital pin 1.
- Radio power enabled by driving `A0` high, followed by a 500 ms settle delay.

Receiver-only connections:

- Rotary encoder button on D3, CLK on D4, DAT on D5.
- SD chip-select on D8 is driven high during setup to keep it inactive.

Pin D1 is also the Arduino Nano serial TX pin. If upload or serial debugging is
unreliable, check that the buzzer circuit is not holding that pin.

## Developer setup

1. Install Arduino IDE or Arduino CLI with an Arduino AVR board package.
2. Install libraries that match the firmware includes:
   - `RF24`
   - `LiquidCrystal_I2C`
   - Built-in Arduino libraries: `SPI`, `Wire`, and `EEPROM`
3. Select the Arduino Nano / ATmega328 board and the correct processor variant
   for the board in use.
4. Upload `firmware/transmitter/laser_timer_v2_tx/laser_timer_v2_tx.ino` to the
   start gate.
5. Upload `firmware/receiver/laser_timer_v2_rx/laser_timer_v2_rx.ino` to the
   finish gate.

## Operating workflow

1. Power the receiver and transmitter. The transmitter blocks on the pairing
   screen until a radio ping is acknowledged. The receiver keeps running while
   showing pairing status until it receives a valid packet.
2. On the receiver, long-press the encoder button for at least 800 ms to open
   the menu.
3. Select `Stopwatch` from the main menu to arm timing.
4. Cross gate 1. The transmitter sends `CMD_GATE1_OPEN`, starts its local buzzer,
   and shows a "Timer counting" screen. The receiver starts timing when it
   receives that command.
5. Cross gate 2. The receiver stops the timer, optionally prints average speed,
   sends `CMD_GATE2_CLOSED` back to the transmitter, and shows `Finished` for
   1.5 seconds. The transmitter shows `Timer complete!` and returns to its ready
   screen.

The receiver reports `Align laser` when its gate sensor is already broken while
idle. Re-align the laser before starting another run.

## Receiver menu and persisted settings

The receiver menu is non-blocking, so radio polling, toast messages, and timer
state continue to be serviced while menus are visible.

- Main menu: `Stopwatch`, `Speed`, `Radio`, `Buzzer`, `Back`.
- `Speed` settings:
  - Toggle speed display on/off.
  - Presets: 40, 10, 5, 2, and 1 yard.
  - Custom distance: 1-99 yards.
  - Display units: yards/MPH or meters/KPH. Distances are stored internally in
    yards and converted for display.
- `Radio` settings:
  - Status shows `Link OK` while a valid packet has arrived within 2 seconds.
  - `Re-pair` shows the pairing screen until a valid radio packet is received,
    or exits on press/long-press.
- `Buzzer` settings:
  - Toggle align-warning beep.
  - Toggle finish beep.

Settings are stored in EEPROM address 0 with magic value `0x52`. Defaults are
speed enabled, 40 yards, custom distance 40 yards, both buzzers enabled, and
yard/MPH display.

## Radio protocol v2

The TX, RX, and `firmware/common` copies of `radio_protocol_v2.h` define the
public radio contract:

- Packet: two packed bytes, `{ magic, cmd }`.
- Magic byte: `0xA5`. Packets with any other magic value are ignored.
- Channel: 108.
- Data rate: 250 kbps.
- CRC: 16-bit.
- Auto-ack: enabled for acknowledged sends.
- Pipes:
  - `addresses[0] == "00001"`: receiver-to-transmitter return path.
  - `addresses[1] == "00002"`: transmitter-to-receiver primary path.

Commands:

| Command | Value | Direction | Meaning |
| --- | ---: | --- | --- |
| `CMD_GATE1_OPEN` | `1` | TX -> RX | Start gate beam broke; receiver starts or continues timing when armed. |
| `CMD_GATE1_CLOSED` | `2` | TX -> RX | Start gate beam restored. |
| `CMD_GATE2_CLOSED` | `3` | RX -> TX | Finish gate completed the run. |
| `CMD_PING` | `0xFF` | TX -> RX | Pairing and heartbeat packet. |

The transmitter sends three unacknowledged `CMD_GATE1_OPEN` packets followed by
one acknowledged packet when gate 1 first opens. While gate 1 remains broken and
the timer is running, it repeats `CMD_GATE1_OPEN` every 50 ms. When idle, it
sends heartbeat pings every 500 ms. The receiver considers the link lost if no
valid packet arrives for 2000 ms.

When changing the protocol, update all three header copies in the same commit.

## Troubleshooting

- `Radio not found`: `radio.begin()` failed. Check radio wiring, power, CSN/CE
  pins 10/9, and that A0-powered radio hardware is present.
- Transmitter stays on `Pairing...`: the receiver is not powered, is not
  listening on pipe `"00002"`, or packets are not being acknowledged.
- Receiver shows `No TX signal`: no valid packet has arrived within the 2 second
  link timeout. Check transmitter power and radio range.
- Receiver shows `Align laser`: the finish gate sensor pin is `HIGH`; align the
  beam or verify the sensor output polarity matches the firmware.
- Timing never starts: arm `Stopwatch` on the receiver before crossing gate 1.
- Speed is missing: enable speed in the receiver `Speed` menu and set the
  correct distance before running.
