// Christian Staresina
// laser_timer_pcb_v1_2024_tx.ino
// 8/24/2024

#include "src/functions_for_laser_timer_pcb_v1_2024_tx.h"
#include "src/macros_for_laser_timer_pcb_v1_2024_tx.h"
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
extern LiquidCrystal_I2C lcd(0x27,16,2);

// Pin configuration
extern const byte gate1_pin;
extern const byte buzzer_pin;

// Timer variables
extern float startMillis;
extern float currentMillis;
extern float periodMillis;

// Menu variables
extern const int menuButton;
extern const byte upButton;
extern const byte downButton;
extern const byte selectButton;
extern byte mainMenu;
extern byte connectionMenu;
extern bool enterMenu;

// Menu button debounce
extern byte previousState;
extern unsigned int previousPress;
extern volatile byte buttonFlag; // "volatile" for use in interrupt
extern byte buttonDebounce; // was 20

extern RF24 radio; // CE, CSN (was 7, 8)
extern const byte addresses[][6];

void setup()
{
  // laser and receiver module
  pinMode(gate1_pin, INPUT);

  pinMode(A0, OUTPUT); // 3V3 regulator enable pin
  digitalWrite(A0, HIGH);
  
  // menu buttons
  pinMode(upButton, INPUT_PULLUP);
  pinMode(downButton, INPUT_PULLUP);
  pinMode(menuButton, INPUT_PULLUP);
  pinMode(selectButton, INPUT_PULLUP);

  // interrupt
  //attachInterrupt(digitalPinToInterrupt(menuButton), menuInterrupt, CHANGE); // CHANGE (can be either LOW, HIGH, RISING, FALLING, CHANGE)

  // RF24 radio module setup
  radio.begin();
  radio.setChannel(108); // 2.508 Ghz - Above most Wifi Channels
  radio.setAutoAck(false);
  
  radio.setPALevel(RF24_PA_MAX); // MIN, LOW, HIGH, or MAX
  radio.setDataRate(RF24_250KBPS); //set as: F24_250KBPS, F24_1MBPS, F24_2MBPS ==>250KBPS = longest range
  radio.openWritingPipe(addresses[1]); //00001
  radio.stopListening();
  //radio.openReadingPipe(1, addresses[0]); //00002
  //radio.startListening();

  // LCD display setup
  lcd.init();
  lcd.backlight();
  lcd.begin(16,2);

  // custom characters
  lcd.createChar(0, laserLeft);
  lcd.createChar(1, laserMid);
  lcd.createChar(2, laserRight);
  lcd.createChar(3, antenna);
  lcd.createChar(4, tripodLeft);
  lcd.createChar(5, tripodMid);
  lcd.createChar(6, tripodRight);
  lcd.createChar(7, backArrow);

  lcd.clear();

  lcd.setCursor(1,0);
  lcd.print("LASER TIMER TX");
  lcd.setCursor(1,1);
  lcd.print("Created by CS");
  delay(3000);
  
  lcd.clear();

  //printLaserImage();
}

// LOOP BEGINNING ----------------------------------------------------------------------------

void loop()
{
  //openMainMenu();
  Sense_Gate1();
}

// LOOP END ----------------------------------------------------------------------------------
