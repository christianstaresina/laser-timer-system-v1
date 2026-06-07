/**
 * display_rx.cpp
 * Laser Timer Firmware v2 — LCD formatting and toast helpers
 */

#include "display_rx.h"
#include <stdio.h>
#include <string.h>

static unsigned long toastUntilMs = 0;
static char toastLine0[LCD_COLS + 1];
static char toastLine1[LCD_COLS + 1];

void lcdPrintLine(uint8_t row, const char* text) {
  lcd.setCursor(0, row);
  uint8_t len = 0;
  while (text[len] != '\0' && len < LCD_COLS) {
    len++;
  }
  lcd.print(text);
  while (len < LCD_COLS) {
    lcd.print(' ');
    len++;
  }
}

void lcdPrintMenuHeading(const char* heading) {
  lcdPrintLine(0, heading);
}

void lcdPrintMenuItemLine(const char* item, uint8_t index, uint8_t count) {
  char line[LCD_COLS + 1];
  uint8_t pos = 0;

  line[pos++] = (char)('0' + (index + 1));
  line[pos++] = '/';
  line[pos++] = (char)('0' + count);
  line[pos++] = ' ';

  while (*item != '\0' && pos < LCD_COLS) {
    line[pos++] = *item++;
  }
  line[pos] = '\0';
  lcdPrintLine(1, line);
}

void showMenuSplash(const char* title) {
  lcdPrintLine(0, title);
  lcdPrintLine(1, "");
}

void showToast(const char* line0, const char* line1, unsigned long durationMs) {
  strncpy(toastLine0, line0, LCD_COLS);
  toastLine0[LCD_COLS] = '\0';
  strncpy(toastLine1, line1, LCD_COLS);
  toastLine1[LCD_COLS] = '\0';
  toastUntilMs = millis() + durationMs;
  lcdPrintLine(0, toastLine0);
  lcdPrintLine(1, toastLine1);
}

void toastTick() {
  if (toastUntilMs == 0) {
    return;
  }
  if (millis() >= toastUntilMs) {
    toastUntilMs = 0;
  }
}

bool toastActive() {
  return toastUntilMs != 0 && millis() < toastUntilMs;
}
