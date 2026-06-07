/**
 * laser_timer_v2_tx.ino
 * Laser Timer Firmware v2 — transmitter (gate 1 / start unit)
 *
 * Christian Staresina
 * 5/31/2026
 *
 * Hardware: ATmega328 + nRF24L01+PA+LNA, I2C LCD, gate 1 laser sensor.
 * Open this sketch folder in Arduino IDE: firmware/transmitter/laser_timer_v2_tx
 */

#include "src/functions_laser_timer_v2_tx.h"
#include "src/macros_laser_timer_v2_tx.h"
#include <SPI.h>
#include <RF24.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
extern LiquidCrystal_I2C lcd(0x27, 16, 2);

extern RF24 radio;
extern const byte addresses[][6];
extern bool radioReady;
extern unsigned long lastHeartbeatMs;

void printLaserImage();
void showTxGateOpenScreen();
void showPairingScreen();
void waitForPairing();

void setup() {
  pinMode(gate1_pin, INPUT);
  pinMode(buzzer_pin, OUTPUT);
  encoderInitTx();

  pinMode(A0, OUTPUT);
  digitalWrite(A0, HIGH);
  delay(RADIO_POWER_SETTLE_MS);

  lcd.init();
  lcd.backlight();
  lcd.begin(16, 2);

  lcd.createChar(0, laserLeft);
  lcd.createChar(1, laserMid);
  lcd.createChar(2, laserRight);
  lcd.createChar(3, antenna);
  lcd.createChar(4, tripodLeft);
  lcd.createChar(5, tripodMid);
  lcd.createChar(6, tripodRight);
  lcd.createChar(7, backArrow);

  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print("LASER TIMER TX");
  lcd.setCursor(1, 1);
  lcd.print("Created by CS");
  delay(3000);

  lcd.clear();
  showPairingScreen();

  SPI.begin();
  radioReady = initRadio(radio);
  if (!radioReady) {
    lcd.setCursor(0, 0);
    lcd.print("Radio not found ");
    while (true) {
      delay(1000);
    }
  }

  radio.openWritingPipe(addresses[RADIO_TX_SEND_PIPE]);
  waitForPairing();

  gateWasOpen = (digitalRead(gate1_pin) == GATE_ACTIVATED);
  if (gateWasOpen) {
    showTxGateOpenScreen();
  } else {
    printLaserImage();
  }
}

void loop() {
  checkEncoderOpensMenu();
  Sense_Gate1();
}
