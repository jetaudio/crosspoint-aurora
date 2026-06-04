//! Reader settings: line spacing and margin, adjustable in-memory.
//!
//! Backs the Settings tab. A small cursor selects a field; inc/dec adjust it
//! within bounds. The reader derives its [`crate::layout::PageMetrics`] from
//! these, so changes take effect on the next book opened. (Persisting to the SD
//! card is a roadmap item — this is the in-memory model and is host-tested.)

/// The adjustable fields, in display order.
pub const SETTING_LABELS: [&str; 2] = ["Line spacing", "Margin"];
pub const SETTING_COUNT: usize = 2;

const LINE_MIN: u16 = 12;
const LINE_MAX: u16 = 24;
const MARGIN_MIN: u16 = 8;
const MARGIN_MAX: u16 = 48;
const STEP: u16 = 2;

/// In-memory reader settings with a selection cursor.
pub struct Settings {
    line_height: u16,
    margin: u16,
    cursor: usize,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            line_height: 14,
            margin: 16,
            cursor: 0,
        }
    }
}

impl Settings {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn line_height(&self) -> u16 {
        self.line_height
    }
    pub fn margin(&self) -> u16 {
        self.margin
    }
    pub fn cursor(&self) -> usize {
        self.cursor
    }
    pub fn label(&self, i: usize) -> &'static str {
        SETTING_LABELS[i]
    }
    pub fn value(&self, i: usize) -> u16 {
        match i {
            0 => self.line_height,
            _ => self.margin,
        }
    }

    /// Move the cursor up (wrapping).
    pub fn up(&mut self) -> bool {
        self.cursor = if self.cursor == 0 {
            SETTING_COUNT - 1
        } else {
            self.cursor - 1
        };
        true
    }
    /// Move the cursor down (wrapping).
    pub fn down(&mut self) -> bool {
        self.cursor = (self.cursor + 1) % SETTING_COUNT;
        true
    }

    /// Increase the selected field (clamped). Returns true if it changed.
    pub fn inc(&mut self) -> bool {
        match self.cursor {
            0 => bump(&mut self.line_height, STEP as i32, LINE_MIN, LINE_MAX),
            _ => bump(&mut self.margin, STEP as i32, MARGIN_MIN, MARGIN_MAX),
        }
    }
    /// Decrease the selected field (clamped). Returns true if it changed.
    pub fn dec(&mut self) -> bool {
        match self.cursor {
            0 => bump(&mut self.line_height, -(STEP as i32), LINE_MIN, LINE_MAX),
            _ => bump(&mut self.margin, -(STEP as i32), MARGIN_MIN, MARGIN_MAX),
        }
    }
}

fn bump(v: &mut u16, delta: i32, min: u16, max: u16) -> bool {
    let next = (*v as i32 + delta).clamp(min as i32, max as i32) as u16;
    if next != *v {
        *v = next;
        true
    } else {
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cursor_wraps() {
        let mut s = Settings::new();
        assert_eq!(s.cursor(), 0);
        s.down();
        assert_eq!(s.cursor(), 1);
        s.down();
        assert_eq!(s.cursor(), 0);
        s.up();
        assert_eq!(s.cursor(), 1);
    }

    #[test]
    fn inc_dec_clamp() {
        let mut s = Settings::new();
        // Line spacing field.
        let start = s.line_height();
        assert!(s.inc());
        assert_eq!(s.line_height(), start + STEP);
        // Drive to the max and confirm clamping.
        while s.inc() {}
        assert_eq!(s.line_height(), LINE_MAX);
        assert!(!s.inc());
        // Margin field.
        s.down();
        while s.dec() {}
        assert_eq!(s.margin(), MARGIN_MIN);
        assert!(!s.dec());
    }
}
