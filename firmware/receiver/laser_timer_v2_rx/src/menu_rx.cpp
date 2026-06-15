/**
 * menu_rx.cpp
 * Laser Timer Firmware v2 — non-blocking table-driven menu system
 */

#include "menu_rx.h"
#include "display_rx.h"
#include "encoder_rx.h"
#include "settings_rx.h"
#include "macros_laser_timer_v2_rx.h"
#include "radio_protocol_v2.h"
#include <RF24.h>
#include <LiquidCrystal_I2C.h>
#include <stdio.h>
#include <string.h>

extern LiquidCrystal_I2C lcd;
extern char timer_state;
extern const byte gate2_pin;
extern bool gate2BeamWasBroken;
extern unsigned long finishedUntilMs;
extern unsigned long lastRadioRxMs;
extern RF24 radio;
extern const byte addresses[][6];
extern bool radioReady;

void PollRadio();
void clearLine1();
void showPairingScreen();
void showPairingSuccess();
void resumeRxListening();
void restoreIdleDisplay();

enum MainMenuItem : uint8_t {
  MM_Stopwatch = 0,
  MM_SpeedSettings,
  MM_Radio,
  MM_Buzzer,
  MM_Back,
  MM_COUNT
};

enum DistanceMenuItem : uint8_t {
  DM_SpeedToggle = 0,
  DM_Yards40,
  DM_Yards10,
  DM_Yards5,
  DM_Yards2,
  DM_Yards1,
  DM_Custom,
  DM_Units,
  DM_Back,
  DM_COUNT
};

enum RadioMenuItem : uint8_t {
  RM_Status = 0,
  RM_RePair,
  RM_Back,
  RM_COUNT
};

enum BuzzerMenuItem : uint8_t {
  BM_Align = 0,
  BM_Finish,
  BM_Back,
  BM_COUNT
};

struct MenuItem {
  void (*onSelect)(uint8_t index);
};

static void actionMainSelect(uint8_t index);
static void actionDistanceSelect(uint8_t index);
static void actionRadioSelect(uint8_t index);
static void actionBuzzerSelect(uint8_t index);
static void getMainItemLabel(uint8_t index, char* buf, size_t bufSize);
static void getDistanceItemLabel(uint8_t index, char* buf, size_t bufSize);
static void getRadioItemLabel(uint8_t index, char* buf, size_t bufSize);
static void getBuzzerItemLabel(uint8_t index, char* buf, size_t bufSize);

static MenuScreen menuScreen = MS_Idle;
static uint8_t menuIndex = 0;
static bool menuOpenRequested = false;
static unsigned long splashUntilMs = 0;
static MenuScreen splashNextScreen = MS_Main;
static uint8_t splashNextIndex = 0;
static int customEditYards = 40;

static uint8_t clampIndex(int index, uint8_t count) {
  if (index < 0) {
    return 0;
  }
  if (index >= (int)count) {
    return count - 1;
  }
  return (uint8_t)index;
}

static void appendInt(char* buf, uint8_t* pos, int value) {
  if (value >= 10) {
    buf[(*pos)++] = (char)('0' + (value / 10));
  }
  buf[(*pos)++] = (char)('0' + (value % 10));
}

static int yardsToDisplayMeters(int yards) {
  return (int)(yards * 0.9144f + 0.5f);
}

static void formatDistanceItemLabel(char* buf, size_t bufSize, int yards) {
  const char marker = (distance_in_yards == yards) ? '*' : ' ';
  if (use_meters) {
    snprintf(buf, bufSize, "%dm %c", yardsToDisplayMeters(yards), marker);
  } else {
    snprintf(buf, bufSize, "%d yd %c", yards, marker);
  }
}

static void formatCustomDistanceLabel(char* buf, size_t bufSize, int yards) {
  const char marker = (distance_in_yards == yards) ? '*' : ' ';
  if (use_meters) {
    snprintf(buf, bufSize, "Custom %dm %c", yardsToDisplayMeters(yards), marker);
  } else {
    snprintf(buf, bufSize, "Custom %d yd %c", yards, marker);
  }
}

static void formatCustomEditLabel(char* buf, size_t bufSize, int yards) {
  if (use_meters) {
    snprintf(buf, bufSize, "Set: %d m", yardsToDisplayMeters(yards));
  } else {
    snprintf(buf, bufSize, "Set: %d yd", yards);
  }
}

static void formatDistancePreviewLine(char* buf, size_t bufSize, uint8_t index, uint8_t count, int yards) {
  uint8_t pos = 0;
  int speedTenths;

  buf[pos++] = (char)('0' + (index + 1));
  buf[pos++] = '/';
  buf[pos++] = (char)('0' + count);
  buf[pos++] = ' ';

  if (use_meters) {
    speedTenths = (int)((yards * 329184L + 25000L) / 50000L);
    appendInt(buf, &pos, speedTenths / 10);
    buf[pos++] = '.';
    buf[pos++] = (char)('0' + (speedTenths % 10));
    strncpy(buf + pos, "KPH@5s", bufSize - pos - 1);
  } else {
    speedTenths = (yards * 90 + 11) / 22;
    appendInt(buf, &pos, speedTenths / 10);
    buf[pos++] = '.';
    buf[pos++] = (char)('0' + (speedTenths % 10));
    strncpy(buf + pos, "MPH@5s", bufSize - pos - 1);
  }
  buf[bufSize - 1] = '\0';
}

static const MenuItem MAIN_ITEMS[MM_COUNT] = {
  {actionMainSelect},
  {actionMainSelect},
  {actionMainSelect},
  {actionMainSelect},
  {actionMainSelect},
};

static const MenuItem DISTANCE_ITEMS[DM_COUNT] = {
  {actionDistanceSelect},
  {actionDistanceSelect},
  {actionDistanceSelect},
  {actionDistanceSelect},
  {actionDistanceSelect},
  {actionDistanceSelect},
  {actionDistanceSelect},
  {actionDistanceSelect},
  {actionDistanceSelect},
};

static void getMainItemLabel(uint8_t index, char* buf, size_t bufSize) {
  switch (index) {
    case MM_Stopwatch:
      strncpy(buf, "Stopwatch", bufSize);
      break;
    case MM_SpeedSettings:
      strncpy(buf, "Speed", bufSize);
      break;
    case MM_Radio:
      strncpy(buf, "Radio", bufSize);
      break;
    case MM_Buzzer:
      strncpy(buf, "Buzzer", bufSize);
      break;
    case MM_Back:
      strncpy(buf, "Back", bufSize);
      break;
    default:
      buf[0] = '\0';
      break;
  }
  buf[bufSize - 1] = '\0';
}

static void getDistanceItemLabel(uint8_t index, char* buf, size_t bufSize) {
  static const int presets[] = {40, 10, 5, 2, 1};

  switch (index) {
    case DM_SpeedToggle:
      snprintf(buf, bufSize, "Speed %s", distance == ENABLED ? "ON" : "OFF");
      break;
    case DM_Yards40:
    case DM_Yards10:
    case DM_Yards5:
    case DM_Yards2:
    case DM_Yards1:
      formatDistanceItemLabel(buf, bufSize, presets[index - DM_Yards40]);
      break;
    case DM_Custom:
      formatCustomDistanceLabel(buf, bufSize, custom_distance_yards);
      break;
    case DM_Units:
      snprintf(buf, bufSize, "Units %s", use_meters ? "meters" : "yards");
      break;
    case DM_Back:
      strncpy(buf, "Back", bufSize);
      break;
    default:
      buf[0] = '\0';
      break;
  }
  buf[bufSize - 1] = '\0';
}

static void getRadioItemLabel(uint8_t index, char* buf, size_t bufSize) {
  switch (index) {
    case RM_Status: {
      bool linked = lastRadioRxMs != 0 && (millis() - lastRadioRxMs <= RADIO_LINK_TIMEOUT_MS);
      strncpy(buf, linked ? "Link OK" : "No link", bufSize);
      break;
    }
    case RM_RePair:
      strncpy(buf, "Re-pair", bufSize);
      break;
    case RM_Back:
      strncpy(buf, "Back", bufSize);
      break;
    default:
      buf[0] = '\0';
      break;
  }
  buf[bufSize - 1] = '\0';
}

static void getBuzzerItemLabel(uint8_t index, char* buf, size_t bufSize) {
  switch (index) {
    case BM_Align:
      snprintf(buf, bufSize, "Align %s", buzzer_align_enabled ? "ON" : "OFF");
      break;
    case BM_Finish:
      snprintf(buf, bufSize, "Finish %s", buzzer_finish_enabled ? "ON" : "OFF");
      break;
    case BM_Back:
      strncpy(buf, "Back", bufSize);
      break;
    default:
      buf[0] = '\0';
      break;
  }
  buf[bufSize - 1] = '\0';
}

static void renderCurrentMenu() {
  char itemLabel[LCD_COLS + 1];
  char line[LCD_COLS + 1];

  switch (menuScreen) {
    case MS_Main:
      lcdPrintMenuHeading("Main Menu");
      getMainItemLabel(menuIndex, itemLabel, sizeof(itemLabel));
      lcdPrintMenuItemLine(itemLabel, menuIndex, MM_COUNT);
      break;
    case MS_Distance:
      if (menuIndex >= DM_Yards40 && menuIndex <= DM_Yards1) {
        static const int presets[] = {40, 10, 5, 2, 1};
        int yards = presets[menuIndex - DM_Yards40];
        formatDistanceItemLabel(line, sizeof(line), yards);
        lcdPrintLine(0, line);
        formatDistancePreviewLine(line, sizeof(line), menuIndex, DM_COUNT, yards);
        lcdPrintLine(1, line);
      } else {
        lcdPrintMenuHeading("Speed Settings");
        getDistanceItemLabel(menuIndex, itemLabel, sizeof(itemLabel));
        lcdPrintMenuItemLine(itemLabel, menuIndex, DM_COUNT);
      }
      break;
    case MS_Radio:
      lcdPrintMenuHeading("Radio");
      getRadioItemLabel(menuIndex, itemLabel, sizeof(itemLabel));
      lcdPrintMenuItemLine(itemLabel, menuIndex, RM_COUNT);
      break;
    case MS_Buzzer:
      lcdPrintMenuHeading("Buzzer");
      getBuzzerItemLabel(menuIndex, itemLabel, sizeof(itemLabel));
      lcdPrintMenuItemLine(itemLabel, menuIndex, BM_COUNT);
      break;
    case MS_Custom:
      formatCustomEditLabel(line, sizeof(line), customEditYards);
      lcdPrintLine(0, line);
      snprintf(itemLabel, sizeof(itemLabel), "%u/%u turn|press", (unsigned)(DM_Custom + 1), DM_COUNT);
      lcdPrintLine(1, itemLabel);
      break;
    default:
      break;
  }
}

static void enterScreen(MenuScreen screen, uint8_t index = 0) {
  menuScreen = screen;
  menuIndex = index;
  if (screen == MS_Custom) {
    customEditYards = custom_distance_yards;
  }
  renderCurrentMenu();
}

static void setDistanceYards(int yards, const char* toast) {
  if (distance_in_yards == yards) {
    showToast(toast, "unchanged", MENU_TOAST_MS);
    return;
  }
  distance_in_yards = yards;
  saveSettings();
  showToast(toast, "", MENU_TOAST_MS);
}

static void actionMainSelect(uint8_t index) {
  switch (index) {
    case MM_Stopwatch:
      timer_state = ENABLED;
      finishedUntilMs = 0;
      gate2BeamWasBroken = (digitalRead(gate2_pin) == GATE_ACTIVATED);
      clearLine1();
      if (radioReady) {
        radio.flush_rx();
      }
      showToast("Starting...", "", MENU_TOAST_MS);
      menuScreen = MS_Idle;
      break;
    case MM_SpeedSettings:
      enterScreen(MS_Distance, 0);
      break;
    case MM_Radio:
      enterScreen(MS_Radio, 0);
      break;
    case MM_Buzzer:
      enterScreen(MS_Buzzer, 0);
      break;
    case MM_Back:
      menuScreen = MS_Idle;
      restoreIdleDisplay();
      break;
    default:
      break;
  }
}

static void actionDistanceSelect(uint8_t index) {
  switch (index) {
    case DM_SpeedToggle:
      distance = (distance == ENABLED) ? DISABLED : ENABLED;
      saveSettings();
      showToast(distance == ENABLED ? "Speed ON" : "Speed OFF", "", MENU_TOAST_MS);
      renderCurrentMenu();
      break;
    case DM_Yards40:
      setDistanceYards(40, "40 yd set");
      renderCurrentMenu();
      break;
    case DM_Yards10:
      setDistanceYards(10, "10 yd set");
      renderCurrentMenu();
      break;
    case DM_Yards5:
      setDistanceYards(5, "5 yd set");
      renderCurrentMenu();
      break;
    case DM_Yards2:
      setDistanceYards(2, "2 yd set");
      renderCurrentMenu();
      break;
    case DM_Yards1:
      setDistanceYards(1, "1 yd set");
      renderCurrentMenu();
      break;
    case DM_Custom:
      enterScreen(MS_Custom, 0);
      break;
    case DM_Units:
      use_meters = !use_meters;
      saveSettings();
      showToast(use_meters ? "Units: meters" : "Units: yards", "", MENU_TOAST_MS);
      renderCurrentMenu();
      break;
    case DM_Back:
      enterScreen(MS_Main, MM_SpeedSettings);
      break;
    default:
      break;
  }
}

static void actionRadioSelect(uint8_t index) {
  switch (index) {
    case RM_Status:
      renderCurrentMenu();
      break;
    case RM_RePair:
      menuScreen = MS_RePairing;
      showPairingScreen();
      break;
    case RM_Back:
      enterScreen(MS_Main, MM_Radio);
      break;
    default:
      break;
  }
}

static void actionBuzzerSelect(uint8_t index) {
  switch (index) {
    case BM_Align:
      buzzer_align_enabled = !buzzer_align_enabled;
      saveSettings();
      showToast(buzzer_align_enabled ? "Align beep ON" : "Align beep OFF", "", MENU_TOAST_MS);
      renderCurrentMenu();
      break;
    case BM_Finish:
      buzzer_finish_enabled = !buzzer_finish_enabled;
      saveSettings();
      showToast(buzzer_finish_enabled ? "Finish beep ON" : "Finish beep OFF", "", MENU_TOAST_MS);
      renderCurrentMenu();
      break;
    case BM_Back:
      enterScreen(MS_Main, MM_Buzzer);
      break;
    default:
      break;
  }
}

static void handleEncoderNav(EncoderEvent event) {
  if (toastActive()) {
    return;
  }

  uint8_t itemCount = 0;
  switch (menuScreen) {
    case MS_Main:
      itemCount = MM_COUNT;
      break;
    case MS_Distance:
      itemCount = DM_COUNT;
      break;
    case MS_Radio:
      itemCount = RM_COUNT;
      break;
    case MS_Buzzer:
      itemCount = BM_COUNT;
      break;
    case MS_Custom:
      if (event == EncCW) {
        if (customEditYards < 99) {
          customEditYards++;
        }
        renderCurrentMenu();
      } else if (event == EncCCW) {
        if (customEditYards > 1) {
          customEditYards--;
        }
        renderCurrentMenu();
      } else if (event == EncPress) {
        custom_distance_yards = customEditYards;
        distance_in_yards = customEditYards;
        saveSettings();
        showToast("Custom set", "", MENU_TOAST_MS);
        enterScreen(MS_Distance, DM_Custom);
        waitForEncoderRelease();
      }
      return;
    default:
      return;
  }

  if (event == EncCW) {
    menuIndex = clampIndex(menuIndex + 1, itemCount);
    renderCurrentMenu();
  } else if (event == EncCCW) {
    menuIndex = clampIndex(menuIndex - 1, itemCount);
    renderCurrentMenu();
  } else if (event == EncPress) {
    switch (menuScreen) {
      case MS_Main:
        MAIN_ITEMS[menuIndex].onSelect(menuIndex);
        break;
      case MS_Distance:
        DISTANCE_ITEMS[menuIndex].onSelect(menuIndex);
        break;
      case MS_Radio:
        actionRadioSelect(menuIndex);
        break;
      case MS_Buzzer:
        actionBuzzerSelect(menuIndex);
        break;
      default:
        break;
    }
    waitForEncoderRelease();
  } else if (event == EncLongPress) {
    menuScreen = MS_Idle;
    restoreIdleDisplay();
    waitForEncoderRelease();
  }
}

static void tickRePairing() {
  if (!radioReady) {
    return;
  }

  while (radio.available()) {
    RadioPacket pkt;
    radio.read(&pkt, sizeof(pkt));
    if (pkt.magic == RADIO_MAGIC) {
      lastRadioRxMs = millis();
      showPairingSuccess();
      splashUntilMs = millis() + RADIO_PAIR_OK_MS;
      splashNextScreen = MS_Main;
      splashNextIndex = 0;
      menuScreen = MS_Splash;
      resumeRxListening();
    }
  }
}

void menuInit() {
  menuScreen = MS_Idle;
  menuIndex = 0;
  menuOpenRequested = false;
}

void menuRequestOpen() {
  menuIndex = 0;
  splashNextScreen = MS_Main;
  splashNextIndex = 0;
  menuOpenRequested = true;
}

bool menuIsActive() {
  return menuScreen != MS_Idle && menuScreen != MS_Splash;
}

bool menuIsRePairing() {
  return menuScreen == MS_RePairing;
}

void menuRefresh() {
  if (menuScreen != MS_Idle && menuScreen != MS_Splash && menuScreen != MS_RePairing) {
    renderCurrentMenu();
  }
}

void menuTick() {
  if (menuScreen == MS_Splash) {
    if (millis() >= splashUntilMs) {
      enterScreen(splashNextScreen, splashNextIndex);
    }
    return;
  }

  if (menuScreen == MS_RePairing) {
    tickRePairing();
    EncoderEvent event = pollEncoder();
    if (event == EncLongPress || event == EncPress) {
      enterScreen(MS_Radio, RM_RePair);
      waitForEncoderRelease();
    }
    return;
  }

  if (menuOpenRequested && menuScreen == MS_Idle) {
    menuOpenRequested = false;
    enterScreen(MS_Main, 0);
    return;
  }

  if (menuScreen == MS_Idle) {
    return;
  }

  PollRadio();
  EncoderEvent event = pollEncoder();
  if (event != EncNone) {
    handleEncoderNav(event);
  }
}
