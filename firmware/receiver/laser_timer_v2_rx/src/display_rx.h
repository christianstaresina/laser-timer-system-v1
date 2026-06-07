/**
 * display_rx.h
 * Laser Timer Firmware v2 — LCD formatting and toast helpers
 */

#ifndef DISPLAY_RX_H
#define DISPLAY_RX_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

extern LiquidCrystal_I2C lcd;

#define LCD_COLS 16
#define LCD_ROWS 2
#define MENU_TOAST_MS 300

void lcdPrintLine(uint8_t row, const char* text);
void lcdPrintMenuHeading(const char* heading);
void lcdPrintMenuItemLine(const char* item, uint8_t index, uint8_t count);
void showMenuSplash(const char* title);
void showToast(const char* line0, const char* line1, unsigned long durationMs);
void toastTick();
bool toastActive();

#endif
