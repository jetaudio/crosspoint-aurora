#pragma once

#include <BoardConfig.h>
#include <HalClock.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "ConfigurableKeys.h"
#include "CrossPointSettings.h"
#include "HomeButtonSettings.h"
#include "KOReaderCredentialStore.h"
#include "ReaderFontSizes.h"
#include "SystemFont.h"
#include "activities/settings/SettingsActivity.h"
#include "components/UITheme.h"
#include "util/DictionaryRegistry.h"

// Build the font family setting dynamically. When registry is non-null, SD card fonts
// are appended after the built-in fonts. Otherwise only built-in fonts are listed.
inline SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry) {
  // Built-in font labels (StrId)
  std::vector<StrId> enumValues = {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS};
  // Runtime string labels for SD card fonts
  std::vector<std::string> enumStringValues;

  // Reserve: first CrossPointSettings::BUILTIN_FONT_COUNT entries use StrId, rest use strings
  if (registry) {
    const auto& families = registry->getFamilies();
    enumStringValues.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(enumStringValues),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  // Capture the SD font count for the lambdas
  const int sdFontCount = static_cast<int>(enumStringValues.size());

  // Total option count = built-in + SD card families
  // For the combined enumStringValues: we need all entries as strings (built-in names + SD names)
  // The render code checks enumStringValues first, then enumValues. So we build enumStringValues
  // with all options when SD fonts are present.
  std::vector<std::string> allStringValues;
  if (sdFontCount > 0) {
    allStringValues.push_back(I18N.get(StrId::STR_NOTO_SERIF));
    allStringValues.push_back(I18N.get(StrId::STR_NOTO_SANS));
    allStringValues.insert(allStringValues.end(), enumStringValues.begin(), enumStringValues.end());
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_FAMILY;
  s.type = SettingType::ENUM;
  s.enumValues = std::move(enumValues);
  s.enumStringValues = std::move(allStringValues);
  s.key = "fontFamily";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-family entry it replaces

  // Capture registry families by copy for the lambdas
  std::vector<std::string> sdFamilyNames;
  if (registry) {
    const auto& families = registry->getFamilies();
    sdFamilyNames.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(sdFamilyNames),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  s.valueGetter = [sdFamilyNames]() -> uint8_t {
    // If an SD card font is selected, find its index
    if (SETTINGS.sdFontFamilyName[0] != '\0') {
      for (int i = 0; i < static_cast<int>(sdFamilyNames.size()); i++) {
        if (sdFamilyNames[i] == SETTINGS.sdFontFamilyName) {
          return static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i);
        }
      }
      // SD font name not found in registry — fall through to built-in
    }
    return SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  };

  s.valueSetter = [sdFamilyNames](uint8_t v) {
    if (v < CrossPointSettings::BUILTIN_FONT_COUNT) {
      SETTINGS.fontFamily = v;
      SETTINGS.sdFontFamilyName[0] = '\0';
    } else {
      int sdIdx = v - CrossPointSettings::BUILTIN_FONT_COUNT;
      if (sdIdx < static_cast<int>(sdFamilyNames.size())) {
        strncpy(SETTINGS.sdFontFamilyName, sdFamilyNames[sdIdx].c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
        SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      }
    }
  };

  return s;
}

// Build the font size setting dynamically: the options are the point sizes the
// active family actually ships, so an SD family built at 10/12/14 offers three
// sizes and a family built at 8..18 offers six. The selected point size persists
// in SETTINGS.fontPointSize (saved/loaded manually in CrossPointSettings::
// toJson/fromJson — the generic loop skips dynamic entries), while the ENUM
// contract shared with the web UI stays index-based.
inline SettingInfo buildFontSizeSetting(const SdCardFontRegistry* registry) {
  // Captured by copy: getSettingsList() returns by value and the lambdas outlive
  // this call, so they must not reference the registry.
  const std::vector<uint8_t> sizes = readerFontPointSizes(registry, SETTINGS.sdFontFamilyName);

  // "pt" is deliberately not translated — see the matching note in
  // TextSettingsActivity::rebuildSizeList().
  std::vector<std::string> labels;
  labels.reserve(sizes.size());
  for (const uint8_t pt : sizes) {
    labels.push_back(std::to_string(pt) + " pt");
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_SIZE;
  s.type = SettingType::ENUM;
  s.enumStringValues = std::move(labels);
  s.key = "fontSize";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-size entry it replaces

  s.valueGetter = [sizes]() -> uint8_t {
    const uint8_t pt = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
    for (int i = 0; i < static_cast<int>(sizes.size()); i++) {
      if (sizes[i] == pt) return static_cast<uint8_t>(i);
    }
    return 0;
  };

  s.valueSetter = [sizes](uint8_t v) {
    if (v < sizes.size()) SETTINGS.fontPointSize = sizes[v];
  };

  return s;
}

// Build the dictionary selection setting dynamically from the folders discovered
// under /dictionaries. "None" plus one option per dictionary; the selected folder
// name persists in SETTINGS.dictionaryName (saved/loaded manually in
// CrossPointSettings::toJson/fromJson — the generic loop skips dynamic entries).
inline SettingInfo buildDictionarySetting(const std::vector<DictionaryEntry>& dictionaries) {
  std::vector<std::string> folderNames;
  folderNames.reserve(dictionaries.size());
  std::transform(dictionaries.begin(), dictionaries.end(), std::back_inserter(folderNames),
                 [](const DictionaryEntry& d) { return d.name; });

  SettingInfo s;
  s.nameId = StrId::STR_DICTIONARY;
  s.type = SettingType::ENUM;
  s.enumStringValues.reserve(folderNames.size() + 1);
  s.enumStringValues.push_back(I18N.get(StrId::STR_NONE_OPT));
  s.enumStringValues.insert(s.enumStringValues.end(), folderNames.begin(), folderNames.end());
  s.category = StrId::STR_CAT_READER;

  s.valueGetter = [folderNames]() -> uint8_t {
    for (size_t i = 0; i < folderNames.size(); i++) {
      // Compare within the settings field capacity: an over-long folder name is
      // stored truncated, and must still match its list entry.
      if (strncmp(folderNames[i].c_str(), SETTINGS.dictionaryName, sizeof(SETTINGS.dictionaryName) - 1) == 0) {
        return static_cast<uint8_t>(i + 1);
      }
    }
    return 0;  // "None", also when the stored folder no longer exists
  };

  s.valueSetter = [folderNames](uint8_t v) {
    if (v == 0 || v > folderNames.size()) {
      SETTINGS.dictionaryName[0] = '\0';
      return;
    }
    strncpy(SETTINGS.dictionaryName, folderNames[v - 1].c_str(), sizeof(SETTINGS.dictionaryName) - 1);
    SETTINGS.dictionaryName[sizeof(SETTINGS.dictionaryName) - 1] = '\0';
  };

  return s;
}

// Labels for CrossPointSettings::BUTTON_ACTION, in enum order. Every
// configurable key (BOOT hold, the IO48 user button, the capacitive Home key)
// picks from this one list, so a new action is added in exactly two places:
// the enum and here.
inline std::vector<StrId> buttonActionValues() {
  return {StrId::STR_ACTION_NONE,     StrId::STR_PAGE_NEXT,   StrId::STR_PAGE_PREV,      StrId::STR_ACTION_BACK,
          StrId::STR_ACTION_HOME,     StrId::STR_READER_MENU, StrId::STR_CONTROL_CENTER, StrId::STR_NIGHT_MODE,
          StrId::STR_FORCE_REFRESH,   StrId::STR_FRONTLIGHT,  StrId::STR_TOUCH_TOGGLE,   StrId::STR_SLEEP,
          StrId::STR_ACTION_POWER_OFF};
}

inline std::vector<StrId> buildLongPressMenuValues() {
  static constexpr StrId VALUES[] = {StrId::STR_KOSYNC, StrId::STR_DISABLED, StrId::STR_BOOKMARK_OPTION,
                                     StrId::STR_DICTIONARY, StrId::STR_READER_MENU};
  const size_t count = BoardConfig::hasHomeKey() ? std::size(VALUES) : std::size(VALUES) - 1;
  return {VALUES, VALUES + count};
}

// Build the drop-cap font picker from the standalone /.dropcap registry. Index 0
// is "Default" (empty dropCapFontName = integer-scale the body glyph); the
// remaining entries are the discovered drop-cap families. Device-only (key=null):
// dropCapFontName is persisted manually in JsonSettingsIO.
inline SettingInfo buildDropCapFontSetting(const SdCardFontRegistry* registry) {
  std::vector<std::string> names;
  if (registry) {
    const auto& families = registry->getFamilies();
    names.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(names),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  std::vector<std::string> labels;
  labels.reserve(names.size() + 1);
  labels.push_back(I18N.get(StrId::STR_DROP_CAP_FONT_DEFAULT));
  labels.insert(labels.end(), names.begin(), names.end());

  SettingInfo s;
  s.nameId = StrId::STR_DROP_CAP_FONT;
  s.type = SettingType::ENUM;
  s.enumStringValues = std::move(labels);
  s.category = StrId::STR_CAT_READER;

  s.valueGetter = [names]() -> uint8_t {
    if (SETTINGS.dropCapFontName[0] != '\0') {
      for (int i = 0; i < static_cast<int>(names.size()); i++) {
        if (names[i] == SETTINGS.dropCapFontName) return static_cast<uint8_t>(i + 1);
      }
    }
    return 0;  // Default
  };

  s.valueSetter = [names](uint8_t v) {
    if (v == 0) {
      SETTINGS.dropCapFontName[0] = '\0';
      return;
    }
    const int idx = v - 1;
    if (idx < static_cast<int>(names.size())) {
      strncpy(SETTINGS.dropCapFontName, names[idx].c_str(), sizeof(SETTINGS.dropCapFontName) - 1);
      SETTINGS.dropCapFontName[sizeof(SETTINGS.dropCapFontName) - 1] = '\0';
    }
  };

  return s;
}

// Theme labels in UI_THEME order. Aurora took index 4 before upstream added the
// cover grid, so the grid is 5 here; it is dropped on boards that cannot run it.
inline std::vector<StrId> homeThemeValues() {
  static constexpr StrId VALUES[] = {StrId::STR_THEME_CLASSIC,     StrId::STR_THEME_LYRA,
                                     StrId::STR_THEME_LYRA_EXTENDED, StrId::STR_THEME_ROUNDEDRAFF,
                                     StrId::STR_THEME_AURORA,        StrId::STR_THEME_COVER_GRID};
  const size_t count = UITheme::supportsCoverGrid() ? std::size(VALUES) : std::size(VALUES) - 1;
  return {VALUES, VALUES + count};
}

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
//
// The static list is constructed exactly once (master's optimization, #1086 +
// #1636) so the per-entry SettingInfo cost is paid once; every call then copies
// it. When an SdCardFontRegistry is supplied AND has SD card fonts installed,
// the font-family entry is replaced in that copy with a registry-aware version.
// The font-size entry is always rebuilt, since its options are point sizes read
// from the active family rather than a fixed enum.
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr,
                                                const std::vector<DictionaryEntry>* dictionaries = nullptr,
                                                const SdCardFontRegistry* dropCapRegistry = nullptr) {
  static const std::vector<SettingInfo> baseList = [] {
    // Enum settings are persisted as numeric values. Assign these labels by enum
    // value so a reordered menu or enum cannot silently swap their behavior.
    std::vector<StrId> sleepScreenValues(CrossPointSettings::SLEEP_SCREEN_MODE_COUNT);
    sleepScreenValues[CrossPointSettings::DARK] = StrId::STR_DARK;
    sleepScreenValues[CrossPointSettings::LIGHT] = StrId::STR_LIGHT;
    sleepScreenValues[CrossPointSettings::CUSTOM] = StrId::STR_CUSTOM;
    sleepScreenValues[CrossPointSettings::COVER] = StrId::STR_COVER;
    sleepScreenValues[CrossPointSettings::COVER_CUSTOM] = StrId::STR_COVER_CUSTOM;
    sleepScreenValues[CrossPointSettings::BLANK] = StrId::STR_NONE_OPT;
    sleepScreenValues[CrossPointSettings::QUICK_RESUME] = StrId::STR_QUICK_RESUME;
    sleepScreenValues[CrossPointSettings::TRANSPARENT_CUSTOM] = StrId::STR_TRANSPARENT;

    std::vector<StrId> statusBarClockValues(CrossPointSettings::STATUS_BAR_CLOCK_MODE_COUNT);
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_HIDE] = StrId::STR_HIDE;
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_RIGHT] = StrId::STR_DIR_RIGHT;
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_LEFT] = StrId::STR_DIR_LEFT;

    std::vector<SettingInfo> v = {
        // --- Display ---
        // Which way up the whole UI is drawn, not just the book: it moved out
        // of Reader when it stopped being the reader's own setting.
        SettingInfo::Enum(
            StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
            {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW},
            "orientation", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen, std::move(sleepScreenValues),
                          "sleepScreen", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                          {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                          {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                          "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_QUICK_RESUME_TIMEOUT, &CrossPointSettings::quickResumeSleepScreen,
                          {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "quickResumeSleepScreen",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                          {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
                          {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15,
                           StrId::STR_PAGES_30, StrId::STR_NEVER},
                          "refreshFrequency", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_UI_THEME, &CrossPointSettings::uiTheme, homeThemeValues(), "uiTheme",
                          StrId::STR_CAT_DISPLAY),
        // System (UI) font face. Dynamic setter swaps the live UI font and clears
        // the glyph cache so the change is visible without a reboot.
        SettingInfo::DynamicEnum(
            StrId::STR_SYSTEM_FONT,
            {StrId::STR_NOTO_SANS, StrId::STR_UBUNTU, StrId::STR_EB_GARAMOND, StrId::STR_SFU_GOUDY},
            []() -> uint8_t { return SETTINGS.systemFont; },
            [](uint8_t v) {
              SETTINGS.systemFont = v < CrossPointSettings::SYSTEM_FONT_COUNT ? v : 0;
              applySystemUiFont();
            },
            "systemFont", StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                            StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_LIGHT_SLEEP_IDLE, &CrossPointSettings::lightSleepIdle, "lightSleepIdle",
                            StrId::STR_CAT_SYSTEM),
#if FREEINK_CAP_FRONTLIGHT
        SettingInfo::Toggle(StrId::STR_RESTORE_LIGHT_ON_WAKE, &CrossPointSettings::frontlightRestoreOnWake,
                            "frontlightRestoreOnWake", StrId::STR_CAT_DISPLAY),
#endif
        // Night mode = inverted output polarity everywhere (ActivityManager
        // applies it to every activity), so it lives in the Display category.
        SettingInfo::Toggle(StrId::STR_NIGHT_MODE, &CrossPointSettings::screenInverted, "screenInverted",
                            StrId::STR_CAT_DISPLAY),

        // --- Reader ---
        // Built-in font-family entry. Replaced per-call with a registry-aware
        // version when SD fonts are installed.
        SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                          {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS}, "fontFamily", StrId::STR_CAT_READER)
            .withTextSettings(),
        // Placeholder: the selectable sizes depend on the active font family, so
        // this entry is always replaced by buildFontSizeSetting() below. It only
        // fixes the setting's position in the Reader category.
        SettingInfo::Enum(StrId::STR_FONT_SIZE, nullptr, {}, "fontSize", StrId::STR_CAT_READER).withTextSettings(),
        SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                          {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE}, "lineSpacing",
                          StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Value(StrId::STR_WORD_SPACING, &CrossPointSettings::wordSpacing,
                           {CrossPointSettings::WORD_SPACING_MIN, CrossPointSettings::WORD_SPACING_MAX,
                            CrossPointSettings::WORD_SPACING_STEP},
                           "wordSpacing", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_CHARACTER_SPACING, &CrossPointSettings::characterSpacing,
                          {StrId::STR_SPACING_MINUS_2, StrId::STR_SPACING_MINUS_1, StrId::STR_SPACING_ZERO,
                           StrId::STR_SPACING_PLUS_1, StrId::STR_SPACING_PLUS_2},
                          "characterSpacing", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin,
                           {CrossPointSettings::SCREEN_MARGIN_MIN, CrossPointSettings::SCREEN_MARGIN_MAX,
                            CrossPointSettings::SCREEN_MARGIN_STEP},
                           "screenMargin", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                          {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                           StrId::STR_BOOK_S_STYLE},
                          "paragraphAlignment", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_FOCUS_READING, &CrossPointSettings::focusReadingEnabled, "focusReadingEnabled",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_DROP_CAPS, &CrossPointSettings::dropCapsEnabled, "dropCapsEnabled",
                            StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_SMALL_CAPS, &CrossPointSettings::smallCapsFirstLine, "smallCapsFirstLine",
                            StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled, "hyphenationEnabled",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing,
                            "extraParagraphSpacing", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_STROKE_WEIGHT, &CrossPointSettings::textStrokeWeight,
                          {StrId::STR_WEIGHT_THINNEST, StrId::STR_WEIGHT_THIN, StrId::STR_WEIGHT_NORMAL,
                           StrId::STR_WEIGHT_THICK, StrId::STR_WEIGHT_THICKEST},
                          "textStrokeWeight", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                          {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                          "imageRendering", StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_READER_MENU_STYLE, &CrossPointSettings::readerMenuStyle,
                          {StrId::STR_MENU_STYLE_LIST, StrId::STR_MENU_STYLE_TOOLBAR}, "readerMenuStyle",
                          StrId::STR_CAT_READER),
        // Night mode = inverted output polarity on the reading surfaces only
        // (EPUB/TXT/XTC; ActivityManager resolves the polarity per render).
        // Reader category, since it does not affect the rest of the UI.
        SettingInfo::Toggle(StrId::STR_NIGHT_MODE, &CrossPointSettings::screenInverted, "screenInverted",
                            StrId::STR_CAT_READER),
        // --- Controls. Three runs, in the order someone looks for them:
        // what the glass does, what the buttons do, and which action each key
        // is bound to. The category is the longest in Settings, and reading it
        // as one undivided list was the complaint that prompted the split.

        // Touch.
        SettingInfo::Toggle(StrId::STR_TOUCH_READER_CONTROLS, &CrossPointSettings::touchReaderControls,
                            "touchReaderControls", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_NEXT_PAGE_GESTURE, &CrossPointSettings::pageTurnGesture,
                          {StrId::STR_TAP_AND_SWIPE, StrId::STR_TAP_ONLY, StrId::STR_SWIPE_ONLY,
                           StrId::STR_INVERTED_TAP, StrId::STR_DISABLED},
                          "pageTurnGesture", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_PREV_PAGE_GESTURE, &CrossPointSettings::previousPageGesture,
                          {StrId::STR_TAP_AND_SWIPE, StrId::STR_TAP_ONLY, StrId::STR_SWIPE_ONLY,
                           StrId::STR_INVERTED_TAP, StrId::STR_DISABLED},
                          "previousPageGesture", StrId::STR_CAT_CONTROLS),
        // Persisted under the legacy "tapForReaderMenu" key: old saves map
        // 0 = Off, 1 = Tap.
        SettingInfo::Enum(StrId::STR_SHOW_READER_MENU, &CrossPointSettings::showReaderMenu,
                          {StrId::STR_STATE_OFF, StrId::STR_STATE_TAP, StrId::STR_STATE_SWIPE_UP}, "tapForReaderMenu",
                          StrId::STR_CAT_CONTROLS),
        // (Tilt page turn is inserted here too, on the boards that have an IMU.)

        // Buttons: what a press means, before which key does it.
        SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                          {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED, StrId::STR_NEXT_NEXT,
                           StrId::STR_PREV_PREV},
                          "sideButtonLayout", StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, &CrossPointSettings::frontButtonFollowOrientation,
                            "frontButtonFollowOrientation", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(
            StrId::STR_SHOW_BUTTON_HINTS, &CrossPointSettings::showButtonHints,
            {StrId::STR_BUTTON_HINTS_FRONT_ONLY, StrId::STR_BUTTON_HINTS_FRONT_EDGE, StrId::STR_BUTTON_HINTS_OFF},
            "showButtonHints", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                          {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                           StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION},
                          "longPressButtonBehavior", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_LONG_PRESS_MENU, &CrossPointSettings::longPressMenuFunction,
                          buildLongPressMenuValues(), "longPressMenuFunction", StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_BACK_SHORT_TO_FILE_BROWSER, &CrossPointSettings::backShortToFileBrowser,
                            "backShortToFileBrowser", StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_PWR_BTN_FOOTNOTE_BACK, &CrossPointSettings::pwrBtnFootnoteBack,
                            "pwrBtnFootnoteBack", StrId::STR_CAT_CONTROLS),

        // Key actions: one tap row and one hold row per key, each drawing on
        // the shared BUTTON_ACTION list (order must match that enum). Rows for
        // keys a board does not have are filtered out below. The capacitive
        // Home key is appended further down (home_button::*, HomeButtonAction).
        SettingInfo::Enum(StrId::STR_USER_BTN_TAP, &CrossPointSettings::userBtnShortAction, buttonActionValues(),
                          "userBtnShortAction", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_USER_BTN_HOLD, &CrossPointSettings::userBtnLongAction, buttonActionValues(),
                          "userBtnLongAction", StrId::STR_CAT_CONTROLS),
        // The four spare pads on a T5 S3 Pro Lite, named by pin. Filtered out
        // below on every build whose board does not offer them.
        SettingInfo::Enum(StrId::STR_KEY_G10_TAP, &CrossPointSettings::keyG10ShortAction, buttonActionValues(),
                          "keyG10ShortAction", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_KEY_G10_HOLD, &CrossPointSettings::keyG10LongAction, buttonActionValues(),
                          "keyG10LongAction", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_KEY_G1_TAP, &CrossPointSettings::keyG1ShortAction, buttonActionValues(),
                          "keyG1ShortAction", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_KEY_G1_HOLD, &CrossPointSettings::keyG1LongAction, buttonActionValues(),
                          "keyG1LongAction", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_KEY_G46_TAP, &CrossPointSettings::keyG46ShortAction, buttonActionValues(),
                          "keyG46ShortAction", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_KEY_G46_HOLD, &CrossPointSettings::keyG46LongAction, buttonActionValues(),
                          "keyG46LongAction", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_KEY_G47_TAP, &CrossPointSettings::keyG47ShortAction, buttonActionValues(),
                          "keyG47ShortAction", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_KEY_G47_HOLD, &CrossPointSettings::keyG47LongAction, buttonActionValues(),
                          "keyG47LongAction", StrId::STR_CAT_CONTROLS),
    // BOOT tap keeps its own option list: Confirm/Footnotes are power-button
    // specific, and the stored indices are load-bearing (SHORT_PWRBTN).
    // Tap sits before hold, matching the other key pairs above.
#if FREEINK_CAP_TOUCH
        SettingInfo::Enum(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
                          {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_NEXT, StrId::STR_FORCE_REFRESH,
                           StrId::STR_FOOTNOTES, StrId::STR_CONFIRM, StrId::STR_PAGE_PREV},
                          "shortPwrBtn", StrId::STR_CAT_CONTROLS),
#else
        SettingInfo::Enum(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
                          {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_NEXT, StrId::STR_FORCE_REFRESH,
                           StrId::STR_FOOTNOTES, StrId::STR_DISABLED, StrId::STR_PAGE_PREV},
                          "shortPwrBtn", StrId::STR_CAT_CONTROLS),
#endif
        SettingInfo::Enum(StrId::STR_PWR_BTN_HOLD, &CrossPointSettings::pwrBtnLongAction, buttonActionValues(),
                          "pwrBtnLongAction", StrId::STR_CAT_CONTROLS),

        // --- System ---
        SettingInfo::Value(
            StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
            {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
            "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM),
        // Filtered out below on boards whose charger cannot cut the pack.
        SettingInfo::Enum(StrId::STR_TIMEOUT_ACTION, &CrossPointSettings::sleepTimeoutAction,
                          {StrId::STR_SLEEP, StrId::STR_ACTION_POWER_OFF}, "sleepTimeoutAction", StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles, "showHiddenFiles",
                            StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_LIBRARY_USE_METADATA, &CrossPointSettings::libraryUseMetadata,
                            "libraryUseMetadata", StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_REMOVE_READ_FROM_RECENTS, &CrossPointSettings::removeReadBooksFromRecents,
                            "removeReadBooksFromRecents", StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_MOVE_FINISHED_TO_READ, &CrossPointSettings::moveFinishedToReadFolder,
                            "moveFinishedToReadFolder", StrId::STR_CAT_SYSTEM),

        // OPDS download folder: persisted + web-exposed, but category-less so it
        // is hidden from the on-device Settings screen (edited via OPDS UI).
        SettingInfo::String(StrId::STR_OPDS_DOWNLOAD_FOLDER, &SETTINGS.opdsDownloadFolder[0],
                            sizeof(SETTINGS.opdsDownloadFolder), "opdsDownloadFolder"),
        // OPDS download filename format: persisted + web-exposed, category-less so it
        // is hidden from the on-device Settings screen (cycled from the OPDS UI).
        SettingInfo::Enum(StrId::STR_OPDS_FILENAME_FORMAT, &CrossPointSettings::opdsFilenameFormat,
                          {StrId::STR_FMT_AUTHOR_TITLE, StrId::STR_FMT_TITLE_AUTHOR, StrId::STR_FMT_TITLE},
                          "opdsFilenameFormat"),

        // Frontlight quick-panel state: persisted and web-exposed, but hidden
        // from the on-device Settings screen because the swipe panel owns it.
        SettingInfo::Value(StrId::STR_BRIGHTNESS, &CrossPointSettings::frontlightBrightness, {0, 100, 5},
                           "frontlightBrightness"),
#if FREEINK_CAP_WARMLIGHT
        SettingInfo::Value(StrId::STR_WARMTH, &CrossPointSettings::frontlightWarmth, {0, 100, 5}, "frontlightWarmth"),
#endif
        SettingInfo::Toggle(StrId::STR_FRONTLIGHT, &CrossPointSettings::frontlightOn, "frontlightOn"),

        // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---
        SettingInfo::DynamicString(
            StrId::STR_KOREADER_USERNAME, [] { return KOREADER_STORE.getUsername(); },
            [](const std::string& v) {
              KOREADER_STORE.setCredentials(v, KOREADER_STORE.getPassword());
              KOREADER_STORE.saveToFile();
            },
            "koUsername", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicString(
            StrId::STR_KOREADER_PASSWORD, [] { return KOREADER_STORE.getPassword(); },
            [](const std::string& v) {
              KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), v);
              KOREADER_STORE.saveToFile();
            },
            "koPassword", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicString(
            StrId::STR_SYNC_SERVER_URL, [] { return KOREADER_STORE.getServerUrl(); },
            [](const std::string& v) {
              KOREADER_STORE.setServerUrl(v);
              KOREADER_STORE.saveToFile();
            },
            "koServerUrl", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicEnum(
            StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod()); },
            [](uint8_t v) {
              KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(v));
              KOREADER_STORE.saveToFile();
            },
            "koMatchMethod", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicEnum(
            StrId::STR_SEND_METADATA, {StrId::STR_STATE_OFF, StrId::STR_STATE_ON},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getSendMetadata()); },
            [](uint8_t v) {
              KOREADER_STORE.setSendMetadata(v != 0);
              KOREADER_STORE.saveToFile();
            },
            "koSendMetadata", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicEnum(
            StrId::STR_SYNC_BEHAVIOR, {StrId::STR_ASK_EVERY_TIME, StrId::STR_SMART_SYNC},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getSyncBehavior()); },
            [](uint8_t v) {
              KOREADER_STORE.setSyncBehavior(static_cast<KOReaderSyncBehavior>(v));
              KOREADER_STORE.saveToFile();
            },
            "koSyncBehavior", StrId::STR_KOREADER_SYNC),
        // --- Status Bar Settings (web-only, uses StatusBarSettingsActivity) ---
        SettingInfo::Toggle(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount,
                            "statusBarChapterPageCount", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Toggle(StrId::STR_BOOK_PROGRESS_PERCENTAGE, &CrossPointSettings::statusBarBookProgressPercentage,
                            "statusBarBookProgressPercentage", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &CrossPointSettings::statusBarProgressBar,
                          {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarProgressBar",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarProgressBarThickness,
                          {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK},
                          "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::statusBarTitle,
                          {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarTitle",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Toggle(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery, "statusBarBattery",
                            StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_XTC_STATUS_BAR, &CrossPointSettings::xtcStatusBarMode,
                          {StrId::STR_HIDE, StrId::STR_BOTTOM, StrId::STR_TOP}, "xtcStatusBarMode",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        // Clock entries (persistence + web settings; the device UI is
        // ClockSettingsActivity under System settings).
        SettingInfo::Enum(StrId::STR_CLOCK, &CrossPointSettings::statusBarClock, std::move(statusBarClockValues),
                          "statusBarClock", StrId::STR_CUSTOMISE_STATUS_BAR),
        // LEGACY: retired quarter-hour UTC offset (biased by 48). Persisted so
        // timezones::activeIndex() can migrate it into clockTimezone.
        SettingInfo::Value(StrId::STR_CLOCK, &CrossPointSettings::clockUtcOffsetQ, {0, 104, 1}, "clockUtcOffsetQ",
                           StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_CLOCK_FORMAT, &CrossPointSettings::clockFormat,
                          {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H}, "clockFormat",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        // Index into the append-only table in src/util/Timezones.cpp; 255 = unset.
        SettingInfo::Value(StrId::STR_TIMEZONE, &CrossPointSettings::clockTimezone, {0, 255, 1}, "clockTimezone",
                           StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_CLOCK_DST, &CrossPointSettings::clockDst,
                          {StrId::STR_CLOCK_DST_AUTO, StrId::STR_STATE_ON, StrId::STR_STATE_OFF}, "clockDst",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Toggle(StrId::STR_CLOCK_IN_HEADER, &CrossPointSettings::clockShowInHeader, "clockShowHeader",
                            StrId::STR_CUSTOMISE_STATUS_BAR),
        // Persistence flag for NTP debounce. Resetting from the web UI forces a re-sync
        // on next WiFi connect, which is useful when crossing time zones.
        SettingInfo::Toggle(StrId::STR_CLOCK_SYNCED, &CrossPointSettings::clockHasBeenSynced, "clockHasBeenSynced",
                            StrId::STR_CUSTOMISE_STATUS_BAR),
        // --- Control Center contents (web-only, uses ControlCenterSettingsActivity) ---
        // One toggle per quick-setting tile, in the panel's grid order. The
        // frontlight row has no flag: it is what the panel is for.
        SettingInfo::Toggle(StrId::STR_NIGHT_MODE, &CrossPointSettings::ccTileNightMode, "ccTileNightMode",
                            StrId::STR_CUSTOMISE_CONTROL_CENTER),
        SettingInfo::Toggle(StrId::STR_FORCE_REFRESH, &CrossPointSettings::ccTileRefresh, "ccTileRefresh",
                            StrId::STR_CUSTOMISE_CONTROL_CENTER),
        SettingInfo::Toggle(StrId::STR_ORIENTATION, &CrossPointSettings::ccTileOrientation, "ccTileOrientation",
                            StrId::STR_CUSTOMISE_CONTROL_CENTER),
        SettingInfo::Toggle(StrId::STR_TOUCH_TOGGLE, &CrossPointSettings::ccTileTouch, "ccTileTouch",
                            StrId::STR_CUSTOMISE_CONTROL_CENTER),
        SettingInfo::Toggle(StrId::STR_SCREENSHOT_BUTTON, &CrossPointSettings::ccTileScreenshot, "ccTileScreenshot",
                            StrId::STR_CUSTOMISE_CONTROL_CENTER),
        SettingInfo::Toggle(StrId::STR_SLEEP, &CrossPointSettings::ccTileSleep, "ccTileSleep",
                            StrId::STR_CUSTOMISE_CONTROL_CENTER),
    };
    // Double-click power frontlight shortcut only exists on the X4 Pro
    if (BoardConfig::isX4Pro()) {
      for (auto it = v.begin(); it != v.end(); ++it) {
        if (it->nameId == StrId::STR_SHORT_PWR_BTN) {
          v.insert(it, SettingInfo::Toggle(StrId::STR_DBL_CLICK_PWR_LIGHT, &CrossPointSettings::doubleClickPwrLight,
                                           "doubleClickPwrLight", StrId::STR_CAT_CONTROLS));
          break;
        }
      }
    }
    // Only show tilt page turn setting when the QMI8658 IMU is present (X3)
    if (halTiltSensor.isAvailable()) {
      // With the touch/gesture rows at the head of Controls, not stranded
      // after the power-button row at the end of the category.
      for (auto it = v.begin(); it != v.end(); ++it) {
        if (it->nameId == StrId::STR_SHOW_READER_MENU) {
          v.insert(it + 1, SettingInfo::Enum(StrId::STR_TILT_PAGE_TURN, &CrossPointSettings::tiltPageTurn,
                                             {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_INVERTED},
                                             "tiltPageTurn", StrId::STR_CAT_CONTROLS));
          break;
        }
      }
    }
    return v;
  }();

  std::vector<SettingInfo> v = baseList;
  if (!BoardConfig::hasTouch()) {
    // The reader menu style stays available on button boards (the toolbar
    // chrome is button-navigable); only the touch controls are hidden.
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) {
                             return s.nameId == StrId::STR_TOUCH_READER_CONTROLS ||
                                    s.nameId == StrId::STR_NEXT_PAGE_GESTURE ||
                                    s.nameId == StrId::STR_PREV_PAGE_GESTURE;
                           }),
            v.end());
  }
  // The reader-menu gesture choice only makes sense where the menu stays
  // reachable without the tap and the bottom edge is free (the capacitive
  // Home key); everywhere else the bottom-edge up-swipe is Home and the
  // center tap is the primary path, so the setting stays at its Tap default.
  if (!BoardConfig::hasHomeKey()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) { return s.nameId == StrId::STR_SHOW_READER_MENU; }),
            v.end());
  }
  // Every key whose action the user picks is listed in CONFIGURABLE_KEYS, so
  // that table decides which of these rows a build has any use for: the
  // expander key on a LilyGo T5S3, plus the four spare pads on a Pro Lite.
  v.erase(std::remove_if(v.begin(), v.end(),
                         [](const SettingInfo& s) {
                           if (!isConfigurableKeyRow(s.nameId)) return false;
                           for (const ConfigurableKey& key : CONFIGURABLE_KEYS) {
                             if (key.tapName == s.nameId || key.holdName == s.nameId) return false;
                           }
                           return true;
                         }),
          v.end());
  // Reader Menu Style picks between the list menu and the toolbar overlay --
  // but a theme that owns the reader chrome (Aurora) always draws the toolbar,
  // so on that theme the row is a switch with nothing behind it.
  if (GUI.ownsReaderChrome()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) { return s.nameId == StrId::STR_READER_MENU_STYLE; }),
            v.end());
  }
  // Side-button layout swaps which of the two side keys pages forward. A board
  // with no key that pages by itself has nothing for it to swap -- the T5 S3
  // has input pins, but every one of them is a configurable key, so what they
  // do is a binding rather than a layout.
  // "Long-press button behaviour" goes with it: it is what a held PAGE-TURN
  // BUTTON does (skip ten pages, or rotate), and a board with no page-turn
  // buttons has no press to hold. A held touch zone still reaches the same
  // code, but that is not what the row says it configures, and it defaults off.
  if (!hasPageTurnButtons()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) {
                             return s.nameId == StrId::STR_SIDE_BTN_LAYOUT ||
                                    s.nameId == StrId::STR_LONG_PRESS_BEHAVIOR;
                           }),
            v.end());
  }
  // "Long-press Menu" is the front Confirm button's hold action; the Home key's
  // hold has its own row. Boards with no Confirm button never show it.
  if (BoardConfig::ACTIVE.input.confirm < 0) {
    v.erase(
        std::remove_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_LONG_PRESS_MENU; }),
        v.end());
  }
  // Power Off is the charger's ship mode. Without such a charger the timeout
  // can only ever sleep, so there is nothing to choose between.
  if (BoardConfig::ACTIVE.batteryGauge.chargerAddr == 0) {
    v.erase(
        std::remove_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_TIMEOUT_ACTION; }),
        v.end());
  }
  if (BoardConfig::hasHomeKey()) {
    v.reserve(v.size() + 3);
    for (unsigned i = 0; i < 3; ++i) {
      v.push_back(SettingInfo::StaticEnum(home_button::GESTURE_LABELS[i], home_button::FIELDS[i],
                                          home_button::ACTION_LABELS, home_button::KEYS[i], StrId::STR_CAT_CONTROLS));
    }
  }
  if (BoardConfig::hasTouch()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) {
                             // Button-hint chrome never draws on touch boards
                             // (getMetrics() zeroes its row), so its toggle
                             // would be a no-op row.
                             return s.nameId == StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION ||
                                    s.nameId == StrId::STR_SUNLIGHT_FADING_FIX ||
                                    s.nameId == StrId::STR_BACK_SHORT_TO_FILE_BROWSER ||
                                    s.nameId == StrId::STR_SHOW_BUTTON_HINTS;
                           }),
            v.end());
  }
  if (registry && registry->getFamilyCount() > 0) {
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_FAMILY; });
    if (it != v.end()) {
      *it = buildFontFamilySetting(registry);
    }
  }
  {
    // Unconditional: even with no SD fonts installed the sizes come from the
    // built-in family rather than a fixed Small/Medium/Large/XL enum.
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_SIZE; });
    if (it != v.end()) {
      *it = buildFontSizeSetting(registry);
    }
  }
  if (dictionaries && !dictionaries->empty()) {
    // Insert at the end of the Reader category (just before the first Controls entry).
    auto it =
        std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.category == StrId::STR_CAT_CONTROLS; });
    v.insert(it, buildDictionarySetting(*dictionaries));
  }
  // Always offer the drop-cap font picker (device UI only — passes a registry).
  // With no fonts under /.dropcap it shows just "Default", which keeps the feature
  // discoverable and tells the user where the picker lives. Insert it right after
  // the Drop Caps toggle so the two sit together.
  if (dropCapRegistry) {
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_DROP_CAPS; });
    if (it != v.end()) {
      v.insert(it + 1, buildDropCapFontSetting(dropCapRegistry));
    }
  }
  return v;
}
