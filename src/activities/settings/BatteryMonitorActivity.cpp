#include "BatteryMonitorActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

// Attribution coefficients.
//
// Nothing on this board can measure a single component, so these are not
// measurements this screen makes: they come from the bench probe (CMD:PWRPROF)
// and from the least-squares fit in scripts/analyze_battery_log.py over real
// logs. Two of them are well conditioned and the rest are educated — which is
// exactly why the residual row exists. If these were quietly scaled to make the
// columns sum to the measured total, a wrong coefficient would look like a
// correct one forever.
constexpr float SYSTEM_AWAKE_MA = 40.0f;       // CPU/system floor + the always-on GT911
constexpr float LIGHT_SLEEP_MA = 13.0f;        // fitted; the best-conditioned term in the model
constexpr float DEEP_SLEEP_MA = 1.2f;          // best clean sleep measured on this pack
constexpr float CPU_BOOST_EXTRA_MA = 12.0f;    // 240 MHz over the 80 MHz idle clock
constexpr float FRONTLIGHT_MA_PER_PCT = 1.0f;  // 2 mA at 2%
constexpr float WIFI_MA = 80.0f;               // radio up, not necessarily transmitting
constexpr float MAH_PER_REFRESH = 0.028f;      // fitted, +-0.009
// A sleep that stalls on the power-button poll leaves the CPU idling with every
// peripheral already down. Costed separately from "awake" because it is a fault,
// not use, and a reader looking at this screen should see it named.
constexpr float SLEEP_STALL_MA = 25.0f;

float mahFromMs(const uint32_t ms, const float mA) { return static_cast<float>(ms) / 3600000.0f * mA; }

std::string formatDuration(const uint32_t seconds) {
  char buf[32];
  const uint32_t h = seconds / 3600;
  const uint32_t m = (seconds % 3600) / 60;
  if (h > 0) {
    snprintf(buf, sizeof(buf), "%luh %02lum", static_cast<unsigned long>(h), static_cast<unsigned long>(m));
  } else {
    snprintf(buf, sizeof(buf), "%lum", static_cast<unsigned long>(m));
  }
  return buf;
}

// One bucket of the estimated split.
struct Bucket {
  StrId label;
  float mah;
};

}  // namespace

BatteryMonitorActivity::BatteryMonitorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("BatteryMonitor", renderer, mappedInput) {}

void BatteryMonitorActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildPending_ = true;
}

const char* BatteryMonitorActivity::headerTitle() const { return backHeader(StrId::STR_CAT_SYSTEM); }

void BatteryMonitorActivity::addRow(const char* label, std::string value, const bool isHeader) {
  rowLabels_.emplace_back(label);
  rowValues_.push_back(std::move(value));
  fui::ListItem item;
  item.actionValue = static_cast<int16_t>(rowItems_.size());
  item.isHeader = isHeader;
  item.enabled = !isHeader;
  rowItems_.push_back(item);
}

void BatteryMonitorActivity::rebuildRows() {
  usage_ = BatteryLog::usage();
  rowLabels_.clear();
  rowValues_.clear();
  rowItems_.clear();

  addRow(tr(STR_BATT_SINCE_CHARGE), "", /*isHeader=*/true);

  if (!usage_.valid || usage_.onCharger) {
    // On the cable the span is pinned to zero by design, so there is nothing to
    // report yet. Say that rather than showing a table of noughts.
    addRow(tr(STR_BATT_NO_DATA), "");
    return;
  }

  // The one measured number on this screen. Everything below is derived.
  const int usedMah = static_cast<int>(usage_.startRemCapMah) - static_cast<int>(usage_.remCapMah);
  const float used = usedMah > 0 ? static_cast<float>(usedMah) : 0.0f;
  char buf[48];

  if (usage_.fccMah > 0) {
    snprintf(buf, sizeof(buf), "%d mAh  %d%%", usedMah, static_cast<int>(used * 100.0f / usage_.fccMah + 0.5f));
  } else {
    snprintf(buf, sizeof(buf), "%d mAh", usedMah);
  }
  addRow(tr(STR_BATT_USED), buf);
  addRow(tr(STR_BATT_ELAPSED), formatDuration(usage_.elapsedS));

  if (usage_.elapsedS > 60) {
    snprintf(buf, sizeof(buf), "%.1f mA", used * 3600.0f / static_cast<float>(usage_.elapsedS));
    addRow(tr(STR_BATT_AVERAGE), buf);
  }

  // The CPU is halted during light sleep, so those milliseconds are not awake
  // time even though the wall clock ran through them. Subtracting keeps the two
  // rates from being charged for the same span.
  const uint32_t awakeMs = usage_.runMs > usage_.lightSleepMs ? usage_.runMs - usage_.lightSleepMs : 0;

  Bucket buckets[] = {
      {StrId::STR_BATT_DEEP_SLEEP, mahFromMs(usage_.deepSleepMs, DEEP_SLEEP_MA)},
      {StrId::STR_BATT_AWAKE, mahFromMs(awakeMs, SYSTEM_AWAKE_MA)},
      {StrId::STR_BATT_LIGHT_SLEEP, mahFromMs(usage_.lightSleepMs, LIGHT_SLEEP_MA)},
      {StrId::STR_BATT_LIGHT, mahFromMs(usage_.frontlightPctMs, FRONTLIGHT_MA_PER_PCT)},
      {StrId::STR_BATT_CPU_BOOST, mahFromMs(usage_.hiClockMs, CPU_BOOST_EXTRA_MA)},
      {StrId::STR_BATT_WIFI_USE, mahFromMs(usage_.wifiMs, WIFI_MA)},
      {StrId::STR_BATT_SCREEN_REFRESH, static_cast<float>(usage_.refreshes) * MAH_PER_REFRESH},
      {StrId::STR_BATT_SLEEP_STALL, mahFromMs(usage_.sleepStallMs, SLEEP_STALL_MA)},
  };

  float attributed = 0.0f;
  for (const auto& b : buckets) attributed += b.mah;

  addRow(tr(STR_BATT_ESTIMATE_NOTE), "", /*isHeader=*/true);

  // Largest first: the point of the screen is which load dominates, and that
  // question should not need reading the whole list.
  std::sort(std::begin(buckets), std::end(buckets), [](const Bucket& a, const Bucket& b) { return a.mah > b.mah; });

  const auto share = [&](const float mah) {
    if (used <= 0.0f) return std::string();
    snprintf(buf, sizeof(buf), "%.0f mAh  %d%%", mah, static_cast<int>(mah * 100.0f / used + 0.5f));
    return std::string(buf);
  };

  for (const auto& b : buckets) {
    // Below half a milliamp-hour a row says nothing but "this was not it".
    if (b.mah < 0.5f) continue;
    addRow(I18N.get(b.label), share(b.mah));
  }

  // Signed on purpose. Positive means something drew charge that none of the
  // coefficients above knows about — which is precisely the shape of the fault
  // this instrumentation went in to find. Negative means the coefficients are
  // too generous for how this pack is actually being used.
  const float residual = used - attributed;
  snprintf(buf, sizeof(buf), "%+.0f mAh", residual);
  addRow(tr(STR_BATT_UNACCOUNTED), buf);

  if (usage_.sleepStalls > 0) {
    snprintf(buf, sizeof(buf), "%lu x", static_cast<unsigned long>(usage_.sleepStalls));
    addRow(tr(STR_BATT_SLEEP_STALL), buf);
  }
  if (!usage_.lastParkOk) {
    addRow(tr(STR_BATT_PARK_FAILED), "");
  }
}

void BatteryMonitorActivity::activateIndex(const int index) {
  (void)index;
  // Nothing here is settable; Select re-reads the gauge instead, which is the
  // only interaction the screen has any use for. The re-read itself happens on
  // the render task (see rebuildRows()); this only asks for it.
  app.clearTapFlash();
  rebuildPending_ = true;
  requestUpdate();
}

void BatteryMonitorActivity::buildScreen(UiScreen& screen) {
  if (rebuildPending_) {
    rebuildPending_ = false;
    rebuildRows();
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Pointers are re-bound on every build rather than in rebuildRows(): a
  // std::string that grows reallocates, and the list holds these as raw char*.
  for (size_t i = 0; i < rowItems_.size(); i++) {
    rowItems_[i].label = rowLabels_[i].c_str();
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;  // also the explicitly-set marker, see SettingsActivity
  syncListViewport(screen, props);
  screen.list(props);
}
