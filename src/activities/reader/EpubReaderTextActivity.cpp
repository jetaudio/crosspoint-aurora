#include "EpubReaderTextActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace {
constexpr StrId kFamilyLabels[] = {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS, StrId::STR_OPEN_DYSLEXIC};
constexpr StrId kSizeLabels[] = {StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE};
constexpr StrId kSpacingLabels[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE};
constexpr StrId kAlignLabels[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                                  StrId::STR_BOOK_S_STYLE};

// Wrap a value into [0, count) after applying a +/-1 step.
uint8_t step(int value, int dir, int count) { return static_cast<uint8_t>((value + dir + count) % count); }
}  // namespace

void EpubReaderTextActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void EpubReaderTextActivity::onExit() { Activity::onExit(); }

std::string EpubReaderTextActivity::rowName(int row) const {
  switch (row) {
    case FONT_FAMILY:
      return tr(STR_FONT_FAMILY);
    case FONT_SIZE:
      return tr(STR_FONT_SIZE);
    case LINE_SPACING:
      return tr(STR_LINE_SPACING);
    case ALIGNMENT:
      return tr(STR_PARA_ALIGNMENT);
    case FOCUS:
      return tr(STR_FOCUS_READING);
    default:
      return "";
  }
}

std::string EpubReaderTextActivity::rowValue(int row) const {
  switch (row) {
    case FONT_FAMILY:
      if (SETTINGS.sdFontFamilyName[0] != '\0') return SETTINGS.sdFontFamilyName;
      return I18N.get(kFamilyLabels[SETTINGS.fontFamily % CrossPointSettings::FONT_FAMILY_COUNT]);
    case FONT_SIZE:
      return I18N.get(kSizeLabels[SETTINGS.fontSize % CrossPointSettings::FONT_SIZE_COUNT]);
    case LINE_SPACING:
      return I18N.get(kSpacingLabels[SETTINGS.lineSpacing % CrossPointSettings::LINE_COMPRESSION_COUNT]);
    case ALIGNMENT:
      return I18N.get(kAlignLabels[SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT]);
    case FOCUS:
      return SETTINGS.focusReadingEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    default:
      return "";
  }
}

void EpubReaderTextActivity::cycle(int row, int dir) {
  switch (row) {
    case FONT_FAMILY:
      // Cycling here always selects a built-in family; drop any SD font override.
      SETTINGS.fontFamily = step(SETTINGS.fontFamily, dir, CrossPointSettings::FONT_FAMILY_COUNT);
      SETTINGS.sdFontFamilyName[0] = '\0';
      break;
    case FONT_SIZE:
      SETTINGS.fontSize = step(SETTINGS.fontSize, dir, CrossPointSettings::FONT_SIZE_COUNT);
      break;
    case LINE_SPACING:
      SETTINGS.lineSpacing = step(SETTINGS.lineSpacing, dir, CrossPointSettings::LINE_COMPRESSION_COUNT);
      break;
    case ALIGNMENT:
      SETTINGS.paragraphAlignment =
          step(SETTINGS.paragraphAlignment, dir, CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT);
      break;
    case FOCUS:
      SETTINGS.focusReadingEnabled = SETTINGS.focusReadingEnabled ? 0 : 1;
      break;
    default:
      break;
  }
  requestUpdate();
}

void EpubReaderTextActivity::loop() {
  // Back: persist and return to the reader (which re-paginates on close).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    finish();
    return;
  }

  // Confirm or Right: next value; Left: previous value.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    cycle(selectedIndex, +1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    cycle(selectedIndex, -1);
    return;
  }

  // Up/Down: move between rows.
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ROW_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ROW_COUNT);
    requestUpdate();
  });
}

void EpubReaderTextActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TOOL_TEXT));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(ROW_COUNT), selectedIndex,
      [this](int index) { return rowName(index); }, nullptr, nullptr, [this](int index) { return rowValue(index); },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
