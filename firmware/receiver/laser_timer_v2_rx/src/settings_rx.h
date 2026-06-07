/**
 * settings_rx.h
 * Laser Timer Firmware v2 — receiver persisted settings
 */

#ifndef SETTINGS_RX_H
#define SETTINGS_RX_H

#include "macros_laser_timer_v2_rx.h"
#include <Arduino.h>

#define SETTINGS_EEPROM_MAGIC 0x52
#define SETTINGS_EEPROM_ADDR 0

extern char distance;
extern int distance_in_yards;
extern int custom_distance_yards;
extern bool buzzer_align_enabled;
extern bool buzzer_finish_enabled;
extern bool use_meters;

void loadSettings();
void saveSettings();

#endif
