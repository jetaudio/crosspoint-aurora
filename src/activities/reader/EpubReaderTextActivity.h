#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Compact in-reader "Text" panel (the Aurora reader toolbar's Text tool). Adjusts
// the most common reader display settings — font family, size, line spacing,
// paragraph alignment, focus reading — bound directly to CrossPointSettings.
// Changes are persisted on exit; the reader re-paginates when this panel closes.
class EpubReaderTextActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  enum Row { FONT_FAMILY = 0, FONT_SIZE, LINE_SPACING, ALIGNMENT, FOCUS, ROW_COUNT };

  std::string rowName(int row) const;
  std::string rowValue(int row) const;
  void cycle(int row, int dir);

 public:
  explicit EpubReaderTextActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("EpubReaderText", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
