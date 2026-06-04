//! Home screen: a small vertical menu navigated with Up/Down and Select.
//!
//! This is the entry screen of the firmware's screen stack. It holds a fixed set
//! of menu items and a selection cursor; the app maps Select on each item to a
//! navigation action (e.g. open the reader).

/// A vertical menu with a wrapping selection cursor.
pub struct Home {
    items: &'static [&'static str],
    selected: usize,
}

impl Home {
    pub fn new(items: &'static [&'static str]) -> Self {
        Self { items, selected: 0 }
    }

    pub fn items(&self) -> &'static [&'static str] {
        self.items
    }

    pub fn selected(&self) -> usize {
        self.selected
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
        let mut h = Home::new(ITEMS);
        assert_eq!(h.selected(), 0);
        assert!(h.down());
        assert_eq!(h.selected(), 1);
        assert!(h.down()); // wrap to top
        assert_eq!(h.selected(), 0);
        assert!(h.up()); // wrap to bottom
        assert_eq!(h.selected(), 1);
    }

    #[test]
    fn empty_menu_is_inert() {
        let mut h = Home::new(&[]);
        assert!(!h.up());
        assert!(!h.down());
        assert_eq!(h.selected(), 0);
    }
}
