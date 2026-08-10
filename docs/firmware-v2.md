# Laser Timer Firmware v2 — Technical Reference

Source-verified architecture notes for the active v2 TX/RX sketches. Behavior below is taken from code under `firmware/`; do not treat the thin product README as authoritative for protocol or state machines.

## 1. Directory layout

```
firmware/
  common/
    radio_protocol_v2.h          # Canonical shared protocol (also copied into each sketch)
  transmitter/
    laser_timer_v2_tx/
      laser_timer_v2_tx.ino      # setup/loop entry
      src/
        macros_laser_timer_v2_tx.h
        functions_laser_timer_v2_tx.h   # all TX logic (header-implemented)
        radio_protocol_v2.h             # copy of common
  receiver/
    laser_timer_v2_rx/
      laser_timer_v2_rx.ino      # setup/loop entry
      src/
        macros_laser_timer_v2_rx.h
        functions_laser_timer_v2_rx.h   # gate/timer/radio core
        radio_protocol_v2.h             # copy of common
        menu_rx.h / menu_rx.cpp
        display_rx.h / display_rx.cpp
        encoder_rx.h / encoder_rx.cpp
        settings_rx.h / settings_rx.cpp
```

Arduino IDE expects each sketch opened from its folder (`laser_timer_v2_tx` / `laser_timer_v2_rx`). Sketch sources use `#include "src/..."`; `firmware/common/` is **not** on the include path at compile time — sketches compile their local `src/radio_protocol_v2.h` copies.

## 2. Radio protocol (`radio_protocol_v2.h`)

### Packet

```c
struct __attribute__((packed)) RadioPacket {
  uint8_t magic;  // RADIO_MAGIC 0xA5
  uint8_t cmd;
};
```

### Commands

| Value | Name | Direction | Role |
|------:|------|-----------|------|
| `1` | `CMD_GATE1_OPEN` | TX → RX | Start / keep timing (beam broken at gate 1) |
| `2` | `CMD_GATE1_CLOSED` | TX → RX | Gate 1 beam restored |
| `3` | `CMD_GATE2_CLOSED` | RX → TX | Finish crossed; TX shows complete |
| `0xFF` | `CMD_PING` | TX → RX | Pairing / heartbeat |

### Addresses and pipes

Both sketches define:

```c
const byte addresses[][6] = {"00001", "00002"};
```

| Index | Address | Macro | Use |
|------:|---------|-------|-----|
| `[0]` | `"00001"` | `RADIO_RX_SEND_PIPE` (0) | RX → TX return (finish) |
| `[1]` | `"00002"` | `RADIO_TX_SEND_PIPE` (1) | TX → RX primary (gate 1, ping) |

Helpers:

- `sendPacket` / `sendGateOpenBurst` → write on pipe index `RADIO_TX_SEND_PIPE` (address `00002`)
- `sendToTransmitter` → write on pipe index `RADIO_RX_SEND_PIPE` (address `00001`)
- RX listens with `openReadingPipe(0, addresses[RADIO_TX_SEND_PIPE])` — **hardware pipe 0**, address `00002`
- TX return listen: `openReadingPipe(0, addresses[RADIO_RX_SEND_PIPE])` — hardware pipe 0, address `00001`

### Radio constants

| Constant | Value | Meaning |
|----------|------:|---------|
| `RADIO_CHANNEL` | 108 | nRF24 channel |
| `RADIO_HEARTBEAT_MS` | 500 | TX ping interval when idle |
| `RADIO_LINK_TIMEOUT_MS` | 2000 | RX “No TX signal” window |
| `RADIO_GATE_REPEAT_MS` | 50 | TX re-sends `GATE1_OPEN` while beam held |
| `RADIO_DISPLAY_MS` | 10 | RX live-time LCD refresh |
| `RADIO_GATE_BURST_COUNT` | 3 | No-ack opens before one “acked” open |
| `RADIO_POWER_SETTLE_MS` | 500 | Delay after driving radio power pin |
| `RADIO_PAIR_RETRY_MS` | 100 | TX pairing retry delay |
| `RADIO_PAIR_OK_MS` | 1500 | Pairing success splash duration |

### `initRadio` / send helpers

`initRadio`:

- `begin()`, PA min → 250 kbps → channel 108 → `setAutoAck(true)` → retries `(5, 15)` → CRC-16 → fixed payload `sizeof(RadioPacket)` → PA high
- Does **not** call `enableDynamicAck()`

`sendPacketOnPipe(..., requireAck)`:

- `stopListening`, `openWritingPipe`, `flush_tx`
- Attempts: 10 if `requireAck`, else 1
- Calls `radio.write(&pkt, sizeof(pkt), requireAck)` — third RF24 arg is **multicast** (see pitfalls)

`sendGateOpenBurst`: three `CMD_GATE1_OPEN` with `requireAck=false`, then one with `requireAck=true`.

## 3. TX firmware architecture

**Entry:** `laser_timer_v2_tx.ino`  
**Logic:** `src/functions_laser_timer_v2_tx.h` (implemented in the header)

### Pins / objects

| Resource | Pin / value |
|----------|-------------|
| Gate 1 sensor | D2 (`GATE_ACTIVATED == HIGH`) |
| Buzzer | D1 |
| Encoder button / CLK / DAT | D3 / D4 / D5 |
| nRF24 CE / CSN | D9 / D10 |
| Radio power enable (firmware) | **A0** driven HIGH |
| LCD | I2C `0x27`, 16×2 |

### `setup()`

1. Gate/buzzer/`encoderInitTx`
2. `A0` HIGH + `RADIO_POWER_SETTLE_MS`
3. LCD init, custom chars, splash `"LASER TIMER TX"` (3 s)
4. Pairing screen → SPI → `initRadio` (halt forever on failure)
5. `openWritingPipe(addresses[RADIO_TX_SEND_PIPE])`
6. **`waitForPairing()`** — blocking loop: `sendPacket(CMD_PING, true)` every 100 ms until success
7. Seed `gateWasOpen` from sensor; show gate-open or idle laser art

### `loop()`

```
checkEncoderOpensMenu();
Sense_Gate1();
```

### Key functions

| Function | Role |
|----------|------|
| `waitForPairing` | Blocks until ping “succeeds”; then `resumeTxListening` |
| `resumeTxListening` | `openReadingPipe(0, addresses[0])` + `startListening` |
| `PollTxRadio` | Drain RX FIFO; on `CMD_GATE2_CLOSED` while `txTimerRunning`, clear flag, show complete, buzz, **block ~1.5 s**, restore idle art |
| `Sense_Gate1` | Rising edge → burst + `txTimerRunning` + counting screen; falling → `GATE1_CLOSED`; idle heartbeat ping; while open+running repeat open every 50 ms; always `handleTxBuzzer` |
| `checkEncoderOpensMenu` | Long-press (≥800 ms) reserved; **TX menu not implemented** |

### Blocking points (TX)

- Radio missing: infinite `while(true)` in `setup`
- `waitForPairing`: infinite until ping returns true
- Pairing success `delay(RADIO_PAIR_OK_MS)`
- Splash `delay(3000)`
- Finish path: `while (millis() < completeUntil)` ~1500 ms inside `PollTxRadio`
- Long-press handler waits for button release with `while (... == LOW) delay(1)`

## 4. RX firmware architecture (modular)

**Entry:** `laser_timer_v2_rx.ino`  
**Core:** `functions_laser_timer_v2_rx.h`  
**Modules:** `menu_rx`, `display_rx`, `encoder_rx`, `settings_rx`

### Module interaction

```
loop()
  ├─ PollRadio()                 # functions — drain TX→RX packets
  ├─ tickFirstPairing()          # pairing splash until first valid packet
  ├─ checkEncoderOpensMenu()     # long-press → menuRequestOpen + waitForEncoderRelease
  ├─ if !menuIsActive():
  │    Timer() → Sense_Gate2 + Gate2_Timer_Action + idle line1
  │  else if timer_state == ENABLED:
  │    Sense_Gate2() only        # sensor still live; no live-time LCD updates
  ├─ handleRxBuzzer()
  ├─ menuTick()                  # may PollRadio again; encoder nav
  └─ toastTick() + restore menu/idle when toast ends
```

### Public APIs between modules

**`menu_rx.h`**

- `menuInit()`, `menuRequestOpen()`, `menuIsActive()`, `menuRefresh()`, `menuTick()`
- Screens: `MS_Idle`, `MS_Splash`, `MS_Main`, `MS_Distance`, `MS_Custom`, `MS_Radio`, `MS_Buzzer`, `MS_RePairing`
- `menuIsActive()` is true only when screen is **not** `MS_Idle` and **not** `MS_Splash`

**`display_rx.h`**

- `lcdPrintLine`, `lcdPrintMenuHeading`, `lcdPrintMenuItemLine`
- `showToast` / `toastTick` / `toastActive` (`MENU_TOAST_MS` = 300)
- `showMenuSplash` — defined, **unused** by current menu code

**`encoder_rx.h`**

- `encoderInit()`, `pollEncoder()` → `EncNone|CW|CCW|Press|LongPress`
- `waitForEncoderRelease()` — blocking
- Debounce: step 50 ms, button 50 ms, long-press 800 ms
- Pins: button D3, CLK D4, DAT D5

**`settings_rx.h`**

- Globals: `distance`, `distance_in_yards`, `custom_distance_yards`, `buzzer_align_enabled`, `buzzer_finish_enabled`, `use_meters`
- `loadSettings()` / `saveSettings()` — EEPROM magic `0x52` at address 0

**`functions_laser_timer_v2_rx.h` (selected)**

- Radio: `PollRadio`, `resumeRxListening`, `tickFirstPairing`, pairing screens
- Timer/gate: `Timer`, `Sense_Gate2`, `Gate2_Timer_Action`, `stopTimerAtGate2`, `printSpeed`
- Display helpers: `restoreIdleDisplay`, `updateLinkDisplay`, `refreshLine1WhenIdle`, `clearLine1`
- Buzzer: `startRxBuzzer`, `handleRxBuzzer`

### Pairing

**Boot:** LCD pairing screen → radio init → listen on address `00002`. `rxAwaitingFirstLink` stays true until any valid magic packet; then success splash for `RADIO_PAIR_OK_MS`, then idle.

**Menu Re-pair (`MS_RePairing`):** shows pairing screen; `tickRePairing()` calls `PollRadio()` then checks `radio.available()` for a second drain path to show success and enter `MS_Splash`. See pitfalls — this path is effectively unreachable for normal arrivals.

### Stopwatch / gate logic / timer states

Two layers of “enabled”:

| Variable | Type | Meaning |
|----------|------|---------|
| `timer_state` | `char` `ENABLED('E')` / `DISABLED('D')` | Armed for runs (menu **Stopwatch**) |
| `gate2.timer_state` | `char` `ON('I')` / `OFF('O')` | Actively counting this pass |
| `gate2.able_state` | `char` | Written `DISABLED` on finish; **not read** elsewhere |
| `gate1_opened` | `bool` | Set from radio cmds; **not used** for decisions |

Arm (Main → Stopwatch):

- `timer_state = ENABLED`
- Clears `finishedUntilMs`, seeds `gate2BeamWasBroken`, `clearLine1()`, **`radio.flush_rx()`**, toast `"Starting..."`, exits menu
- Does **not** reset `gate2.timer_state` / `startMillis` (normally already OFF/0 after a finish)

Start count: `PollRadio` on `CMD_GATE1_OPEN` while `timer_state == ENABLED` and `gate2.timer_state == OFF` → set ON, `startMillis = millis()`, clear finish hold / line1.

Finish: rising edge on gate 2 while `gate2.timer_state == ON` → `stopTimerAtGate2`:

- Freeze time, `gate2.timer_state = OFF`, optional speed line, `sendToTransmitter(CMD_GATE2_CLOSED, true)`, `"Finished"` for 1500 ms, optional finish beep
- **`timer_state` remains `ENABLED`** — next `GATE1_OPEN` starts another run without re-opening the menu

Live time: `Gate2_Timer_Action` prints `periodMillis` (seconds, 2 decimals) every 10 ms while counting — only when menu inactive.

### RX menu map

- **Main:** Stopwatch, Speed, Radio, Buzzer, Back  
- **Speed:** Speed ON/OFF, presets 40/10/5/2/1 yd (meters display via `yards*0.9144`), Custom (1–99 yd edit), Units yards/meters, Back  
- **Radio:** Link OK / No link (from `lastRadioRxMs`), Re-pair, Back  
- **Buzzer:** Align ON/OFF, Finish ON/OFF, Back  
- Long-press in a menu exits to idle; short press selects; toast briefly blocks nav

### RX pins

| Resource | Pin |
|----------|-----|
| Gate 2 | D2 (`GATE_ACTIVATED == HIGH`) |
| Buzzer | D1 |
| Encoder | D3 / D4 / D5 |
| nRF24 CE / CSN | D9 / D10 (`radio_cs_pin` also 10) |
| SD CS (held high only) | D8 — **no SD feature** |
| Radio power (firmware) | **A0** HIGH |
| LCD | I2C `0x27` |

## 5. Pitfalls and constraints (source-verified)

### Ack polarity / dynamic ACK

`sendPacketOnPipe` passes `requireAck` as RF24 `write()`’s third argument, which is **`multicast`**: `true` = no ACK. So `requireAck=true` requests **no** ACK, and `false` requests ACK — inverted relative to the parameter name. `enableDynamicAck()` is never called; per-packet NO_ACK behavior is therefore not correctly enabled either.

- `firmware/common/radio_protocol_v2.h:69-85` (and both sketch copies)

### `flush_rx` on Stopwatch arm

Arming Stopwatch calls `radio.flush_rx()`, which can drop an in-flight `CMD_GATE1_OPEN` that arrived just before arm.

- `menu_rx.cpp:341-348`

### `timer_state` stays ENABLED after finish

`stopTimerAtGate2` sets `gate2.timer_state = OFF` but never clears global `timer_state`. After a finished run the unit remains armed for the next start packet.

- `functions_laser_timer_v2_rx.h:289-317`, `161-174`

### Poll radio double-drain (re-pair)

`loop` always calls `PollRadio()` first (drains FIFO). `tickRePairing` calls `PollRadio()` again, then `if (radio.available())` for the Link-OK UI. Packets are already consumed; the post-drain `available()` path is effectively unreachable except for a tiny race.

- `laser_timer_v2_rx.ino:92`, `menu_rx.cpp:528-546`  
- Additionally, open menus call `PollRadio()` again in `menuTick` (`menu_rx.cpp:600`)

### A0 vs A7 power enable

Firmware drives **A0** HIGH for radio power settle (TX and RX). PCB maps Nano **A7** to net `/3V3_EN` (`Laser_Timer_PCB.kicad_pcb` pad A7 → net 26). On Arduino Nano, A7 is analog-input-only (no digital output). Firmware pin and PCB enable net disagree.

- TX: `laser_timer_v2_tx.ino:35-37`  
- RX: `laser_timer_v2_rx.ino:41-43`  
- PCB: A7 → `/3V3_EN`

### `waitForEncoderRelease` blocking

```c
while (digitalRead(encoderButton) == LOW) { delay(1); }
```

Blocks the entire RX loop (no radio/timer service) until release. Used after menu open, selects, custom confirm, re-pair cancel, etc.

- `encoder_rx.cpp:66-72`

### Three copies of protocol header

Identical today (`diff` clean) at:

1. `firmware/common/radio_protocol_v2.h`
2. `firmware/transmitter/laser_timer_v2_tx/src/radio_protocol_v2.h`
3. `firmware/receiver/laser_timer_v2_rx/src/radio_protocol_v2.h`

Sketches include the local copy only — edits must be triplicated or the build must be changed to share one file.

### Menu open while armed

When `menuIsActive()` and `timer_state == ENABLED`, `Sense_Gate2` still runs but `Gate2_Timer_Action` does not — finish can still trigger; live seconds do not update on LCD.

- `laser_timer_v2_rx.ino:96-103`

### Other notes

- TX encoder long-press blocks on release; menu stub only (`functions_laser_timer_v2_tx.h:193-200`)
- `gate2.able_state` and RX `gate1_opened` are written but unused for control flow
- Speed math uses `periodMillis` in seconds; divide-by-zero if finish somehow ran with `periodMillis == 0` is not guarded in `printSpeed`

## 6. EEPROM / settings / LCD UX

### EEPROM (`settings_rx`)

`SettingsBlock` at EEPROM address 0:

| Field | Default if magic ≠ `0x52` |
|-------|---------------------------|
| `distance` | `ENABLED` (`'E'`) — speed line on finish |
| `distance_in_yards` | 40 |
| `custom_distance_yards` | 40 |
| `buzzer_align` | true |
| `buzzer_finish` | true |
| `use_meters` | false |

`saveSettings()` on every toggle/preset/custom/units change. TX has **no** EEPROM settings.

### LCD UX patterns

- 16×2 I2C, custom laser/tripod/antenna/back-arrow glyphs
- Idle RX: `"Ready"` or `"No TX signal"` / `"Align laser"` / pairing lines
- Counting: seconds on line 0; finish adds `"Finished"` and optional `x.xMPH` / `KPH` on line 0
- Menus: heading + `n/N item` on line 1; distance presets show speed-at-5s preview on line 1
- Toasts 300 ms; pairing success 1500 ms

### Encoder UI flows (RX)

1. Idle long-press (≥800 ms) → main menu (after release wait)  
2. Rotate CW/CCW → index; press → action; long-press → exit idle  
3. Custom: rotate 1–99, press saves both `custom_distance_yards` and `distance_in_yards`  
4. Re-pair: press or long-press cancels back to Radio item  

TX: short press ignored; long-press reserved (no UI).

## 7. Build / flash assumptions

From sketch headers and includes (no PlatformIO / CI config in repo):

| Item | Assumption |
|------|------------|
| Board | ATmega328 (Arduino Nano v3.x on shared PCB) |
| IDE | Arduino IDE; open sketch folder as project root |
| Libraries | `RF24` (TMRh20), `SPI`, `Wire`, `LiquidCrystal_I2C`, `EEPROM` (RX) |
| LCD address | `0x27`, 16×2 |
| Radio | nRF24L01+PA+LNA, CE=9, CSN=10, channel 108, 250 kbps, CRC-16, fixed 2-byte payload |
| Gate polarity | Beam broken = `HIGH` on D2 for both units |

No `platformio.ini`, Makefile, or library version pins are present in-tree.

## 8. Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| `Radio not found` | `radio.begin()` failed: wiring, CE/CSN 9/10, or radio power path (`docs/hardware-production.md`). |
| TX stuck on `Pairing...` | RX not powered / not listening on `"00002"`, or ACK path failing. Power RX first. |
| RX `No TX signal` | No valid packet within 2 s; check TX power, range, channel 108. |
| RX `Align laser` | Gate 2 sensor pin is `HIGH` while idle; realign or check polarity. |
| Timing never starts | Arm **Stopwatch** before breaking gate 1. |
| No speed line | Enable speed and set distance in the Speed menu. |
| Flaky upload / serial | Buzzer shares `D1` / Nano TX. |
| Re-pair never auto-exits | Packet drain before `tickRePairing` sees `radio.available()`; leave with a press. |
| Second run starts without menu | Expected: after finish, `timer_state` stays `ENABLED`. |

## 9. Change checklist

When changing the radio contract or pin map:

1. Edit all three `radio_protocol_v2.h` copies in the same commit (or switch sketches to a single include).
2. Keep TX write / RX listen addresses matched (`"00002"` primary, `"00001"` return).
3. Re-test pairing (RX first), an armed run, finish notify, and TX complete UI.
4. If GPIO changes, update both sketches and `docs/hardware-production.md`.
5. If `SettingsBlock` layout changes, add a migration strategy beyond magic `0x52`.

Hardware pin map and fab artifacts: [`docs/hardware-production.md`](hardware-production.md).
