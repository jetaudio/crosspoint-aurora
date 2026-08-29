#pragma once

#include <BoardConfig.h>
#include <I18nKeys.h>
#include <stdint.h>

#include <array>

#include "CrossPointSettings.h"
#include "HalGPIO.h"

// Keys whose meaning belongs to the user, not to the activity on screen.
//
// A board key normally arrives with a fixed identity -- BTN_UP scrolls up, and
// every screen knows what that means. These do not: they are masked out of the
// ordinary button queries, and main.cpp's dispatcher is the only thing that
// sees them, turning a tap and a hold into whichever action the user picked in
// Settings -> Controls -> Key actions.
//
// They are still ordinary board pins, mapped into BoardConfig's input struct,
// which is what makes that mask the whole of the special-casing. Everything
// else -- pull-ups, debounce, press and release edges, waking the CPU from
// light sleep, counting as user activity against the inactivity timer -- is the
// HAL's job here exactly as it is for a key with a fixed identity, and getting
// all of it for free is the point. A pin read outside the HAL gets none of it:
// GPIO10 spent a firmware version being polled by hand in main.cpp, and paid
// for it with a ~600 ms tap latency and a device that deep-slept while its
// owner was reading, because the inactivity timer had never heard of it.
struct ConfigurableKey {
  uint8_t button;  // HalGPIO::BTN_*
  uint8_t CrossPointSettings::* shortAction;
  uint8_t CrossPointSettings::* longAction;
  StrId tapName;
  StrId holdName;
};

// The expander user button (labelled IO48 on the case). It has no board pin:
// BoardT5S3's input hook reports it as BTN_DOWN, which is why the profile
// leaves DOWN unassigned.
inline constexpr ConfigurableKey KEY_EXPANDER = {HalGPIO::BTN_DOWN, &CrossPointSettings::userBtnShortAction,
                                                 &CrossPointSettings::userBtnLongAction, StrId::STR_USER_BTN_TAP,
                                                 StrId::STR_USER_BTN_HOLD};

// The pads a T5 S3 Pro Lite has spare where the LoRa module would sit, named by
// the pin a key gets soldered to. See BoardT5S3Pins.h for why these and not the
// GPS pair, and why GPIO46 below is listed but never offered. Declared on every
// build (they are only data; the settings fields and strings exist everywhere)
// so that the row filter can recognise their names on a board without them.
inline constexpr ConfigurableKey KEY_G10 = {HalGPIO::BTN_UP, &CrossPointSettings::keyG10ShortAction,
                                            &CrossPointSettings::keyG10LongAction, StrId::STR_KEY_G10_TAP,
                                            StrId::STR_KEY_G10_HOLD};
inline constexpr ConfigurableKey KEY_G1 = {HalGPIO::BTN_LEFT, &CrossPointSettings::keyG1ShortAction,
                                           &CrossPointSettings::keyG1LongAction, StrId::STR_KEY_G1_TAP,
                                           StrId::STR_KEY_G1_HOLD};
// Kept as data even though no build lists it below: the settings rows for it
// still exist (fields, strings, the web API), and the row filter recognises a
// key by name to hide the rows of a key this board does not have. GPIO46 is not
// free on the T5 S3 -- the EPD i80 bus holds it as its DC pin -- so it is no
// longer offered as a key anywhere; see BoardT5S3Pins.h.
inline constexpr ConfigurableKey KEY_G46 = {HalGPIO::BTN_RIGHT, &CrossPointSettings::keyG46ShortAction,
                                            &CrossPointSettings::keyG46LongAction, StrId::STR_KEY_G46_TAP,
                                            StrId::STR_KEY_G46_HOLD};
inline constexpr ConfigurableKey KEY_G47 = {HalGPIO::BTN_BACK, &CrossPointSettings::keyG47ShortAction,
                                            &CrossPointSettings::keyG47LongAction, StrId::STR_KEY_G47_TAP,
                                            StrId::STR_KEY_G47_HOLD};

// What this build actually has, in the order Key actions lists them.
#if FREEINK_DEVICE_LILYGO && !T5S3_HAS_LORA_GPS
inline constexpr std::array<ConfigurableKey, 4> CONFIGURABLE_KEYS = {KEY_EXPANDER, KEY_G10, KEY_G1, KEY_G47};
#elif FREEINK_DEVICE_LILYGO
// Pro: the radio owns those four pins, and the expander key is all that is left.
inline constexpr std::array<ConfigurableKey, 1> CONFIGURABLE_KEYS = {KEY_EXPANDER};
#else
inline constexpr std::array<ConfigurableKey, 0> CONFIGURABLE_KEYS = {};
#endif

// The bits main.cpp hides from the ordinary button queries.
inline constexpr uint8_t configurableKeyMask() {
  uint8_t mask = 0;
  for (const ConfigurableKey& key : CONFIGURABLE_KEYS) {
    mask = static_cast<uint8_t>(mask | (1u << key.button));
  }
  return mask;
}

// True for any settings row that binds a configurable key, on any board.
inline bool isConfigurableKeyRow(const StrId nameId) {
  for (const ConfigurableKey& key : {KEY_EXPANDER, KEY_G10, KEY_G1, KEY_G46, KEY_G47}) {
    if (key.tapName == nameId || key.holdName == nameId) return true;
  }
  return false;
}

// Whether a press of this board's own keys pages the book by itself. False when
// every key it has is configurable: the settings that describe page-turn
// buttons ("Side button layout", "Long-press button behaviour") then have no
// press to act on, whatever the input pins say.
inline bool hasPageTurnButtons() {
  constexpr uint8_t mask = configurableKeyMask();
  const auto pages = [](const int8_t pin, const uint8_t button) {
    return pin >= 0 && (mask & static_cast<uint8_t>(1u << button)) == 0;
  };
  return pages(BoardConfig::ACTIVE.input.up, HalGPIO::BTN_UP) ||
         pages(BoardConfig::ACTIVE.input.down, HalGPIO::BTN_DOWN);
}
