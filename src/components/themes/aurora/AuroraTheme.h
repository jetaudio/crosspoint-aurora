#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

namespace AuroraMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = BaseMetrics::values;
  // Aurora draws its own one-line status/title bar (drawHeaderBar), not the
  // base two-row header (status strip + title row) the 84px default is sized
  // for. Inheriting that height pushes every list ~40px below its divider.
  v.headerHeight = 45;
  // Aurora draws the whole home screen itself, so it loads several recent books:
  // index 0 is the "Now Reading" featured card, the rest form the library list.
  v.homeRecentBooksCount = 6;
  v.homeContinueReadingInMenu = false;  // featured card is the continue-reading affordance
  // Featured "Now Reading" cover is drawn large (~160x240, Lyra-style). Generate/
  // cache the thumbnail at that height so drawBitmap blits it ~1:1 instead of
  // crushing a 400px image down (nearest-neighbor downscale looked garbled).
  v.homeCoverHeight = 225;
  // Recent-book cards draw a small cover; generate that thumbnail at its display height so
  // drawBitmap blits it ~1:1 (downscaling the big 225px thumbnail renders small covers black).
  // Keep this equal to the card cover height used in AuroraTheme.cpp (drawHomeScreen): it must
  // be <= kCardHeight (76) and paired with kCardCoverW for the ~2:3 cover aspect.
  v.homeCardCoverHeight = 72;
  // The NotoSans UI font is taller than the old Ubuntu one; give subtitle rows
  // (bookmarks, recent books, ...) a little more height so title + subtitle fit.
  v.listWithSubtitleRowHeight = 56;
  // Slim arrow edge-hints (drawSideButtonHints): a narrower strip than the base
  // theme's 30px text boxes, so less of the page is reserved on the right.
  v.sideButtonHintsWidth = 24;
  // Aurora is a 1-bit look: dithered grays turn to muddy texture on the T5S3's
  // etched matte glass, so every shared surface (tab pills, popups, sliders)
  // uses solid black or outlines instead.
  v.oneBitChrome = true;
  // iOS control-center shape: round tiles / step buttons, a soft sheet edge,
  // and a full-stadium capsule slider (255 = radius of half the control).
  v.controlRadius = 18;
  v.sheetRadius = 22;
  v.capsuleRadius = 255;
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
                      int activeTab) const override;

  int bottomBarHeight() const override;
  void drawBottomBar(GfxRenderer& renderer, Rect barRect, const std::vector<std::string>& labels,
                     const std::vector<UIIcon>& icons, int activeTab) const override;

  // Settings deliberately keeps CrossPoint's stock category-tab layout (the
  // curated flat variant hid too much behind its Advanced page and duplicated
  // rows); Aurora only adds the home dock beneath it. drawSettingsScreen and
  // the flat SettingsActivity branch stay dormant behind this false.
  bool ownsSettingsLayout() const override { return false; }
  void drawSettingsScreen(GfxRenderer& renderer, Rect content, const char* title,
                          const std::vector<SettingsListItem>& items) const override;

  bool ownsReaderChrome() const override { return true; }
  void drawReaderToolbar(GfxRenderer& renderer, Rect screen, const ReaderToolbarInfo& info) const override;
  void drawReaderPanel(GfxRenderer& renderer, Rect screen, const char* title, int itemCount, int selectedIndex,
                       const std::function<std::string(int)>& rowText,
                       const std::function<std::string(int)>& rowValue = nullptr, int activeTool = -1) const override;

  // Tap-target geometry mirroring drawHomeScreen / drawReaderToolbar /
  // drawReaderPanel — keep in sync with those draw functions.
  HomeHitLayout homeHitLayout(const GfxRenderer& renderer) const override;
  int settingsItemAt(const GfxRenderer& renderer, Rect content, const std::vector<SettingsListItem>& items, int x,
                     int y) const override;
  ReaderToolbarHit readerToolbarHitAreas(const GfxRenderer& renderer) const override;
  ReaderPanelHit readerPanelHitAreas(const GfxRenderer& renderer) const override;

  // Aurora restyle of the shared list/header primitives so every screen that uses
  // them (reader menu, TOC, file browser, recent books, ...) gets the Aurora look:
  // a status-bar style header and rounded light-gray selected rows.
  // backButton is ignored: Aurora sub-screens title their band "‹ Parent",
  // which is itself the tap-to-go-back target (Activity::backHeader).
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool backButton = true) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle = nullptr,
                const std::function<UIIcon(int index)>& rowIcon = nullptr,
                const std::function<std::string(int index)>& rowValue = nullptr, bool highlightValue = false,
                const std::function<bool(int index)>& rowDimmed = nullptr) const override;

  // Front button-hint row, identical to the base theme but with the label font one
  // step smaller (SMALL instead of UI_10) so the hints read lighter under Aurora.
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;

  // Slim edge-button hints: two small boxes on the right edge (X4) / sides (X3),
  // each holding an up/down arrow glyph instead of rotated "Up"/"Down" text.
  void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const override;

 private:
  // Draws the shared Aurora status bar — page title (left, bold), optional clock
  // (X3 only), battery (right) and the divider rule beneath — into the band whose
  // top edge is `top`, spanning [x, x + width). The title sits kStatusY below the
  // band top and the divider kDividerGap below the title, inset by the content side
  // padding. Returns the divider's Y so callers can lay out the page body below it.
  // Every page (home, settings, file browser, recent books, ...) routes through
  // this so the separator's length and spacing are identical across screens.
  int drawHeaderBar(const GfxRenderer& renderer, int x, int top, int width, const char* title) const;
};
