#include "NetworkModeSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/HomeTabBar.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int MENU_ITEM_COUNT = 3;
}  // namespace

void NetworkModeSelectionActivity::onEnter() {
  Activity::onEnter();

  // Reset selection
  selectedIndex = 0;

  // Trigger first update
  requestUpdate();
}

void NetworkModeSelectionActivity::onExit() { Activity::onExit(); }

void NetworkModeSelectionActivity::loop() {
  // Handle back button - cancel
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
    return;
  }

  // Aurora: this is the Transfer tab's landing screen, so Left/Right switch tabs.
  const bool tabMode = GUI.ownsHomeLayout();
  if (tabMode && HomeTabBar::handleLeftRight(mappedInput, HomeTabBar::Transfer)) return;

  // Handle confirm button - select current option
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    NetworkMode mode = NetworkMode::JOIN_NETWORK;
    if (selectedIndex == 1) {
      mode = NetworkMode::CONNECT_CALIBRE;
    } else if (selectedIndex == 2) {
      mode = NetworkMode::CREATE_HOTSPOT;
    }
    onModeSelected(mode);
    return;
  }

  // Navigation: side Up/Down (front Left/Right are reserved for tabs in Aurora).
  if (tabMode) {
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, MENU_ITEM_COUNT);
      requestUpdate();
    });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, MENU_ITEM_COUNT);
      requestUpdate();
    });
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, MENU_ITEM_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, MENU_ITEM_COUNT);
    requestUpdate();
  });
}

void NetworkModeSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Aurora draws the persistent tab bar here (Transfer tab) and only reserves the
  // hint row the user actually shows.
  const bool tabMode = GUI.ownsHomeLayout();
  const int barH = tabMode ? GUI.bottomBarHeight() : 0;
  const int hintH = (tabMode && !SETTINGS.showButtonHints) ? 0 : metrics.buttonHintsHeight;

  // Menu items and descriptions
  static constexpr StrId menuItems[MENU_ITEM_COUNT] = {StrId::STR_JOIN_NETWORK, StrId::STR_CALIBRE_WIRELESS,
                                                       StrId::STR_CREATE_HOTSPOT};
  static constexpr StrId menuDescs[MENU_ITEM_COUNT] = {StrId::STR_JOIN_DESC, StrId::STR_CALIBRE_DESC,
                                                       StrId::STR_HOTSPOT_DESC};
  static constexpr UIIcon menuIcons[MENU_ITEM_COUNT] = {UIIcon::Wifi, UIIcon::Library, UIIcon::Hotspot};

  const char* title = tabMode ? tr(STR_TAB_TRANSFER) : tr(STR_FILE_TRANSFER);

  if (GUI.ownsSettingsLayout()) {
    // Render the transfer options as a settings-style card list so this landing
    // screen matches the Settings page. The ▸ chevron marks each entry as "opens".
    std::vector<SettingsListItem> items;
    items.reserve(MENU_ITEM_COUNT);
    for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
      SettingsListItem it;
      it.text = I18N.get(menuItems[i]);
      it.selected = (i == selectedIndex);
      it.showChevron = true;
      items.push_back(it);
    }
    GUI.drawSettingsScreen(renderer, Rect{0, 0, pageWidth, pageHeight - hintH - barH}, title, items);
  } else {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - hintH - metrics.verticalSpacing * 2 - barH;
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(MENU_ITEM_COUNT), selectedIndex,
        [](int index) { return std::string(I18N.get(menuItems[index])); },
        [](int index) { return std::string(I18N.get(menuDescs[index])); }, [](int index) { return menuIcons[index]; });
  }

  // Draw help text at bottom
  if (tabMode) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    HomeTabBar::draw(renderer, pageWidth, pageHeight, HomeTabBar::Transfer);
  } else {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}

void NetworkModeSelectionActivity::onModeSelected(NetworkMode mode) {
  setResult(NetworkModeResult{mode});
  finish();
}

void NetworkModeSelectionActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
