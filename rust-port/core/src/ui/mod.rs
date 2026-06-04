//! UI layer: input events, the reader screen, and a render helper.
//!
//! Mirrors the C++ firmware's screen model at a small scale — logical `Event`s
//! decoded from physical buttons, driving a screen (currently the `Reader`).

pub mod reader;

pub use reader::Reader;

use embedded_graphics::mono_font::MonoTextStyle;
use embedded_graphics::pixelcolor::BinaryColor;
use embedded_graphics::prelude::*;
use embedded_graphics::text::{Baseline, Text};

use crate::layout::{Line, PageMetrics};
use crate::pins::Button;

/// Logical UI events, decoded from the physical buttons.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Event {
    PrevPage,
    NextPage,
    Select,
    Back,
    Up,
    Down,
}

/// Map a physical button to a UI event. Left/Right turn pages in the reader.
pub fn map_button(btn: Button) -> Option<Event> {
    Some(match btn {
        Button::Left => Event::PrevPage,
        Button::Right => Event::NextPage,
        Button::Confirm => Event::Select,
        Button::Back => Event::Back,
        Button::Up => Event::Up,
        Button::Down => Event::Down,
        Button::Power => return None,
    })
}

/// Draw a list of laid-out lines down the page at `metrics.line_height` spacing.
/// The caller is responsible for clearing the target first. Text is drawn with
/// the top baseline so `y` is the line's top edge.
pub fn render_lines<D>(
    target: &mut D,
    lines: &[Line<'_>],
    metrics: &PageMetrics,
    style: MonoTextStyle<'_, BinaryColor>,
) -> Result<(), D::Error>
where
    D: DrawTarget<Color = BinaryColor>,
{
    let x = metrics.margin_x as i32;
    let mut y = metrics.margin_y as i32;
    for line in lines {
        if !line.text.is_empty() {
            Text::with_baseline(line.text, Point::new(x, y), style, Baseline::Top).draw(target)?;
        }
        y += metrics.line_height as i32;
    }
    Ok(())
}
