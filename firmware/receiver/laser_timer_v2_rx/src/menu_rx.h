/**
 * menu_rx.h
 * Laser Timer Firmware v2 — non-blocking table-driven menu system
 */

#ifndef MENU_RX_H
#define MENU_RX_H

#include <Arduino.h>

enum MenuScreen : uint8_t {
  MS_Idle = 0,
  MS_Splash,
  MS_Main,
  MS_Distance,
  MS_Custom,
  MS_Radio,
  MS_Buzzer,
  MS_RePairing
};

void menuInit();
void menuRequestOpen();
bool menuIsActive();
bool menuIsRePairing();
void menuRefresh();
void menuTick();

#endif
