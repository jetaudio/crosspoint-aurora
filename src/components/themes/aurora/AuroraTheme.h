#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

namespace AuroraMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = BaseMetrics::values;
  // Aurora draws the whole home screen itself, so it loads several recent books:
  // index 0 is the "Now Reading" featured card, the rest form the library list.
  v.homeRecentBooksCount = 6;
  v.homeContinueReadingInMenu = false;  // featured card is the continue-reading affordance
  // Featured cover is drawn small (~100x150). Generate/cache the thumbnail at that
  // height so drawBitmap blits it ~1:1 instead of crushing a 400px image down
  // (nearest-neighbor downscale looked garbled).
  v.homeCoverHeight = 150;
  return v;
}();
}  // namespace AuroraMetrics

// Aurora theme: a redesigned home screen with a slim status bar, a "Now Reading"
// featured card, and a "Library" list of recent books. All other screens fall
// back to the Classic (BaseTheme) rendering.
class AuroraTheme : public BaseTheme {
 public:
  bool ownsHomeLayout() const override { return true; }
  void drawHomeScreen(GfxRenderer& renderer, Rect content, const std::vector<RecentBook>& recentBooks,
                      const std::vector<std::string>& barLabels, const std::vector<UIIcon>& barIcons, int listSelected,
                      int barSelected) const override;

  bool ownsSettingsLayout() const override { return true; }
  void drawSettingsScreen(GfxRenderer& renderer, Rect content, const std::vector<std::string>& categories,
                          int activeCategory, const std::vector<std::string>& names,
                          const std::vector<std::string>& values, int selectedIndex) const override;
};
