//! The Aurora home screen: a library list + a bottom icon tab bar, with the
//! signature **two-zone navigation**.
//!
//! Ports the interaction model described in the project's Aurora design (see
//! `CLAUDE.md`): the side **Up/Down** buttons browse the content list; the front
//! **Left/Right** buttons move the bottom tab bar; **Select** activates whichever
//! zone was last used. This is the state machine only — rendering lives in the
//! firmware. It is host-unit-tested.

use crate::ui::Menu;

/// The four bottom tabs (Browse Files / Recent Books / Settings / File Transfer).
pub const TAB_LABELS: [&str; 4] = ["Browse", "Recent", "Settings", "Transfer"];
pub const TAB_COUNT: usize = 4;

/// Which navigation zone currently has focus.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Zone {
    Content,
    TabBar,
}

/// What a Select press resolves to, given the active zone.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum HomeAction {
    /// Open the library item at this index.
    OpenContent(usize),
    /// Activate the tab at this index (0..TAB_COUNT).
    OpenTab(usize),
    /// Nothing to do (e.g. empty library).
    None,
}

/// Aurora home model: a content list (the library) + a tab cursor + the active
/// zone.
pub struct AuroraHome {
    content: Menu,
    tab: usize,
    zone: Zone,
}

impl AuroraHome {
    pub fn new(content: Menu) -> Self {
        Self {
            content,
            tab: 0,
            zone: Zone::Content,
        }
    }

    pub fn content(&self) -> &Menu {
        &self.content
    }
    pub fn content_mut(&mut self) -> &mut Menu {
        &mut self.content
    }
    pub fn tab(&self) -> usize {
        self.tab
    }
    pub fn tab_label(&self) -> &'static str {
        TAB_LABELS[self.tab]
    }
    pub fn zone(&self) -> Zone {
        self.zone
    }

    /// The currently highlighted library item (for the "Now Reading"/featured
    /// preview).
    pub fn featured(&self) -> Option<&str> {
        self.content.selected_item()
    }

    /// Side Up — move the content selector and focus the content zone.
    pub fn up(&mut self) -> bool {
        self.zone = Zone::Content;
        self.content.up()
    }
    /// Side Down — move the content selector and focus the content zone.
    pub fn down(&mut self) -> bool {
        self.zone = Zone::Content;
        self.content.down()
    }
    /// Front Left — move the tab cursor left and focus the tab bar.
    pub fn left(&mut self) -> bool {
        self.zone = Zone::TabBar;
        if self.tab > 0 {
            self.tab -= 1;
            true
        } else {
            false
        }
    }
    /// Front Right — move the tab cursor right and focus the tab bar.
    pub fn right(&mut self) -> bool {
        self.zone = Zone::TabBar;
        if self.tab + 1 < TAB_COUNT {
            self.tab += 1;
            true
        } else {
            false
        }
    }

    /// Resolve a Select press against the active zone.
    pub fn select(&self) -> HomeAction {
        match self.zone {
            Zone::Content => {
                if self.content.is_empty() {
                    HomeAction::None
                } else {
                    HomeAction::OpenContent(self.content.selected())
                }
            }
            Zone::TabBar => HomeAction::OpenTab(self.tab),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn home() -> AuroraHome {
        AuroraHome::new(Menu::from_items(&["book-a.txt", "book-b.epub"]))
    }

    #[test]
    fn side_buttons_drive_content_zone() {
        let mut h = home();
        assert_eq!(h.zone(), Zone::Content);
        assert!(h.down());
        assert_eq!(h.zone(), Zone::Content);
        assert_eq!(h.featured(), Some("book-b.epub"));
        assert_eq!(h.select(), HomeAction::OpenContent(1));
    }

    #[test]
    fn front_buttons_drive_tab_zone() {
        let mut h = home();
        assert!(h.right());
        assert_eq!(h.zone(), Zone::TabBar);
        assert_eq!(h.tab(), 1);
        assert_eq!(h.tab_label(), "Recent");
        assert_eq!(h.select(), HomeAction::OpenTab(1));
        // Walk to the last tab and confirm clamping.
        assert!(h.right());
        assert!(h.right());
        assert!(!h.right());
        assert_eq!(h.tab(), TAB_COUNT - 1);
    }

    #[test]
    fn zone_switches_with_input() {
        let mut h = home();
        h.right(); // tab zone
        assert_eq!(h.zone(), Zone::TabBar);
        h.down(); // back to content zone
        assert_eq!(h.zone(), Zone::Content);
        assert_eq!(h.select(), HomeAction::OpenContent(h.content().selected()));
    }

    #[test]
    fn empty_library_select_is_none() {
        let mut h = AuroraHome::new(Menu::new());
        assert_eq!(h.select(), HomeAction::None);
        // Tab zone still works.
        h.right();
        assert_eq!(h.select(), HomeAction::OpenTab(1));
    }
}
