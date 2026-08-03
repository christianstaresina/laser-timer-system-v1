# Firmware v2

Source-verified against the transmitter and receiver sketches on this branch.
Intent: document architecture, the public radio contract, operating workflows,
and constraints developers hit when changing or debugging the firmware.

## Sketches and dependencies

| Unit | Sketch directory | Role |
| --- | --- | --- |
| Transmitter | `firmware/transmitter/laser_timer_v2_tx/` | Gate 1 start unit. Detects beam break, sends start events, waits for finish confirmation. |
| Receiver | `firmware/receiver/laser_timer_v2_rx/` | Gate 2 finish unit. Timer, LCD menu, EEPROM settings, speed display. |

Libraries:

- Built in (AVR): `SPI`, `Wire`, `EEPROM`
- Install separately: `RF24`, `LiquidCrystal_I2C`

There is no PlatformIO / Arduino CLI project file in-repo. Open the sketch
folder in the Arduino IDE so `src/` is compiled with the `.ino`.

## Receiver architecture

The RX sketch was modularized so the main loop can keep servicing radio and
timing while menus and toasts are visible.

| Module | Responsibility |
| --- | --- |
| `laser_timer_v2_rx.ino` | Setup + per-loop scheduling of radio, timer/gate, buzzer, menu, toast. |
| `src/functions_laser_timer_v2_rx.h` | Gate 2 timer, `PollRadio()`, link UI, speed math, buzzer timing. |
| `src/menu_rx.cpp` / `.h` | `MenuScreen` state machine and actions (including re-pair). |
| `src/encoder_rx.cpp` / `.h` | Rotation debounce; short vs long (≥ 800 ms) press. |
| `src/display_rx.cpp` / `.h` | 16x2 line helpers and non-blocking toast expiry (`MENU_TOAST_MS` = 300). |
| `src/settings_rx.cpp` / `.h` | EEPROM load/save for receiver settings. |

Loop behavior (verified in `laser_timer_v2_rx.ino`):

1. Always call `PollRadio()` and `tickFirstPairing()`.
2. Open the menu on encoder long-press when idle.
3. If the menu is closed, run `Timer()` (gate 2 + display).
4. If the menu is open **and** stopwatch is armed (`timer_state == ENABLED`),
   still call `Sense_Gate2()` so an armed run can finish during menu navigation.
   Live timer LCD refresh (`Gate2_Timer_Action`) does not run in this branch.
5. `handleRxBuzzer()`, `menuTick()`, then `toastTick()` / restore screens.

Transmitter loop is simpler: `checkEncoderOpensMenu()` then `Sense_Gate1()`
(which polls the return radio path, gate sensor, heartbeats, and buzzer). The TX
encoder long-press path is reserved; the comment in code notes the TX menu is
not implemented yet.

## Shared hardware assumptions

| Signal | Pin / value | Notes |
| --- | --- | --- |
| Beam sensor | `D2`, active `HIGH` when broken | `GATE_ACTIVATED` |
| Buzzer | `D1` | Also Nano serial TX; can interfere with upload/serial |
| Encoder SW / CLK / DAT | `D3` / `D4` / `D5` | RX menu; TX long-press reserved (menu not implemented) |
| nRF24 CE / CSN | `D9` / `D10` | `RF24 radio(9, 10)` |
| LCD | I2C `0x27`, 16×2 | Both sketches |
| Radio power settle | Drive `A0` high, wait 500 ms | See hardware doc for PCB mismatch |
| RX SD CS placeholder | `D8` driven high in setup | No SD logging in current firmware |

## Radio protocol

Packet and helper definitions exist in three identical files today:

- `firmware/common/radio_protocol_v2.h` (reference / shared copy)
- `firmware/transmitter/laser_timer_v2_tx/src/radio_protocol_v2.h` (compiled)
- `firmware/receiver/laser_timer_v2_rx/src/radio_protocol_v2.h` (compiled)

Sketches include the local `src/` copy. Change all three in the same commit, or
repoint includes to a single source of truth.

### Packet format

```cpp
struct __attribute__((packed)) RadioPacket {
  uint8_t magic; // RADIO_MAGIC = 0xA5
  uint8_t cmd;
};
```

Packets with any other magic are ignored.

| Command | Value | Direction | Meaning |
| --- | ---: | --- | --- |
| `CMD_GATE1_OPEN` | `1` | TX → RX | Gate 1 beam broke; start/continue RX timer when armed. |
| `CMD_GATE1_CLOSED` | `2` | TX → RX | Gate 1 beam restored. |
| `CMD_GATE2_CLOSED` | `3` | RX → TX | Gate 2 finished the run. |
| `CMD_PING` | `0xFF` | TX → RX | Pairing / heartbeat. |

Addresses (`addresses[][6]`):

| Index | Address | Role |
| --- | --- | --- |
| `0` (`RADIO_RX_SEND_PIPE`) | `"00001"` | RX → TX return path. |
| `1` (`RADIO_TX_SEND_PIPE`) | `"00002"` | TX → RX primary path. |

How pipes are opened in code:

- RX setup / `resumeRxListening()`: `openReadingPipe(0, addresses[RADIO_TX_SEND_PIPE])`
  — RF24 reading-pipe **index** 0, address `"00002"`.
- TX after pairing / `resumeTxListening()`: `openReadingPipe(0, addresses[RADIO_RX_SEND_PIPE])`
  — reading-pipe index 0, address `"00001"`.
- TX writes with `sendPacket` → writing pipe address `"00002"`.
- RX finish notify uses `sendToTransmitter` → writing pipe address `"00001"`.

Radio settings from `initRadio()`:

- Channel `108`, `RF24_250KBPS`, CRC-16, `setAutoAck(true)`, retries `(5, 15)`
- Payload size `sizeof(RadioPacket)`
- PA starts at `RF24_PA_MIN`, then raised to `RF24_PA_HIGH`

### ACK helper caveat (current code)

`sendPacketOnPipe(..., requireAck)` passes `requireAck` straight into
`radio.write(&pkt, sizeof(pkt), requireAck)`.

In RF24, that third argument is **`multicast`**: `true` means NOACK, `false`
means ACK. `initRadio()` does **not** call `enableDynamicAck()`.

Consequences with the checked-in firmware:

- Global auto-ack stays enabled.
- The boolean does **not** provide the named per-packet ACK/NOACK behavior.
- Gate-open burst still issues three `requireAck=false` calls plus one
  `requireAck=true` call, but do not infer delivery semantics from those names
  until the wrapper polarity and `enableDynamicAck()` are fixed and hardware-tested.

### Timing constants

| Constant | Value | Purpose |
| --- | ---: | --- |
| `RADIO_POWER_SETTLE_MS` | 500 | After driving radio power-enable high |
| `RADIO_PAIR_RETRY_MS` | 100 | TX pairing retry delay |
| `RADIO_PAIR_OK_MS` | 1500 | Link-success LCD hold |
| `RADIO_HEARTBEAT_MS` | 500 | TX idle ping interval |
| `RADIO_LINK_TIMEOUT_MS` | 2000 | RX “No TX signal” / Radio menu link window |
| `RADIO_GATE_REPEAT_MS` | 50 | TX repeat while gate 1 stays open during a run |
| `RADIO_DISPLAY_MS` | 10 | RX live timer LCD refresh |
| `RADIO_GATE_BURST_COUNT` | 3 | Unacked portion of gate-open burst (by argument name) |
| `RX_FINISHED_DISPLAY_MS` | 1500 | RX “Finished” hold |
| `ENCODER_LONG_PRESS_MS` | 800 | Open / exit menu |

## Runtime workflows

### Boot and pairing

1. RX: GPIO + EEPROM settings + menu init → LCD splash (3 s) → pairing screen →
   `initRadio()` → listen on address `"00002"`.
2. TX: GPIO + LCD splash → `initRadio()` → `waitForPairing()` **blocks** in setup
   until `sendPacket(..., CMD_PING, true)` reports success → opens return
   listening pipe → idle laser screen (or gate-open screen if beam already broken).

Power the receiver first. If RX is not listening, TX stays on **Pairing...**.

RX first-link UX is non-blocking: `tickFirstPairing()` keeps the pairing prompt
visible until the first valid packet (unless a menu/toast is active). The first
packet clears `rxAwaitingFirstLink` and shows success for `RADIO_PAIR_OK_MS`.

### Starting and finishing a run

1. Long-press RX encoder → **Stopwatch**. Sets receiver-level `timer_state` to
   `ENABLED`, clears `finishedUntilMs`, samples current gate-2 beam state, and
   `flush_rx()` when the radio is ready. It does **not** clear an in-progress
   `gate2.timer_state` / `startMillis` (see pitfalls).
2. Gate 1 break → TX `sendGateOpenBurst()`, sets `txTimerRunning`, shows
   **Timer counting**, buzzes.
3. RX ignores gate commands until armed. First valid `CMD_GATE1_OPEN` starts
   `gate2.timer_state = ON` and `startMillis`.
4. Gate 2 break while running → `stopTimerAtGate2()`: print time, optional speed,
   one `sendToTransmitter(..., CMD_GATE2_CLOSED, true)`, show **Finished** for
   1.5 s, optional finish beep. Receiver-level `timer_state` stays `ENABLED`, so
   a later `CMD_GATE1_OPEN` can start another run without re-selecting Stopwatch.
5. TX `PollTxRadio()` handles `CMD_GATE2_CLOSED`, shows **Timer complete!**,
   buzzes, then **blocks ~1.5 s** in a `while (millis() < completeUntil)` loop
   before returning to the idle laser screen.

While gate 1 stays open and the TX timer is running, TX repeats
`CMD_GATE1_OPEN` every 50 ms. While idle, it sends `CMD_PING` every 500 ms.

## Receiver menu and settings

Open with long press (800 ms). Rotate to move, short-press to select, long-press
inside a menu to return to idle.

| Main item | Behavior |
| --- | --- |
| `Stopwatch` | Arms receiver timing (`timer_state = ENABLED`) and flushes the RX FIFO. |
| `Speed` | Speed on/off, distance presets, custom yards, units. |
| `Radio` | Live link status + **Re-pair**. |
| `Buzzer` | Align beep / finish beep toggles. |
| `Back` | Idle display. |

Speed:

- Presets: 40 / 10 / 5 / 2 / 1 yard.
- Custom: 1–99 yards stored internally.
- Units: yards/MPH or meters/KPH (stored distance remains yards; display converts).

Radio:

- **Link OK** if a valid packet arrived within `RADIO_LINK_TIMEOUT_MS`.
- **Re-pair** enters `MS_RePairing` and shows the pairing screen. Exit with a
  short or long press. Automatic “Link OK” completion is unreliable; see
  pitfalls.

### EEPROM

Stored at address `0`, magic `0x52` (`SETTINGS_EEPROM_MAGIC`). Wrong magic →
keep RAM defaults.

| Field | Default |
| --- | --- |
| `distance` (speed display enable) | `ENABLED` |
| `distance_in_yards` | `40` |
| `custom_distance_yards` | `40` |
| `buzzer_align_enabled` | `true` |
| `buzzer_finish_enabled` | `true` |
| `use_meters` | `false` |

No schema version beyond the magic byte. Add a migration strategy before
changing the `SettingsBlock` layout.

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| `Radio not found` | `radio.begin()` failed: wiring, CE/CSN 9/10, or radio power path. |
| TX stuck on `Pairing...` | RX not powered / not listening on `"00002"`, or ACK path failing. |
| RX `No TX signal` | No valid packet within 2 s; check TX power/range/channel. |
| RX `Align laser` | Gate 2 sensor pin is `HIGH` while idle; realign or check polarity. |
| Timing never starts | Arm **Stopwatch** before breaking gate 1. |
| No speed line | Enable speed and set distance in the Speed menu. |
| Flaky upload / serial | Buzzer shares `D1` / Nano TX. |
| Re-pair never auto-exits | Packet drain before `tickRePairing` sees `radio.available()`; leave with a press. |

## Developer pitfalls (current branch)

These are verified in the checked-in sources; do not assume they are fixed
unless the code changes.

1. **`requireAck` / RF24 `multicast` polarity** — helper argument names do not
   match RF24 semantics; `enableDynamicAck()` is never called. See radio section.
2. **Blocking encoder release (RX)** — `waitForEncoderRelease()` spins with
   `delay(1)` while the button is held. That freezes radio/timer work for the
   hold duration after menu opens/selects/exits.
3. **Blocking TX complete display** — on `CMD_GATE2_CLOSED`, TX waits ~1.5 s in
   a busy loop and does not sense the gate or poll radio during that window.
4. **Blocking TX long-press** — TX encoder long-press also waits for release in a
   `while` loop (menu still unimplemented).
5. **Re-pair packet drain** — `loop()` always calls `PollRadio()` before
   `menuTick()`. `tickRePairing()` then calls `PollRadio()` again and only
   succeeds if `radio.available()` is still true afterward. After either drain,
   the success branch is effectively unreachable; leave re-pair with a press.
   `PollRadio()` does not advance the re-pair menu screen when a menu is active.
6. **Stopwatch arm + `flush_rx()`** — selecting Stopwatch sets
   `timer_state = ENABLED` and clears finish/beam edge state, but does not reset
   `gate2.timer_state` / `startMillis` / related run fields. The FIFO flush can
   also discard an in-flight `CMD_GATE1_OPEN`. After a finish, `timer_state`
   remains `ENABLED`, so another start packet can begin a new run without
   re-arming.
7. **Triple protocol headers** — edit all three `radio_protocol_v2.h` copies
   together.
8. **`A0` vs PCB `A7`/`3V3_EN`** — firmware power-enable pin does not match the
   checked-in regulator enable net; Nano `A7` is analog-input-only. See
   `docs/hardware-production.md`.
