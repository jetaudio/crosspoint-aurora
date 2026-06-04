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
constexpr int kDividerGap = 32;    // Distance from status text to the divider line
                                   // (sized for the large UI_12 page title)
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

int AuroraTheme::drawHeaderBar(const GfxRenderer& renderer, int x, int top, int width, const char* title) const {
  const auto& metrics = AuroraMetrics::values;
  const int P = metrics.contentSidePadding;
  const int textY = top + kStatusY;

  // Title (left, large + bold), truncated so a long name (e.g. a folder path) can't
  // run into the battery on the right.
  if (title != nullptr) {
    const int rightReserve = P + metrics.batteryWidth + 52;  // battery icon + "NN%"
    const int maxW = std::max(20, (x + width - rightReserve) - (x + P));
    const auto t = renderer.truncatedText(kTitleFontId, title, maxW, EpdFontFamily::BOLD);
    renderer.drawText(kTitleFontId, x + P, textY, t.c_str(), true, EpdFontFamily::BOLD);
  }

  // Clock (center, X3 only — the X4 has no RTC).
  if (gpio.deviceIsX3() && SETTINGS.statusBarClock && halClock.isAvailable()) {
    char timeBuf[9];
    if (halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1)) {
      renderer.drawCenteredText(kCaptionFontId, textY, timeBuf);
    }
  }

  // Battery (right).
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  drawBatteryRight(renderer,
                   Rect{x + width - P - metrics.batteryWidth, textY, metrics.batteryWidth, metrics.batteryHeight},
                   showBatteryPercentage);

  // Divider rule below the status text, inset by the content side padding.
  const int dividerY = textY + kDividerGap;
  renderer.drawLine(x + P, dividerY, x + width - P, dividerY);
  return dividerY;
}

void AuroraTheme::drawHomeScreen(GfxRenderer& renderer, Rect content, const std::vector<RecentBook>& recentBooks,
                                 const std::vector<std::string>& barLabels, const std::vector<UIIcon>& barIcons,
                                 int listSelected, int activeTab) const {
  const auto& metrics = AuroraMetrics::values;
  const int W = content.width;
  const int P = metrics.contentSidePadding;

  // --- Status bar: page title (left), clock (center, X3 only), battery (right), divider ---
  const int dividerY = drawHeaderBar(renderer, content.x, content.y, W, tr(STR_LIBRARY));

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

  // --- "Recent Books" header + recent-books list (books only; nav lives in the bottom bar) ---
  // The page title is now "Library", so this in-content section uses "Recent Books"
  // to avoid showing "Library" twice on the same screen.
  // The list sits level with the right-edge arrow hints, so reserve that strip when
  // hints are shown (same inset the settings list uses) to avoid overlap.
  const int rightInset = SETTINGS.showButtonHints ? (metrics.sideButtonHintsWidth + 10) : 0;
  const int libHeaderY = thumbY + kThumbHeight + kSectionGap;
  renderer.drawText(kHeaderFontId, P, libHeaderY, tr(STR_MENU_RECENT_BOOKS), true, EpdFontFamily::BOLD);
  const int underlineY = libHeaderY + renderer.getLineHeight(kHeaderFontId) + 4;
  renderer.drawLine(P, underlineY, W - P - rightInset, underlineY);

  const int barTop = content.height - kBottomBarHeight;
  const int listTop = underlineY + 12;
  const int featuredOffset = hasFeatured ? 1 : 0;
  const int libBookCount = std::max(0, static_cast<int>(recentBooks.size()) - featuredOffset);
  // listSelected: 0 = featured card, 1.. = library row; map to the list's own index.
  const int librarySelected = listSelected >= 1 ? listSelected - 1 : -1;
  const int listHeight = std::max(0, barTop - listTop);

  drawList(
      renderer, Rect{0, listTop, W - rightInset, listHeight}, libBookCount, librarySelected,
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
  const int dividerY = drawHeaderBar(renderer, content.x, content.y, W, title);

  // --- Flat, section-grouped value list (no category pills) ---
  const int listTop = dividerY + 10;
  const int listBottom = content.y + content.height;
  const int listHeight = std::max(0, listBottom - listTop);
  const int rowH = kSettingRowHeight;
  // Section-header band: the label sits near the top (kHeaderTextTop) and the rest
  // is breathing room before the group's card, so names don't crowd their options.
  const int headerH = 38;
  constexpr int kHeaderTextTop = 10;
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
      renderer.drawText(kCaptionFontId, P + 2, y + kHeaderTextTop, items[i].text.c_str(), true, EpdFontFamily::BOLD);
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

  // Route through the shared status bar so the title, battery and divider match the
  // home/settings screens exactly. Callers pass rect.y = topPadding, so the band
  // top is rect.y - topPadding (the screen top), giving the same divider position
  // and inset length as every other page.
  const int dividerY = drawHeaderBar(renderer, rect.x, rect.y - m.topPadding, rect.width, title);

  // Optional subtitle (rare): small, right-aligned just under the divider.
  if (subtitle != nullptr) {
    const auto s = renderer.truncatedText(SMALL_FONT_ID, subtitle, rect.width - P * 2, EpdFontFamily::REGULAR);
    const int sw = renderer.getTextWidth(SMALL_FONT_ID, s.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - P - sw, dividerY + 5, s.c_str(), true);
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

void AuroraTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  // Governed by the same user toggle as the front button hint row.
  if (!SETTINGS.showButtonHints) return;

  // Same box positions/height as the base theme (they line up with the physical
  // side buttons); Aurora only narrows the strip and swaps the rotated "Up"/"Down"
  // text for filled up/down arrows.
  const int boxW = AuroraMetrics::values.sideButtonHintsWidth;  // slim strip (24px)
  constexpr int boxH = 80;                                      // matches the physical button extent
  constexpr int margin = 4;
  const int screenW = renderer.getScreenWidth();

  const bool showTop = topBtn != nullptr && topBtn[0] != '\0';
  const bool showBottom = bottomBtn != nullptr && bottomBtn[0] != '\0';

  // A box with a filled up/down triangle centred in it.
  auto drawArrowBox = [&](int x, int y, bool up) {
    renderer.drawRect(x, y, boxW, boxH);
    const int cx = x + boxW / 2;
    const int cy = y + boxH / 2;
    constexpr int half = 6;  // arrowhead half-width
    constexpr int tall = 7;  // arrowhead half-height
    if (up) {
      int xs[3] = {cx, cx - half, cx + half};
      int ys[3] = {cy - tall, cy + tall, cy + tall};
      renderer.fillPolygon(xs, ys, 3, true);
    } else {
      int xs[3] = {cx - half, cx + half, cx};
      int ys[3] = {cy - tall, cy - tall, cy + tall};
      renderer.fillPolygon(xs, ys, 3, true);
    }
  };

  if (gpio.deviceIsX3()) {
    // X3: Up box on the left edge, Down box on the right edge.
    constexpr int y = 155;
    if (showTop) drawArrowBox(margin, y, true);
    if (showBottom) drawArrowBox(screenW - margin - boxW, y, false);
  } else {
    // X4: both boxes stacked on the right edge.
    constexpr int topY = 345;
    const int x = screenW - margin - boxW;
    if (showTop) drawArrowBox(x, topY, true);
    if (showBottom) drawArrowBox(x, topY + boxH, false);
  }
}

void AuroraTheme::drawReaderToolbar(GfxRenderer& renderer, Rect screen, const ReaderToolbarInfo& info) const {
  const int X = screen.x;
  const int Y = screen.y;
  const int W = screen.width;
  const int H = screen.height;
  const int pad = 20;

  // Chevron ("<" / ">") drawn from strokes so we don't depend on font glyph coverage.
  auto chevron = [&](int cx, int cy, int s, bool left) {
    const int dx = left ? -s / 2 : s / 2;
    renderer.drawLine(cx - dx, cy - s, cx + dx, cy, 2, true);
    renderer.drawLine(cx + dx, cy, cx - dx, cy + s, 2, true);
  };

  // --- Top bar: back chevron (left), centered book title, focus indicator (right) ---
  const int topH = 50;
  renderer.fillRect(X, Y, W, topH, false);  // clear the page text behind the bar
  renderer.drawLine(X, Y + topH, X + W, Y + topH);

  chevron(X + 26, Y + topH / 2, 8, true);

  if (info.bookTitle != nullptr) {
    // Reserve room on the right for the battery (and a focus dot) so the centred
    // title can't run under them.
    const auto t = renderer.truncatedText(kTitleFontId, info.bookTitle, W - 160, EpdFontFamily::BOLD);
    const int th = renderer.getLineHeight(kTitleFontId);
    renderer.drawCenteredText(kTitleFontId, Y + (topH - th) / 2, t.c_str(), true, EpdFontFamily::BOLD);
  }

  // Battery (right), matching the status bar shown on every other screen (replaces
  // the old focus-reading ring; focus state still lives in the Text tool).
  const auto& m = AuroraMetrics::values;
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryY = Y + (topH - m.batteryHeight) / 2;
  drawBatteryRight(renderer, Rect{X + W - pad - m.batteryWidth, batteryY, m.batteryWidth, m.batteryHeight},
                   showBatteryPercentage);

  // --- Bottom bar: scrub row, meta row, tool row ---
  const int bottomH = 160;
  const int by = Y + H - bottomH;
  renderer.fillRect(X, by, W, bottomH, false);
  renderer.drawLine(X, by, X + W, by);

  // Scrub row: prev/next chapter buttons flanking a progress track + knob.
  const int btn = 34;
  const int sy = by + 16;
  const int leftBtnX = X + 16;
  const int rightBtnX = X + W - 16 - btn;
  renderer.drawRoundedRect(leftBtnX, sy, btn, btn, 1, 7, true);
  renderer.drawRoundedRect(rightBtnX, sy, btn, btn, 1, 7, true);
  chevron(leftBtnX + btn / 2, sy + btn / 2, 6, true);
  chevron(rightBtnX + btn / 2, sy + btn / 2, 6, false);
  const int trackX0 = leftBtnX + btn + 14;
  const int trackX1 = rightBtnX - 14;
  const int trackY = sy + btn / 2;
  renderer.drawLine(trackX0, trackY, trackX1, trackY, 2, true);
  const float prog = std::max(0.0f, std::min(1.0f, info.progress));
  const int knobX = trackX0 + static_cast<int>(prog * (trackX1 - trackX0));
  renderer.fillRoundedRect(knobX - 6, trackY - 6, 12, 12, 6, Color::Black);

  // Meta row: chapter title (left), chapter page X/Y + book percent (right).
  // Sit nearer the scrub row so the title isn't crowded against the tool-row rule
  // below it (the scrub→meta gap had slack to spare).
  const int my = by + 58;
  renderer.drawLine(X + 16, my, X + W - 16, my);
  if (info.chapterTitle != nullptr && info.chapterTitle[0] != '\0') {
    const auto ch = renderer.truncatedText(kCaptionFontId, info.chapterTitle, W / 2 - pad, EpdFontFamily::BOLD);
    renderer.drawText(kCaptionFontId, X + pad, my + 7, ch.c_str(), true, EpdFontFamily::BOLD);
  }
  const std::string pageInfo = std::string(tr(STR_PAGE_LABEL)) + " " + std::to_string(info.chapterPage) + "/" +
                               std::to_string(info.chapterPageCount) + "   " + std::to_string(info.bookPercent) + "%";
  const int pw = renderer.getTextWidth(kCaptionFontId, pageInfo.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(kCaptionFontId, X + W - pad - pw, my + 7, pageInfo.c_str(), true, EpdFontFamily::BOLD);

  // Tool row: Contents / Text / More, focused one in a rounded pill. Glyphs drawn
  // from primitives (hamburger / "Aa" / three dots) to avoid font-glyph deps.
  const int ty = by + 90;
  renderer.drawLine(X + 16, ty, X + W - 16, ty);
  const char* labels[3] = {tr(STR_TOOL_CONTENTS), tr(STR_TOOL_TEXT), tr(STR_TOOL_MORE)};
  const int slotW = W / 3;
  for (int i = 0; i < 3; ++i) {
    const int cx = X + slotW * i + slotW / 2;
    if (i == info.focusedTool) {
      renderer.drawRoundedRect(cx - 44, ty + 6, 88, 40, 2, 10, true);
    }
    const int gy = ty + 14;
    if (i == 0) {  // Contents: hamburger
      for (int k = 0; k < 3; ++k) renderer.drawLine(cx - 12, gy + k * 6, cx + 12, gy + k * 6, 2, true);
    } else if (i == 1) {  // Text: "Aa"
      const int aw = renderer.getTextWidth(kTitleFontId, "Aa", EpdFontFamily::BOLD);
      renderer.drawText(kTitleFontId, cx - aw / 2, ty + 8, "Aa", true, EpdFontFamily::BOLD);
    } else {  // More: three dots
      for (int k = -1; k <= 1; ++k) renderer.fillRect(cx + k * 8 - 1, gy + 6, 3, 3, true);
    }
    const int lw = renderer.getTextWidth(kBarLabelFontId, labels[i], EpdFontFamily::BOLD);
    renderer.drawText(kBarLabelFontId, cx - lw / 2, ty + 46, labels[i], true, EpdFontFamily::BOLD);
  }
}

void AuroraTheme::drawReaderPanel(GfxRenderer& renderer, Rect screen, const char* title, int itemCount,
                                  int selectedIndex, const std::function<std::string(int)>& rowText,
                                  const std::function<std::string(int)>& rowValue) const {
  const int X = screen.x;
  const int Y = screen.y;
  const int W = screen.width;
  const int H = screen.height;
  const int P = AuroraMetrics::values.contentSidePadding;

  // Bottom sheet covering the lower part of the screen; the page stays visible above.
  const int panelTop = Y + (H * 38) / 100;
  renderer.fillRect(X, panelTop, W, (Y + H) - panelTop, false);  // clear page text behind the sheet
  renderer.drawLine(X, panelTop, X + W, panelTop);
  renderer.drawLine(X, panelTop + 1, X + W, panelTop + 1);  // 2px top edge

  const int titleY = panelTop + 14;
  renderer.drawText(kTitleFontId, X + P, titleY, title, true, EpdFontFamily::BOLD);
  const int dividerY = titleY + renderer.getLineHeight(kTitleFontId) + 8;
  renderer.drawLine(X + P, dividerY, X + W - P, dividerY);

  const int contentTop = dividerY + 10;
  const int hintReserve = 44;  // leave room for the button hint row at the bottom
  const int contentH = (Y + H) - contentTop - hintReserve;
  drawList(renderer, Rect{X, contentTop, W, contentH}, itemCount, selectedIndex, rowText, nullptr, nullptr, rowValue,
           true);
}
