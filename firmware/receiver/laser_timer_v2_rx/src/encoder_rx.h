/**
 * encoder_rx.h
 * Laser Timer Firmware v2 — rotary encoder input
 */

#ifndef ENCODER_RX_H
#define ENCODER_RX_H

#include <Arduino.h>

enum EncoderEvent : uint8_t {
  EncNone = 0,
  EncCW,
  EncCCW,
  EncPress,
  EncLongPress
};

extern const int encoderButton;
extern const byte encoderCLK;
extern const byte encoderDAT;

#define ENCODER_STEP_DEBOUNCE_MS 50
#define ENCODER_BUTTON_DEBOUNCE_MS 50
#define ENCODER_LONG_PRESS_MS 800

void encoderInit();
EncoderEvent pollEncoder();
void waitForEncoderRelease();

#endif
