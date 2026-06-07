/**
 * encoder_rx.cpp
 * Laser Timer Firmware v2 — rotary encoder input
 */

#include "encoder_rx.h"

const int encoderButton = 3;
const byte encoderCLK = 4;
const byte encoderDAT = 5;

static int lastStateCLK = 0;
static unsigned long lastStepMs = 0;
static unsigned long buttonDownMs = 0;
static bool buttonWasDown = false;
static bool pressHandled = false;

void encoderInit() {
  pinMode(encoderCLK, INPUT);
  pinMode(encoderDAT, INPUT);
  pinMode(encoderButton, INPUT_PULLUP);
  lastStateCLK = digitalRead(encoderCLK);
}

EncoderEvent pollEncoder() {
  EncoderEvent event = EncNone;
  unsigned long now = millis();

  int currentStateCLK = digitalRead(encoderCLK);
  if (currentStateCLK != lastStateCLK && currentStateCLK == HIGH) {
    if (now - lastStepMs >= ENCODER_STEP_DEBOUNCE_MS) {
      lastStepMs = now;
      if (digitalRead(encoderDAT) != currentStateCLK) {
        event = EncCW;
      } else {
        event = EncCCW;
      }
    }
  }
  lastStateCLK = currentStateCLK;

  bool buttonDown = digitalRead(encoderButton) == LOW;
  if (buttonDown && !buttonWasDown) {
    buttonDownMs = now;
    pressHandled = false;
  }

  if (buttonDown && !pressHandled) {
    if (now - buttonDownMs >= ENCODER_LONG_PRESS_MS) {
      pressHandled = true;
      event = EncLongPress;
    }
  }

  if (!buttonDown && buttonWasDown && !pressHandled) {
    if (now - buttonDownMs >= ENCODER_BUTTON_DEBOUNCE_MS) {
      event = EncPress;
    }
    pressHandled = true;
  }

  buttonWasDown = buttonDown;
  return event;
}

void waitForEncoderRelease() {
  while (digitalRead(encoderButton) == LOW) {
    delay(1);
  }
  buttonWasDown = false;
  pressHandled = true;
}
