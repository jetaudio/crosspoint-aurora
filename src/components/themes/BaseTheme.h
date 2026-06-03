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
  int menuRowHeight;
  int menuSpacing;

  int tabSpacing;
  int tabBarHeight;

  int scrollBarWidth;
  int scrollBarRightOffset;

  int homeTopPadding;
  int homeCoverHeight;
  int homeCoverTileHeight;
  int homeRecentBooksCount;
  bool homeContinueReadingInMenu;
  int homeMenuTopOffset;

  int buttonHintsHeight;
  int sideButtonHintsWidth;

  int progressBarHeight;
  int progressBarMarginTop;
  int statusBarHorizontalMargin;
  int statusBarVerticalMargin;

  int keyboardKeyWidth;
  int keyboardKeyHeight;
  int keyboardKeySpacing;
  int keyboardBottomKeyHeight;
  int keyboardBottomKeySpacing;
  bool keyboardBottomAligned;
  bool keyboardCenteredText;
  int keyboardVerticalOffset;
  int keyboardTextFieldWidthPercent;
  int keyboardWidthPercent;
  int keyboardKeyCornerRadius;
  bool keyboardFillUnselected;
  bool keyboardOutlineAllUnselected;
  bool keyboardDrawSpecialOutlineWhenUnselected;
  int keyboardSecondaryLabelRightPadding;
  int keyboardSecondaryLabelTopPadding;
  int keyboardMinArrowHeadSize;

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

  int textFieldHorizontalPadding;
  int textFieldNormalThickness;
  int textFieldCursorThickness;
  int textFieldLineEndOffset;
};

enum UIIcon { None = 0, Folder, Text, Image, Book, File, Recent, Settings, Transfer, Library, Wifi, Hotspot, Bookmark };

enum class KeyboardKeyType { Normal, Shift, Mode, Space, Del, Ok, Disabled };

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
                                 .menuRowHeight = 45,
                                 .menuSpacing = 8,
                                 .tabSpacing = 10,
                                 .tabBarHeight = 50,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 40,
                                 .homeCoverHeight = 400,
                                 .homeCoverTileHeight = 400,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = 10,
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyWidth = 22,
                                 .keyboardKeyHeight = 40,
                                 .keyboardKeySpacing = 0,
                                 .keyboardBottomKeyHeight = 35,
                                 .keyboardBottomKeySpacing = 5,
                                 .keyboardBottomAligned = true,
                                 .keyboardCenteredText = false,
                                 .keyboardVerticalOffset = -13,
                                 .keyboardTextFieldWidthPercent = 85,
                                 .keyboardWidthPercent = 90,
                                 .keyboardKeyCornerRadius = 0,
                                 .keyboardFillUnselected = false,
                                 .keyboardOutlineAllUnselected = false,
                                 .keyboardDrawSpecialOutlineWhenUnselected = true,
                                 .keyboardSecondaryLabelRightPadding = 1,
                                 .keyboardSecondaryLabelTopPadding = 0,
                                 .keyboardMinArrowHeadSize = 0,
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
                                 .textFieldHorizontalPadding = 6,
                                 .textFieldNormalThickness = 1,
                                 .textFieldCursorThickness = 3,
                                 .textFieldLineEndOffset = 0};
}

class BaseTheme {
 public:
  virtual ~BaseTheme() = default;

  // Component drawing methods
  void drawProgressBar(const GfxRenderer& renderer, Rect rect, size_t current, size_t total) const;
  void drawBatteryLeft(const GfxRenderer& renderer, Rect rect,
                       bool showPercentage = true) const;  // Left aligned (reader mode)
  void drawBatteryRight(const GfxRenderer& renderer, Rect rect,
                        bool showPercentage = true) const;  // Right aligned (UI headers)
  virtual void fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const;
  virtual void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                               const char* btn4) const;
  virtual void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const;
  virtual int getListPageItems(int contentHeight, bool hasSubtitle) const;
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
  virtual void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                          bool selected) const;
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
  virtual void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                              const std::function<std::string(int index)>& buttonLabel,
                              const std::function<UIIcon(int index)>& rowIcon) const;
  virtual Rect drawPopup(const GfxRenderer& renderer, const char* message) const;
  virtual void fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const;
  void drawStatusBar(GfxRenderer& renderer, const float bookProgress, const int currentPage, const int pageCount,
                     std::string title, const int paddingBottom = 0, const int textYOffset = 0,
                     const bool fillMargin = true) const;
  void drawHelpText(const GfxRenderer& renderer, Rect rect, const char* label) const;
  virtual void drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode = false,
                             int contentStartX = 0, int contentWidth = 0) const;
  virtual void drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label, const bool isSelected,
                               const char* secondaryLabel = nullptr, KeyboardKeyType keyType = KeyboardKeyType::Normal,
                               bool inactiveSelection = false) const;
  virtual bool showsFileIcons() const { return false; }

  // Shared constants and helpers for battery drawing (used by all themes)
  static constexpr int batteryPercentSpacing = 4;
  static void drawBatteryOutline(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight);
  static void drawBatteryLightningBolt(const GfxRenderer& renderer, int boltX, int boltY);
};
