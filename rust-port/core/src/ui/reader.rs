//! The reader: paginate a text buffer and navigate it page by page.
//!
//! This ports the heart of the C++ reader: take a loaded book buffer, flow it
//! through the wrap/paginate pass, and present one screenful at a time with
//! Left/Right page navigation. Zero allocation — the lines for the current page
//! land in a fixed `heapless::Vec`, recomputed on demand (e-ink refreshes are
//! infrequent, so an O(text) reflow per page turn is fine and saves a per-line
//! index table).

use heapless::Vec;

use crate::layout::{self, Line, PageMetrics};
use crate::parser::TextReader;

/// Max lines drawn on a single page (bounds the static buffer). A 480 px tall
/// panel at a ~16 px line height shows < 30 lines; 48 leaves headroom.
pub const MAX_LINES_PER_PAGE: usize = 48;

/// A character-advance function (pixel width incl. spacing) for the active font.
pub type Advance = fn(char) -> u16;

/// Paginating reader over a borrowed text buffer.
pub struct Reader<'a> {
    text: &'a [u8],
    metrics: PageMetrics,
    advance: Advance,
    page: usize,
}

impl<'a> Reader<'a> {
    pub fn new(text: &'a [u8], metrics: PageMetrics, advance: Advance) -> Self {
        Self {
            text,
            metrics,
            advance,
            page: 0,
        }
    }

    pub fn current_page(&self) -> usize {
        self.page
    }

    pub fn metrics(&self) -> &PageMetrics {
        &self.metrics
    }

    /// Advance the running line cursor over every wrapped line in the book,
    /// invoking `f(global_line_index, line)` for each. Stops early if `f`
    /// returns `false`. Paragraphs are separated by one blank line.
    fn for_each_line<F: FnMut(usize, Line<'a>) -> bool>(&self, mut f: F) {
        let usable = self.metrics.usable_width();
        let blank = Line { text: "" };
        let mut global = 0usize;
        let mut first = true;
        for para in TextReader::new(self.text) {
            // A blank spacer line between paragraphs (not before the first).
            if !first {
                if !f(global, blank) {
                    return;
                }
                global += 1;
            }
            first = false;

            for line in layout::wrap(para.as_str(), usable, self.advance) {
                if !f(global, line) {
                    return;
                }
                global += 1;
            }
        }
    }

    /// Total wrapped lines across the whole book.
    pub fn total_lines(&self) -> usize {
        let mut count = 0;
        self.for_each_line(|_, _| {
            count += 1;
            true
        });
        count
    }

    /// Total number of pages (at least 1 so an empty book still shows a page).
    pub fn total_pages(&self) -> usize {
        layout::page_count(self.total_lines(), &self.metrics).max(1)
    }

    /// The wrapped lines that belong on `self.page`.
    pub fn page_lines(&self) -> Vec<Line<'a>, MAX_LINES_PER_PAGE> {
        let lpp = self.metrics.lines_per_page();
        let mut out: Vec<Line<'a>, MAX_LINES_PER_PAGE> = Vec::new();
        if lpp == 0 {
            return out;
        }
        let start = self.page * lpp;
        let end = start + lpp;
        self.for_each_line(|idx, line| {
            if idx >= end {
                return false; // past this page — stop walking
            }
            if idx >= start {
                let _ = out.push(line);
            }
            true
        });
        out
    }

    /// Go to the next page if there is one. Returns true if the page changed.
    pub fn next_page(&mut self) -> bool {
        if self.page + 1 < self.total_pages() {
            self.page += 1;
            true
        } else {
            false
        }
    }

    /// Go to the previous page if there is one. Returns true if it changed.
    pub fn prev_page(&mut self) -> bool {
        if self.page > 0 {
            self.page -= 1;
            true
        } else {
            false
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn metrics(line_height: u16, height: u16) -> PageMetrics {
        PageMetrics {
            width: 800,
            height,
            margin_x: 0,
            margin_y: 0,
            line_height,
        }
    }

    fn fixed(_c: char) -> u16 {
        10
    }

    #[test]
    fn paginates_and_navigates() {
        // 6 short paragraphs → 6 lines + 5 blank spacers = 11 lines.
        let text = b"a\n\nb\n\nc\n\nd\n\ne\n\nf";
        // line_height 10, height 30, margins 0 → 3 lines per page → 4 pages.
        let m = metrics(10, 30);
        let mut r = Reader::new(text, m, fixed);
        assert_eq!(r.metrics.lines_per_page(), 3);
        assert_eq!(r.total_lines(), 11);
        assert_eq!(r.total_pages(), 4);

        // Page 0 holds the first three lines: "a", "", "b".
        let p0 = r.page_lines();
        assert_eq!(p0.len(), 3);
        assert_eq!(p0[0].text, "a");
        assert_eq!(p0[1].text, "");
        assert_eq!(p0[2].text, "b");

        assert!(r.next_page());
        assert_eq!(r.current_page(), 1);
        assert!(r.prev_page());
        assert_eq!(r.current_page(), 0);
        assert!(!r.prev_page()); // clamp at start

        // Walk to the last page and confirm clamping.
        while r.next_page() {}
        assert_eq!(r.current_page(), 3);
        assert!(!r.next_page());
    }

    #[test]
    fn empty_book_has_one_page() {
        let r = Reader::new(b"", metrics(16, 480), fixed);
        assert_eq!(r.total_pages(), 1);
        assert_eq!(r.page_lines().len(), 0);
    }
}
