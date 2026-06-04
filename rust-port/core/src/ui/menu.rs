//! A reusable vertical menu widget: a list of owned item strings with a
//! wrapping cursor.
//!
//! Used for both the Home screen (fixed entries) and the file Browser (entries
//! built at runtime from the SD-card root listing). Items are owned `heapless`
//! strings so the browser can hold names read off the card without allocation.

use heapless::String;
use heapless::Vec;

/// Max characters per menu entry. FAT short names are 8.3 (≤ 12 chars); 24
/// leaves room for a little decoration.
pub const MAX_ITEM_LEN: usize = 24;
/// Max entries in a menu.
pub const MAX_ITEMS: usize = 32;

/// A vertical menu with a wrapping selection cursor over owned item strings.
#[derive(Default)]
pub struct Menu {
    items: Vec<String<MAX_ITEM_LEN>, MAX_ITEMS>,
    selected: usize,
}

impl Menu {
    /// An empty menu (fill with [`Menu::push`]).
    pub fn new() -> Self {
        Self {
            items: Vec::new(),
            selected: 0,
        }
    }

    /// Build a menu from a fixed list of `&str` items (truncated to fit).
    pub fn from_items(items: &[&str]) -> Self {
        let mut m = Self::new();
        for it in items {
            let _ = m.push(it);
        }
        m
    }

    /// Append an item (truncated to `MAX_ITEM_LEN`). Returns false if the menu
    /// is already full.
    pub fn push(&mut self, text: &str) -> bool {
        let mut s: String<MAX_ITEM_LEN> = String::new();
        for ch in text.chars() {
            if s.push(ch).is_err() {
                break; // truncate to capacity
            }
        }
        self.items.push(s).is_ok()
    }

    /// Remove all items and reset the cursor.
    pub fn clear(&mut self) {
        self.items.clear();
        self.selected = 0;
    }

    pub fn len(&self) -> usize {
        self.items.len()
    }

    pub fn is_empty(&self) -> bool {
        self.items.is_empty()
    }

    pub fn selected(&self) -> usize {
        self.selected
    }

    /// The `i`-th item's text, if present.
    pub fn item(&self, i: usize) -> Option<&str> {
        self.items.get(i).map(|s| s.as_str())
    }

    /// The selected item's text, if any.
    pub fn selected_item(&self) -> Option<&str> {
        self.item(self.selected)
    }

    /// Move the cursor up, wrapping to the bottom. Returns true if it moved.
    pub fn up(&mut self) -> bool {
        if self.items.is_empty() {
            return false;
        }
        self.selected = if self.selected == 0 {
            self.items.len() - 1
        } else {
            self.selected - 1
        };
        true
    }

    /// Move the cursor down, wrapping to the top. Returns true if it moved.
    pub fn down(&mut self) -> bool {
        if self.items.is_empty() {
            return false;
        }
        self.selected = (self.selected + 1) % self.items.len();
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wraps_both_directions() {
        let mut m = Menu::from_items(&["Open book", "About"]);
        assert_eq!(m.len(), 2);
        assert_eq!(m.selected(), 0);
        assert!(m.down());
        assert_eq!(m.selected(), 1);
        assert!(m.down()); // wrap to top
        assert_eq!(m.selected(), 0);
        assert!(m.up()); // wrap to bottom
        assert_eq!(m.selected(), 1);
        assert_eq!(m.selected_item(), Some("About"));
    }

    #[test]
    fn empty_menu_is_inert() {
        let mut m = Menu::new();
        assert!(m.is_empty());
        assert!(!m.up());
        assert!(!m.down());
        assert_eq!(m.selected_item(), None);
    }

    #[test]
    fn push_truncates_and_caps() {
        let mut m = Menu::new();
        let long = "this-name-is-way-too-long-to-fit-in-the-buffer.txt";
        assert!(m.push(long));
        assert!(m.item(0).unwrap().len() <= MAX_ITEM_LEN);
        // Fill to capacity and confirm the cap.
        while m.push("x") {}
        assert_eq!(m.len(), MAX_ITEMS);
    }
}
