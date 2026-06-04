//! Bounds-checked reader for the firmware's `.bin` bitmap-font format.
//!
//! The header layout below mirrors the fixed-size record the C++ font tooling
//! emits. Parsing is pure slice arithmetic returning `Option`, so a truncated or
//! corrupt file yields `None` instead of reading out of bounds.

/// Fixed 12-byte font file header.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FontHeader {
    pub magic: u16,
    pub glyph_count: u16,
    pub ascent: i16,
    pub descent: i16,
    pub line_height: u16,
    pub first_codepoint: u16,
}

/// Expected little-endian magic ("XF" = 0x4658) — adjust to the real tool value.
pub const FONT_MAGIC: u16 = 0x4658;

pub(crate) const HEADER_LEN: usize = 12;
pub(crate) const GLYPH_RECORD_LEN: usize = 8;

/// Byte offset where the bitmap blob begins (just after the glyph metrics table).
pub(crate) fn bitmap_blob_start(header: &FontHeader) -> usize {
    HEADER_LEN + header.glyph_count as usize * GLYPH_RECORD_LEN
}

/// Number of bytes in a glyph's 1bpp bitmap (`ceil(width/8) * height`).
pub fn glyph_bitmap_len(m: &GlyphMetrics) -> usize {
    ((m.width as usize) + 7) / 8 * m.height as usize
}

/// The glyph's 1bpp bitmap rows (MSB-first, set bit = ink), or `None` if the
/// blob is truncated.
pub fn glyph_bitmap<'a>(buf: &'a [u8], header: &FontHeader, m: &GlyphMetrics) -> Option<&'a [u8]> {
    let start = bitmap_blob_start(header) + m.bitmap_offset as usize;
    let len = glyph_bitmap_len(m);
    buf.get(start..start + len)
}

fn rd_u16(buf: &[u8], at: usize) -> Option<u16> {
    let b = buf.get(at..at + 2)?;
    Some(u16::from_le_bytes([b[0], b[1]]))
}

fn rd_i16(buf: &[u8], at: usize) -> Option<i16> {
    rd_u16(buf, at).map(|v| v as i16)
}

impl FontHeader {
    /// Parse the header from the front of `buf`. Returns `None` on truncation or
    /// magic mismatch.
    pub fn parse(buf: &[u8]) -> Option<Self> {
        if buf.len() < HEADER_LEN {
            return None;
        }
        let magic = rd_u16(buf, 0)?;
        if magic != FONT_MAGIC {
            return None;
        }
        Some(FontHeader {
            magic,
            glyph_count: rd_u16(buf, 2)?,
            ascent: rd_i16(buf, 4)?,
            descent: rd_i16(buf, 6)?,
            line_height: rd_u16(buf, 8)?,
            first_codepoint: rd_u16(buf, 10)?,
        })
    }
}

/// Per-glyph metrics record.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GlyphMetrics {
    pub width: u8,
    pub height: u8,
    pub x_offset: i8,
    pub y_offset: i8,
    pub advance: u8,
    /// Byte offset of this glyph's bitmap within the file's bitmap blob.
    pub bitmap_offset: u16,
}

impl GlyphMetrics {
    /// Look up the metrics for the `index`-th glyph (0-based) in the metrics
    /// table that follows the header. `None` if out of range / truncated.
    pub fn at(buf: &[u8], index: u16) -> Option<Self> {
        let start = HEADER_LEN + index as usize * GLYPH_RECORD_LEN;
        let r = buf.get(start..start + GLYPH_RECORD_LEN)?;
        Some(GlyphMetrics {
            width: r[0],
            height: r[1],
            x_offset: r[2] as i8,
            y_offset: r[3] as i8,
            advance: r[4],
            bitmap_offset: u16::from_le_bytes([r[6], r[7]]),
        })
    }

    /// Map a codepoint to its glyph index relative to `header.first_codepoint`.
    pub fn index_of(header: &FontHeader, codepoint: u32) -> Option<u16> {
        let first = header.first_codepoint as u32;
        if codepoint < first {
            return None;
        }
        let idx = codepoint - first;
        if idx >= header.glyph_count as u32 {
            return None;
        }
        Some(idx as u16)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> [u8; HEADER_LEN + GLYPH_RECORD_LEN] {
        let mut b = [0u8; HEADER_LEN + GLYPH_RECORD_LEN];
        b[0..2].copy_from_slice(&FONT_MAGIC.to_le_bytes());
        b[2..4].copy_from_slice(&1u16.to_le_bytes()); // glyph_count
        b[8..10].copy_from_slice(&20u16.to_le_bytes()); // line_height
        b[10..12].copy_from_slice(&32u16.to_le_bytes()); // first_codepoint ' '
                                                         // glyph 0
        b[HEADER_LEN] = 6; // width
        b[HEADER_LEN + 1] = 12; // height
        b[HEADER_LEN + 4] = 7; // advance
        b
    }

    #[test]
    fn parses_header_and_glyph() {
        let buf = sample();
        let h = FontHeader::parse(&buf).unwrap();
        assert_eq!(h.glyph_count, 1);
        assert_eq!(h.first_codepoint, 32);
        let idx = GlyphMetrics::index_of(&h, ' ' as u32).unwrap();
        let g = GlyphMetrics::at(&buf, idx).unwrap();
        assert_eq!(g.advance, 7);
        assert!(GlyphMetrics::index_of(&h, 'Z' as u32).is_none());
    }

    #[test]
    fn rejects_bad_magic() {
        let mut buf = sample();
        buf[0] = 0;
        assert!(FontHeader::parse(&buf).is_none());
    }
}
