//! A reusable vertical menu widget: a list of items with a wrapping cursor.
//!
//! Used for both the Home screen and the file Browser — anywhere the firmware
//! shows a selectable list navigated with Up/Down and chosen with Select.

/// A vertical menu with a wrapping selection cursor over a fixed item list.
pub struct Menu {
    items: &'static [&'static str],
    selected: usize,
}

impl Menu {
    pub fn new(items: &'static [&'static str]) -> Self {
        Self { items, selected: 0 }
    }

    pub fn items(&self) -> &'static [&'static str] {
        self.items
    }

    pub fn selected(&self) -> usize {
        self.selected
    }

    /// The selected item's text, if any.
    pub fn selected_item(&self) -> Option<&'static str> {
        self.items.get(self.selected).copied()
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

    const ITEMS: &[&str] = &["Open book", "About"];

    #[test]
    fn wraps_both_directions() {
        let mut m = Menu::new(ITEMS);
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
        let mut m = Menu::new(&[]);
        assert!(!m.up());
        assert!(!m.down());
        assert_eq!(m.selected(), 0);
        assert_eq!(m.selected_item(), None);
    }
}
