#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class GfxRenderer;
struct RecentBook;

struct Rect {
  int x;
  int y;
  int width;
  int height;

  explicit Rect(int x = 0, int y = 0, int width = 0, int height = 0) : x(x), y(y), width(width), height(height) {}
};

struct TabInfo {
  const char* label;
  bool selected;
};

// One entry in a sectioned settings list (see drawSettingsScreen). A header row
// renders a small uppercase section label; a value row renders name + value
// (+ optional ▸ chevron) and may be the selected row.
struct SettingsListItem {
  bool isHeader = false;
  std::string text;          // section label (header) or setting name (row)
  std::string value;         // setting value text (row only; empty for actions)
  bool selected = false;     // row is currently selected
  bool showChevron = false;  // draw a ▸ affordance (actions / submenu entries)
};

// Data the reader passes to drawReaderToolbar() — the Aurora overlay shown on
// Select while reading. Chapter page/percent are what the reader already tracks
// (chapter-relative page X/Y + whole-book percent); there is no book-level page
// numbering. focusedTool is 0=Contents, 1=Text, 2=More.
struct ReaderToolbarInfo {
  const char* bookTitle = nullptr;
  const char* chapterTitle = nullptr;
  int chapterPage = 0;       // 1-based page within the current chapter
  int chapterPageCount = 0;  // total pages in the current chapter
  int bookPercent = 0;       // 0..100 progress through the whole book
  float progress = 0.0f;     // 0..1 position for the scrub knob
  int focusedTool = 0;       // 0=Contents, 1=Text, 2=More
  bool focusReadingOn = false;
};

// Tap-target geometry for theme-owned screens. The themes that own a layout
// (Aurora) draw it outside FreeInkUI, so there are no registered hit rects;
// these structs mirror the draw math so activities can hit-test taps. Every
// rect is in logical screen coordinates. `valid` stays false on themes that
// don't own the corresponding layout.
struct HomeHitLayout {
  int featuredTop = 0;  // y-band of the Continue Reading card (full width)
  int featuredBottom = 0;
  int listTop = 0;  // first library card row
  int cardStride = 0;
  int cardHeight = 0;
  int pageItems = 0;  // library cards per page
  int barTop = 0;     // bottom tab bar top edge
  bool valid = false;
};

struct ReaderToolbarHit {
  // Rect's constructor is explicit, so spell the member defaults out (a bare
  // {} would be copy-list-initialization, which explicit forbids).
  Rect topBar = Rect();                      // tap = dismiss (the back chevron lives here)
  Rect prevBtn = Rect();                     // scrub row: previous chapter
  Rect nextBtn = Rect();                     // scrub row: next chapter
  Rect track = Rect();                       // scrub row: progress track (tap = jump to fraction)
  Rect tools[3] = {Rect(), Rect(), Rect()};  // Contents / Text / More
  int bottomTop = 0;                         // bottom bar top edge; taps between topBar and here dismiss
  bool valid = false;
};

struct ReaderPanelHit {
  int panelTop = 0;  // bottom-sheet top edge; taps above dismiss to the toolbar
  int listTop = 0;   // first row y
  int rowHeight = 0;
  int pageItems = 0;
  // The sheet's own Contents/Text/More switcher row (tap to jump panels).
  Rect tools[3] = {Rect(), Rect(), Rect()};
  bool valid = false;
};

struct ThemeMetrics {
  int batteryWidth;
  int batteryHeight;

  int topPadding;
  int batteryBarHeight;
  int headerHeight;
  int verticalSpacing;

  int previewPadding;
  int previewHeightPercent;

  int contentSidePadding;
  int listRowHeight;
  int listWithSubtitleRowHeight;
  // FreeInkUI list shape, consumed by uiThemeTokens() for screens rendered
  // through FreeInkApp: the theme supplies geometry and selection style, the
  // uiScale fonts supply the sizes. Plain data by design — the eventual
  // SD-card theme files will provide exactly these values.
  int listRowGap;          // vertical gap between rows
  int listRowRadius;       // row corner radius (RoundedRaff cards, Lyra pill)
  int listInset;           // horizontal inset of the whole list band
  int listSidePadding;     // text inset within a row
  int listSelectionStyle;  // 0=invert fill, 1=light pill, 2=underline, 3=triangle (fui::SelectionStyle order)
  int listScrollWidth;     // scroll indicator thickness
  int listScrollSide;      // 0 = right edge, 1 = left edge
  bool listTitleBold;      // bold row titles (RoundedRaff)
  // FreeInkUI header shape, same contract as the list fields above.
  int headerSidePadding;    // title text inset
  int headerUnderlineSize;  // bottom rule thickness (Lyra), 0 = none
  int headerTitleAlign;     // 0 = left, 1 = center, 2 = right (fui::TextAlign order)
  int headerBatterySide;    // 0 = right edge, 1 = left edge
  // Battery in its own corner strip (batteryBarHeight tall) with the title on
  // the lower sub-band spanning the full width (Lyra), vs sharing the title
  // line with a width reserve (Classic, RoundedRaff).
  bool headerBatteryDetached;
  int menuRowHeight;
  int menuSpacing;

  int tabSpacing;
  int tabBarHeight;
  // Selected-tab pill fills its equal-width slot (legacy RoundedRaff tabs)
  // instead of shrinking to hug the label (legacy Lyra tabs).
  bool tabPillFullSlot = false;
  // Pure 1-bit chrome: no dithered-gray washes, pills, or dim layers anywhere.
  // Aurora sets this — dithered grays read as muddy texture on the T5S3's matte
  // panel, so selection is carried by solid black / outlines instead.
  bool oneBitChrome = false;

  int scrollBarWidth;
  int scrollBarRightOffset;

  int homeTopPadding;
  int homeCoverHeight;
  int homeCoverTileHeight;
  // Height for the small per-book cover thumbnails a card-style home list draws (0 = the
  // theme has no separate card covers, so only homeCoverHeight thumbnails are generated).
  // Generating the thumbnail at its display size lets drawBitmap blit it ~1:1; downscaling a
  // large 1-bit thumbnail instead fills small covers solid black (drawBitmap1Bit has no
  // area-averaging downscaler).
  int homeCardCoverHeight;
  int homeRecentBooksCount;
  bool homeContinueReadingInMenu;
  int homeMenuTopOffset;

  int buttonHintsHeight;
  int sideButtonHintsWidth;

  int progressBarHeight;
  int progressBarMarginTop;
  int statusBarHorizontalMargin;
  int statusBarVerticalMargin;
  int keyboardKeyHeight;
  int keyboardKeySpacing;
  bool keyboardCenteredText;
  int keyboardVerticalOffset;
  int keyboardTextFieldWidthPercent;
  int keyboardWidthPercent;

  float popupTopOffsetRatio;
  int popupMarginX;
  int popupMarginY;
  int popupFrameThickness;
  int popupCornerRadius;
  bool popupTextBold;
  bool popupTextInverted;
  int popupTextBaselineOffsetY;
  int popupProgressBarHeight;
  bool popupProgressDrawOutline;
  bool popupProgressClampPercent;
  bool popupProgressFillInverted;
  bool popupProgressOutlineInverted;

  int optionPopupItemSpacing;
  int optionPopupInnerPadding;
  int optionPopupSelectionVPadding;
  int optionPopupDialogSideMargin;

  int textFieldHorizontalPadding;
  int textFieldNormalThickness;
  int textFieldCursorThickness;
  int textFieldLineEndOffset;

  // FreeInkUI control shape (the control center panel), same contract as the
  // list fields above: quick-setting tiles and slider step buttons, the
  // sheet's free-edge corners, and the capsule slider's corners (255 = full
  // stadium, i.e. radius = half the control height).
  int controlRadius;
  int sheetRadius;
  int capsuleRadius;
};

enum UIIcon {
  None = 0,
  Folder,
  Text,
  Image,
  Book,
  File,
  Recent,
  Settings,
  Transfer,
  Library,
  Wifi,
  Hotspot,
  Bookmark,
  Usb,
  Blocks
};

// Default theme implementation (Classic Theme)
// Additional themes can inherit from this and override methods as needed

namespace BaseMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 15,
                                 .batteryHeight = 12,
                                 .topPadding = 5,
                                 .batteryBarHeight = 20,
                                 .headerHeight = 45,
                                 .verticalSpacing = 10,
                                 .previewPadding = 12,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 20,
                                 .listRowHeight = 30,
                                 .listWithSubtitleRowHeight = 50,
                                 .listRowGap = 0,
                                 .listRowRadius = 0,
                                 .listInset = 0,
                                 .listSidePadding = 20,
                                 .listSelectionStyle = 0,  // invert fill
                                 .listScrollWidth = 4,
                                 .listScrollSide = 0,
                                 .listTitleBold = false,
                                 .headerSidePadding = 18,
                                 .headerUnderlineSize = 0,
                                 .headerTitleAlign = 1,  // centered
                                 .headerBatterySide = 0,
                                 .headerBatteryDetached = false,
                                 .menuRowHeight = 45,
                                 .menuSpacing = 8,
                                 .tabSpacing = 10,
                                 .tabBarHeight = 50,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 40,
                                 .homeCoverHeight = 400,
                                 .homeCoverTileHeight = 400,
                                 .homeCardCoverHeight = 0,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = 10,
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyHeight = 48,
                                 .keyboardKeySpacing = 0,
                                 .keyboardCenteredText = false,
                                 .keyboardVerticalOffset = -13,
                                 .keyboardTextFieldWidthPercent = 85,
                                 .keyboardWidthPercent = 94,
                                 .popupTopOffsetRatio = 0.075f,
                                 .popupMarginX = 15,
                                 .popupMarginY = 15,
                                 .popupFrameThickness = 2,
                                 .popupCornerRadius = 0,
                                 .popupTextBold = true,
                                 .popupTextInverted = true,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 4,
                                 .popupProgressDrawOutline = false,
                                 .popupProgressClampPercent = false,
                                 .popupProgressFillInverted = true,
                                 .popupProgressOutlineInverted = true,
                                 .optionPopupItemSpacing = 6,
                                 .optionPopupInnerPadding = 16,
                                 .optionPopupSelectionVPadding = 4,
                                 .optionPopupDialogSideMargin = 20,
                                 .textFieldHorizontalPadding = 6,
                                 .textFieldNormalThickness = 1,
                                 .textFieldCursorThickness = 3,
                                 .textFieldLineEndOffset = 0,
                                 .controlRadius = 0,
                                 .sheetRadius = 0,
                                 .capsuleRadius = 0};
}

class BaseTheme {
 public:
  virtual ~BaseTheme() = default;

  // Component drawing methods
  static void drawProgressBar(const GfxRenderer& renderer, Rect rect, size_t current, size_t total);
  void drawBatteryLeft(const GfxRenderer& renderer, Rect rect,
                       bool showPercentage = true) const;  // Left aligned (reader mode)
  // Right aligned (UI headers). develop's own headers draw the battery through
  // fui::batteryIndicator; Aurora paints its slim status row directly, so it still
  // needs this pair.
  void drawBatteryRight(const GfxRenderer& renderer, Rect rect, bool showPercentage = true) const;
  virtual void fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const;
  virtual void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                               const char* btn4) const;
  // Shared by every theme's drawButtonHints(): centres a hint label in its box,
  // wrapping to two lines rather than overflowing when it's too wide to fit.
  static void drawHintLabel(const GfxRenderer& renderer, int fontId, const char* label, int x, int boxWidth, int boxTop,
                            int boxHeight, int singleLineYOffset);
  virtual void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const;
  // Menu row height as DRAWN by drawButtonMenu. HomeActivity builds its touch
  // grid from this, so hit bands always match the visuals (RoundedRaff derives
  // its row height from the font, not the metrics table).
  virtual int getMenuRowHeight(const GfxRenderer& renderer) const;
  virtual int getListRowStep(bool hasSubtitle) const;
  virtual int getListPageItems(int contentHeight, bool hasSubtitle) const;
  // The legacy full-list painter. Upstream migrated every list screen to
  // FreeInkUI and dropped this; aurora's font pickers (FontSelectionActivity,
  // DropCapFontSelectionActivity) still draw through the theme, and AuroraTheme
  // overrides it, so the base implementation stays until those are ported.
  virtual void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                        const std::function<std::string(int index)>& rowTitle,
                        const std::function<std::string(int index)>& rowSubtitle = nullptr,
                        const std::function<UIIcon(int index)>& rowIcon = nullptr,
                        const std::function<std::string(int index)>& rowValue = nullptr, bool highlightValue = false,
                        const std::function<bool(int index)>& rowDimmed = nullptr) const;
  virtual void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                          const char* subtitle = nullptr) const;
  virtual void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                             const char* rightLabel = nullptr) const;
  virtual void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                   const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                   bool& bufferRestored, std::function<bool()> storeCoverBuffer) const;
  // Opt-in hook for themes that render the entire home screen themselves (status
  // bar + featured card + library list) instead of the default
  // header/cover/button-menu layout driven by HomeActivity. Default themes return
  // false and keep the legacy path untouched.
  virtual bool ownsHomeLayout() const { return false; }
  // Draws the whole home screen: a "Now Reading" featured card, a library list of
  // recent books, and the persistent bottom navigation bar (barLabels/barIcons).
  // listSelected indexes the vertical content list (0 = featured card, 1.. =
  // library rows; -1 if nothing is selected). activeTab is the bottom-bar tab to
  // highlight (the Library tab when on Home).
  virtual void drawHomeScreen(GfxRenderer&, Rect, const std::vector<RecentBook>&, const std::vector<std::string>&,
                              const std::vector<UIIcon>&, int /*listSelected*/, int /*activeTab*/) const {}

  // Height (px) of the persistent bottom navigation bar, or 0 for themes that
  // don't draw one. Activities reserve this much at the bottom of their content.
  virtual int bottomBarHeight() const { return 0; }
  // Draws the persistent bottom navigation bar inside barRect: one icon+label
  // slot per tab, with a highlight on activeTab. No-op for themes without a bar.
  virtual void drawBottomBar(GfxRenderer&, Rect /*barRect*/, const std::vector<std::string>& /*labels*/,
                             const std::vector<UIIcon>& /*icons*/, int /*activeTab*/) const {}

  // Opt-in hook for themes that render the whole Settings screen themselves
  // (status bar + sectioned value list). Default themes keep the legacy
  // header/tabbar/list layout.
  virtual bool ownsSettingsLayout() const { return false; }
  // Draws a settings screen as a flat, section-grouped list (no category tabs).
  // `title` is the status-bar title; `items` is the interleaved list of section
  // headers and value rows in display order.
  virtual void drawSettingsScreen(GfxRenderer&, Rect, const char* /*title*/,
                                  const std::vector<SettingsListItem>& /*items*/) const {}

  // Opt-in hook for themes that own the reader chrome (Aurora): a clean reading
  // page (no persistent bottom status bar) plus the two-bar toolbar overlay drawn
  // by drawReaderToolbar(). Default themes return false and keep the classic
  // status bar + full-screen list menu.
  virtual bool ownsReaderChrome() const { return false; }
  // Draws the reader toolbar overlay (top bar + bottom scrub/meta/tool rows) on
  // top of the already-rendered page. `screen` is the full screen rect.
  virtual void drawReaderToolbar(GfxRenderer&, Rect /*screen*/, const ReaderToolbarInfo& /*info*/) const {}
  // Draws a reader overlay panel (a bottom sheet with a title and a scrollable
  // item list) over the page — the Aurora reader's Contents/Text/More tools.
  // activeTool highlights the panel's own Contents/Text/More switcher row
  // (0/1/2); pass -1 on themes/boards without one.
  virtual void drawReaderPanel(GfxRenderer&, Rect /*screen*/, const char* /*title*/, int /*itemCount*/,
                               int /*selectedIndex*/, const std::function<std::string(int)>& /*rowText*/,
                               const std::function<std::string(int)>& /*rowValue*/ = nullptr,
                               int /*activeTool*/ = -1) const {}
  // Tap-target geometry mirroring the theme-owned layouts above (see the
  // structs' comments). Themes that own the layout must keep these in sync
  // with their draw functions.
  virtual HomeHitLayout homeHitLayout(const GfxRenderer&) const { return {}; }
  // Item index (into `items`) under logical point (x, y) on the theme-owned
  // settings screen, or -1. Only non-header rows hit. `content` must be the
  // same rect the caller passes to drawSettingsScreen.
  virtual int settingsItemAt(const GfxRenderer&, Rect /*content*/, const std::vector<SettingsListItem>& /*items*/,
                             int /*x*/, int /*y*/) const {
    return -1;
  }
  virtual ReaderToolbarHit readerToolbarHitAreas(const GfxRenderer&) const { return {}; }
  virtual ReaderPanelHit readerPanelHitAreas(const GfxRenderer&) const { return {}; }
  virtual void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                              const std::function<std::string(int index)>& buttonLabel,
                              const std::function<UIIcon(int index)>& rowIcon) const;
  virtual Rect drawPopup(const GfxRenderer& renderer, const char* message) const;
  virtual void fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const;
  static void drawStatusBar(GfxRenderer& renderer, const float bookProgress, const int currentPage, const int pageCount,
                            std::string title, const int paddingBottom = 0, const int textYOffset = 0,
                            const bool fillMargin = true, const bool isPageBookmarked = false,
                            const bool pageCountEstimated = false);
  static void drawHelpText(const GfxRenderer& renderer, Rect rect, const char* label);
  virtual void drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode = false,
                             int contentStartX = 0, int contentWidth = 0) const;
  virtual bool showsFileIcons() const { return false; }

  // Shared constants and helpers for battery drawing (used by all themes)
  static constexpr int batteryPercentSpacing = 4;
  static void drawBatteryOutline(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight);
  static void drawBatteryLightningBolt(const GfxRenderer& renderer, int boltX, int boltY);
};
