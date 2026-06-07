/**
 * functions_laser_timer_v2_rx.h
 * Laser Timer Firmware v2 — receiver gate 2, timer, and radio logic
 *
 * Christian Staresina
 * 5/31/2026
 *
 * Gate 2 (finish) stops the timer and notifies the transmitter via
 * CMD_GATE2_CLOSED on the return RF pipe.
 */

#ifndef FUNCTIONS_LASER_TIMER_V2_RX_H
#define FUNCTIONS_LASER_TIMER_V2_RX_H

#include "macros_laser_timer_v2_rx.h"
#include "radio_protocol_v2.h"
#include "settings_rx.h"
#include "menu_rx.h"
#include "encoder_rx.h"
#include "display_rx.h"
#include <SPI.h>
#include <RF24.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
extern LiquidCrystal_I2C lcd;

struct Gate {
  char timer_state;
  char able_state;
};

float avg_speed = 0;

extern const byte gate2_pin = 2;
extern const byte buzzer_pin = 1;
extern const byte radio_cs_pin = 10;
extern const byte sd_cs_pin = 8;

Gate gate2 = {OFF, DISABLED};
bool gate1_opened = false;

extern char timer_state = DISABLED;
unsigned long startMillis = 0;
unsigned long lastDisplayMs = 0;
extern float periodMillis = 0;

extern RF24 radio(9, 10);
extern const byte addresses[][6] = {"00001", "00002"};

bool radioReady = false;
bool rxAwaitingFirstLink = true;
unsigned long lastRadioRxMs = 0;
unsigned long pairingSuccessUntilMs = 0;

bool gate2BeamWasBroken = false;
unsigned long finishedUntilMs = 0;
unsigned long buzzerUntilMs = 0;

#define RX_FINISHED_DISPLAY_MS 1500
#define RX_BUZZER_ALIGN_MS 450
#define RX_BUZZER_FINISH_MS 700
#define RX_BUZZER_HZ 2000

void clearLine1();
void startRxBuzzer(unsigned long durationMs);
void handleRxBuzzer();
void refreshLine1WhenIdle();
void restoreIdleDisplay();
void showPairingScreen();
void showPairingSuccess();
void tickFirstPairing();
void resumeRxListening();
void PollRadio();
void updateLinkDisplay();
void Gate2_Timer_Action();
void Sense_Gate2();
void Timer(char timer_state);
void printSpeed();
void checkEncoderOpensMenu();

void showPairingScreen() {
  lcd.setCursor(0, 0);
  lcd.print(RADIO_PAIR_LINE0);
  lcd.setCursor(0, 1);
  lcd.print(RADIO_PAIR_LINE1);
}

void showPairingSuccess() {
  lcd.setCursor(0, 0);
  lcd.print(RADIO_PAIR_OK0);
  lcd.setCursor(0, 1);
  lcd.print(RADIO_PAIR_OK1);
}

void tickFirstPairing() {
  if (pairingSuccessUntilMs != 0) {
    if (millis() >= pairingSuccessUntilMs) {
      pairingSuccessUntilMs = 0;
      if (!menuIsActive()) {
        restoreIdleDisplay();
      }
    }
    return;
  }

  if (!rxAwaitingFirstLink || menuIsActive() || toastActive()) {
    return;
  }

  showPairingScreen();
}

void resumeRxListening() {
  radio.openReadingPipe(0, addresses[RADIO_TX_SEND_PIPE]);
  radio.startListening();
}

void checkEncoderOpensMenu() {
  if (menuIsActive()) {
    return;
  }
  if (pollEncoder() == EncLongPress) {
    menuRequestOpen();
    waitForEncoderRelease();
  }
}

void Timer(char timer_state) {
  if (timer_state != ENABLED || menuIsActive()) {
    return;
  }
  Sense_Gate2();
  Gate2_Timer_Action();
  refreshLine1WhenIdle();
}

void PollRadio() {
  if (!radioReady) {
    return;
  }

  while (radio.available()) {
    RadioPacket pkt;
    radio.read(&pkt, sizeof(pkt));

    if (pkt.magic != RADIO_MAGIC) {
      continue;
    }

    lastRadioRxMs = millis();

    if (rxAwaitingFirstLink) {
      rxAwaitingFirstLink = false;
      if (!menuIsActive()) {
        showPairingSuccess();
        pairingSuccessUntilMs = millis() + RADIO_PAIR_OK_MS;
      }
    }

    switch (pkt.cmd) {
      case CMD_GATE1_OPEN:
        if (timer_state != ENABLED) {
          break;
        }
        gate1_opened = true;
        if (gate2.timer_state == OFF) {
          gate2.timer_state = ON;
          startMillis = millis();
          lastDisplayMs = 0;
          finishedUntilMs = 0;
          gate2BeamWasBroken = (digitalRead(gate2_pin) == GATE_ACTIVATED);
          clearLine1();
        }
        break;
      case CMD_GATE1_CLOSED:
        if (timer_state != ENABLED) {
          break;
        }
        gate1_opened = false;
        break;
      case CMD_PING:
        break;
      default:
        break;
    }
  }
}

void restoreIdleDisplay() {
  if (menuIsActive()) {
    return;
  }
  if (rxAwaitingFirstLink) {
    showPairingScreen();
    return;
  }
  lcd.setCursor(0, 0);
  lcd.print("                ");
  if (gate2.timer_state == ON) {
    return;
  }
  if (digitalRead(gate2_pin) == GATE_ACTIVATED) {
    lcd.setCursor(0, 1);
    lcd.print("Align laser      ");
    return;
  }
  if (finishedUntilMs != 0 && millis() < finishedUntilMs) {
    return;
  }
  updateLinkDisplay();
}

void updateLinkDisplay() {
  static bool linkWasLost = false;

  if (gate2.timer_state == ON) {
    return;
  }

  if (digitalRead(gate2_pin) == GATE_ACTIVATED) {
    return;
  }

  if (finishedUntilMs != 0 && millis() < finishedUntilMs) {
    return;
  }

  bool linked = lastRadioRxMs != 0 && (millis() - lastRadioRxMs <= RADIO_LINK_TIMEOUT_MS);
  if (!linked) {
    linkWasLost = true;
    lcd.setCursor(0, 1);
    lcd.print("No TX signal   ");
    return;
  }

  if (linkWasLost) {
    linkWasLost = false;
    lcd.setCursor(0, 0);
    lcd.print("                ");
  }
  lcd.setCursor(0, 1);
  lcd.print("Ready            ");
}

void Gate2_Timer_Action() {
  if (gate2.timer_state != ON) {
    return;
  }

  unsigned long now = millis();
  periodMillis = (now - startMillis) / 1000.0f;

  if (now - lastDisplayMs >= RADIO_DISPLAY_MS) {
    lastDisplayMs = now;
    lcd.setCursor(0, 0);
    lcd.print(periodMillis, 2);
    lcd.print("s               ");
  }
}

void clearLine1() {
  lcd.setCursor(0, 1);
  lcd.print("                ");
}

void startRxBuzzer(unsigned long durationMs) {
  buzzerUntilMs = millis() + durationMs;
}

void handleRxBuzzer() {
  if (buzzerUntilMs != 0 && millis() < buzzerUntilMs) {
    tone(buzzer_pin, RX_BUZZER_HZ);
  } else {
    noTone(buzzer_pin);
    buzzerUntilMs = 0;
  }
}

void refreshLine1WhenIdle() {
  if (gate2.timer_state == ON) {
    return;
  }
  if (finishedUntilMs != 0 && millis() < finishedUntilMs) {
    return;
  }
  restoreIdleDisplay();
}

void stopTimerAtGate2(unsigned long now) {
  periodMillis = (now - startMillis) / 1000.0f;

  gate2.timer_state = OFF;
  gate2.able_state = DISABLED;
  gate1_opened = false;

  lcd.setCursor(0, 0);
  lcd.print(periodMillis, 2);
  lcd.print("s               ");

  if (distance == ENABLED) {
    printSpeed();
  }

  sendToTransmitter(radio, addresses, CMD_GATE2_CLOSED, true);
  resumeRxListening();

  finishedUntilMs = now + RX_FINISHED_DISPLAY_MS;
  lcd.setCursor(0, 1);
  lcd.print("Finished         ");
  if (buzzer_finish_enabled) {
    startRxBuzzer(RX_BUZZER_FINISH_MS);
  }

  periodMillis = 0;
  startMillis = 0;
  lastDisplayMs = 0;
}

void Sense_Gate2() {
  bool beamBroken = (digitalRead(gate2_pin) == GATE_ACTIVATED);
  unsigned long now = millis();

  if (finishedUntilMs != 0 && now >= finishedUntilMs) {
    finishedUntilMs = 0;
    restoreIdleDisplay();
  }

  if (beamBroken && !gate2BeamWasBroken && gate2.timer_state == ON) {
    gate2BeamWasBroken = true;
    stopTimerAtGate2(now);
  } else if (beamBroken && !gate2BeamWasBroken) {
    gate2BeamWasBroken = true;
    lcd.setCursor(0, 1);
    lcd.print("Align laser      ");
    if (buzzer_align_enabled) {
      startRxBuzzer(RX_BUZZER_ALIGN_MS);
    }
  } else if (!beamBroken && gate2BeamWasBroken) {
    gate2BeamWasBroken = false;
    if (finishedUntilMs == 0) {
      restoreIdleDisplay();
    }
  } else if (beamBroken && gate2BeamWasBroken) {
    if (finishedUntilMs != 0 && now < finishedUntilMs) {
      lcd.setCursor(0, 1);
      lcd.print("Finished         ");
    }
  }
}

void printSpeed() {
  if (use_meters) {
    float distKm = distance_in_yards * 0.9144f / 1000.0f;
    avg_speed = (distKm / periodMillis) * 3600.0f;
    lcd.setCursor(avg_speed < 10 ? 9 : 8, 0);
    lcd.print(avg_speed, 1);
    lcd.print("KPH");
  } else {
    avg_speed = (distance_in_yards / periodMillis) * (3600.0f / 1760.0f);
    lcd.setCursor(avg_speed < 10 ? 9 : 8, 0);
    lcd.print(avg_speed, 1);
    lcd.print("MPH");
  }
}

byte laserLeft[8] = {B11111,B10100,B10011,B11000,B10011,B10011,B01100,B00011};
byte laserMid[8] = {B11111,B00000,B00000,B11111,B10000,B10000,B10000,B11111};
byte laserRight[8] = {B00000,B11000,B00100,B11110,B00011,B00010,B00010,B11110};
byte antenna[8] = {B00000,B00001,B00010,B00100,B11000,B00000,B00000,B00000};
byte tripodLeft[8] = {B00001,B00011,B00110,B01100,B11000,B00000,B00000,B00000};
byte tripodMid[8] = {B11100,B10011,B01000,B01000,B00100,B00100,B00010,B00000};
byte tripodRight[8] = {B00000,B00000,B11000,B00110,B00000,B00000,B00000,B00000};
byte backArrow[8] = {B00100,B01000,B11111,B01001,B00101,B00001,B01111,B00000};

#endif // FUNCTIONS_LASER_TIMER_V2_RX_H
