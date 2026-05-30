#ifndef _FUNCTIONS_H    // Put these two lines at the top of your file.
#define _FUNCTIONS_H    // (Use a suitable name, usually based on the file name.)

#include "macros_for_laser_timer_pcb_v1_2024_rx.h"
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Wire.h>
//#include <SD.h>
#include <LiquidCrystal_I2C.h>
extern LiquidCrystal_I2C lcd;

// Create gate2 object
struct Gate {
  //char position_state; // OPENED or CLOSED
  char timer_state; // ON or OFF
  char able_state; // ENABLED or DISABLED
  unsigned int timer_count;
};

extern char initial_menu_state = ON;
char distance = ENABLED;
extern char set_distance_state = DISABLED;
byte distance_count = 0;
int distance_in_yards = 40;
float avg_speed = 0;
//extern char speedometer_state = DISABLED;

char buzzer_state = OFF;
int buzzer_count = 0;

// Pin configuration
extern const byte gate2_pin = 2;
extern const byte buzzer_pin = 1;
extern const byte radio_cs_pin = 10; // SPI chip select (CS) pin for slave 1
extern const byte sd_cs_pin = 8; // SPI chip select (CS) pin for slave 2

// Rotary encoder setup
extern const int encoderButton = 3;
extern const byte encoderCLK = 4;
const byte encoderDAT = 5;
byte counter = 0;
extern int currentStateCLK = 0;
int lastStateCLK = 0;
String currentDir = "";
unsigned long lastButtonPress = 0;

int encoderCLKcount = 0;

// Gate configuration
Gate gate2 = {OFF, DISABLED, 0};
bool gate1_opened = false;

// Timer variables
extern char timer_state = DISABLED;
bool start_timer = true;
extern float startMillis = 0;
extern float currentMillis = 0;
extern float periodMillis = 0;

// Menu variables
//extern const byte selectButton = 6;
extern byte mainMenu = 1;
//extern byte connectionMenu = 1;
extern bool enterMenu = false;
extern byte distanceMenu = 1;

// Menu button debounce
extern byte previousState = HIGH;
extern unsigned int previousPress = 0;
extern volatile byte buttonFlag = 0; // "volatile" for use in interrupt
extern byte buttonDebounce = 70; // was 20

extern RF24 radio(9, 10); // CE, CSN (was 7, 8)
extern const byte addresses[][6] = {"00001", "00002"};




// INITIALIZATIONS
void Receive_Gate1_Opened_Message();
void Gate2_Timer_Action();
void Sense_Gate2();
void Timer(char timer_state);
void menuInterrupt();
void updateMainMenu();
void executeMainMenuAction();
void openMainMenu();
void updateDistanceMenu();
void executeDistanceMenuAction();
void openDistanceMenu();
void printSpeed();
void updateEncoder();
void rotaryEncoder();



// DEFINITIONS

void Timer(char timer_state) {
  switch(timer_state) {
    case ENABLED:
      Receive_Gate1_Opened_Message();
      Gate2_Timer_Action();
      Sense_Gate2();
      if (digitalRead(encoderButton) == LOW) {
        initial_menu_state = ON;
        delay(100);
      }
      break;
    default: break;
  }
}

// first gate opened message
void Receive_Gate1_Opened_Message() {
  //SPI.beginTransaction(SPISettings(16000000, MSBFIRST, SPI_MODE0));
  //digitalWrite(radio_cs_pin, LOW); // enable transceiver SPI chip select
  
  if (radio.available()) {
    radio.read(&gate1_opened, sizeof(gate1_opened));
  }
      
  if (gate1_opened) {
    gate2.timer_state = ON;
    //gate2.able_state = ENABLED;
  }

  //digitalWrite(radio_cs_pin, HIGH);
  //SPI.endTransaction();
}

// display elapsed time
void Gate2_Timer_Action() {
  switch (gate2.timer_state) {
    case ON:
      switch (start_timer) {
        case true:
          startMillis = currentMillis = millis();
          start_timer = false;
          break;
        default:
          currentMillis = millis();
          break;
      }

      periodMillis = (currentMillis - startMillis) / 1000;

      gate2.timer_count++;
      switch (gate2.timer_count) {
        case 2:
          gate2.timer_count = 0;
          lcd.setCursor(0,0);
          lcd.print(periodMillis, 3);
          lcd.print("s               ");
        default: break;
      }
      break;
    case OFF: break;
  }
}

// ending gate
void Sense_Gate2() {
  if (digitalRead(gate2_pin) == GATE_ACTIVATED) {
    lcd.setCursor(0,1);
    lcd.print("Align Laser      ");
    buzzer_state = ON;

    if (gate2.timer_state == ON) {
      gate2.timer_state = OFF; // stop timer
      gate2.timer_count = 0; // reset
      gate2.able_state = DISABLED;
      gate1_opened = false; // reset
      start_timer = true; // reset
        
      // display final time
      lcd.setCursor(0,0);
      lcd.print(periodMillis, 3);
      lcd.print("s               ");
      
      // display average speed
      if (distance == ENABLED) {
        printSpeed();
      }
      
      periodMillis = 0; // reset
      currentMillis = 0; // reset
      startMillis = 0; // reset
    }
  }
  else {
    lcd.setCursor(0,1);
    lcd.print("Ready            ");
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

void printSpeed() {
  avg_speed = (distance_in_yards / periodMillis) * (3600 / 1760); // (3600 / 1760) is [(# of seconds in 1 hour) / (# of yards in 1 mile)]
  if (avg_speed < 10) {
    lcd.setCursor(9,0);
  }
  else {
    lcd.setCursor(8,0);
  }
  lcd.print(avg_speed);
  lcd.print("MPH");
}

// Rotary encoder function for initial test setup
void updateEncoder(){
	// Read the current state of CLK
	currentStateCLK = digitalRead(encoderCLK);

	// If last and current state of CLK are different, then pulse occurred
	// React to only 1 state change to avoid double count
	if (currentStateCLK != lastStateCLK  && currentStateCLK == 1){

		// If the DT state is different than the CLK state then
		// the encoder is rotating CCW so decrement
		if (digitalRead(encoderDAT) != currentStateCLK) {
			counter--;
			currentDir ="CCW";
		} else {
			// Encoder is rotating CW so increment
			counter++;
			currentDir ="CW";
		}

		Serial.print("Direction: ");
		Serial.print(currentDir);
		Serial.print(" | Counter: ");
		Serial.println(counter);
	}

	// Remember last CLK state
	lastStateCLK = currentStateCLK;
}

void rotaryEncoder() {
  // *NOTE: I initially copied the following rotary encoder code from https://lastminuteengineers.com/rotary-encoder-arduino-tutorial/ on 6/9/2024

  // Read the current state of CLK
	currentStateCLK = digitalRead(encoderCLK);
  
	// If last and current state of CLK are different, then pulse occurred
	// React to only 1 state change to avoid double count
	if (currentStateCLK != lastStateCLK) {
		// If the DT state is different than the CLK state then
		// the encoder is rotating CW so increment
		if (digitalRead(encoderDAT) != currentStateCLK) {
			counter++;
			currentDir = "CW";
		}
    else if (digitalRead(encoderDAT) == currentStateCLK) {
			// Encoder is rotating CCW so decrement
			counter--;
			currentDir = "CCW";
		}

		Serial.print("Direction: ");
		Serial.print(currentDir);
		Serial.print(" | Counter: ");
		Serial.println(counter);
	}

	// Remember last CLK state
	lastStateCLK = currentStateCLK;

	// Read the button state
	int btnState = digitalRead(encoderButton);

	//If we detect LOW signal, button is pressed
	if (btnState == LOW) {
		//if 50ms have passed since last LOW pulse, it means that the
		//button has been pressed, released and pressed again
		if (millis() - lastButtonPress > 50) {
			Serial.println("Button pressed!");
		}

		// Remember last button press event
		lastButtonPress = millis();
	}

	// Put in a slight delay to help debounce the reading
	//delay(1);
}


/*
void Set_Distance(char set_distance_state) {
  switch(set_distance_state) {
    case ENABLED:
      
      break;
    default: break;
  }
}

void Speedometer(char speedometer_state) {
  switch(speedometer_state) {
    case ENABLED:
      Receive_Gate1_Opened_Message();
      break;
    default: break;
  }
}
/*

/*
void Gate_Open_Or_Closed() {
  lcd.setCursor(0,1);
  
  if (digitalRead(gate2_pin) == GATE_ACTIVATED) {
    lcd.print("Align Laser      ");
    buzzer_state = ON;
  }
  else {
    lcd.print("Ready            ");
  }

  switch (buzzer_state) {
    case ON:
      tone(buzzer_pin, 3000); // 3kHZ sound
      buzzer_count++;
      switch(buzzer_count) {
        case 30:
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
*/

// Custom characters for laser image
/*
byte laserBeam[] =
{
  B00000,
  B00000,
  B00000,
  B10101,
  B00000,
  B00000,
  B00000,
  B00000
};
*/

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

/*
float speedCalc()
{
  return ( (10/periodMillis)*0.68181818 );
}

float speedCalc3()
{
  return ( (10/periodMillis3)*0.68181818 );
}
*/


void menuInterrupt()
{
  buttonFlag = 1;
}

// Main Menu Start ---------------------------------------------------------

void openMainMenu()
{
  if( ( (millis() - previousPress) > buttonDebounce && buttonFlag ) || initial_menu_state == ON ) // && buttonFlag
  {
    previousPress = millis();
    
    if( (digitalRead(encoderButton) == LOW && previousState == HIGH) || initial_menu_state == ON ) // || digitalRead(selectButton) == LOW
    {
      initial_menu_state = OFF;
      enterMenu = true;
      lcd.setCursor(0,0);
      lcd.print("      MENU      ");
      lcd.setCursor(0,1);
      lcd.print("                ");
      delay(1000); // was 1200
      updateMainMenu();

      while (enterMenu == true)
      {
        // Read the current state of CLK
	      currentStateCLK = digitalRead(encoderCLK);

	      // If last and current state of CLK are different, then pulse occurred
	      // React to only 1 state change to avoid double count
	      if (currentStateCLK != lastStateCLK && currentStateCLK == HIGH)
        {
          // If the DT state is different than the CLK state then
		      // the encoder is rotating CW so increment
		      if (digitalRead(encoderDAT) != currentStateCLK)
          {
			      mainMenu++;
			      updateMainMenu();
            delay(50);
		      }
          else if (digitalRead(encoderDAT) == currentStateCLK)
          {
			      // Encoder is rotating CCW so decrement
			      mainMenu--;
			      updateMainMenu();
            delay(50);
		      }
	      }

	      // Remember last CLK state
	      lastStateCLK = currentStateCLK;
  
        if (digitalRead(encoderButton) == LOW)
        {
          enterMenu = false;
          executeMainMenuAction();
          delay(100);
        }

        //if (digitalRead(menuButton) == LOW)
        //{
        //  enterMenu = false;
        //}
      }
      previousState = LOW;
    }
    
    //else if(digitalRead(encoderButton) == HIGH || digitalRead(selectButton) == HIGH && previousState == LOW)
    //{
      //previousState = HIGH;
    //}
    buttonFlag = 0;
  }
}

void updateMainMenu()
{
  switch (mainMenu)
  {
    case 0: // to keep from scrolling past first menu item
      mainMenu = 1; // first menu item
      break;
    case 1:
      lcd.setCursor(0,0);
      lcd.print(">Stopwatch      ");
      lcd.setCursor(0,1);
      lcd.print(" Set Speedometer");
      break;
    case 2:
      lcd.setCursor(0,0);
      lcd.print(" Stopwatch      ");
      lcd.setCursor(0,1);
      lcd.print(">Set Speedometer");
      break;
    /*
    case 3:
      lcd.setCursor(0,0);
      lcd.print(">Exit Menu      ");
      lcd.setCursor(0,1);
      lcd.print("                ");
      break;
    */
    case 3: // to keep from scrolling past last menu item
      mainMenu = 2; // last menu item
      break;
  }
}

void executeMainMenuAction()
{
  switch (mainMenu)
  {
    case 1:
      timer_state = ENABLED;
      mainMenu = 1;
      lcd.home();
      lcd.print("  Starting...   ");
      lcd.setCursor(0,1);
      lcd.print("                ");
      delay(1000);
      lcd.clear();
      break;
    case 2:
      timer_state = DISABLED;
      mainMenu = 1;
      lcd.home();
      lcd.print(" Speed Settings ");
      lcd.setCursor(0,1);
      lcd.print("                ");
      delay(1000);
      lcd.clear();
      //Set_Distance(ENABLED);
      openDistanceMenu();
      break;
    /*
    case 3:
      
      mainMenu = 1;
      lcd.home();
      lcd.print("Exiting         ");
      lcd.setCursor(0,1);
      lcd.print("                ");
      delay(1000);
      lcd.clear();
      break;
    */
  }
}

// Main Menu End -----------------------------------------------------------

// Distance Menu Start ---------------------------------------------------------

void openDistanceMenu()
{
      enterMenu = true;
      
      //lcd.setCursor(0,0);
      //lcd.print("      MENU      ");
      //lcd.setCursor(0,1);
      //lcd.print("                ");
      //delay(1200);
      
      updateDistanceMenu();

      while (enterMenu == true)
      {
        // Read the current state of CLK
	      currentStateCLK = digitalRead(encoderCLK);

	      // If last and current state of CLK are different, then pulse occurred
	      // React to only 1 state change to avoid double count
	      if (currentStateCLK != lastStateCLK && currentStateCLK == HIGH)
        {
          // If the DT state is different than the CLK state then
		      // the encoder is rotating CW so increment
		      if (digitalRead(encoderDAT) != currentStateCLK)
          {
			      distanceMenu++;
            updateDistanceMenu();
            delay(50);
		      }
          else
          {
			      // Encoder is rotating CCW so decrement
			      distanceMenu--;
			      updateDistanceMenu();
            delay(50);
		      }
	      }

	      // Remember last CLK state
	      lastStateCLK = currentStateCLK;
  
        if (digitalRead(encoderButton) == LOW)
        {
          enterMenu = false;
          executeDistanceMenuAction();
          delay(100);
          //while (!digitalRead(selectButton));
        }

        //if (digitalRead(menuButton) == LOW)
        //{
        //  enterMenu = false;
        //}
      }
}

void updateDistanceMenu()
{
  switch (distanceMenu)
  {
    case 0: // to keep from scrolling past first menu item
      distanceMenu = 1; // first menu item
      break;
    case 1:
      lcd.setCursor(0,0);
      lcd.print(">Enable/Disable ");
      lcd.setCursor(0,1);
      lcd.print(" 40 yards       ");
      break;
    case 2:
      lcd.setCursor(0,0);
      lcd.print(" Enable/Disable ");
      lcd.setCursor(0,1);
      lcd.print(">40 yards       ");
      break;
    case 3:
      lcd.setCursor(0,0);
      lcd.print(">10 yards       ");
      lcd.setCursor(0,1);
      lcd.print(" 5 yards        ");
      break;
    case 4:
      lcd.setCursor(0,0);
      lcd.print(" 10 yards       ");
      lcd.setCursor(0,1);
      lcd.print(">5 yards        ");
      break;
    case 5:
      lcd.setCursor(0,0);
      lcd.print(">2 yards        ");
      lcd.setCursor(0,1);
      lcd.print(" 1 yard         ");
      break;
    case 6:
      lcd.setCursor(0,0);
      lcd.print(" 2 yards        ");
      lcd.setCursor(0,1);
      lcd.print(">1 yard         ");
      break;
    case 7:
      lcd.setCursor(0,0);
      lcd.print(">Back           ");
      lcd.setCursor(0,1);
      lcd.print("                ");
      break;
    case 8: // to keep from scrolling past last menu item
      distanceMenu = 7; // last menu item
      break;
    default: break;
  }
}

void executeDistanceMenuAction()
{
  switch (distanceMenu)
  {
    case 1:
      distanceMenu = 1;
      switch (distance_count) {
        case 0:
          distance = DISABLED;
          distance_count++;
          lcd.home();
          lcd.print(" Speed Disabled ");
          lcd.setCursor(0,1);
          lcd.print("                ");
          delay(1000);
          lcd.clear();
          openDistanceMenu();
          break;
        case 1:
          distance = ENABLED;
          distance_count--;
          lcd.home();
          lcd.print(" Speed Enabled  ");
          lcd.setCursor(0,1);
          lcd.print("                ");
          delay(1000);
          lcd.clear();
          openDistanceMenu();
          break;
      }
      break;
    case 2:
      distanceMenu = 1;
      distance_in_yards = 40;
      lcd.home();
      lcd.print("  40 Yards Set  ");
      lcd.setCursor(0,1);
      lcd.print("                ");
      delay(1000);
      lcd.clear();
      openDistanceMenu();
      break;
    case 3:
      distanceMenu = 1;
      distance_in_yards = 10;
      lcd.home();
      lcd.print("  10 Yards Set  ");
      lcd.setCursor(0,1);
      lcd.print("                ");
      delay(1000);
      lcd.clear();
      openDistanceMenu();
      break;
    case 4:
      distanceMenu = 1;
      distance_in_yards = 5;
      lcd.home();
      lcd.print("  5 Yards Set   ");
      lcd.setCursor(0,1);
      lcd.print("                ");
      delay(1000);
      lcd.clear();
      openDistanceMenu();
      break;
    case 5:
      distanceMenu = 1;
      distance_in_yards = 2;
      lcd.home();
      lcd.print("  2 Yards Set   ");
      lcd.setCursor(0,1);
      lcd.print("                ");
      delay(1000);
      lcd.clear();
      openDistanceMenu();
      break;
    case 6:
      distanceMenu = 1;
      distance_in_yards = 1;
      lcd.home();
      lcd.print("  1 Yard Set    ");
      lcd.setCursor(0,1);
      lcd.print("                ");
      delay(1000);
      lcd.clear();
      openDistanceMenu();
      break;
    case 7:
      distanceMenu = 1;
      lcd.home();
      //lcd.print("   Exiting...   ");
      //lcd.setCursor(0,1);
      //lcd.print("                ");
      //delay(1000);
      lcd.clear();
      initial_menu_state = ON;
      //openMainMenu();
      break;
    default: break;
  }
}

// Distance Menu End -----------------------------------------------------------

/*
void executeConnectionMenuAction()
{
  switch (connectionMenu)
  {
    case 1:
      connectionMenu = 1;
      testConnectionTX();
      break;
    case 2:
      connectionMenu = 1;
      testConnectionRX();
      break;
    case 3:
      //updateMainMenu();
      connectionMenu = 1;
      openMainMenu2();
      break;
  }
}

void openConnectionMenu()
{
  if((millis() - previousPress) > buttonDebounce)
  {
    previousPress = millis();
    
    if(digitalRead(selectButton) == LOW && previousState == HIGH)
    {
      updateConnectionMenu();
      previousState = LOW;
      enterMenu = true;
      delay(100);
      
while (enterMenu == true)
{
  if (digitalRead(selectButton) == HIGH && previousState == LOW)
  {
    previousState = HIGH;
  
    while (previousState == HIGH) // enterMenu == true
      {
        if (digitalRead(downButton) == LOW)
        {
          connectionMenu++;
          updateConnectionMenu();
          delay(100);
        }
  
        if (digitalRead(upButton) == LOW)
        {
          connectionMenu--;
          updateConnectionMenu();
          delay(100);
        }
  
        if (digitalRead(selectButton) == LOW)
        {
          previousState = LOW;
          enterMenu = false;
          executeConnectionMenuAction();
          delay(100);
          //while (!digitalRead(selectButton));
        }

        //delay(100);
        openMainMenu();
      }
  }
openMainMenu();
}
      
      //previousState = LOW;
    }
    
    else if(digitalRead(selectButton) == HIGH && previousState == LOW)
    {
      previousState = HIGH;
    }
    //buttonFlag = 0;
  }
}

void updateConnectionMenu()
{
  switch (connectionMenu)
  {
    case 0: // to keep from scrolling past first menu item
      connectionMenu = 1; // first menu item
      break;
    case 1:
      lcd.setCursor(0,0);
      lcd.print(">Transmit Signal");
      lcd.setCursor(0,1);
      lcd.print(" Receive Signal ");
      break;
    case 2:
      lcd.setCursor(0,0);
      lcd.print(" Transmit Signal");
      lcd.setCursor(0,1);
      lcd.print(">Receive Signal ");
      break;
    case 3:
      lcd.setCursor(0,0);
      lcd.print(">");
      lcd.print(char(7));
      lcd.print("Go Back       ");
      lcd.setCursor(0,1);
      lcd.print("                ");
      break;
    case 4: // to keep from scrolling past last menu item
      connectionMenu = 3; // last menu item
      break;
  }
}
*/

/*
void action2()
{
  lcd.clear();
  lcd.print(">Executing #2");
  delay(1500);
}
void action3()
{
  lcd.clear();
  lcd.print(">Executing #3");
  delay(1500);
}
void action4()
{
  lcd.clear();
  lcd.print(">Executing #4");
  delay(1500);
}
*/

/*
void printLaserImage()
{
  lcd.setCursor(0,0);
  lcd.print("Pass laser  ");
  //lcd.print("Ready       ");
  //lcd.print("            ");
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
*/

/*
void openMainMenu2()
{
  lcd.home();
  lcd.clear();
  lcd.print("openMainMenu2");
  delay(3000);
  //if((millis() - previousPress) > buttonDebounce) // && buttonFlag
  //{
    //previousPress = millis();
    
    //if(digitalRead(selectButton) == LOW && previousState == HIGH)
    //{
      enterMenu = true;
      updateMainMenu();

      while (enterMenu == true)
      {
        if (digitalRead(downButton) == LOW)
        {
          mainMenu++;
          updateMainMenu();
          delay(100);
        }
  
        if (digitalRead(upButton) == LOW)
        {
          mainMenu--;
          updateMainMenu();
          delay(100);
        }
  
        if (digitalRead(selectButton) == LOW)
        {
          enterMenu = false;
          executeMainMenuAction();
          delay(100);
          //while (!digitalRead(selectButton));
        }
      }
      //previousState = LOW;
    //}
    
    //else if(digitalRead(selectButton) == HIGH && previousState == LOW)
    //{
      //previousState = HIGH;
    //}
    //buttonFlag = 0;
  //}
}
*/

// TIMER BEGINNING ---------------------------------------------------------------------------



// TIMER END ---------------------------------------------------------------------------------

#endif // _HEADERFILE_H    // Put this line at the end of your file.
