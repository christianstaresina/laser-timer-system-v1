# Laser Timer System

A portable two-gate laser timer for sprint and speed training. The
transmitter unit sits at gate 1 and starts a run when its laser beam is
broken. The receiver unit sits at gate 2, times the run, stops on the finish
beam, and displays elapsed time plus optional average speed.

The hardware design is V1. The active Arduino firmware is V2 and uses an
nRF24L01+ radio link, 16x2 I2C LCDs, laser gate sensors, buzzers, and a rotary
encoder menu on the receiver.

## Repository layout

```text
firmware/
  transmitter/laser_timer_v2_tx/   Start-gate Arduino sketch
  receiver/laser_timer_v2_rx/      Finish-gate Arduino sketch and RX modules
  common/radio_protocol_v2.h       Reference copy of the RF packet protocol
hardware/transmitter-or-receiver/  KiCad schematic and PCB design
production/transmitter-or-receiver/
  bom/                             PCBA bill of materials
  gerbers/                         Fabrication outputs
```

## Firmware architecture

### Transmitter: `firmware/transmitter/laser_timer_v2_tx`

The transmitter is the start gate. On boot it:

1. Configures the gate sensor, buzzer, encoder pins, LCD, and nRF24 radio.
2. Drives `A0` high, waits `RADIO_POWER_SETTLE_MS` (500 ms), then initializes
   the radio.
3. Sends `CMD_PING` packets until the receiver acknowledges the link.
4. Shows the ready screen if the gate beam is aligned, or `Align laser` if the
   beam is already broken.

During a run, `Sense_Gate1()` detects the first gate crossing and sends a
`CMD_GATE1_OPEN` burst to the receiver. While the start beam remains broken and
the run is active, it repeats the gate-open packet every
`RADIO_GATE_REPEAT_MS` (50 ms). When the beam clears it sends
`CMD_GATE1_CLOSED`. After the receiver stops the timer, the transmitter listens
on the return pipe for `CMD_GATE2_CLOSED`, shows `Timer complete!`, and sounds
the finish buzzer.

The transmitter has encoder pins reserved, but the TX menu is not implemented;
a long press is currently ignored after release.

### Receiver: `firmware/receiver/laser_timer_v2_rx`

The receiver is the finish gate and user interface. It is split into modules:

- `functions_laser_timer_v2_rx.h` - radio polling, gate 2 sensing, timer state,
  speed display, and buzzer handling.
- `menu_rx.*` - non-blocking rotary encoder menu.
- `settings_rx.*` - EEPROM-backed receiver settings.
- `encoder_rx.*` - debounced rotary encoder events.
- `display_rx.*` - LCD line formatting and short toast messages.

On boot the receiver loads settings, initializes the menu and LCD, starts the
radio, opens the TX-to-RX reading pipe, and shows a non-blocking pairing screen
until the first valid packet arrives. When the main menu starts the stopwatch,
the receiver waits for `CMD_GATE1_OPEN`, records `millis()` as the run start,
updates the elapsed seconds display, and stops when gate 2 is crossed. It then
sends `CMD_GATE2_CLOSED` back to the transmitter, displays `Finished`, and
optionally shows average speed.

Idle receiver status:

- `Ready` means a transmitter packet was received within
  `RADIO_LINK_TIMEOUT_MS` (2 seconds).
- `No TX signal` means no transmitter packet has arrived within that timeout.
- `Align laser` means the finish beam is currently broken outside an active
  run.

## Receiver menu

Open the receiver menu with a rotary encoder long press
(`ENCODER_LONG_PRESS_MS`, 800 ms). Rotate to move, press to select, and long
press again to exit the current menu back to the idle display.

Main menu:

- `Stopwatch` - enables timing and waits for the transmitter start gate.
- `Speed` - controls speed calculation settings.
- `Radio` - shows link status or enters a re-pairing screen.
- `Buzzer` - toggles alignment and finish buzzer feedback.
- `Back` - returns to the idle display.

Speed settings:

- `Speed ON/OFF` toggles whether average speed is printed after a run.
- Presets: 40, 10, 5, 2, or 1 yard.
- `Custom` edits a custom distance from 1 to 99 yards.
- `Units` toggles yards/MPH vs meters/KPH display. Distances are stored in
  yards; meter labels are rounded for display.

Persisted receiver settings live at EEPROM address 0 with magic byte `0x52`:
speed display enabled, selected distance, custom distance, alignment buzzer,
finish buzzer, and unit display mode. Defaults are speed enabled, 40 yards,
both buzzers enabled, and yards/MPH.

## Radio protocol

The v2 protocol is a packed two-byte packet:

```cpp
struct RadioPacket {
  uint8_t magic; // RADIO_MAGIC, 0xA5
  uint8_t cmd;
};
```

Commands:

| Command | Value | Direction | Meaning |
| --- | ---: | --- | --- |
| `CMD_GATE1_OPEN` | `1` | TX -> RX | Start gate beam broken; start or reinforce the run start. |
| `CMD_GATE1_CLOSED` | `2` | TX -> RX | Start gate beam restored. |
| `CMD_GATE2_CLOSED` | `3` | RX -> TX | Finish gate crossed; transmitter may show completion. |
| `CMD_PING` | `0xFF` | TX -> RX | Pairing and heartbeat packet. |

Radio settings are defined in `radio_protocol_v2.h`: channel 108,
250 kbps data rate, 16-bit CRC, auto-ack enabled, fixed payload size, and
addresses `"00001"` for RX-to-TX and `"00002"` for TX-to-RX. The transmitter
sends low-risk heartbeats without requiring acknowledgements, but pairing,
start-gate burst completion, and finish notification use acknowledged writes.

The sketches currently include local copies of `radio_protocol_v2.h` under
each sketch's `src/` directory, plus the reference copy under `firmware/common`.
Keep all copies synchronized if protocol constants or packet fields change.

## Arduino setup

Open and upload each sketch folder separately in the Arduino IDE:

- Transmitter: `firmware/transmitter/laser_timer_v2_tx`
- Receiver: `firmware/receiver/laser_timer_v2_rx`

The code targets an ATmega328-style Arduino Nano setup and uses these Arduino
libraries:

- Built-in: `SPI`, `Wire`, `EEPROM`
- External: `RF24`, `LiquidCrystal_I2C`

Important firmware pin assignments:

| Function | Transmitter pin | Receiver pin |
| --- | ---: | ---: |
| Laser gate sensor | D2 | D2 |
| Buzzer | D1 | D1 |
| Encoder button | D3 | D3 |
| Encoder CLK | D4 | D4 |
| Encoder DAT | D5 | D5 |
| nRF24 CE | D9 | D9 |
| nRF24 CSN | D10 | D10 |
| SD card CS | - | D8 |
| Radio power enable | A0 | A0 |
| LCD I2C address | `0x27` | `0x27` |

Both sketches treat the laser gate as active high: `HIGH` means the beam is
broken and the athlete has crossed the gate.

## Operating workflow

1. Power the receiver and transmitter.
2. Wait for pairing. The transmitter blocks on `Pairing...` until the receiver
   acknowledges `CMD_PING`; the receiver shows pairing until any valid packet
   arrives.
3. Align both lasers. The receiver or transmitter displays `Align laser` when
   its gate beam is broken before a run.
4. On the receiver, long-press the encoder, choose `Stopwatch`, and press.
5. Cross gate 1 to start timing.
6. Cross gate 2 to stop timing. The receiver shows elapsed seconds and, if
   enabled, average speed. Both units provide buzzer feedback when enabled.

## Troubleshooting

- **Transmitter stays on `Pairing...`**: confirm the receiver is powered,
  flashed with the v2 RX sketch, and using the same `radio_protocol_v2.h`
  constants and addresses. Check nRF24 CE/CSN wiring and power; both sketches
  enable radio power on `A0` before calling `radio.begin()`.
- **Receiver shows `No TX signal`**: it has not received a valid transmitter
  packet within 2 seconds. Check the transmitter, radio wiring, antenna modules,
  and that the TX sketch is running.
- **LCD shows `Radio not found`**: `radio.begin()` failed. Inspect the nRF24
  module, CE/CSN pins, SPI wiring, and power supply.
- **Gate immediately shows `Align laser`**: the gate input is reading `HIGH`.
  Realign the laser/sensor path or check the sensor wiring.
- **Speed is missing after a run**: enable `Speed ON` in the receiver Speed
  menu and confirm the distance setting is correct before starting Stopwatch.
- **Upload or serial issues on Nano**: the buzzer is assigned to D1, which is
  also the hardware UART TX pin on Arduino Nano boards. Disconnect conflicting
  hardware if it interferes with flashing or serial debugging.

## Hardware and production files

The KiCad project under `hardware/transmitter-or-receiver` defines the shared
transmitter/receiver PCB. Fabrication files and the PCBA BOM are generated
under `production/transmitter-or-receiver`. Regenerate production outputs from
KiCad after schematic or PCB edits so the BOM, drill files, and Gerbers stay in
sync with the design source.
