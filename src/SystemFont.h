#pragma once

// Registers the system (UI) font face selected by SETTINGS.systemFont into the
// renderer's UI_*_FONT_ID slots and clears the glyph cache so the change shows
// immediately. Defined in main.cpp (it needs the renderer, font-cache, and the
// built-in face globals). Called at boot and whenever the System Font setting
// changes.
void applySystemUiFont();
