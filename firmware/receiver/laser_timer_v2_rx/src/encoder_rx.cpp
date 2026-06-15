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
static bool suppressUntilRelease = false;

void encoderInit() {
  pinMode(encoderCLK, INPUT);
  pinMode(encoderDAT, INPUT);
  pinMode(encoderButton, INPUT_PULLUP);
  lastStateCLK = digitalRead(encoderCLK);
}

EncoderEvent pollEncoder() {
  EncoderEvent event = EncNone;
  unsigned long now = millis();
  bool buttonDown = digitalRead(encoderButton) == LOW;

  if (suppressUntilRelease) {
    lastStateCLK = digitalRead(encoderCLK);
    if (!buttonDown) {
      suppressUntilRelease = false;
      buttonWasDown = false;
      pressHandled = true;
    } else {
      buttonWasDown = true;
    }
    return EncNone;
  }

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
  if (digitalRead(encoderButton) == LOW) {
    suppressUntilRelease = true;
    buttonWasDown = true;
  } else {
    buttonWasDown = false;
  }
  pressHandled = true;
}
