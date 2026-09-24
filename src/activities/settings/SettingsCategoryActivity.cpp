#include "SettingsCategoryActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <span>

#include "AboutActivity.h"
#include "BatteryMonitorActivity.h"
#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "ClockSettingsActivity.h"
#include "ControlCenterSettingsActivity.h"
#include "DropCapFontSelectionActivity.h"
#include "FontDownloadActivity.h"
#include "HomeButtonSettingsActivity.h"
#include "KOReaderSettingsActivity.h"
#include "KeyActionsSettingsActivity.h"
#include "KeyboardLayoutsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "SilentRestart.h"
#include "StatusBarSettingsActivity.h"
#include "TextSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
#include "util/ScreenOrientation.h"

namespace fui = freeink::ui;

namespace {

// --- Sections -----------------------------------------------------------------
// Each category page is a run of sections, each a list of setting names in the
// order they are shown. A setting no section names lands in the category's
// fallback section, so one added upstream still shows up without being placed.

struct Section {
  StrId heading;
  std::span<const StrId> rows;
};

constexpr StrId kDisplayLook[] = {StrId::STR_UI_THEME, StrId::STR_SYSTEM_FONT, StrId::STR_NIGHT_MODE,
                                  StrId::STR_ORIENTATION, StrId::STR_HIDE_BATTERY};
constexpr StrId kDisplayPanel[] = {StrId::STR_REFRESH_FREQ, StrId::STR_SUNLIGHT_FADING_FIX,
                                   StrId::STR_RESTORE_LIGHT_ON_WAKE, StrId::STR_CUSTOMISE_CONTROL_CENTER};
constexpr StrId kDisplaySleep[] = {StrId::STR_SLEEP_SCREEN, StrId::STR_SLEEP_COVER_MODE, StrId::STR_SLEEP_COVER_FILTER,
                                   StrId::STR_QUICK_RESUME_TIMEOUT};
constexpr Section kDisplaySections[] = {{StrId::STR_SEC_APPEARANCE, kDisplayLook},
                                        {StrId::STR_SEC_EINK_PANEL, kDisplayPanel},
                                        {StrId::STR_SLEEP_SCREEN, kDisplaySleep}};

constexpr StrId kReaderText[] = {StrId::STR_TEXT_SETTINGS, StrId::STR_MANAGE_FONTS, StrId::STR_DROP_CAPS,
                                 StrId::STR_DROP_CAP_FONT, StrId::STR_SMALL_CAPS};
constexpr StrId kReaderPage[] = {StrId::STR_IMAGES, StrId::STR_READER_MENU_STYLE, StrId::STR_CUSTOMISE_STATUS_BAR};
constexpr Section kReaderSections[] = {{StrId::STR_SEC_TEXT, kReaderText}, {StrId::STR_SEC_PAGE, kReaderPage}};

constexpr StrId kControlsTouch[] = {StrId::STR_TOUCH_READER_CONTROLS, StrId::STR_NEXT_PAGE_GESTURE,
                                    StrId::STR_PREV_PAGE_GESTURE, StrId::STR_SHOW_READER_MENU,
                                    StrId::STR_TILT_PAGE_TURN};
constexpr StrId kControlsButtons[] = {StrId::STR_REMAP_FRONT_BUTTONS,          StrId::STR_SIDE_BTN_LAYOUT,
                                      StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, StrId::STR_SHOW_BUTTON_HINTS,
                                      StrId::STR_LONG_PRESS_BEHAVIOR,          StrId::STR_LONG_PRESS_MENU,
                                      StrId::STR_BACK_SHORT_TO_FILE_BROWSER,   StrId::STR_PWR_BTN_FOOTNOTE_BACK,
                                      StrId::STR_DBL_CLICK_PWR_LIGHT,          StrId::STR_SEC_KEY_ACTIONS};
constexpr Section kControlsSections[] = {{StrId::STR_SEC_TOUCH, kControlsTouch},
                                         {StrId::STR_SEC_BUTTONS, kControlsButtons}};

constexpr StrId kSystemPower[] = {StrId::STR_TIME_TO_SLEEP, StrId::STR_TIMEOUT_ACTION, StrId::STR_LIGHT_SLEEP_IDLE,
                                  StrId::STR_BATTERY_MONITOR};
constexpr StrId kSystemLibrary[] = {StrId::STR_SHOW_HIDDEN_FILES, StrId::STR_LIBRARY_USE_METADATA,
                                    StrId::STR_REMOVE_READ_FROM_RECENTS, StrId::STR_MOVE_FINISHED_TO_READ,
                                    StrId::STR_CLEAR_READING_CACHE};
constexpr StrId kSystemConnect[] = {StrId::STR_WIFI_NETWORKS, StrId::STR_KOREADER_SYNC, StrId::STR_OPDS_SERVERS,
                                    StrId::STR_CLOCK};
constexpr StrId kSystemDevice[] = {StrId::STR_LANGUAGE, StrId::STR_KEYBOARD_LAYOUTS, StrId::STR_CHECK_UPDATES,
                                   StrId::STR_SD_FIRMWARE_UPDATE, StrId::STR_ABOUT};
constexpr Section kSystemSections[] = {{StrId::STR_SEC_POWER, kSystemPower},
                                       {StrId::STR_SEC_LIBRARY, kSystemLibrary},
                                       {StrId::STR_SEC_CONNECTIONS, kSystemConnect},
                                       {StrId::STR_CAT_DEVICE, kSystemDevice}};

using Category = SettingsCategoryActivity::Category;

std::span<const Section> sectionsFor(const Category category) {
  switch (category) {
    case Category::Display:
      return kDisplaySections;
    case Category::Reader:
      return kReaderSections;
    case Category::Controls:
      return kControlsSections;
    case Category::System:
      return kSystemSections;
  }
  return {};
}

// Where a setting no section names goes. Controls keeps its leftovers with the
// buttons, as the tabbed list did; elsewhere they get a trailing "Other".
StrId fallbackSection(const Category category) {
  return category == Category::Controls ? StrId::STR_SEC_BUTTONS : StrId::STR_SEC_OTHER;
}

StrId categoryId(const Category category) {
  switch (category) {
    case Category::Display:
      return StrId::STR_CAT_DISPLAY;
    case Category::Reader:
      return StrId::STR_CAT_READER;
    case Category::Controls:
      return StrId::STR_CAT_CONTROLS;
    case Category::System:
      return StrId::STR_CAT_SYSTEM;
  }
  return StrId::STR_CAT_SYSTEM;
}

// The category's settings from the shared list, filtered for this board, plus
// the device-only rows that open their own screens.
std::vector<SettingInfo> collectSettings(const Category category) {
  // Pick up fonts uploaded/deleted over the web server since the last reader
  // activity ran, and dictionaries copied to the SD card since the last visit.
  sdFontSystem.refreshIfDirty();
  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);

  const StrId want = categoryId(category);
  std::vector<SettingInfo> out;
  for (auto& setting : getSettingsList(&sdFontSystem.registry(), &dictionaries, &sdFontSystem.dropCapRegistry())) {
    if (setting.category != want) continue;
    switch (category) {
      case Category::Display:
        // The sunlight fading fix is a grayscale-waveform compensation that does
        // not apply on the X4 Pro / X4 Classic (plain OTP waveform, same panels).
        if (setting.valuePtr == &CrossPointSettings::fadingFix &&
            (BoardConfig::isX4Pro() || BoardConfig::isX4Classic())) {
          continue;
        }
        break;
      case Category::Reader:
        // Rows folded into the Text Settings screen stay in the shared list for
        // the web settings API only. Night mode is the same setting Display
        // already shows; one place is enough.
        if (setting.inTextSettings || setting.valuePtr == &CrossPointSettings::screenInverted) continue;
        break;
      case Category::Controls:
        if (BoardConfig::hasHomeKey() && setting.valuePtr == &CrossPointSettings::longPressMenuFunction) continue;
        if (setting.valuePtr == &CrossPointSettings::pwrBtnFootnoteBack &&
            SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) {
          continue;
        }
        // The per-key tap/hold pickers live on their own screen (one row below
        // opens it); they stay in the shared list for the web API.
        if (KeyActionsSettingsActivity::owns(setting.nameId)) continue;
        break;
      case Category::System:
        break;
    }
    out.push_back(std::move(setting));
  }

  switch (category) {
    case Category::Display:
      // The control center is display chrome, reachable on every board (top-edge
      // swipe, status-bar tap, or a key bound to it), so the row is not touch-gated.
      out.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_CONTROL_CENTER, SettingAction::CustomiseControlCenter));
      break;
    case Category::Reader:
      out.push_back(SettingInfo::Action(StrId::STR_TEXT_SETTINGS, SettingAction::TextSettings));
      out.push_back(SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts));
      out.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));
      break;
    case Category::Controls:
      if (!BoardConfig::hasTouch()) {
        out.push_back(SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
      }
      out.push_back(SettingInfo::Action(StrId::STR_SEC_KEY_ACTIONS, SettingAction::KeyActions));
      break;
    case Category::System:
      out.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
      // Clock configuration only exists where the RTC probe found hardware.
      if (halClock.isAvailable()) {
        out.push_back(SettingInfo::Action(StrId::STR_CLOCK, SettingAction::ClockSettings));
      }
      out.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
      out.push_back(SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser));
      out.push_back(SettingInfo::Action(StrId::STR_BATTERY_MONITOR, SettingAction::BatteryMonitor));
      out.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
      // OTA fetches this board's own release asset; boards whose asset isn't
      // published yet just report no update available.
      out.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
      out.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
      out.push_back(SettingInfo::Action(StrId::STR_KEYBOARD_LAYOUTS, SettingAction::KeyboardLayouts));
      out.push_back(SettingInfo::Action(StrId::STR_ABOUT, SettingAction::About));
      out.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
      break;
  }
  return out;
}

// True when activating the row opens something (a picker, another screen)
// rather than flipping the value in place: those rows end in "›".
bool opensSomething(const SettingInfo& setting) {
  if (setting.type == SettingType::ACTION || setting.nameId == StrId::STR_TIME_TO_SLEEP ||
      setting.nameId == StrId::STR_DROP_CAP_FONT) {
    return true;
  }
  if (setting.type != SettingType::ENUM) return false;
  const size_t options =
      setting.enumStringValues.empty() ? setting.enumLabels().size() : setting.enumStringValues.size();
  return options > 2;
}

}  // namespace

SettingsCategoryActivity::SettingsCategoryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   const Category category)
    : UiListActivity("SettingsCategory", renderer, mappedInput), category(category) {}

void SettingsCategoryActivity::onEnter() {
  preserveQuickResumeTimeoutOn =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  quickResumeTimeoutAutoEnabled = false;
  syncQuickResumeTimeoutForSleepScreen(/*sleepScreenChanged=*/true, /*quickResumeTimeoutChanged=*/false);
  rebuildRows();
  UiListActivity::onEnter();
  // Touch boards open with no row focused: the inverted cursor only means
  // something once a button has moved it.
  if (mappedInput.hasTouch()) nav.selected = -1;
}

void SettingsCategoryActivity::rebuildRows() {
  std::vector<SettingInfo> pool = collectSettings(category);
  std::vector<bool> placed(pool.size(), false);

  settings_.clear();
  rowSection_.clear();
  // Stable placement: each section takes its named rows in its own order;
  // whatever is left joins the fallback section, in list order.
  const auto sections = sectionsFor(category);
  const StrId fallback = fallbackSection(category);
  const auto placeLeftovers = [&] {
    for (size_t i = 0; i < pool.size(); ++i) {
      if (placed[i]) continue;
      placed[i] = true;
      settings_.push_back(pool[i]);
      rowSection_.push_back(fallback);
    }
  };
  bool fallbackPlaced = false;
  for (const Section& section : sections) {
    for (const StrId id : section.rows) {
      for (size_t i = 0; i < pool.size(); ++i) {
        if (placed[i] || pool[i].nameId != id) continue;
        placed[i] = true;
        settings_.push_back(pool[i]);
        rowSection_.push_back(section.heading);
        break;
      }
    }
    if (section.heading == fallback) {
      placeLeftovers();
      fallbackPlaced = true;
    }
  }
  if (!fallbackPlaced) placeLeftovers();

  rowValues_.assign(settings_.size(), std::string());
  rowItems_.assign(settings_.size(), fui::ListItem{});
  for (size_t i = 0; i < settings_.size(); ++i) {
    fui::ListItem& item = rowItems_[i];
    item.label = I18N.get(settings_[i].nameId);
    item.actionValue = static_cast<int16_t>(i);
    if (i == 0 || rowSection_[i] != rowSection_[i - 1]) item.sectionHeading = I18N.get(rowSection_[i]);
  }
  if (nav.selected >= listCount()) nav.selected = std::max(0, listCount() - 1);
}

const char* SettingsCategoryActivity::headerTitle() const { return backHeader(StrId::STR_SETTINGS_TITLE); }

void SettingsCategoryActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Touch boards never draw the physical-button hint row, so they get the band
  // back for rows instead of leaving it blank.
  const bool hintsShown = !mappedInput.hasTouch() && SETTINGS.showFrontButtonHints();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(hintsShown ? metrics.buttonHintsHeight : 0), 0});

  // Row structure was built by rebuildRows(); only the live values refresh
  // here, assigned into the existing strings.
  for (size_t i = 0; i < settings_.size(); ++i) {
    fui::ListItem& item = rowItems_[i];
    const SettingInfo& setting = settings_[i];
    if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
      item.toggle = true;
      item.toggleChecked = SETTINGS.*(setting.valuePtr) != 0;
      item.value = nullptr;
      continue;
    }
    rowValues_[i] = settingValueText(setting);
    if (opensSomething(setting)) {
      rowValues_[i] += rowValues_[i].empty() ? "›" : "  ›";
    }
    item.value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  const auto& theme = screen.theme();
  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Labels in the body face, values a size down: the name is what you scan
  // for, the value is secondary. Long labels wrap onto a second line.
  props.labelText = theme.bodyText;
  props.labelText.maxLines = 2;
  props.valueText = theme.smallText;
  // A switch big enough to read at arm's length and to hit with a thumb,
  // rounded with the theme's capsule token.
  props.toggleWidth = 48;
  props.toggleHeight = 26;
  props.toggleRadius = theme.capsuleRadius;
  props.toggleKnobRadius = theme.capsuleRadius;
  props.toggleKnobInset = 4;
  props.toggleBorderWidth = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void SettingsCategoryActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

bool SettingsCategoryActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void SettingsCategoryActivity::onBackButton() {
  SETTINGS.saveToFile();
  finish();
}

void SettingsCategoryActivity::activateIndex(const int index) {
  if (optionPopup.isActive() || index < 0 || index >= listCount()) return;
  // Most rows repaint a different surface (popup, sub-activity, new value); a
  // lingering tap flash would gray an unrelated element.
  app.clearTapFlash();
  mappedInput.resetHomeButtonInput();
  activateSetting(settings_[index]);
  // Tap-first: a tapped row is not a cursor position. Leaving it focused kept
  // it inverted once its popup or sub-screen closed.
  if (mappedInput.hasTouch()) nav.selected = -1;
  requestUpdate();
}

// Toggle/cycle/open one row.
void SettingsCategoryActivity::activateSetting(const SettingInfo& setting) {
  const bool sleepScreenChanged = setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    SETTINGS.*(setting.valuePtr) = !(SETTINGS.*(setting.valuePtr));
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    const auto enumLabels = setting.enumLabels();
    if (enumLabels.size() > 2) {
      const auto valuePtr = setting.valuePtr;
      optionPopup.show(setting.nameId, enumLabels.data(), static_cast<int>(enumLabels.size()), currentValue,
                       [this, valuePtr, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
                         SETTINGS.*valuePtr = idx;
                         syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
                         SETTINGS.saveToFile();
                         rebuildRows();
                         applyUiSettingChange(valuePtr);
                       });
      return;
    }
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(enumLabels.size());
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    if (setting.nameId == StrId::STR_DROP_CAP_FONT) {
      // The drop-cap font picker (preview + list) instead of cycling.
      startActivityForResult(
          std::make_unique<DropCapFontSelectionActivity>(renderer, mappedInput, &sdFontSystem.dropCapRegistry()),
          [this](const ActivityResult&) {
            SETTINGS.saveToFile();
            rebuildRows();
          });
      return;
    }
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumLabels().size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    const uint8_t cur = setting.valueGetter();
    if (totalValues > 2) {
      const auto valueSetter = setting.valueSetter;
      auto onSelect = [this, valueSetter, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
        valueSetter(idx);
        syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
        SETTINGS.saveToFile();
        rebuildRows();
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, cur, std::move(onSelect));
      } else {
        const auto enumLabels = setting.enumLabels();
        optionPopup.show(setting.nameId, enumLabels.data(), static_cast<int>(enumLabels.size()), cur,
                         std::move(onSelect));
      }
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
    auto resultHandler = [this](const ActivityResult&) {
      SETTINGS.saveToFile();
      requestUpdate();
    };
    // Screens that can change which rows exist (fonts, language) rebuild them.
    auto rebuildHandler = [this](const ActivityResult&) {
      SETTINGS.saveToFile();
      rebuildRows();
      requestUpdate();
    };

    switch (setting.action) {
      case SettingAction::HomeButton:
        if (auto activity = makeUniqueNoThrow<HomeButtonSettingsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), resultHandler);
        } else {
          LOG_ERR("SET", "OOM: Home button settings");
        }
        break;
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
      case SettingAction::ClockSettings:
        if (auto activity = makeUniqueNoThrow<ClockSettingsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), resultHandler);
        } else {
          LOG_ERR("SET", "OOM: ClockSettingsActivity");
        }
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Network: {
        auto activity = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, false);
        if (!activity) {
          LOG_ERR("SET", "OOM: WifiSelectionActivity");
          return;
        }
        startActivityForResult(std::move(activity), [](const ActivityResult&) {
          SETTINGS.saveToFile();
          // Every other WiFi consumer hands the radio to a session it owns;
          // this row only saves credentials, so nothing would ever release the
          // driver's heap. The scan alone brings it up, so tear down whether or
          // not the user joined a network.
          if (WiFi.getMode() == WIFI_MODE_NULL) return;
          WiFi.disconnect(false);
          delay(30);
          // Runs from the loop task with no lock held; the restart popup paints
          // straight to the panel.
          RenderLock lock;
          silentRestartToSettings();
        });
        break;
      }
      case SettingAction::BatteryMonitor:
        startActivityForResult(std::make_unique<BatteryMonitorActivity>(renderer, mappedInput), resultHandler);
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
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput), rebuildHandler);
        break;
      case SettingAction::TextSettings:
        startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                      TextSettingsActivity::Tab::Family),
                               rebuildHandler);
        break;
      case SettingAction::Language:
        // Row labels are translated once in rebuildRows(), so a language
        // switch needs the rebuild.
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput), rebuildHandler);
        break;
      case SettingAction::KeyboardLayouts:
        if (auto activity = makeUniqueNoThrow<KeyboardLayoutsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), resultHandler);
        } else {
          LOG_ERR("SET", "OOM: KeyboardLayoutsActivity");
        }
        break;
      case SettingAction::About:
        if (auto activity = makeUniqueNoThrow<AboutActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), resultHandler);
        } else {
          LOG_ERR("SET", "OOM: AboutActivity");
        }
        break;
      case SettingAction::None:
        break;
    }
    return;  // the result handlers save
  } else {
    return;
  }

  syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
  SETTINGS.saveToFile();
  rebuildRows();
  applyUiSettingChange(setting.valuePtr);
}

void SettingsCategoryActivity::applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr) {
  if (valuePtr == &CrossPointSettings::orientation) {
    // Turn the screen under the user's finger: this page is drawn the new way
    // up, which needs a full refresh and a rebuilt interaction table.
    applyScreenOrientation(renderer);
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    resetUi();
    return;
  }
  if (valuePtr == &CrossPointSettings::uiTheme) {
    // Theme changes take effect on this screen: reload and re-derive tokens so
    // the next repaint is in the new look.
    UITheme::getInstance().reload();
    resetUi();
  }
}

void SettingsCategoryActivity::syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged,
                                                                    bool quickResumeTimeoutChanged) {
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

void SettingsCategoryActivity::openSleepTimeoutPicker() {
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

std::string SettingsCategoryActivity::settingValueText(const SettingInfo& setting) {
  if (setting.action == SettingAction::HomeButton) return tr(STR_CONFIGURE);
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    return SETTINGS.*(setting.valuePtr) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }
  if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    // A corrupt/migrated settings byte must not index past the enum table.
    const uint8_t value = SETTINGS.*(setting.valuePtr);
    const auto enumLabels = setting.enumLabels();
    if (value >= enumLabels.size()) return "";
    return I18N.get(enumLabels[value]);
  }
  if (setting.type == SettingType::ENUM && setting.valueGetter) {
    const uint8_t value = setting.valueGetter();
    if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
      return setting.enumStringValues[value];
    }
    const auto enumLabels = setting.enumLabels();
    if (value < enumLabels.size()) return I18N.get(enumLabels[value]);
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
