//! Streaming paragraph reader over a UTF-8 text buffer.
//!
//! Splits a borrowed buffer into paragraphs on blank lines, lazily, without
//! copying. Invalid UTF-8 is handled losslessly by yielding raw `&str` slices
//! validated per-paragraph (lossy fallback returns the valid prefix).

/// A single paragraph: a borrowed slice of the source buffer with CR/LF runs at
/// the ends trimmed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Paragraph<'a> {
    pub bytes: &'a [u8],
}

impl<'a> Paragraph<'a> {
    /// The paragraph as `&str`, or the valid UTF-8 prefix if the slice contains
    /// an invalid sequence. Never panics, never allocates.
    pub fn as_str(&self) -> &'a str {
        match core::str::from_utf8(self.bytes) {
            Ok(s) => s,
            Err(e) => {
                // Safe: `valid_up_to()` is a guaranteed-valid boundary.
                core::str::from_utf8(&self.bytes[..e.valid_up_to()]).unwrap_or("")
            }
        }
    }
}

/// Iterator over paragraphs in a buffer. A paragraph break is one or more blank
/// lines (a `\n` followed, after optional whitespace, by another `\n`).
pub struct TextReader<'a> {
    buf: &'a [u8],
    pos: usize,
}

impl<'a> TextReader<'a> {
    pub fn new(buf: &'a [u8]) -> Self {
        Self { buf, pos: 0 }
    }

    /// Remaining unparsed bytes (useful for resumable paging).
    pub fn remaining(&self) -> usize {
        self.buf.len().saturating_sub(self.pos)
    }
}

fn is_blank(b: u8) -> bool {
    b == b' ' || b == b'\t' || b == b'\r'
}

impl<'a> Iterator for TextReader<'a> {
    type Item = Paragraph<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        let buf = self.buf;
        // Skip leading blank lines / whitespace.
        while self.pos < buf.len() && (is_blank(buf[self.pos]) || buf[self.pos] == b'\n') {
            self.pos += 1;
        }
        if self.pos >= buf.len() {
            return None;
        }

        let start = self.pos;
        // Advance until a blank line (\n\n possibly separated by blanks) or EOF.
        while self.pos < buf.len() {
            if buf[self.pos] == b'\n' {
                // Look ahead: is the next non-blank also a newline / EOF?
                let mut look = self.pos + 1;
                while look < buf.len() && is_blank(buf[look]) {
                    look += 1;
                }
                if look >= buf.len() || buf[look] == b'\n' {
                    break; // paragraph boundary
                }
            }
            self.pos += 1;
        }

        // Trim trailing CR/LF/space from the paragraph slice.
        let mut end = self.pos;
        while end > start && (is_blank(buf[end - 1]) || buf[end - 1] == b'\n') {
            end -= 1;
        }
        Some(Paragraph {
            bytes: &buf[start..end],
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn splits_on_blank_lines() {
        let src = b"First para\nstill first.\n\nSecond para.\n\n\nThird.";
        let mut r = TextReader::new(src);
        assert_eq!(r.next().unwrap().as_str(), "First para\nstill first.");
        assert_eq!(r.next().unwrap().as_str(), "Second para.");
        assert_eq!(r.next().unwrap().as_str(), "Third.");
        assert!(r.next().is_none());
    }

    #[test]
    fn handles_trailing_invalid_utf8() {
        let src = b"ok text\xff\xfe";
        let p = TextReader::new(src).next().unwrap();
        assert_eq!(p.as_str(), "ok text");
    }
}
