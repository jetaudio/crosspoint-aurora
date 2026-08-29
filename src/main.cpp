#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <Wire.h>
#if FREEINK_DEVICE_LILYGO
#include <BoardT5S3.h>
#endif
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <PowerManager.h>
#include <SPI.h>
#include <WiFi.h>
#include <XteinkDetect.h>
#include <builtinFonts/all.h>
#if FREEINK_CAP_TOUCH
#include <esp_sleep.h>
#include <esp_sntp.h>
#endif

#include <array>
#include <cstring>

#include "BatteryLog.h"
#include "ConfigurableKeys.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "SystemFont.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/LoadingIcon.h"
#include "platform/UsbSerialJtagHandoff.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenOrientation.h"
#include "util/ScreenshotUtil.h"

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());
static unsigned long allowSleepAt = 0;
static unsigned long lastX4ProPowerClickAt = 0;

namespace {
constexpr unsigned long X4PRO_POWER_DOUBLE_CLICK_MS = 500;
constexpr unsigned long X4PRO_POWER_CLICK_MAX_HOLD_MS = 300;
}  // namespace

// A wake hold must never become an in-app power-button action.  Boot may continue
// while the button is held; swallow the one release that ends that wake gesture.
static bool wakePowerReleasePending = false;

// Fonts
EpdFont notoserif14RegularFont(&notoserif_14_regular);
EpdFont notoserif14BoldFont(&notoserif_14_bold);
EpdFont notoserif14ItalicFont(&notoserif_14_italic);
EpdFont notoserif14BoldItalicFont(&notoserif_14_bolditalic);
EpdFontFamily notoserif14FontFamily(&notoserif14RegularFont, &notoserif14BoldFont, &notoserif14ItalicFont,
                                    &notoserif14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont notoserif12RegularFont(&notoserif_12_regular);
EpdFont notoserif12BoldFont(&notoserif_12_bold);
EpdFont notoserif12ItalicFont(&notoserif_12_italic);
EpdFont notoserif12BoldItalicFont(&notoserif_12_bolditalic);
EpdFontFamily notoserif12FontFamily(&notoserif12RegularFont, &notoserif12BoldFont, &notoserif12ItalicFont,
                                    &notoserif12BoldItalicFont);
EpdFont notoserif16RegularFont(&notoserif_16_regular);
EpdFont notoserif16BoldFont(&notoserif_16_bold);
EpdFont notoserif16ItalicFont(&notoserif_16_italic);
EpdFont notoserif16BoldItalicFont(&notoserif_16_bolditalic);
EpdFontFamily notoserif16FontFamily(&notoserif16RegularFont, &notoserif16BoldFont, &notoserif16ItalicFont,
                                    &notoserif16BoldItalicFont);
EpdFont notoserif18RegularFont(&notoserif_18_regular);
EpdFont notoserif18BoldFont(&notoserif_18_bold);
EpdFont notoserif18ItalicFont(&notoserif_18_italic);
EpdFont notoserif18BoldItalicFont(&notoserif_18_bolditalic);
EpdFontFamily notoserif18FontFamily(&notoserif18RegularFont, &notoserif18BoldFont, &notoserif18ItalicFont,
                                    &notoserif18BoldItalicFont);

EpdFont notosans12RegularFont(&notosans_12_regular);
EpdFont notosans12BoldFont(&notosans_12_bold);
EpdFont notosans12ItalicFont(&notosans_12_italic);
EpdFont notosans12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily notosans12FontFamily(&notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);
EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFont notosans14ItalicFont(&notosans_14_italic);
EpdFont notosans14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);
EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFont notosans16ItalicFont(&notosans_16_italic);
EpdFont notosans16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);
EpdFont notosans18RegularFont(&notosans_18_regular);
EpdFont notosans18BoldFont(&notosans_18_bold);
EpdFont notosans18ItalicFont(&notosans_18_italic);
EpdFont notosans18BoldItalicFont(&notosans_18_bolditalic);
EpdFontFamily notosans18FontFamily(&notosans18RegularFont, &notosans18BoldFont, &notosans18ItalicFont,
                                   &notosans18BoldItalicFont);

#endif  // OMIT_FONTS

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

// System (UI) font faces. Two selectable cuts back the UI_*_FONT_ID slots used
// by every menu/status/home screen; applySystemUiFont() registers the one chosen
// by SETTINGS.systemFont. Size is fixed (10 / 12) — only the typeface changes.
//   Noto Sans  — the default Aurora look (notosansui_* + Hebrew fallback).
//   Ubuntu     — upstream's Medium UI weight (ubuntu_*_medium), whose fontstack
//                already carries the Vietnamese cut plus Hebrew and Arabic
//                fallbacks, so aurora no longer needs its own Ubuntu-VN build.
EpdFont uiNoto10RegularFont(&notosansui_10_regular);
EpdFont uiNoto10BoldFont(&notosansui_10_bold);
EpdFontFamily uiNoto10FontFamily(&uiNoto10RegularFont, &uiNoto10BoldFont);
EpdFont uiNoto12RegularFont(&notosansui_12_regular);
EpdFont uiNoto12BoldFont(&notosansui_12_bold);
EpdFontFamily uiNoto12FontFamily(&uiNoto12RegularFont, &uiNoto12BoldFont);

EpdFont uiUbuntu10RegularFont(&ubuntu_10_medium);
EpdFont uiUbuntu10BoldFont(&ubuntu_10_bold);
EpdFontFamily uiUbuntu10FontFamily(&uiUbuntu10RegularFont, &uiUbuntu10BoldFont);
EpdFont uiUbuntu12RegularFont(&ubuntu_12_medium);
EpdFont uiUbuntu12BoldFont(&ubuntu_12_bold);
EpdFontFamily uiUbuntu12FontFamily(&uiUbuntu12RegularFont, &uiUbuntu12BoldFont);

EpdFont uiGaramond10RegularFont(&ebgaramond_10_regular);
EpdFont uiGaramond10BoldFont(&ebgaramond_10_bold);
EpdFontFamily uiGaramond10FontFamily(&uiGaramond10RegularFont, &uiGaramond10BoldFont);
EpdFont uiGaramond12RegularFont(&ebgaramond_12_regular);
EpdFont uiGaramond12BoldFont(&ebgaramond_12_bold);
EpdFontFamily uiGaramond12FontFamily(&uiGaramond12RegularFont, &uiGaramond12BoldFont);

EpdFont uiGoudy10RegularFont(&sfugoudy_10_regular);
EpdFont uiGoudy10BoldFont(&sfugoudy_10_bold);
EpdFontFamily uiGoudy10FontFamily(&uiGoudy10RegularFont, &uiGoudy10BoldFont);
EpdFont uiGoudy12RegularFont(&sfugoudy_12_regular);
EpdFont uiGoudy12BoldFont(&sfugoudy_12_bold);
EpdFontFamily uiGoudy12FontFamily(&uiGoudy12RegularFont, &uiGoudy12BoldFont);

// Register the UI face selected by SETTINGS.systemFont into the UI_*_FONT_ID
// slots and drop cached glyphs so the swap is visible immediately. Called once
// at boot and again whenever the System Font setting changes.
void applySystemUiFont() {
  const EpdFontFamily* f10 = &uiNoto10FontFamily;
  const EpdFontFamily* f12 = &uiNoto12FontFamily;
  switch (SETTINGS.systemFont) {
    case CrossPointSettings::SYS_FONT_UBUNTU:
      f10 = &uiUbuntu10FontFamily;
      f12 = &uiUbuntu12FontFamily;
      break;
    case CrossPointSettings::SYS_FONT_EB_GARAMOND:
      f10 = &uiGaramond10FontFamily;
      f12 = &uiGaramond12FontFamily;
      break;
    case CrossPointSettings::SYS_FONT_SFU_GOUDY:
      f10 = &uiGoudy10FontFamily;
      f12 = &uiGoudy12FontFamily;
      break;
    default:
      break;
  }
  renderer.replaceFont(UI_10_FONT_ID, *f10);
  renderer.replaceFont(UI_12_FONT_ID, *f12);
  fontCacheManager.clearCache();
}

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
// Light-sleep probe results, in RTC memory rather than printed live. Entering
// light sleep kills the USB-Serial/JTAG console for good (the device keeps
// running fine -- only the console dies, until the cable is replugged), so the
// probe cannot report over the same channel it just switched off. RTC_NOINIT
// survives both a bare console loss and a reset, covering either recovery.
#define LSLEEP_RESULT_MAGIC 0x4C534C50u  // 'LSLP'
RTC_NOINIT_ATTR uint32_t lsleepMagic;
RTC_NOINIT_ATTR uint32_t lsleepIterations;
RTC_NOINIT_ATTR uint32_t lsleepElapsedMs;
RTC_NOINIT_ATTR uint32_t lsleepRemCapStart;
RTC_NOINIT_ATTR uint32_t lsleepRemCapEnd;
RTC_NOINIT_ATTR uint32_t lsleepTimerWakes;
RTC_NOINIT_ATTR uint32_t lsleepGpioWakes;
RTC_NOINIT_ATTR int32_t lsleepCurrentMa;
RTC_NOINIT_ATTR uint32_t lsleepReachedEnd;  // 0 until the loop actually finished

RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash. See setup() for the resolution.
enum class BootResume : uint8_t {
  Splash,          // cold boot, flash, panic, or plain reboot
  Silent,          // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  SplashlessWake,  // wake from deep sleep with the splash suppressed by the SD flag
};

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
static bool deepSleepInProgress = false;

// The inactivity clock. Lives here rather than inside loop() because two things
// besides the loop need it: the auto-sleep timeout and the idle light sleep both
// hang off it, and CMD:IDLE reports it. Anything that resets this suppresses
// BOTH sleeps, which is a lot of consequence for a line that used to be one
// unnamed condition in a six-term ||.
static unsigned long lastActivityTime = 0;
// Which of those six terms fired on the pass that last reset the clock. Purely
// diagnostic, and the reason this file now names them: a reader whose idle timer
// is being held open by a phantom input looks, from the outside, exactly like a
// reader whose light sleep setting is off.
static const char* lastActivitySource = "boot";

#if FREEINK_CAP_TOUCH
static bool finishWifiSessionWithoutRestart() {
  if (!BoardConfig::hasTouch()) return false;

  // A software reset does not cycle externally powered touch/frontlight rails.
  // Shut down the network stack in place so those peripherals retain state.
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  WiFi.mode(WIFI_OFF);
  delay(100);
  LOG_DBG("MAIN", "WiFi stopped without restart on touch device");
  return true;
}
#endif

void silentRestart() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
#if FREEINK_CAP_TOUCH
  if (finishWifiSessionWithoutRestart()) return;
#endif
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=home)");
  // E-ink retains the previous frame until Home's first paint lands (~2-3s).
  // Without an overlay, users don't see the reboot and fire input through to
  // Home. Select on the default selectorIndex=0 then opens the most-recent
  // book, looking like a trampoline back to the reader they just exited.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void silentRestartToReader() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
#if FREEINK_CAP_TOUCH
  if (finishWifiSessionWithoutRestart()) return;
#endif
  silentRebootTarget = SILENT_REBOOT_TARGET_READER;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=reader)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void restartToHomeAfterStorageHandoff() {
  if (deepSleepInProgress) return;  // sleeping supersedes the storage handoff reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Restart after storage handoff (target=home)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  handoffUsbOtgToSerialJtag();
  ESP.restart();
}

bool handleX4ProFrontlightDoubleClick() {
  if (!BoardConfig::isX4Pro() || !gpio.wasReleased(HalGPIO::BTN_POWER)) {
    return false;
  }

  const unsigned long now = millis();
  if (gpio.getPowerButtonHeldTime() > X4PRO_POWER_CLICK_MAX_HOLD_MS) {
    lastX4ProPowerClickAt = 0;
    return false;
  }

  if (lastX4ProPowerClickAt == 0 || now - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
    lastX4ProPowerClickAt = now;
    return false;
  }

  lastX4ProPowerClickAt = 0;
  const bool lightOn = !Frontlight.isOn();
  Frontlight.setOn(lightOn);
  SETTINGS.frontlightOn = lightOn ? 1 : 0;
  SETTINGS.saveToFile();
  LOG_INF("LIGHT", "Frontlight toggled %s by power-button double-click", lightOn ? "on" : "off");
  return true;
}

constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";

static void saveSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForWrite("SLP", SLEEP_FRAME_FILE, file)) return;
  file.write(renderer.getFrameBuffer(), renderer.getBufferSize());
  file.close();
}

static bool loadSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForRead("SLP", SLEEP_FRAME_FILE, file)) return false;
  const size_t bufferSize = display.getBufferSize();
  const size_t bytesRead = file.read(display.getFrameBuffer(), bufferSize);
  file.close();
  if (bytesRead != bufferSize) {
    Storage.remove(SLEEP_FRAME_FILE);
    return false;
  }
  Storage.remove(SLEEP_FRAME_FILE);
  return true;
}

// Enter deep sleep mode
// `reason` is recorded in the battery log's SLEEP row. The console dies with
// the CPU, so that row is the only evidence left of which path took the device
// down -- the timeout, a key bound to Sleep, the low-battery guard or a tile.
void enterDeepSleep(bool fromTimeout = false, const char* reason = "other") {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  // Retire the render task first. Everything below runs on this task: the sleep
  // screen paints from SleepActivity::onEnter(), then the panel, the SD card and
  // the switched rails go down. A render arriving in the middle of that drives a
  // display whose power is being cut and hangs inside the driver's busy-wait
  // instead of returning — a lockup, not a crash with a backtrace. Sleep used to
  // be reached only from an idle power-button press; a control-center tile or a
  // key bound to "Sleep" can fire while the UI is still repainting.
  activityManager.stopRendering();
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  const bool isQuickResumeSleep =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);
  // Every sleep mode leaves a complete retained frame on the e-ink panel. Keep
  // it visible until the first useful reader or home paint replaces it.
  APP_STATE.showBootScreen = false;

  APP_STATE.saveToFile();
  // Last row while the card is still mounted: startDeepSleep() unmounts it, and
  // RAM is about to be lost. Marks the start of a sleep gap in the log.
  char sleepEvent[24];
  snprintf(sleepEvent, sizeof(sleepEvent), "SLEEP:%s", reason);
  BatteryLog::flushNow(sleepEvent);
  // Stamp the wall clock too: millis() restarts at zero on the wake reset, so
  // this is the only record of how long the gap that starts here actually was.
  BatteryLog::noteSleepEntry();
  LOG_INF("SLP", "Deep sleep (%s)", reason);

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;
  activityManager.goToSleep(fromTimeout);

  if (isQuickResumeSleep) {
    saveSleepFrameBuffer();
  } else if (Storage.exists(SLEEP_FRAME_FILE)) {
    // A stale Quick Resume frame must not replace the selected sleep screen during wake.
    Storage.remove(SLEEP_FRAME_FILE);
  }

  // Tear down WiFi so the modem power domain isn't held alive across deep sleep.
  // Wake from deep sleep is effectively a chip reset, so no state needs to survive.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  halTiltSensor.deepSleep();
  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

// --- Configurable button actions ---------------------------------------------
// Every physical key the T5S3 class of board offers (BOOT, the expander user
// button labelled IO48, the capacitive Home key) has a user-chosen tap action
// and hold action, drawn from one shared list (CrossPointSettings::
// BUTTON_ACTION). Actions that already have a well-defined route through the
// input layer (Back, Home, reader menu, control center, page turns) are raised
// as frame-scoped requests so the owning activity handles them exactly as it
// handles the equivalent gesture; the rest act globally right here.

// Brief on-screen confirmation for an action with no visible surface of its
// own (the touch kill-switch). Deliberately blocking: the user just pressed a
// key and the panel needs long enough to be read.
// One-shot action the control center asks for on its way out (screenshot /
// sleep tiles): it must run only after the panel has closed and the screen
// underneath has repainted, which the panel itself can't wait for.
static int deferredPanelAction = 0;
void requestActionAfterPanelClose(const int action) { deferredPanelAction = action; }

static void runDeferredPanelAction() {
  const int action = deferredPanelAction;
  if (action == 0) return;
  deferredPanelAction = 0;
  // The panel's finish() queued the repaint of the activity below; wait for
  // it so the framebuffer holds that screen, not the sheet.
  activityManager.requestUpdateAndWait();
  if (action == 1) {
    RenderLock lock;
    ScreenshotUtil::takeScreenshot(renderer);
  } else if (action == 2) {
    enterDeepSleep(false, "panel-tile");
  }
}

static void showActionToast(const char* text) {
  {
    RenderLock lock;
    GUI.drawPopup(renderer, text);
  }
  delay(900);
  activityManager.requestUpdate();
}

// Returns true when the action consumed this input frame entirely.
static bool runButtonAction(const uint8_t action) {
  switch (action) {
    case CrossPointSettings::BTN_ACT_NONE:
      return false;
    case CrossPointSettings::BTN_ACT_PAGE_NEXT:
      MappedInputManager::requestPageNext();
      return false;  // the reader consumes it in its own loop()
    case CrossPointSettings::BTN_ACT_PAGE_PREV:
      MappedInputManager::requestPagePrev();
      return false;
    case CrossPointSettings::BTN_ACT_BACK:
      MappedInputManager::requestBackAction();
      return false;
    case CrossPointSettings::BTN_ACT_HOME:
      MappedInputManager::requestHomeAction();
      return false;
    case CrossPointSettings::BTN_ACT_READER_MENU:
      MappedInputManager::requestMenuAction();
      return false;
    case CrossPointSettings::BTN_ACT_CONTROL_CENTER:
      MappedInputManager::requestControlCenterAction();
      return false;
    case CrossPointSettings::BTN_ACT_NIGHT_MODE:
      SETTINGS.screenInverted = SETTINGS.screenInverted ? 0 : 1;
      SETTINGS.saveToFile();
      // Night mode flips the panel's output polarity, so the whole frame is new
      // content: force the clean waveform rather than a differential update.
      activityManager.handleForcedRefresh();
      activityManager.requestUpdate();
      return true;
    case CrossPointSettings::BTN_ACT_REFRESH:
      if (!activityManager.handleForcedRefresh()) {
        RenderLock lock;
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      }
      return true;
    case CrossPointSettings::BTN_ACT_FRONTLIGHT: {
      if (!Frontlight.present()) return true;
      const bool lightOn = !Frontlight.isOn();
      Frontlight.setOn(lightOn);
      SETTINGS.frontlightOn = lightOn ? 1 : 0;
      SETTINGS.saveToFile();
      LOG_INF("LIGHT", "Frontlight toggled %s by button action", lightOn ? "on" : "off");
      return true;
    }
    case CrossPointSettings::BTN_ACT_TOUCH_TOGGLE: {
      // Toggles the reader's touch controls setting (same as the control center
      // tile), not a digitizer kill-switch: the UI stays tappable, only the
      // reader's page-turn taps/swipes go quiet for a palm on the glass.
      const bool enabled = SETTINGS.toggleTouchReaderControls();
      SETTINGS.saveToFile();
      LOG_INF("MAIN", "Touch reader controls %s by button action", enabled ? "enabled" : "disabled");
      showActionToast(enabled ? tr(STR_TOUCH_ENABLED) : tr(STR_TOUCH_DISABLED));
      return true;
    }
    case CrossPointSettings::BTN_ACT_SLEEP:
      if (millis() < allowSleepAt) return true;
      enterDeepSleep(false, "key-action");
      return true;
    default:
      return false;
  }
}

// Raw edge tracking for one configurable button: fires the hold action once the
// hold threshold passes (while still down, like a phone's long press) and the
// tap action on a release that never reached the threshold.
struct ConfigurableButton {
  unsigned long pressedAt = 0;
  bool down = false;
  bool longFired = false;
};

static constexpr unsigned long BUTTON_LONG_PRESS_MS = 600;

// Raised whenever a key this dispatcher owns is down or has just been let go,
// whether or not its action consumed the frame. The inactivity timer reads it.
// These keys are invisible to the ordinary activity check: GPIO10 is read
// straight off the SoC, outside HalGPIO entirely. Reading a book with one of
// them therefore looked exactly like reading nothing at all, and the device
// deep-slept mid-page one full sleep-timeout after the last *touch* -- which is
// the "it slept while I was reading" report, and the battery log agreed:
// SLEEP:timeout-300s on a pass that had counted five page turns.
// dispatchConfigurableButtons() runs later in the pass than the check, so this
// is read one pass late; against a timer measured in minutes, that is nothing.
static bool configurableKeyActivity = false;

static bool serviceConfigurableButton(ConfigurableButton& state, const bool isDown, const bool releaseEdge,
                                      const uint8_t shortAction, const uint8_t longAction) {
  if (isDown || releaseEdge) configurableKeyActivity = true;
  if (isDown && !state.down) {
    state.down = true;
    state.longFired = false;
    state.pressedAt = millis();
  }
  if (state.down && !state.longFired && isDown && millis() - state.pressedAt >= BUTTON_LONG_PRESS_MS) {
    state.longFired = true;
    // Report only what the action itself consumed. Claiming the frame
    // unconditionally (the old `|| true`) broke every request-based action on
    // a hold: page turn / back / home / menu / control centre only raise a
    // request for the activity to pick up later in this same pass, and
    // returning true here makes loop() return before activityManager.loop()
    // runs -- the next pass then clears the request unseen. Frontlight and the
    // other act-now cases were unaffected, which is what made it look like a
    // page-turn bug. The trailing release is already swallowed by longFired.
    return runButtonAction(longAction);
  }
  if (releaseEdge || (state.down && !isDown)) {
    const bool wasLong = state.longFired;
    state.down = false;
    state.longFired = false;
    if (wasLong) return true;  // the hold already acted; the lift is not a tap
    return runButtonAction(shortAction);
  }
  return false;
}

static bool dispatchConfigurableButtons() {
  bool consumed = false;

  // Capacitive Home key: the SDK already classifies tap vs hold for it.
  if (gpio.hasHomeKey()) {
    if (gpio.wasHomeKeyLongPressed()) {
      consumed = runButtonAction(SETTINGS.homeKeyLongAction) || consumed;
    } else if (gpio.wasHomeKeyTapped()) {
      // Back and Home already reach their consumers through the mapped-input
      // paths (wasReleased(Back) / wasHomeGesture), which know the activity
      // stack; re-raising them here would double-fire.
      if (SETTINGS.homeKeyShortAction != CrossPointSettings::BTN_ACT_BACK &&
          SETTINGS.homeKeyShortAction != CrossPointSettings::BTN_ACT_HOME) {
        consumed = runButtonAction(SETTINGS.homeKeyShortAction) || consumed;
      }
    }
  }

  // Every key the user gets to bind. They are ordinary board keys as far as the
  // HAL is concerned -- debounced, edge-detected, masked out of the normal
  // queries so they cannot scroll a list behind the user's back -- and this is
  // the only place their identity is read. Serial-injected presses arrive on
  // the same path, which is what makes them testable without the hardware.
  // std::array, not a C array: a board with no configurable keys leaves the
  // table empty, and a zero-length C array is not a thing the standard has.
  static std::array<ConfigurableButton, CONFIGURABLE_KEYS.size()> buttons;
  for (size_t i = 0; i < CONFIGURABLE_KEYS.size(); i++) {
    const ConfigurableKey& key = CONFIGURABLE_KEYS[i];
    consumed = serviceConfigurableButton(buttons[i], gpio.rawIsPressed(key.button), gpio.rawWasReleased(key.button),
                                         SETTINGS.*key.shortAction, SETTINGS.*key.longAction) ||
               consumed;
  }

  return consumed;
}

void setupDisplayAndFonts(bool seamless = false) {
#if !FREEINK_MCU_C3
  // C3 resolves its controller in HalGPIO::begin() before SPI claims the
  // display pins. X4 Pro skips that C3-only path, so probe here before
  // display.begin() selects and initializes its panel driver.
  static bool controllerResolved = false;
  if (!controllerResolved) {
    controllerResolved = true;
    if (freeink::applyXteinkDisplayController()) {
      LOG_DBG("MAIN", "Panel controller: UltraChip UC81xx variant detected");
    }
  }
#endif

  display.begin(seamless);
  renderer.begin();
  // Before the first frame: the screen orientation is a device setting, so a
  // device put down sideways comes back sideways from a reset or a deep sleep
  // rather than snapping to portrait until something turns it again.
  applyScreenOrientation(renderer);
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSERIF_14_FONT_ID, notoserif14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(NOTOSERIF_12_FONT_ID, notoserif12FontFamily);
  renderer.insertFont(NOTOSERIF_16_FONT_ID, notoserif16FontFamily);
  renderer.insertFont(NOTOSERIF_18_FONT_ID, notoserif18FontFamily);

  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
#endif                  // OMIT_FONTS
  applySystemUiFont();  // registers UI_10/UI_12 with the face from SETTINGS.systemFont
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);

  // Discover and load SD card fonts
  sdFontSystem.begin(renderer);

  LOG_DBG("MAIN", "Fonts setup");
}

void setup() {
  BoardConfig::holdPowerRails();

#ifdef ENABLE_SERIAL_LOG
#ifdef CROSSPOINT_WAIT_FOR_USB_SERIAL
  // Development builds preserve reliable early CDC logs; release builds let
  // enumeration proceed asynchronously so users do not pay this startup cost.
  delay(250);
#endif
  Serial.begin(115200);
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

  HalSystem::begin();

#if FREEINK_DEVICE_LILYGO
  // The T5 S3's shared buses are board-owned and nothing in the SDK brings them up:
  // I2C (PCA9535 expander, TPS65185 EPD PMIC, GT911, BQ27220/BQ25896) and the SD SPI
  // bus, plus parking LoRa/GPS and registering the expander button hook. Must run
  // before gpio.begin() (button hook) and Storage.begin() (SPI), and before the
  // display driver's power hooks touch the PCA9535.
  BoardT5S3::begin();
  // Every route into deep sleep runs this last, while I2C is still alive: the
  // panel PMIC hangs off the expander, and the expander's output latches outlive
  // the ESP's sleep AND its wake reset, so an unparked TPS65185 keeps drawing
  // for the whole sleep with nothing on screen to show for it. The display
  // driver's own power-down is guarded by a cached flag and cannot be relied on
  // — and the two early returns further down reach sleep before the driver has
  // even been initialised.
  freeink::PowerManager::setSleepParkHook([] { BoardT5S3::parkEpdPowerForSleep(); });
#endif

  // checkPanic() clears the watchdog capture marker after a successful SD
  // dump, so retain the boot classification for the later activity route.
  const bool rebootedFromPanic = HalSystem::isRebootFromPanic();

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too — RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_READER) ? silentRebootTarget : 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;

  gpio.begin();
  // Hide every configurable key from the normal button queries so none of them
  // can scroll a list behind the user's back: dispatchConfigurableButtons()
  // owns them and turns each tap/hold into the configured action. Serial
  // injection bypasses the mask, so those presses still work for debugging.
  gpio.setMaskedButtons(configurableKeyMask());
  powerManager.begin();

  const auto wakeupReason = gpio.getWakeupReason();
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton && !gpio.verifyPowerButtonWakeup()) {
    powerManager.startDeepSleep(gpio);
  }

  // Not a wake anyone asked for: the previous sleep found the power line still
  // asserted after the release poll timed out, so instead of spinning on it (the
  // old behaviour, ~25 mA for as long as it lasted) it parked on a retry timer.
  // Test the line again and go straight back down; there is no one in front of
  // the screen to show a boot to.
  if (freeink::PowerManager::wokeFromStuckRetry()) {
    powerManager.startDeepSleep(gpio);
  }

  const auto recoveryButton =
      BoardConfig::isX4Pro() ? MappedInputManager::Button::Down : MappedInputManager::Button::Up;
  const bool recoveryFirmwareMode = wakeupReason == HalGPIO::WakeupReason::PowerButton && !BoardConfig::isPaperMono() &&
                                    mappedInputManager.isPressed(recoveryButton);

  halTiltSensor.begin();
  halClock.begin();

#if FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3
  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");
#else
  LOG_INF("MAIN", "Device: %s", BoardConfig::ACTIVE.name);
#endif

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }

  HalSystem::checkPanic();

  // Touch boards default the reader menu to the toolbar overlay instead of the
  // full-screen list. Seeded before the load: fromJson() falls back to the
  // in-memory value only when the file carries no readerMenuStyle key, so a
  // user's saved choice (either style) still wins.
  if (gpio.hasTouch()) {
    SETTINGS.readerMenuStyle = CrossPointSettings::READER_MENU_TOOLBAR;
  }
  SETTINGS.loadFromFile();
  APP_STATE.loadFromFile();
  const bool isSleepWake = wakeupReason == HalGPIO::WakeupReason::PowerButton;
  const bool isPersistedSleepWake = isSleepWake && !APP_STATE.showBootScreen;

  if (recoveryFirmwareMode) {
    LOG_INF("MAIN", "Recovery firmware mode (%s + POWER held at boot)", BoardConfig::isX4Pro() ? "DOWN" : "UP");
  }

  // Touch boards default the reader menu to the toolbar overlay instead of the
  // full-screen list. Seeded before the load: fromJson() falls back to the
  // in-memory value only when the file carries no readerMenuStyle key, so a
  // user's saved choice (either style) still wins.
  if (gpio.hasTouch()) {
    SETTINGS.readerMenuStyle = CrossPointSettings::READER_MENU_TOOLBAR;
  }
  SETTINGS.loadFromFile();
  RECENT_BOOKS.loadFromFile();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  // Brightness and warmth are always restored. A normal wake starts with the
  // light off unless Restore Light on Wake is enabled; silent maintenance
  // reboots preserve the live state so they do not unexpectedly go dark.
  const bool restoreLightOn = SETTINGS.frontlightOn != 0 && (SETTINGS.frontlightRestoreOnWake != 0 || isSilentReboot);
  Frontlight.begin(SETTINGS.frontlightBrightness, SETTINGS.frontlightWarmth, restoreLightOn);

  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      wakePowerReleasePending = true;
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // Most devices return to sleep after a USB-powered cold boot.
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_PAPERMONO
      // X4 Pro must stay awake so USB Serial/JTAG remains available after
      // leaving USB Drive and reconnecting the cable. Paper Mono has no
      // armable GPIO wake because its button is behind the PMIC. Sleeping
      // either device here would strand it in a USB-replug boot loop.
      break;
#else
      powerManager.startDeepSleep(gpio);
      break;
#endif
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  // Resolve the single boot-presentation decision. Skipping the splash also
  // skips the panel-clearing pass and the X3 initial-full-sync arming (see
  // HalDisplay::begin), so the first paint is FAST_REFRESH (~500ms) over the
  // retained frame and input dispatches against a visible UI.
  // Only a verified deep-sleep wake may use the one-shot persisted flag.
  // Otherwise a stale flag could suppress the splash on a cold boot.
  const BootResume resume = isSilentReboot         ? BootResume::Silent
                            : isPersistedSleepWake ? BootResume::SplashlessWake
                                                   : BootResume::Splash;
  bool allowFastInitialReaderRefresh = false;
  bool needsWakeRefresh = false;

  setupDisplayAndFonts(resume != BootResume::Splash);

  switch (resume) {
    case BootResume::Silent:
      // Splash skipped: the routing block below picks the target activity; the
      // panel keeps showing the pre-reboot popup until that first paint lands.
      break;
    case BootResume::SplashlessWake:
      // One-shot flag: re-arm the splash for the next ordinary boot. Save
      // before any painting so a hang in the blocking paint path can't strand
      // us in a splashless-with-no-frame loop on the next boot.
      APP_STATE.showBootScreen = true;
      APP_STATE.saveToFile();
      if (Storage.exists(SLEEP_FRAME_FILE) && loadSleepFrameBuffer()) {
        const bool useDifferentialRefresh = gpio.deviceIsX3();
        if (useDifferentialRefresh) {
          // begin() clears the X3 controller RAM, so restore the saved frame as
          // the baseline before replacing the moon with the loading icon.
          renderer.cleanupGrayscaleWithFrameBuffer();
        }

        const auto pageHeight = renderer.getScreenHeight();
        renderer.drawImage(LoadingIcon, 0, pageHeight - LOADINGICON_HEIGHT, LOADINGICON_WIDTH, LOADINGICON_HEIGHT);
        if (useDifferentialRefresh) {
          renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
          allowFastInitialReaderRefresh = true;
        } else {
          renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        }
      } else {
        // The first Home/Reader paint is followed by an explicit clean refresh
        // because the panel still physically shows the sleep image.
        needsWakeRefresh = true;
      }
      break;
    case BootResume::Splash:
      // Boot splash removed for speed: setupDisplayAndFonts() above already ran the
      // panel-clearing pass for a clean screen, so skip painting the splash frame and
      // let the routing block below paint the target activity (home/reader) directly.
      break;
  }

  // Output polarity is resolved per render by ActivityManager (night mode
  // inverts only the reading surfaces), so nothing to restore here.

  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
  } else if (rebootedFromPanic) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER &&
             !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (resume == BootResume::Silent) {
    // target == home (or reader with no open book): land on home — don't fall
    // through to the sleep-wake "resume reader" logic, which fires on stale
    // openEpubPath + lastSleepFromReader from a prior session.
    activityManager.goHome();
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0)
    activityManager.goHome(HomeMenuItem::NONE, needsWakeRefresh);
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path, allowFastInitialReaderRefresh);
  }

  if (resume == BootResume::Silent) {
    // Block until the first paint physically completes. refreshDisplay()
    // waits on the panel BUSY pin so when this returns the user can see the
    // new activity. Without the wait, an edge captured by gpio.update()
    // during boot dispatches against an invisible Home and the default
    // selectorIndex=0 opens the most-recent book.
    activityManager.requestUpdateAndWait();
    // Absorb any button held at this point into currentState as a non-edge:
    // two gpio.update() calls separated by > InputManager's 5ms debounce
    // transition the held bit through lastDebounceTime into currentState
    // without setting pressedEvents, so the first loop()'s own gpio.update()
    // sees state == currentState and emits nothing.
    gpio.update();
    delay(10);
    gpio.update();
  } else if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    // A normal sleep wake retains the wallpaper until the first activity
    // physically refreshes the panel. Complete that paint before setup exits
    // so the screen cannot remain on the retained sleep frame.
    activityManager.requestUpdateAndWait();
  }

  BatteryLog::begin();

  allowSleepAt = millis() + 2000;
}

// --- Power-draw instrumentation (CMD:PWR / CMD:HIZ) ---------------------------
// Measuring idle consumption is the only way to tell whether a power change is
// worth its risk, and the board already carries the meter: a BQ27220 fuel gauge
// (0x55) plus a BQ25896 charger (0x6B) on the shared I2C bus. BatteryMonitor
// reads three of the gauge's registers but exposes no current, so these two
// commands go straight to the silicon.
//
// The catch: with USB attached, VBUS feeds SYS and the gauge sees only the
// CHARGE current, never the system load — so a naive reading over the serial
// cable is meaningless. CMD:HIZ works around it. Setting the BQ25896's EN_HIZ
// bit disconnects the charger's input stage from VBUS, so SYS is drawn entirely
// from the battery while the USB DATA lines stay up and the serial console
// survives. Current() then reports true system draw. Clear it to resume
// charging.
//
// Register maps are deliberately NOT hardcoded beyond the three the codebase has
// already proven on hardware (Voltage 0x08, Current 0x0C, StateOfCharge 0x2C in
// BatteryMonitor.cpp). The rest of the standard command space is dumped raw so
// the coulomb counter can be identified from real values rather than from a
// datasheet transcription that may not match this part.
namespace {

// The gauge's I2C controller, mirroring BatteryMonitor::gaugeWire().
TwoWire& debugGaugeWire() {
#if SOC_I2C_NUM > 1
  if (BoardConfig::ACTIVE.batteryGauge.i2cBus == 1) return Wire1;
#endif
  return Wire;
}

// Gauge commands are 16-bit little-endian; charger registers are single bytes.
bool debugReadReg16(uint8_t addr, uint8_t reg, uint16_t& out) {
  if (addr == 0) return false;
  TwoWire& bus = debugGaugeWire();
  bus.beginTransmission(addr);
  bus.write(reg);
  if (bus.endTransmission(false) != 0) return false;
  if (bus.requestFrom(static_cast<int>(addr), 2) != 2) return false;
  const uint8_t lo = bus.read();
  const uint8_t hi = bus.read();
  out = static_cast<uint16_t>(lo | (hi << 8));
  return true;
}

bool debugReadReg8(uint8_t addr, uint8_t reg, uint8_t& out) {
  if (addr == 0) return false;
  TwoWire& bus = debugGaugeWire();
  bus.beginTransmission(addr);
  bus.write(reg);
  if (bus.endTransmission(false) != 0) return false;
  if (bus.requestFrom(static_cast<int>(addr), 1) != 1) return false;
  out = bus.read();
  return true;
}

bool debugWriteReg8(uint8_t addr, uint8_t reg, uint8_t value) {
  if (addr == 0) return false;
  TwoWire& bus = debugGaugeWire();
  bus.beginTransmission(addr);
  bus.write(reg);
  bus.write(value);
  return bus.endTransmission() == 0;
}

// Dump everything the power block can tell us. Decoded values first (the three
// registers BatteryMonitor already trusts), then raw blocks.
void dumpPowerTelemetry() {
  const auto& g = BoardConfig::ACTIVE.batteryGauge;
  if (g.gaugeAddr == 0) {
    logSerial.printf("PWR_ERR:no_gauge\n");
    return;
  }

  // Bring the bus up the same way a normal battery read would, so this works
  // even if nothing has polled the gauge yet this boot.
  static const BatteryMonitor debugBattery;
  const auto st = debugBattery.readStatus();

#if FREEINK_DEVICE_LILYGO
  // The T5S3's I2C bus is shared with the PCA9535, the EPD PMIC and the
  // digitizer; take the board mutex so a refresh in flight cannot interleave.
  BoardT5S3::ScopedI2CLock lock;
#endif

  uint16_t mv = 0, cur = 0, soc = 0;
  const bool mvOk = debugReadReg16(g.gaugeAddr, 0x08, mv);
  const bool curOk = debugReadReg16(g.gaugeAddr, 0x0C, cur);
  const bool socOk = debugReadReg16(g.gaugeAddr, 0x2C, soc);
  // Current() is signed: negative = discharging (this is what we want to read),
  // positive = charging.
  const int16_t curMa = static_cast<int16_t>(cur);
  logSerial.printf("PWR: mv=%u(ok=%d) current_ma=%d(ok=%d) soc=%u(ok=%d) charging=%d ext=%d ms=%lu\n", mv, mvOk, curMa,
                   curOk, soc, socOk, st.charging, st.externalPower, millis());

  // Raw standard-command space, 8 words per line: small writes so HWCDC's TX
  // ring never overruns (see the SCREENSHOT dump for the same constraint).
  for (uint8_t base = 0x00; base < 0x40; base += 0x10) {
    char line[128];
    int n = snprintf(line, sizeof(line), "PWR_GAUGE:%02X:", base);
    for (uint8_t off = 0; off < 0x10 && n > 0 && n < static_cast<int>(sizeof(line)); off += 2) {
      uint16_t w = 0;
      const bool ok = debugReadReg16(g.gaugeAddr, static_cast<uint8_t>(base + off), w);
      n += snprintf(line + n, sizeof(line) - n, ok ? " %04X" : " ----", w);
    }
    logSerial.printf("%s\n", line);
    logSerial.flush();
  }

  if (g.chargerAddr != 0) {
    char line[160];
    int n = snprintf(line, sizeof(line), "PWR_CHG:");
    for (uint8_t reg = 0x00; reg <= 0x14 && n > 0 && n < static_cast<int>(sizeof(line)); ++reg) {
      uint8_t v = 0;
      const bool ok = debugReadReg8(g.chargerAddr, reg, v);
      n += snprintf(line + n, sizeof(line) - n, ok ? " %02X" : " --", v);
    }
    logSerial.printf("%s\n", line);
    uint8_t reg00 = 0;
    if (debugReadReg8(g.chargerAddr, 0x00, reg00)) {
      logSerial.printf("PWR_HIZ:%d\n", (reg00 & 0x80) ? 1 : 0);
    }
  }
  logSerial.flush();
}

// Profile where the current actually goes, one load at a time.
//
// Order matters and it is the opposite of intuition: measure the BIG loads
// first. The frontlight and the GT911 are tens of mA and single-digit mA, both
// far above the gauge's noise floor, so Current() reads them directly in
// seconds. Only the CPU's own idle draw is small enough to need the slow
// coulomb-counting method -- so it is measured last, against a floor that is
// by then already known. Optimising the CPU before knowing what the peripherals
// cost would be optimising the wrong thing.
//
// Ends in a restart: holding the GT911 in reset is easy, but bringing it back
// needs the SDK's private beginGt911() and its address-selection timing. A
// reboot restores it deterministically, and the results are in RTC memory.
void setChargerHiz(bool enable);  // defined below; the profiles run entirely on battery

// Sweep the frontlight and measure each step, because its cost is NOT linear in
// the percentage the user sets. FrontlightManager maps percent -> duty through a
// gamma 1.6554 table, and the PT4103 boost adds a fixed per-PWM-cycle overhead
// that does not shrink with duty. The honest model is affine in DUTY,
// I = a*duty + b, which needs at least two points -- one of them at low duty or
// `b` is ill-conditioned.
//
// Sweeping beats inferring here: at 2% the draw is ~2 mA, sitting on a +-1 mA
// noise floor, so the dim end is the part least worth trusting to a single
// measurement. Measure the bright steps accurately and let the dim end be
// predicted by the fit rather than measured badly.
void runFrontlightSweep() {
  const auto& g = BoardConfig::ACTIVE.batteryGauge;
  constexpr uint8_t BQ27220_CURRENT = 0x0C;
  if (g.gaugeAddr == 0 || !Frontlight.present()) {
    logSerial.printf("FLSWEEP_ERR:no_gauge_or_frontlight\n");
    return;
  }

  const bool wasOn = Frontlight.isOn();
  const uint8_t wasBrightness = Frontlight.brightness();

  setChargerHiz(true);
  Frontlight.setOn(false);
  delay(3000);
  long dark = 0;
  for (int i = 0; i < 4; ++i) {
    uint16_t raw = 0;
    debugReadReg16(g.gaugeAddr, BQ27220_CURRENT, raw);
    dark += static_cast<int16_t>(raw);
    delay(1000);
  }
  dark /= 4;
  logSerial.printf("FLSWEEP:off ma=%ld\n", dark);

  static const uint8_t STEPS[] = {1, 5, 10, 25, 50, 75, 100};
  Frontlight.setOn(true);
  for (uint8_t pct : STEPS) {
    Frontlight.setBrightness(pct);
    delay(3000);
    long sum = 0;
    for (int i = 0; i < 4; ++i) {
      uint16_t raw = 0;
      debugReadReg16(g.gaugeAddr, BQ27220_CURRENT, raw);
      sum += static_cast<int16_t>(raw);
      delay(1000);
    }
    const long mean = sum / 4;
    // Report the delta against the dark floor: that is the frontlight's own
    // cost, with the rest of the board subtracted out.
    logSerial.printf("FLSWEEP:pct=%u ma=%ld light_ma=%ld\n", pct, mean, dark - mean);
    logSerial.flush();
  }

  Frontlight.setBrightness(wasBrightness);
  Frontlight.setOn(wasOn);
  setChargerHiz(false);
  logSerial.printf("FLSWEEP_DONE:restored on=%d pct=%u\n", wasOn, wasBrightness);
  logSerial.flush();
}

void runPowerProfile() {
  const auto& g = BoardConfig::ACTIVE.batteryGauge;
  constexpr uint8_t BQ27220_CURRENT = 0x0C;
  if (g.gaugeAddr == 0 || g.chargerAddr == 0) {
    logSerial.printf("PROF_ERR:no_gauge_or_charger\n");
    return;
  }

  // Averaged because Current() is a ~1 Hz conversion and jitters by a mA or two;
  // min/max come out too so a noisy step is visible rather than hidden.
  auto sample = [&](const char* label) {
    delay(4000);  // let the gauge's averaging window clear the change
    long sum = 0;
    int lo = 32767, hi = -32768;
    for (int i = 0; i < 5; ++i) {
      uint16_t raw = 0;
      debugReadReg16(g.gaugeAddr, BQ27220_CURRENT, raw);
      const int ma = static_cast<int16_t>(raw);
      sum += ma;
      if (ma < lo) lo = ma;
      if (ma > hi) hi = ma;
      delay(1000);
    }
    const long mean = sum / 5;
    logSerial.printf("PROF:%s mean_ma=%ld min=%d max=%d\n", label, mean, lo, hi);
    logSerial.flush();
    return mean;
  };

  // Everything below is measured on battery: with VBUS feeding SYS the gauge
  // would report charge current instead of load. Set here rather than left to
  // the caller so a forgotten CMD:HIZ:1 cannot silently invalidate a run.
  setChargerHiz(true);

  const bool frontlightWasOn = Frontlight.isOn();
  const uint8_t frontlightBrightness = Frontlight.brightness();
  logSerial.printf("PROF_START:frontlight_on=%d brightness=%u\n", frontlightWasOn, frontlightBrightness);

  if (!frontlightWasOn) {
    Frontlight.setOn(true);
    Frontlight.setBrightness(frontlightBrightness);
  }
  const long withLight = sample("frontlight_on");

  Frontlight.setOn(false);
  const long noLight = sample("frontlight_off");

  // Park the digitizer the way the deep-sleep path does (TouchConfig::
  // holdResetInSleep): nothing gates its power on this board, so reset is the
  // only off switch it has.
  const auto& t = BoardConfig::ACTIVE.touch;
  if (t.reset >= 0) {
    gpio_hold_dis(static_cast<gpio_num_t>(t.reset));
    pinMode(t.reset, OUTPUT);
    digitalWrite(t.reset, LOW);
  }
  const long noTouch = sample("touch_off");

  setChargerHiz(false);

  logSerial.printf("PROF_SUMMARY:frontlight_ma=%ld gt911_ma=%ld floor_ma=%ld\n", withLight - noLight, noLight - noTouch,
                   noTouch);
  logSerial.printf("PROF_NOTE:restarting to restore the digitizer\n");
  logSerial.flush();
  delay(500);
  esp_restart();
}

// Spend `seconds` in light sleep and report what it cost, so the saving can be
// measured before deciding whether to build it into the main loop.
//
// Measuring this needs care. The gauge cannot be read while the CPU is halted,
// and Current() at a few mA sits near the BQ27220's noise floor anyway, so the
// real number comes from the coulomb counter: RemainingCapacity before and
// after, over a window long enough to move a whole mAh (at ~2 mA that is about
// 30 minutes). Current() is still sampled immediately on wake as a rough
// cross-check -- its conversion window will have been mostly asleep.
//
// Run it with CMD:HIZ:1 or the numbers describe the charger, not the board.
void runLightSleepProbe(uint32_t seconds, bool useLightSleep) {  // NOLINT(readability-function-size)
  const auto& g = BoardConfig::ACTIVE.batteryGauge;
  constexpr uint8_t BQ27220_REMAINING_CAPACITY = 0x10;
  constexpr uint8_t BQ27220_CURRENT = 0x0C;
  if (g.gaugeAddr == 0 || seconds == 0) {
    logSerial.printf("LSLEEP_ERR:no_gauge_or_zero\n");
    return;
  }
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    logSerial.printf("LSLEEP_ERR:wifi_active\n");
    return;
  }

  uint16_t remCapStart = 0;
  debugReadReg16(g.gaugeAddr, BQ27220_REMAINING_CAPACITY, remCapStart);
  // The frontlight is the largest single load on this board -- tens of mA, far
  // more than anything the CPU does at idle. Leaving it on would swamp the very
  // difference being measured, so park it and restore it afterwards.
  // Own the battery-isolation switch rather than trusting the caller to have set
  // it: this probe deliberately kills the USB console partway through, so a
  // forgotten CMD:HIZ:0 afterwards would leave the board running off its battery
  // with no way to notice. The loop is known to return (verified: finished=1),
  // so clearing it at the end is reliable.
  setChargerHiz(true);

  const bool frontlightWasOn = Frontlight.isOn();
  if (frontlightWasOn) Frontlight.setOn(false);

  logSerial.printf("LSLEEP_START:secs=%lu mode=%s remcap=%u frontlight_was=%d\n", static_cast<unsigned long>(seconds),
                   useLightSleep ? "lightsleep" : "baseline", remCapStart, frontlightWasOn);
  // Stamp the slot BEFORE sleeping and clear reachedEnd: if the loop never
  // returns, the next boot sees a valid magic with reachedEnd == 0, which
  // distinguishes 'light sleep killed it' from 'probe never ran'.
  lsleepMagic = LSLEEP_RESULT_MAGIC;
  lsleepReachedEnd = 0;
  lsleepIterations = seconds;
  lsleepRemCapStart = remCapStart;
  lsleepRemCapEnd = 0;
  lsleepElapsedMs = 0;
  lsleepTimerWakes = 0;
  lsleepGpioWakes = 0;
  lsleepCurrentMa = 0;

  logSerial.flush();
  delay(50);  // let the console drain before the peripheral loses its clock

  // The framebuffer and the page cache live in PSRAM; its rail must stay up or
  // the screen comes back as noise.
  esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON);

  // Wake on the page-turn button as well as the timer, so the probe stays
  // interruptible and the GPIO path gets exercised at the same time.
#if FREEINK_DEVICE_LILYGO
  gpio_wakeup_enable(static_cast<gpio_num_t>(T5S3_BOOT_BTN), GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
#endif

  // Bound the loop by ITERATION COUNT, not by a millis() deadline. Whether
  // millis() advances across light sleep is exactly the thing under test, and
  // when it does not, a `while (millis() - start < target)` loop never
  // terminates -- it needs that many milliseconds of AWAKE time, which at one
  // second of sleep per iteration is hours. That hung the first version.
  const unsigned long startMs = millis();
  uint32_t timerWakes = 0, gpioWakes = 0, otherWakes = 0;
  for (uint32_t i = 0; i < seconds; ++i) {
    if (!useLightSleep) {
      // Baseline: reproduce what the idle main loop does today -- reduced clock,
      // 50 ms delays -- for the same wall time, measured the same way. Without
      // an A/B measured through one instrument, the light-sleep number has
      // nothing trustworthy to be compared against.
      powerManager.setPowerSaving(true);
      for (int j = 0; j < 20; ++j) delay(50);
      ++timerWakes;
      if ((i % 10) == 9) {
        logSerial.printf("LSLEEP_TICK:%lu/%lu ms=%lu\n", static_cast<unsigned long>(i + 1),
                         static_cast<unsigned long>(seconds), millis());
        logSerial.flush();
      }
      continue;
    }
    esp_sleep_enable_timer_wakeup(1000000ULL);  // 1 s chunks, so the probe stays interruptible
    esp_light_sleep_start();
    switch (esp_sleep_get_wakeup_cause()) {
      case ESP_SLEEP_WAKEUP_TIMER:
        ++timerWakes;
        break;
      case ESP_SLEEP_WAKEUP_GPIO:
        ++gpioWakes;
        break;
      default:
        ++otherWakes;
        break;
    }
    // Heartbeat, so a run that loses the console still proves it was cycling.
    if ((i % 10) == 9) {
      logSerial.printf("LSLEEP_TICK:%lu/%lu ms=%lu\n", static_cast<unsigned long>(i + 1),
                       static_cast<unsigned long>(seconds), millis());
      logSerial.flush();
    }
  }
  // Reported, not trusted: comparing this against `seconds` is how we learn
  // whether the system clock is corrected for time spent asleep.
  const unsigned long elapsedMs = millis() - startMs;

  // Read Current() first: its averaging window is the one that just ended.
  uint16_t curRaw = 0, remCapEnd = 0;
  debugReadReg16(g.gaugeAddr, BQ27220_CURRENT, curRaw);
  debugReadReg16(g.gaugeAddr, BQ27220_REMAINING_CAPACITY, remCapEnd);

#if FREEINK_DEVICE_LILYGO
  gpio_wakeup_disable(static_cast<gpio_num_t>(T5S3_BOOT_BTN));
#endif
  if (frontlightWasOn) Frontlight.setOn(true);
  setChargerHiz(false);

  lsleepElapsedMs = elapsedMs;
  lsleepRemCapEnd = remCapEnd;
  lsleepTimerWakes = timerWakes;
  lsleepGpioWakes = gpioWakes;
  lsleepCurrentMa = static_cast<int16_t>(curRaw);
  lsleepReachedEnd = 1;

  const int deltaMah = static_cast<int>(remCapStart) - static_cast<int>(remCapEnd);
  // mAh over hours = mA. Integer maths, so scale before dividing.
  const long avgMa = elapsedMs > 0 ? (deltaMah * 3600000L) / static_cast<long>(elapsedMs) : 0;
  logSerial.printf("LSLEEP_DONE:elapsed_ms=%lu remcap=%u->%u delta_mah=%d avg_ma=%ld current_ma=%d\n", elapsedMs,
                   remCapStart, remCapEnd, deltaMah, avgMa, static_cast<int16_t>(curRaw));
  logSerial.printf("LSLEEP_WAKES:timer=%lu gpio=%lu other=%lu\n", static_cast<unsigned long>(timerWakes),
                   static_cast<unsigned long>(gpioWakes), static_cast<unsigned long>(otherWakes));
  logSerial.flush();
}

// Print whatever the last probe stored. Safe to call at any time, including
// from a fresh boot after the console came back.
void dumpLightSleepResult() {
  if (lsleepMagic != LSLEEP_RESULT_MAGIC) {
    logSerial.printf("LSLEEP_RESULT:none\n");
    return;
  }
  const int deltaMah = static_cast<int>(lsleepRemCapStart) - static_cast<int>(lsleepRemCapEnd);
  const long avgMa =
      lsleepElapsedMs > 0 ? (static_cast<long>(deltaMah) * 3600000L) / static_cast<long>(lsleepElapsedMs) : 0;
  logSerial.printf("LSLEEP_RESULT:finished=%lu iters=%lu elapsed_ms=%lu remcap=%lu->%lu delta_mah=%d avg_ma=%ld\n",
                   static_cast<unsigned long>(lsleepReachedEnd), static_cast<unsigned long>(lsleepIterations),
                   static_cast<unsigned long>(lsleepElapsedMs), static_cast<unsigned long>(lsleepRemCapStart),
                   static_cast<unsigned long>(lsleepRemCapEnd), deltaMah, avgMa);
  logSerial.printf("LSLEEP_RESULT_WAKES:timer=%lu gpio=%lu current_ma=%ld\n",
                   static_cast<unsigned long>(lsleepTimerWakes), static_cast<unsigned long>(lsleepGpioWakes),
                   static_cast<long>(lsleepCurrentMa));
  logSerial.flush();
}

// Toggle BQ25896 EN_HIZ (REG00 bit 7). Read back so the caller sees what the
// charger actually latched rather than what we asked for.
void setChargerHiz(bool enable) {
  const auto& g = BoardConfig::ACTIVE.batteryGauge;
  if (g.chargerAddr == 0) {
    logSerial.printf("HIZ_ERR:no_charger\n");
    return;
  }
#if FREEINK_DEVICE_LILYGO
  BoardT5S3::ScopedI2CLock lock;
#endif
  uint8_t reg00 = 0;
  if (!debugReadReg8(g.chargerAddr, 0x00, reg00)) {
    logSerial.printf("HIZ_ERR:read\n");
    return;
  }
  const uint8_t updated = enable ? static_cast<uint8_t>(reg00 | 0x80) : static_cast<uint8_t>(reg00 & 0x7F);
  if (!debugWriteReg8(g.chargerAddr, 0x00, updated)) {
    logSerial.printf("HIZ_ERR:write\n");
    return;
  }
  uint8_t verify = 0;
  debugReadReg8(g.chargerAddr, 0x00, verify);
  logSerial.printf("HIZ_OK:%d reg00=%02X->%02X\n", (verify & 0x80) ? 1 : 0, reg00, verify);
}

}  // namespace

// --- Low-battery protection ---------------------------------------------------
// Nothing in the firmware used to watch the battery: getBatteryPercentage() fed
// the status-bar glyph and nothing else, so the reader ran flat out until the
// hardware cut power mid-page. Reading position survives that (saveProgress()
// runs on every page change), but the reader dying without warning does not
// make for a device you trust on a trip.
//
// Phone-shaped policy: warn once, warn harder, then put itself away safely
// rather than being switched off by physics.
namespace {
constexpr uint16_t BATTERY_WARN_PCT = 15;     // first notice
constexpr uint16_t BATTERY_CRITICAL_PCT = 5;  // last notice
constexpr uint16_t BATTERY_SHUTDOWN_PCT = 2;  // save and sleep
// Voltage backstop, and NOT redundant with the percentage above. The gauge's
// scale is calibrated to its own terminate voltage, while this board browns out
// when the 3.3 V regulator runs out of headroom -- which happens at a HIGHER
// voltage, i.e. while the gauge still reports several percent left. Without
// this rule the 2% shutdown could simply never be reached. 3.40 V is a loaded
// Li-ion pack with very little usable charge left, and comfortably above any
// plausible regulator dropout.
constexpr uint16_t BATTERY_SHUTDOWN_MV = 3400;
// Re-arm each warning only after a clear recovery, so a percentage dithering
// across the threshold cannot pop the same toast every few seconds.
constexpr uint16_t BATTERY_REARM_MARGIN_PCT = 3;

bool warnedLowBattery = false;
bool warnedCriticalBattery = false;

#ifdef ENABLE_SERIAL_LOG
// CMD:BATTSIM:<pct>[:<mv>] overrides the readings so the thresholds below can be
// exercised on a full battery -- the one code path you cannot afford to ship
// untested is the one that only ever runs when the device is nearly dead.
// CMD:BATTSIM:-1 returns to the real gauge.
int simBatteryPct = -1;
int simBatteryMv = -1;
#endif

// Returns true when it has taken over the frame (a toast was shown, or the
// device is on its way to sleep and the caller must not keep running).
bool checkLowBattery() {
#ifdef ENABLE_SERIAL_LOG
  // An override also has to bypass the charging gate below, or the feature
  // stays untestable: the only way to hold a serial console is over the same
  // cable that makes isCharging() true.
  const bool simulating = simBatteryPct >= 0;
#else
  constexpr bool simulating = false;
#endif

  // On the cable there is nothing to protect against, and the reading is the
  // charger's business anyway. Clear the latches so an unplug starts fresh.
  if (!simulating && gpio.isCharging()) {
    warnedLowBattery = false;
    warnedCriticalBattery = false;
    return false;
  }

  uint16_t pct = powerManager.getBatteryPercentage();
  uint16_t mv = powerManager.getBatteryMillivolts();
#ifdef ENABLE_SERIAL_LOG
  if (simBatteryPct >= 0) pct = static_cast<uint16_t>(simBatteryPct);
  if (simBatteryMv >= 0) mv = static_cast<uint16_t>(simBatteryMv);
#endif

  // A board with no battery telemetry at all reports 0% forever; that must not
  // read as "empty" and sleep the device on every boot.
  if (pct == 0 && mv == 0) {
    return false;
  }

  const bool voltageCritical = mv != 0 && mv <= BATTERY_SHUTDOWN_MV;
  if (pct <= BATTERY_SHUTDOWN_PCT || voltageCritical) {
    LOG_INF("BATT", "Low-battery shutdown: %u%%, %u mV", pct, mv);
    {
      RenderLock lock;
      GUI.drawPopup(renderer, tr(STR_BATTERY_SHUTDOWN));
    }
    delay(2000);  // long enough to read before the panel retains the sleep frame
    enterDeepSleep(false, "low-battery");
    return true;
  }

  if (pct <= BATTERY_CRITICAL_PCT) {
    if (!warnedCriticalBattery) {
      warnedCriticalBattery = true;
      LOG_INF("BATT", "Critical-battery warning: %u%%, %u mV", pct, mv);
      showActionToast(tr(STR_BATTERY_CRITICAL));
      return true;
    }
    return false;
  }
  if (pct > BATTERY_CRITICAL_PCT + BATTERY_REARM_MARGIN_PCT) {
    warnedCriticalBattery = false;
  }

  if (pct <= BATTERY_WARN_PCT) {
    if (!warnedLowBattery) {
      warnedLowBattery = true;
      LOG_INF("BATT", "Low-battery warning: %u%%, %u mV", pct, mv);
      showActionToast(tr(STR_BATTERY_LOW));
      return true;
    }
    return false;
  }
  if (pct > BATTERY_WARN_PCT + BATTERY_REARM_MARGIN_PCT) {
    warnedLowBattery = false;
  }
  return false;
}
}  // namespace

// --- Idle light sleep ---------------------------------------------------------
// Between page turns the reader used to spin: delay(50) at a reduced clock,
// burning 40 mA to display a page that is already on the panel and needs no
// power to stay there. Measured, light sleep takes that to 8 mA -- and since the
// e-paper holds its image and the user reads for a minute at a time, almost all
// of a reading session is spent in exactly this state.
//
// Deep sleep cannot be used here: waking from it is a cold boot, one to two
// seconds, which is unacceptable for a button press. Light sleep resumes in
// about a millisecond.
namespace {

// How long the device must be idle before it is worth sleeping. Short enough
// that a page being read is nearly all sleep, long enough that flicking through
// several pages in a row never pays sleep/wake overhead.
constexpr unsigned long LIGHT_SLEEP_IDLE_MS = 3000;

// Backstop wake. The page-forward button is the expander key, which can only be
// read over I2C -- so unlike BOOT and the touch IRQ it cannot be sampled by a
// sleeping CPU at all, only woken from by the PCA9535 INT line on GPIO38. That
// pin sits outside the RTC bank, and while light-sleep GPIO wakeup goes through
// the always-powered digital GPIO peripheral and should work, "should" is not
// good enough for the button the user presses most. A short timer means the
// worst case is a polled button, not a dead one.
constexpr uint64_t LIGHT_SLEEP_TIMER_US = 150000;  // 150 ms

bool lightSleepArmed = false;

#ifdef ENABLE_SERIAL_LOG
// CMD:LSFORCE:1 overrides the "not while on the cable" guard below. Without it
// the feature is untestable: it only ever engages unplugged, which is exactly
// when there is no console to observe it from.
bool lightSleepForced = false;
#endif

// Level-triggered LOW, because all three lines are active-low with pull-ups.
void armLightSleepWakeSources() {
  if (lightSleepArmed) return;
  lightSleepArmed = true;

  // Framebuffer and page cache live in octal PSRAM; its rail must stay up or the
  // screen comes back as noise.
  esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON);

  const auto arm = [](int pin) {
    if (pin < 0) return;
    const auto gpio = static_cast<gpio_num_t>(pin);
    // Keep the awake pin configuration through sleep. The S3 otherwise applies a
    // SEPARATE sleep-time configuration on sleep entry, which drops the pull-up
    // and leaves these active-low lines floating -- the same documented gotcha
    // the SDK calls out for the frontlight's LEDC pads. On the GT911 INT line
    // that shows up as a sensitivity fault rather than an obvious failure: a
    // brief, weak pulse from a fingertip no longer pulls a floating line low
    // cleanly, so only broad firm contact registers.
    gpio_sleep_sel_dis(gpio);
    gpio_pullup_en(gpio);
    gpio_wakeup_enable(gpio, GPIO_INTR_LOW_LEVEL);
  };
  // Every key the board has a pin for. A key that cannot wake the CPU is not
  // read until the 150 ms timer does, and a debounce then needs a second sample
  // after that -- which is how a tap came to want ~600 ms of holding before the
  // pins moved into BoardConfig. Unwired pins are safe to arm: the HAL holds
  // them pulled up, so an empty pad sits HIGH rather than waking on noise.
  const auto& input = BoardConfig::ACTIVE.input;
  for (const int8_t pin : {input.back, input.confirm, input.left, input.right, input.up, input.down, input.power}) {
    arm(pin);
  }
#if FREEINK_DEVICE_LILYGO
  arm(T5S3_PCA9535_INT);  // the expander key, which has no pin of its own
#endif
  arm(BoardConfig::ACTIVE.touch.irq);  // touch and the capacitive home key
  esp_sleep_enable_gpio_wakeup();
}

// Why light sleep is not being entered, or nullptr when nothing is in the way.
//
// This used to be a chain of bare `return false`s inside tryIdleLightSleep().
// Every one of them is a correct reason to stay awake and none of them left a
// trace, so when light sleep silently stopped happening for a week the log could
// only show the absence -- lsleep_ms flat while page turns kept climbing -- and
// not the cause. Naming each rejection costs a string constant.
const char* lightSleepBlocker(unsigned long idleForMs) {
  if (!SETTINGS.lightSleepIdle) return "setting-off";
  if (idleForMs < LIGHT_SLEEP_IDLE_MS) return "not-idle-yet";

  // Between page turns and nowhere else. A page held on screen mid-book is the
  // only state that is genuinely idle for minutes at a time; the home screen,
  // the reader menu and every settings page expect input to be acted on
  // promptly, and the saving there would be worth far less than the risk.
  if (!activityManager.isReaderOnTop()) return "not-reader";

  // Anything with work in flight must keep its clocks: light sleep halts BOTH
  // cores, not just this task.
  if (activityManager.skipLoopDelay()) return "busy-loop";  // webserver / OTA
  if (WiFi.getMode() != WIFI_MODE_NULL) return "wifi-up";   // radio needs its clocks
  if (deepSleepInProgress) return "deep-sleeping";
  if (activityManager.isRenderBusy()) return "render-busy";  // would freeze it mid-draw

  // A held button would re-trigger a level wake instantly, spinning instead of
  // sleeping -- and both hold actions (power off, touch toggle) time their hold
  // in the main loop, so the loop has to keep running while a key is down.
  if (gpio.isPressed(HalGPIO::BTN_POWER)) return "power-held";
  if (gpio.rawIsPressed(HalGPIO::BTN_DOWN)) return "userkey-held";

#ifdef ENABLE_SERIAL_LOG
  // Light sleep kills the USB-Serial/JTAG console for good (verified: the device
  // keeps running, the console does not come back until the cable is replugged).
  // On the cable there is nothing to save anyway, so keep the console usable.
  if (!lightSleepForced && gpio.isCharging()) return "on-cable";
#endif
  return nullptr;
}

// Most recent answer from the above, plus how often the sleep itself was refused
// by the SDK. Reported by CMD:IDLE.
const char* lastLightSleepBlocker = "never-tried";
uint32_t lightSleepEntries = 0;
uint32_t lightSleepRejects = 0;

// True when the frame was spent asleep and the caller should skip its own delay.
bool tryIdleLightSleep(unsigned long idleForMs) {
  const char* blocker = lightSleepBlocker(idleForMs);
  if (blocker != nullptr) {
    // "not-idle-yet" is the normal state of a device someone is reading on, so
    // recording it would overwrite the answer the caller actually wants.
    if (strcmp(blocker, "not-idle-yet") != 0) lastLightSleepBlocker = blocker;
    return false;
  }

  // Last, because it can block: a deferred panel refresh may still be running
  // after the render task has let go of its lock, and the panel must be idle
  // before both cores stop. No-op when nothing is pending. Placed after every
  // cheap guard so a frame that will not sleep never waits here.
  display.waitRefreshComplete();

  armLightSleepWakeSources();
  esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_TIMER_US);

  // Rejection is normal and frequent -- a pending interrupt, or a peripheral
  // holding a sleep lock. Measured at roughly one call in three during the
  // bench probe. Report it so the caller falls back to its ordinary delay
  // rather than busy-looping through a sleep that never happens.
  const unsigned long before = millis();
  const bool slept = esp_light_sleep_start() == ESP_OK;
  if (slept) {
    ++lightSleepEntries;
    lastLightSleepBlocker = "none";
  } else {
    ++lightSleepRejects;
    lastLightSleepBlocker = "sdk-refused";
  }
  // millis() runs off the RTC timer, which keeps counting while the CPU is
  // halted, so this delta is the time the telemetry would otherwise miss
  // entirely (BatteryLog::accumulate only runs between sleeps).
  if (slept) BatteryLog::noteLightSleep(static_cast<uint32_t>(millis() - before));
  return slept;
}

}  // namespace

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.setSharedConfirmPowerShortPressEmitsPower(SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
  // Button-action requests live for one input frame: cleared here, raised by
  // dispatchConfigurableButtons() below, consumed by the activity's loop().
  MappedInputManager::clearFrameActionRequests();
  mappedInputManager.update();

  if (activityManager.requiresExclusiveStorageLoop()) {
    // USB Drive handed the raw SD card to the host. Do not run screenshots,
    // sleep, shortcuts, or normal navigation while its filesystem is detached.
    activityManager.loop();
    if (activityManager.preventAutoSleep()) {
      powerManager.setPowerSaving(false);
      delay(10);
    } else {
      // No host is active, so a slower loop is safe. The activity itself times
      // out the raw-storage handoff rather than entering deep sleep detached.
      powerManager.setPowerSaving(true);
      delay(50);
    }
    return;
  }

  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, activityManager.isReaderActivity());

  renderer.setFadingFix(SETTINGS.fadingFix);
  renderer.setGlyphWeight(SETTINGS.textStrokeWeight);

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_DIM:%dx%d\n", renderer.getDisplayWidth(), renderer.getDisplayHeight());
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        // HWCDC drops the tail of a large write once its 256-byte TX ring
        // overruns the host's drain rate (a timeout flips its internal
        // "connected" flag and the rest is discarded). Pace the dump to the
        // USB drain rate: small chunks, flush between them, give up only if
        // the host stops draining entirely.
        uint32_t stalls = 0;
        for (uint32_t off = 0; off < bufferSize && stalls < 400;) {
          const uint32_t n = bufferSize - off < 256 ? bufferSize - off : 256;
          const size_t w = logSerial.write(buf + off, n);
          logSerial.flush();
          if (w == 0) {
            ++stalls;
            delay(5);
          } else {
            stalls = 0;
            off += w;
          }
        }
        logSerial.printf("SCREENSHOT_END\n");
      } else if (cmd.startsWith("KEY:")) {
        // CMD:KEY:<NAME>[:<holdMs>] — fake a button press (see HalGPIO::injectButton)
        String name = cmd.substring(4);
        unsigned long holdMs = 0;
        const int colon = name.indexOf(':');
        if (colon >= 0) {
          holdMs = name.substring(colon + 1).toInt();
          name = name.substring(0, colon);
        }
        int idx = -1;
        if (name == "BACK")
          idx = HalGPIO::BTN_BACK;
        else if (name == "CONFIRM")
          idx = HalGPIO::BTN_CONFIRM;
        else if (name == "LEFT")
          idx = HalGPIO::BTN_LEFT;
        else if (name == "RIGHT")
          idx = HalGPIO::BTN_RIGHT;
        else if (name == "UP")
          idx = HalGPIO::BTN_UP;
        else if (name == "DOWN")
          idx = HalGPIO::BTN_DOWN;
        else if (name == "POWER")
          idx = HalGPIO::BTN_POWER;
        if (idx >= 0) {
          gpio.injectButton(static_cast<uint8_t>(idx), holdMs);
          logSerial.printf("KEY_OK:%s:%lu\n", name.c_str(), holdMs);
        } else {
          logSerial.printf("KEY_ERR:%s\n", name.c_str());
        }
      } else if (cmd.startsWith("TAP:") || cmd.startsWith("LONG:") || cmd.startsWith("SWIPE:")) {
        // CMD:TAP:x:y / CMD:LONG:x:y / CMD:SWIPE:x1:y1:x2:y2 — coordinates in
        // the LOGICAL frame (what a screenshot of the current orientation
        // shows). Inverse of GfxRenderer::tapToLogical back to the normalized
        // native-panel space the touch controller reports in.
        const int pW = renderer.getDisplayWidth();
        const int pH = renderer.getDisplayHeight();
        auto toNorm = [&](int lx, int ly, float& nx, float& ny) {
          int phyX = 0, phyY = 0;
          switch (renderer.getOrientation()) {
            case GfxRenderer::Orientation::Portrait:
              phyX = ly;
              phyY = pH - 1 - lx;
              break;
            case GfxRenderer::Orientation::PortraitInverted:
              phyX = pW - 1 - ly;
              phyY = lx;
              break;
            case GfxRenderer::Orientation::LandscapeClockwise:
              phyX = pW - 1 - lx;
              phyY = pH - 1 - ly;
              break;
            case GfxRenderer::Orientation::LandscapeCounterClockwise:
            default:
              phyX = lx;
              phyY = ly;
              break;
          }
          nx = (phyX + 0.5f) / pW;
          ny = (phyY + 0.5f) / pH;
        };
        int v[4] = {0, 0, 0, 0};
        int nVals = 0;
        {
          String rest = cmd.substring(cmd.indexOf(':') + 1);
          while (nVals < 4 && rest.length() > 0) {
            const int colon = rest.indexOf(':');
            v[nVals++] = (colon >= 0 ? rest.substring(0, colon) : rest).toInt();
            if (colon < 0) break;
            rest = rest.substring(colon + 1);
          }
        }
        float nx1, ny1;
        toNorm(v[0], v[1], nx1, ny1);
        if (cmd.startsWith("SWIPE:") && nVals == 4) {
          float nx2, ny2;
          toNorm(v[2], v[3], nx2, ny2);
          gpio.injectSwipe(nx1, ny1, nx2, ny2);
          logSerial.printf("TOUCH_OK:SWIPE:%d:%d:%d:%d\n", v[0], v[1], v[2], v[3]);
        } else if (cmd.startsWith("LONG:") && nVals == 2) {
          gpio.injectTouchLongPress(nx1, ny1);
          logSerial.printf("TOUCH_OK:LONG:%d:%d\n", v[0], v[1]);
        } else if (cmd.startsWith("TAP:") && nVals == 2) {
          gpio.injectTouchTap(nx1, ny1);
          logSerial.printf("TOUCH_OK:TAP:%d:%d\n", v[0], v[1]);
        } else {
          logSerial.printf("TOUCH_ERR:%s\n", cmd.c_str());
        }
      } else if (cmd == "BATT") {
        // Fuel-gauge diagnostics: raw SOC/voltage/charging as the gauge
        // reports them (e.g. to tell a "98% ceiling" gauge-sync issue from a
        // firmware display bug).
        static const BatteryMonitor debugBattery;
        const auto st = debugBattery.readStatus();
        logSerial.printf("BATT: pct=%u(known=%d) mv=%u(known=%d) charging=%d(known=%d) ext=%d(known=%d)\n",
                         st.percentage, st.percentageKnown, st.millivolts, st.millivoltsKnown, st.charging,
                         st.chargingKnown, st.externalPower, st.externalPowerKnown);
      } else if (cmd == "PWR") {
        // Power-draw snapshot. Pair with CMD:HIZ:1 or the current reading is
        // the charge current, not the system load.
        dumpPowerTelemetry();
      } else if (cmd == "FLSWEEP") {
        runFrontlightSweep();
      } else if (cmd == "BATTLOG") {
        // Dump the battery CSV over the wire, so reading a couple of days of
        // telemetry does not mean pulling the SD card out of the device.
        // Paced like the SCREENSHOT dump: HWCDC silently drops the tail of a
        // large write once its 256-byte TX ring overruns.
        HalFile logFile = Storage.open("/.crosspoint/battery.csv", O_RDONLY);
        if (!logFile) {
          logSerial.printf("BATTLOG_ERR:not_found\n");
        } else {
          const size_t total = logFile.fileSize();
          logSerial.printf("BATTLOG_START:%u\n", static_cast<unsigned>(total));
          uint8_t chunk[192];
          uint32_t stalls = 0;
          for (;;) {
            const int got = logFile.read(chunk, sizeof(chunk));
            if (got <= 0) break;
            // Retry the chunk in place rather than re-reading it: seeking back
            // on every stall is one more thing to get subtly wrong.
            int sent = 0;
            while (sent < got && stalls < 400) {
              const size_t w = logSerial.write(chunk + sent, got - sent);
              if (w == 0) {
                ++stalls;
                delay(5);
              } else {
                stalls = 0;
                sent += static_cast<int>(w);
                logSerial.flush();
              }
            }
            // Breathe between chunks. HWCDC can drop a chunk silently once the
            // host falls behind -- a first dump came back 874 of 1001 bytes with
            // no error anywhere, and a re-dump was complete.
            delay(2);
            if (stalls >= 400) break;  // host stopped draining entirely
          }
          logFile.close();
          logSerial.printf("\nBATTLOG_END\n");
        }
        logSerial.flush();
      } else if (cmd == "PWRPROF") {
        runPowerProfile();
      } else if (cmd == "LSLEEPRESULT") {
        dumpLightSleepResult();
      } else if (cmd.startsWith("LSLEEP:")) {
        runLightSleepProbe(static_cast<uint32_t>(cmd.substring(7).toInt()), true);
      } else if (cmd.startsWith("BASE:")) {
        runLightSleepProbe(static_cast<uint32_t>(cmd.substring(5).toInt()), false);
      } else if (cmd.startsWith("DSLEEP:")) {
#ifdef ENABLE_SERIAL_LOG
        // CMD:DSLEEP:<seconds> -- take the real deep-sleep path and come back
        // on a timer. The one thing a bench cannot otherwise do: a physical
        // button is the only way in and out, and the console does not survive
        // the trip, so a fault here (the panel PMIC left live, a release poll
        // that never returns, a reset instead of a sleep) has until now only
        // been visible as a hole in the battery log the morning after.
        // CMD:DSLEEP:<seconds>[:held] -- append :held to make the release poll
        // believe the power line never came back up, which is the branch that
        // decides between parking on a retry timer and waking straight back up
        // into a full boot.
        String rest = cmd.substring(7);
        bool simulateHeld = false;
        const int heldSep = rest.indexOf(':');
        if (heldSep >= 0) {
          simulateHeld = rest.substring(heldSep + 1) == "held";
          rest = rest.substring(0, heldSep);
        }
        const uint32_t secs = static_cast<uint32_t>(rest.toInt());
        logSerial.printf("DSLEEP_OK:%lu held=%d\n", static_cast<unsigned long>(secs), simulateHeld ? 1 : 0);
        logSerial.flush();
        freeink::PowerManager::armDebugTimerWake(secs > 0 ? secs : 20, simulateHeld);
        enterDeepSleep(false, simulateHeld ? "debug-held" : "debug-timer");
#endif
      } else if (cmd == "IDLE") {
#ifdef ENABLE_SERIAL_LOG
        // CMD:IDLE -- why the device is not saving power right now. Answers the
        // two questions the battery log cannot: what keeps resetting the
        // inactivity clock, and which guard is turning light sleep down.
        const unsigned long idleMs = millis() - lastActivityTime;
        logSerial.printf("IDLE:ms=%lu src=%s ls=%s entries=%lu rejects=%lu now=%s\n", idleMs, lastActivitySource,
                         lastLightSleepBlocker, static_cast<unsigned long>(lightSleepEntries),
                         static_cast<unsigned long>(lightSleepRejects),
                         lightSleepBlocker(idleMs) ? lightSleepBlocker(idleMs) : "clear");
        // src=key says a bound key is down, not which one. On a board where one
        // of them is an expander bit and four are bare solder pads, that is the
        // whole question.
        const auto& in = BoardConfig::ACTIVE.input;
        const int8_t keyPins[] = {in.back, in.confirm, in.left, in.right, in.up, in.down, in.power};
        for (const ConfigurableKey& key : CONFIGURABLE_KEYS) {
          const int8_t pin = key.button < 7 ? keyPins[key.button] : -1;
          // Both answers, because they can disagree: the pin is what the pad
          // reads right now, down= is what the debounced input layer believes.
          logSerial.printf("IDLE_KEY:btn=%u pin=%d level=%d down=%d\n", key.button, pin,
                           pin >= 0 ? digitalRead(pin) : -1, gpio.rawIsPressed(key.button) ? 1 : 0);
        }
        logSerial.flush();
#endif
      } else if (cmd.startsWith("LSFORCE:")) {
#ifdef ENABLE_SERIAL_LOG
        lightSleepForced = cmd.substring(8).toInt() != 0;
        logSerial.printf("LSFORCE_OK:%d\n", lightSleepForced ? 1 : 0);
        logSerial.flush();
#endif
      } else if (cmd.startsWith("BATTSIM:")) {
#ifdef ENABLE_SERIAL_LOG
        // CMD:BATTSIM:<pct>[:<mv>] -- see simBatteryPct.
        String rest = cmd.substring(8);
        const int colon = rest.indexOf(':');
        if (colon >= 0) {
          simBatteryMv = rest.substring(colon + 1).toInt();
          rest = rest.substring(0, colon);
        }
        simBatteryPct = rest.toInt();
        if (simBatteryPct < 0) simBatteryMv = -1;  // one switch turns the whole override off
        warnedLowBattery = false;
        warnedCriticalBattery = false;
        logSerial.printf("BATTSIM_OK:pct=%d mv=%d\n", simBatteryPct, simBatteryMv);
#endif
      } else if (cmd.startsWith("HIZ:")) {
        // CMD:HIZ:1 runs the board off its battery with USB still attached, so
        // the gauge measures real consumption; CMD:HIZ:0 restores charging.
        setChargerHiz(cmd.substring(4).toInt() != 0);
      } else if (cmd.startsWith("SET:") || cmd.startsWith("GET:")) {
        // CMD:SET:<jsonKey>:<value> / CMD:GET:<jsonKey> -- read or write any
        // uint8 setting by the same key the JSON API uses, so bringing up a
        // feature does not mean walking its screens by hand. Writes go through
        // saveToFile() like the UI does.
        const bool writing = cmd.startsWith("SET:");
        String rest = cmd.substring(4);
        int value = 0;
        if (writing) {
          const int colon = rest.lastIndexOf(':');
          if (colon < 0) {
            logSerial.printf("SET_ERR:need_key:value\n");
            logSerial.flush();
            return;
          }
          value = rest.substring(colon + 1).toInt();
          rest = rest.substring(0, colon);
        }
        const SettingInfo* found = nullptr;
        for (const auto& info : getSettingsList(&sdFontSystem.registry())) {
          if (info.key != nullptr && rest == info.key) {
            static SettingInfo copy;
            copy = info;
            found = &copy;
            break;
          }
        }
        if (found == nullptr || found->valuePtr == nullptr) {
          logSerial.printf("%s_ERR:unknown_key:%s\n", writing ? "SET" : "GET", rest.c_str());
        } else if (writing) {
          SETTINGS.*(found->valuePtr) = static_cast<uint8_t>(value);
          SETTINGS.saveToFile();
          logSerial.printf("SET_OK:%s=%u\n", rest.c_str(), SETTINGS.*(found->valuePtr));
        } else {
          logSerial.printf("GET:%s=%u\n", rest.c_str(), SETTINGS.*(found->valuePtr));
        }
        logSerial.flush();
      } else if (cmd.startsWith("GPIO:")) {
        // CMD:GPIO:<pin>[:<watchMs>] -- read a pin from the console, for
        // bringing up a wire before there is any firmware that uses it.
        // Configured INPUT_PULLUP, so a switch to GND reads 1 idle / 0 pressed;
        // with watchMs it polls and reports every change over that window.
        String rest = cmd.substring(5);
        unsigned long watchMs = 0;
        const int colon = rest.indexOf(':');
        if (colon >= 0) {
          watchMs = static_cast<unsigned long>(rest.substring(colon + 1).toInt());
          rest = rest.substring(0, colon);
        }
        const int pin = rest.toInt();
        if (pin < 0 || pin > 48) {
          logSerial.printf("GPIO_ERR:pin_out_of_range\n");
        } else {
          pinMode(pin, INPUT_PULLUP);
          delayMicroseconds(50);  // let the pull settle before the first sample
          int level = digitalRead(pin);
          logSerial.printf("GPIO:%d=%d (pullup; a switch to GND reads 0 when pressed)\n", pin, level);
          const unsigned long until = millis() + (watchMs > 30000 ? 30000 : watchMs);
          unsigned long changes = 0;
          while (millis() < until) {
            const int now = digitalRead(pin);
            if (now != level) {
              level = now;
              ++changes;
              logSerial.printf("GPIO:%d=%d t=%lums\n", pin, level, millis());
              logSerial.flush();
            }
            delay(5);
          }
          if (watchMs > 0) logSerial.printf("GPIO_DONE:%d changes=%lu\n", pin, changes);
          // Back to a plain input: INPUT_PULLUP on a pin the board drives (the
          // T5 S3's GPIO10 is the LoRa IRQ) is not a state to walk away from.
          pinMode(pin, INPUT);
        }
        logSerial.flush();
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  if (lastActivityTime == 0) lastActivityTime = millis();
  // Read and clear unconditionally: leaving it set behind a short-circuited ||
  // would hand the next pass an activity it did not have.
  const bool keyActivity = configurableKeyActivity;
  configurableKeyActivity = false;
  // Evaluated separately rather than as one || so the winner can be named. A
  // single term stuck true here silently disables both the auto-sleep timeout
  // and the idle light sleep, and the device just quietly stops saving power.
  const char* activitySource = nullptr;
  if (keyActivity) {
    activitySource = "key";
  } else if (gpio.wasAnyPressed() || gpio.wasAnyReleased()) {
    activitySource = "button";
  } else if (gpio.wasTouchActivity()) {
    activitySource = "touch";
  } else if (halTiltSensor.hadActivity()) {
    activitySource = "tilt";
  } else if (activityManager.preventAutoSleep()) {
    activitySource = "activity-holds";
  }
  if (activitySource != nullptr) {
    lastActivitySource = activitySource;
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  // Plugging or unplugging is someone handling the device, and it also changes
  // which rules apply: the inactivity timer is suspended on the cable, so the
  // idle clock can be minutes past the timeout by the time the cable comes out.
  // Without this the device slept the instant it was unplugged mid-page.
  {
    static bool wasCharging = gpio.isCharging();
    const bool charging = gpio.isCharging();
    if (charging != wasCharging) {
      wasCharging = charging;
      lastActivityTime = millis();
      LOG_DBG("SLP", "USB %s: idle timer restarted", charging ? "connected" : "disconnected");
    }
  }

  // Let wake continue as soon as its hold has been verified. The release can
  // arrive after setup, so consume that one input frame rather than making it
  // a page turn, refresh, or other short power-button action.
  if (wakePowerReleasePending && !gpio.isPressed(HalGPIO::BTN_POWER)) {
    wakePowerReleasePending = false;
    return;
  }

  static bool screenshotButtonsReleased = true;
  static bool screenshotComboActive = false;
  // raw: the user button is masked out of the normal queries (it drives
  // configurable actions), but the screenshot chord still wants its real state.
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.rawIsPressed(HalGPIO::BTN_DOWN)) {
    screenshotComboActive = true;
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  }
  if (screenshotComboActive) {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) return;
    if (gpio.wasReleased(HalGPIO::BTN_POWER)) {
      screenshotButtonsReleased = true;
      screenshotComboActive = false;
      return;
    }
    screenshotButtonsReleased = true;
    screenshotComboActive = false;
  }

  // Consume the second X4 Pro power-button release so it does not also run a
  // configured short-power action after toggling the frontlight.
  if (handleX4ProFrontlightDoubleClick()) {
    return;
  }

  // Configurable buttons: the capacitive Home key and the board's user button
  // (IO48 on the LilyGo T5S3, behind the PCA9535 expander) each run a
  // user-chosen action on tap and on hold.
  if (dispatchConfigurableButtons()) {
    return;
  }

#if FREEINK_CAP_TOUCH
  // A single X4 Pro power click becomes Confirm only after the frontlight
  // double-click window expires without a second click.
  mappedInputManager.setPowerConfirmClickFrame(false);
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PWR_CONFIRM && BoardConfig::isX4Pro() &&
      lastX4ProPowerClickAt != 0 && millis() - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
    lastX4ProPowerClickAt = 0;
    mappedInputManager.setPowerConfirmClickFrame(true);
  }
#endif

  // Sits with the other sleep guards, and after them: a device that is already
  // going to sleep for its own reasons does not need a battery toast first.
  if (millis() >= allowSleepAt && checkLowBattery()) {
    return;
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  // Not while the cable is in. Deep sleep on USB is all cost and no saving:
  // the charger is feeding SYS anyway, waking needs a button press, and the
  // console (and any serial tooling on it) dies with the CPU. A power-button
  // press still sleeps the device deliberately -- this only skips the
  // inactivity timer.
  //
  // isCharging(), not isUsbConnected(): the latter reads a dedicated detect
  // GPIO, which the T5 S3 does not have (usbDetect is PIN_UNASSIGNED there, so
  // it answers false with the cable plugged in -- the device still slept).
  // isCharging() falls back to the charger's PG_STAT, which stays set for as
  // long as external power is present, including after the pack tops off.
  if (sleepTimeoutMs > 0 && !gpio.isCharging() && millis() - lastActivityTime >= sleepTimeoutMs) {
    char why[24];
    snprintf(why, sizeof(why), "timeout-%lus", (millis() - lastActivityTime) / 1000UL);
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", millis() - lastActivityTime);
    enterDeepSleep(true, why);
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // A hold that woke the device must be released before it can count as a new
  // in-app long press. Otherwise a user who keeps holding after wake would put
  // the device straight back to sleep once allowSleepAt expires. The latch also
  // consults the raw pin: the debounced state can report a false release during
  // boot, which would arm the gate while the wake hold is still in progress.
  static bool powerReleasedSinceWake = false;
  if (!gpio.isPressed(HalGPIO::BTN_POWER) && !gpio.isPowerButtonPhysicallyPressed()) powerReleasedSinceWake = true;

  // BOOT/power hold runs its configured action (Sleep by default). The latch
  // keeps a continued hold from repeating it, and makes the eventual release
  // skip the short action so one press never runs both.
  static bool powerLongFired = false;
  if (powerLongFired) {
    if (!gpio.isPressed(HalGPIO::BTN_POWER) && !gpio.isPowerButtonPhysicallyPressed()) powerLongFired = false;
    return;
  }
  if (powerReleasedSinceWake && millis() >= allowSleepAt && gpio.isPowerButtonPhysicallyPressed() &&
      gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() > SETTINGS.getPowerButtonDuration()) {
    // If the screenshot combination is potentially being pressed, don't act
    if (gpio.rawIsPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    powerLongFired = true;
    runButtonAction(SETTINGS.pwrBtnLongAction);
    return;
    // With the default Sleep action this is unreachable — enterDeepSleep()
    return;
  }

#if FREEINK_DEVICE_PAPERMONO
  // Paper Mono reports the PMIC power button as a one-tick click, so the held
  // path above cannot fire. With the default Ignore action, retain the normal
  // power-button meaning and shut down; explicit alternate bindings still win.
  if ((SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP ||
       SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::IGNORE) &&
      millis() >= allowSleepAt && mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    enterDeepSleep();
    return;
  }
#endif

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
      mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    if (!activityManager.handleForcedRefresh()) {
      RenderLock lock;
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }

  BatteryLog::tick();

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  runDeferredPanelAction();
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      // Sleeping already consumed the idle time; falling through to delay(50)
      // as well would just add latency to the next button press.
      if (!tryIdleLightSleep(millis() - lastActivityTime)) {
        delay(50);
      }
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
