#include "AuroraTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

namespace {
constexpr int kStatusY = 12;       // Status bar text baseline (top of cell)
constexpr int kDividerGap = 26;    // Distance from status text to the divider line
constexpr int kThumbHeight = 240;  // Featured cover thumbnail height (large, Lyra-style)
constexpr int kThumbWidth = 160;   // Featured cover thumbnail width (~2:3)
constexpr int kTextGap = 16;       // Gap between thumbnail and title block
constexpr int kIconSize = 32;      // Placeholder cover glyph / bottom-bar icon size
constexpr int kSectionGap = 28;    // Gap above the "Library" header
constexpr int kBottomBarHeight = 70;
constexpr int kSettingRowHeight = 40;  // Settings value row height
constexpr int kSettingValueCol = 150;  // Reserved width for the right value column
constexpr int kSettingArrow = 7;       // Triangle arrow size for adjustable values
constexpr int kHeaderFontId = UI_10_FONT_ID;
constexpr int kTitleFontId = UI_12_FONT_ID;
constexpr int kBodyFontId = UI_10_FONT_ID;
// Settings rows use UI_10 (not the larger UI_12 of home/list titles) so the screen
// reads compact and refined rather than chunky -- the name matches the value size.
constexpr int kSettingNameFontId = UI_10_FONT_ID;
constexpr int kBarLabelFontId = SMALL_FONT_ID;
constexpr int kCaptionFontId = SMALL_FONT_ID;

// Resolve a UIIcon to its 32px bitmap (mirrors LyraTheme's iconForName for the
// icons the bottom bar uses; keeps Aurora self-contained).
const uint8_t* barIconBitmap(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Library:
      return LibraryIcon;
    default:
      return nullptr;
  }
}
}  // namespace

void AuroraTheme::drawHomeScreen(GfxRenderer& renderer, Rect content, const std::vector<RecentBook>& recentBooks,
                                 const std::vector<std::string>& barLabels, const std::vector<UIIcon>& barIcons,
                                 int listSelected, int activeTab) const {
  const auto& metrics = AuroraMetrics::values;
  const int W = content.width;
  const int P = metrics.contentSidePadding;

  // --- Status bar: product label (left), clock (center, X3 only), battery (right) ---
  renderer.drawText(kCaptionFontId, P, kStatusY, "CrossPoint", true, EpdFontFamily::BOLD);

  if (gpio.deviceIsX3() && SETTINGS.statusBarClock && halClock.isAvailable()) {
    char timeBuf[9];
    if (halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1)) {
      renderer.drawCenteredText(kCaptionFontId, kStatusY, timeBuf);
    }
  }

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = W - P - metrics.batteryWidth;
  drawBatteryRight(renderer, Rect{batteryX, kStatusY, metrics.batteryWidth, metrics.batteryHeight},
                   showBatteryPercentage);

  const int dividerY = kStatusY + kDividerGap;
  renderer.drawLine(P, dividerY, W - P, dividerY);

  // --- "Now Reading" featured card (recentBooks[0]) ---
  const int labelY = dividerY + 14;
  renderer.drawText(kHeaderFontId, P, labelY, tr(STR_NOW_READING), true, EpdFontFamily::BOLD);

  const int thumbY = labelY + renderer.getLineHeight(kHeaderFontId) + 8;
  const bool hasFeatured = !recentBooks.empty();
  const bool featuredSelected = listSelected == 0;

  if (hasFeatured) {
    const RecentBook& book = recentBooks[0];
    const int thumbX = P;

    // Cover thumbnail (load a single small BMP; falls back to a placeholder glyph).
    bool drewCover = false;
    if (!book.coverBmpPath.empty()) {
      const std::string coverBmpPath = UITheme::getCoverThumbPath(book.coverBmpPath, metrics.homeCoverHeight);
      HalFile file;
      if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderer.drawBitmap(bitmap, thumbX, thumbY, kThumbWidth, kThumbHeight);
          drewCover = true;
        }
        file.close();
      }
    }
    if (!drewCover) {
      renderer.fillRect(thumbX, thumbY, kThumbWidth, kThumbHeight, true);
      renderer.drawIcon(CoverIcon, thumbX + (kThumbWidth - kIconSize) / 2, thumbY + (kThumbHeight - kIconSize) / 2,
                        kIconSize, kIconSize);
    }
    renderer.drawRect(thumbX, thumbY, kThumbWidth, kThumbHeight);

    // Title + author block to the right of the cover, vertically centered against
    // the tall cover.
    const int txtX = thumbX + kThumbWidth + kTextGap;
    const int txtW = W - P - txtX;
    const auto titleLines = renderer.wrappedText(kTitleFontId, book.title.c_str(), txtW, 3, EpdFontFamily::BOLD);
    const bool hasAuthor = !book.author.empty();
    int blockHeight = static_cast<int>(titleLines.size()) * renderer.getLineHeight(kTitleFontId);
    if (hasAuthor) blockHeight += renderer.getLineHeight(kBodyFontId) + 6;
    int textY = thumbY + std::max(4, (kThumbHeight - blockHeight) / 2);
    for (const auto& line : titleLines) {
      renderer.drawText(kTitleFontId, txtX, textY, line.c_str(), true, EpdFontFamily::BOLD);
      textY += renderer.getLineHeight(kTitleFontId);
    }
    if (hasAuthor) {
      textY += 6;
      const auto author = renderer.truncatedText(kBodyFontId, book.author.c_str(), txtW);
      renderer.drawText(kBodyFontId, txtX, textY, author.c_str());
    }

    // Selection emphasis around the whole card (double border).
    if (featuredSelected) {
      const int boxX = P - 6;
      const int boxY = thumbY - 6;
      const int boxW = W - 2 * (P - 6);
      const int boxH = kThumbHeight + 12;
      renderer.drawRect(boxX, boxY, boxW, boxH);
      renderer.drawRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2);
    }
  } else {
    const int emptyY = thumbY + (kThumbHeight - renderer.getLineHeight(kTitleFontId)) / 2;
    renderer.drawCenteredText(kTitleFontId, emptyY, tr(STR_NO_OPEN_BOOK));
  }

  // --- "Library" header + recent-books list (books only; nav lives in the bottom bar) ---
  const int libHeaderY = thumbY + kThumbHeight + kSectionGap;
  renderer.drawText(kHeaderFontId, P, libHeaderY, tr(STR_LIBRARY), true, EpdFontFamily::BOLD);
  const int underlineY = libHeaderY + renderer.getLineHeight(kHeaderFontId) + 4;
  renderer.drawLine(P, underlineY, W - P, underlineY);

  const int barTop = content.height - kBottomBarHeight;
  const int listTop = underlineY + 12;
  const int featuredOffset = hasFeatured ? 1 : 0;
  const int libBookCount = std::max(0, static_cast<int>(recentBooks.size()) - featuredOffset);
  // listSelected: 0 = featured card, 1.. = library row; map to the list's own index.
  const int librarySelected = listSelected >= 1 ? listSelected - 1 : -1;
  const int listHeight = std::max(0, barTop - listTop);

  drawList(
      renderer, Rect{0, listTop, W, listHeight}, libBookCount, librarySelected,
      [&recentBooks, featuredOffset](int i) { return recentBooks[featuredOffset + i].title; }, nullptr,
      [](int) -> UIIcon { return Book; });

  // --- Persistent bottom navigation bar (Library tab active on Home) ---
  drawBottomBar(renderer, Rect{0, barTop, W, kBottomBarHeight}, barLabels, barIcons, activeTab);
}

int AuroraTheme::bottomBarHeight() const { return kBottomBarHeight; }

void AuroraTheme::drawBottomBar(GfxRenderer& renderer, Rect barRect, const std::vector<std::string>& labels,
                                const std::vector<UIIcon>& icons, int activeTab) const {
  const int barCount = static_cast<int>(std::min(labels.size(), icons.size()));
  if (barCount <= 0) return;

  const int barTop = barRect.y;
  renderer.drawLine(barRect.x, barTop, barRect.x + barRect.width, barTop);
  const int slotW = barRect.width / barCount;
  for (int i = 0; i < barCount; ++i) {
    const int slotX = barRect.x + i * slotW;
    const int centerX = slotX + slotW / 2;

    // Active tab: light-gray rounded pill (the shared Aurora selection style).
    if (i == activeTab) {
      renderer.fillRoundedRect(slotX + 6, barTop + 5, slotW - 12, barRect.height - 10, 8, Color::LightGray);
    }

    const uint8_t* iconBitmap = barIconBitmap(icons[i]);
    if (iconBitmap != nullptr) {
      renderer.drawIcon(iconBitmap, centerX - kIconSize / 2, barTop + 10, kIconSize, kIconSize);
    }

    const auto style = (i == activeTab) ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const auto label = renderer.truncatedText(kBarLabelFontId, labels[i].c_str(), slotW - 6, style);
    const int labelWidth = renderer.getTextWidth(kBarLabelFontId, label.c_str(), style);
    renderer.drawText(kBarLabelFontId, centerX - labelWidth / 2, barTop + 10 + kIconSize + 2, label.c_str(), true,
                      style);
  }
}

void AuroraTheme::drawSettingsScreen(GfxRenderer& renderer, Rect content, const char* title,
                                     const std::vector<SettingsListItem>& items) const {
  const auto& metrics = AuroraMetrics::values;
  const int W = content.width;
  const int P = metrics.contentSidePadding;

  // --- Status bar: title (left), clock (X3 only), battery (right), divider ---
  renderer.drawText(kCaptionFontId, P, kStatusY, title, true, EpdFontFamily::BOLD);
  if (gpio.deviceIsX3() && SETTINGS.statusBarClock && halClock.isAvailable()) {
    char timeBuf[9];
    if (halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1)) {
      renderer.drawCenteredText(kCaptionFontId, kStatusY, timeBuf);
    }
  }
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  drawBatteryRight(renderer, Rect{W - P - metrics.batteryWidth, kStatusY, metrics.batteryWidth, metrics.batteryHeight},
                   showBatteryPercentage);
  const int dividerY = kStatusY + kDividerGap;
  renderer.drawLine(P, dividerY, W - P, dividerY);

  // --- Flat, section-grouped value list (no category pills) ---
  const int listTop = dividerY + 10;
  const int listBottom = content.y + content.height;
  const int listHeight = std::max(0, listBottom - listTop);
  const int rowH = kSettingRowHeight;
  const int headerH = 28;
  const int rightInset = SETTINGS.showButtonHints ? (metrics.sideButtonHintsWidth + 10) : 0;
  const int rowLeft = P - 4;
  const int rowRight = W - (P - 4) - rightInset;
  const int valueRightX = rowRight - 10;

  const int n = static_cast<int>(items.size());
  auto itemH = [&](int i) { return items[i].isHeader ? headerH : rowH; };

  // Selected row drives which page is visible.
  int selItem = 0;
  for (int i = 0; i < n; ++i) {
    if (items[i].selected) {
      selItem = i;
      break;
    }
  }

  // Partition items into pages that fit listHeight; render the page holding the
  // selection. (Section headers count toward page height, like rows.)
  std::vector<int> pageStarts = {0};
  {
    int acc = 0;
    for (int i = 0; i < n; ++i) {
      const int h = itemH(i);
      if (acc + h > listHeight && i > pageStarts.back()) {
        pageStarts.push_back(i);
        acc = 0;
      }
      acc += h;
    }
  }
  int pageStart = 0;
  int pageEnd = n;
  for (int p = 0; p < static_cast<int>(pageStarts.size()); ++p) {
    const int start = pageStarts[p];
    const int end = (p + 1 < static_cast<int>(pageStarts.size())) ? pageStarts[p + 1] : n;
    if (selItem >= start && selItem < end) {
      pageStart = start;
      pageEnd = end;
      break;
    }
  }

  // Draw one value row at vertical position rowY (inside a group card).
  auto drawRow = [&](const SettingsListItem& it, int rowY) {
    const int cy = rowY + rowH / 2;
    const auto style = it.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    if (it.selected) {
      // Inset so the highlight stays inside the card border.
      renderer.fillRoundedRect(rowLeft + 2, rowY + 2, (rowRight - rowLeft) - 4, rowH - 4, 10, Color::LightGray);
    }

    // Right-aligned ▸ chevron (signals "Select opens/changes this row").
    int contentRightX = valueRightX;
    if (it.showChevron) {
      const int rTip = valueRightX;
      int xr[3] = {rTip, rTip - kSettingArrow, rTip - kSettingArrow};
      int yr[3] = {cy, cy - 5, cy + 5};
      renderer.fillPolygon(xr, yr, 3, true);
      contentRightX = rTip - kSettingArrow - 8;
    }

    // Value (right of the name, left of the chevron).
    if (!it.value.empty()) {
      const auto vTrunc = renderer.truncatedText(kBodyFontId, it.value.c_str(), kSettingValueCol, style);
      const int vw = renderer.getTextWidth(kBodyFontId, vTrunc.c_str(), style);
      const int vy = rowY + (rowH - renderer.getLineHeight(kBodyFontId)) / 2;
      renderer.drawText(kBodyFontId, contentRightX - vw, vy, vTrunc.c_str(), true, style);
      contentRightX -= vw + 10;
    }

    const int nameMaxWidth = std::max(40, contentRightX - (P + 8));
    const auto name = renderer.truncatedText(kSettingNameFontId, it.text.c_str(), nameMaxWidth, style);
    renderer.drawText(kSettingNameFontId, P + 8, rowY + (rowH - renderer.getLineHeight(kSettingNameFontId)) / 2,
                      name.c_str(), true, style);
  };

  // Each group renders as a bold uppercase label above a rounded card that boxes
  // its rows (with thin separators between them), like the reference design.
  int y = listTop;
  int i = pageStart;
  while (i < pageEnd) {
    if (y + itemH(i) > listBottom) break;

    if (items[i].isHeader) {
      renderer.drawText(kCaptionFontId, P + 2, y + 8, items[i].text.c_str(), true, EpdFontFamily::BOLD);
      y += headerH;
      ++i;
      continue;
    }

    const int cardTop = y;
    int rows = 0;
    while (i < pageEnd && !items[i].isHeader && y + rowH <= listBottom) {
      drawRow(items[i], y);
      y += rowH;
      ++i;
      ++rows;
    }
    if (rows > 0) {
      renderer.drawRoundedRect(rowLeft, cardTop, rowRight - rowLeft, rows * rowH, 1, 12, true);
      for (int k = 1; k < rows; ++k) {
        const int sy = cardTop + k * rowH;
        renderer.drawLine(rowLeft + 10, sy, rowRight - 10, sy);
      }
    }
  }
}

void AuroraTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  const auto& m = AuroraMetrics::values;
  const int P = m.contentSidePadding;

  // Battery (right), like the Aurora home/settings status bar.
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  drawBatteryRight(renderer,
                   Rect{rect.x + rect.width - P - m.batteryWidth, rect.y + 8, m.batteryWidth, m.batteryHeight},
                   showBatteryPercentage);

  // Title (left, bold) — Aurora uses a left-aligned status-bar title, not centered.
  if (title != nullptr) {
    const int titleY = rect.y + (rect.height - renderer.getLineHeight(kTitleFontId)) / 2;
    const int rightReserve = P + m.batteryWidth + 52;  // battery icon + "NN%"
    const int maxW = std::max(20, (rect.x + rect.width - rightReserve) - (rect.x + P));
    const auto t = renderer.truncatedText(kTitleFontId, title, maxW, EpdFontFamily::BOLD);
    renderer.drawText(kTitleFontId, rect.x + P, titleY, t.c_str(), true, EpdFontFamily::BOLD);
  }

  // Divider rule at the bottom of the header band.
  renderer.drawLine(rect.x, rect.y + rect.height, rect.x + rect.width, rect.y + rect.height);

  // Optional subtitle (rare): small, right-aligned just under the header.
  if (subtitle != nullptr) {
    const auto s = renderer.truncatedText(SMALL_FONT_ID, subtitle, rect.width - P * 2, EpdFontFamily::REGULAR);
    const int sw = renderer.getTextWidth(SMALL_FONT_ID, s.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - P - sw, rect.y + rect.height + 5, s.c_str(), true);
  }
}

void AuroraTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                           const std::function<std::string(int index)>& rowTitle,
                           const std::function<std::string(int index)>& rowSubtitle,
                           const std::function<UIIcon(int index)>& /*rowIcon*/,
                           const std::function<std::string(int index)>& rowValue, bool /*highlightValue*/,
                           const std::function<bool(int index)>& rowDimmed) const {
  const auto& m = AuroraMetrics::values;
  const int P = m.contentSidePadding;
  const int rowHeight = (rowSubtitle != nullptr) ? m.listWithSubtitleRowHeight : m.listRowHeight;
  const int pageItems = rowHeight > 0 ? rect.height / rowHeight : 0;
  if (pageItems <= 0) return;

  // Page up/down arrows on the right when there is more than one page.
  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    constexpr int arrowSize = 6;
    constexpr int margin = 15;
    const int centerX = rect.x + rect.width - 10 - margin;
    const int indicatorTop = rect.y;
    const int indicatorBottom = rect.y + rect.height - arrowSize;
    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + i * 2;
      renderer.drawLine(centerX - i, indicatorTop + i, centerX - i + lineWidth - 1, indicatorTop + i);
    }
    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + (arrowSize - 1 - i) * 2;
      const int startX = centerX - (arrowSize - 1 - i);
      renderer.drawLine(startX, indicatorBottom - arrowSize + 1 + i, startX + lineWidth - 1,
                        indicatorBottom - arrowSize + 1 + i);
    }
  }

  const int contentWidth = rect.width - 5;
  const int pageStartIndex = (selectedIndex >= 0 ? selectedIndex : 0) / pageItems * pageItems;

  // Aurora selection: a rounded light-gray pill (vs the base theme's inverted bar),
  // so row text stays black instead of inverting.
  if (selectedIndex >= 0) {
    const int selY = rect.y + (selectedIndex % pageItems) * rowHeight - 2;
    renderer.fillRoundedRect(rect.x + P - 6, selY, rect.width - 2 * (P - 6), rowHeight, 10, Color::LightGray);
  }

  constexpr int minValueGap = 10;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; ++i) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;

    int rowTextWidth = contentWidth - P * 2;
    std::string valueText;
    if (rowValue != nullptr) {
      valueText = rowValue(i);
      if (!valueText.empty()) {
        const int maxValW = std::max(0, rowTextWidth - 40 - minValueGap);
        valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), maxValW);
        rowTextWidth -= renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + minValueGap;
      }
    }

    const auto item = renderer.truncatedText(UI_10_FONT_ID, rowTitle(i).c_str(), rowTextWidth);
    renderer.drawText(UI_10_FONT_ID, rect.x + P, itemY, item.c_str(), true);

    // Dimmed rows: checkerboard dither for a gray-text effect.
    if (rowDimmed && rowDimmed(i)) {
      const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, item.c_str());
      const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
      const int tx = rect.x + P;
      for (int py = itemY; py < itemY + lineH; ++py)
        for (int px = tx; px < tx + titleWidth; ++px)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }

    if (rowSubtitle != nullptr) {
      const std::string subtitleText = rowSubtitle(i);
      if (!subtitleText.empty()) {
        const auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
        renderer.drawText(SMALL_FONT_ID, rect.x + P, itemY + 24, subtitle.c_str(), true);
      }
    }

    if (!valueText.empty()) {
      const int vw = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
      const int valueY = (rowSubtitle != nullptr) ? itemY + 10 : itemY;
      renderer.drawText(UI_10_FONT_ID, rect.x + contentWidth - P - vw, valueY, valueText.c_str(), true);
    }
  }
}
