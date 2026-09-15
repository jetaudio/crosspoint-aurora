#include "NetworkModeSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/HomeTabBar.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId menuItems[NetworkModeSelectionActivity::MENU_ITEM_COUNT] = {
    StrId::STR_JOIN_NETWORK,
    StrId::STR_CALIBRE_WIRELESS,
    StrId::STR_CREATE_HOTSPOT,
#if FREEINK_CAP_USB_MSC
    StrId::STR_USB_DRIVE,
#endif
};
constexpr StrId menuDescs[NetworkModeSelectionActivity::MENU_ITEM_COUNT] = {
    StrId::STR_JOIN_DESC,
    StrId::STR_CALIBRE_DESC,
    StrId::STR_HOTSPOT_DESC,
#if FREEINK_CAP_USB_MSC
    StrId::STR_USB_DRIVE_DESC,
#endif
};
constexpr UIIcon menuIcons[NetworkModeSelectionActivity::MENU_ITEM_COUNT] = {
    UIIcon::Wifi,
    UIIcon::Library,
    UIIcon::Hotspot,
#if FREEINK_CAP_USB_MSC
    UIIcon::Usb,
#endif
};
}  // namespace

NetworkModeSelectionActivity::NetworkModeSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("NetworkModeSelection", renderer, mappedInput) {
  // Entirely static, so built once here rather than every buildScreen() call.
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    fui::ListItem item;
    item.label = I18N.get(menuItems[i]);
    item.subtitle = I18N.get(menuDescs[i]);
    item.icon = listIconFor(menuIcons[i], 32);  // subtitle rows carry the larger icon
    item.actionValue = static_cast<int16_t>(i);
    rowItems_[i] = item;
  }
}

int NetworkModeSelectionActivity::listCount() const { return MENU_ITEM_COUNT; }

// Aurora hosts this screen as the Transfer tab of the bottom bar, so it carries
// the tab's name there and the stand-alone screen title everywhere else.
const char* NetworkModeSelectionActivity::headerTitle() const {
  return GUI.ownsHomeLayout() ? tr(STR_TAB_TRANSFER) : tr(STR_FILE_TRANSFER);
}

bool NetworkModeSelectionActivity::handleCustomInput() {
  // Aurora: this is the Transfer tab's landing screen, so front Left/Right walk
  // the bottom bar (the base's list navigation keeps the side Up/Down buttons).
  if (!GUI.ownsHomeLayout()) return false;
  return HomeTabBar::handleLeftRight(mappedInput, HomeTabBar::Transfer) ||
         HomeTabBar::handleTap(mappedInput, renderer, HomeTabBar::Transfer);
}

// The front Left/Right pair belongs to the bottom bar in tab mode, so only the side
// buttons move the selection there (the base's logical Nav pair folds them in).
void NetworkModeSelectionActivity::navigateButtons() {
  if (!GUI.ownsHomeLayout()) {
    UiListActivity::navigateButtons();
    return;
  }
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] {
    moveSelectionTo(ButtonNavigator::previousIndex(activeNav().selected, MENU_ITEM_COUNT));
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] {
    moveSelectionTo(ButtonNavigator::nextIndex(activeNav().selected, MENU_ITEM_COUNT));
  });
}

void NetworkModeSelectionActivity::drawFooter() {
  if (!GUI.ownsHomeLayout()) {
    UiListActivity::drawFooter();
    return;
  }
  // Tab mode: front buttons move between tabs, the side pair moves the selection.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  HomeTabBar::draw(renderer, renderer.getScreenWidth(), renderer.getScreenHeight(), HomeTabBar::Transfer);
}

void NetworkModeSelectionActivity::activateIndex(const int index) {
  // Selection leaves this screen; a lingering flash would gray an unrelated
  // element on the next render.
  app.clearTapFlash();
  nav.selected = index;

  onModeSelected(static_cast<NetworkMode>(index));
}

void NetworkModeSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints — plus the
  // Aurora bottom bar, and minus the hint row when the user hid it.
  const bool tabMode = GUI.ownsHomeLayout();
  const int hintH = (tabMode && !SETTINGS.showButtonHints) ? 0 : metrics.buttonHintsHeight;
  const int bottom = hintH + (tabMode ? GUI.bottomBarHeight() : 0);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(bottom), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems_ was built once in the constructor and is reused here on every
  // repaint.
  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEM_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
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
