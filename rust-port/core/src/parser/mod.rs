//! Zero-allocation parsers for book text and `.bin` bitmap fonts.
//!
//! Everything here works on borrowed `&[u8]` slices with bounds-checked
//! indexing — no `unsafe`, no heap. Matches the firmware's "load from SD into a
//! fixed buffer, parse in place" model.

pub mod font;
pub mod text;

// Re-export the public parser surface. Not yet consumed by the superloop, but
// forms the module's API as the reader is ported.
#[allow(unused_imports)]
pub use font::{FontHeader, GlyphMetrics};
#[allow(unused_imports)]
pub use text::{Paragraph, TextReader};
