#ifndef _FUNCTIONS_H    // Put these two lines at the top of your file.
#define _FUNCTIONS_H    // (Use a suitable name, usually based on the file name.)

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
extern LiquidCrystal_I2C lcd;
#include "macros_for_laser_timer_pcb_v1_2024_tx.h"

char buzzer_state = OFF;
int buzzer_count = 0;

// Pin configuration
extern const byte gate1_pin = 2;
extern const byte buzzer_pin = 1;

// Gate configuration
bool gate1_opened = false;
bool laser_crossed = false;
unsigned int print_crossed_count = 0;

// Timer variables
extern float startMillis = 0;
extern float currentMillis = 0;
extern float periodMillis = 0;

// Menu variables
extern const int menuButton = 3;
extern const byte upButton = 4;
extern const byte downButton = 5;
extern const byte selectButton = 6;
extern byte mainMenu = 1;
extern byte connectionMenu = 1;
extern bool enterMenu = false;

// Menu button debounce
extern byte previousState = HIGH;
extern unsigned int previousPress;
extern volatile byte buttonFlag= 0; // "volatile" for use in interrupt
extern byte buttonDebounce = 70; // was 20

extern RF24 radio(9, 10); // CE, CSN (was 7, 8)
extern const byte addresses[][6] = {"00001", "00002"};

// starting gate
void Sense_Gate1() {
  if (digitalRead(gate1_pin) == GATE_ACTIVATED) {
    radio.openWritingPipe(addresses[1]); //00001
    radio.stopListening();
    gate1_opened = true;
    radio.write(&gate1_opened, sizeof(gate1_opened));
    laser_crossed = true;
    lcd.setCursor(0,1);
    lcd.print("Align Laser      ");
    buzzer_state = ON;
  }
  else {
    radio.openReadingPipe(1, addresses[0]); //00002
    radio.startListening();
    gate1_opened = false;
    lcd.setCursor(0,1);
    lcd.print("Ready            ");
  }

  switch (laser_crossed) {
    case true:
      lcd.setCursor(0,0);
      lcd.print("Counting");
      print_crossed_count++;
      switch (print_crossed_count) {
        case 100:
          print_crossed_count = 0;
          laser_crossed = false;
          lcd.setCursor(0,0);
          lcd.print("        ");
          break;
        default: break;
      }
      break;
    default: break;
  }

  switch (buzzer_state) {
    case ON:
      tone(buzzer_pin, 3000); // 3kHZ sound
      buzzer_count++;
      switch(buzzer_count) {
        case 20:
          noTone(buzzer_pin); // buzzer off
          buzzer_count = 0;
          buzzer_state = OFF;
          break;
        default: break;
      }
      break;
    default: break;
  }
}

byte laserLeft[8] =
{
  B11111,
  B10100,
  B10011,
  B11000,
  B10011,
  B10011,
  B01100,
  B00011
};

byte laserMid[8] =
{
  B11111,
  B00000,
  B00000,
  B11111,
  B10000,
  B10000,
  B10000,
  B11111
};

byte laserRight[8] =
{
  B00000,
  B11000,
  B00100,
  B11110,
  B00011,
  B00010,
  B00010,
  B11110
};

byte antenna[8] =
{
  B00000,
  B00001,
  B00010,
  B00100,
  B11000,
  B00000,
  B00000,
  B00000
};

byte tripodLeft[8] =
{
  B00001,
  B00011,
  B00110,
  B01100,
  B11000,
  B00000,
  B00000,
  B00000
};

byte tripodMid[8] =
{
  B11100,
  B10011,
  B01000,
  B01000,
  B00100,
  B00100,
  B00010,
  B00000
};

byte tripodRight[8] =
{
  B00000,
  B00000,
  B11000,
  B00110,
  B00000,
  B00000,
  B00000,
  B00000
};

byte backArrow[8] =
{
  B00100,
  B01000,
  B11111,
  B01001,
  B00101,
  B00001,
  B01111,
  B00000
};

void printLaserImage()
{
  lcd.setCursor(0,0);
  lcd.print("Pass laser  ");
  lcd.print(char(0));
  lcd.print(char(1));
  lcd.print(char(2));
  lcd.print(char(3));
  lcd.setCursor(0,1);
  lcd.print("when ready  ");
  lcd.print(char(4));
  lcd.print(char(5));
  lcd.print(char(6));
}

#endif // _HEADERFILE_H    // Put this line at the end of your file.