#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "DropCapFontSelectionActivity.h"
#include "FontDownloadActivity.h"
#include "FontSelectionActivity.h"
#include "KOReaderSettingsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "StatusBarSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/HomeTabBar.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// Map one Unicode codepoint to uppercase for the scripts the UI ships: ASCII,
// Latin-1, Latin Extended-A/-B, Vietnamese (Latin Extended Additional) and
// Cyrillic. Anything else passes through unchanged. std::toupper can't be used
// for this because it works one byte at a time and mangles multi-byte UTF-8
// (e.g. "Đọc sách" -> "ĐọC SáCH" with the accented letters left lowercase).
uint32_t toUpperCodepoint(uint32_t cp) {
  if (cp >= 'a' && cp <= 'z') return cp - 32;                          // ASCII
  if (cp >= 0x00E0 && cp <= 0x00FE && cp != 0x00F7) return cp - 0x20;  // à–þ (skip ÷)
  if (cp == 0x00FF) return 0x0178;                                     // ÿ → Ÿ
  if (cp >= 0x0100 && cp <= 0x017F) {                                  // Latin Extended-A
    if (cp == 0x0131) return 0x0049;                                   // ı → I
    if (cp == 0x017F) return 0x0053;                                   // ſ → S
    if ((cp >= 0x0100 && cp <= 0x0137) || (cp >= 0x014A && cp <= 0x0177))
      return (cp & 1) ? cp - 1 : cp;  // upper is even (e.g. ă 0x0103 → Ă 0x0102, đ 0x0111 → Đ 0x0110)
    if ((cp >= 0x0139 && cp <= 0x0148) || (cp >= 0x0179 && cp <= 0x017E))
      return (cp & 1) ? cp : cp - 1;  // upper is odd
    return cp;
  }
  if (cp == 0x01A1) return 0x01A0;                                  // ơ → Ơ
  if (cp == 0x01B0) return 0x01AF;                                  // ư → Ư
  if (cp >= 0x1EA0 && cp <= 0x1EFF) return (cp & 1) ? cp - 1 : cp;  // Vietnamese tone marks: upper even
  if (cp >= 0x0430 && cp <= 0x044F) return cp - 0x20;               // Cyrillic а–я
  if (cp >= 0x0450 && cp <= 0x045F) return cp - 0x50;               // Cyrillic ѐ–џ
  return cp;
}

// UTF-8-aware uppercase. Decodes each codepoint, uppercases it, re-encodes.
// Invalid byte sequences are copied through verbatim.
std::string utf8ToUpper(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  const size_t n = in.size();
  size_t i = 0;
  while (i < n) {
    const unsigned char b = static_cast<unsigned char>(in[i]);
    uint32_t cp = 0;
    size_t len = 0;
    if (b < 0x80) {
      cp = b;
      len = 1;
    } else if ((b >> 5) == 0x6) {
      cp = b & 0x1F;
      len = 2;
    } else if ((b >> 4) == 0xE) {
      cp = b & 0x0F;
      len = 3;
    } else if ((b >> 3) == 0x1E) {
      cp = b & 0x07;
      len = 4;
    } else {
      out.push_back(in[i++]);  // stray continuation/invalid lead byte
      continue;
    }
    if (i + len > n) {
      out.push_back(in[i++]);  // truncated sequence
      continue;
    }
    bool ok = true;
    for (size_t k = 1; k < len; ++k) {
      const unsigned char cb = static_cast<unsigned char>(in[i + k]);
      if ((cb >> 6) != 0x2) {
        ok = false;
        break;
      }
      cp = (cp << 6) | (cb & 0x3F);
    }
    if (!ok) {
      out.push_back(in[i++]);
      continue;
    }
    cp = toUpperCodepoint(cp);
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    i += len;
  }
  return out;
}

}  // namespace

const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,
                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};

void SettingsActivity::rebuildSettingsLists() {
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();

  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  for (auto& setting : getSettingsList(&sdFontSystem.registry(), &sdFontSystem.dropCapRegistry())) {
    if (setting.category == StrId::STR_NONE_OPT) continue;
    if (setting.category == StrId::STR_CAT_DISPLAY) {
      displaySettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_READER) {
      readerSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_CONTROLS) {
      if (setting.valuePtr == &CrossPointSettings::pwrBtnFootnoteBack &&
          SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) {
        continue;
      }
      controlsSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_SYSTEM) {
      systemSettings.push_back(setting);
    }
  }

  // Append device-only ACTION items
  controlsSettings.insert(controlsSettings.begin(),
                          SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
  // Insert "Manage Fonts" right after the font family setting so users discover it naturally
  readerSettings.insert(readerSettings.begin() + 1,
                        SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts));
  // The persistent reader status bar is only drawn by themes that don't own the
  // reader chrome (Aurora keeps the reading page clean and hides it), so its
  // customisation entry would do nothing there — only offer it when it applies.
  if (!GUI.ownsReaderChrome()) {
    readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));
  }

  // Update currentSettings pointer and count for the active category
  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &displaySettings;
      break;
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());

  // Aurora renders a flat sectioned list built from the same category data.
  if (GUI.ownsSettingsLayout()) buildAuroraEntries();
}

void SettingsActivity::onEnter() {
  Activity::onEnter();

  // Reset selection to first category
  selectedCategoryIndex = 0;
  selectedSettingIndex = 0;
  auroraSelected = 0;
  preserveQuickResumeTimeoutOn =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  quickResumeTimeoutAutoEnabled = false;
  syncQuickResumeTimeoutForSleepScreen(/*sleepScreenChanged=*/true, /*quickResumeTimeoutChanged=*/false);

  rebuildSettingsLists();

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::loop() {
  // Aurora layout: flat sectioned list. Up/Down move between rows; Select
  // toggles/cycles/opens the row; front Left/Right switch tabs (top-level page
  // only); Back saves and exits (to the Library tab, or to the Settings tab when
  // on the Advanced sub-page).
  if (GUI.ownsSettingsLayout()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      SETTINGS.saveToFile();
      if (advancedPage) {
        finish();
      } else {
        onGoHome();
      }
      return;
    }
    if (!advancedPage && (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                          mappedInput.wasReleased(MappedInputManager::Button::Right))) {
      SETTINGS.saveToFile();
      HomeTabBar::handleLeftRight(mappedInput, HomeTabBar::Settings);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      const SettingInfo* setting = auroraSelectedSetting();
      if (setting != nullptr) {
        activateSetting(*setting);
        requestUpdate();
      }
      return;
    }
    if (auroraSelectableCount > 0) {
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] {
        auroraSelected = ButtonNavigator::previousIndex(auroraSelected, auroraSelectableCount);
        requestUpdate();
      });
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] {
        auroraSelected = ButtonNavigator::nextIndex(auroraSelected, auroraSelectableCount);
        requestUpdate();
      });
    }
    return;
  }

  bool hasChangedCategory = false;

  // Handle actions with early return
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedSettingIndex == 0) {
      selectedCategoryIndex = (selectedCategoryIndex < categoryCount - 1) ? (selectedCategoryIndex + 1) : 0;
      hasChangedCategory = true;
      requestUpdate();
    } else {
      toggleCurrentSetting();
      requestUpdate();
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (selectedSettingIndex > 0) {
      selectedSettingIndex = 0;
      requestUpdate();
    } else {
      SETTINGS.saveToFile();
      onGoHome();
    }
    return;
  }

  // Handle navigation
  buttonNavigator.onNextRelease([this] {
    selectedSettingIndex = ButtonNavigator::nextIndex(selectedSettingIndex, settingsCount + 1);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedSettingIndex = ButtonNavigator::previousIndex(selectedSettingIndex, settingsCount + 1);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::nextIndex(selectedCategoryIndex, categoryCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::previousIndex(selectedCategoryIndex, categoryCount);
    requestUpdate();
  });

  if (hasChangedCategory) {
    selectedSettingIndex = (selectedSettingIndex == 0) ? 0 : 1;
    switch (selectedCategoryIndex) {
      case 0:
        currentSettings = &displaySettings;
        break;
      case 1:
        currentSettings = &readerSettings;
        break;
      case 2:
        currentSettings = &controlsSettings;
        break;
      case 3:
        currentSettings = &systemSettings;
        break;
    }
    settingsCount = static_cast<int>(currentSettings->size());
  }
}

void SettingsActivity::toggleCurrentSetting() {
  const int selectedSetting = selectedSettingIndex - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) return;
  activateSetting((*currentSettings)[selectedSetting]);
}

void SettingsActivity::activateSetting(const SettingInfo& setting) {
  const bool sleepScreenChanged = setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    if (setting.nameId == StrId::STR_FONT_FAMILY) {
      // Launch font selection submenu instead of cycling
      startActivityForResult(std::make_unique<FontSelectionActivity>(renderer, mappedInput, &sdFontSystem.registry()),
                             [this](const ActivityResult&) {
                               SETTINGS.saveToFile();
                               rebuildSettingsLists();
                             });
      return;
    }
    if (setting.nameId == StrId::STR_DROP_CAP_FONT) {
      // Launch the drop-cap font picker (preview + list) instead of cycling.
      startActivityForResult(
          std::make_unique<DropCapFontSelectionActivity>(renderer, mappedInput, &sdFontSystem.dropCapRegistry()),
          [this](const ActivityResult&) {
            SETTINGS.saveToFile();
            rebuildSettingsLists();
          });
      return;
    }
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumValues.size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    const uint8_t cur = setting.valueGetter();
    setting.valueSetter((cur + 1) % totalValues);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Network:
        startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
        break;
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DownloadFonts:
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::Language:
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OpenAdvanced:
        startActivityForResult(std::make_unique<SettingsActivity>(renderer, mappedInput, /*advanced=*/true),
                               [](const ActivityResult&) {});
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
  SETTINGS.saveToFile();
  rebuildSettingsLists();
  selectedSettingIndex = std::min(selectedSettingIndex, settingsCount);
}

std::string SettingsActivity::settingValueText(const SettingInfo& setting) const {
  std::string valueText;
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    valueText = (SETTINGS.*(setting.valuePtr)) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t value = SETTINGS.*(setting.valuePtr);
    if (value < setting.enumValues.size()) {
      valueText = I18N.get(setting.enumValues[value]);
    }
  } else if (setting.type == SettingType::ENUM && setting.valueGetter) {
    const uint8_t value = setting.valueGetter();
    if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
      valueText = setting.enumStringValues[value];
    } else if (value < setting.enumValues.size()) {
      valueText = I18N.get(setting.enumValues[value]);
    }
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
      if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) {
        valueText = tr(STR_SLEEP_NEVER);
      } else {
        char valueBuffer[32];
        snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
                 static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
        valueText = valueBuffer;
      }
    } else {
      valueText = std::to_string(SETTINGS.*(setting.valuePtr));
    }
  }
  return valueText;
}

void SettingsActivity::adjustCurrentSetting(int direction) {
  const int idx = selectedSettingIndex - 1;
  if (idx < 0 || idx >= settingsCount) {
    return;
  }
  const auto& setting = (*currentSettings)[idx];
  const bool sleepScreenChanged = setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    return;  // edited via the Confirm picker
  }
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    SETTINGS.*(setting.valuePtr) = !(SETTINGS.*(setting.valuePtr));
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const int n = static_cast<int>(setting.enumValues.size());
    if (n <= 0) return;
    const int cur = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = static_cast<uint8_t>((cur + direction + n) % n);
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    if (setting.nameId == StrId::STR_FONT_FAMILY) {
      return;  // chosen via the Confirm submenu
    }
    const int n = setting.enumStringValues.empty() ? static_cast<int>(setting.enumValues.size())
                                                   : static_cast<int>(setting.enumStringValues.size());
    if (n <= 0) return;
    const int cur = setting.valueGetter();
    setting.valueSetter(static_cast<uint8_t>((cur + direction + n) % n));
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int step = setting.valueRange.step == 0 ? 1 : setting.valueRange.step;
    int v = static_cast<int>(SETTINGS.*(setting.valuePtr)) + direction * step;
    if (v > setting.valueRange.max) v = setting.valueRange.min;
    if (v < setting.valueRange.min) v = setting.valueRange.max;
    SETTINGS.*(setting.valuePtr) = static_cast<uint8_t>(v);
  } else {
    return;  // ACTION rows are activated with Confirm, not adjusted
  }

  syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
  SETTINGS.saveToFile();
}

void SettingsActivity::changeCategory(int direction) {
  selectedCategoryIndex = (direction > 0) ? ButtonNavigator::nextIndex(selectedCategoryIndex, categoryCount)
                                          : ButtonNavigator::previousIndex(selectedCategoryIndex, categoryCount);
  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &displaySettings;
      break;
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());
}

void SettingsActivity::syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged) {
  if (quickResumeTimeoutChanged) {
    preserveQuickResumeTimeoutOn =
        SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
    quickResumeTimeoutAutoEnabled = false;
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
    if (SETTINGS.quickResumeSleepScreen != CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT) {
      SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
      quickResumeTimeoutAutoEnabled = !preserveQuickResumeTimeoutOn;
    } else if (sleepScreenChanged && !preserveQuickResumeTimeoutOn) {
      quickResumeTimeoutAutoEnabled = true;
    }
    return;
  }

  if (sleepScreenChanged && quickResumeTimeoutAutoEnabled && !preserveQuickResumeTimeoutOn) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
    quickResumeTimeoutAutoEnabled = false;
  }
}

void SettingsActivity::openSleepTimeoutPicker() {
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, StrId::STR_SLEEP_TIMER_STEP_HINT,
          SETTINGS.sleepTimeoutMinutes, CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES,
          CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5, StrId::STR_SLEEP_TIMER_VALUE_FORMAT, false, true,
          StrId::STR_SLEEP_NEVER),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

bool SettingsActivity::isTopLevelSetting(StrId nameId) {
  switch (nameId) {
    case StrId::STR_FONT_FAMILY:
    case StrId::STR_MANAGE_FONTS:
    case StrId::STR_DROP_CAPS:
    case StrId::STR_DROP_CAP_FONT:
    case StrId::STR_FONT_SIZE:
    case StrId::STR_LINE_SPACING:
    case StrId::STR_SCREEN_MARGIN:
    case StrId::STR_PARA_ALIGNMENT:
    case StrId::STR_UI_THEME:
    case StrId::STR_SLEEP_SCREEN:
    case StrId::STR_REFRESH_FREQ:
    case StrId::STR_SHOW_BUTTON_HINTS:
    case StrId::STR_WIFI_NETWORKS:
    case StrId::STR_TIME_TO_SLEEP:
    case StrId::STR_LANGUAGE:
    case StrId::STR_CHECK_UPDATES:
      return true;
    default:
      return false;
  }
}

void SettingsActivity::buildAuroraEntries() {
  auroraEntries.clear();

  const std::vector<SettingInfo>* cats[] = {&displaySettings, &readerSettings, &controlsSettings, &systemSettings};
  auto findByName = [&](StrId id) -> const SettingInfo* {
    for (const auto* vec : cats)
      for (const auto& s : *vec)
        if (s.nameId == id) return &s;
    return nullptr;
  };
  auto addSection = [&](StrId header, std::initializer_list<StrId> ids) {
    bool headerAdded = false;
    for (StrId id : ids) {
      const SettingInfo* s = findByName(id);
      if (s == nullptr) continue;
      if (!headerAdded) {
        auroraEntries.push_back(AuroraEntry{true, header, {}});
        headerAdded = true;
      }
      auroraEntries.push_back(AuroraEntry{false, StrId::STR_NONE_OPT, *s});
    }
  };

  if (!advancedPage) {
    // Curated, important settings grouped into Reading / Display / Device
    // (section names match the reference design).
    addSection(StrId::STR_SEC_READING,
               {StrId::STR_FONT_FAMILY, StrId::STR_MANAGE_FONTS, StrId::STR_DROP_CAPS, StrId::STR_DROP_CAP_FONT,
                StrId::STR_FONT_SIZE, StrId::STR_LINE_SPACING, StrId::STR_SCREEN_MARGIN, StrId::STR_PARA_ALIGNMENT});
    addSection(StrId::STR_CAT_DISPLAY,
               {StrId::STR_UI_THEME, StrId::STR_SLEEP_SCREEN, StrId::STR_REFRESH_FREQ, StrId::STR_SHOW_BUTTON_HINTS});
    addSection(StrId::STR_CAT_DEVICE,
               {StrId::STR_WIFI_NETWORKS, StrId::STR_TIME_TO_SLEEP, StrId::STR_LANGUAGE, StrId::STR_CHECK_UPDATES});
    // Spacer header (no label) so the entry below sits in its own card.
    auroraEntries.push_back(AuroraEntry{true, StrId::STR_NONE_OPT, {}});
    // Everything else lives behind this entry.
    auroraEntries.push_back(AuroraEntry{
        false, StrId::STR_NONE_OPT, SettingInfo::Action(StrId::STR_ADVANCED_SETTINGS, SettingAction::OpenAdvanced)});
  } else {
    // Advanced page: all non-top-level settings, grouped by their category.
    auto addRemaining = [&](StrId header, const std::vector<SettingInfo>& vec) {
      bool headerAdded = false;
      for (const auto& s : vec) {
        if (isTopLevelSetting(s.nameId)) continue;
        if (!headerAdded) {
          auroraEntries.push_back(AuroraEntry{true, header, {}});
          headerAdded = true;
        }
        auroraEntries.push_back(AuroraEntry{false, StrId::STR_NONE_OPT, s});
      }
    };
    addRemaining(StrId::STR_SEC_READING, readerSettings);
    addRemaining(StrId::STR_CAT_DISPLAY, displaySettings);
    addRemaining(StrId::STR_CAT_CONTROLS, controlsSettings);
    addRemaining(StrId::STR_CAT_SYSTEM, systemSettings);
  }

  auroraSelectableCount = 0;
  for (const auto& e : auroraEntries)
    if (!e.isHeader) ++auroraSelectableCount;
  if (auroraSelected >= auroraSelectableCount) auroraSelected = std::max(0, auroraSelectableCount - 1);
}

const SettingInfo* SettingsActivity::auroraSelectedSetting() const {
  int row = 0;
  for (const auto& e : auroraEntries) {
    if (e.isHeader) continue;
    if (row == auroraSelected) return &e.setting;
    ++row;
  }
  return nullptr;
}

void SettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto& metrics = UITheme::getInstance().getMetrics();

  // Aurora owns the whole settings layout: a flat, section-grouped value list
  // plus (on the top-level page) the persistent bottom tab bar.
  if (GUI.ownsSettingsLayout()) {
    std::vector<SettingsListItem> items;
    items.reserve(auroraEntries.size());
    int row = 0;
    for (const auto& entry : auroraEntries) {
      if (entry.isHeader) {
        // STR_NONE_OPT marks a spacer (blank label). Otherwise uppercase the
        // section label, matching the reference design. UTF-8-aware so accented
        // scripts (Vietnamese, Cyrillic, …) uppercase correctly, not byte-wise.
        std::string header = entry.header == StrId::STR_NONE_OPT ? std::string() : std::string(I18N.get(entry.header));
        header = utf8ToUpper(header);
        items.push_back(SettingsListItem{true, std::move(header), "", false, false});
        continue;
      }
      SettingsListItem item;
      item.text = I18N.get(entry.setting.nameId);
      item.value = settingValueText(entry.setting);
      item.selected = row == auroraSelected;
      item.showChevron = true;
      items.push_back(std::move(item));
      ++row;
    }

    const int hintRowHeight = SETTINGS.showFrontButtonHints() ? metrics.buttonHintsHeight : 0;
    const int barH = advancedPage ? 0 : GUI.bottomBarHeight();
    const char* title = advancedPage ? tr(STR_ADVANCED_SETTINGS) : tr(STR_SETTINGS_TITLE);
    GUI.drawSettingsScreen(renderer, Rect{0, 0, pageWidth, pageHeight - hintRowHeight - barH}, title, items);

    // Front hints: Back, Select; Left/Right switch tabs on the top-level page.
    // The top-level settings tab's "Back" returns to the home screen, so label it
    // STR_HOME (matching the file browser's root); the advanced sub-page genuinely
    // goes back, so keep STR_BACK there.
    const char* leftHint = advancedPage ? "" : tr(STR_DIR_LEFT);
    const char* rightHint = advancedPage ? "" : tr(STR_DIR_RIGHT);
    const char* backLabel = advancedPage ? tr(STR_BACK) : tr(STR_HOME);
    const auto labels = mappedInput.mapLabels(backLabel, tr(STR_SELECT), leftHint, rightHint);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    // Side hints (2): Up/Down move between rows. Self-guarded (Front + Edge mode only).
    GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));

    if (!advancedPage) HomeTabBar::draw(renderer, pageWidth, pageHeight, HomeTabBar::Settings);

    renderer.displayBuffer();
    return;
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SETTINGS_TITLE),
                 CROSSPOINT_VERSION);

  std::vector<TabInfo> tabs;
  tabs.reserve(categoryCount);
  for (int i = 0; i < categoryCount; i++) {
    tabs.push_back({I18N.get(categoryNames[i]), selectedCategoryIndex == i});
  }
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 selectedSettingIndex == 0);

  const auto& settings = *currentSettings;
  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.buttonHintsHeight +
                         metrics.verticalSpacing * 2)},
      settingsCount, selectedSettingIndex - 1,
      [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); }, nullptr, nullptr,
      [this, &settings](int i) { return settingValueText(settings[i]); }, true);

  // Draw help text
  const auto confirmLabel =
      (selectedSettingIndex == 0)
          ? I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount])
          : (selectedSettingIndex > 0 && (*currentSettings)[selectedSettingIndex - 1].nameId == StrId::STR_TIME_TO_SLEEP
                 ? tr(STR_SELECT)
                 : tr(STR_TOGGLE));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
