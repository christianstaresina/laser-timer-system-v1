# Laser Timer System

A portable two-gate laser timer for speed training. The transmitter unit sits at
gate 1 and starts a run when its beam is broken. The receiver unit sits at gate 2,
stops the timer when its beam is broken, and displays the elapsed time plus an
optional average speed.

The active firmware is **v2**. It replaced the older `laser_timer_pcb_v1_2024_*`
sketches with a paired nRF24 workflow, a receiver-side rotary encoder menu,
EEPROM-backed settings, and clearer LCD status screens.

## Repository layout

| Path | Purpose |
| --- | --- |
| `firmware/transmitter/laser_timer_v2_tx` | Arduino sketch for the gate 1 / start unit. |
| `firmware/receiver/laser_timer_v2_rx` | Arduino sketch for the gate 2 / finish unit. |
| `firmware/common/radio_protocol_v2.h` | Shared reference copy of the v2 packet format and radio constants. |
| `hardware/transmitter-or-receiver` | KiCad project for the shared transmitter/receiver PCB. |
| `production/transmitter-or-receiver` | Fabrication outputs, including gerbers and BOM. |

Each Arduino sketch keeps its own `src/radio_protocol_v2.h` copy because the
Arduino IDE compiles sketch-local headers most reliably. When changing the radio
protocol, update the transmitter, receiver, and `firmware/common` copies together.

## Firmware setup

Open and upload each sketch folder directly in the Arduino IDE:

- Transmitter: `firmware/transmitter/laser_timer_v2_tx`
- Receiver: `firmware/receiver/laser_timer_v2_rx`

The firmware targets an ATmega328-style Arduino environment and uses these
libraries:

- `SPI`, `Wire`, and `EEPROM` from the Arduino core
- `RF24` for the nRF24L01+ radio
- `LiquidCrystal_I2C` for the 16x2 LCD at I2C address `0x27`

There is no automated build configuration in this repository. After firmware
changes, verify both sketches by compiling and uploading them from the Arduino
IDE with the matching board and port selected.

## Hardware assumptions from firmware

Both units use a 16x2 I2C LCD, nRF24L01+PA+LNA radio, buzzer, rotary encoder
button, and a laser gate sensor. The transmitter and receiver sketches currently
define these pins:

| Signal | Transmitter | Receiver | Notes |
| --- | ---: | ---: | --- |
| Gate sensor | `D2` | `D2` | `HIGH` means the beam is broken. |
| Encoder button | `D3` | `D3` | Uses `INPUT_PULLUP`; long press opens menu on RX. |
| Encoder CLK | `D4` | `D4` | Receiver uses rotation for menu navigation. |
| Encoder DAT | `D5` | `D5` | Receiver uses rotation for menu navigation. |
| Buzzer | `D1` | `D1` | Driven with `tone()`/`noTone()`. |
| nRF24 CE / CSN | `D9` / `D10` | `D9` / `D10` | Both sketches instantiate `RF24(9, 10)`. |
| SD CS placeholder | n/a | `D8` | Receiver holds this chip-select high. |
| Radio power enable | `A0` | `A0` | Driven `HIGH` before radio initialization. |

The sketches pause for `RADIO_POWER_SETTLE_MS` after enabling `A0`, then initialize
SPI and the radio. If `radio.begin()` fails, the LCD displays `Radio not found`
and the sketch stays in a delay loop.

## Runtime workflow

1. Power on the receiver and transmitter.
2. Both LCDs show the pairing screen while the transmitter sends `CMD_PING`
   packets to the receiver.
3. When the receiver hears a valid packet and the transmitter gets an ACK, both
   units show `Link OK!` / `Connected`.
4. On the receiver, long-press the encoder to open the main menu, then select
   `Stopwatch` to arm timing.
5. Break gate 1. The transmitter sends a short `CMD_GATE1_OPEN` burst and shows
   `Timer counting`; the receiver starts timing.
6. Break gate 2. The receiver stops timing, optionally prints average speed,
   sends `CMD_GATE2_CLOSED` back to the transmitter, and shows `Finished`.
7. The transmitter shows `Timer complete!`, beeps, then returns to the ready
   screen.

If the receiver has not heard a radio packet for `RADIO_LINK_TIMEOUT_MS`
(currently 2000 ms), its idle display changes to `No TX signal`. The transmitter
sends heartbeat pings every `RADIO_HEARTBEAT_MS` while idle.

## Receiver menu and persisted settings

Open the receiver menu with a long encoder press. Rotate to move through items,
short-press to select, and long-press from an active menu to return to idle. Menu
indices clamp at the first and last item; they do not wrap.

Main menu:

- `Stopwatch` arms the receiver so gate 1 packets can start a run.
- `Speed` opens speed display settings.
- `Radio` shows link status or starts re-pairing.
- `Buzzer` toggles alignment and finish beeps.
- `Back` returns to idle.

Speed settings:

- Toggle speed display on or off.
- Select preset distances of 40, 10, 5, 2, or 1 yard.
- Select `Custom` and rotate to set 1-99 yards.
- Toggle display units between yards/MPH and meters/KPH.

Buzzer settings:

- `Align` controls the beep when gate 2 is blocked while idle.
- `Finish` controls the beep when a run completes.

The receiver persists these values at EEPROM address `0` using a settings block
guarded by `SETTINGS_EEPROM_MAGIC` (`0x52`):

- speed display enabled/disabled
- active distance in yards
- custom distance in yards
- alignment buzzer enabled/disabled
- finish buzzer enabled/disabled
- yard vs meter display mode

If the magic byte is missing, firmware keeps the compiled defaults: speed enabled,
40 yards, both buzzers enabled, and yard/MPH display.

## Radio protocol

The v2 protocol uses two-byte packets:

```cpp
struct RadioPacket {
  uint8_t magic; // RADIO_MAGIC, currently 0xA5
  uint8_t cmd;
};
```

Pipe layout:

- `addresses[0]` (`"00001"`): receiver to transmitter return channel.
- `addresses[1]` (`"00002"`): transmitter to receiver primary channel.

Commands:

| Command | Value | Direction | Meaning |
| --- | ---: | --- | --- |
| `CMD_GATE1_OPEN` | `1` | TX -> RX | Start gate beam broke; receiver may start timing. |
| `CMD_GATE1_CLOSED` | `2` | TX -> RX | Start gate beam is clear again. |
| `CMD_GATE2_CLOSED` | `3` | RX -> TX | Finish gate completed the run. |
| `CMD_PING` | `0xFF` | TX -> RX | Pairing and idle heartbeat. |

Radio initialization uses channel `108`, 250 Kbps data rate, auto ACK, 16-bit CRC,
fixed payload size equal to `sizeof(RadioPacket)`, and high PA level after startup.
Gate 1 start packets are sent as a short no-ACK burst followed by an ACK-required
packet to reduce missed starts.

## Troubleshooting

- **LCD says `Radio not found`**: check nRF24 wiring, CE/CSN pins (`D9`/`D10`),
  power on `A0`, SPI wiring, and radio power stability.
- **Receiver says `No TX signal`**: power-cycle or re-pair both units, confirm the
  protocol headers match in all three locations, and verify both radios use the
  same channel and addresses.
- **Gate appears crossed immediately**: the firmware treats `HIGH` on `D2` as a
  broken beam. Check sensor polarity and alignment.
- **Gate 1 crossing does not start timing**: the receiver must be armed through
  `Main Menu -> Stopwatch`; gate 1 packets are ignored while timing is disabled.
- **Speed is missing from the result**: enable speed display under
  `Speed -> Speed ON` and confirm the selected distance/units.
- **Menu does not open on transmitter**: transmitter long-press handling is
  reserved for a future menu; the implemented settings menu is receiver-side.
