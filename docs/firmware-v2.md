# Firmware v2

This document covers the active Arduino firmware under `firmware/`. It is
source-verified against the transmitter and receiver sketches in this repo.

## Sketches and dependencies

Open each sketch directory directly in the Arduino IDE:

| Unit | Sketch directory | Role |
| --- | --- | --- |
| Transmitter | `firmware/transmitter/laser_timer_v2_tx/` | Gate 1 start unit. Sends start events and waits for finish confirmation. |
| Receiver | `firmware/receiver/laser_timer_v2_rx/` | Gate 2 finish unit. Runs the timer, LCD menu, saved settings, and speed display. |

Libraries used by the sketches:

- Built in with Arduino AVR: `SPI`, `Wire`, `EEPROM`
- Install separately: `RF24`, `LiquidCrystal_I2C`

There is no PlatformIO or CLI build config in this repository. Use the Arduino
IDE board/library manager or add one before automating builds.

## Shared hardware assumptions

Both sketches assume:

| Signal | Pin or value | Source |
| --- | --- | --- |
| Beam sensor | `D2`, active `HIGH` when the beam is broken | `GATE_ACTIVATED` and `gate*_pin` |
| Buzzer | `D1` | `buzzer_pin` |
| Encoder button / CLK / DAT | `D3` / `D4` / `D5` | RX encoder module and TX reserved menu input |
| nRF24 CE / CSN | `D9` / `D10` | `RF24 radio(9, 10)` |
| LCD | I2C address `0x27`, 16 columns x 2 rows | sketch globals |

The receiver also declares `sd_cs_pin = 8` and drives it high during setup, but
there is no SD library usage or data logging flow in the current firmware.

## Radio protocol

The packet definition lives in three identical files today:

- `firmware/common/radio_protocol_v2.h`
- `firmware/transmitter/laser_timer_v2_tx/src/radio_protocol_v2.h`
- `firmware/receiver/laser_timer_v2_rx/src/radio_protocol_v2.h`

The transmitter and receiver compile the `src/` copies through local includes;
the `firmware/common/` header is a reference copy only unless the include paths
are changed. Keep all copies synchronized when changing the wire protocol.

### Packet format

```cpp
struct __attribute__((packed)) RadioPacket {
  uint8_t magic; // RADIO_MAGIC, 0xA5
  uint8_t cmd;
};
```

Commands:

| Command | Value | Direction | Meaning |
| --- | ---: | --- | --- |
| `CMD_GATE1_OPEN` | `1` | TX -> RX | Gate 1 beam broke; start or continue the receiver timer. |
| `CMD_GATE1_CLOSED` | `2` | TX -> RX | Gate 1 beam restored. |
| `CMD_GATE2_CLOSED` | `3` | RX -> TX | Gate 2 beam broke and the run finished. |
| `CMD_PING` | `0xFF` | TX -> RX | Pairing/heartbeat packet. |

Pipe addresses:

| Index | Address | Used for |
| --- | --- | --- |
| `0` | `"00001"` | RX -> TX return channel. |
| `1` | `"00002"` | TX -> RX primary channel. |

Radio settings:

- Channel `108`
- Data rate `RF24_250KBPS`
- CRC-16, auto-ack enabled, payload size `sizeof(RadioPacket)`
- Retries `5, 15`
- PA level starts at `RF24_PA_MIN` during init and is raised to `RF24_PA_HIGH`
- Gate-open burst: three calls marked `requireAck = false`, followed by one
  call marked `requireAck = true`

The `requireAck` helper argument does not currently provide per-packet ACK
control. It is passed directly to RF24's `write(..., multicast)` argument,
where `true` means NOACK and `false` means ACK, and `initRadio()` does not call
`enableDynamicAck()`. With the checked-in configuration, global auto-ack
remains enabled and the multicast argument has no effect. Do not infer
acknowledgement behavior from the helper argument names; fix and hardware-test
this wrapper before depending on mixed ACK/NOACK delivery.

Timing constants:

| Constant | Value | Purpose |
| --- | ---: | --- |
| `RADIO_POWER_SETTLE_MS` | 500 ms | Delay after firmware drives the radio power-enable pin high. |
| `RADIO_PAIR_RETRY_MS` | 100 ms | Transmitter retry delay while waiting for pairing ACKs. |
| `RADIO_PAIR_OK_MS` | 1500 ms | Duration for the link-success LCD message. |
| `RADIO_HEARTBEAT_MS` | 500 ms | Transmitter heartbeat interval while idle. |
| `RADIO_LINK_TIMEOUT_MS` | 2000 ms | Receiver "No TX signal" threshold. |
| `RADIO_GATE_REPEAT_MS` | 50 ms | Repeat interval while gate 1 remains broken during a run. |
| `RADIO_DISPLAY_MS` | 10 ms | Receiver timer LCD refresh interval. |

## Runtime flow

### Receiver module boundaries

The receiver sketch is intentionally split so the loop can service radio,
timing, input, and transient LCD messages without putting the whole unit into a
blocking menu:

| Module | Responsibility |
| --- | --- |
| `laser_timer_v2_rx.ino` | Initializes hardware and schedules radio, timer, buzzer, menu, and toast work each loop. |
| `src/functions_laser_timer_v2_rx.h` | Owns the gate 2 timer, radio packet handling, link state, speed calculation, and buzzer timing. |
| `src/menu_rx.cpp` | Implements the `MenuScreen` state machine and menu actions. |
| `src/encoder_rx.cpp` | Debounces rotation and classifies short and long button presses. |
| `src/display_rx.cpp` | Formats 16x2 LCD lines and manages non-blocking toast expiry. |
| `src/settings_rx.cpp` | Loads and saves receiver settings in EEPROM. |

`PollRadio()` and `tickFirstPairing()` run on every receiver loop. When the menu
is closed, `Timer()` handles gate 2 and display updates. When the menu is open
and the stopwatch is armed, the loop still calls `Sense_Gate2()` so an active
run can finish. `menuTick()` advances the menu state machine, and `toastTick()`
expires short status messages before the appropriate menu or idle screen is
restored.

### Boot and pairing

1. Receiver setup initializes GPIO, loads EEPROM settings, initializes the menu,
   starts the LCD, initializes RF24, opens pipe `1` for TX -> RX packets, and
   starts listening.
2. Transmitter setup initializes GPIO, LCD, SPI, and RF24, then calls
   `waitForPairing()`.
3. `waitForPairing()` blocks in transmitter setup until the send helper reports
   success. With global auto-ack enabled, this is intended to represent delivery
   of `CMD_PING`. The transmitter then opens the RX -> TX pipe and starts
   listening for finish packets.

Power the receiver first during normal operation. If the receiver is not
listening, the transmitter remains on the "Pairing..." screen.

Receiver pairing is non-blocking. Until its first valid packet,
`tickFirstPairing()` keeps the pairing prompt visible when no menu or toast is
active, but the encoder can still open the menu. The first packet clears
`rxAwaitingFirstLink` and shows the success screen for `RADIO_PAIR_OK_MS`.

### Starting and finishing a run

1. Long-press the receiver encoder to open the menu, then select **Stopwatch**.
   This sets the receiver-level `timer_state` to `ENABLED`.
2. When gate 1 breaks, the transmitter sends a gate-open burst and sets
   `txTimerRunning = true`.
3. The receiver ignores gate commands until `timer_state == ENABLED`. When armed,
   the first valid `CMD_GATE1_OPEN` starts the gate 2 timer.
4. When gate 2 breaks while the timer is running, the receiver:
   - calculates `periodMillis`
   - prints elapsed seconds
   - optionally prints speed
   - sends acknowledged `CMD_GATE2_CLOSED` back to the transmitter
   - shows "Finished" for 1500 ms
5. The transmitter handles `CMD_GATE2_CLOSED`, shows "Timer complete!", buzzes,
   and returns to the idle laser screen.

## Receiver menu and settings

Open the receiver menu with a long encoder press (`800 ms`). Rotate to move
through items, short-press to select, and long-press inside a menu to return to
idle.

Main menu:

| Item | Behavior |
| --- | --- |
| `Stopwatch` | Arms the receiver timer and flushes pending RX packets. |
| `Speed` | Opens speed display and distance settings. |
| `Radio` | Shows live link status and offers re-pairing. |
| `Buzzer` | Toggles alignment and finish beeps. |
| `Back` | Returns to idle display. |

Speed menu:

- Toggle speed display on/off.
- Presets: 40, 10, 5, 2, and 1 yard.
- Custom distance: 1 to 99 yards internally.
- Units toggle: yards/MPH or meters/KPH. Meter values are displayed by
  converting the stored yard distance.
- Preset screens include a preview speed line for a 5 second run.

Radio menu:

- `Link OK` means a packet arrived within `RADIO_LINK_TIMEOUT_MS`.
- `No link` means the receiver has not recently heard from the transmitter.
- `Re-pair` shows the pairing screen until a valid packet arrives or the user
  presses/long-presses to leave the screen.

Buzzer menu:

- `Align` controls the gate 2 alignment beep.
- `Finish` controls the run-complete beep.

### EEPROM settings

Receiver settings are stored at EEPROM address `0` with magic byte `0x52`.
Defaults remain in RAM if the magic byte does not match.

| Field | Default | Notes |
| --- | --- | --- |
| `distance` | `ENABLED` | Controls whether speed is printed after a run. |
| `distance_in_yards` | `40` | Used for MPH/KPH calculation. |
| `custom_distance_yards` | `40` | Last custom value. |
| `buzzer_align_enabled` | `true` | Gate 2 alignment beep. |
| `buzzer_finish_enabled` | `true` | Finish beep. |
| `use_meters` | `false` | Display units only; stored distance remains yards. |

There is no schema version beyond the magic byte. If the settings layout changes,
add a migration/version strategy before shipping the change.

## Developer pitfalls

- The transmitter pairing loop is blocking; start the receiver first when
  testing the link.
- `sendPacketOnPipe(..., requireAck)` passes that boolean to RF24 as
  `multicast`, whose polarity and setup requirements differ. See the radio
  protocol note above before changing retry or burst behavior.
- Selecting **Stopwatch** arms the receiver. Gate packets are ignored while the
  receiver-level `timer_state` is disabled.
- The receiver still checks gate 2 while the menu is open if the stopwatch is
  armed, so a run can finish during menu navigation.
- The transmitter encoder long-press is reserved, but the TX menu is not
  implemented.
- Edit all three `radio_protocol_v2.h` copies or change the build/include setup
  so both sketches use one source of truth.
- The current firmware drives `A0` high for radio power settling. The checked-in
  KiCad PCB labels the 3.3 V regulator enable net as `A7`/`3V3_EN`; see
  `docs/hardware-production.md` before changing hardware or power sequencing.
