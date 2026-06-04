//! Renderer for the firmware's on-disk `.bin` bitmap fonts.
//!
//! Reads glyph metrics + 1bpp bitmaps parsed by [`crate::parser::font`] and
//! blits them to any `embedded-graphics` [`DrawTarget`], with **variable-width**
//! advances (proportional fonts) — the wrap pass can use [`text_width`] /
//! [`char_advance`] so layout matches what is drawn.
//!
//! The firmware currently renders with embedded-graphics' built-in mono fonts;
//! this path activates once a real `.bin` font asset is loaded from the SD card.
//! It is exercised by host tests with a synthetic font.

use embedded_graphics::pixelcolor::BinaryColor;
use embedded_graphics::prelude::*;

use crate::parser::font::{glyph_bitmap, FontHeader, GlyphMetrics};

/// Advance (in pixels) used for a codepoint the font does not contain.
pub const FALLBACK_ADVANCE: u16 = 4;

/// The advance width of a single character in `font`.
pub fn char_advance(font: &[u8], header: &FontHeader, ch: char) -> u16 {
    match GlyphMetrics::index_of(header, ch as u32).and_then(|i| GlyphMetrics::at(font, i)) {
        Some(m) => m.advance as u16,
        None => FALLBACK_ADVANCE,
    }
}

/// Total advance width of `text` in `font`.
pub fn text_width(font: &[u8], header: &FontHeader, text: &str) -> u32 {
    text.chars()
        .map(|c| char_advance(font, header, c) as u32)
        .sum()
}

/// Blit one glyph bitmap at pen position `(x, y)` (top-left, before per-glyph
/// offsets). `color` paints set bits.
fn draw_glyph<D>(
    target: &mut D,
    bitmap: &[u8],
    m: &GlyphMetrics,
    x: i32,
    y: i32,
    color: BinaryColor,
) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    let row_bytes = (m.width as usize + 7) / 8;
    for row in 0..m.height as usize {
        for col in 0..m.width as usize {
            let byte = bitmap[row * row_bytes + col / 8];
            let bit = (byte >> (7 - (col % 8))) & 1;
            if bit == 1 {
                let px = x + m.x_offset as i32 + col as i32;
                let py = y + m.y_offset as i32 + row as i32;
                target.draw_iter(core::iter::once(Pixel(Point::new(px, py), color)))?;
            }
        }
    }
    Ok(())
}

/// Render `text` starting at `origin` (the top-left pen position). Returns the
/// total advance width. Unknown glyphs advance by [`FALLBACK_ADVANCE`].
pub fn draw_text<D>(
    target: &mut D,
    font: &[u8],
    header: &FontHeader,
    text: &str,
    origin: Point,
    color: BinaryColor,
) -> Result<u32, D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    let mut x = origin.x;
    for ch in text.chars() {
        match GlyphMetrics::index_of(header, ch as u32).and_then(|i| GlyphMetrics::at(font, i)) {
            Some(m) => {
                if let Some(bm) = glyph_bitmap(font, header, &m) {
                    draw_glyph(target, bm, &m, x, origin.y, color)?;
                }
                x += m.advance as i32;
            }
            None => x += FALLBACK_ADVANCE as i32,
        }
    }
    Ok((x - origin.x) as u32)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::font::FONT_MAGIC;
    use std::vec::Vec;

    // A tiny in-memory 1bpp canvas implementing DrawTarget for assertions.
    struct Canvas {
        w: u32,
        h: u32,
        on: Vec<bool>,
    }
    impl Canvas {
        fn new(w: u32, h: u32) -> Self {
            Self {
                w,
                h,
                on: std::vec![false; (w * h) as usize],
            }
        }
        fn get(&self, x: u32, y: u32) -> bool {
            self.on[(y * self.w + x) as usize]
        }
    }
    impl OriginDimensions for Canvas {
        fn size(&self) -> Size {
            Size::new(self.w, self.h)
        }
    }
    impl DrawTarget for Canvas {
        type Color = BinaryColor;
        type Error = core::convert::Infallible;
        fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
        where
            I: IntoIterator<Item = Pixel<Self::Color>>,
        {
            for Pixel(p, c) in pixels {
                if p.x >= 0 && p.y >= 0 && (p.x as u32) < self.w && (p.y as u32) < self.h {
                    self.on[(p.y as u32 * self.w + p.x as u32) as usize] = c == BinaryColor::On;
                }
            }
            Ok(())
        }
    }

    // Font: 1 glyph ('A' at codepoint 65... but first_codepoint set to 'A') that
    // is a 3x3 bitmap of an X shape, advance 4.
    fn synth_font() -> Vec<u8> {
        const HEADER_LEN: usize = 12;
        const REC: usize = 8;
        let mut b = std::vec![0u8; HEADER_LEN + REC];
        b[0..2].copy_from_slice(&FONT_MAGIC.to_le_bytes());
        b[2..4].copy_from_slice(&1u16.to_le_bytes()); // glyph_count
        b[8..10].copy_from_slice(&8u16.to_le_bytes()); // line_height
        b[10..12].copy_from_slice(&(b'A' as u16).to_le_bytes()); // first_codepoint
                                                                 // glyph 0 metrics
        b[HEADER_LEN] = 3; // width
        b[HEADER_LEN + 1] = 3; // height
        b[HEADER_LEN + 2] = 0; // x_offset
        b[HEADER_LEN + 3] = 0; // y_offset
        b[HEADER_LEN + 4] = 4; // advance
        b[HEADER_LEN + 6..HEADER_LEN + 8].copy_from_slice(&0u16.to_le_bytes()); // bitmap_offset
                                                                                // bitmap: 3 rows, 1 byte each (row_bytes = ceil(3/8)=1). X shape:
                                                                                // row0: 1 0 1 -> 0b101_00000
                                                                                // row1: 0 1 0 -> 0b010_00000
                                                                                // row2: 1 0 1 -> 0b101_00000
        b.push(0b1010_0000);
        b.push(0b0100_0000);
        b.push(0b1010_0000);
        b
    }

    #[test]
    fn renders_glyph_pixels() {
        let font = synth_font();
        let header = FontHeader::parse(&font).unwrap();
        let mut canvas = Canvas::new(8, 8);
        let w = draw_text(
            &mut canvas,
            &font,
            &header,
            "A",
            Point::new(0, 0),
            BinaryColor::On,
        )
        .unwrap();
        assert_eq!(w, 4); // advance
                          // The X shape corners + centre are on; edges off.
        assert!(canvas.get(0, 0) && canvas.get(2, 0));
        assert!(!canvas.get(1, 0));
        assert!(canvas.get(1, 1));
        assert!(!canvas.get(0, 1) && !canvas.get(2, 1));
        assert!(canvas.get(0, 2) && canvas.get(2, 2));
    }

    #[test]
    fn widths_and_fallback() {
        let font = synth_font();
        let header = FontHeader::parse(&font).unwrap();
        assert_eq!(char_advance(&font, &header, 'A'), 4);
        // Unknown glyph → fallback.
        assert_eq!(char_advance(&font, &header, 'z'), FALLBACK_ADVANCE);
        assert_eq!(
            text_width(&font, &header, "Az"),
            4 + FALLBACK_ADVANCE as u32
        );
    }
}
