/**
 * functions_laser_timer_v2_tx.h
 * Laser Timer Firmware v2 — transmitter gate, radio, LCD, and buzzer logic
 *
 * Christian Staresina
 * 5/31/2026
 *
 * Gate 1 (start) triggers an RF burst to the receiver and a local buzzer.
 * While the receiver timer runs, this unit shows a counting screen until
 * the receiver reports gate 2 (finish) via CMD_GATE2_CLOSED.
 */

#ifndef FUNCTIONS_LASER_TIMER_V2_TX_H
#define FUNCTIONS_LASER_TIMER_V2_TX_H

#include <SPI.h>
#include <RF24.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
extern LiquidCrystal_I2C lcd;
#include "macros_laser_timer_v2_tx.h"
#include "radio_protocol_v2.h"

bool radioReady = false;

unsigned long buzzerUntilMs = 0;

#define TX_BUZZER_GATE1_MS 450
#define TX_BUZZER_FINISH_MS 700
#define TX_BUZZER_HZ 2000

extern const byte gate1_pin = 2;
extern const byte buzzer_pin = 1;

extern const int encoderButton = 3;
extern const byte encoderCLK = 4;
extern const byte encoderDAT = 5;

#define TX_ENCODER_LONG_PRESS_MS 800

bool gate1_opened = false;
bool gateWasOpen = false;
bool txTimerRunning = false;

unsigned long lastHeartbeatMs = 0;
unsigned long lastGateRepeatMs = 0;

extern RF24 radio(9, 10);
extern const byte addresses[][6] = {"00001", "00002"};

void printLaserImage();
void showTxGateOpenScreen();
void showTxCountingScreen();
void showTxRunCompleteScreen();
void showPairingScreen();
void showPairingSuccess();
void waitForPairing();
void resumeTxListening();
void PollTxRadio();
void startTxBuzzer(unsigned long durationMs);
void handleTxBuzzer();
void Sense_Gate1();
void encoderInitTx();
void checkEncoderOpensMenu();

// ---------------------------------------------------------------------------
// LCD screens
// ---------------------------------------------------------------------------

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

void showTxGateOpenScreen() {
  lcd.setCursor(0, 0);
  lcd.print("Gate 1 crossed  ");
  lcd.setCursor(0, 1);
  lcd.print("Align laser      ");
}

void showTxCountingScreen() {
  lcd.setCursor(0, 0);
  lcd.print("Timer counting  ");
  lcd.setCursor(0, 1);
  lcd.print("Waiting gate 2  ");
}

void showTxRunCompleteScreen() {
  lcd.setCursor(0, 0);
  lcd.print(TX_COMPLETE_LINE0);
  lcd.setCursor(0, 1);
  lcd.print("                ");
}

void printLaserImage() {
  lcd.setCursor(0, 0);
  lcd.print("Pass laser  ");
  lcd.print(char(0));
  lcd.print(char(1));
  lcd.print(char(2));
  lcd.print(char(3));
  lcd.setCursor(0, 1);
  lcd.print("when ready  ");
  lcd.print(char(4));
  lcd.print(char(5));
  lcd.print(char(6));
}

// ---------------------------------------------------------------------------
// Radio pairing and return-path polling
// ---------------------------------------------------------------------------

void resumeTxListening() {
  radio.openReadingPipe(0, addresses[RADIO_RX_SEND_PIPE]);
  radio.startListening();
}

void waitForPairing() {
  showPairingScreen();
  while (true) {
    if (sendPacket(radio, addresses, CMD_PING, true)) {
      showPairingSuccess();
      delay(RADIO_PAIR_OK_MS);
      lastHeartbeatMs = millis();
      resumeTxListening();
      return;
    }
    delay(RADIO_PAIR_RETRY_MS);
  }
}

void PollTxRadio() {
  if (!radioReady) {
    return;
  }

  resumeTxListening();

  while (radio.available()) {
    RadioPacket pkt;
    radio.read(&pkt, sizeof(pkt));

    if (pkt.magic != RADIO_MAGIC) {
      continue;
    }

    if (pkt.cmd == CMD_GATE2_CLOSED && txTimerRunning) {
      txTimerRunning = false;
      showTxRunCompleteScreen();
      startTxBuzzer(TX_BUZZER_FINISH_MS);
      unsigned long completeUntil = millis() + 1500;
      while (millis() < completeUntil) {
        handleTxBuzzer();
      }
      printLaserImage();
    }
  }
}

// ---------------------------------------------------------------------------
// Encoder — long press opens menu (short press ignored)
// ---------------------------------------------------------------------------

static unsigned long txButtonDownMs = 0;
static bool txButtonWasDown = false;
static bool txPressHandled = false;

void encoderInitTx() {
  pinMode(encoderCLK, INPUT);
  pinMode(encoderDAT, INPUT);
  pinMode(encoderButton, INPUT_PULLUP);
}

void checkEncoderOpensMenu() {
  unsigned long now = millis();
  bool buttonDown = digitalRead(encoderButton) == LOW;

  if (buttonDown && !txButtonWasDown) {
    txButtonDownMs = now;
    txPressHandled = false;
  }

  if (buttonDown && !txPressHandled && (now - txButtonDownMs >= TX_ENCODER_LONG_PRESS_MS)) {
    txPressHandled = true;
    while (digitalRead(encoderButton) == LOW) {
      delay(1);
    }
    txButtonWasDown = false;
    // TX menu not implemented yet; long-press is reserved for menu entry.
  }

  if (!buttonDown && txButtonWasDown && !txPressHandled) {
    txPressHandled = true;
  }

  txButtonWasDown = buttonDown;
}

// ---------------------------------------------------------------------------
// Buzzer — millis-timed tone on gate 1 start and run complete
// ---------------------------------------------------------------------------

void startTxBuzzer(unsigned long durationMs) {
  buzzerUntilMs = millis() + durationMs;
}

void handleTxBuzzer() {
  if (buzzerUntilMs != 0 && millis() < buzzerUntilMs) {
    tone(buzzer_pin, TX_BUZZER_HZ);
  } else {
    noTone(buzzer_pin);
    buzzerUntilMs = 0;
  }
}

// ---------------------------------------------------------------------------
// Gate 1 sensor and main loop handler
// ---------------------------------------------------------------------------

void Sense_Gate1() {
  if (!radioReady) {
    return;
  }

  PollTxRadio();

  bool gateOpen = (digitalRead(gate1_pin) == GATE_ACTIVATED);
  unsigned long now = millis();

  if (gateOpen && !gateWasOpen) {
    gate1_opened = true;
    sendGateOpenBurst(radio, addresses);
    lastGateRepeatMs = now;
    gateWasOpen = true;
    txTimerRunning = true;
    showTxCountingScreen();
    startTxBuzzer(TX_BUZZER_GATE1_MS);
    resumeTxListening();
  } else if (!gateOpen && gateWasOpen) {
    gate1_opened = false;
    sendPacket(radio, addresses, CMD_GATE1_CLOSED, false);
    gateWasOpen = false;
    if (!txTimerRunning) {
      printLaserImage();
    }
  } else if (!gateOpen && !txTimerRunning && (now - lastHeartbeatMs >= RADIO_HEARTBEAT_MS)) {
    sendPacket(radio, addresses, CMD_PING, false);
    lastHeartbeatMs = now;
    resumeTxListening();
  } else if (gateOpen && txTimerRunning && (now - lastGateRepeatMs >= RADIO_GATE_REPEAT_MS)) {
    sendPacket(radio, addresses, CMD_GATE1_OPEN, false);
    lastGateRepeatMs = now;
    resumeTxListening();
  } else if (txTimerRunning) {
    showTxCountingScreen();
  }

  handleTxBuzzer();
}

// ---------------------------------------------------------------------------
// Custom LCD characters (laser beam and tripod icons)
// ---------------------------------------------------------------------------

byte laserLeft[8] = {
  B11111, B10100, B10011, B11000, B10011, B10011, B01100, B00011
};

byte laserMid[8] = {
  B11111, B00000, B00000, B11111, B10000, B10000, B10000, B11111
};

byte laserRight[8] = {
  B00000, B11000, B00100, B11110, B00011, B00010, B00010, B11110
};

byte antenna[8] = {
  B00000, B00001, B00010, B00100, B11000, B00000, B00000, B00000
};

byte tripodLeft[8] = {
  B00001, B00011, B00110, B01100, B11000, B00000, B00000, B00000
};

byte tripodMid[8] = {
  B11100, B10011, B01000, B01000, B00100, B00100, B00010, B00000
};

byte tripodRight[8] = {
  B00000, B00000, B11000, B00110, B00000, B00000, B00000, B00000
};

byte backArrow[8] = {
  B00100, B01000, B11111, B01001, B00101, B00001, B01111, B00000
};

#endif
