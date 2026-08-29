#pragma once
#include <string>
#include <vector>

#include "BatteryLog.h"
#include "activities/UiListActivity.h"

// "Battery monitor": where the charge went since the cable came out.
//
// The gauge sits in series with the pack, so the only thing this device can
// measure is the TOTAL drawn — there is no per-component sensing anywhere on the
// board. The split shown here is therefore an ESTIMATE: BatteryLog counts the
// time the device spent in each state, this screen multiplies those by bench
// figures, and the leftover is shown as its own row rather than being spread
// around to make the columns add up. A breakdown whose residual is hidden is a
// breakdown that cannot be caught being wrong.
//
// Read-only, and deliberately not self-refreshing: every repaint is an e-paper
// refresh, which is itself one of the loads being measured. Select re-reads.
class BatteryMonitorActivity final : public UiListActivity {
 public:
  explicit BatteryMonitorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;

  // Re-read the gauge and rebuild every row from the current counters. Render
  // task only: buildScreen() hands the list raw pointers into rowLabels_ /
  // rowValues_, and activateIndex() runs on the loop task, so rebuilding from
  // there would swap the strings out from under a frame being drawn.
  void rebuildRows();
  void addRow(const char* label, std::string value, bool isHeader = false);

  // Raised by activateIndex(), consumed by buildScreen() — see rebuildRows().
  bool rebuildPending_ = true;
  BatteryLog::Usage usage_{};
  std::vector<std::string> rowLabels_;
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> rowItems_;
};
