/**
 * settings_rx.cpp
 * Laser Timer Firmware v2 — EEPROM-backed receiver settings
 */

#include "settings_rx.h"
#include <EEPROM.h>

char distance = ENABLED;
int distance_in_yards = 40;
int custom_distance_yards = 40;
bool buzzer_align_enabled = true;
bool buzzer_finish_enabled = true;
bool use_meters = false;

struct SettingsBlock {
  uint8_t magic;
  char distance;
  int16_t distance_in_yards;
  int16_t custom_distance_yards;
  uint8_t buzzer_align;
  uint8_t buzzer_finish;
  uint8_t use_meters;
};

void loadSettings() {
  SettingsBlock block;
  EEPROM.get(SETTINGS_EEPROM_ADDR, block);

  if (block.magic != SETTINGS_EEPROM_MAGIC) {
    return;
  }

  distance = block.distance;
  distance_in_yards = block.distance_in_yards;
  custom_distance_yards = block.custom_distance_yards;
  buzzer_align_enabled = block.buzzer_align != 0;
  buzzer_finish_enabled = block.buzzer_finish != 0;
  use_meters = block.use_meters != 0;
}

void saveSettings() {
  SettingsBlock block;
  block.magic = SETTINGS_EEPROM_MAGIC;
  block.distance = distance;
  block.distance_in_yards = distance_in_yards;
  block.custom_distance_yards = custom_distance_yards;
  block.buzzer_align = buzzer_align_enabled ? 1 : 0;
  block.buzzer_finish = buzzer_finish_enabled ? 1 : 0;
  block.use_meters = use_meters ? 1 : 0;
  EEPROM.put(SETTINGS_EEPROM_ADDR, block);
}
