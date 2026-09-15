#include "FileBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/ContextMenuActivity.h"
#include "activities/util/HomeTabBar.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;
}  // namespace

std::string getFileName(std::string filename);
std::string getFileExtension(const std::string& filename);

FileBrowserActivity::FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::string initialPath, const Mode mode)
    : UiListActivity("FileBrowser", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      mode(mode),
      basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}

void FileBrowserActivity::loadFiles() {
  files.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    rebuildRowItems();  // files is empty; also drops any now-stale cached rows
    return;
  }

  root.rewindDirectory();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    root.close();
    rebuildRowItems();
    return;
  }

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    const bool isDirectory = file.isDirectory();
    if ((!SETTINGS.showHiddenFiles && fileNameBuffer[0] == '.') ||
        strcmp(fileNameBuffer.get(), "System Volume Information") == 0) {
      continue;
    }

    if (isDirectory) {
      files.emplace_back(std::string(fileNameBuffer.get()) + "/");
    } else {
      std::string_view filename{fileNameBuffer.get()};
      if (mode == Mode::PickFirmware) {
        // Firmware picker: only show .bin files.
        if (FsHelpers::checkFileExtension(filename, ".bin")) {
          files.emplace_back(filename);
        }
      } else if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
                 FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
                 FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename)) {
        files.emplace_back(filename);
      }
    }
  }
  root.close();
  FsHelpers::sortFileList(files);
  rebuildRowItems();
}

// Derives rowNames/rowExtensions/rowItems from `files`. Called whenever
// `files` changes (end of loadFiles()) so buildScreen() can reuse the cached
// rows on every repaint instead of re-deriving a name/extension string (and a
// ListItem) per file each time it's called.
void FileBrowserActivity::rebuildRowItems() {
  rowsUseFileIcons = UITheme::getInstance().getTheme().showsFileIcons();
  rowNames.resize(files.size());
  rowExtensions.resize(files.size());
  rowItems.clear();
  rowItems.reserve(files.size());
  for (size_t i = 0; i < files.size(); i++) {
    rowNames[i] = getFileName(files[i]);
    rowExtensions[i] = getFileExtension(files[i]);
    fui::ListItem item;
    item.label = rowNames[i].c_str();
    if (!rowExtensions[i].empty()) item.value = rowExtensions[i].c_str();
    item.icon = listIconFor(UITheme::getFileIcon(files[i]));
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }

  // One SD pass for every CJK filename in the folder; repaints then hit the
  // resident tables instead of re-reading per-string. Getter form: no
  // concatenated copy (a bare-new string append aborts under heap pressure).
  // The last index covers the bottom path band: basepath (possibly a CJK
  // folder name) draws in the same small font, so it must live in the same
  // batch or it would evict the rows' glyphs when the heap gate disables
  // union merging. (prewarmFallbackText appends the truncation ellipsis.)
  struct PrewarmCtx {
    const std::vector<std::string>* names;
    const std::string* path;
  } prewarmCtx{&rowNames, &basepath};
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        const auto* c = static_cast<const PrewarmCtx*>(ctx);
        return i < c->names->size() ? (*c->names)[i].c_str() : c->path->c_str();
      },
      &prewarmCtx, static_cast<uint32_t>(rowNames.size()) + 1);
}

void FileBrowserActivity::onEnter() {
  UiListActivity::onEnter();

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "malloc failed for name buffer");
    return;
  }

  // If Confirm was held while this activity opened (typical when launched from a menu), ignore
  // its release — otherwise we'd immediately auto-open whatever is at index 0.
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    // Opened straight onto a file (e.g. resume): a Back still held from the previous screen
    // must not read as this screen's long-press jump to root.
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);

    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    // The first screen build pulls the viewport to it (ListNav follow-on-build).
    nav.selected = static_cast<int>(findEntry(fileName));
  } else {
    loadFiles();
  }
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
  rowNames.clear();
  rowExtensions.clear();
  rowItems.clear();
  fileNameBuffer.reset();
}

// To avoid traversing directories twice (once for cache clearing, once for deletion),
// we do both in one pass here, instead of using Storage.removeDir
bool FileBrowserActivity::removeDirFile(const std::string& fullPath) {
  auto file = Storage.open(fullPath.c_str());
  if (!file) {
    LOG_ERR("FileBrowser", "Failed to open for metadata clearing: %s", fullPath.c_str());
    return false;
  }

  if (!file.isDirectory()) {
    file.close();
    clearBookCache(fullPath);
    return Storage.remove(fullPath.c_str());
  }
  file.close();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    return false;
  }

  // Stack of (dirPath, postOrder): postOrder=true means rmdir this path after children are processed.
  std::vector<std::pair<std::string, bool>> stack;
  stack.reserve(16);
  stack.push_back({fullPath, false});

  while (!stack.empty()) {
    auto [currentPath, postOrder] = std::move(stack.back());
    stack.pop_back();

    if (postOrder) {
      if (!Storage.rmdir(currentPath.c_str())) {
        LOG_ERR("FileBrowser", "Failed to rmdir: %s", currentPath.c_str());
        return false;
      }
      continue;
    }

    auto dir = Storage.open(currentPath.c_str());
    if (!dir) {
      LOG_ERR("FileBrowser", "Failed to open dir: %s", currentPath.c_str());
      return false;
    }
    if (!dir.isDirectory()) {
      LOG_ERR("FileBrowser", "Not a directory: %s", currentPath.c_str());
      return false;
    }

    // Push this dir for post-order rmdir (after all children are processed).
    stack.push_back({currentPath, true});

    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
      if (strcmp(fileNameBuffer.get(), ".") == 0 || strcmp(fileNameBuffer.get(), "..") == 0) {
        continue;
      }
      std::string entryPath = currentPath;
      if (entryPath.back() != '/') {
        entryPath += "/";
      }
      entryPath += fileNameBuffer.get();

      const bool isDir = entry.isDirectory();
      entry.close();

      if (isDir) {
        stack.push_back({std::move(entryPath), false});
      } else {
        clearBookCache(entryPath);
        if (!Storage.remove(entryPath.c_str())) {
          LOG_ERR("FileBrowser", "Failed to remove file: %s", entryPath.c_str());
          return false;
        }
      }
    }
  }

  return true;
}

// Open the long-press context menu for `entry` as an overlay. Rename opens a keyboard prefilled
// with the current name; Delete opens a confirmation dialog before anything is removed.
void FileBrowserActivity::showContextMenu(const std::string& entry) {
  std::string directory = basepath;
  if (directory.back() != '/') directory += "/";

  // Name without the trailing slash directories carry in the list, and whether it is a directory.
  const bool isDir = !entry.empty() && entry.back() == '/';
  std::string name = entry;
  if (isDir) name.pop_back();

  const std::string fullPath = directory + name;

  auto menuHandler = [this, directory, name, fullPath, isDir](const ActivityResult& res) {
    if (res.isCancelled) return;
    const auto* menu = std::get_if<MenuResult>(&res.data);
    if (!menu) return;
    switch (static_cast<ContextAction>(menu->action)) {
      case ContextAction::Rename:
        promptRename(directory, name, isDir);
        break;
      case ContextAction::Delete:
        confirmAndDelete(fullPath, name);
        break;
    }
  };

  std::vector<ContextMenuActivity::Item> items = {
      {StrId::STR_RENAME, static_cast<int>(ContextAction::Rename)},
      {StrId::STR_DELETE, static_cast<int>(ContextAction::Delete)},
  };
  startActivityForResult(std::make_unique<ContextMenuActivity>(renderer, mappedInput, name, std::move(items)),
                         menuHandler);
}

// Show the on-screen keyboard prefilled with the editable part of the name; on confirm, rename the
// entry in the same directory and refresh the list (keeping the selection on the renamed entry).
// Only the base name is editable — the extension is preserved (directories have no extension).
void FileBrowserActivity::promptRename(const std::string& directory, const std::string& currentName, bool isDir) {
  // Split off the extension so the user only edits the base name. A leading dot (hidden file) is
  // not treated as an extension separator.
  std::string base = currentName;
  std::string ext;
  if (!isDir) {
    const auto dot = currentName.rfind('.');
    if (dot != std::string::npos && dot != 0) {
      base = currentName.substr(0, dot);
      ext = currentName.substr(dot);
    }
  }

  auto handler = [this, directory, currentName, base, ext, isDir](const ActivityResult& res) {
    if (res.isCancelled) return;
    const auto* kb = std::get_if<KeyboardResult>(&res.data);
    if (!kb) return;
    const std::string newBase = kb->text;
    if (newBase.empty() || newBase == base) return;

    const std::string newName = newBase + ext;
    const std::string oldPath = directory + currentName;
    const std::string newPath = directory + newName;
    // The book cache is keyed by a hash of the file path, so a rename orphans it; clear the old
    // entry's cache first (a no-op for non-book files) so it doesn't linger.
    clearBookCache(oldPath);
    if (Storage.rename(oldPath.c_str(), newPath.c_str())) {
      LOG_DBG("FileBrowser", "Renamed: %s -> %s", oldPath.c_str(), newPath.c_str());
      {
        // buildScreen() reads the row caches on the render task; see activateSelected().
        RenderLock lock(*this);
        loadFiles();
        nav.selected = static_cast<int>(findEntry(isDir ? newName + "/" : newName));
        nav.follow(listCount());
      }
      requestUpdate(true);
    } else {
      LOG_ERR("FileBrowser", "Rename failed: %s -> %s", oldPath.c_str(), newPath.c_str());
    }
  };

  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RENAME), base,
                                                                 /*maxLength=*/0, InputType::Text),
                         handler);
}

// Show the delete-confirmation overlay and, if confirmed, remove the file/directory.
void FileBrowserActivity::confirmAndDelete(const std::string& fullPath, const std::string& displayName) {
  // The context menu has just closed; repaint the file list so its box doesn't bleed through
  // under the confirmation overlay (both share the framebuffer).
  requestUpdateAndWait();

  auto handler = [this, fullPath](const ActivityResult& res) {
    if (res.isCancelled) {
      LOG_DBG("FileBrowser", "Delete cancelled by user");
      return;
    }
    // Repaint the file list now so the confirmation dialog disappears immediately, before the
    // delete (clearing a book's cache can take a moment and would otherwise leave it on screen).
    requestUpdateAndWait();
    LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
    if (removeDirFile(fullPath)) {
      LOG_DBG("FileBrowser", "Deleted successfully");
      {
        // buildScreen() reads the row caches on the render task; see activateSelected().
        RenderLock lock(*this);
        loadFiles();
        if (files.empty()) {
          nav.selected = 0;
        } else if (nav.selected >= listCount()) {
          // Move selection to the new "last" item
          nav.selected = listCount() - 1;
        }
        nav.follow(listCount());
      }
      requestUpdate(true);
    } else {
      LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
    }
  };

  std::string heading = tr(STR_DELETE) + std::string("? ");
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, displayName), handler);
}

void FileBrowserActivity::activateIndex(const int index) {
  (void)index;  // base already synced nav.selected to the tapped row
  // Activation navigates or opens; a lingering flash would gray an unrelated
  // row on the next list.
  app.clearTapFlash();
  activateSelected();
}

void FileBrowserActivity::onRowLongPress(const int index) {
  (void)index;  // base already synced nav.selected to the pressed row
  // The menu draws over this screen; a lingering flash would gray a row under it.
  app.clearTapFlash();
  if (mode != Mode::Books || files.empty()) return;
  if (nav.selected < 0 || nav.selected >= listCount()) return;
  showContextMenu(files[nav.selected]);
}

void FileBrowserActivity::activateSelected() {
  if (files.empty()) return;
  // A touch activation can carry a row index captured before a delete/reload
  // shrank the list; the next render re-registers the rows.
  if (nav.selected < 0 || nav.selected >= listCount()) return;

  const std::string& entry = files[nav.selected];
  bool isDirectory = (entry.back() == '/');

  // Firmware picker: select file -> return path; navigate into directories normally.
  if (mode == Mode::PickFirmware && !isDirectory) {
    std::string cleanBasePath = basepath;
    if (cleanBasePath.back() != '/') cleanBasePath += "/";
    ActivityResult res{FilePathResult{cleanBasePath + entry}};
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  {
    // --- OPEN / NAVIGATE ---
    // Deleting and renaming live in the long-press context menu (showContextMenu),
    // so a plain activation only ever opens a file or descends into a folder.
    // buildScreen() runs on the render task and reads basepath plus the
    // ListItem label/value pointers into rowNames/rowExtensions that
    // rebuildRowItems() frees; mutate only under the render lock.
    RenderLock lock(*this);
    if (basepath.back() != '/') basepath += "/";

    if (isDirectory) {
      basepath += entry.substr(0, entry.length() - 1);
      loadFiles();
      nav.selected = 0;
      nav.top = 0;
      lock.unlock();
      requestUpdate();
    } else {
      const std::string fullPath = basepath + entry;
      lock.unlock();  // onSelectBook launches an activity; don't hold the lock across it
      onSelectBook(fullPath);
    }
  }
  return;
}

// Tab mode reserves the front Left/Right pair for the bottom bar, so only the side
// buttons walk the list there (the base uses the logical Nav pair, which folds
// front Left/Right in and would move the selection on a held tab press).
void FileBrowserActivity::navigateButtons() {
  if (!(mode == Mode::Books && GUI.ownsHomeLayout())) {
    UiListActivity::navigateButtons();
    return;
  }
  const int count = listCount();
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, count] {
    moveSelectionTo(ButtonNavigator::previousIndex(activeNav().selected, count));
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this, count] {
    moveSelectionTo(ButtonNavigator::nextIndex(activeNav().selected, count));
  });
}

bool FileBrowserActivity::handleCustomInput() {
  // Aurora "Browse" tab: front Left/Right walk the bottom bar (the side Up/Down pair
  // keeps the list). Not in the firmware picker, a pushed sub-activity with no tab bar.
  if (mode == Mode::Books && GUI.ownsHomeLayout() && HomeTabBar::handleLeftRight(mappedInput, HomeTabBar::Browse)) {
    return true;
  }
  if (mode == Mode::Books && GUI.ownsHomeLayout() && HomeTabBar::handleTap(mappedInput, renderer, HomeTabBar::Browse)) {
    return true;
  }

  // Long press Confirm opens the context menu the moment the 1s threshold is reached, while
  // the button is still held (no need to release first). `contextMenuArmed` fires it once per
  // hold and resets whenever Confirm is up, so a later short press still opens normally.
  if (mode == Mode::Books && mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
    if (!contextMenuArmed && !lockNextConfirmRelease && !files.empty() && mappedInput.getHeldTime() >= GO_HOME_MS &&
        nav.selected >= 0 && nav.selected < listCount()) {
      contextMenuArmed = true;
      showContextMenu(files[nav.selected]);
      return true;
    }
  } else {
    contextMenuArmed = false;
  }

  // A Back carried in from the previous screen (see onEnter) owns its own release.
  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    return true;
  }

  // Long press BACK (1s+) goes to root folder (Books mode only).
  // In firmware-pick mode we keep navigation simple: short Back = up dir / cancel.
  // A synthetic Back (home-key tap bound to Back) has no hold time of its own,
  // so it must never read as a long press here.
  if (mode == Mode::Books && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      !mappedInput.wasSyntheticBack() && mappedInput.getHeldTime() >= GO_HOME_MS && basepath != "/") {
    {
      // buildScreen() runs on the render task and reads basepath plus the
      // row caches rebuildRowItems() frees; mutate only under the render lock.
      RenderLock lock(*this);
      basepath = "/";
      loadFiles();
      nav.selected = 0;
      nav.top = 0;
    }
    requestUpdate();
    return true;
  }

  return false;
}

bool FileBrowserActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (lockNextConfirmRelease) {
      lockNextConfirmRelease = false;
      return true;
    }
    activateSelected();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or go home if at root. A synthetic Back
    // (home-key tap bound to Back) counts as short — see wasSyntheticBack().
    if (mappedInput.wasSyntheticBack() || mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        {
          // buildScreen() runs on the render task and reads basepath plus the
          // row caches rebuildRowItems() frees; mutate only under the render lock.
          RenderLock lock(*this);
          basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
          if (basepath.empty()) basepath = "/";
          loadFiles();

          const auto pos = oldPath.find_last_of('/');
          const std::string dirName = oldPath.substr(pos + 1) + "/";
          nav.selected = static_cast<int>(findEntry(dirName));
          nav.top = 0;
          nav.follow(listCount());
        }

        requestUpdate();
      } else if (mode == Mode::PickFirmware) {
        // Firmware picker at root: cancel back to caller instead of going home.
        ActivityResult res;
        res.isCancelled = true;
        setResult(std::move(res));
        finish();
      } else {
        onGoHome();
      }
    }
    return true;
  }

  return false;
}

std::string getFileName(std::string filename) {
  // Display copy only — `files[]` keeps the raw directory-entry bytes, because
  // FAT long-filename lookup is byte-exact: an NFC-normalized path would fail
  // to open the NFD entry macOS wrote. Composing here fixes rendering (fonts
  // carry precomposed syllables / letters only) without touching paths.
  filename = utf8ComposeNfc(filename);
  if (filename.back() == '/') {
    filename.pop_back();
    if (!UITheme::getInstance().getTheme().showsFileIcons()) {
      return "[" + filename + "]";
    }
    return filename;
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

std::string getFileExtension(const std::string& filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  return filename.substr(pos);
}

void FileBrowserActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints — plus the Aurora
  // bottom bar in tab mode, and minus the hint row when the user hid it. The right
  // inset keeps the rows clear of the side Up/Down hints drawn along that edge.
  const bool tabMode = mode == Mode::Books && GUI.ownsHomeLayout();
  const int hintH = (tabMode && !SETTINGS.showButtonHints) ? 0 : metrics.buttonHintsHeight;
  const int bottom = hintH + (tabMode ? GUI.bottomBarHeight() : 0);
  const int rightInset = (tabMode && SETTINGS.showButtonHints) ? (metrics.sideButtonHintsWidth + 10) : 0;
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(rightInset), static_cast<int16_t>(bottom), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Full path band at the bottom: separator on top, left-truncated so the
  // deepest directory stays visible.
  {
    const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(pathLineHeight + metrics.verticalSpacing));
    screen.target().fill(fui::Rect{band.x, band.y, band.width, 3}, fui::Paint::solid(fui::Color::Black));
    const int pathY =
        band.y + metrics.verticalSpacing / 2 + (band.height - metrics.verticalSpacing / 2 - pathLineHeight) / 2;
    const int pathMaxWidth = band.width - metrics.contentSidePadding * 2;
    const char* pathStr = basepath.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
      const char* p = pathStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      pathDisplay = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, band.x + metrics.contentSidePadding, pathY, pathDisplay);
  }

  if (files.empty()) {
    screen.centeredText(mode == Mode::PickFirmware ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND),
                        screen.theme().bodyText);
    return;
  }

  // rowNames/rowExtensions/rowItems are built once per loadFiles() call (see
  // rebuildRowItems()) and reused here. getFileName()'s folder-bracket format
  // depends on the theme, so a theme change picked up while this activity was
  // paused underneath another screen invalidates the cache before it's read.
  if (rowsUseFileIcons != UITheme::getInstance().getTheme().showsFileIcons()) {
    rebuildRowItems();
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  // Tap opens/navigates; long-press prompts delete (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.valueInset = 8;  // air between the extension and the row edge
  // Names use up to two small-font lines; shared list layout sizes each row.
  fui::TextStyle label = screen.theme().smallText;
  label.maxLines = 2;
  props.labelText = label;

  // The trailing value here is just the short extension: skip the balanced
  // 60%-band wrap cap and let both name lines run the full width before it.
  props.balanceWrappedLabelWithValue = false;
  syncListViewport(screen, props);
  screen.list(props);
}

void FileBrowserActivity::drawChrome() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName =
      (mode == Mode::PickFirmware)
          ? std::string(tr(STR_SELECT_FIRMWARE_FILE))
          : ((basepath == "/") ? std::string(tr(STR_BROWSER)) : basepath.substr(basepath.rfind('/') + 1));
  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());
}

void FileBrowserActivity::drawFooter() {
  const char* backLabel = (basepath == "/") ? (mode == Mode::PickFirmware ? tr(STR_BACK) : tr(STR_HOME)) : tr(STR_BACK);
  // In PickFirmware mode, Confirm on a .bin returns the path to the caller (not "open"); show
  // STR_SELECT instead. Directories in the same picker still descend, so keep STR_OPEN there.
  const bool selectingFirmwareFile = mode == Mode::PickFirmware && !files.empty() && nav.selected >= 0 &&
                                     nav.selected < listCount() && files[nav.selected].back() != '/';
  const char* confirmLabel = files.empty() ? "" : (selectingFirmwareFile ? tr(STR_SELECT) : tr(STR_OPEN));
  if (mode == Mode::Books && GUI.ownsHomeLayout()) {
    // Front Left/Right switch tabs; the side Up/Down pair moves the list selection.
    const auto tabLabels = mappedInput.mapLabels(backLabel, confirmLabel, tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, tabLabels.btn1, tabLabels.btn2, tabLabels.btn3, tabLabels.btn4);
    GUI.drawSideButtonHints(renderer, files.empty() ? "" : tr(STR_DIR_UP), files.empty() ? "" : tr(STR_DIR_DOWN));
    HomeTabBar::draw(renderer, renderer.getScreenWidth(), renderer.getScreenHeight(), HomeTabBar::Browse);
    return;
  }

  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, files.empty() ? "" : tr(STR_DIR_UP),
                                            files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  const auto entry = std::find(files.begin(), files.end(), name);
  return entry != files.end() ? static_cast<size_t>(entry - files.begin()) : 0;
}
