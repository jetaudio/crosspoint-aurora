#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SettingsCategoryActivity.h"
#include "activities/util/HomeTabBar.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/settingsIcons.h"

namespace fui = freeink::ui;

namespace {
using Category = SettingsCategoryActivity::Category;

struct CategoryRow {
  Category category;
  StrId name;
  StrId description;
};

constexpr CategoryRow kCategories[] = {
    {Category::Display, StrId::STR_CAT_DISPLAY, StrId::STR_CAT_DISPLAY_DESC},
    {Category::Reader, StrId::STR_CAT_READER, StrId::STR_CAT_READER_DESC},
    {Category::Controls, StrId::STR_CAT_CONTROLS, StrId::STR_CAT_CONTROLS_DESC},
    {Category::System, StrId::STR_CAT_SYSTEM, StrId::STR_CAT_SYSTEM_DESC},
};

fui::BitmapRef categoryIcon(const Category category) {
  switch (category) {
    case Category::Display:
      return fui::bitmapFromIcon(icon_settings_display_32);
    case Category::Reader:
      return fui::bitmapFromIcon(icon_settings_reader_32);
    case Category::Controls:
      return fui::bitmapFromIcon(icon_settings_controls_32);
    case Category::System:
      return fui::bitmapFromIcon(icon_settings_system_32);
  }
  return {};
}
}  // namespace

SettingsActivity::SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Settings", renderer, mappedInput) {
  static_assert(sizeof(kCategories) / sizeof(kCategories[0]) == kCategoryCount);
  buildRows();
}

// Labels are resolved through I18N, so a language change made inside a
// category page needs the rows rebuilt on return.
void SettingsActivity::buildRows() {
  for (int i = 0; i < kCategoryCount; ++i) {
    fui::ListItem item;
    item.label = I18N.get(kCategories[i].name);
    item.subtitle = I18N.get(kCategories[i].description);
    item.icon = categoryIcon(kCategories[i].category);
    item.value = "›";
    item.actionValue = static_cast<int16_t>(i);
    rowItems_[i] = item;
  }
}

void SettingsActivity::onEnter() {
  UiListActivity::onEnter();
  // Touch boards open with no row focused (tap-first).
  if (mappedInput.hasTouch()) nav.selected = -1;
}

void SettingsActivity::onExit() {
  UiListActivity::onExit();
  UITheme::getInstance().reload();  // Re-apply the theme in case it was changed
}

const char* SettingsActivity::headerTitle() const { return tr(STR_SETTINGS_TITLE); }

bool SettingsActivity::handleCustomInput() {
  // Aurora: this is the Settings tab's landing screen, so the dock under it
  // switches tabs by tap, and the front Left/Right walk it.
  if (!GUI.ownsHomeLayout()) return false;
  if (HomeTabBar::handleLeftRight(mappedInput, HomeTabBar::Settings) ||
      HomeTabBar::handleTap(mappedInput, renderer, HomeTabBar::Settings)) {
    SETTINGS.saveToFile();
    return true;
  }
  return false;
}

// With the dock, the front Left/Right pair belongs to it, so only the side
// buttons move the selection.
void SettingsActivity::navigateButtons() {
  if (!GUI.ownsHomeLayout()) {
    UiListActivity::navigateButtons();
    return;
  }
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] {
    moveSelectionTo(ButtonNavigator::previousIndex(activeNav().selected, kCategoryCount));
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] {
    moveSelectionTo(ButtonNavigator::nextIndex(activeNav().selected, kCategoryCount));
  });
}

void SettingsActivity::onBackButton() {
  SETTINGS.saveToFile();
  onGoHome();
}

void SettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= kCategoryCount) return;
  // The category page replaces this surface; a lingering flash would gray an
  // unrelated element on the next render.
  app.clearTapFlash();
  startActivityForResult(std::make_unique<SettingsCategoryActivity>(renderer, mappedInput, kCategories[index].category),
                         [this](const ActivityResult&) {
                           // The category page may have changed the theme,
                           // orientation or language: re-derive tokens and
                           // labels before the next paint.
                           buildRows();
                           resetUi();
                           if (mappedInput.hasTouch()) nav.selected = -1;
                           requestUpdate();
                         });
}

void SettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the header band, above the button hints (never drawn on
  // touch boards) and, on Aurora, the home dock.
  const bool hintsShown = !mappedInput.hasTouch() && SETTINGS.showFrontButtonHints();
  const int bottom = (hintsShown ? metrics.buttonHintsHeight : 0) + (GUI.ownsHomeLayout() ? GUI.bottomBarHeight() : 0);
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0, static_cast<int16_t>(bottom), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_;
  props.count = kCategoryCount;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.valueText = screen.theme().bodyText;  // the "›" reads at the label's size
  props.iconSize = 32;
  syncListViewport(screen, props);
  screen.list(props);
}

void SettingsActivity::drawFooter() {
  if (!GUI.ownsHomeLayout()) {
    UiListActivity::drawFooter();
    return;
  }
  // Tab mode: front buttons move between tabs, the side pair moves the selection.
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  HomeTabBar::draw(renderer, renderer.getScreenWidth(), renderer.getScreenHeight(), HomeTabBar::Settings);
}
