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
constexpr int kThumbHeight = 150;  // Featured cover thumbnail height
constexpr int kThumbWidth = 100;   // Featured cover thumbnail width (~2:3)
constexpr int kTextGap = 16;       // Gap between thumbnail and title block
constexpr int kIconSize = 32;      // Placeholder cover glyph / bottom-bar icon size
constexpr int kSectionGap = 28;    // Gap above the "Library" header
constexpr int kBottomBarHeight = 70;
constexpr int kHeaderFontId = UI_10_FONT_ID;
constexpr int kTitleFontId = UI_12_FONT_ID;
constexpr int kBodyFontId = UI_10_FONT_ID;
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
                                 int listSelected, int barSelected) const {
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

    // Title + author block to the right of the cover.
    const int txtX = thumbX + kThumbWidth + kTextGap;
    const int txtW = W - P - txtX;
    int textY = thumbY + 4;
    const auto titleLines = renderer.wrappedText(kTitleFontId, book.title.c_str(), txtW, 3, EpdFontFamily::BOLD);
    for (const auto& line : titleLines) {
      renderer.drawText(kTitleFontId, txtX, textY, line.c_str(), true, EpdFontFamily::BOLD);
      textY += renderer.getLineHeight(kTitleFontId);
    }
    if (!book.author.empty()) {
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

  // --- Bottom navigation bar: 4 icon + label tabs ---
  const int barCount = static_cast<int>(std::min(barLabels.size(), barIcons.size()));
  if (barCount > 0) {
    renderer.drawLine(0, barTop, W, barTop);
    const int slotW = W / barCount;
    for (int i = 0; i < barCount; ++i) {
      const int slotX = i * slotW;
      const int centerX = slotX + slotW / 2;

      if (i == barSelected) {
        renderer.fillRoundedRect(slotX + 6, barTop + 5, slotW - 12, kBottomBarHeight - 10, 8, Color::LightGray);
      }

      const uint8_t* iconBitmap = barIconBitmap(barIcons[i]);
      if (iconBitmap != nullptr) {
        renderer.drawIcon(iconBitmap, centerX - kIconSize / 2, barTop + 10, kIconSize, kIconSize);
      }

      const auto label = renderer.truncatedText(kBarLabelFontId, barLabels[i].c_str(), slotW - 6);
      const int labelWidth = renderer.getTextWidth(kBarLabelFontId, label.c_str());
      renderer.drawText(kBarLabelFontId, centerX - labelWidth / 2, barTop + 10 + kIconSize + 2, label.c_str());
    }
  }
}
