#pragma once

#include <I18n.h>

#include <cstdint>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "SettingsActivity.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// One Settings category as a touch-first page: rows grouped under section
// headings, an on/off setting drawn as a switch, and a trailing "›" on every
// row that opens a picker or another screen. The header names the way back
// ("‹ Settings") and is itself a tap target.
class SettingsCategoryActivity final : public UiListActivity {
 public:
  enum class Category : uint8_t { Display, Reader, Controls, System };

  SettingsCategoryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Category category);
  void onEnter() override;
  void render(RenderLock&& lock) override;

 private:
  const Category category;

  // Rows in display order. rowSection_[i] is the section row i belongs to; the
  // first row of each section carries its heading (ListItem::sectionHeading),
  // so rows and settings stay 1:1 and navigation needs no header skipping.
  std::vector<SettingInfo> settings_;
  std::vector<StrId> rowSection_;
  std::vector<freeink::ui::ListItem> rowItems_;
  // Live value text per row ("Portrait ›"), refreshed on every build by
  // assigning into the existing strings.
  std::vector<std::string> rowValues_;

  OptionPopup optionPopup;
  bool preserveQuickResumeTimeoutOn = false;
  bool quickResumeTimeoutAutoEnabled = false;

  // Collect this category's settings, order them into sections, and rebuild
  // the row structure. Call when the list itself may have changed (fonts
  // uploaded, dictionaries copied, a setting that hides another).
  void rebuildRows();
  void activateSetting(const SettingInfo& setting);
  void openSleepTimeoutPicker();
  void syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged);
  void applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr);
  static std::string settingValueText(const SettingInfo& setting);

  // --- UiListActivity contract ---
  int listCount() const override { return static_cast<int>(settings_.size()); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void onBackButton() override;
};
