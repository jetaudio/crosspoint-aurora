#include "SettingsActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "BatteryMonitorActivity.h"
#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "ConfigurableKeys.h"
#include "ControlCenterSettingsActivity.h"
#include "CrossPointSettings.h"
#include "DropCapFontSelectionActivity.h"
#include "FontDownloadActivity.h"
#include "KOReaderSettingsActivity.h"
#include "KeyActionsSettingsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "StatusBarSettingsActivity.h"
#include "TextSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/HomeTabBar.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/ScreenOrientation.h"

namespace fui = freeink::ui;

const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,
                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};

SettingsActivity::SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool advanced)
    : UiTabListActivity("Settings", renderer, mappedInput), advancedPage(advanced) {}

void SettingsActivity::rebuildSettingsLists() {
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();

  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  // Rescan /dictionaries on every rebuild: cheap (one directory listing) and
  // picks up dictionaries copied to the SD card since the last visit.
  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);

  for (auto& setting : getSettingsList(&sdFontSystem.registry(), &dictionaries, &sdFontSystem.dropCapRegistry())) {
    if (setting.category == StrId::STR_NONE_OPT) continue;
    if (setting.category == StrId::STR_CAT_DISPLAY) {
      // The sunlight fading fix is a grayscale-waveform compensation that does
      // not apply on the X4 Pro (plain OTP waveform, no custom grayscale LUT).
      if (setting.valuePtr == &CrossPointSettings::fadingFix && BoardConfig::isX4Pro()) {
        continue;
      }
      displaySettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_READER) {
      // Settings merged into "Text Settings"
      // (they stay in the shared list for the web settings API)
      if (setting.inTextSettings) continue;
      readerSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_CONTROLS) {
      if (setting.valuePtr == &CrossPointSettings::pwrBtnFootnoteBack &&
          SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) {
        continue;
      }
      // The per-key tap/hold pickers live on their own screen; they stay in
      // the shared list for the web API, and one Action row below opens them.
      if (KeyActionsSettingsActivity::owns(setting.nameId)) continue;
      controlsSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_SYSTEM) {
      systemSettings.push_back(setting);
    }
  }

  controlsSettings.push_back(SettingInfo::Action(StrId::STR_SEC_KEY_ACTIONS, SettingAction::KeyActions));

  // Append device-only ACTION items
  if (!BoardConfig::hasTouch()) {
    controlsSettings.insert(controlsSettings.begin(),
                            SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  }
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_BATTERY_MONITOR, SettingAction::BatteryMonitor));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
  // OTA fetches this board's own release asset (see OtaUpdater); boards whose
  // asset isn't published yet just report no update available.
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
  readerSettings.insert(readerSettings.begin(),
                        SettingInfo::Action(StrId::STR_TEXT_SETTINGS, SettingAction::TextSettings));
  readerSettings.insert(readerSettings.begin() + 1,
                        SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));
  // The control center is display chrome, and it is reachable on every board
  // (top-edge swipe, status-bar tap, or a key bound to it), so the row is not
  // gated on touch.
  displaySettings.push_back(
      SettingInfo::Action(StrId::STR_CUSTOMISE_CONTROL_CENTER, SettingAction::CustomiseControlCenter));

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
  rebuildRowItems();

  // Aurora renders a flat sectioned list built from the same category data.
  if (GUI.ownsSettingsLayout()) buildAuroraEntries();
}

void SettingsActivity::onEnter() {
  UiTabListActivity::onEnter();

  // Reset selection to first category (ring position 0, the tab bar, comes
  // from the base's per-tab nav reset)
  selectedCategoryIndex = 0;
  preserveQuickResumeTimeoutOn =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  quickResumeTimeoutAutoEnabled = false;
  syncQuickResumeTimeoutForSleepScreen(/*sleepScreenChanged=*/true, /*quickResumeTimeoutChanged=*/false);

  rebuildSettingsLists();
}

void SettingsActivity::selectCategory(const int categoryIndex) {
  selectedCategoryIndex = categoryIndex;
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
  activeNav().top = 0;  // category switches start the list at the top (no per-tab memory here)
  rebuildRowItems();
}

// Rebuilds rowValues_/rowItems_ (label + actionValue) for *currentSettings.
// Structural — call only when the active category or a category's setting
// list changes, never from buildScreen(), which only refreshes rowValues_
// content and rowItems_[].value pointers in place.
// Which section a Controls row belongs to. Anything else (and every other
// category) returns STR_NONE_OPT and stays header-less: the split earns its
// keep on the one category long enough to need it.
static StrId settingSection(const SettingInfo& s) {
  if (s.category != StrId::STR_CAT_CONTROLS) return StrId::STR_NONE_OPT;
  switch (s.nameId) {
    case StrId::STR_TOUCH_READER_CONTROLS:
    case StrId::STR_SHOW_READER_MENU:
    case StrId::STR_TILT_PAGE_TURN:
      return StrId::STR_SEC_TOUCH;
    case StrId::STR_SIDE_BTN_LAYOUT:
    case StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION:
    case StrId::STR_SHOW_BUTTON_HINTS:
    case StrId::STR_LONG_PRESS_BEHAVIOR:
    case StrId::STR_LONG_PRESS_MENU:
    case StrId::STR_BACK_SHORT_TO_FILE_BROWSER:
    case StrId::STR_PWR_BTN_FOOTNOTE_BACK:
      return StrId::STR_SEC_BUTTONS;
    default:
      // Whatever is left (including the entry that opens the key-actions
      // screen) sits with the button rows.
      return StrId::STR_SEC_BUTTONS;
  }
}

void SettingsActivity::rebuildRowItems() {
  const auto& settings = *currentSettings;
  rowValues_.assign(settings.size(), std::string());
  rowItems_.clear();
  rowSetting_.clear();
  rowItems_.reserve(settings.size() + 3);
  rowSetting_.reserve(settings.size() + 3);
  StrId section = StrId::STR_NONE_OPT;
  for (size_t i = 0; i < settings.size(); i++) {
    const StrId want = settingSection(settings[i]);
    if (want != StrId::STR_NONE_OPT && want != section) {
      section = want;
      fui::ListItem header;
      header.isHeader = true;
      header.label = I18N.get(want);
      rowItems_.push_back(header);
      rowSetting_.push_back(-1);
    }
    fui::ListItem item;
    item.label = I18N.get(settings[i].nameId);
    item.actionValue = static_cast<int16_t>(rowItems_.size());
    rowItems_.push_back(item);
    rowSetting_.push_back(static_cast<int16_t>(i));
  }
}

// ring 0 is the tab band; rows start at 1.
const SettingInfo* SettingsActivity::settingAtRing(const int ring) const {
  const int row = ring - 1;
  if (row < 0 || row >= static_cast<int>(rowSetting_.size())) return nullptr;
  const int16_t index = rowSetting_[row];
  if (index < 0 || index >= static_cast<int>(currentSettings->size())) return nullptr;
  return &(*currentSettings)[index];
}

// Headers are rows the list draws but never dispatches, so button navigation
// has to hop over them; a tap cannot land on one in the first place.
void SettingsActivity::navigateButtons() {
  const int ringSize = listCount() + 1;
  const auto step = [this, ringSize](int direction) {
    int ring = ringPos();
    for (int guard = 0; guard < ringSize; ++guard) {
      ring =
          direction > 0 ? ButtonNavigator::nextIndex(ring, ringSize) : ButtonNavigator::previousIndex(ring, ringSize);
      if (ring == 0 || settingAtRing(ring) != nullptr) break;  // 0 = the tab band
    }
    moveRingTo(ring);
  };
  buttonNavigator.onNextRelease([step] { step(1); });
  buttonNavigator.onPreviousRelease([step] { step(-1); });
  buttonNavigator.onNextContinuous([this] { stepTab(1); });
  buttonNavigator.onPreviousContinuous([this] { stepTab(-1); });
}

void SettingsActivity::onTabAction(const int index) {
  if (optionPopup.isActive()) return;
  selectCategory(index);
  activeNav().selected = 0;  // tab taps land with the tab bar focused
  // The switched-to tab repaints as the selected pill; a flash overlay on top
  // of it just repaints the pill in the focused style.
  app.clearTapFlash();
}

void SettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  (void)index;  // toggleCurrentSetting reads the ring position
  // Most rows repaint a different surface (popup, sub-activity, new value);
  // a lingering tap flash would gray an unrelated element.
  app.clearTapFlash();
  toggleCurrentSetting();
  // Tap-first: a tapped row is not a cursor position. Leaving it focused
  // (inverted) after the tap meant the row stayed black once its sub-screen or
  // popup closed, and Back then had to clear that focus before a second Back
  // left Settings. Hand the focus back to the tab band; the viewport stays put.
  if (mappedInput.hasTouch()) {
    activeNav().selected = 0;
  }
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr) {
  if (valuePtr == &CrossPointSettings::orientation) {
    // Turn the screen under the user's finger: this row is the screen's
    // orientation, so the settings list itself is drawn the new way up. The
    // whole frame changes shape, so it needs re-driving rather than a fast
    // partial update, and the interaction table has to be rebuilt against the
    // new layout before a tap can be routed again.
    applyScreenOrientation(renderer);
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    resetUi();
    return;
  }
  // Theme changes take effect immediately, on this screen — reload the theme
  // and re-derive the app's tokens so the very next repaint is in the new look.
  if (valuePtr != &CrossPointSettings::uiTheme) {
    return;
  }
  UITheme::getInstance().reload();
  // Re-derive the shared tokens for the new look; the gate stays closed until
  // the repaint that rebuilds the interaction table in the new layout.
  resetUi();
}

bool SettingsActivity::handleCustomInput() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (GUI.ownsSettingsLayout()) {
    handleAuroraInput();
    return true;  // the flat layout owns all input; skip the tab-list base
  }
  // Aurora home layout: the dock below the stock settings switches home tabs
  // by tap (category switching keeps the stock paths: tap a category tab, or
  // Confirm on the tab ring).
  if (GUI.ownsHomeLayout() && HomeTabBar::handleTap(mappedInput, renderer, HomeTabBar::Settings)) {
    SETTINGS.saveToFile();
    return true;
  }
  return false;
}

Rect SettingsActivity::auroraContentRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int hintRowHeight = SETTINGS.showFrontButtonHints() ? metrics.buttonHintsHeight : 0;
  const int barH = advancedPage ? 0 : GUI.bottomBarHeight();
  return Rect{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight() - hintRowHeight - barH};
}

bool SettingsActivity::handleAuroraInput() {
  // Back saves and exits (top level -> Home; the Advanced sub-page pops).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    if (advancedPage) {
      finish();
    } else {
      onGoHome();
    }
    return true;
  }
  // Front Left/Right walk the bottom tab bar (top-level page only).
  if (!advancedPage && (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                        mappedInput.wasReleased(MappedInputManager::Button::Right))) {
    SETTINGS.saveToFile();
    HomeTabBar::handleLeftRight(mappedInput, HomeTabBar::Settings);
    return true;
  }
  if (!advancedPage && HomeTabBar::handleTap(mappedInput, renderer, HomeTabBar::Settings)) {
    SETTINGS.saveToFile();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const SettingInfo* setting = auroraSelectedSetting();
    if (setting != nullptr) {
      activateSetting(*setting);
      requestUpdate();
    }
    return true;
  }

  // Touch: tap a row to activate it directly; swipe up/down to page.
  {
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
      // Right-edge strip pages the list by taps (swipes are unreliable on the
      // T5S3's etched glass; they still work as a secondary path below).
      if (tx >= renderer.getScreenWidth() - 44 && auroraSelectableCount > 0) {
        const int step = ty >= renderer.getScreenHeight() / 2 ? 6 : -6;
        const int next = std::clamp(auroraSelected + step, 0, auroraSelectableCount - 1);
        if (next != auroraSelected) {
          auroraSelected = next;
          requestUpdate();
        }
        return true;
      }
      const auto items = buildSettingsItems();
      const int itemIdx = GUI.settingsItemAt(renderer, auroraContentRect(), items, tx, ty);
      if (itemIdx >= 0) {
        int row = -1;
        for (int i = 0; i <= itemIdx; ++i) {
          if (!items[i].isHeader) ++row;
        }
        if (row >= 0 && row < auroraSelectableCount) {
          auroraSelected = row;
          const SettingInfo* setting = auroraSelectedSetting();
          if (setting != nullptr) {
            activateSetting(*setting);
            requestUpdate();
          }
        }
      }
      return true;
    }
    const auto swipe = mappedInput.wasSwipe();
    if (auroraSelectableCount > 0 &&
        (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down)) {
      const int step = swipe == MappedInputManager::SwipeDir::Up ? 6 : -6;
      const int next = std::clamp(auroraSelected + step, 0, auroraSelectableCount - 1);
      if (next != auroraSelected) {
        auroraSelected = next;
        requestUpdate();
      }
      return true;
    }
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
  return true;
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
    case StrId::STR_HOME_KEY_TAP:
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
    // Curated, important settings grouped into Reading / Display / Device.
    // Develop folds the font family/size/spacing rows into the Text Settings
    // screen (inTextSettings), so the curated section leads with that entry.
    addSection(StrId::STR_SEC_READING, {StrId::STR_TEXT_SETTINGS, StrId::STR_FONT_FAMILY, StrId::STR_MANAGE_FONTS,
                                        StrId::STR_DROP_CAPS, StrId::STR_DROP_CAP_FONT, StrId::STR_FONT_SIZE,
                                        StrId::STR_LINE_SPACING, StrId::STR_SCREEN_MARGIN, StrId::STR_PARA_ALIGNMENT});
    addSection(StrId::STR_CAT_DISPLAY, {StrId::STR_UI_THEME, StrId::STR_SLEEP_SCREEN, StrId::STR_REFRESH_FREQ,
                                        StrId::STR_SHOW_BUTTON_HINTS, StrId::STR_HOME_KEY_TAP});
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
    // Controls is by far the longest category -- touch behaviour, button
    // layout and a dozen per-key action pickers in one undivided run. Split it
    // the way the hardware does, and let anything unlisted fall through to a
    // trailing group so a new setting still shows up without being placed here.
    auto addGroupFrom = [&](const std::vector<SettingInfo>& vec, StrId header, const std::vector<StrId>& ids,
                            std::vector<StrId>& placed) {
      bool headerAdded = false;
      for (StrId id : ids) {
        const auto it = std::find_if(vec.begin(), vec.end(), [id](const SettingInfo& s) { return s.nameId == id; });
        if (it == vec.end() || isTopLevelSetting(it->nameId)) continue;
        if (!headerAdded) {
          auroraEntries.push_back(AuroraEntry{true, header, {}});
          headerAdded = true;
        }
        auroraEntries.push_back(AuroraEntry{false, StrId::STR_NONE_OPT, *it});
        placed.push_back(id);
      }
    };

    addRemaining(StrId::STR_SEC_READING, readerSettings);
    addRemaining(StrId::STR_CAT_DISPLAY, displaySettings);

    std::vector<StrId> placedControls;
    addGroupFrom(controlsSettings, StrId::STR_SEC_TOUCH,
                 {StrId::STR_TOUCH_READER_CONTROLS, StrId::STR_SHOW_READER_MENU, StrId::STR_TILT_PAGE_TURN},
                 placedControls);
    addGroupFrom(controlsSettings, StrId::STR_SEC_BUTTONS,
                 {StrId::STR_SIDE_BTN_LAYOUT, StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, StrId::STR_SHOW_BUTTON_HINTS,
                  StrId::STR_LONG_PRESS_BEHAVIOR, StrId::STR_LONG_PRESS_MENU, StrId::STR_BACK_SHORT_TO_FILE_BROWSER,
                  StrId::STR_PWR_BTN_FOOTNOTE_BACK},
                 placedControls);
    {
      std::vector<StrId> keyRows{StrId::STR_HOME_KEY_TAP, StrId::STR_HOME_KEY_HOLD};
      for (const ConfigurableKey& key : CONFIGURABLE_KEYS) {
        keyRows.push_back(key.tapName);
        keyRows.push_back(key.holdName);
      }
      keyRows.push_back(StrId::STR_SHORT_PWR_BTN);
      keyRows.push_back(StrId::STR_PWR_BTN_HOLD);
      addGroupFrom(controlsSettings, StrId::STR_SEC_KEY_ACTIONS, keyRows, placedControls);
    }
    {
      std::vector<SettingInfo> leftover;
      for (const auto& s : controlsSettings) {
        if (std::find(placedControls.begin(), placedControls.end(), s.nameId) == placedControls.end()) {
          leftover.push_back(s);
        }
      }
      addRemaining(StrId::STR_CAT_CONTROLS, leftover);
    }

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

std::vector<SettingsListItem> SettingsActivity::buildSettingsItems() const {
  std::vector<SettingsListItem> items;
  items.reserve(auroraEntries.size());
  int row = 0;
  for (const auto& entry : auroraEntries) {
    if (entry.isHeader) {
      // STR_NONE_OPT marks a spacer (blank label). Otherwise uppercase the
      // section label. UTF-8-aware so accented scripts uppercase correctly.
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
  return items;
}

void SettingsActivity::renderAurora() {
  renderer.clearScreen();
  const auto items = buildSettingsItems();
  const char* title = advancedPage ? tr(STR_ADVANCED_SETTINGS) : tr(STR_SETTINGS_TITLE);
  GUI.drawSettingsScreen(renderer, auroraContentRect(), title, items);

  // Front hints (self-hidden on touch boards): Back/Home, Select, tab arrows.
  const char* leftHint = advancedPage ? "" : tr(STR_DIR_LEFT);
  const char* rightHint = advancedPage ? "" : tr(STR_DIR_RIGHT);
  const char* backLabel = advancedPage ? tr(STR_BACK) : tr(STR_HOME);
  const auto labels = mappedInput.mapLabels(backLabel, tr(STR_SELECT), leftHint, rightHint);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  if (!advancedPage) {
    HomeTabBar::draw(renderer, renderer.getScreenWidth(), renderer.getScreenHeight(), HomeTabBar::Settings);
  }

  renderer.displayBuffer();
}

void SettingsActivity::stepTab(const int direction) {
  // Ring position 0 stays on the tab bar; a row selection collapses to the
  // new category's first row (per-tab memory is deliberately not kept here).
  const bool onTabBar = ringPos() == 0;
  selectedCategoryIndex = direction > 0 ? ButtonNavigator::nextIndex(selectedCategoryIndex, categoryCount)
                                        : ButtonNavigator::previousIndex(selectedCategoryIndex, categoryCount);
  selectCategory(selectedCategoryIndex);
  activeNav().selected = onTabBar ? 0 : 1;
  requestUpdate();
}

bool SettingsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      stepTab(1);
    } else {
      toggleCurrentSetting();
      requestUpdate();
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (ringPos() > 0) {
      activeNav().selected = 0;
      requestUpdate();
    } else {
      SETTINGS.saveToFile();
      onGoHome();
    }
    return true;
  }

  return false;
}

void SettingsActivity::toggleCurrentSetting() {
  const SettingInfo* setting = settingAtRing(ringPos());
  if (setting == nullptr) return;  // the tab band, or a section header
  activateSetting(*setting);
  activeNav().selected = std::min(ringPos(), listCount());
}

// Toggle/cycle/open one settings row. Shared by the category-tab presentation
// (via toggleCurrentSetting) and the Aurora flat list.
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
    if (setting.enumValues.size() > 2) {
      const auto valuePtr = setting.valuePtr;
      optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()),
                       currentValue, [this, valuePtr, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
                         SETTINGS.*valuePtr = idx;
                         syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
                         SETTINGS.saveToFile();
                         rebuildSettingsLists();
                         applyUiSettingChange(valuePtr);
                       });
      requestUpdate();
      return;
    }
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
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
    if (totalValues > 2) {
      const auto valueSetter = setting.valueSetter;
      auto onSelect = [this, valueSetter, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
        valueSetter(idx);
        syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
        SETTINGS.saveToFile();
        rebuildSettingsLists();
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, cur, std::move(onSelect));
      } else {
        optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()), cur,
                         std::move(onSelect));
      }
      requestUpdate();
      return;
    }
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
      case SettingAction::KeyActions:
        startActivityForResult(std::make_unique<KeyActionsSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseControlCenter:
        startActivityForResult(std::make_unique<ControlCenterSettingsActivity>(renderer, mappedInput), resultHandler);
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
      case SettingAction::BatteryMonitor:
        startActivityForResult(std::make_unique<BatteryMonitorActivity>(renderer, mappedInput), nullptr);
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
      case SettingAction::TextSettings:
        startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                      TextSettingsActivity::Tab::Family),
                               [this](const ActivityResult&) {
                                 // TextSettingsActivity saves on each change; no save needed here.
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::Language:
        // Row labels are translated once in rebuildRowItems() and don't
        // re-run on Pop (see ActivityManager::loop()), so a language switch
        // needs an explicit rebuild here rather than the generic resultHandler.
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::OpenAdvanced:
        startActivityForResult(std::make_unique<SettingsActivity>(renderer, mappedInput, /*advanced=*/true),
                               [this](const ActivityResult&) { rebuildSettingsLists(); });
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
  applyUiSettingChange(setting.valuePtr);
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
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, SETTINGS.sleepTimeoutMinutes,
          CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5,
          StrId::STR_SLEEP_TIMER_VALUE_FORMAT, false, StrId::STR_SLEEP_NEVER),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

std::string SettingsActivity::settingValueText(const SettingInfo& setting) {
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    return SETTINGS.*(setting.valuePtr) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }
  if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    // Guard like the valueGetter branch below: a corrupt/migrated settings
    // byte must not index past the enum table.
    const uint8_t value = SETTINGS.*(setting.valuePtr);
    if (value >= setting.enumValues.size()) return "";
    return I18N.get(setting.enumValues[value]);
  }
  if (setting.type == SettingType::ENUM && setting.valueGetter) {
    const uint8_t value = setting.valueGetter();
    if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
      return setting.enumStringValues[value];
    }
    if (value < setting.enumValues.size()) {
      return I18N.get(setting.enumValues[value]);
    }
    return "";
  }
  if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
      if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) {
        return tr(STR_SLEEP_NEVER);
      }
      char valueBuffer[32];
      snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
               static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
      return valueBuffer;
    }
    return std::to_string(SETTINGS.*(setting.valuePtr));
  }
  return "";
}

void SettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints and (on
  // Aurora) the persistent home dock.
  const int dockH = GUI.ownsHomeLayout() ? GUI.bottomBarHeight() : 0;
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight + dockH), 0});

  buildTabBar(screen);

  // rowItems_ (label/actionValue) was built by rebuildRowItems() when the
  // category was last selected/rebuilt; only the live value text needs
  // refreshing here, by assigning into the existing rowValues_ strings (no
  // vector growth) rather than building a new items/values vector on every
  // render.
  const auto& settings = *currentSettings;
  for (size_t row = 0; row < rowItems_.size(); row++) {
    const int16_t index = rowSetting_[row];
    if (index < 0) continue;  // section header: no value column
    rowValues_[index] = settingValueText(settings[index]);
    rowItems_[row].value = rowValues_[index].empty() ? nullptr : rowValues_[index].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Titles match the value's font size (smallText) so both sides of a row
  // read as one unit; labels that still don't fit wrap onto a second line.
  // maxLines=2 also marks the style explicitly set (an all-default smallText
  // fails textStyleUnset and the list would substitute bodyText back); the
  // common fits-on-one-line case takes the renderer's fast path anyway.
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

void SettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  if (GUI.ownsSettingsLayout()) {
    renderAurora();
    return;
  }

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app. The firmware
  // version is deliberately not in the header: it competes with the clock and
  // battery for the same band (it lives in the About/update screens instead).
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SETTINGS_TITLE));

  renderUi();

  const int ring = ringPos();
  const auto confirmLabel =
      (ring == 0) ? I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount])
                  : (ring > 0 && (*currentSettings)[ring - 1].nameId == StrId::STR_TIME_TO_SLEEP ? tr(STR_SELECT)
                                                                                                 : tr(STR_TOGGLE));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Aurora: keep the home dock under the stock settings so the other tabs
  // stay one tap away.
  if (GUI.ownsHomeLayout()) {
    HomeTabBar::draw(renderer, pageWidth, renderer.getScreenHeight(), HomeTabBar::Settings);
  }

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
