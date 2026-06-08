# Laser Timer System V1

A portable laser timer system consisting of a start gate and an end gate. When
the athlete crosses the start gate, the timer begins counting. When the athlete
crosses the end gate, the receiver stops the timer and shows the elapsed time on
its built-in LCD.

One common use is sprint timing: place the gates a measured distance apart,
start a run by crossing gate 1, then read the finish time and optional average
speed at gate 2.

## Repository layout

- `firmware/transmitter/laser_timer_v2_tx/` - Arduino sketch for the gate 1
  transmitter unit.
- `firmware/receiver/laser_timer_v2_rx/` - Arduino sketch for the gate 2
  receiver unit with LCD menu, stopwatch, speed settings, radio status, and
  buzzer settings.
- `firmware/common/radio_protocol_v2.h` - reference copy of the shared nRF24
  packet format. Each sketch also carries a local copy under `src/`; keep these
  protocol headers synchronized when changing radio commands or timing.
- `hardware/transmitter-or-receiver/` - KiCad source for the shared PCB.
- `production/transmitter-or-receiver/` - generated fabrication outputs,
  including Gerbers and BOM.

## Firmware v2 architecture

Firmware v2 uses two ATmega328-based units with nRF24L01+PA+LNA radios and
16x2 I2C LCDs:

1. The transmitter watches gate 1. When the gate 1 beam is broken, it sends a
   `CMD_GATE1_OPEN` burst to the receiver, shows "Timer counting", and waits for
   a finish message.
2. The receiver watches gate 2. When stopwatch mode is enabled and a gate 1
   open packet arrives, it starts timing. When the gate 2 beam is broken, it
   stops timing, displays the result, optionally displays speed, and sends
   `CMD_GATE2_CLOSED` back to the transmitter.
3. Both units use periodic or repeated packets for resilience: the transmitter
   sends heartbeat pings while idle and repeats gate-open packets while a run is
   active.

The shared radio protocol is defined by `radio_protocol_v2.h`:

- Packet shape: two packed bytes, `magic` (`0xA5`) and `cmd`.
- Commands: `CMD_GATE1_OPEN`, `CMD_GATE1_CLOSED`, `CMD_GATE2_CLOSED`, and
  `CMD_PING`.
- Radio settings: channel `108`, 250 Kbps data rate, 16-bit CRC, auto-ack
  enabled, and high PA level after startup settling.
- Pipe layout: address `"00002"` is transmitter-to-receiver, and `"00001"` is
  receiver-to-transmitter.

## Arduino setup

Open each sketch folder directly in the Arduino IDE:

- Transmitter: `firmware/transmitter/laser_timer_v2_tx`
- Receiver: `firmware/receiver/laser_timer_v2_rx`

Install the Arduino libraries used by the sketches:

- `RF24`
- `LiquidCrystal_I2C`
- Built-in Arduino libraries: `SPI`, `Wire`, and `EEPROM` (receiver settings)

The sketches are written for ATmega328-style boards. Source-verified pin usage:

| Function | Pin |
| --- | --- |
| Gate sensor input | `2` |
| Buzzer output | `1` |
| Encoder button | `3` |
| Encoder CLK | `4` |
| Encoder DAT | `5` |
| nRF24 CE | `9` |
| nRF24 CSN | `10` |
| SD card CS placeholder on receiver | `8` |
| Radio power enable | `A0` set `HIGH` at startup |
| LCD | I2C address `0x27`, 16 columns x 2 rows |

Both gate sensors are treated as active-high: `HIGH` means the laser beam is
broken.

## Operating workflow

1. Power the receiver and transmitter. Both show a pairing screen during radio
   startup. The transmitter blocks until it can send an acknowledged ping; the
   receiver shows "Link OK!" after the first valid packet.
2. Align both lasers. If a receiver beam is already broken while idle, the LCD
   shows "Align laser". The transmitter similarly shows "Gate 1 crossed" /
   "Align laser" if gate 1 starts open.
3. On the receiver, long-press the encoder button to open the menu, choose
   `Stopwatch`, then press the encoder. This enables the receiver to start a
   timed run from gate 1 packets.
4. Cross gate 1. The transmitter sends the start burst, beeps, and shows
   "Timer counting" / "Waiting gate 2". The receiver starts the stopwatch.
5. Cross gate 2. The receiver stops the timer, displays the finish time,
   optionally appends speed, sends the finish packet, and beeps if finish buzzer
   is enabled. The transmitter shows "Timer complete!" briefly before returning
   to its ready screen.

## Receiver menu and persisted settings

The receiver menu is controlled by the rotary encoder:

- Long press from the idle screen: open the main menu.
- Rotate: move through menu items.
- Short press: select the current item.
- Long press inside a menu: exit back to idle.

Main menu items:

- `Stopwatch` - enables timing. Gate 1 packets are ignored until this is
  selected.
- `Speed` - toggles speed display, selects preset distances (`40`, `10`, `5`,
  `2`, or `1` yard), edits a custom distance from `1` to `99` yards, and toggles
  display units between yards/MPH and meters/KPH. Distances are stored
  internally in yards; meter displays are converted from yards.
- `Radio` - shows link status or enters a re-pairing screen.
- `Buzzer` - toggles alignment and finish beeps independently.
- `Back` - returns to the idle display.

Receiver settings are stored in EEPROM at address `0` with magic byte `0x52`.
Defaults are speed enabled, 40-yard distance, 40-yard custom distance, align
buzzer enabled, finish buzzer enabled, and yard/MPH display units.

## Troubleshooting

- `Radio not found`: `radio.begin()` failed. Check nRF24 wiring, CE/CSN pins,
  SPI wiring, radio power, and the A0-controlled radio power rail.
- Pairing never completes: make sure both sketches use matching
  `radio_protocol_v2.h` values for channel, addresses, packet magic, and command
  IDs. The transmitter stays on the pairing screen until it receives an
  acknowledgement from the receiver.
- Receiver says `No TX signal`: the receiver has not seen a valid transmitter
  packet within the link timeout. Check transmitter power, radio range, antenna
  orientation, and matching protocol headers.
- Gate crossing does not start a run: enable `Stopwatch` from the receiver menu
  first. The receiver intentionally ignores gate 1 start packets while stopwatch
  mode is disabled.
- LCD says `Align laser`: the corresponding gate input is `HIGH`, which the
  firmware interprets as a broken beam. Re-align the laser/sensor pair or check
  sensor polarity.
- Speed looks wrong: verify the selected distance and units in the receiver
  `Speed` menu. KPH values are computed by converting the stored yard distance
  to meters.
