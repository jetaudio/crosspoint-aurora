//! Text layout: word-wrapping and pagination, zero-allocation.
//!
//! Mirrors the firmware's reader layout pass — given a paragraph, a usable text
//! width, and a per-character advance function, it greedily wraps words into
//! lines, then groups lines into screen-height pages. All output lands in
//! caller-provided `heapless` buffers, so there is no heap use and the worst-
//! case memory is fixed at compile time.

use heapless::Vec;

/// Margins / geometry for a text page, in pixels.
#[derive(Clone, Copy, Debug)]
pub struct PageMetrics {
    pub width: u16,
    pub height: u16,
    pub margin_x: u16,
    pub margin_y: u16,
    pub line_height: u16,
}

impl PageMetrics {
    pub fn usable_width(&self) -> u16 {
        self.width.saturating_sub(self.margin_x * 2)
    }
    pub fn usable_height(&self) -> u16 {
        self.height.saturating_sub(self.margin_y * 2)
    }
    /// How many lines fit on one page.
    pub fn lines_per_page(&self) -> usize {
        if self.line_height == 0 {
            return 0;
        }
        (self.usable_height() / self.line_height) as usize
    }
}

/// A laid-out line: a borrowed sub-slice of the source paragraph string.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Line<'a> {
    pub text: &'a str,
}

/// Maximum lines we will wrap from a single paragraph (bounds the static buffer).
pub const MAX_LINES: usize = 64;

/// Greedily wrap `text` to fit `usable_width`, using `advance(ch)` for each
/// character's pixel width (incl. its trailing spacing). Breaks on spaces; a
/// single word longer than the line is hard-split at the character that
/// overflows. Returns the wrapped lines (truncated at [`MAX_LINES`]).
pub fn wrap<'a, F>(text: &'a str, usable_width: u16, mut advance: F) -> Vec<Line<'a>, MAX_LINES>
where
    F: FnMut(char) -> u16,
{
    let mut lines: Vec<Line<'a>, MAX_LINES> = Vec::new();
    let max = usable_width as u32;

    let mut line_start = 0usize; // byte index where current line begins
    let mut last_break: Option<usize> = None; // byte index just after last space
    let mut width: u32 = 0;

    let mut it = text.char_indices().peekable();
    while let Some((i, ch)) = it.next() {
        if ch == '\n' {
            push_line(&mut lines, text, line_start, i);
            line_start = next_char_boundary(text, i);
            last_break = None;
            width = 0;
            continue;
        }

        let cw = advance(ch) as u32;
        if width + cw > max && i > line_start && ch == ' ' {
            // The overflowing character is itself the inter-word space: end the
            // line before it and swallow the space (it never starts the next
            // line). Without this we'd break at the *previous* space and drop a
            // word that actually fit on the line.
            push_line(&mut lines, text, line_start, i);
            if lines.len() == MAX_LINES {
                return lines;
            }
            line_start = next_char_boundary(text, i);
            last_break = None;
            width = 0;
            continue;
        }
        if width + cw > max && i > line_start {
            // Overflow on a word character: break at the last space if we have
            // one, else hard-split the word.
            let (break_at, resume_at) = match last_break {
                Some(b) if b > line_start => (trim_end(text, b), b),
                _ => (i, i),
            };
            push_line(&mut lines, text, line_start, break_at);
            if lines.len() == MAX_LINES {
                return lines;
            }
            line_start = resume_at;
            last_break = None;
            // Recompute the running width from the resumed position to `i`.
            width = measure(&text[resume_at..=i], &mut advance) - cw;
        }

        width += cw;
        if ch == ' ' {
            last_break = Some(next_char_boundary(text, i));
        }
    }

    if line_start < text.len() {
        push_line(&mut lines, text, line_start, text.len());
    }
    lines
}

fn measure<F>(s: &str, advance: &mut F) -> u32
where
    F: FnMut(char) -> u16,
{
    s.chars().map(|c| advance(c) as u32).sum()
}

fn trim_end(text: &str, mut end: usize) -> usize {
    while end > 0 && text.as_bytes()[end - 1] == b' ' {
        end -= 1;
    }
    end
}

fn next_char_boundary(text: &str, i: usize) -> usize {
    let mut j = i + 1;
    while j < text.len() && !text.is_char_boundary(j) {
        j += 1;
    }
    j
}

fn push_line<'a>(lines: &mut Vec<Line<'a>, MAX_LINES>, text: &'a str, start: usize, end: usize) {
    let end = end.max(start);
    let _ = lines.push(Line {
        text: &text[start..end],
    });
}

/// Number of pages needed to show `line_count` lines at `metrics`.
pub fn page_count(line_count: usize, metrics: &PageMetrics) -> usize {
    let per = metrics.lines_per_page();
    if per == 0 {
        return 0;
    }
    line_count.div_ceil(per)
}

#[cfg(test)]
mod tests {
    use super::*;

    // Fixed 10px advance per char makes expectations easy.
    fn fixed(_c: char) -> u16 {
        10
    }

    #[test]
    fn wraps_on_spaces() {
        // width 50 → 5 chars per line.
        let lines = wrap("aa bb cc dd", 50, fixed);
        assert_eq!(lines[0].text, "aa bb");
        assert_eq!(lines[1].text, "cc dd");
    }

    #[test]
    fn respects_hard_newline() {
        let lines = wrap("ab\ncd", 1000, fixed);
        assert_eq!(lines.len(), 2);
        assert_eq!(lines[0].text, "ab");
        assert_eq!(lines[1].text, "cd");
    }

    #[test]
    fn paginates() {
        let m = PageMetrics {
            width: 800,
            height: 480,
            margin_x: 20,
            margin_y: 20,
            line_height: 24,
        };
        assert!(m.lines_per_page() > 0);
        assert_eq!(page_count(0, &m), 0);
        assert_eq!(page_count(m.lines_per_page(), &m), 1);
        assert_eq!(page_count(m.lines_per_page() + 1, &m), 2);
    }
}
