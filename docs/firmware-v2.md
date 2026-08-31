# Laser Timer Firmware v2 — Technical Reference

Source-verified against the v2 TX/RX sketches on this branch. Product README
covers intent; this file is the protocol and state-machine reference.

## 1. Directory layout

```
firmware/
  common/radio_protocol_v2.h          # canonical copy (not on the Arduino include path)
  transmitter/laser_timer_v2_tx/
    laser_timer_v2_tx.ino             # setup/loop; constructs LCD at I2C 0x27
    src/macros_laser_timer_v2_tx.h
    src/functions_laser_timer_v2_tx.h # all TX logic (header-implemented)
    src/radio_protocol_v2.h           # compile-time copy
  receiver/laser_timer_v2_rx/
    laser_timer_v2_rx.ino             # setup/loop
    src/functions_laser_timer_v2_rx.h # gate/timer/radio core
    src/menu_rx.{h,cpp}               # table-driven menu
    src/display_rx.{h,cpp}
    src/encoder_rx.{h,cpp}
    src/settings_rx.{h,cpp}           # EEPROM
    src/macros_laser_timer_v2_rx.h
    src/radio_protocol_v2.h
```

Arduino IDE expects each sketch opened from its folder. `#include "src/..."`
resolves next to the `.ino`. The three `radio_protocol_v2.h` files are identical
today; editing only `firmware/common/` does not change the build.

## 2. Radio protocol (`radio_protocol_v2.h`)

### Packet

```c
struct __attribute__((packed)) RadioPacket {
  uint8_t magic;  // RADIO_MAGIC 0xA5
  uint8_t cmd;
};
```

Fixed payload size is `sizeof(RadioPacket)` (2 bytes). CRC-16, channel 108,
250 kbps, PA high after init.

### Commands

| Value | Name | Direction | Role |
|------:|------|-----------|------|
| `1` | `CMD_GATE1_OPEN` | TX → RX | Start / keep timing (beam broken at gate 1) |
| `2` | `CMD_GATE1_CLOSED` | TX → RX | Gate 1 beam restored (single packet, not a burst) |
| `3` | `CMD_GATE2_CLOSED` | RX → TX | Finish crossed; TX shows complete (single packet) |
| `0xFF` | `CMD_PING` | TX → RX | Pairing attempt and idle heartbeat |

### Addresses and pipes

Both sketches define `const byte addresses[][6] = {"00001", "00002"};`

| Index | Address | Macro | Use |
|------:|---------|-------|-----|
| `[0]` | `"00001"` | `RADIO_RX_SEND_PIPE` (0) | RX → TX return (finish) |
| `[1]` | `"00002"` | `RADIO_TX_SEND_PIPE` (1) | TX → RX primary (gate 1, ping) |

Listen/write pairing that must stay matched:

- TX writes primary with `openWritingPipe(addresses[RADIO_TX_SEND_PIPE])` (`00002`).
- RX listens with `openReadingPipe(0, addresses[RADIO_TX_SEND_PIPE])` — **hardware pipe 0**, address `00002`.
- RX finish uses `sendToTransmitter` → pipe index `RADIO_RX_SEND_PIPE` (`00001`).
- TX return listen: `openReadingPipe(0, addresses[RADIO_RX_SEND_PIPE])` — hardware pipe 0, address `00001`.

### Radio constants

| Constant | Value | Meaning |
|----------|------:|---------|
| `RADIO_CHANNEL` | 108 | nRF24 channel |
| `RADIO_HEARTBEAT_MS` | 500 | TX ping interval when idle and beam restored |
| `RADIO_LINK_TIMEOUT_MS` | 2000 | RX “No TX signal” / menu “No link” window |
| `RADIO_GATE_REPEAT_MS` | 50 | TX re-sends `GATE1_OPEN` while beam held and `txTimerRunning` |
| `RADIO_DISPLAY_MS` | 10 | RX live-time LCD refresh |
| `RADIO_GATE_BURST_COUNT` | 3 | Leading `GATE1_OPEN` sends before the trailing send |
| `RADIO_POWER_SETTLE_MS` | 500 | Delay after driving `A0` high |
| `RADIO_PAIR_RETRY_MS` | 100 | TX pairing retry delay |
| `RADIO_PAIR_OK_MS` | 1500 | Pairing success splash |

### `initRadio` / send helpers

`initRadio`: `begin()` → PA min → 250 kbps → channel 108 → `setAutoAck(true)` →
retries `(5, 15)` → CRC-16 → fixed payload → PA high. Does **not** call
`enableDynamicAck()`.

`sendPacketOnPipe(..., requireAck)`:

- `stopListening`, `openWritingPipe`, `flush_tx`
- Attempts: 10 if `requireAck`, else 1
- `radio.write(&pkt, sizeof(pkt), requireAck)` — the third RF24 argument is
  **`multicast`**, not “please ACK” (see pitfalls)

`sendGateOpenBurst`: three `CMD_GATE1_OPEN` with `requireAck=false`, then one
with `requireAck=true`. Combined with the multicast polarity, that is **not**
“three no-ACK then one ACK”.

## 3. Operating runbook (LCD + packets)

Power **RX first**, then TX. Typical screens:

| Step | TX LCD | RX LCD | Radio |
|------|--------|--------|-------|
| Boot splash 3 s | `LASER TIMER TX` | `LASER TIMER RX` | none |
| Pairing | `Pairing...` / `Waiting for link` | same | TX loops `CMD_PING` every 100 ms |
| Link | `Link OK!` / `Connected` (1.5 s) | same, then idle | RX accepts **any** `magic==0xA5` packet |
| Idle | `Pass laser` art | line 1 `Ready` (or `No TX signal` / `Align laser`) | TX `CMD_PING` every 500 ms if beam restored |
| Arm Stopwatch | unchanged | toast `Starting...` then idle | RX `flush_rx()` (can drop an in-flight open) |
| Gate 1 break | `Timer counting` / `Waiting gate 2` | live `N.NNs` | `sendGateOpenBurst`; repeats `GATE1_OPEN` every 50 ms while held |
| Gate 1 restore (run still live) | still counting | still counting | single `CMD_GATE1_CLOSED` |
| Gate 1 re-break during count | another burst + counting screen | clock **does not** restart (`gate2.timer_state` already `ON`) | extra `GATE1_OPEN` packets |
| Gate 2 break | after RX notify: `Timer complete!` (~1.5 s block) then idle art | frozen time, `Finished` 1.5 s, optional `x.xMPH`/`KPH` | `CMD_GATE2_CLOSED` |
| Next run | wait for a new gate-1 rising edge | **already armed** (`timer_state` stays `ENABLED`) | next `GATE1_OPEN` starts another count |

Arming is **receiver-only** (Main → Stopwatch). TX has no EEPROM and no menu
implementation (long-press is reserved and currently a no-op after release).

Speed on the finish LCD is **on by default** (`distance == ENABLED`, 40 yards)
until the Speed menu turns it off.

## 4. TX firmware architecture

**Entry:** `laser_timer_v2_tx.ino`
**Logic:** `src/functions_laser_timer_v2_tx.h`

### Pins / objects

| Resource | Pin / value |
|----------|-------------|
| Gate 1 sensor | D2 (`GATE_ACTIVATED == HIGH`) |
| Buzzer | D1, 2000 Hz; 450 ms on start, 700 ms on finish |
| Encoder button / CLK / DAT | D3 (`INPUT_PULLUP`) / D4 / D5 (`INPUT`, no pull-up) |
| nRF24 CE / CSN | D9 / D10 |
| Radio power pin (firmware) | **A0** driven HIGH |
| LCD | I2C `0x27`, 16×2 |

### `setup()`

1. Gate / buzzer / `encoderInitTx`
2. `A0` HIGH + `RADIO_POWER_SETTLE_MS`
3. LCD init, custom chars, splash `"LASER TIMER TX"` (3 s)
4. Pairing screen → SPI → `initRadio` (halt forever on failure: `"Radio not found"`)
5. `openWritingPipe(addresses[RADIO_TX_SEND_PIPE])`
6. **`waitForPairing()`** — blocking: `sendPacket(CMD_PING, true)` every 100 ms until the write helper returns true
7. Seed `gateWasOpen` from the sensor; show gate-open or idle laser art

Unlike RX, TX does not `pinMode` CSN / D8 before `initRadio`.

### `loop()`

```
checkEncoderOpensMenu();
Sense_Gate1();   // always PollTxRadio() first when radioReady
```

### `Sense_Gate1` branches

| Condition | Action |
|-----------|--------|
| Rising edge (beam breaks) | `sendGateOpenBurst`, `txTimerRunning = true`, counting screen, start beep |
| Falling edge (beam restores) | single `CMD_GATE1_CLOSED` (`requireAck=false`); idle art only if `!txTimerRunning` |
| Idle, beam restored, not running | `CMD_PING` every 500 ms |
| Beam held and running | `CMD_GATE1_OPEN` every 50 ms |
| Else if `txTimerRunning` | rewrite counting screen |

Heartbeat **does not** run while `txTimerRunning` is true, even after the beam
is restored. Finish notify is the only way TX leaves that state.

### Blocking points (TX)

- Radio missing: infinite `while(true)` in `setup`
- `waitForPairing`: infinite until `sendPacket` returns true
- Pairing success `delay(RADIO_PAIR_OK_MS)` and boot splash `delay(3000)`
- Finish path: `while (millis() < completeUntil)` ~1500 ms inside `PollTxRadio` — **no gate or encoder service** in that window
- Long-press waits for button release with `while (... == LOW) delay(1)`

If the beam is still broken when the finish hold ends, `gateWasOpen` stays true,
so the next start needs a falling edge then a rising edge.

## 5. RX firmware architecture (modular)

**Entry:** `laser_timer_v2_rx.ino`
**Core:** `functions_laser_timer_v2_rx.h`
**Modules:** `menu_rx`, `display_rx`, `encoder_rx`, `settings_rx`

### `loop()`

```
PollRadio()                      # drain TX→RX packets
tickFirstPairing()               # pairing splash until first valid packet
checkEncoderOpensMenu()          # long-press → menuRequestOpen + waitForEncoderRelease
if !menuIsActive():
     Timer() → Sense_Gate2 + Gate2_Timer_Action + idle line1
else if timer_state == ENABLED:
     Sense_Gate2() only          # sensor live; no live-time LCD updates
handleRxBuzzer()
menuTick()                       # may PollRadio again; encoder nav
toastTick() + restore menu/idle when toast ends
```

`menuIsActive()` is true when the screen is **not** `MS_Idle` and **not**
`MS_Splash`. Opening the menu from idle goes Idle → Main directly (`MS_Splash`
is used after re-pair success only). `showMenuSplash()` is unused.

### Public APIs between modules

**`menu_rx.h`:** `menuInit`, `menuRequestOpen`, `menuIsActive`, `menuRefresh`,
`menuTick`. Screens: `MS_Idle`, `MS_Splash`, `MS_Main`, `MS_Distance`,
`MS_Custom`, `MS_Radio`, `MS_Buzzer`, `MS_RePairing`.

**`display_rx.h`:** `lcdPrintLine`, `lcdPrintMenuHeading`, `lcdPrintMenuItemLine`,
`showToast` / `toastTick` / `toastActive` (`MENU_TOAST_MS` = 300).

**`encoder_rx.h`:** `encoderInit`, `pollEncoder` → `EncNone|CW|CCW|Press|LongPress`,
`waitForEncoderRelease` (blocking). Debounce: step 50 ms, button 50 ms, long-press
800 ms. CW/CCW sampled on CLK rising edge vs DAT. Pins: D3 / D4 / D5.

**`settings_rx.h`:** globals `distance`, `distance_in_yards`, `custom_distance_yards`,
`buzzer_align_enabled`, `buzzer_finish_enabled`, `use_meters`. EEPROM magic `0x52`
at address 0.

### Pairing

**Boot:** pairing screen → radio init → listen on `"00002"`.
`rxAwaitingFirstLink` stays true until **any** packet with `RADIO_MAGIC`; then
success splash for `RADIO_PAIR_OK_MS`, then idle. PING is sufficient but not
required.

**Menu Re-pair (`MS_RePairing`):** shows pairing screen. `tickRePairing()` calls
`PollRadio()` then checks `radio.available()` for a second drain used to show
success. See pitfalls — that auto-exit is effectively unreachable.

### Stopwatch / timer states

Two layers of “enabled”:

| Variable | Type | Meaning |
|----------|------|---------|
| `timer_state` | `char` `ENABLED('E')` / `DISABLED('D')` | Armed for runs (menu **Stopwatch**) |
| `gate2.timer_state` | `char` `ON('I')` / `OFF('O')` | Actively counting this pass |
| `gate2.able_state` | `char` | Written `DISABLED` on finish; **never read** |
| `gate1_opened` | `bool` | Written from radio cmds; **not used** for decisions |

Arm (Main → Stopwatch):

- `timer_state = ENABLED`
- Clears `finishedUntilMs`, seeds `gate2BeamWasBroken`, `clearLine1()`, **`radio.flush_rx()`**, toast `"Starting..."`, exits menu
- Does **not** reset `gate2.timer_state` / `startMillis`

Start count: `PollRadio` on `CMD_GATE1_OPEN` while armed and `gate2.timer_state == OFF`
→ set ON, `startMillis = millis()`. Further `GATE1_OPEN` while already `ON` only
sets `gate1_opened = true`.

Finish: rising edge on gate 2 while counting → `stopTimerAtGate2`:

- Freeze LCD time, `gate2.timer_state = OFF`, optional `printSpeed()`, `sendToTransmitter(CMD_GATE2_CLOSED, true)`, `"Finished"` for 1500 ms, optional 700 ms finish beep
- Then **zeros** `periodMillis` / `startMillis` (LCD already painted)
- **`timer_state` remains `ENABLED`**

Live seconds update only when the menu is inactive (`Gate2_Timer_Action`).

### RX menu map

- **Main:** Stopwatch, Speed, Radio, Buzzer, Back
- **Speed:** Speed ON/OFF; presets 40/10/5/2/1 yd (meters label is `round(yards*0.9144)`); Custom 1–99 yd; Units yards/meters; Back
- Preset rows show an **@5s speed preview** on line 1:
  - yards: tenths = `(yards * 90 + 11) / 22` → `N.N MPH@5s`
  - meters: tenths = `(yards * 329184 + 25000) / 50000` → `N.N KPH@5s`
- Preset toasts always say `"N yd set"` even when units are meters
- **Radio:** Link OK / No link (from `lastRadioRxMs`), Re-pair, Back
- **Buzzer:** Align ON/OFF (450 ms), Finish ON/OFF (700 ms), Back
- Long-press in a menu exits to idle; short press selects; toasts (300 ms) ignore encoder nav

Encoder CLK/DAT have no MCU pull-ups. Adding a menu item: extend the matching
`*MenuItem` enum and `get*ItemLabel` / `action*Select` in `menu_rx.cpp`, bump
the item count used by `lcdPrintMenuItemLine` (`index+1` / `count` must stay
single ASCII digits — current max is Speed’s 9 items).

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

## 6. EEPROM / settings

`SettingsBlock` at EEPROM address 0 (`settings_rx.cpp`). If magic ≠ `0x52`,
compile-time defaults are kept (no write until the user changes a setting):

| Field | Default |
|-------|---------|
| `distance` | `ENABLED` (`'E'`) — print speed on finish |
| `distance_in_yards` | 40 |
| `custom_distance_yards` | 40 |
| `buzzer_align` / `buzzer_finish` | true |
| `use_meters` | false |

`loadSettings()` copies the block when magic matches. There is **no checksum
and no range clamp** — a coincidental `0x52` in blank EEPROM, or a layout
change without a new magic, can load garbage. Custom edit clamps 1–99 yards
at the UI only.

`saveSettings()` on every toggle / preset / custom / units change. Distance is
**always stored in yards**; meters is display-only. Custom confirm writes both
`custom_distance_yards` and `distance_in_yards`. TX has no EEPROM.

`printSpeed()` (finish LCD):

- meters: `(yards * 0.9144 / 1000) / periodMillis * 3600` → `KPH`
- yards: `(yards / periodMillis) * (3600 / 1760)` → `MPH`

`periodMillis` is seconds. There is no divide-by-zero guard if start and finish
share the same `millis()` tick.

## 7. Pitfalls and constraints (source-verified)

### Ack polarity / dynamic ACK

`requireAck` is passed as RF24 `write()`’s third argument (`multicast`): `true`
means **no ACK requested**. `enableDynamicAck()` is never called.

Operational effects:

- TX `waitForPairing()` uses `sendPacket(CMD_PING, true)`. A true return is
  **not** an ACK from the receiver. Confirm link on the RX LCD.
- `sendGateOpenBurst` is named as “no-ack then acked”; the flags are inverted
  relative to that name.
- Idle heartbeat and `CMD_GATE1_CLOSED` use `requireAck=false` (ACK requested,
  1 attempt). Finish uses `requireAck=true` (multicast, 10 attempts).

- `firmware/common/radio_protocol_v2.h` (and both sketch copies), `sendPacketOnPipe`

### `flush_rx` on Stopwatch arm

Arming Stopwatch calls `radio.flush_rx()`, which can drop an in-flight
`CMD_GATE1_OPEN`.

- `menu_rx.cpp` `actionMainSelect` / `MM_Stopwatch`

### `timer_state` stays ENABLED after finish

`stopTimerAtGate2` never clears global `timer_state`. After a finished run the
unit remains armed.

- `functions_laser_timer_v2_rx.h` `stopTimerAtGate2`, `PollRadio`

### Poll radio double-drain (re-pair)

`loop` always calls `PollRadio()` first (drains FIFO). `tickRePairing` calls
`PollRadio()` again, then `if (radio.available())` for the Link-OK UI. Packets
are already consumed; cancel re-pair with a press. Open menus also call
`PollRadio()` again in `menuTick`.

- `laser_timer_v2_rx.ino` `loop`, `menu_rx.cpp` `tickRePairing` / `menuTick`

### A0 vs A7 power enable

Firmware drives **A0** HIGH. PCB maps Nano **A7** to `/3V3_EN`. Nano A7 is
analog-input-only. See `docs/hardware-production.md`.

### `waitForEncoderRelease` blocking

```c
while (digitalRead(encoderButton) == LOW) { delay(1); }
```

Blocks the entire RX loop (no radio/timer service) until release. Used after
menu open, selects, custom confirm, re-pair cancel.

- `encoder_rx.cpp`

### TX finish hold

`PollTxRadio` busy-waits ~1.5 s on `CMD_GATE2_CLOSED`. Gate edges and encoder
input during that window are missed. Heartbeats also pause while
`txTimerRunning` is set (from gate-1 break until finish, including while
waiting at gate 2).

- `functions_laser_timer_v2_tx.h` `PollTxRadio`, `Sense_Gate1`

### Three copies of the protocol header

Identical today. Sketches include the local copy only — edits must be
triplicated or the build must share one file.

### Menu open while armed

When `menuIsActive()` and `timer_state == ENABLED`, `Sense_Gate2` still runs but
`Gate2_Timer_Action` does not — finish can still fire; live seconds do not
update.

- `laser_timer_v2_rx.ino` `loop`

### Other notes

- TX encoder long-press blocks on release; menu stub only
- Macros `OPENED` / `CLOSED` are unused
- RX `D8` is driven high as `sd_cs_pin`; no SD code and no SD part on the BoM
- Align beep only on a **new** beam-break while not counting

## 8. Build / flash assumptions

No PlatformIO, Makefile, or library version pins exist in-tree.

| Item | Assumption |
|------|------------|
| Board | ATmega328P (Arduino Nano v3.x on the shared PCB) |
| IDE | Arduino IDE; open the sketch **folder** as the project root |
| Libraries | `RF24` (TMRh20), `SPI`, `Wire`, `LiquidCrystal_I2C`, `EEPROM` (RX) |
| LCD | I2C `0x27`, 16×2; constructed in each `.ino` |
| Radio | nRF24L01+PA+LNA, CE=9, CSN=10, channel 108, 250 kbps, CRC-16, 2-byte payload |
| Gate polarity | Beam broken = `HIGH` on D2 |

Buzzer on `D1` shares the Nano hardware UART TX line — disconnect or expect
flaky USB uploads while the buzzer is loaded.

## 9. Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| `Radio not found` | `radio.begin()` failed: wiring, CE/CSN 9/10, or 3.3 V radio power (`docs/hardware-production.md`). |
| TX stuck on `Pairing...` | Write helper never returns true. Power RX first; check channel 108 and antennas. TX “success” is not an RX ACK. |
| RX stays on pairing | No packet with `magic==0xA5` received. Confirm TX is transmitting on `"00002"`. |
| RX `No TX signal` | No valid packet within 2 s; TX heartbeats only when idle **and** beam restored. |
| RX `Align laser` | Gate 2 pin is `HIGH` while not counting; realign or check polarity. |
| Timing never starts | Arm **Stopwatch** before breaking gate 1. Arming also `flush_rx()`. |
| Clock does not restart mid-run | Expected: extra `GATE1_OPEN` while `gate2.timer_state == ON` is ignored for timing. |
| Speed line on first finish without menu | Expected: Speed defaults ON at 40 yd. |
| No speed line | Enable Speed and set distance; `printSpeed` runs only when `distance == ENABLED`. |
| Flaky upload / serial | Buzzer shares `D1` / Nano TX. |
| Re-pair never auto-exits | Packet drain before `tickRePairing` sees `radio.available()`; leave with a press. |
| Second run starts without menu | Expected: `timer_state` stays `ENABLED`. |
| TX never shows complete | RX must send `CMD_GATE2_CLOSED` on `"00001"`; TX only handles it while `txTimerRunning`. |
| Next TX start ignored | Beam still broken after the 1.5 s complete screen, or falling edge missed during that block. |

## 10. Change checklist

When changing the radio contract or pin map:

1. Edit all three `radio_protocol_v2.h` copies in the same commit (or switch sketches to a single include).
2. Keep TX write / RX listen addresses matched (`"00002"` primary, `"00001"` return).
3. Re-test: power RX first, pairing screens on both units, Stopwatch arm, gate-1 start, gate-2 finish, TX complete UI, and a second run without re-arming.
4. If GPIO changes, update both sketches and `docs/hardware-production.md`.
5. If `SettingsBlock` layout changes, bump magic off `0x52` and add a migration — load does not version fields.

Hardware pin map and fab artifacts: [`hardware-production.md`](hardware-production.md).
