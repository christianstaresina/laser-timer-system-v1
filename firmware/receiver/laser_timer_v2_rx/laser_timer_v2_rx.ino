/**
 * laser_timer_v2_rx.ino
 * Laser Timer Firmware v2 — receiver (gate 2 / finish unit)
 *
 * Christian Staresina
 * 5/31/2026
 *
 * Hardware: ATmega328 + nRF24L01+PA+LNA, I2C LCD, gate 2 laser sensor, rotary encoder.
 * Open this sketch folder in Arduino IDE: firmware/receiver/laser_timer_v2_rx
 */

#include "src/functions_laser_timer_v2_rx.h"
#include "src/macros_laser_timer_v2_rx.h"
#include <SPI.h>
#include <RF24.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
extern LiquidCrystal_I2C lcd(0x27, 16, 2);

extern char initial_menu_state;
extern char timer_state;

extern const byte gate2_pin;
extern const byte radio_cs_pin;
extern const byte sd_cs_pin;

extern RF24 radio;
extern const byte addresses[][6];
extern bool radioReady;

void showPairingScreen();
void waitForPairing();

void setup() {
  pinMode(gate2_pin, INPUT);
  pinMode(buzzer_pin, OUTPUT);
  pinMode(radio_cs_pin, OUTPUT);
  pinMode(sd_cs_pin, OUTPUT);
  pinMode(A0, OUTPUT);
  digitalWrite(A0, HIGH);
  delay(RADIO_POWER_SETTLE_MS);

  pinMode(encoderCLK, INPUT);
  pinMode(encoderDAT, INPUT);
  pinMode(encoderButton, INPUT_PULLUP);

  lastStateCLK = digitalRead(encoderCLK);

  digitalWrite(radio_cs_pin, HIGH);
  digitalWrite(sd_cs_pin, HIGH);

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
  lcd.print("LASER TIMER RX");
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

  radio.openReadingPipe(0, addresses[RADIO_TX_SEND_PIPE]);
  radio.startListening();
  waitForPairing();
}

void loop() {
  PollRadio();
  openMainMenu();
  Timer(timer_state);
}
