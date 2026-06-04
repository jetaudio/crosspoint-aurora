//! Front-button decode from the two analog ADC ladders.
//!
//! The decode thresholds are transcribed from
//! `open-x4-sdk/libs/hardware/InputManager/src/InputManager.cpp:17-18`:
//!
//! ```text
//! ADC_RANGES_1 = {3900(none), 3100, 2090, 750, INT32_MIN}  // Back/Confirm/Left/Right
//! ADC_RANGES_2 = {3900(none), 1120, INT32_MIN}             // Up/Down
//! ```
//!
//! A reading is matched to the first button whose lower bound it is **above**.
//! Readings at/above `ADC_NO_BUTTON` mean nothing is pressed. The hardware ADC
//! glue (owning `Adc`/pins) lives in `app`; this stays pure so it is unit-tested
//! on the host.

use crate::pins::Button;

/// Above this on either ladder ⇒ no button pressed (InputManager.h:112).
pub const ADC_NO_BUTTON: u16 = 3900;

/// Decode group 1 (ADC pin 1): Back / Confirm / Left / Right.
pub fn decode_group1(adc: u16) -> Option<Button> {
    if adc >= ADC_NO_BUTTON {
        None
    } else if adc >= 3100 {
        Some(Button::Back)
    } else if adc >= 2090 {
        Some(Button::Confirm)
    } else if adc >= 750 {
        Some(Button::Left)
    } else {
        Some(Button::Right)
    }
}

/// Decode group 2 (ADC pin 2): Up / Down.
pub fn decode_group2(adc: u16) -> Option<Button> {
    if adc >= ADC_NO_BUTTON {
        None
    } else if adc >= 1120 {
        Some(Button::Up)
    } else {
        Some(Button::Down)
    }
}

/// Edge-detecting debouncer: reports a button only on the press transition
/// (idle → pressed), so a held button fires once. Mirrors the firmware's
/// `wasPressed` semantics.
#[derive(Default)]
pub struct ButtonState {
    last: Option<Button>,
}

impl ButtonState {
    pub const fn new() -> Self {
        Self { last: None }
    }

    /// Feed the freshly-decoded button (or `None`). Returns `Some` exactly once
    /// per fresh press.
    pub fn update(&mut self, current: Option<Button>) -> Option<Button> {
        let edge = match (self.last, current) {
            (None, Some(b)) => Some(b),
            // A change between two different held buttons also counts as a press.
            (Some(prev), Some(b)) if prev != b => Some(b),
            _ => None,
        };
        self.last = current;
        edge
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn group1_thresholds() {
        assert_eq!(decode_group1(4000), None);
        assert_eq!(decode_group1(3200), Some(Button::Back));
        assert_eq!(decode_group1(2500), Some(Button::Confirm));
        assert_eq!(decode_group1(1000), Some(Button::Left));
        assert_eq!(decode_group1(100), Some(Button::Right));
    }

    #[test]
    fn group2_thresholds() {
        assert_eq!(decode_group2(4000), None);
        assert_eq!(decode_group2(2000), Some(Button::Up));
        assert_eq!(decode_group2(500), Some(Button::Down));
    }

    #[test]
    fn edge_detection_fires_once() {
        let mut s = ButtonState::new();
        assert_eq!(s.update(Some(Button::Up)), Some(Button::Up));
        assert_eq!(s.update(Some(Button::Up)), None); // held
        assert_eq!(s.update(None), None);
        assert_eq!(s.update(Some(Button::Up)), Some(Button::Up)); // re-press
    }
}
