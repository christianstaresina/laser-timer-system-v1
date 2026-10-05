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

`functions_laser_timer_v2_rx.h` and `functions_laser_timer_v2_tx.h` **define**
functions and globals. The include guard does not make a second include safe.
Only the `.ino` includes them. Another `.cpp` should `extern`-declare symbols
the way `menu_rx.cpp` does.

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

`CMD_GATE1_CLOSED` only clears `gate1_opened` on RX. It does not stop or restart
the clock. RX never sends or handles `CMD_GATE2_CLOSED` locally.

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
`setRetries(5, 15)` → CRC-16 → fixed payload → PA high. In RF24 those retry
arguments are **delay in 250 µs steps**, then **retry count**: 5 × 250 µs =
1250 µs between attempts, and 15 retries. That is not “5 retries of 15 ms”.
Does **not** call `enableDynamicAck()`. RF24 `IRQ` is not wired on the PCB.

`sendGateOpenBurst` runs synchronously inside `Sense_Gate1` (four writes). The
trailing write loops up to 10 times with `delay(2)` between failures, and each
`radio.write` waits out the retry window, so a start edge stalls the TX loop
for the whole burst.

`sendPacketOnPipe(..., requireAck)`:

- `stopListening`, `openWritingPipe`, `flush_tx`
- Attempts: 10 if `requireAck`, else 1
- `radio.write(&pkt, sizeof(pkt), requireAck)` — the third RF24 argument is
  **`multicast`**, not “please ACK” (see pitfalls)

`sendGateOpenBurst`: three `CMD_GATE1_OPEN` with `requireAck=false`, then one
with `requireAck=true`. Combined with the multicast polarity, that is **not**
“three no-ACK then one ACK”.

## 3. Operating runbook (LCD + packets)

Power **RX first**, then TX. LCD **row 0 is the top line**, row 1 is the bottom
line. Idle `Ready` / `No TX signal` / `Align laser` print on the **bottom** row;
live time (`N.NNs`) prints on the **top** row.

Typical screens:

| Step | TX LCD | RX LCD | Radio |
|------|--------|--------|-------|
| Boot splash 3 s | `LASER TIMER TX` / `Created by CS` | `LASER TIMER RX` / `Created by CS` | none |
| Pairing | `Pairing...` / `Waiting for link` | same | TX loops `CMD_PING` every 100 ms |
| Link | `Link OK!` / `Connected` (1.5 s) | same, then idle | RX accepts **any** `magic==0xA5` packet |
| Idle | `Pass laser` art, or `Gate 1 crossed` / `Align laser` if D2 is already HIGH | bottom `Ready` (or `No TX signal` / `Align laser`) | TX `CMD_PING` every 500 ms **only if** beam restored and not running |
| Arm Stopwatch | unchanged | toast `Starting...` then idle | RX `flush_rx()` (can drop an in-flight open) |
| Gate 1 break | `Timer counting` / `Waiting gate 2` | live `N.NNs` on row 0 | `sendGateOpenBurst`; repeats `GATE1_OPEN` every 50 ms while held |
| Gate 1 restore (run still live) | still counting | still counting | single `CMD_GATE1_CLOSED` |
| Gate 1 re-break during count | another burst + counting screen | clock **does not** restart (`gate2.timer_state` already `ON`) | extra `GATE1_OPEN` packets |
| Gate 2 break | after RX notify: `Timer complete!` (~1.5 s block) then idle art | frozen time, `Finished` 1.5 s, optional `x.xMPH`/`KPH` | `CMD_GATE2_CLOSED` |
| Next run | wait for a new gate-1 rising edge | **already armed** (`timer_state` stays `ENABLED`) | next `GATE1_OPEN` starts another count |
| RX power cycle | — | pairing, then idle **disarmed** | Speed/distance/buzzer reload from EEPROM; Stopwatch must be selected again |

Arming is **receiver-only** (Main → Stopwatch) and **RAM-only**. TX has no EEPROM
and no menu implementation (long-press is reserved and currently a no-op after
release). TX encoder CLK/DAT are `pinMode(INPUT)` but never polled.

The RX encoder long-press is accepted **before the first radio packet**
(`tickFirstPairing` returns early when `menuIsActive()` or `toastActive()`).
`PollRadio()` still completes pairing in the background; leaving the menu then
shows idle `Ready` if a valid packet arrived.

Speed on the finish LCD is **on by default** (`distance == ENABLED`, 40 yards)
until the Speed menu turns it off. Choosing a preset or custom distance does
**not** turn Speed back on if it was OFF.

Neither sketch calls `Serial`. D1 is the buzzer (`tone` at 2000 Hz) and D0 is
only the J9 expansion pad, so there is no debug log. Gate sensors are
`pinMode(INPUT)` with **no pull-up and no debounce** — a floating or glitchy
D2 is an immediate edge.

The align beep (450 ms) runs only while the receiver is **armed**. Disarmed,
`Sense_Gate2` is not called; `restoreIdleDisplay` can still print `Align laser`
from a raw pin read, with no beep. Arming while the beam is already broken
samples `gate2BeamWasBroken` and does not beep.

## 4. TX firmware architecture

**Entry:** `laser_timer_v2_tx.ino`
**Logic:** `src/functions_laser_timer_v2_tx.h`

### Pins / objects

| Resource | Pin / value |
|----------|-------------|
| Gate 1 sensor | D2 (`GATE_ACTIVATED == HIGH`) |
| Buzzer | D1, 2000 Hz; 450 ms on start, 700 ms on finish |
| Encoder button / CLK / DAT | D3 (`INPUT_PULLUP`) / D4 / D5 (`INPUT`, no pull-up) |
| nRF24 CE / CSN | D9 / D10 (`RF24 radio(9, 10)`) |
| nRF24 SPI | Nano hardware SPI: D11 MOSI, D12 MISO, D13 SCK |
| Radio power pin (firmware) | **A0** driven HIGH |
| LCD | I2C `0x27`, 16×2 |

### `setup()`

1. Gate / buzzer / `encoderInitTx`
2. `A0` HIGH + `RADIO_POWER_SETTLE_MS`
3. LCD init, custom chars, splash `"LASER TIMER TX"` (3 s)
4. Pairing screen → SPI → `initRadio` (halt forever on failure: `"Radio not found"`)
5. `openWritingPipe(addresses[RADIO_TX_SEND_PIPE])`
6. **`waitForPairing()`** — blocking: `sendPacket(CMD_PING, true)` every 100 ms until the write helper returns true
7. Seed `gateWasOpen` from the sensor; show gate-open (`Gate 1 crossed` / `Align laser`) or idle laser art

Unlike RX, TX does not `pinMode` CSN / D8 before `initRadio`. Encoder polling
starts only in `loop()`, so the TX button is dead until pairing returns.

### `loop()`

```
checkEncoderOpensMenu();   // long-press blocks until button release
Sense_Gate1();             // always PollTxRadio() first when radioReady
```

### `Sense_Gate1` branches

| Condition | Action |
|-----------|--------|
| Rising edge (beam breaks) | `sendGateOpenBurst`, `txTimerRunning = true`, counting screen, start beep |
| Falling edge (beam restores) | single `CMD_GATE1_CLOSED` (`requireAck=false`); idle art only if `!txTimerRunning` |
| Idle, beam restored, not running | `CMD_PING` every 500 ms |
| Beam held and running | `CMD_GATE1_OPEN` every 50 ms |
| Else if `txTimerRunning` | rewrite counting screen |
| Beam held and **not** running | no heartbeat, no burst (typical after pairing with D2 already HIGH) |

Heartbeat **does not** run while `txTimerRunning` is true, even after the beam
is restored. Finish notify is the only way TX leaves that state.

### Blocking points (TX)

- Radio missing: infinite `while(true)` in `setup`
- `waitForPairing`: infinite until `sendPacket` returns true
- Pairing success `delay(RADIO_PAIR_OK_MS)` and boot splash `delay(3000)`
- Finish path: `while (millis() < completeUntil)` ~1500 ms inside `PollTxRadio` — **no gate or encoder service** in that window
- Long-press waits for button release with `while (... == LOW) delay(1)` **before** `Sense_Gate1` in the same loop — gate edges during that hold are missed

If the beam is still broken when the finish hold ends, `gateWasOpen` stays true
and `printLaserImage()` still runs, so the LCD shows `Pass laser` / `when ready`
while D2 is HIGH. The next start needs a falling edge, then a rising edge.

Idle art is those literal strings: row 0 is `Pass laser  ` plus custom glyphs
0–3 (laser left / mid / right + antenna); row 1 is `when ready  ` plus glyphs
4–6 (tripod). Glyph 7 (`backArrow`) is created on both sketches and never
printed. The RX sketch creates glyphs 0–7 and never prints them.

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

`Sense_Gate2` runs only when `timer_state == ENABLED` (from `Timer()`, or alone
while the menu is active). Disarmed idle still refreshes row 1 through
`refreshLine1WhenIdle()` → `restoreIdleDisplay()`.

Menu open is **deferred**: `checkEncoderOpensMenu` only sets `menuOpenRequested`
and blocks in `waitForEncoderRelease()`. `menuTick()` later (same iteration)
enters Main. Until then `menuIsActive()` is still false, so one `Timer()` pass
can run after the long-press.

### Public APIs between modules

**`menu_rx.h`:** `menuInit`, `menuRequestOpen`, `menuIsActive`, `menuRefresh`,
`menuTick`. Screens: `MS_Idle`, `MS_Splash`, `MS_Main`, `MS_Distance`,
`MS_Custom`, `MS_Radio`, `MS_Buzzer`, `MS_RePairing`.

**`display_rx.h`:** `lcdPrintLine`, `lcdPrintMenuHeading`, `lcdPrintMenuItemLine`,
`showMenuSplash` (unused), `showToast`, `toastTick`, `toastActive`.
`LCD_COLS` 16, `MENU_TOAST_MS` 300.

**`encoder_rx.h`:** `encoderInit`, `pollEncoder`, `waitForEncoderRelease`.
Events: `EncNone`, `EncCW`, `EncCCW`, `EncPress`, `EncLongPress`.
Debounce: step 50 ms, button 50 ms, long-press 800 ms.
On a rising CLK edge, `DAT != CLK` → `EncCW`, else `EncCCW`.
`pollEncoder()` returns one event; a button press in the same poll overwrites
rotation.

**`settings_rx.h`:** `loadSettings`, `saveSettings`, globals `distance`,
`distance_in_yards`, `custom_distance_yards`,
`buzzer_align_enabled`, `buzzer_finish_enabled`, `use_meters`. EEPROM magic `0x52`
at address 0.

### Pairing

**Boot:** pairing screen → radio init → listen on `"00002"`.
RX `pinMode`s CSN (`D10`) and `D8` HIGH before `initRadio`.
`rxAwaitingFirstLink` stays true until **any** packet with `RADIO_MAGIC`; then
success splash for `RADIO_PAIR_OK_MS`, then idle. PING is sufficient but not
required. First-link success returns to **idle**, not the menu.

**Menu Re-pair (`MS_RePairing`):** shows pairing screen. `tickRePairing()` calls
`PollRadio()` then checks `radio.available()` for a second drain used to show
success. See pitfalls — that auto-exit is effectively unreachable. If a packet
were seen here, splash would then enter **Main** (`splashNextScreen = MS_Main`),
not idle. Cancel re-pair with a press.

### Stopwatch / timer states

Two layers of “enabled”:

| Variable | Type | Meaning |
|----------|------|---------|
| `timer_state` | `char` `ENABLED('E')` / `DISABLED('D')` | Armed for runs (menu **Stopwatch**). RAM only — not in EEPROM. |
| `gate2.timer_state` | `char` `ON('I')` / `OFF('O')` | Actively counting this pass |
| `gate2.able_state` | `char` | Written `DISABLED` on finish; **never read** |
| `gate1_opened` | `bool` | Written from radio cmds; **not used** for decisions |

Arm (Main → Stopwatch):

- `timer_state = ENABLED`
- Clears `finishedUntilMs`, seeds `gate2BeamWasBroken`, `clearLine1()`, **`radio.flush_rx()`**, toast `"Starting..."`, exits menu
- Does **not** reset `gate2.timer_state` / `startMillis`

Selecting Stopwatch again while a run is already counting therefore leaves the
clock running (and can drop a queued start packet via `flush_rx()`).

Start count: `PollRadio` on `CMD_GATE1_OPEN` while armed and `gate2.timer_state == OFF`
→ set ON, `startMillis = millis()`, and **sample** `gate2BeamWasBroken` from D2.
Further `GATE1_OPEN` while already `ON` only sets `gate1_opened = true`.

Finish is a **rising edge** on D2 (`beamBroken && !gate2BeamWasBroken`) while
counting. If the finish beam is already broken when the start packet arrives,
the clock starts but `stopTimerAtGate2` waits for restore then a new break.

Finish path (`stopTimerAtGate2`):

- Freeze LCD time on row 0 (`N.NNs` from `print(periodMillis, 2)` plus the
  16-character pad `"s               "`), `gate2.timer_state = OFF`, optional
  `printSpeed()` overlay on the **same** row (cursor column 9 if speed < 10,
  else 8). Column 8 for 16.4 MPH yields `5.00s   16.4MPH` (three spaces). The
  pad is wider than the columns left on row 0, so it wraps onto row 1 by the
  width of the number (`5.00` wraps 4 characters). Live counting assumes row 1
  stays blank; `Finished` is written over that wrap.
- `sendToTransmitter(CMD_GATE2_CLOSED, true)`, then `resumeRxListening()`
  (reading pipe 0 on `"00002"`, `startListening()`), `"Finished"` for 1500 ms
  on row 1, optional 700 ms finish beep
- Then **zeros** `periodMillis` / `startMillis` (LCD already painted)
- **`timer_state` remains `ENABLED`**

While the start beam is still broken, TX keeps sending `CMD_GATE1_OPEN` every
50 ms until it has read `CMD_GATE2_CLOSED` and cleared `txTimerRunning`. Any of
those packets that arrive after the finish is a **new start**: `gate2.timer_state`
is already `OFF` and the receiver is still armed.

If the finish beam is still occupied when `finishedUntilMs` expires,
`restoreIdleDisplay()` switches row 1 from `Finished` to `Align laser`.

Live seconds update only when the menu is inactive (`Gate2_Timer_Action`).

Align beep (450 ms) fires only on a **new** D2 rising edge while **armed** and
**not** counting. Disarmed, the same pin can still show `Align laser` with no
beep.

### RX menu map

- **Main:** Stopwatch, Speed, Radio, Buzzer, Back. Opens at index 0 (Stopwatch).
  Encoder does **not** wrap — `clampIndex` stops at the first and last items.
  Long-press from Main, Speed, Radio, Buzzer, or Custom exits to **idle**, not
  the parent screen. On the re-pair screen, long-press matches a short press:
  cancel back to Radio on the Re-pair row. It does **not** go idle. **Back**
  on Speed/Radio/Buzzer returns to Main on that parent item.
- **Speed:** Speed ON/OFF; presets 40/10/5/2/1 yd (meters label is `round(yards*0.9144)`); Custom 1–99 yd; Units yards/meters; Back
- Preset rows do **not** use the `Speed Settings` heading. Row 0 is the
  distance label (`40 yd *` or `37m *`); row 1 is the `@5s` preview. Speed
  ON/OFF, Custom, Units, and Back keep the heading plus the `N/9` item line.
- Current distance is marked with a trailing `*` wherever `distance_in_yards`
  equals that row’s yard value. Preset 40 and Custom both show `*` when both
  stored values are 40.
- Preset rows show an **@5s speed preview** on the bottom row (`N/9 …`):
  - yards: tenths = `(yards * 90 + 11) / 22` → `N.N MPH@5s` (40 yd → `16.4 MPH@5s`)
  - meters: tenths = `(yards * 329184 + 25000) / 50000` → `N.N KPH@5s` (40 yd → `26.3 KPH@5s`)
- Preset `showToast` strings are always `"N yd set"` (second line empty), even
  in meters mode. Re-selecting the same distance uses that same first line plus
  `"unchanged"`. Both are painted over immediately; see the toast note below.
- Setting a preset or custom value does **not** set `distance = ENABLED`. If Speed
  is OFF, finish still skips `printSpeed()`.
- **Custom:** top `Set: N yd` or `Set: N m`; bottom `7/9 turn|press`. Encoder still steps **1 yard** (clamped 1–99) when the label shows meters — the meter text is `round(yards * 0.9144)`. Press saves both `custom_distance_yards` and `distance_in_yards` (does not enable Speed).
- **Radio:** Link OK / No link (from `lastRadioRxMs`), Re-pair, Back. Pressing
  Link OK / No link only re-renders.
- **Buzzer:** Align ON/OFF (450 ms), Finish ON/OFF (700 ms), Back
- Short press selects. `showToast` runs for Speed, presets (`"N yd set"` /
  `"unchanged"`), units, buzzer, and custom (`"Custom set"`), then the same
  handler calls `renderCurrentMenu()` or `enterScreen()`, so that text is
  overwritten before `loop()` returns. **"Starting..."** stays visible because
  Stopwatch leaves the menu and does not redraw. `toastActive()` is still true
  for `MENU_TOAST_MS` (300), and `handleEncoderNav` ignores encoder events for
  that window even when the toast is not on screen. Idle long-press
  (`checkEncoderOpensMenu`) does not consult `toastActive()`.
- `menuIsActive()` is false for `MS_Splash`, so `Timer()` can still run during the re-pair success splash.

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
| nRF24 CE / CSN | D9 / D10 (`radio_cs_pin` also 10; held HIGH before `initRadio`) |
| nRF24 SPI | D11 MOSI, D12 MISO, D13 SCK |
| SD CS (held high only) | D8 — **no SD feature** |
| Radio power (firmware) | **A0** HIGH |
| LCD | I2C `0x27` |

## 6. EEPROM / settings

`SettingsBlock` at EEPROM address 0 (`settings_rx.cpp`), written with
`EEPROM.put` / read with `EEPROM.get`. Field order:

`uint8 magic` (`0x52`), `char distance`, `int16 distance_in_yards`,
`int16 custom_distance_yards`, `uint8 buzzer_align`, `uint8 buzzer_finish`,
`uint8 use_meters`.

`SettingsBlock` is **not** packed (`RadioPacket` is). With `int16_t` 2-byte
aligned, the fields sit at offsets 0, 1, 2, 4, 6, 7, 8 and the struct is padded
to **10 bytes**. `EEPROM.put` writes the tail byte. `saveSettings()` does not
clear it, so the byte can be stack garbage. Load ignores it. A layout change
needs a new magic — do not treat an old image as 9 bytes.

`timer_state` is **not** in this block. After a power cycle the receiver is
disarmed (`DISABLED`) until Stopwatch is selected again.

If magic ≠ `0x52`, compile-time defaults are kept (no write until the user
changes a setting):

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

`printSpeed()` (finish LCD) uses `periodMillis` in **seconds** (despite the name):

- meters: `(yards * 0.9144 / 1000) / periodMillis * 3600` → `KPH`
- yards: `(yards / periodMillis) * (3600 / 1760)` → `MPH`

Example: 40 yd in 5.00 s → `16.4 MPH` (or `26.3 KPH` with units meters). Cursor
column is 9 if speed < 10, else 8. There is no divide-by-zero guard if start
and finish share the same `millis()` tick.

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
`CMD_GATE1_OPEN`. Re-selecting Stopwatch while already counting does not reset
`gate2.timer_state`.

- `menu_rx.cpp` `actionMainSelect` / `MM_Stopwatch`

### `timer_state` stays ENABLED after finish — and is lost on reboot

`stopTimerAtGate2` never clears global `timer_state`. After a finished run the
unit remains armed until power-cycle, which reloads compile-time/`EEPROM`
settings and leaves `timer_state` at `DISABLED`.

- `functions_laser_timer_v2_rx.h` `stopTimerAtGate2`, `PollRadio`
- `settings_rx.cpp` (no `timer_state` field)

### Poll radio double-drain (re-pair)

`loop` always calls `PollRadio()` first (drains FIFO). `tickRePairing` calls
`PollRadio()` again, then `if (radio.available())` for the Link-OK UI. Packets
are already consumed; cancel re-pair with a press. Open menus also call
`PollRadio()` again in `menuTick`.

- `laser_timer_v2_rx.ino` `loop`, `menu_rx.cpp` `tickRePairing` / `menuTick`

### A0 vs A7 power enable

Firmware drives **A0** HIGH. PCB maps Nano **A7** to `/3V3_EN`. Nano A7 is
analog-input-only. Board `+5V`/`+3V3` are not the Nano 5 V / 3.3 V pins. See
`docs/hardware-production.md`.

### `waitForEncoderRelease` blocking

```c
while (digitalRead(encoderButton) == LOW) { delay(1); }
```

Blocks the entire RX loop (no radio/timer service) until release. Used after
menu open, selects, custom confirm, re-pair cancel. TX uses the same pattern
in `checkEncoderOpensMenu` **before** `Sense_Gate1`.

- `encoder_rx.cpp`, `functions_laser_timer_v2_tx.h`

### Encoder event overwrite

`pollEncoder()` can emit only one event per call. If CLK steps and the button
fires in the same poll, the button (`EncPress` / `EncLongPress`) overwrites the
rotation.

- `encoder_rx.cpp` `pollEncoder`

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
update. `Sense_Gate2` and `stopTimerAtGate2` write the LCD directly, so
`Align laser` or the finish screen paints over the menu until the next encoder
event calls `renderCurrentMenu()`. Menu state itself stays on the open screen.

- `laser_timer_v2_rx.ino` `loop`, `functions_laser_timer_v2_rx.h` `Sense_Gate2`

### Confirmation toasts are redrawn immediately

Every select path except Stopwatch calls `showToast` and then
`renderCurrentMenu()` or `enterScreen()` in the same function. The strings
(`Speed ON`, `40 yd set`, `unchanged`, `Units: meters`, `Align beep ON`,
`Custom set`, and the matching OFF strings) do not remain on the LCD.
`toastActive()` still blocks in-menu encoder handling for 300 ms.
`"Starting..."` is the toast that stays up.

- `menu_rx.cpp` `actionDistanceSelect`, `actionBuzzerSelect`, `handleEncoderNav`

### Re-pair long-press returns to Radio

`MS_RePairing` treats `EncLongPress` like `EncPress`: `enterScreen(MS_Radio, RM_RePair)`.
Other menu screens use long-press to exit to idle. This screen does not.

- `menu_rx.cpp` `menuTick`

### Finish while the start beam is still broken

RX will accept a new `CMD_GATE1_OPEN` as soon as `gate2.timer_state` is `OFF`.
TX emits that command every 50 ms while D2 is HIGH and `txTimerRunning` is
still set. `txTimerRunning` clears only inside `PollTxRadio` after
`CMD_GATE2_CLOSED` is read. A finish with gate 1 still occupied can therefore
start another RX count immediately, and TX can stay on `Timer counting` until
a later finish packet lands. After TX does accept the finish, it shows
`Pass laser` even if D2 is still HIGH.

- `functions_laser_timer_v2_tx.h` `Sense_Gate1`, `PollTxRadio`
- `functions_laser_timer_v2_rx.h` `stopTimerAtGate2`, `PollRadio`

### Floating gate input

Both sketches use `pinMode(gate_pin, INPUT)` and a raw `digitalRead`. There is
no pull-up and no debounce. An open J5 harness or a noisy beam module is a
start or finish edge.

- `laser_timer_v2_rx.ino` `setup`, `laser_timer_v2_tx.ino` `setup`

### Gate 2 already broken at start

On `CMD_GATE1_OPEN`, RX samples `gate2BeamWasBroken` from D2. Finish requires a
later rising edge. A misaligned or occupied finish gate starts the clock and
then appears “stuck running” until the beam restores and breaks again.

- `functions_laser_timer_v2_rx.h` `PollRadio` / `Sense_Gate2`

### Other notes

- TX encoder long-press blocks on release; rotation is unused; menu stub only
- Macros `OPENED` / `CLOSED` are unused
- RX `D8` is driven high as `sd_cs_pin`; no SD code and no SD part on the BoM
- Align beep only on a **new** beam-break while not counting
- Custom distance encoder always steps yards, even when the LCD shows meters
- Speed ON/OFF is independent of the stored yard value

## 8. Build / flash assumptions

No PlatformIO, Makefile, or library version pins exist in-tree.

| Item | Assumption |
|------|------------|
| Board | ATmega328P (Arduino Nano v3.x on the shared PCB) |
| IDE | Arduino IDE; open the sketch **folder** as the project root |
| Libraries | `RF24` (TMRh20), `SPI`, `Wire`, `LiquidCrystal_I2C`, `EEPROM` (RX) |
| Debug | No `Serial` output. Status is the LCD text in the runbook. |
| LCD | I2C `0x27`, 16×2; constructed in each `.ino` |
| Radio | nRF24L01+PA+LNA, CE=9, CSN=10, SPI D11/D12/D13, channel 108, 250 kbps, CRC-16, 2-byte payload |
| Gate polarity | Beam broken = `HIGH` on D2 |

Buzzer on `D1` shares the Nano hardware UART TX line — disconnect or expect
flaky USB uploads while the buzzer is loaded.

## 9. Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| `Radio not found` | `radio.begin()` failed: wiring, CE/CSN 9/10, SPI D11–D13, or 3.3 V radio power (`docs/hardware-production.md`). Board `+3V3` is U2, not the Nano 3.3 V pin. Probe TP3 (`+3V3`) against TP4 (GND). |
| No serial output | Expected. v2 never calls `Serial`. D1 is the buzzer. |
| TX stuck on `Pairing...` | Write helper never returns true. Power RX first; check channel 108 and antennas. TX “success” is not an RX ACK. |
| RX stays on pairing | No packet with `magic==0xA5` received. Confirm TX is transmitting on `"00002"`. |
| RX `No TX signal` | No valid packet within 2 s; TX heartbeats only when idle **and** beam restored. Occupied start beam after pairing also suppresses pings. |
| RX `Align laser` | Gate 2 pin is `HIGH` while not counting; realign or check polarity. After `Finished`, this is expected if the beam is still occupied. |
| `Align laser` but no beep | Receiver is disarmed (`Sense_Gate2` is not running), or the beam was already broken when Stopwatch was armed, so it was not a new edge. |
| TX `Gate 1 crossed` / `Align laser` while idle | Start beam is occupied (`gateWasOpen` true); restore then re-break to start. |
| LCD blank / garbage | Backpack not `0x27`, or J3 harness not `+5V / SCL / SDA / GND`. |
| Encoder turns ignored | CLK/DAT have no MCU pull-ups; J4 is `+5V / SW / CLK / DAT / GND`. A press in the same poll overwrites a step. Menu does not wrap past the last item. |
| Timing never starts | Arm **Stopwatch** before breaking gate 1. Arming also `flush_rx()`. After any RX reboot, re-arm — EEPROM does not store armed state. |
| Clock starts but never finishes | Finish needs a **new** D2 rising edge while counting. If the beam was already broken at `GATE1_OPEN`, restore then re-break. |
| Clock does not restart mid-run | Expected: extra `GATE1_OPEN` while `gate2.timer_state == ON` is ignored for timing. Same if Stopwatch is selected again during a live run. |
| Speed line on first finish without menu | Expected: Speed defaults ON at 40 yd. |
| Distance set but no speed on finish | Speed toggle is independent; turn Speed ON. Custom confirm does not enable it. |
| Custom meters steps oddly | Encoder still increments **yards**; the meter label is rounded. |
| Menu works before `Link OK` | Expected: long-press is allowed before the first packet. |
| No speed line | Enable Speed and set distance; `printSpeed` runs only when `distance == ENABLED`. |
| Flaky upload / serial | Buzzer shares `D1` / Nano TX. |
| Re-pair never auto-exits | Packet drain before `tickRePairing` sees `radio.available()`; leave with a press. |
| Second run starts without menu | Expected: `timer_state` stays `ENABLED` until power-cycle. |
| Clock restarts at the finish line | Start beam still broken. TX repeats `GATE1_OPEN` every 50 ms until it reads `CMD_GATE2_CLOSED`, and RX is still armed. |
| TX shows `Pass laser` while the start beam is broken | Expected immediately after the 1.5 s complete screen. `printLaserImage()` does not read D2. Restore the beam, then break it again. |
| Random starts or finishes | D2 is a floating input with no debounce. Check the J5 harness and that the sensor idles LOW. |
| Setting change shows no toast, knob dead briefly | Expected. Select handlers redraw the menu over the toast; in-menu encoder input is ignored for 300 ms. Only `Starting...` stays on screen. |
| Re-pair long-press returns to Radio | Expected. On that screen long-press cancels like a short press. It does not exit to idle. |
| Menu row replaced by `Align laser` or `Finished` | Menu was open while armed. `Sense_Gate2` paints the LCD; the next encoder event redraws the menu. The finish still counts. |
| TX never shows complete | RX must send `CMD_GATE2_CLOSED` on `"00001"`; TX only handles it while `txTimerRunning`. |
| Next TX start ignored | Beam still broken after the 1.5 s complete screen, or falling edge missed during that block or a TX encoder long-press. |

## 10. Change checklist

When changing the radio contract or pin map:

1. Edit all three `radio_protocol_v2.h` copies in the same commit (or switch sketches to a single include).
2. Keep TX write / RX listen addresses matched (`"00002"` primary, `"00001"` return).
3. Re-test: power RX first, pairing screens on both units, menu-before-link, Stopwatch arm, gate-1 start, gate-2 finish (including finish-beam-already-broken and start-beam-still-broken), TX complete UI (idle art while D2 still HIGH), a second run without re-arming, and a receiver power-cycle (must re-arm).
4. If GPIO or harness pinouts change, update both sketches and `docs/hardware-production.md`.
5. If `SettingsBlock` layout changes, bump magic off `0x52` and add a migration — load does not version fields. Adding persistent arming requires a new field; today `timer_state` is RAM-only.

Hardware pin map and fab artifacts: [`hardware-production.md`](hardware-production.md).
