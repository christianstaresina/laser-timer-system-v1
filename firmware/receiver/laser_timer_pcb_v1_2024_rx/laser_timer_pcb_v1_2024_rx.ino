// Christian Staresina
// laser_timer_pcb_v1_2024_rx.ino
// 8/24/2024

#include "src/functions_for_laser_timer_pcb_v1_2024_rx.h"
#include "src/macros_for_laser_timer_pcb_v1_2024_rx.h"
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
extern LiquidCrystal_I2C lcd(0x27,16,2);

extern char initial_menu_state;
extern char timer_state;
extern char set_distance_state;
//extern char speedometer_state;

// Pin configuration
extern const byte gate2_pin;
extern const byte buzzer_pin;
extern const byte radio_cs_pin;
extern const byte sd_cs_pin;

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

extern RF24 radio; // CE, CSN (9, 10)
extern const byte addresses[][6];

void setup()
{
  // Pin Input/Output Configuration
  pinMode(gate2_pin, INPUT);
  pinMode(buzzer_pin, OUTPUT);
  pinMode(radio_cs_pin, OUTPUT);
  pinMode(sd_cs_pin, OUTPUT);
  pinMode(A0, OUTPUT); // 3V3 regulator enable pin
  digitalWrite(A0, HIGH);

  SPI.begin(); // initialize the SPI library

  // rotary encoder
	pinMode(encoderCLK,INPUT);
	pinMode(encoderDAT,INPUT);
	pinMode(encoderButton, INPUT_PULLUP);

	// Setup Serial Monitor
	//Serial.begin(9600);

	// Read the initial state of CLK
	lastStateCLK = digitalRead(encoderCLK);

  // Call updateEncoder() when any high/low changed seen
	// on interrupt 0 (pin 2), or interrupt 1 (pin 3)
	//attachInterrupt(0, updateEncoder, CHANGE);
	//attachInterrupt(1, updateEncoder, CHANGE);
  
  // menu buttons
  //pinMode(upButton, INPUT_PULLUP);
  //pinMode(downButton, INPUT_PULLUP);
  //pinMode(menuButton, INPUT_PULLUP);
  //pinMode(selectButton, INPUT_PULLUP);

  digitalWrite(radio_cs_pin, HIGH); // initially disable transceiver SPI chip select
  digitalWrite(sd_cs_pin, HIGH); // initially disable sd card SPI chip select

  // Menu interrupt
  //attachInterrupt(digitalPinToInterrupt(encoderButton), menuInterrupt, LOW); // CHANGE (can be either LOW, HIGH, RISING, FALLING, CHANGE)

  // RF24 radio module setup
  radio.begin();
  radio.setChannel(108); // 2.508 Ghz - Above most Wifi Channels
  radio.setAutoAck(false);
  //radio.openWritingPipe(addresses[0]); //00002
  radio.openReadingPipe(1, addresses[1]); //00001
  radio.setPALevel(RF24_PA_MAX); // MIN, LOW, HIGH, or MAX
  radio.setDataRate(RF24_250KBPS); //set as: F24_250KBPS, F24_1MBPS, F24_2MBPS ==>250KBPS = longest range
  radio.startListening();

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
  lcd.print("LASER TIMER RX");
  lcd.setCursor(1,1);
  lcd.print("Created by CS");
  delay(3000);
  lcd.clear();
  delay(500);

  //printLaserImage();
}

// LOOP BEGINNING ----------------------------------------------------------------------------

void loop()
{
  openMainMenu();
  Timer(timer_state);
  //updateEncoder();
  //rotaryEncoder();

  //Speedometer(speedometer_state);
  //Receive_Gate1_Opened_Message();
  //Gate2_Timer_Action();
  //Sense_Gate2();
  
  //Gate_Open_Or_Closed(); // use for debugging
}

// LOOP END ----------------------------------------------------------------------------------
